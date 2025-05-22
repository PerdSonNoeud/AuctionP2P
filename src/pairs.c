#include "include/pairs.h"
#include "include/multicast.h"
#include "include/message.h"
#include "include/utils.h"
#include "include/auction.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#define MAX_ATTEMPTS 3
#define TIMEOUT 5

struct PairSystem pSystem;
extern struct AuctionSystem auctionSys;

int init_pairs() {
  pSystem.pairs = malloc(10 * sizeof(struct Pair));
  if (!pSystem.pairs) {
    perror("malloc a échoué");
    return -1;
  }

  pSystem.count = 0;
  pSystem.capacity = 10;
  
  // Générer un ID aléatoire entre 1 et 10000 pour éviter les conflits
  srand(time(NULL));
  pSystem.my_id = 1 + (rand() % 10000);
  printf("ID généré aléatoirement: %d\n", pSystem.my_id);

  // Default IP address
  inet_pton(AF_INET6, "::1", &pSystem.my_ip);
  pSystem.my_port = 8000;

  // Default multicast addresses - s'assurer qu'elles sont correctement formatées
  strcpy(pSystem.liaison_addr, "ff02::1");  // Adresse lien-local multicast IPv6
  pSystem.liaison_port = 8080;
  strcpy(pSystem.auction_addr, "ff02::2");  // Adresse lien-local multicast IPv6
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

  // Initialiser les champs nécessaires pour éviter le problème LSIG
  message_set_mess(request, "Demande de connexion");
  message_set_sig(request, "");
  request->id = pSystem.my_id;

  int buffer_size = get_buffer_size(request);
  char *buffer = malloc(buffer_size); // Allocate buffer for outgoing messages
  if (buffer == NULL) {
    perror("malloc a échoué");
    free_message(request);
    close(send_sock);
    close(recv_sock);
    return -1;
  }
  
  // Déboguer le message avant envoi
  printf("Envoi d'une demande de connexion - ID: %d\n", request->id);
  
  if (message_to_buffer(request, buffer, buffer_size) < 0) {
    perror("message_to_buffer a échoué");
    free(buffer);
    free_message(request);
    close(send_sock);
    close(recv_sock);
    return -1;
  }

  // Afficher le contenu du buffer pour débogage
  printf("Contenu du buffer envoyé: ");
  for (int i = 0; i < 20 && i < buffer_size; i++) {
    printf("%c", buffer[i]);
  }
  printf("\n");

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
  
  // Allouer le buffer une seule fois en dehors de la boucle
  char recv_buffer[1024];
  memset(recv_buffer, 0, sizeof(recv_buffer));

  while (attempts < MAX_ATTEMPTS) {
    int len = receive_multicast(recv_sock, recv_buffer, sizeof(recv_buffer), &sender);
    if (len > 0) {
      // Afficher le contenu du buffer pour débogage
      printf("Contenu du buffer reçu (%d octets): ", len);
      for (int i = 0; i < 20 && i < len; i++) {
        printf("%c", recv_buffer[i]);
      }
      printf("\n");
      
      struct message *response = malloc(sizeof(struct message));
      if (response == NULL) {
        perror("malloc a échoué");
        close(send_sock);
        close(recv_sock);
        return -1;
      }
      
      // Initialiser les champs pour éviter les problèmes
      response->mess = NULL;
      response->sig = NULL;

      if (buffer_to_message(response, recv_buffer) < 0) {
        perror("buffer_to_message a échoué");
        free_message(response);
        attempts++;
        
        if (attempts < MAX_ATTEMPTS) {
          printf("Nouvelle tentative de réception (%d/%d)...\n", attempts + 1, MAX_ATTEMPTS);
          sleep(1);
          continue;  // Essayer à nouveau plutôt que d'abandonner
        } else {
          close(send_sock);
          close(recv_sock);
          return -1;
        }
      }

      // Obtenir l'adresse IP de l'expéditeur sous forme de chaîne
      char sender_ip_str[INET6_ADDRSTRLEN];
      inet_ntop(AF_INET6, &sender.sin6_addr, sender_ip_str, sizeof(sender_ip_str));
      
      // Obtenir notre adresse IP sous forme de chaîne
      char my_ip_str[INET6_ADDRSTRLEN];
      inet_ntop(AF_INET6, &pSystem.my_ip, my_ip_str, sizeof(my_ip_str));

      // Ignorer nos propres messages (même ID ET même adresse IP)
      if (response->id == pSystem.my_id && response->code == CODE_DEMANDE_LIAISON && 
          strcmp(sender_ip_str, my_ip_str) == 0) {
        printf("Ignoré: notre propre message de demande de connexion (même ID et IP)\n");
        free_message(response);
        continue;
      }

      if (response->code == CODE_REPONSE_LIAISON) { // CODE = 4 (response)
        printf("Réponse de connexion reçue\n");
        
        // Utiliser l'adresse IP et le port de l'expéditeur
        response->ip = sender.sin6_addr;
        response->port = ntohs(sender.sin6_port);
        
        // Display found peer information
        char ip_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &response->ip, ip_str, sizeof(ip_str));
        printf("Pair trouvé: ID=%d, IP=%s, Port=%d\n", response->id, ip_str, response->port);

        // Add the peer
        add_pair(response->id, response->ip, response->port);
        
        // Demander explicitement la liste des enchères existantes
        printf("Demande des enchères existantes...\n");
        
        // Attendre plus longtemps pour permettre au pair de nous envoyer les enchères existantes
        printf("Attente de la synchronisation des enchères (3 secondes)...\n");
        sleep(3);
        
        free_message(response);
        close(send_sock);
        close(recv_sock);
        return 0; // Successfully joined existing system
      } else {
        printf("Message reçu mais code incorrect: %d\n", response->code);
        free_message(response);
      }
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      printf("Timeout atteint, tentative %d/%d\n", attempts + 1, MAX_ATTEMPTS);
    } else {
      perror("receive_multicast a échoué");
    }

    attempts++;
    if (attempts < MAX_ATTEMPTS) {
      // Resend the request for each new attempt
      if (send_multicast(send_sock, pSystem.liaison_addr, pSystem.liaison_port, buffer, buffer_size) < 0) {
        printf("Échec du renvoi de la demande\n");
      } else {
        printf("Demande de connexion renvoyée (tentative %d/%d)\n", attempts + 1, MAX_ATTEMPTS);
      }

      sleep(1); // Wait before retrying
    } else {
      printf("Nombre maximal de tentatives atteint, aucune réponse reçue\n");
    }
  }

  free(buffer);
  free_message(request);
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
  for (int i = 0; i < 20 && i < len; i++) {
    printf("%c", buffer[i]);
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
    free_message(msg);
    return -1;
  }

  // Check message code
  if (msg->code == CODE_DEMANDE_LIAISON) { // CODE = 3 for connection request
    printf("Demande de connexion reçue\n");
    
    // Obtenir l'adresse IP de l'expéditeur sous forme de chaîne
    char sender_ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &sender.sin6_addr, sender_ip_str, sizeof(sender_ip_str));
    
    // Obtenir notre adresse IP sous forme de chaîne
    char my_ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &pSystem.my_ip, my_ip_str, sizeof(my_ip_str));
    
    // Vérifier si c'est notre propre message (même ID ET même adresse IP)
    if (msg->id == pSystem.my_id && strcmp(sender_ip_str, my_ip_str) == 0) {
      printf("Message ignoré : c'est notre propre demande de connexion (même ID et IP)\n");
      free_message(msg);
      return 0;
    }

    // Display requester info
    printf("Demande de connexion de ID=%d, IP=%s, Port=%d\n", msg->id, sender_ip_str, ntohs(sender.sin6_port));

    // Add this peer to our list with IP from sender address
    msg->ip = sender.sin6_addr; // Use the actual sender IP
    msg->port = ntohs(sender.sin6_port); // Use the actual sender port
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
    message_set_sig(response, ""); // Empty signature but necessary

    // Convert the response to buffer
    int resp_buffer_size = get_buffer_size(response);
    char *resp_buffer = malloc(resp_buffer_size);
    if (resp_buffer == NULL) {
      perror("malloc a échoué");
      free_message(response);
      free_message(msg);
      close(send_sock);
      return -1;
    }

    memset(resp_buffer, 0, resp_buffer_size);

    if (message_to_buffer(response, resp_buffer, resp_buffer_size) < 0) {
      perror("message_to_buffer a échoué");
      free(resp_buffer);
      free_message(response);
      free_message(msg);
      close(send_sock);
      return -1;
    }

    // Debug response before sending
    printf("Contenu du buffer de réponse: ");
    for (int i = 0; i < 20 && i < resp_buffer_size; i++) {
      printf("%c", resp_buffer[i]);
    }
    printf("\n");

    // Send the response
    printf("Envoi de la réponse au demandeur...\n");
    if (send_multicast(send_sock, pSystem.liaison_addr, pSystem.liaison_port,
                      resp_buffer, resp_buffer_size) < 0) {
      perror("send_multicast a échoué");
      free(resp_buffer);
      free_message(response);
      free_message(msg);
      close(send_sock);
      return -1;
    }

    printf("Réponse envoyée avec succès\n");
    
    // Envoyer les informations sur les enchères existantes au nouveau pair
    if (auctionSys.count > 0) {
      printf("Envoi de %d enchères existantes au nouveau pair\n", auctionSys.count);
      
      // Attendre un court instant pour s'assurer que le pair a eu le temps de configurer son récepteur d'enchères
      usleep(500000); // 500ms
      
      // Utiliser la fonction de diffusion de toutes les enchères
      broadcast_all_auctions();
    } else {
      printf("Aucune enchère existante à envoyer au nouveau pair\n");
    }

    free(resp_buffer);
    free_message(response);
    free_message(msg);
    close(send_sock);
    return 1;
  } else if (msg->code == CODE_REPONSE_LIAISON) { // CODE = 4 for response
    // Obtenir l'adresse IP de l'expéditeur sous forme de chaîne
    char sender_ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &sender.sin6_addr, sender_ip_str, sizeof(sender_ip_str));
    
    // Obtenir notre adresse IP sous forme de chaîne
    char my_ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &pSystem.my_ip, my_ip_str, sizeof(my_ip_str));
    
    // Ignorer nos propres réponses (même ID ET même adresse IP)
    if (msg->id == pSystem.my_id && strcmp(sender_ip_str, my_ip_str) == 0) {
      printf("Message ignoré : c'est notre propre réponse de connexion (même ID et IP)\n");
      free_message(msg);
      return 0;
    }
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
