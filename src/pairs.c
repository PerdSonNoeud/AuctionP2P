#include "include/pairs.h"
#include "include/multicast.h"
#include "include/message.h"
#include "include/utils.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

#define MAX_ATTEMPTS 3
#define TIMEOUT 5

struct PairSystem pSystem;

int init_pairs() {
  pSystem.pairs = malloc(10 * sizeof(struct Pair));
  if (!pSystem.pairs) {
    perror("malloc a échoué");
    return -1;
  }

  pSystem.count = 0;
  pSystem.capacity = 10;
  pSystem.my_id = 1; // Default ID

  // Default IP address
  inet_pton(AF_INET6, "::1", &pSystem.my_ip);
  pSystem.my_port = 8000;

  // Default multicast addresses
  strcpy(pSystem.liaison_addr, "ff02::1");
  pSystem.liaison_port = 8080;
  strcpy(pSystem.auction_addr, "ff02::2");
  pSystem.auction_port = 8081;

  // Generate keys that will be used for signing messages
  // Ensure the keys are generated only once
  generate_ed25519_key("pub-ed25519-key.pem", "priv-ed25519-key.pem");

  return 0;
}

int join_auction() {
  // Socket to send
  int send_sock = setup_multicast_sender();
  if (send_sock < 0) return -1;

  // Socket to receive
  int recv_sock = setup_multicast_receiver(pSystem.liaison_addr, pSystem.liaison_port);
  if (recv_sock < 0) {
    close(send_sock);
    return -1;
  }

  // Send a request to join the system
  struct message *request = init_message(CODE_DEMANDE_LIAISON); // CODE = 3 for join request
  if (request == NULL) {
    perror("Échec de l'initialisation du message");
    close(send_sock);
    close(recv_sock);
    return -1;
  }
  int buffer_size = get_buffer_size(request);
  char *buffer = malloc(buffer_size); // Allocate buffer for outgoing messages
  if (buffer == NULL) {
    perror("malloc a échoué");
    free(request);
    close(send_sock);
    close(recv_sock);
    return -1;
  }
  if (message_to_buffer(request, buffer, buffer_size) < 0) {
    perror("message_to_buffer a échoué");
    free(buffer);
    free(request);
    close(send_sock);
    close(recv_sock);
    return -1;
  }

  int result = send_multicast(send_sock, pSystem.liaison_addr, pSystem.liaison_port,
                              buffer, buffer_size);
  if (result < 0) {
    close(send_sock);
    close(recv_sock);
    return -1;
  }
  printf("Demande de connexion envoyée\n");

  // Set timeout
  struct timeval tv;
  tv.tv_sec = TIMEOUT;
  tv.tv_usec = 0;
  if (setsockopt(recv_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) < 0) {
    perror("setsockopt a échoué");
    close(send_sock);
    close(recv_sock);
    return -1;
  }

  // Wait for response
  struct sockaddr_in6 sender;
  int attempts = 0;

  while (attempts < MAX_ATTEMPTS) {
    int len = receive_multicast(recv_sock, buffer, sizeof(buffer), &sender);
    if (len > 0) {
      struct message *response = malloc(sizeof(struct message));
      if (response == NULL) {
        perror("malloc a échoué");
        close(send_sock);
        close(recv_sock);
        return -1;
      }

      if (buffer_to_message(response, buffer) < 0) {
        perror("message_to_buffer a échoué");
        free(response);
        close(send_sock);
        close(recv_sock);
        return -1;
      }

      if (response->code == CODE_REPONSE_LIAISON) { // CODE = 4 (response)
        printf("Réponse de connexion reçue\n");
        // Display found peer information
        char ip_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &response->ip, ip_str, sizeof(ip_str));
        printf("Pair trouvé: ID=%d, IP=%s, Port=%d\n", response->id, ip_str, response->port);

        // Add the peer
        add_pair(response->id, response->ip, response->port);
        close(send_sock);
        close(recv_sock);
        return 0; // Successfully joined existing system
      } else {
        printf("Message reçu mais code incorrect: %d\n", response->code);
      }
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      printf("Timeout atteint, tentative %d/%d\n", attempts + 1, MAX_ATTEMPTS);
    } else {
      perror("receive_multicast a échoué");
    }

    attempts++;
    if (attempts < MAX_ATTEMPTS) {
      // Resend the request for each new attempt
      if (send_multicast(send_sock, pSystem.liaison_addr, pSystem.liaison_port, request, sizeof(request)) < 0) {
        printf("Échec du renvoi de la demande\n");
      } else {
        printf("Demande de connexion renvoyée (tentative %d/%d)\n", attempts + 1, MAX_ATTEMPTS);
      }

      sleep(1); // Wait before retrying
    } else {
      printf("Nombre maximal de tentatives atteint, aucune réponse reçue\n");
    }
  }

  close(send_sock);
  close(recv_sock);
  return -1; // Connection failed
}

