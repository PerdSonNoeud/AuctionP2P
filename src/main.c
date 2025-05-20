#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <poll.h>
#include "include/auction.h"
#include "include/multicast.h"

// Variables globales
int running = 1;
int recv_sock = -1;
int send_sock = -1;
extern PairSystem pSystem;  // Déclarer pSystem comme externe

// Fonction pour créer un nouveau réseau P2P
void create_network() {
    printf("Création d'un nouveau réseau P2P...\n");

    // Initialiser le système de pairs si ce n'est pas déjà fait
    if (init_pairs() < 0) {
        fprintf(stderr, "Échec de l'initialisation du système de pairs\n");
        return;
    }

    // Configurer le socket de réception multicast
    recv_sock = setup_multicast_receiver(pSystem.liaison_addr, pSystem.liaison_port);
    if (recv_sock < 0) {
        fprintf(stderr, "Échec de la création du socket récepteur multicast\n");
        return;
    }

    // Configurer le socket d'envoi pour répondre aux requêtes
    send_sock = setup_multicast_sender();
    if (send_sock < 0) {
        fprintf(stderr, "Échec de la création du socket émetteur multicast\n");
        close(recv_sock);
        return;
    }

    // Afficher les infos du nœud actuel
    char my_ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &pSystem.my_ip, my_ip_str, sizeof(my_ip_str));
    printf("\nNœud P2P créé avec succès:\n");
    printf("ID: %d\n", pSystem.my_id);
    printf("IP: %s\n", my_ip_str);
    printf("Port: %d\n", pSystem.my_port);
    printf("Adresse multicast: %s:%d\n", pSystem.liaison_addr, pSystem.liaison_port);
    printf("\nEn attente de connexions...\n");

    // Configuration pour poll
    struct pollfd fds[2];
    
    // Surveillance du socket réseau
    fds[0].fd = recv_sock;
    fds[0].events = POLLIN;
    
    // Surveillance de l'entrée standard (pour détecter la commande de sortie)
    fds[1].fd = STDIN_FILENO;
    fds[1].events = POLLIN;
    
    printf("\nAppuyez sur 'q' et Entrée pour quitter le programme\n");
    
    running = 1;
    while (running) {
        int poll_result = poll(fds, 2, 1000); // Timeout de 1 seconde
        
        if (poll_result < 0) {
            perror("Erreur lors de l'appel à poll");
            break;
        }
        
        // Vérifier si des données sont disponibles sur le socket réseau
        if (fds[0].revents & POLLIN) {
            int result = handle_join(recv_sock);
            if (result > 0) {
                printf("Demande de connexion reçue et traitée\n");
                
                // Afficher les pairs connectés
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
        
        // Vérifier si des données sont disponibles sur stdin
        if (fds[1].revents & POLLIN) {
            char input;
            if (read(STDIN_FILENO, &input, 1) > 0) {
                if (input == 'q' || input == 'Q') {
                    running = 0;
                }
            }
        }
    }

    // Petit délai avant de fermer les sockets pour éviter les problèmes de réutilisation
    sleep(1);

    // Fermer les sockets
    close(recv_sock);
    close(send_sock);

    printf("Réseau P2P fermé\n");
}

// Fonction pour rejoindre un réseau P2P existant
void join_network() {
    printf("Tentative de connexion à un réseau P2P existant...\n");

    // Initialiser le système de pairs si ce n'est pas déjà fait
    if (init_pairs() < 0) {
        fprintf(stderr, "Échec de l'initialisation du système de pairs\n");
        return;
    }

    // Personnaliser l'ID (optionnel)
    printf("Entrez votre ID souhaité (laissez vide pour défaut %d): ", pSystem.my_id);
    char id_str[10];
    if (fgets(id_str, sizeof(id_str), stdin) != NULL) {
        // Enlever le \n final
        id_str[strcspn(id_str, "\n")] = 0;
        if (strlen(id_str) > 0) {
            pSystem.my_id = (unsigned short)atoi(id_str);
        }
    }

    printf("Tentative de connexion avec ID=%d...\n", pSystem.my_id);

    // Essayer de rejoindre le réseau
    if (join_auction() < 0) {
        fprintf(stderr, "Échec de la connexion au réseau P2P\n");
        return;
    }

    printf("\nConnexion au réseau P2P réussie!\n");

    // Afficher les données du réseau
    printf("\nPairs actuellement connectés: %d\n", pSystem.count);
    for (int i = 0; i < pSystem.count; i++) {
        char ip_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &pSystem.pairs[i].ip, ip_str, sizeof(ip_str));
        printf("Pair %d: ID=%d, IP=%s, Port=%d, Actif=%s\n",
               i+1, pSystem.pairs[i].id, ip_str, pSystem.pairs[i].port,
               pSystem.pairs[i].active ? "Oui" : "Non");
    }

    // Configurer le socket de réception pour recevoir les messages du réseau
    recv_sock = setup_multicast_receiver(pSystem.liaison_addr, pSystem.liaison_port);
    if (recv_sock < 0) {
        fprintf(stderr, "Échec de la création du socket récepteur multicast\n");
        return;
    }

    // Configurer le socket d'envoi pour participer au réseau
    send_sock = setup_multicast_sender();
    if (send_sock < 0) {
        fprintf(stderr, "Échec de la création du socket émetteur multicast\n");
        close(recv_sock);
        return;
    }

    // Configuration pour poll
    struct pollfd fds[2];
    
    // Surveillance du socket réseau
    fds[0].fd = recv_sock;
    fds[0].events = POLLIN;
    
    // Surveillance de l'entrée standard (pour détecter la commande de sortie)
    fds[1].fd = STDIN_FILENO;
    fds[1].events = POLLIN;
    
    // Attendre l'entrée utilisateur pour quitter
    printf("\nVous êtes maintenant connecté au réseau P2P\n");
    printf("Appuyez sur 'q' et Entrée pour quitter le programme\n");

    running = 1;
    while (running) {
        int poll_result = poll(fds, 2, 1000); // Timeout de 1 seconde
        
        if (poll_result < 0) {
            perror("Erreur lors de l'appel à poll");
            break;
        }
        
        // Vérifier si des données sont disponibles sur le socket réseau
        if (fds[0].revents & POLLIN) {
            int result = handle_join(recv_sock);
            if (result > 0) {
                printf("Demande de connexion reçue et traitée\n");
                
                // Afficher les pairs connectés
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
        
        // Vérifier si des données sont disponibles sur stdin
        if (fds[1].revents & POLLIN) {
            char input;
            if (read(STDIN_FILENO, &input, 1) > 0) {
                if (input == 'q' || input == 'Q') {
                    running = 0;
                }
            }
        }
    }

    // Petit délai avant de fermer les sockets pour éviter les problèmes de réutilisation
    sleep(1);

    // Fermer les sockets
    close(recv_sock);
    close(send_sock);

    printf("Déconnexion du réseau P2P\n");
}

// Fonction principale
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

    // Consommer le retour à la ligne
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
