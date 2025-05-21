#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <poll.h>
#include "include/auction.h"
#include "include/multicast.h"

// Global variables
int running = 1;
int recv_sock = -1;
int send_sock = -1;
extern struct PairSystem pSystem;  // Declare pSystem as external

// Function to create a new P2P network
void create_network() {
    printf("Création d'un nouveau réseau P2P...\n");

    // Initialize the peer system if not already done
    if (init_pairs() < 0) {
        fprintf(stderr, "Échec de l'initialisation du système de pairs\n");
        return;
    }

    // Configure multicast receiver socket
    recv_sock = setup_multicast_receiver(pSystem.liaison_addr, pSystem.liaison_port);
    if (recv_sock < 0) {
        fprintf(stderr, "Échec de la création du socket récepteur multicast\n");
        return;
    }

    // Configure sender socket to respond to requests
    send_sock = setup_multicast_sender();
    if (send_sock < 0) {
        fprintf(stderr, "Échec de la création du socket émetteur multicast\n");
        close(recv_sock);
        return;
    }

    // Display current node info
    char my_ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &pSystem.my_ip, my_ip_str, sizeof(my_ip_str));
    printf("\nNœud P2P créé avec succès:\n");
    printf("ID: %d\n", pSystem.my_id);
    printf("IP: %s\n", my_ip_str);
    printf("Port: %d\n", pSystem.my_port);
    printf("Adresse multicast: %s:%d\n", pSystem.liaison_addr, pSystem.liaison_port);
    printf("\nEn attente de connexions...\n");

    // Configuration for poll
    struct pollfd fds[2];
    
    // Monitor network socket
    fds[0].fd = recv_sock;
    fds[0].events = POLLIN;
    
    // Monitor standard input (to detect exit command)
    fds[1].fd = STDIN_FILENO;
    fds[1].events = POLLIN;
    
    printf("\nAppuyez sur 'q' et Entrée pour quitter le programme\n");
    
    running = 1;
    while (running) {
        int poll_result = poll(fds, 2, 1000); // 1 second timeout

        if (poll_result < 0) {
            perror("Erreur lors de l'appel à poll");
            break;
        }

        // Check if data is available on the network socket
        if (fds[0].revents & POLLIN) {
            int result = handle_join(recv_sock);
            if (result > 0) {
                printf("Demande de connexion reçue et traitée\n");
                
                // Display connected peers
                printf("\nPairs actuellement connectés: %d\n", pSystem.count);
                for (int i = 0; i < pSystem.count; i++) {
                    char ip_str[INET6_ADDRSTRLEN];
                    inet_ntop(AF_INET6, &pSystem.pairs[i].ip, ip_str, sizeof(ip_str));
                    printf("Pair %d: ID=%d, IP=%s, Port=%d, Actif=%s\n",
                           i+1, pSystem.pairs[i].id, ip_str, pSystem.pairs[i].port,
                           pSystem.pairs[i].active ? "Oui" : "Non");
                }
            }
        }

        // Check if data is available on stdin
        if (fds[1].revents & POLLIN) {
            char input;
            if (read(STDIN_FILENO, &input, 1) > 0) {
                if (input == 'q' || input == 'Q') {
                    running = 0;
                }
            }
        }
    }

    // Small delay before closing sockets to avoid reuse issues
    sleep(1);

    // Close sockets
    close(recv_sock);
    close(send_sock);

    printf("Réseau P2P fermé\n");
}

