#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include "include/auction.h"
#include "include/multicast.h"
#include "include/message.h"
#include "include/utils.h"

// Global variables
int running = 1;
int recv_sock = -1;
int send_sock = -1;
int auction_recv_sock = -1;  // Socket pour recevoir les messages d'enchère
extern struct PairSystem pSystem;  // Declare pSystem as external
extern struct AuctionSystem auctionSys;  // Declare auctionSys as external
extern pthread_mutex_t auction_mutex;  // Declare auction_mutex as external

// Déclaration des fonctions
void sync_auctions();

// Fonction pour traiter les messages d'enchère reçus
int handle_auction_message(int sock) {
    struct sockaddr_in6 sender;
    char buffer[2048]; // Buffer statique avec taille augmentée
    memset(buffer, 0, sizeof(buffer));

    int len = receive_multicast(sock, buffer, sizeof(buffer), &sender);
    if (len <= 0) {
        return 0; // Pas de données ou erreur
    }

    // Mémoriser les derniers messages pour éviter les doublons
    static char last_message[2048] = {0};
    static time_t last_message_time = 0;
    time_t now = time(NULL);
    
    // Si c'est exactement le même message reçu dans les 2 dernières secondes, l'ignorer
    if (strcmp(buffer, last_message) == 0 && difftime(now, last_message_time) < 2) {
        return 0; // Ignorer les doublons récents
    }
    
    // Mémoriser ce message
    strncpy(last_message, buffer, sizeof(last_message) - 1);
    last_message_time = now;
    
    // Affichage réduit des données reçues pour débogage
    printf("Message d'enchère reçu (%d octets)\n", len);
    
    struct message *msg = malloc(sizeof(struct message));
    if (msg == NULL) {
        perror("malloc a échoué");
        return -1;
    }

    // Initialiser les champs pour éviter les problèmes de mémoire
    msg->mess = NULL;
    msg->sig = NULL;

    // Extraire les informations du message
    if (buffer_to_message(msg, buffer) < 0) {
        perror("buffer_to_message a échoué");
        free_message(msg);
        return -1;
    }

    // Affichage simplifié du message
    printf("Message code: %d, ID: %d", msg->code, msg->id);
    if (msg->code == CODE_NOUVELLE_VENTE || msg->code == CODE_ENCHERE || 
        msg->code == CODE_ENCHERE_SUPERVISEUR || msg->code == CODE_FIN_VENTE) {
        printf(", NUMV: %u, PRIX: %u", msg->numv, msg->prix);
    }
    printf("\n");

    // Obtenir l'adresse IP de l'expéditeur sous forme de chaîne
    char sender_ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &sender.sin6_addr, sender_ip_str, sizeof(sender_ip_str));
    
    // Obtenir notre adresse IP sous forme de chaîne
    char my_ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &pSystem.my_ip, my_ip_str, sizeof(my_ip_str));

    // Pour les nouvelles enchères, nous voulons traiter même nos propres messages pour assurer la cohérence
    if (msg->code != CODE_NOUVELLE_VENTE && msg->id == pSystem.my_id && 
        strcmp(sender_ip_str, my_ip_str) == 0 &&
        msg->code != CODE_ENCHERE && 
        msg->code != CODE_ENCHERE_SUPERVISEUR) {
        printf("Message d'enchère ignoré : c'est notre propre message (code %d)\n", msg->code);
        free_message(msg);
        return 0;
    }

    // Traiter le message selon son code
    switch (msg->code) {
        case CODE_NOUVELLE_VENTE:  // Code 8 - Nouvelle vente
            printf("Nouvelle vente reçue - ID: %d, NUMV: %u, PRIX: %u\n", 
                   msg->id, msg->numv, msg->prix);
            
            // Créer une structure pour le créateur
            struct Pair creator;
            creator.id = msg->id;
            creator.ip = sender.sin6_addr;
            creator.port = ntohs(sender.sin6_port);
            creator.active = 1;
            
            // Chercher si cette enchère existe déjà
            struct Auction *existing = find_auction(msg->numv);
            if (!existing) {
                // L'enchère n'existe pas encore, on la crée avec l'ID spécifié
                printf("Création d'une nouvelle enchère avec ID=%u, prix=%u\n", msg->numv, msg->prix);
                unsigned int auction_id = init_auction_with_id(&creator, msg->prix, msg->numv);
                
                if (auction_id == 0) {
                    printf("Erreur: Échec de la création de l'enchère %u\n", msg->numv);
                } else {
                    printf("Enchère %u ajoutée au système\n", msg->numv);
                    // Vérifier que l'enchère est bien dans le système
                    existing = find_auction(msg->numv);
                    if (existing) {
                        printf("Enchère vérifiée dans le système: ID=%u, prix=%u, créateur=%d\n",
                              existing->auction_id, existing->current_price, existing->creator_id);
                    } else {
                        printf("ERREUR: Impossible de trouver l'enchère %u après sa création!\n", msg->numv);
                    }
                }
            } else {
                printf("L'enchère %u existe déjà dans le système (prix=%u, créateur=%d)\n", 
                       existing->auction_id, existing->current_price, existing->creator_id);
            }
            break;
            
        case CODE_ENCHERE:  // Code 9 - Enchère d'un pair
            printf("Enchère reçue - ID: %d, NUMV: %u, PRIX: %u\n", 
                   msg->id, msg->numv, msg->prix);
            handle_bid(msg);
            break;
            
        case CODE_ENCHERE_SUPERVISEUR:  // Code 10 - Enchère relayée par le superviseur
            printf("Enchère relayée par le superviseur - ID: %d, NUMV: %u, PRIX: %u\n", 
                   msg->id, msg->numv, msg->prix);
            handle_supervisor_bid(msg);
            break;
            
        case CODE_FIN_VENTE_WARNING:  // Code 11 - Avertissement de fin de vente
            printf("Avertissement de fin de vente - ID: %d, NUMV: %u, PRIX: %u\n", 
                   msg->id, msg->numv, msg->prix);
            break;
            
        case CODE_FIN_VENTE:  // Code 12 - Fin de vente
            printf("Fin de vente - ID gagnant: %d, NUMV: %u, PRIX final: %u\n", 
                   msg->id, msg->numv, msg->prix);
            break;
            
        case CODE_QUIT_SYSTEME:  // Code 13 - Un pair quitte le système
            printf("Un pair quitte le système - ID: %d\n", msg->id);
            // Marquer le pair comme inactif
            for (int i = 0; i < pSystem.count; i++) {
                if (pSystem.pairs[i].id == msg->id) {
                    pSystem.pairs[i].active = 0;
                    printf("Pair %d marqué comme inactif\n", msg->id);
                    break;
                }
            }
            break;
            
        case CODE_REFUS_PRIX:  // Code 15 - Refus d'enchère pour prix invalide
            printf("Refus d'enchère pour prix invalide - ID: %d, NUMV: %u, PRIX: %u\n", 
                   msg->id, msg->numv, msg->prix);
            break;
            
        default:
            printf("Message d'enchère inconnu reçu - Code: %d\n", msg->code);
    }

    free_message(msg);
    return 1;
}

