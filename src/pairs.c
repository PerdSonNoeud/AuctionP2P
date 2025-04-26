#include "include/pairs.h"
#include "include/multicast.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_ATTEMPTS 3
#define TIMEOUT 5

PairSystem pSystem;

int init_pairs() {
  pSystem.pairs = malloc(10 * sizeof(Pair));
  if (!pSystem.pairs) {
    perror("malloc failed");
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

  // TODO : Envoyer une demande de rejoindre le système
  uint8_t request[100];
  request[0] = 3; // CODE = 3
  memcpy(&request[1], &pSystem.my_id, sizeof(pSystem.my_id));
  memcpy(&request[3], &pSystem.my_ip, sizeof(pSystem.my_ip));
  memcpy(&request[19], &pSystem.my_port, sizeof(pSystem.my_port));
  int len = send_multicast(send_sock, pSystem.liaison_addr,
                           pSystem.liaison_port, request, sizeof(request));
  if (len < 0) {
    close(send_sock);
    close(recv_sock);
    return -1;
  }
  printf("Join request sent\n");
  close(send_sock);
  // Attendre la réponse
  struct timeval tv;
  tv.tv_sec = TIMEOUT;
  tv.tv_usec = 0;
  if (setsockopt(recv_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv,
                 sizeof tv) < 0) {
    perror("setsockopt failed");
    return -1;
  }
  // Attendre la réponse
  uint8_t buffer[100];
  struct sockaddr_in6 sender;
  int attempts = 0;
  while (attempts < MAX_ATTEMPTS) {
    int len = receive_multicast(recv_sock, buffer, sizeof(buffer), &sender);
    if (len > 0 && buffer[0] == 4) { // CODE = 4
      printf("Join response received\n");
      unsigned short id;
      memcpy(&id, &buffer[1], sizeof(id));
      struct in6_addr ip;
      memcpy(&ip, &buffer[3], sizeof(ip));
      unsigned short port;
      memcpy(&port, &buffer[19], sizeof(port));

      // Ajouter le pair
      add_pair(id, ip, port);
      close(recv_sock);
      return 0; // Système existant rejoint
    }
    attempts++;
    if (attempts < MAX_ATTEMPTS) {
      sleep(1); // Attendre avant de réessayer
    } else {
      printf("Max attempts reached, no response received\n");
      return -1; // Pas de réponse
    }
  }

  return -1; // Système existant rejoint
}

int handle_join(int sock) {
  uint8_t buffer[100];
  struct sockaddr_in6 sender;

  int len = receive_multicast(sock, buffer, sizeof(buffer), &sender);
  if (len > 0 && buffer[0] == 3) { // CODE = 3
    printf("Received join request\n");

    // Répondre avec l'ID et l'adresse

    // Socket pour répondre
    int send_sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (send_sock < 0)
      return -1;

    // Envoyer l'ID et l'adresse

    close(send_sock);
    return 1;
  }

  return 0;
}

int add_pair(unsigned short id, struct in6_addr ip, unsigned short port) {
  // Vérifier si déjà présent
  for (int i = 0; i < pSystem.count; i++) {
    if (pSystem.pairs[i].id == id) {
      pSystem.pairs[i].ip = ip;
      pSystem.pairs[i].port = port;
      pSystem.pairs[i].active = 1;
      return 0;
    }
  }

  // Ajouter nouveau pair
  if (pSystem.count >= pSystem.capacity) {
    Pair *new_pairs =
        realloc(pSystem.pairs, pSystem.capacity * 2 * sizeof(Pair));
    if (!new_pairs)
      return -1;
    pSystem.pairs = new_pairs;
    pSystem.capacity *= 2;
  }

  pSystem.pairs[pSystem.count].id = id;
  pSystem.pairs[pSystem.count].ip = ip;
  pSystem.pairs[pSystem.count].port = port;
  pSystem.pairs[pSystem.count].active = 1;
  pSystem.count++;

  return 0;
}
