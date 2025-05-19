#include "include/pairs.h"
#include "include/multicast.h"
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

PairSystem pSystem;

int init_pairs() {
  pSystem.pairs = malloc(10 * sizeof(Pair));
  if (!pSystem.pairs) {
    perror("malloc a échoué");
    return -1;
  }

  pSystem.count = 0;
  pSystem.capacity = 10;
  pSystem.my_id = 1; // ID par défaut

  // Adresse IP par défaut
  inet_pton(AF_INET6, "::1", &pSystem.my_ip);
  pSystem.my_port = 8000;

  // Adresses multicast par défaut
  strcpy(pSystem.liaison_addr, "ff02::1");
  pSystem.liaison_port = 8080;
  strcpy(pSystem.auction_addr, "ff02::2");
  pSystem.auction_port = 8081;

  return 0;
}

int join_auction() {
  // Socket pour envoyer
  int send_sock = setup_multicast_sender();
  if (send_sock < 0)
    return -1;

  // Socket pour recevoir
  int recv_sock =
      setup_multicast_receiver(pSystem.liaison_addr, pSystem.liaison_port);
  if (recv_sock < 0) {
    close(send_sock);
    return -1;
  }

  // Envoyer une demande de rejoindre le système
  uint8_t request[100];
  memset(request, 0, sizeof(request)); // Initialiser le buffer à 0
  
  request[0] = 3; // CODE = 3
  memcpy(&request[1], &pSystem.my_id, sizeof(pSystem.my_id));
  memcpy(&request[3], &pSystem.my_ip, sizeof(pSystem.my_ip));
  memcpy(&request[19], &pSystem.my_port, sizeof(pSystem.my_port));
  
  int result = send_multicast(send_sock, pSystem.liaison_addr,
                           pSystem.liaison_port, request, sizeof(request));
  if (result < 0) {
    close(send_sock);
    close(recv_sock);
    return -1;
  }
  printf("Demande de connexion envoyée\n");
  
  // Configuration du timeout
  struct timeval tv;
  tv.tv_sec = TIMEOUT;
  tv.tv_usec = 0;
  if (setsockopt(recv_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv,
                 sizeof tv) < 0) {
    perror("setsockopt a échoué");
    close(send_sock);
    close(recv_sock);
    return -1;
  }
  
  // Attendre la réponse
  uint8_t buffer[100];
  struct sockaddr_in6 sender;
  int attempts = 0;
  
  while (attempts < MAX_ATTEMPTS) {
    memset(buffer, 0, sizeof(buffer)); // Initialiser le buffer
    
    int len = receive_multicast(recv_sock, buffer, sizeof(buffer), &sender);
    if (len > 0) {
      if (buffer[0] == 4) { // CODE = 4 (réponse)
        printf("Réponse de connexion reçue\n");
        unsigned short id;
        memcpy(&id, &buffer[1], sizeof(id));
        struct in6_addr ip;
        memcpy(&ip, &buffer[3], sizeof(ip));
        unsigned short port;
        memcpy(&port, &buffer[19], sizeof(port));

        // Afficher l'information du pair trouvé
        char ip_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &ip, ip_str, sizeof(ip_str));
        printf("Pair trouvé: ID=%d, IP=%s, Port=%d\n", id, ip_str, port);

        // Ajouter le pair
        add_pair(id, ip, port);
        close(send_sock);
        close(recv_sock);
        return 0; // Système existant rejoint avec succès
      } else {
        printf("Message reçu mais code incorrect: %d\n", buffer[0]);
      }
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      printf("Timeout atteint, tentative %d/%d\n", attempts + 1, MAX_ATTEMPTS);
    } else {
      perror("receive_multicast a échoué");
    }
    
    attempts++;
    if (attempts < MAX_ATTEMPTS) {
      // Renvoyer la demande pour chaque nouvelle tentative
      if (send_multicast(send_sock, pSystem.liaison_addr, pSystem.liaison_port, 
                       request, sizeof(request)) < 0) {
        printf("Échec du renvoi de la demande\n");
      } else {
        printf("Demande de connexion renvoyée (tentative %d/%d)\n", 
               attempts + 1, MAX_ATTEMPTS);
      }
      
      sleep(1); // Attendre avant de réessayer
    } else {
      printf("Nombre maximal de tentatives atteint, aucune réponse reçue\n");
    }
  }

  close(send_sock);
  close(recv_sock);
  return -1; // Échec de connexion
}

int handle_join(int sock) {
  uint8_t buffer[100];
  struct sockaddr_in6 sender;

  int len = receive_multicast(sock, buffer, sizeof(buffer), &sender);
  if (len > 0 && buffer[0] == 3) { // CODE = 3 pour demande de jointure
    printf("Demande de connexion reçue\n");

    // Extraire les informations du demandeur
    unsigned short requester_id;
    memcpy(&requester_id, &buffer[1], sizeof(requester_id));
    
    struct in6_addr requester_ip;
    memcpy(&requester_ip, &buffer[3], sizeof(requester_ip));
    
    unsigned short requester_port;
    memcpy(&requester_port, &buffer[19], sizeof(requester_port));
    
    // Afficher les infos du demandeur
    char requester_ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &requester_ip, requester_ip_str, sizeof(requester_ip_str));
    printf("Demande de connexion de ID=%d, IP=%s, Port=%d\n", 
           requester_id, requester_ip_str, requester_port);

    // Ajouter ce pair à notre liste
    add_pair(requester_id, requester_ip, requester_port);
    
    // Créer un socket pour envoyer la réponse
    int send_sock = setup_multicast_sender();
    if (send_sock < 0) {
      perror("setup_multicast_sender a échoué");
      return -1;
    }

    // Préparer la réponse
    uint8_t response[100];
    memset(response, 0, sizeof(response)); // Initialiser le buffer à 0
    
    response[0] = 4; // CODE = 4 pour réponse à une demande de jointure
    memcpy(&response[1], &pSystem.my_id, sizeof(pSystem.my_id));
    memcpy(&response[3], &pSystem.my_ip, sizeof(pSystem.my_ip));
    memcpy(&response[19], &pSystem.my_port, sizeof(pSystem.my_port));
    
    // Envoyer la réponse
    printf("Envoi de la réponse au demandeur...\n");
    if (send_multicast(send_sock, pSystem.liaison_addr, pSystem.liaison_port, 
                      response, sizeof(response)) < 0) {
      perror("send_multicast a échoué");
      close(send_sock);
      return -1;
    }
    
    printf("Réponse envoyée avec succès\n");
    close(send_sock);
    return 1;
  }

  return 0;
}

int add_pair(unsigned short id, struct in6_addr ip, unsigned short port) {
  // Vérifier si le pair existe déjà
  for (int i = 0; i < pSystem.count; i++) {
    if (pSystem.pairs[i].id == id) {
      // Mettre à jour les informations du pair existant
      pSystem.pairs[i].ip = ip;
      pSystem.pairs[i].port = port;
      pSystem.pairs[i].active = 1;
      return 0;
    }
  }
  
  // Ajouter un nouveau pair
  if (pSystem.count >= pSystem.capacity) {
    int new_capacity = pSystem.capacity * 2;
    Pair *new_pairs = realloc(pSystem.pairs, new_capacity * sizeof(Pair));
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