// Function to join an existing P2P network
void join_network() {
    printf("Tentative de connexion à un réseau P2P existant...\n");

    // Initialize the peer system if not already done
    if (init_pairs() < 0) {
        fprintf(stderr, "Échec de l'initialisation du système de pairs\n");
        return;
    }

    // Customize ID (optional)
    printf("Entrez votre ID souhaité (laissez vide pour défaut %d): ", pSystem.my_id);
    char id_str[10];
    if (fgets(id_str, sizeof(id_str), stdin) != NULL) {
        // Remove trailing newline
        id_str[strcspn(id_str, "\n")] = 0;
        if (strlen(id_str) > 0) {
            pSystem.my_id = (unsigned short)atoi(id_str);
        }
    }

    printf("Tentative de connexion avec ID=%d...\n", pSystem.my_id);

    // Try to join the network
    if (join_auction() < 0) {
        fprintf(stderr, "Échec de la connexion au réseau P2P\n");
        return;
    }

    printf("\nConnexion au réseau P2P réussie!\n");

    // Display network data
    printf("\nPairs actuellement connectés: %d\n", pSystem.count);
    for (int i = 0; i < pSystem.count; i++) {
        char ip_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &pSystem.pairs[i].ip, ip_str, sizeof(ip_str));
        printf("Pair %d: ID=%d, IP=%s, Port=%d, Actif=%s\n",
               i+1, pSystem.pairs[i].id, ip_str, pSystem.pairs[i].port,
               pSystem.pairs[i].active ? "Oui" : "Non");
    }

    // Set up receiver socket to receive network messages
    recv_sock = setup_multicast_receiver(pSystem.liaison_addr, pSystem.liaison_port);
    if (recv_sock < 0) {
        fprintf(stderr, "Échec de la création du socket récepteur multicast\n");
        return;
    }

    // Set up sender socket to participate in the network
    send_sock = setup_multicast_sender();
    if (send_sock < 0) {
        fprintf(stderr, "Échec de la création du socket émetteur multicast\n");
        close(recv_sock);
        return;
    }

    // Configuration for poll
    struct pollfd fds[2];
    
    // Monitor network socket
    fds[0].fd = recv_sock;
    fds[0].events = POLLIN;
    
    // Monitor standard input (to detect exit command)
    fds[1].fd = STDIN_FILENO;
    fds[1].events = POLLIN;
    
    // Wait for user input to exit
    printf("\nVous êtes maintenant connecté au réseau P2P\n");
    printf("Appuyez sur 'q' et Entrée pour quitter le programme\n");

    running = 1;
    while (running) {
        int poll_result = poll(fds, 2, 1000); // 1 second timeout
        
        if (poll_result < 0) {
            perror("Erreur lors de l'appel à poll");
            break;
        }
        
        // Check if data is available on the network socket
        if (fds[0].revents & POLLIN) {
            int result = handle_join(recv_sock);
            if (result > 0) {
                printf("Demande de connexion reçue et traitée\n");
                
                // Display connected peers
                printf("\nPairs actuellement connectés: %d\n", pSystem.count);
                for (int i = 0; i < pSystem.count; i++) {
                    char ip_str[INET6_ADDRSTRLEN];
                    inet_ntop(AF_INET6, &pSystem.pairs[i].ip, ip_str, sizeof(ip_str));
                    printf("Pair %d: ID=%d, IP=%s, Port=%d, Actif=%s\n",
                           i+1, pSystem.pairs[i].id, ip_str, pSystem.pairs[i].port,
                           pSystem.pairs[i].active ? "Oui" : "Non");
                }
            }
        }
        
        // Check if data is available on stdin
        if (fds[1].revents & POLLIN) {
            char input;
            if (read(STDIN_FILENO, &input, 1) > 0) {
                if (input == 'q' || input == 'Q') {
                    running = 0;
                }
            }
        }
    }

    // Small delay before closing sockets to avoid reuse issues
    sleep(1);

    // Close sockets
    close(recv_sock);
    close(send_sock);

    printf("Déconnexion du réseau P2P\n");
}

// Main function
int main() {
    int choice;

    printf("=== Application P2P ===\n\n");
    printf("1. Créer un nouveau réseau P2P\n");
    printf("2. Rejoindre un réseau P2P existant\n");
    printf("0. Quitter\n");
    printf("\nVotre choix: ");

    if (scanf("%d", &choice) != 1) {
        fprintf(stderr, "Entrée invalide\n");
        return EXIT_FAILURE;
    }

    // Consume the newline
    while (getchar() != '\n');

    switch (choice) {
        case 0:
            printf("Au revoir!\n");
            break;

        case 1:
            create_network();
            break;

        case 2:
            join_network();
            break;

        default:
            fprintf(stderr, "Choix invalide\n");
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
