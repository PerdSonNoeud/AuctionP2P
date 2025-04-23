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
    
    // Message de demande (CODE = 3)
    uint8_t msg = 3;
    
    int attempt = 0;
    int found = 0;
    
    while (attempt < MAX_ATTEMPTS && !found) {
        // Envoyer demande
        send_multicast(send_sock, system.liaison_addr, system.liaison_port, &msg, sizeof(msg));
        printf("Sent join request (attempt %d)\n", attempt + 1);
        
        // Attendre réponse
        fd_set readfds;
        struct timeval tv;
        
        FD_ZERO(&readfds);
        FD_SET(recv_sock, &readfds);
        
        tv.tv_sec = TIMEOUT;
        tv.tv_usec = 0;
        
        if (select(recv_sock + 1, &readfds, NULL, NULL, &tv) > 0) {
            // Si on reçoit quelque chose
            uint8_t buffer[100];
            struct sockaddr_in6 sender;
            
            int len = receive_multicast(recv_sock, buffer, sizeof(buffer), &sender);
            if (len > 0 && buffer[0] == 4) {  // CODE = 4
                found = 1;
                printf("Found existing auction system!\n");
                
                // Extraire l'ID et l'adresse du pair qui a répondu
                unsigned short peer_id = buffer[1] | (buffer[2] << 8);
                struct in6_addr peer_ip;
                memcpy(&peer_ip, &buffer[3], 16);
                unsigned short peer_port = buffer[19] | (buffer[20] << 8);
                
                // Ajouter ce pair
                add_pair(peer_id, peer_ip, peer_port);
                
                // TODO: Implémenter connexion TCP
            }
        } else {
            attempt++;
        }
    }
    
    close(send_sock);
    close(recv_sock);
    
    if (!found) {
        printf("No auction system found, creating new one\n");
        // Ajouter moi-même comme premier pair
        add_pair(system.my_id, system.my_ip, system.my_port);
        return 1;  // Nouveau système créé
    }
    
    return 0;  // Système existant rejoint
}

int handle_join(int sock) {
    uint8_t buffer[100];
    struct sockaddr_in6 sender;
    
    int len = receive_multicast(sock, buffer, sizeof(buffer), &sender);
    if (len > 0 && buffer[0] == 3) {  // CODE = 3
        printf("Received join request\n");
        
        // Préparer réponse (CODE = 4)
        uint8_t response[21];
        response[0] = 4;
        response[1] = system.my_id & 0xFF;
        response[2] = (system.my_id >> 8) & 0xFF;
        memcpy(&response[3], &system.my_ip, 16);
        response[19] = system.my_port & 0xFF;
        response[20] = (system.my_port >> 8) & 0xFF;
        
        // Socket pour répondre
        int send_sock = socket(AF_INET6, SOCK_DGRAM, 0);
        if (send_sock < 0) return -1;
        
        // Envoyer réponse en unicast
        if (sendto(send_sock, response, sizeof(response), 0,
                  (struct sockaddr*)&sender, sizeof(sender)) < 0) {
            perror("sendto failed");
        }
        
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
    
    printf("Added peer with ID %d\n", id);
    return 0;
}