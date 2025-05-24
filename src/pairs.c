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

#define UNKNOWN_SIZE 1024 // Default size for unknown buffer sizes
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
  strcpy(pSystem.liaison_addr, "ff12::");
  pSystem.liaison_port = 8080;
  strcpy(pSystem.auction_addr, "ff12::");
  pSystem.auction_port = 8081;

  return 0;
}

int setup_timeout(int sock, int sec) {
  struct timeval tv;
  tv.tv_sec = sec;
  tv.tv_usec = 0;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv)) < 0) {
    perror("setsockopt a échoué");
    return -1;
  }
  return 0;
}

int join_pairs(int m_sender) {
  // Send a request to join the system (CODE = 3)
  struct message *request = init_message(CODE_DEMANDE_LIAISON);
  if (request == NULL) {
    perror("Échec de l'initialisation du message");
    return -1;
  }
  // Get the buffer size for the request
  int buffer_size = get_buffer_size(request);
  char *buffer = malloc(buffer_size);
  if (buffer == NULL) {
    perror("malloc a échoué");
    free_message(request);
    return -1;
  }
  // Convert the message to a buffer
  if (message_to_buffer(request, buffer, buffer_size) < 0) {
    perror("message_to_buffer a échoué");
    free_message(request);
    free(buffer);
    return -1;
  }
  // Converted to buffer, no longer needed
  free_message(request);

  // Send the multicast request
  int result = send_multicast(m_sender, pSystem.liaison_addr, pSystem.liaison_port, buffer, buffer_size);
  if (result < 0) {
    perror("send_multicast a échoué");
    free(buffer);
    return -1;
  }
  printf("    Demande de connexion envoyée... (CODE = 3)\n");

  // Socket to receive unicast responses in UDP
  int u_recv = setup_unicast_receiver(pSystem.my_port);
  // Set timeout for unicast socket
  if (setup_timeout(u_recv, TIMEOUT) < 0) {
    perror("setup_timeout a échoué");
    close(u_recv);
    free(buffer);
    return -1;
  }

  // Prepare to receive unicast responses
  struct sockaddr_in6 sender;
  socklen_t sender_len = sizeof(sender);
  char resp_buffer[UNKNOWN_SIZE];

  int attempts = 1;
  // Wait for response
  while (attempts < MAX_ATTEMPTS) {
    // Reset the response buffer after each attempt
    memset(resp_buffer, 0, UNKNOWN_SIZE);
    // Try to receive a message
    int len = recvfrom(u_recv, resp_buffer, UNKNOWN_SIZE - 1, 0,
                      (struct sockaddr*)&sender, &sender_len);
    if (len > 0) { // Received data
      // Ensure null-termination
      resp_buffer[len] = '\0';
      printf("    Réponse reçue en unicast (%d octets)\n", len);
      // Initialize a message structure to hold the response
      struct message *response = malloc(sizeof(struct message));
      if (response == NULL) {
        perror("malloc a échoué");
        close(u_recv);
        free(buffer);
        return -1;
      }
      // Convert the buffer to a message
      if (buffer_to_message(response, resp_buffer) < 0) {
        perror("buffer_to_message a échoué");
        free_message(response);
        close(u_recv);
        free(buffer);
        return -1;
      }
      // Valid response, no longer needed
      close(u_recv);

      // Check the response code
      if (response->code == CODE_REPONSE_LIAISON) { // CODE = 4 (response)
        free(buffer); // Free the buffer as we have a valid response
        printf("    Réponse de connexion reçue (CODE = 4)\n");
        // Get sender's address
        char sender_ip_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &sender.sin6_addr, sender_ip_str, sizeof(sender_ip_str));
        // Save the sender as a new pair
        if (add_pair(response->id, sender.sin6_addr, response->port) < 0) {
          perror("add_pair a échoué");
          return -1;
        }

        int client_sock = setup_client_socket(sender_ip_str, response->port);
        if (client_sock < 0) {
          perror("setup_client_socket a échoué");
          free_message(response);
          return -1;
        }
        free_message(response);

        // Send a message to the sender with code 5
        printf("    Envoi d'un message d'information en TCP... (CODE = 5)\n");
        struct message *info_msg = init_message(CODE_INFO_PAIR); // CODE = 5 for Info Pair
        if (info_msg == NULL) {
          perror("Échec de l'initialisation du message d'information");
          close(client_sock);
          return -1;
        }
        // Initialize the info
        // TODO Add cle, public key for TLS
        struct info *my_info = malloc(sizeof(struct info));
        if (my_info == NULL) {
          perror("malloc a échoué (info pair)");
          free_message(info_msg);
          close(client_sock);
          return -1;
        }
        if (init_info(my_info, pSystem.my_id, pSystem.my_ip, pSystem.my_port) < 0) {
          perror("init_info a échoué");
          free(my_info);
          free_message(info_msg);
          close(client_sock);
          return -1;
        }
        if (message_set_nb(info_msg, 1) < 0) {
          perror("message_set_nb a échoué");
          free(my_info);
          free_message(info_msg);
          close(client_sock);
          return -1;
        }
        if (message_set_info(info_msg, 0, my_info)) {
          perror("message_set_info a échoué");
          free(my_info);
          free_message(info_msg);
          close(client_sock);
          return -1;
        }

        int info_buffer_size = get_buffer_size(info_msg);
        char info_buffer[info_buffer_size];
        memset(info_buffer, 0, sizeof(info_buffer));
        if (message_to_buffer(info_msg, info_buffer, info_buffer_size) < 0) {
          perror("message_to_buffer a échoué (info pair)");
          free_message(info_msg);
          close(client_sock);
          return -1;
        }
        free_message(info_msg);

        // Send the message (CODE = 5)
        if (send(client_sock, info_buffer, info_buffer_size, 0) <= 0) {
          perror("send a échoué (info pair)");
          close(client_sock);
          return -1;
        }

        // Wait for response from sender (CODE = 50 or CODE = 51)
        buffer = malloc(UNKNOWN_SIZE);
        if (buffer == NULL) {
          perror("malloc a échoué (buffer)");
          close(client_sock);
          return -1;
        }

        len = recv(client_sock, buffer, UNKNOWN_SIZE - 1, 0);
        if (len > 0) {
          buffer[len] = '\0'; // Ensure null-terminated string
          printf("    Réponse reçue de l'expéditeur (%d octets)\n", len);

          struct message *response = malloc(sizeof(struct message));
          if (response == NULL) {
            perror("malloc a échoué");
            close(client_sock);
            free(buffer);
            return -1;
          }
          if (buffer_to_message(response, buffer) < 0) {
            perror("buffer_to_message a échoué");
            free_message(response);
            close(client_sock);
            free(buffer);
            return -1;
          }
          free(buffer); // Converted to message, no longer needed

          if (response->code == CODE_ID_ACCEPTED) {
            printf("    ID accepté: %d\n", pSystem.my_id);
          } else if (response->code == CODE_ID_CHANGED) {
            printf("    ID changé: %d\n", response->id);
            pSystem.my_id = response->id;
          } else {
            printf("  Code de réponse inattendu: %d\n", response->code);
            free_message(response);
            close(client_sock);
            return -1;
          }
          free_message(response);
          // Add the peer to the system
        } else {
          perror("recv a échoué");
          close(client_sock);
          free(buffer);
          return -1;
        }

        buffer = malloc(UNKNOWN_SIZE);
        if (buffer == NULL) {
          perror("malloc a échoué (buffer)");
          close(client_sock);
          return -1;
        }
        // Recv info about the auction system (CODE = 7)
        len = recv(client_sock, buffer, UNKNOWN_SIZE - 1, 0);
        if (len > 0) {
          buffer[len] = '\0'; // Ensure null-terminated string
          printf("    Informations sur le système reçues (%d octets)\n", len);

          struct message *response = malloc(sizeof(struct message));
          if (response == NULL) {
            perror("malloc a échoué");
            close(client_sock);
            free(buffer);
            return -1;
          }
          if (buffer_to_message(response, buffer) < 0) {
            perror("buffer_to_message a échoué (info système)");
            free_message(response);
            close(client_sock);
            free(buffer);
            return -1;
          }
          free(buffer); // Converted to message, no longer needed

          if (response->code == CODE_INFO_SYSTEME) { // CODE = 7 for system info
            printf("    Mise à jour des informations du système d'enchères...\n");
            // Update the system information
            char ip_str[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &response->ip, ip_str, sizeof(ip_str));
            strcpy(pSystem.auction_addr, ip_str);
            pSystem.auction_port = response->port;
            int max = 1;
            if (response->nb > 0) {
              max = response->nb; // Update max if we have more than one pair
            }
            pSystem.count = max; // Update the count of pairs + 1 for the one we're connected to
            // Update the pairs list
            for (int i = 0; i < response->nb; i++) {
              if (add_pair(response->info[i].id, response->info[i].ip, response->info[i].port) < 0) {
                perror("add_pair a échoué (info système)");
                free_message(response);
                close(client_sock);
                return -1;
              }
            }
          }
        free_message(response);
        }
        close(u_recv);
        return 0;
      } else {
        if (response->code == 0 || response->code == CODE_DEMANDE_LIAISON) {
          continue; // Ignore empty messages
        }
        printf("  Message reçu mais code incorrect: %d\n", response->code);
      }
      free_message(response);
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      printf("    Timeout atteint, tentative %d/%d\n", attempts, MAX_ATTEMPTS);
    } else {
      perror("recvfrom a échoué");
    }

    if (attempts < MAX_ATTEMPTS) {
      // Resend the request for each new attempt
      if (send_multicast(m_sender, pSystem.liaison_addr, pSystem.liaison_port, buffer, strlen(buffer)) < 0) {
        printf("    Échec du renvoi de la demande\n");
      } else {
        printf("    Demande de connexion renvoyée (tentative %d/%d)\n", attempts + 1, MAX_ATTEMPTS);
      }
      attempts++;

      sleep(1); // Wait before retrying
    } else {
      printf("    Nombre maximal de tentatives atteint, aucune réponse reçue\n");
    }
  }

  free(buffer);
  close(u_recv);
  return -1; // Connection failed
}