int handle_join(int sock) {
  struct sockaddr_in6 sender;
  char buffer[1024]; // Static buffer with fixed size
  memset(buffer, 0, sizeof(buffer));

  int len = receive_multicast(sock, buffer, sizeof(buffer), &sender);
  if (len <= 0) {
    return 0; // No data or error
  }

  // Display received raw data for debugging
  printf("Données reçues (%d octets): ", len);
  for (int i = 0; i < 10 && i < len; i++) {
    printf("%02x ", (unsigned char)buffer[i]);
  }
  printf("\n");

  struct message *msg = malloc(sizeof(struct message));
  if (msg == NULL) {
    perror("malloc a échoué");
    return -1;
  }

  // Initialize fields to avoid memory issues
  msg->mess = NULL;
  msg->sig = NULL;

  // Extract requester information
  if (buffer_to_message(msg, buffer) < 0) {
    perror("buffer_to_message a échoué");
    free(msg);
    return -1;
  }

  // Check message code
  if (msg->code == CODE_DEMANDE_LIAISON) { // CODE = 3 for connection request
    printf("Demande de connexion reçue\n");

      // Display requester info
      char requester_ip_str[INET6_ADDRSTRLEN];
      inet_ntop(AF_INET6, &msg->ip, requester_ip_str, sizeof(requester_ip_str));
      printf("Demande de connexion de ID=%d, IP=%s, Port=%d\n", msg->id, requester_ip_str, msg->port);

      // Add this peer to our list
      add_pair(msg->id, msg->ip, msg->port);

      // Create a socket to send the response
      int send_sock = setup_multicast_sender();
      if (send_sock < 0) {
        perror("setup_multicast_sender a échoué");
        free_message(msg);
        return -1;
      }

      // Prepare the response
      struct message *response = init_message(CODE_REPONSE_LIAISON); // CODE = 4 for response
      if (response == NULL) {
        perror("init_message a échoué");
        free_message(msg);
        close(send_sock);
        return -1;
      }

    response->id = pSystem.my_id;
    message_set_ip(response, pSystem.my_ip);
    message_set_port(response, pSystem.my_port);
    message_set_mess(response, "Bienvenue dans le système P2P");

    // Convert the response to buffer
    char resp_buffer[1024];
    memset(resp_buffer, 0, sizeof(resp_buffer));

    if (message_to_buffer(response, resp_buffer, sizeof(resp_buffer)) < 0) {
      perror("message_to_buffer a échoué");
      free_message(response);
      free_message(msg);
      close(send_sock);
      return -1;
    }

    // Send the response
    printf("Envoi de la réponse au demandeur...\n");
    if (send_multicast(send_sock, pSystem.liaison_addr, pSystem.liaison_port,
                      resp_buffer, strlen(resp_buffer)) < 0) {
      perror("send_multicast a échoué");
      free_message(response);
      free_message(msg);
      close(send_sock);
      return -1;
    }

    printf("Réponse envoyée avec succès\n");
    free_message(response);
    free_message(msg);
    close(send_sock);
    return 1;
  }

  free_message(msg);
  return 0;
}

int add_pair(unsigned short id, struct in6_addr ip, unsigned short port) {
  // Check if the peer already exists
  for (int i = 0; i < pSystem.count; i++) {
    if (pSystem.pairs[i].id == id) {
      // Update existing peer information
      pSystem.pairs[i].ip = ip;
      pSystem.pairs[i].port = port;
      pSystem.pairs[i].active = 1;
      return 0;
    }
  }

  // Add a new peer
  if (pSystem.count >= pSystem.capacity) {
    int new_capacity = pSystem.capacity * 2;
    struct Pair *new_pairs = realloc(pSystem.pairs, new_capacity * sizeof(struct Pair));
    if (!new_pairs) {
      perror("realloc a échoué");
      return -1;
    }
    pSystem.pairs = new_pairs;
    pSystem.capacity = new_capacity;
  }

  pSystem.pairs[pSystem.count].id = id;
  pSystem.pairs[pSystem.count].ip = ip;
  pSystem.pairs[pSystem.count].port = port;
  pSystem.pairs[pSystem.count].active = 1;
  pSystem.count++;

  return 0;
}