// Fonction pour créer une nouvelle enchère
void create_auction() {
    if (init_auction_system() < 0) {
        fprintf(stderr, "Échec de l'initialisation du système d'enchères\n");
        return;
    }

    unsigned int initial_price;
    printf("Entrez le prix initial de l'enchère: ");
    scanf("%u", &initial_price);
    while(getchar() != '\n'); // Vider le buffer d'entrée

    // Créer une structure pour le créateur
    struct Pair creator;
    creator.id = pSystem.my_id;
    creator.ip = pSystem.my_ip;
    creator.port = pSystem.my_port;
    creator.active = 1;

    // Initialiser et démarrer l'enchère
    unsigned int auction_id = init_auction(&creator, initial_price);
    if (auction_id == 0) {
        fprintf(stderr, "Échec de la création de l'enchère\n");
        return;
    }

    printf("Enchère %u créée avec succès\n", auction_id);
    start_auction(auction_id);
}

// Fonction pour faire une offre sur une enchère
void make_bid() {
    unsigned int auction_id;
    unsigned int price;
    
    // Afficher les enchères disponibles
    printf("\nEnchères actives:\n");
    int active_auctions = 0;
    for (int i = 0; i < auctionSys.count; i++) {
        struct Auction *auction = &auctionSys.auctions[i];
        if (!is_auction_finished(auction->auction_id)) {
            printf("%d. ID: %u, Prix actuel: %u, Créateur: %d\n", 
                   i+1, auction->auction_id, auction->current_price, auction->creator_id);
            active_auctions++;
        }
    }
    
    if (active_auctions == 0) {
        printf("Aucune enchère active disponible\n");
        return;
    }
    
    printf("\nEntrez l'ID de l'enchère: ");
    scanf("%u", &auction_id);
    while(getchar() != '\n'); // Vider le buffer d'entrée
    
    // Vérifier que l'enchère existe
    struct Auction *auction = find_auction(auction_id);
    if (!auction) {
        fprintf(stderr, "Erreur: Enchère %u introuvable\n", auction_id);
        return;
    }
    
    // Vérifier que l'enchère n'est pas terminée
    if (is_auction_finished(auction_id)) {
        fprintf(stderr, "Erreur: L'enchère %u est terminée\n", auction_id);
        return;
    }
    
    printf("Prix actuel: %u\n", auction->current_price);
    printf("Entrez votre prix: ");
    scanf("%u", &price);
    while(getchar() != '\n'); // Vider le buffer d'entrée
    
    // Vérifier que le prix est supérieur au prix actuel
    if (price <= auction->current_price) {
        fprintf(stderr, "Erreur: Le prix doit être supérieur au prix actuel\n");
        return;
    }
    
    // Envoyer l'enchère
    int send_sock = setup_multicast_sender();
    if (send_sock < 0) {
        perror("Échec de création du socket d'envoi multicast");
        return;
    }
    
    struct message *msg = init_message(CODE_ENCHERE);
    if (!msg) {
        perror("Échec de l'initialisation du message");
        close(send_sock);
        return;
    }
    
    msg->id = pSystem.my_id;
    msg->numv = auction_id;
    msg->prix = price;
    
    int buffer_size = get_buffer_size(msg);
    char *buffer = malloc(buffer_size);
    if (!buffer) {
        perror("Échec de l'allocation du buffer");
        free_message(msg);
        close(send_sock);
        return;
    }
    
    if (message_to_buffer(msg, buffer, buffer_size) < 0) {
        perror("Échec de la conversion du message en buffer");
        free(buffer);
        free_message(msg);
        close(send_sock);
        return;
    }
    
    if (send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size) < 0) {
        perror("Échec de l'envoi de l'enchère");
    } else {
        printf("Enchère envoyée: %u sur l'enchère %u\n", price, auction_id);
        
        // Simuler localement la mise à jour du prix pour affichage immédiat
        pthread_mutex_lock(&auction_mutex);
        auction->current_price = price;
        auction->id_dernier_prop = pSystem.my_id;
        auction->last_bid_time = time(NULL);
        
        printf("Prix local mis à jour, en attente de confirmation du superviseur...\n");
        pthread_mutex_unlock(&auction_mutex);
        
        // Afficher les informations mises à jour
        display_auctions();
    }
    
    free(buffer);
    free_message(msg);
    close(send_sock);
}

