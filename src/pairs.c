#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include "include/pairs.h"
#include "include/multicast.h"

#define MAX_ATTEMPTS 3
#define TIMEOUT 5

PairSystem system;

int init_pairs() {
    system.pairs = malloc(10 * sizeof(Pair));
    if (!system.pairs) {
        perror("malloc failed");
        return -1;
    }
    
    system.count = 0;
    system.capacity = 10;
    system.my_id = 1;  // ID par défaut
    
    // Adresse IP par défaut
    inet_pton(AF_INET6, "::1", &system.my_ip);
    system.my_port = 8000;
    
    // Adresses multicast par défaut
    strcpy(system.liaison_addr, "ff02::1");
    system.liaison_port = 8080;
    strcpy(system.auction_addr, "ff02::2");
    system.auction_port = 8081;
    
    return 0;
}

int join_auction() {
    // Socket pour envoyer
    int send_sock = setup_multicast_sender();
    if (send_sock < 0) return -1;
    
    // Socket pour recevoir
    int recv_sock = setup_multicast_receiver(system.liaison_addr, system.liaison_port);
    if (recv_sock < 0) {
        close(send_sock);
        return -1;
    }

    //TODO : Envoyer une demande de rejoindre le système
    
    return 0;  // Système existant rejoint
}

int handle_join(int sock) {
    uint8_t buffer[100];
    struct sockaddr_in6 sender;
    
    int len = receive_multicast(sock, buffer, sizeof(buffer), &sender);
    if (len > 0 && buffer[0] == 3) {  // CODE = 3
        printf("Received join request\n");
        
        // Répondre avec l'ID et l'adresse
        
        // Socket pour répondre
        int send_sock = socket(AF_INET6, SOCK_DGRAM, 0);
        if (send_sock < 0) return -1;
        
        // Envoyer l'ID et l'adresse
        
        close(send_sock);
        return 1;
    }
    
    return 0;
}

int add_pair(unsigned short id, struct in6_addr ip, unsigned short port) {
    // Vérifier si déjà présent
    for (int i = 0; i < system.count; i++) {
        if (system.pairs[i].id == id) {
            system.pairs[i].ip = ip;
            system.pairs[i].port = port;
            system.pairs[i].active = 1;
            return 0;
        }
    }
    
    // Ajouter nouveau pair
    if (system.count >= system.capacity) {
        Pair *new_pairs = realloc(system.pairs, system.capacity * 2 * sizeof(Pair));
        if (!new_pairs) return -1;
        system.pairs = new_pairs;
        system.capacity *= 2;
    }
    
    system.pairs[system.count].id = id;
    system.pairs[system.count].ip = ip;
    system.pairs[system.count].port = port;
    system.pairs[system.count].active = 1;
    system.count++;
    
    return 0;
}