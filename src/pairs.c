#include "include/pairs.h"
#include "include/sockets.h"
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

  printf("  Adresse IP locale: ");
  char my_ip_str[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, &pSystem.my_ip, my_ip_str, sizeof(my_ip_str));
  printf("%s:%d\n", my_ip_str, pSystem.my_port);

  // Default multicast addresses
  strcpy(pSystem.liaison_addr, "ff12::");
  pSystem.liaison_port = 8080;
  strcpy(pSystem.auction_addr, "ff12::");
  pSystem.auction_port = 8081;

  return 0;
}

int join_auction() {
  // Socket to send
  int send_sock = setup_multicast_sender();
  if (send_sock < 0) return -1;

  // Socket to receive multicast
  int recv_sock = setup_multicast_receiver(pSystem.liaison_addr, pSystem.liaison_port);
  if (recv_sock < 0) {
    close(send_sock);
    return -1;
  }

  int unicast_sock = setup_unicast_sender(pSystem.my_port);

  // Send a request to join the system
  struct message *request = init_message(CODE_DEMANDE_LIAISON); // CODE = 3 for join request
  if (request == NULL) {
    perror("Échec de l'initialisation du message");
    close(unicast_sock);
    close(send_sock);
    close(recv_sock);
    return -1;
  }
  int buffer_size = get_buffer_size(request);
  char *buffer = malloc(buffer_size); // Allocate buffer for outgoing messages
  if (buffer == NULL) {
    perror("malloc a échoué");
    free_message(request);
    close(unicast_sock);
    close(send_sock);
    close(recv_sock);
    return -1;
  }
  if (message_to_buffer(request, buffer, buffer_size) < 0) {
    perror("message_to_buffer a échoué");
    free(buffer);
    free_message(request);
    close(unicast_sock);
    close(send_sock);
    close(recv_sock);
    return -1;
  }

  int result = send_multicast(send_sock, pSystem.liaison_addr, pSystem.liaison_port, buffer, buffer_size);
  if (result < 0) {
    close(unicast_sock);
    close(send_sock);
    close(recv_sock);
    return -1;
  }
  printf("  Demande de connexion envoyée... (CODE = 3)\n");

  // Set timeout for unicast socket
  struct timeval tv;
  tv.tv_sec = TIMEOUT;
  tv.tv_usec = 0;
  if (setsockopt(unicast_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) < 0) {
    perror("setsockopt a échoué");
    free(buffer);
    free_message(request);
    close(unicast_sock);
    close(send_sock);
    close(recv_sock);
    return -1;
  }

  // Wait for response
  struct sockaddr_in6 sender;
  int attempts = 0;

  while (attempts < MAX_ATTEMPTS) {
    // Attendre la réponse unicast sur notre socket dédié
    socklen_t sender_len = sizeof(sender);
    free(buffer);
    buffer = malloc(1024); // Allocate buffer for incoming messages
    if (buffer == NULL) {
      perror("malloc a échoué");
      free_message(request);
      close(unicast_sock);
      close(send_sock);
      close(recv_sock);
      return -1;
    }

    int len = recvfrom(unicast_sock, buffer, strlen(buffer) - 1, 0,
                      (struct sockaddr*)&sender, &sender_len);

    if (len > 0) {
      buffer[len] = '\0'; // S'assurer que la chaîne est terminée
      printf("  Réponse reçue en unicast (%d octets)\n", len);

      struct message *response = malloc(sizeof(struct message));
      if (response == NULL) {
        perror("malloc a échoué");
        free(buffer);
        free_message(request);
        close(unicast_sock);
        close(send_sock);
        close(recv_sock);
        return -1;
      }
      if (buffer_to_message(response, buffer) < 0) {
        perror("buffer_to_message a échoué");
        free(response);
        free(buffer);
        free_message(request);
        close(unicast_sock);
        close(send_sock);
        close(recv_sock);
        return -1;
      }
      close(unicast_sock);

      if (response->code == CODE_REPONSE_LIAISON) { // CODE = 4 (response)
        printf("  Réponse de connexion reçue\n");
        // Get sender's address
        char sender_ip_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &sender.sin6_addr, sender_ip_str, sizeof(sender_ip_str));
        printf("  Pair trouvé: ID=%d, IP=%s, Port=%d\n", response->id, sender_ip_str, response->port);

        unicast_sock = setup_client_socket(sender_ip_str, response->port);
        if (unicast_sock < 0) {
          perror("setup_client_socket a échoué");
          free(response);
          free(buffer);
          free_message(request);
          close(send_sock);
          close(recv_sock);
          return -1;
        }
        // Send a message to the sender with code 5
        struct message *info_msg = init_message(CODE_INFO_PAIR); // CODE = 5 for Info Pair
        info_msg->id = pSystem.my_id;
        message_set_ip(info_msg, pSystem.my_ip);
        message_set_port(info_msg, pSystem.my_port);
        // TODO Add cle, public key for TLS

        int info_buffer_size = get_buffer_size(info_msg);
        char info_buffer[info_buffer_size];
        memset(info_buffer, 0, sizeof(info_buffer));
        if (message_to_buffer(info_msg, info_buffer, info_buffer_size) < 0) {
          perror("message_to_buffer a échoué (info pair)");
          free_message(info_msg);
          free_message(response);
          free_message(request);
          close(send_sock);
          return -1;
        }

        // Send the message
        if (send(unicast_sock, info_buffer, info_buffer_size, 0) <= 0) {
          perror("send a échoué (info pair)");
          free_message(info_msg);
          free_message(response);
          free_message(request);
          close(send_sock);
          close(unicast_sock);
          return -1;
        }

        // Wait for response from sender (CODE = 50 or CODE = 51)
        memset(buffer, 0, buffer_size);
        len = recv(unicast_sock, buffer, buffer_size - 1, 0);
        if (len > 0) {
          buffer[len] = '\0'; // Ensure null-terminated string
          printf("  Réponse reçue de l'expéditeur (%d octets)\n", len);

          struct message *response = malloc(sizeof(struct message));
          if (response == NULL) {
            perror("malloc a échoué");
            free(buffer);
            free_message(info_msg);
            free_message(request);
            close(unicast_sock);
            close(send_sock);
            return -1;
          }
          if (buffer_to_message(response, buffer) < 0) {
            perror("buffer_to_message a échoué");
            free(response);
            free(buffer);
            free_message(info_msg);
            free_message(request);
            close(unicast_sock);
            close(send_sock);
            return -1;
          }

          if (response->code == CODE_ID_ACCEPTED) {
            printf("  ID accepté: %d\n", pSystem.my_id);
          } else if (response->code == CODE_ID_CHANGED) {
            printf("  ID changé: %d\n", response->id);
            pSystem.my_id = response->id;
          } else {
            printf("  Code de réponse inattendu: %d\n", response->code);
            free(response);
            free(buffer);
            free_message(info_msg);
            free_message(request);
            close(unicast_sock);
            close(send_sock);
            close(recv_sock);
            return -1;
          }
          // Add the peer to the system
        } else {
          perror("recv a échoué");
          free(response);
          free(buffer);
          free_message(info_msg);
          free_message(request);
          close(unicast_sock);
          close(send_sock);
          close(recv_sock);
          return -1;
        }

        free(response);
        free(buffer);
        free_message(info_msg);
        free_message(request);
        close(unicast_sock);
        close(send_sock);
        close(recv_sock);
        return 0;
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
      perror("recvfrom a échoué");
    }

    if (attempts < MAX_ATTEMPTS) {
      // Resend the request for each new attempt
      if (send_multicast(send_sock, pSystem.liaison_addr, pSystem.liaison_port, buffer, strlen(buffer)) < 0) {
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

  free(buffer);
  free_message(request);
  close(unicast_sock);
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

    close(send_sock);
    send_sock = setup_unicast_sender(response->port);
    if (send_sock < 0) {
      perror("setup_unicast_sender a échoué");
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }

    // Send the response (CODE 4) en unicast vers sender
    if (send_unicast(send_sock, &sender, resp_buffer, strlen(resp_buffer)) < 0) {
      perror("send_unicast a échoué");
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }
    printf("Réponse envoyée avec succès (CODE = 4)\n");

    // Conncect to the sender with TCP socket
    int unicast_sock = setup_server_socket(pSystem.my_port);
    if (unicast_sock < 0) {
      perror("setup_server_socket a échoué");
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }
    // Accept the connection
    if (listen(unicast_sock, 0) < 0) {
      perror("listen a échoué");
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }

    struct sockaddr_in6 client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_sock = accept(unicast_sock, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_sock < 0) {
      perror("accept a échoué");
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }

    // Wait for code 5 (Info Pair)
    char info_buffer[1024];
    memset(info_buffer, 0, sizeof(info_buffer));
    int info_len = recv(client_sock, info_buffer, sizeof(info_buffer) - 1, 0);
    if (info_len <= 0) {
      perror("recv a échoué");
      close(client_sock);
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }
    info_buffer[info_len] = '\0'; // Ensure null termination
    struct message *info_msg = malloc(sizeof(struct message));
    if (info_msg == NULL) {
      perror("malloc a échoué");
      close(client_sock);
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }
    if (buffer_to_message(info_msg, info_buffer) < 0) {
      perror("buffer_to_message a échoué");
      free(info_msg);
      close(client_sock);
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }
    // Check if ID is valid
    int client_id = info_msg->id;
    int found = 0;
    while (!found) {
      for (int i = 0; i < pSystem.count; i++) {
        if (pSystem.pairs[i].id == client_id) {
          found = 1;
          break;
        }
      }
      if (!found) {
        // Generate a new ID not in use
        client_id++;
      }
    }
    // Add the new peer
    if (add_pair(client_id, info_msg->ip, info_msg->port) < 0) {
      perror("add_pair a échoué");
      free(info_msg);
      close(client_sock);
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }
    // Init the response (50 if the ID is not used, 51 otherwise)
    if (info_msg->id == client_id) {
      free_message(response);
      response = init_message(CODE_ID_ACCEPTED);
    } else {
      response = init_message(CODE_ID_CHANGED);
      // Give the new ID to the client
      response->id = client_id;
    }
    memset(resp_buffer, 0, sizeof(resp_buffer));
    if (message_to_buffer(response, resp_buffer, sizeof(resp_buffer)) < 0) {
      perror("message_to_buffer a échoué");
      free(info_msg);
      close(client_sock);
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }
    // Send the response
    if (send(client_sock, resp_buffer, strlen(resp_buffer), 0) < 0) {
      perror("send a échoué");
      free(info_msg);
      close(client_sock);
      free_message(response);
      free_message(request);
      close(send_sock);
      return -1;
    }
    printf("Réponse envoyée avec succès (CODE = %d)\n", response->code);

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

int quit_auction() {
  return 0;
}

void free_pairs() {
  extern struct PairSystem pSystem;
  if (pSystem.pairs != NULL) {
    free(pSystem.pairs);
    pSystem.pairs = NULL;
  }
  pSystem.count = 0;
  pSystem.capacity = 0;
}