// Fonction pour afficher les enchères actives
void display_auctions() {
    printf("\n=== Enchères actives ===\n");
    int active_count = 0;
    
    for (int i = 0; i < auctionSys.count; i++) {
        struct Auction *auction = &auctionSys.auctions[i];
        if (!is_auction_finished(auction->auction_id)) {
            printf("ID: %u, Prix actuel: %u, Créateur: %d\n", 
                   auction->auction_id, auction->current_price, auction->creator_id);
            active_count++;
        }
    }
    
    if (active_count == 0) {
        printf("Aucune enchère active\n");
    }
    
    printf("\n=== Enchères terminées ===\n");
    int finished_count = 0;
    
    for (int i = 0; i < auctionSys.count; i++) {
        struct Auction *auction = &auctionSys.auctions[i];
        if (is_auction_finished(auction->auction_id)) {
            printf("ID: %u, Prix final: %u, Gagnant: %d\n", 
                   auction->auction_id, auction->current_price, auction->id_dernier_prop);
            finished_count++;
        }
    }
    
    if (finished_count == 0) {
        printf("Aucune enchère terminée\n");
    }
}

// Fonction pour créer un nouveau réseau P2P
void create_network() {
    printf("Création d'un nouveau réseau P2P...\n");

    // Initialiser le système de pairs s'il ne l'est pas déjà
    if (init_pairs() < 0) {
        fprintf(stderr, "Échec de l'initialisation du système de pairs\n");
        return;
    }
    
    // Initialiser le système d'enchères
    if (init_auction_system() < 0) {
        fprintf(stderr, "Échec de l'initialisation du système d'enchères\n");
        return;
    }

    // Configurer le socket de réception multicast pour la liaison
    recv_sock = setup_multicast_receiver(pSystem.liaison_addr, pSystem.liaison_port);
    if (recv_sock < 0) {
        fprintf(stderr, "Échec de la création du socket récepteur multicast pour liaison\n");
        return;
    }
    
    // Configurer le socket de réception multicast pour les enchères
    auction_recv_sock = setup_multicast_receiver(pSystem.auction_addr, pSystem.auction_port);
    if (auction_recv_sock < 0) {
        fprintf(stderr, "Échec de la création du socket récepteur multicast pour enchères\n");
        close(recv_sock);
        return;
    }

    // Configurer le socket d'émission
    send_sock = setup_multicast_sender();
    if (send_sock < 0) {
        fprintf(stderr, "Échec de la création du socket émetteur multicast\n");
        close(recv_sock);
        close(auction_recv_sock);
        return;
    }

    // Afficher les informations du nœud actuel
    char my_ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &pSystem.my_ip, my_ip_str, sizeof(my_ip_str));
    printf("\nNœud P2P créé avec succès:\n");
    printf("ID: %d\n", pSystem.my_id);
    printf("IP: %s\n", my_ip_str);
    printf("Port: %d\n", pSystem.my_port);
    printf("Adresse multicast liaison: %s:%d\n", pSystem.liaison_addr, pSystem.liaison_port);
    printf("Adresse multicast enchères: %s:%d\n", pSystem.auction_addr, pSystem.auction_port);
    
    // Afficher le nombre de descripteurs ouverts
    print_open_files();
    
    printf("\nEn attente de connexions et d'enchères...\n");

    // Configuration pour poll
    struct pollfd fds[3];
    
    // Socket 1: monitorer le socket réseau de liaison
    fds[0].fd = recv_sock;
    fds[0].events = POLLIN;
    
    // Socket 2: monitorer le socket réseau d'enchères
    fds[1].fd = auction_recv_sock;
    fds[1].events = POLLIN;
    
    // Socket 3: monitorer l'entrée standard (pour détecter les commandes utilisateur)
    fds[2].fd = STDIN_FILENO;
    fds[2].events = POLLIN;
    
    printf("\nCommandes disponibles:\n");
    printf("1 - Créer une enchère\n");
    printf("2 - Faire une offre\n");
    printf("3 - Afficher les enchères actives\n");
    printf("q - Quitter le programme\n");
    printf("> ");
    fflush(stdout);
    
    running = 1;
    while (running) {
        int poll_result = poll(fds, 3, 1000); // 1 seconde de timeout

        if (poll_result < 0) {
            perror("Erreur lors de l'appel à poll");
            break;
        }

        // Vérifier si des données sont disponibles sur le socket de liaison
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
                printf("> ");
                fflush(stdout);
            }
        }
        
        // Vérifier si des données sont disponibles sur le socket d'enchères
        if (fds[1].revents & POLLIN) {
            int result = handle_auction_message(auction_recv_sock);
            if (result > 0) {
                printf("> ");
                fflush(stdout);
            }
        }

        // Vérifier si des données sont disponibles sur l'entrée standard
        if (fds[2].revents & POLLIN) {
            char input[10];
            if (fgets(input, sizeof(input), stdin) != NULL) {
                input[strcspn(input, "\n")] = 0; // Supprimer le saut de ligne
                
                if (input[0] == 'q' || input[0] == 'Q') {
                    // Quitter le système d'enchères proprement
                    quit_auction_system();
                    running = 0;
                } else if (input[0] == '1') {
                    // Créer une enchère
                    create_auction();
                    printf("> ");
                    fflush(stdout);
                } else if (input[0] == '2') {
                    // Faire une offre
                    make_bid();
                    printf("> ");
                    fflush(stdout);
                } else if (input[0] == '3') {
                    // Afficher les enchères
                    display_auctions();
                    printf("> ");
                    fflush(stdout);
                } else {
                    printf("Commande inconnue. Commandes disponibles:\n");
                    printf("1 - Créer une enchère\n");
                    printf("2 - Faire une offre\n");
                    printf("3 - Afficher les enchères actives\n");
                    printf("q - Quitter le programme\n");
                    printf("> ");
                    fflush(stdout);
                }
            }
        }
    }

    // Petit délai avant de fermer les sockets pour éviter les problèmes de réutilisation
    sleep(1);

    // Fermer les sockets
    close(recv_sock);
    close(auction_recv_sock);
    close(send_sock);
    
    // Vérifier les descripteurs de fichiers après fermeture
    printf("Après fermeture des sockets:\n");
    print_open_files();
    
    // Nettoyer le système d'enchères
    cleanup_auction_system();

    printf("Réseau P2P fermé\n");
}

