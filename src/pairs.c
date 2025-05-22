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

  int result = send_multicast(send_sock, pSystem.liaison_addr, pSystem.liaison_port, buffer, buffer_size);
  if (result < 0) {
    close(send_sock);
    close(recv_sock);
    return -1;
  }
  printf("  Demande de connexion envoyée\n");

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
        printf("  Réponse de connexion reçue\n");
        // Display found peer information
        char ip_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &response->ip, ip_str, sizeof(ip_str));
        printf("  Pair trouvé: ID=%d, IP=%s, Port=%d\n", response->id, ip_str, response->port);

        // Add the peer
        add_pair(response->id, response->ip, response->port);

        // Attendre le message 5 (CODE_INFO_PAIR)
        int info_attempts = 0;
        int info_received = 0;
        while (info_attempts < MAX_ATTEMPTS && !info_received) {
          int info_len = receive_multicast(recv_sock, buffer, sizeof(buffer), &sender);
          if (info_len > 0) {
            struct message *info_msg = malloc(sizeof(struct message));
            if (info_msg == NULL) {
              perror("malloc a échoué");
              break;
            }
            if (buffer_to_message(info_msg, buffer) < 0) {
              perror("buffer_to_message a échoué");
              free(info_msg);
              break;
            }
            if (info_msg->code == CODE_INFO_PAIR) {
              printf("  Message CODE_INFO_PAIR reçu\n");
              char ip_str[INET6_ADDRSTRLEN];
              inet_ntop(AF_INET6, &info_msg->ip, ip_str, sizeof(ip_str));
              printf("  Pair info: ID=%d, IP=%s, Port=%d\n", info_msg->id, ip_str, info_msg->port);
              add_pair(info_msg->id, info_msg->ip, info_msg->port);
              info_received = 1;
              free(info_msg);
              break;
            }
            free(info_msg);
          } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("  Timeout en attente du message 5 (tentative %d/%d)\n", info_attempts + 1, MAX_ATTEMPTS);
          }
          info_attempts++;
        }
        close(send_sock);
        close(recv_sock);
        return info_received ? 0 : -1;
      } else {
        if (response->code == 0 || response->code == CODE_DEMANDE_LIAISON) {
          continue; // Ignore empty messages
        }
        printf("  Message reçu mais code incorrect: %d\n", response->code);
      }
      free(response);
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      printf("  Timeout atteint, tentative %d/%d\n", attempts + 1, MAX_ATTEMPTS);
    } else {
      perror("receive_multicast a échoué");
    }

    if (attempts < MAX_ATTEMPTS) {
      // Resend the request for each new attempt
      if (send_multicast(send_sock, pSystem.liaison_addr, pSystem.liaison_port, request, sizeof(request)) < 0) {
        printf("  Échec du renvoi de la demande\n");
      } else {
        printf("  Demande de connexion renvoyée (tentative %d/%d)\n", attempts + 1, MAX_ATTEMPTS);
      }
      attempts++;

      sleep(1); // Wait before retrying
    } else {
      printf("  Nombre maximal de tentatives atteint, aucune réponse reçue\n");
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

  struct message *request = malloc(sizeof(struct message));
  if (request == NULL) {
    perror("malloc a échoué");
    return -1;
  }
  // Extract requester information
  if (buffer_to_message(request, buffer) < 0) {
    perror("buffer_to_message a échoué");
    free_message(request);
    return -1;
  }

  // Check message code
  if (request->code == CODE_DEMANDE_LIAISON) { // CODE = 3 for connection request
    printf("Demande de connexion reçue\n");

    // Obtain sender's IP address
    char sender_ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &sender.sin6_addr, sender_ip_str, sizeof(sender_ip_str));

    // Display requester info
    printf("Demande de connexion, IP=%s, Port=%d\n", sender_ip_str, ntohs(sender.sin6_port));

    // Create a socket to send the response
    int send_sock = setup_multicast_sender();
    if (send_sock < 0) {
      perror("setup_multicast_sender a échoué");
      free_message(request);
      return -1;
    }

    // Prepare the response
    struct message *response = init_message(CODE_REPONSE_LIAISON); // CODE = 4 for response
    if (response == NULL) {
      perror("init_message a échoué");
      free_message(request);
      close(send_sock);
      return -1;
    }

    response->id = pSystem.my_id;
    message_set_ip(response, pSystem.my_ip);
    message_set_port(response, pSystem.my_port);

    // Convert the response to buffer
    char resp_buffer[1024];
    memset(resp_buffer, 0, sizeof(resp_buffer));

    if (message_to_buffer(response, resp_buffer, sizeof(resp_buffer)) < 0) {
      perror("message_to_buffer a échoué");
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }

    // Send the response (CODE 4) en unicast vers sender
    printf("Envoi de la réponse au demandeur en unicast...\n");
    if (send_unicast(send_sock, &sender, resp_buffer, strlen(resp_buffer)) < 0) {
      perror("send_unicast a échoué");
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }
    printf("Réponse envoyée avec succès\n");

    // Envoi du message CODE_INFO_PAIR (code 5) TODO c'est censé être dans join_auction()
    struct message *info_msg = init_message(CODE_INFO_PAIR);
    if (info_msg == NULL) {
      perror("init_message a échoué");
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }
    info_msg->id = pSystem.my_id;
    message_set_ip(info_msg, pSystem.my_ip);
    message_set_port(info_msg, pSystem.my_port);
    message_set_mess(info_msg, "Info pair après liaison");

    char info_buffer[1024];
    memset(info_buffer, 0, sizeof(info_buffer));
    if (message_to_buffer(info_msg, info_buffer, sizeof(info_buffer)) < 0) {
      perror("message_to_buffer a échoué (info pair)");
      free_message(info_msg);
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }
    printf("Envoi du message CODE_INFO_PAIR...\n");
    if (send_multicast(send_sock, pSystem.liaison_addr, pSystem.liaison_port,
                      info_buffer, strlen(info_buffer)) < 0) {
      perror("send_multicast a échoué (info pair)");
      free_message(info_msg);
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }
    printf("Message CODE_INFO_PAIR envoyé\n");

    free_message(info_msg);
    free_message(response);
    free_message(request);
    close(send_sock);
    return 1;
  } else if (request->code == CODE_REPONSE_LIAISON) { // CODE = 4 for response
    // Ignorer tous les messages qui ont notre ID
    if (request->id == pSystem.my_id) {
      printf("Message ignoré : message avec notre propre ID (%d)\n", pSystem.my_id);
      free_message(request);
      return 0;
    }

  }

  free_message(request);
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