int handle_join(int m_recv, int server_sock) {
  // Sender address
  struct sockaddr_in6 sender;
  char buffer[UNKNOWN_SIZE]; // Static buffer with fixed size
  memset(buffer, 0, UNKNOWN_SIZE);

  int len = receive_multicast(m_recv, buffer, UNKNOWN_SIZE, &sender);
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
    // Free request, no need to keep it
    free_message(request);
    // Obtain sender's IP address
    char sender_ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &sender.sin6_addr, sender_ip_str, sizeof(sender_ip_str));
    // Display requester info
    printf("\nDemande de connexion, IP=%s, Port=%d\n", sender_ip_str, ntohs(sender.sin6_port));

    // Prepare the response
    struct message *response = init_message(CODE_REPONSE_LIAISON); // CODE = 4 for response
    if (response == NULL) {
      perror("init_message a échoué");
      return -1;
    }

    response->id = pSystem.my_id;
    message_set_ip(response, pSystem.my_ip);
    message_set_port(response, pSystem.my_port);

    // Convert the response to buffer
    int resp_buffer_size = get_buffer_size(response);
    char resp_buffer[resp_buffer_size];
    memset(resp_buffer, 0, resp_buffer_size);

    if (message_to_buffer(response, resp_buffer, resp_buffer_size) < 0) {
      perror("message_to_buffer a échoué");
      free_message(response);
      return -1;
    }
    // Free the response message
    free_message(response);

    // Send the response (CODE 4) en unicast vers sender
    int u_send = setup_unicast_sender(sender_ip_str, pSystem.my_port);
    if (u_send < 0) {
      perror("setup_unicast_sender a échoué");
      return -1;
    }
    sender.sin6_port = htons(pSystem.my_port);
    int len = sendto(u_send, resp_buffer, strlen(resp_buffer), 0,
                     (struct sockaddr *)&sender, sizeof(sender));
    close(u_send);
    if (len <= 0) {
      perror("sendto a échoué");
      return -1;
    }
    printf("  Réponse de la demande de connexion envoyée... (CODE = 4)\n");

    struct sockaddr_in6 client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_sock < 0) {
      perror("accept a échoué");
      return -1;
    }

    // Wait for code 5 (Info Pair)
    char info_buffer[UNKNOWN_SIZE];
    memset(info_buffer, 0, UNKNOWN_SIZE);
    int info_len = recv(client_sock, info_buffer, UNKNOWN_SIZE - 1, 0);
    if (info_len <= 0) {
      perror("recv a échoué");
      close(client_sock);
      return -1;
    }
    info_buffer[info_len] = '\0'; // Ensure null termination
    printf("    Information reçue du pair (%d octets)\n", info_len);
    struct message *info_msg = malloc(sizeof(struct message));
    if (info_msg == NULL) {
      perror("malloc a échoué");
      close(client_sock);
      return -1;
    }
    if (buffer_to_message(info_msg, info_buffer) < 0) {
      perror("buffer_to_message a échoué");
      free_message(info_msg);
      close(client_sock);
      return -1;
    }
    if (info_msg->code == CODE_INFO_PAIR_BROADCAST) { // CODE = 6 if it is a broadcast instead
      if (info_msg->info[0].id == pSystem.my_id) {
        printf("Message ignoré : message avec notre propre ID (%d)\n", pSystem.my_id);
        free_message(info_msg);
        return 0; // Ignore messages with our own ID
      }
      // Add the new peer to the system
      if (add_pair(info_msg->info[0].id, info_msg->info[0].ip, info_msg->info[0].port) < 0) {
        perror("add_pair a échoué");
        free_message(info_msg);
        return -1;
      }
      free_message(info_msg);
      close(client_sock);
      return 1; // Successfully added the peer
    }
    // Check if ID is valid
    int client_id = info_msg->info[0].id;
    int found = 1;
    while (found == 1) {
      found = 0;
      if (pSystem.my_id == client_id) {
        // If the ID is the same as our own, increment it
        printf("    ID %d est notre propre ID, génération d'un nouvel ID...\n", client_id);
        client_id++;
        found = 1; // Set found to 1 to continue checking
      } else {
        for (int i = 0; i < pSystem.count; i++) {
          if (pSystem.pairs[i].id == client_id) {
            found = 1;
            break;
          }
        }
        if (found) {
          // Generate a new ID not in use
          printf("    ID %d déjà utilisé, génération d'un nouvel ID...\n", client_id);
          client_id++;
        }
      }
    }
    // Init the response (50 if the ID is not used, 51 otherwise)
    if (info_msg->info[0].id == client_id) {
      response = init_message(CODE_ID_ACCEPTED);
    } else {
      response = init_message(CODE_ID_CHANGED);
      // Give the new ID to the client
      response->id = client_id;
    }

    memset(resp_buffer, 0, sizeof(resp_buffer)); // Code = 50 or 51, most likely to be smaller than Code 4
    if (message_to_buffer(response, resp_buffer, sizeof(resp_buffer)) < 0) {
      perror("message_to_buffer a échoué");
      free_message(response);
      close(client_sock);
      return -1;
    }
    // Send the response (CODE = 50 or 51)
    if (send(client_sock, resp_buffer, strlen(resp_buffer), 0) < 0) {
      perror("send a échoué");
      close(client_sock);
      free_message(response);
      return -1;
    }
    if (response->code == CODE_ID_ACCEPTED) printf("  Validation d'ID envoyé... (CODE = 50)\n");
    else printf("  Changement d'ID envoyé... (CODE = 51)\n");

    free_message(response);
    // Add the new pair to the system (CODE = 6)
    sleep(1); // Wait for the other pairs to end their handle_join() process
    send_new_pair(client_id, client_addr.sin6_addr, info_msg->info[0].port);
    sleep(1); // Wait for the new pair to be sent
    // Prepare the system information message (CODE = 7)
    struct message *system_info = init_message(CODE_INFO_SYSTEME);
    if (system_info == NULL) {
      perror("Échec de l'initialisation du message d'information système");
      close(client_sock);
      return -1;
    }
    // Set the system information
    system_info->id = pSystem.my_id;
    struct in6_addr auction_ip;
    inet_pton(AF_INET6, pSystem.auction_addr, &auction_ip); // Default IP address
    message_set_ip(system_info, auction_ip);
    message_set_port(system_info, pSystem.auction_port);
    if (message_set_nb(system_info, pSystem.count) < 0) {
      perror("message_set_nb a échoué");
      free_message(system_info);
      close(client_sock);
      return -1;
    }
    // Set the pairs information
    for (int i = 0; i < pSystem.count; i++) {
      struct info *pair_info = malloc(sizeof(struct info));
      if (pair_info == NULL) {
        perror("malloc a échoué (info pair)");
        free_message(system_info);
        close(client_sock);
        return -1;
      }
      if (init_info(pair_info, pSystem.pairs[i].id, pSystem.pairs[i].ip, pSystem.pairs[i].port) < 0) {
        perror("init_info a échoué");
        free(pair_info);
        free_message(system_info);
        close(client_sock);
        return -1;
      }
      if (message_set_info(system_info, i, pair_info) < 0) {
        perror("message_set_info a échoué");
        free(pair_info);
        free_message(system_info);
        close(client_sock);
        return -1;
      }
    }
    // Convert the system info message to buffer
    int system_info_buffer_size = get_buffer_size(system_info);
    char system_info_buffer[system_info_buffer_size];
    memset(system_info_buffer, 0, system_info_buffer_size);
    if (message_to_buffer(system_info, system_info_buffer, system_info_buffer_size) < 0) {
      perror("message_to_buffer a échoué (info système)");
      free_message(system_info);
      close(client_sock);
      return -1;
    }
    // Free the system info message as we have the buffer now
    free_message(system_info);
    // Send the system info message (CODE = 7)
    if (send(client_sock, system_info_buffer, system_info_buffer_size, 0) < 0) {
      perror("send a échoué (info système)");
      close(client_sock);
      return -1;
    }
    printf("  Envoie des pairs du systèmes... (CODE = 7)\n");

    close(client_sock);
    // Add the new peer after sending the new pair to all peers
    if (add_pair(client_id, client_addr.sin6_addr, info_msg->info[0].port) < 0) {
      perror("add_pair a échoué");
      free_message(info_msg);
      return -1;
    }
    free_message(info_msg);
    return 1;

  } else if (request->code == CODE_REPONSE_LIAISON) { // CODE = 4
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

int send_new_pair(unsigned short id, struct in6_addr ip, unsigned short port) {
  printf("Envoi des informations du nouveau pair à tous les pairs...\n");
  for (int i = 0; i < pSystem.count; i++) {
    char peer_ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &pSystem.pairs[i].ip, peer_ip_str, sizeof(peer_ip_str));

    int sock = setup_client_socket(peer_ip_str, pSystem.pairs[i].port);
    if (sock < 0) {
      perror("Échec de la connexion au pair");
      continue; // Skip to the next peer if connection fails
    }
    // Prepare the message to send
    struct message *msg = init_message(CODE_INFO_PAIR_BROADCAST);
    if (msg == NULL) {
      perror("Échec de l'initialisation du message");
      close(sock);
      return -1;
    }
    struct info *new_info = malloc(sizeof(struct info));
    if (new_info == NULL) {
      perror("malloc a échoué (info pair)");
      free_message(msg);
      close(sock);
      return -1;
    }
    if (init_info(new_info, id, ip, port) < 0) {
      perror("init_info a échoué");
      free(new_info);
      free_message(msg);
      close(sock);
      return -1;
    }
    if (message_set_nb(msg, 1) < 0) {
      perror("message_set_nb a échoué");
      free(new_info);
      free_message(msg);
      close(sock);
      return -1;
    }
    if (message_set_info(msg, 0, new_info) < 0) {
      perror("message_set_info a échoué");
      free(new_info);
      free_message(msg);
      close(sock);
      return -1;
    }

    int buffer_size = get_buffer_size(msg);
    char *buffer = malloc(buffer_size);
    if (buffer == NULL) {
      perror("malloc a échoué");
      free_message(msg);
      close(sock);
      return -1;
    }
    if (message_to_buffer(msg, buffer, buffer_size) < 0) {
      perror("message_to_buffer a échoué");
      free(buffer);
      free_message(msg);
      close(sock);
      return -1;
    }
    // Free the message as we have the buffer now
    free_message(msg);
    // Send the message
    if (send(sock, buffer, buffer_size, 0) < 0) {
      perror("send a échoué");
      free(buffer);
      close(sock);
      return -1;
    }
    printf("  Message envoyé au pair %d: ID=%d, IP=%s, Port=%d\n",
           i + 1, id, peer_ip_str, port);
    free(buffer);
    close(sock);
  }
  return 0;
}

int add_pair(unsigned short id, struct in6_addr ip, unsigned short port) {
  char ip_str[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, &ip, ip_str, sizeof(ip_str));
  printf("    Ajout du pair: ID=%d, IP=%s, Port=%d\n", id, ip_str, port);

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

void print_pairs() {
  printf("  Pairs connectés:\n");
  for (int i = 0; i < pSystem.count; i++) {
    char ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &pSystem.pairs[i].ip, ip_str, sizeof(ip_str));
    printf("    Pair %d: ID=%d, IP=%s, Port=%d, Actif=%s\n",
           i + 1, pSystem.pairs[i].id, ip_str, pSystem.pairs[i].port,
           pSystem.pairs[i].active ? "Oui" : "Non");
  }
}

void print_network_info() {
  printf("\n  Informations du réseau P2P:\n");
  printf("    ID local: %d\n", pSystem.my_id);
  char my_ip_str[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, &pSystem.my_ip, my_ip_str, sizeof(my_ip_str));
  printf("    IP locale: %s\n", my_ip_str);
  printf("    Port local: %d\n", pSystem.my_port);
  printf("    Nombre de pairs connectés: %d\n", pSystem.count);

  print_pairs();
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