// Fonction pour rejoindre un réseau P2P existant
void join_network() {
    printf("Tentative de connexion à un réseau P2P existant...\n");

    // Initialiser le système de pairs s'il ne l'est pas déjà
    if (init_pairs() < 0) {
        fprintf(stderr, "Échec de l'initialisation du système de pairs\n");
        return;
    }

    // Initialiser le système d'enchères
    if (init_auction_system() < 0) {
        fprintf(stderr, "Échec de l'initialisation du système d'enchères\n");
        return;
    }

    // Personnaliser l'ID (optionnel)
    printf("Entrez votre ID souhaité (laissez vide pour défaut %d): ", pSystem.my_id);
    char id_str[10];
    if (fgets(id_str, sizeof(id_str), stdin) != NULL) {
        // Supprimer le saut de ligne
        id_str[strcspn(id_str, "\n")] = 0;
        if (strlen(id_str) > 0) {
            pSystem.my_id = (unsigned short)atoi(id_str);
        }
    }

    printf("Tentative de connexion avec ID=%d...\n", pSystem.my_id);

    // Configurer le socket de réception multicast pour la liaison tout de suite
    recv_sock = setup_multicast_receiver(pSystem.liaison_addr, pSystem.liaison_port);
    if (recv_sock < 0) {
        fprintf(stderr, "Échec de la création du socket récepteur multicast pour liaison\n");
        return;
    }

    // Configurer le socket de réception multicast pour les enchères tout de suite également
    auction_recv_sock = setup_multicast_receiver(pSystem.auction_addr, pSystem.auction_port);
    if (auction_recv_sock < 0) {
        fprintf(stderr, "Échec de la création du socket récepteur multicast pour enchères\n");
        close(recv_sock);
        return;
    }

    // Configurer le socket d'émission pour participer au réseau
    send_sock = setup_multicast_sender();
    if (send_sock < 0) {
        fprintf(stderr, "Échec de la création du socket émetteur multicast\n");
        close(recv_sock);
        close(auction_recv_sock);
        return;
    }

    // Essayer de rejoindre le réseau
    if (join_auction() < 0) {
        fprintf(stderr, "Échec de la connexion au réseau P2P\n");
        close(recv_sock);
        close(auction_recv_sock);
        close(send_sock);
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
    
    // Afficher le nombre de descripteurs ouverts
    print_open_files();

    // Attendre explicitement les messages d'enchères pendant quelques secondes
    printf("Attente des informations d'enchères en cours...\n");
    time_t start_time = time(NULL);
    struct sockaddr_in6 sender;
    
    // Attendre jusqu'à 5 secondes pour recevoir des messages d'enchères
    while (difftime(time(NULL), start_time) < 5) {
        char buffer[1024];
        memset(buffer, 0, sizeof(buffer));
        
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(auction_recv_sock, &readfds);
        
        // Timeout de 0.5 secondes
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000;
        
        int select_result = select(auction_recv_sock + 1, &readfds, NULL, NULL, &tv);
        if (select_result > 0 && FD_ISSET(auction_recv_sock, &readfds)) {
            int len = receive_multicast(auction_recv_sock, buffer, sizeof(buffer), &sender);
            if (len > 0) {
                // Traiter le message d'enchère
                handle_auction_message(auction_recv_sock);
            }
        }
    }
    
    printf("Synchronisation terminée.\n");
    
    // Configuration pour poll
    struct pollfd fds[3];
    
    // Socket 1: monitorer le socket réseau de liaison
    fds[0].fd = recv_sock;
    fds[0].events = POLLIN;
    
    // Socket 2: monitorer le socket réseau d'enchères
    fds[1].fd = auction_recv_sock;
    fds[1].events = POLLIN;
    
    // Socket 3: monitorer l'entrée standard (pour détecter les commandes utilisateur)
    fds[2].fd = STDIN_FILENO;
    fds[2].events = POLLIN;
    
    // Attendre l'entrée utilisateur pour quitter
    printf("\nVous êtes maintenant connecté au réseau P2P\n");
    printf("\nCommandes disponibles:\n");
    printf("1 - Créer une enchère\n");
    printf("2 - Faire une offre\n");
    printf("3 - Afficher les enchères actives\n");
    printf("4 - Synchroniser les enchères\n");
    printf("q - Quitter le programme\n");
    printf("> ");
    fflush(stdout);

    running = 1;
    while (running) {
        int poll_result = poll(fds, 3, 1000); // 1 seconde de timeout
        
        if (poll_result < 0) {
            perror("Erreur lors de l'appel à poll");
            break;
        }
        
        // Vérifier si des données sont disponibles sur le socket de liaison
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
                printf("> ");
                fflush(stdout);
            }
        }
        
        // Vérifier si des données sont disponibles sur le socket d'enchères
        if (fds[1].revents & POLLIN) {
            int result = handle_auction_message(auction_recv_sock);
            if (result > 0) {
                printf("> ");
                fflush(stdout);
            }
        }
        
        // Vérifier si des données sont disponibles sur l'entrée standard
        if (fds[2].revents & POLLIN) {
            char input[10];
            if (fgets(input, sizeof(input), stdin) != NULL) {
                input[strcspn(input, "\n")] = 0; // Supprimer le saut de ligne
                
                if (input[0] == 'q' || input[0] == 'Q') {
                    // Quitter le système d'enchères proprement
                    quit_auction_system();
                    running = 0;
                } else if (input[0] == '1') {
                    // Créer une enchère
                    create_auction();
                    printf("> ");
                    fflush(stdout);
                } else if (input[0] == '2') {
                    // Faire une offre
                    make_bid();
                    printf("> ");
                    fflush(stdout);
                } else if (input[0] == '3') {
                    // Afficher les enchères
                    display_auctions();
                    printf("> ");
                    fflush(stdout);
                } else if (input[0] == '4') {
                    // Synchroniser les enchères
                    sync_auctions();
                    printf("> ");
                    fflush(stdout);
                } else {
                    printf("Commande inconnue. Commandes disponibles:\n");
                    printf("1 - Créer une enchère\n");
                    printf("2 - Faire une offre\n");
                    printf("3 - Afficher les enchères actives\n");
                    printf("4 - Synchroniser les enchères\n");
                    printf("q - Quitter le programme\n");
                    printf("> ");
                    fflush(stdout);
                }
            }
        }
    }

    // Petit délai avant de fermer les sockets pour éviter les problèmes de réutilisation
    sleep(1);

    // Fermer les sockets
    close(recv_sock);
    close(auction_recv_sock);
    close(send_sock);
    
    // Vérifier les descripteurs de fichiers après fermeture
    printf("Après fermeture des sockets:\n");
    print_open_files();
    
    // Nettoyer le système d'enchères
    cleanup_auction_system();

    printf("Déconnexion du réseau P2P\n");
}

// Fonction pour synchroniser manuellement les enchères
void sync_auctions() {
    printf("\n=== Synchronisation des enchères ===\n");
    int count = broadcast_all_auctions();
    
    if (count > 0) {
        printf("%d enchères diffusées avec succès\n", count);
    } else if (count == 0) {
        printf("Aucune enchère à synchroniser\n");
    } else {
        printf("Erreur lors de la synchronisation des enchères\n");
    }
}

// Main function
int main() {
    int choice;

    printf("=== Application P2P d'Enchères ===\n\n");
    printf("1. Créer un nouveau réseau P2P\n");
    printf("2. Rejoindre un réseau P2P existant\n");
    printf("0. Quitter\n");
    printf("\nVotre choix: ");

    if (scanf("%d", &choice) != 1) {
        fprintf(stderr, "Entrée invalide\n");
        return EXIT_FAILURE;
    }

    // Consumer le saut de ligne
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
