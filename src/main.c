#include "include/auction.h"
#include "include/message.h"
#include "include/sockets.h"
#include "include/utils.h"
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

// Global variables
int running = 1;
int m_recv = -1;
int m_send = -1;
int server_sock = -1;

int auc_sock = -1;                // Socket pour recevoir les messages d'enchère
extern struct PairSystem pSystem; // Declare pSystem as external
extern struct AuctionSystem auctionSys; // Declare auctionSys as external
extern pthread_mutex_t auction_mutex;   // Declare auction_mutex as external

void sync_auctions();
void display_auctions(); // Ajoutez cette ligne
void create_auction();   // Ajoutez aussi les autres pour la cohérence
void make_bid();
void create_network();
void sync_auctions();

// Fonction pour traiter les messages d'enchère reçus
int handle_auction_message(int sock) {
  struct sockaddr_in6 sender;
  char buffer[UNKNOWN_SIZE];
  memset(buffer, 0, sizeof(buffer));

  int len = receive_multicast(sock, buffer, sizeof(buffer), &sender);
  if (len <= 0)
    return 0; // No data or error

  struct message *msg = malloc(sizeof(struct message));
  if (msg == NULL) {
    perror("malloc a échoué");
    return -1;
  }

  if (buffer_to_message(msg, buffer) < 0) {
    perror("buffer_to_message a échoué");
    free_message(msg);
    return -1;
  }

  switch (msg->code) {
  case CODE_NOUVELLE_VENTE: // Code 8 - New auction     TODO : Move this to
                            // auction.c
    printf("Nouvelle vente reçue - ID: %d, NUMV: %u, PRIX: %u\n", msg->id,
           msg->numv, msg->prix);

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
      printf("Création d'une nouvelle enchère avec ID=%u, prix=%u\n", msg->numv,
             msg->prix);
      unsigned int auction_id =
          init_auction_with_id(&creator, msg->prix, msg->numv);

      if (auction_id == 0) {
        printf("Erreur: Échec de la création de l'enchère %u\n", msg->numv);
      } else {
        printf("Enchère %u ajoutée au système\n", msg->numv);
        // Vérifier que l'enchère est bien dans le système
        existing = find_auction(msg->numv);
        if (existing) {
          printf(
              "Enchère vérifiée dans le système: ID=%u, prix=%u, créateur=%d\n",
              existing->auction_id, existing->current_price,
              existing->creator_id);
        } else {
          printf(
              "ERREUR: Impossible de trouver l'enchère %u après sa création!\n",
              msg->numv);
        }
      }
    } else {
      printf(
          "L'enchère %u existe déjà dans le système (prix=%u, créateur=%d)\n",
          existing->auction_id, existing->current_price, existing->creator_id);
    }
    break;

  case CODE_ENCHERE: // Code 9 - Enchère d'un pair
    printf("Enchère reçue - ID: %d, NUMV: %u, PRIX: %u\n", msg->id, msg->numv,
           msg->prix);
    handle_bid(msg);
    break;

  case CODE_ENCHERE_SUPERVISEUR: // Code 10 - Enchère relayée par le superviseur
    printf("Enchère relayée par le superviseur - ID: %d, NUMV: %u, PRIX: %u\n",
           msg->id, msg->numv, msg->prix);
    handle_supervisor_bid(msg);
    break;

  case CODE_FIN_VENTE_WARNING: // Code 11 - Avertissement de fin de vente
    printf("Avertissement de fin de vente - ID: %d, NUMV: %u, PRIX: %u\n",
           msg->id, msg->numv, msg->prix);
    break;

  case CODE_FIN_VENTE: // Code 12 - Fin de vente
    printf("Fin de vente - ID gagnant: %d, NUMV: %u, PRIX final: %u\n", msg->id,
           msg->numv, msg->prix);
    break;
  }

  return 0;
}

// Fonction pour créer une nouvelle enchère     TODO : Move to auction.c
void create_auction() {
  if (auctionSys.auctions == NULL) {
    if (init_auction_system() < 0) {
      fprintf(stderr, "Échec de l'initialisation du système d'enchères\n");
      return;
    }
  }

  unsigned int initial_price;
  printf("Entrez le prix initial de l'enchère: ");
  if (scanf("%u", &initial_price) != 1) {
    fprintf(stderr, "Erreur de saisie du prix\n");
    while (getchar() != '\n')
      ; // Vider le buffer
    return;
  }
  while (getchar() != '\n')
    ; // Vider le buffer d'entrée

  printf("Prix initial: %u\n", initial_price);

  // Créer une structure pour le créateur
  struct Pair creator;
  creator.id = pSystem.my_id;
  creator.ip = pSystem.my_ip;
  creator.port = pSystem.my_port;
  creator.active = 1;

  printf("Créateur de l'enchère: ID=%d, IP=%s, PORT=%d\n", creator.id,
         inet_ntop(AF_INET6, &creator.ip, NULL, 0), creator.port);

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

  if (auctionSys.auctions == NULL) {
    fprintf(stderr, "Erreur: Système d'enchères non initialisé\n");
    return;
  }

  // Afficher les enchères disponibles
  printf("\nEnchères actives:\n");
  int active_auctions = 0;

  for (int i = 0; i < auctionSys.count; i++) {
    struct Auction *auction = &auctionSys.auctions[i];
    if (!is_auction_finished(auction->auction_id)) {
      printf("%d. ID: %u, Prix actuel: %u, Créateur: %d\n", active_auctions + 1,
             auction->auction_id, auction->current_price, auction->creator_id);
      active_auctions++;
    }
  }
  pthread_mutex_unlock(&auction_mutex);

  if (active_auctions == 0) {
    printf("Aucune enchère active disponible\n");
    return;
  }

  printf("\nEntrez l'ID de l'enchère: ");
  scanf("%u", &auction_id);
  while (getchar() != '\n')
    ; // Vider le buffer d'entrée

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
  while (getchar() != '\n')
    ; // Vider le buffer d'entrée

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

  if (send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port,
                     buffer, buffer_size) < 0) {
    perror("Échec de l'envoi de l'enchère");
  } else {
    pthread_mutex_lock(&auction_mutex);
    if (auction && !is_auction_finished(auction_id)) {
      // Mise à jour temporaire pour l'affichage
      unsigned int old_price = auction->current_price;

      auction->current_price = price;
      auction->id_dernier_prop = pSystem.my_id;
      auction->last_bid_time = time(NULL);

      printf(
          "Prix local mis à jour de %u à %u, en attente de confirmation...\n",
          old_price, price);

      // Afficher les informations mises à jour
      pthread_mutex_unlock(&auction_mutex);
      display_auctions();
    } else {
      pthread_mutex_unlock(&auction_mutex);
      printf("Attention: L'enchère pourrait être terminée\n");
    }
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
      printf("ID: %u, Prix actuel: %u, Créateur: %d\n", auction->auction_id,
             auction->current_price, auction->creator_id);
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
      printf("ID: %u, Prix final: %u, Gagnant: %d\n", auction->auction_id,
             auction->current_price, auction->id_dernier_prop);
      finished_count++;
    }
  }

  if (finished_count == 0) {
    printf("Aucune enchère terminée\n");
  }
}

/**
 * @brief Join an existing P2P network or create a new one
 *
 * Prompts the user to enter their desired ID and attempts to join an existing
 * P2P network. If no network is found, it creates a new one.
 *
 * @return 0 on success, 1 if no network was found, negative value on error
 */
int join_network() {
  printf("  Tentative de connexion à un réseau P2P existant...\n");

  // Customize ID (optional)
  printf("  Entrez votre ID souhaité (laissez vide pour défaut %d): ",
         pSystem.my_id);
  char id_str[10];
  if (fgets(id_str, sizeof(id_str), stdin) != NULL) {
    // Remove trailing newline
    id_str[strcspn(id_str, "\n")] = 0;
    if (strlen(id_str) > 0) {
      pSystem.my_id = (unsigned short)atoi(id_str);
    }
  }

  printf("  Tentative de connexion avec ID=%d...\n", pSystem.my_id);
  // Try to join the P2P network
  if (join_pairs(m_send) < 0) {
    printf("  Aucun réseau P2P trouvé.\n");
    return 1;
  }
  return 0;
}

void recv_auction_info() {
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
    FD_SET(auc_sock, &readfds);

    // Timeout de 0.5 secondes
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;

    int select_result = select(auc_sock + 1, &readfds, NULL, NULL, &tv);
    if (select_result > 0 && FD_ISSET(auc_sock, &readfds)) {
      int len = receive_multicast(auc_sock, buffer, sizeof(buffer), &sender);
      if (len > 0) {
        // Traiter le message d'enchère
        handle_auction_message(auc_sock);
      }
    }
  }

  printf("Synchronisation terminée.\n");
}

// Fonction pour synchroniser manuellement les enchères
void sync_auctions() {
  printf("\n=== Synchronisation des enchères ===\n");
  int count = broadcast_all_auctions();

  if (count > 0)
    printf("%d enchères diffusées avec succès\n", count);
  else if (count == 0)
    printf("Aucune enchère à synchroniser\n");
  else
    printf("Erreur lors de la synchronisation des enchères\n");
}

void print_commands() {
  printf("\nCommandes disponibles :\n  1 - Créer une enchère\n  2 - Faire une "
         "offre\n"
         "  3 - Afficher les enchères actives\n  q - Quitter le programme\n> ");
  fflush(stdout);
}

/**
 * @brief Main function to run the P2P system
 *
 * Initializes the P2P system, attempts to join an existing network,
 * and handles incoming connections and messages.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on error
 */
int main() {
  printf("===== Bienvenue dans le système P2P =====\n\n");
  printf("Recherche de système P2P existant...\n");

  // Initialize the peer system
  if (init_pairs() < 0) {
    fprintf(stderr, "  Échec de l'initialisation du système de pairs\n");
    return EXIT_FAILURE;
  }
  // Initialize the auction system
  if (init_auction_system() < 0) {
    fprintf(stderr, "Échec de l'initialisation du système d'enchères\n");
    return EXIT_FAILURE;
  }

  // Configure sender socket to respond to requests
  m_send = setup_multicast_sender();
  if (m_send < 0) {
    fprintf(stderr, "  Échec de la création du socket émetteur multicast\n");
    return EXIT_FAILURE;
  }

  // Try to join an existing network
  int ret = join_network();
  if (ret < 0) { // Error handling
    fprintf(stderr, "Échec de la connexion au réseau P2P\n");
    return EXIT_FAILURE;
  } else if (ret == 1) { // No existing network found, create a new one
    printf("\nRéseau P2P non trouvé, création d'un nouveau réseau...\n");
  } else if (ret == 0) { // Successfully joined an existing network
    printf("\nRéseau P2P trouvé, vous êtes maintenant connecté.\n");
  } else {
    fprintf(stderr, "Erreur lors de la connexion au réseau P2P\n");
    return EXIT_FAILURE;
  }

  // Configure multicast receiver socket for connections
  m_recv = setup_multicast_receiver(pSystem.liaison_addr, pSystem.liaison_port);
  if (m_recv < 0) {
    fprintf(stderr, "  Échec de la création du socket récepteur multicast\n");
    close(m_send);
    return EXIT_FAILURE;
  }

  // Setup server socket for TCP connections
  server_sock = setup_server_socket(pSystem.my_port);
  if (server_sock < 0) {
    fprintf(stderr, "  Échec de la création du socket serveur TCP\n");
    close(m_recv);
    close(m_send);
    return EXIT_FAILURE;
  }

  // Configure multicast receiver socket for auctions
  auc_sock =
      setup_multicast_receiver(pSystem.auction_addr, pSystem.auction_port);
  if (auc_sock < 0) {
    fprintf(
        stderr,
        "Échec de la création du socket récepteur multicast pour enchères\n");
    close(m_recv);
    close(m_send);
    close(server_sock);
    return EXIT_FAILURE;
  }

  // Print network information
  print_network_info();

  // Configuration for poll
  struct pollfd fds[4];

  // Monitor network socket
  fds[0].fd = m_recv;
  fds[0].events = POLLIN;

  // Monitor standard input (to detect exit command)
  fds[1].fd = STDIN_FILENO;
  fds[1].events = POLLIN;

  // Monitor server socket for incoming connections
  fds[2].fd = server_sock;
  fds[2].events = POLLIN;

  // Monitor server socket for auction
  fds[3].fd = auc_sock;
  fds[3].events = POLLIN;

  print_commands();

  running = 1;
  while (running) {
    int poll_result = poll(fds, 4, 1000); // 1 second timeout

    if (poll_result < 0) {
      perror("Erreur lors de l'appel à poll");
      break;
    }

    // Check if data is available on the network socket
    if (fds[0].revents & POLLIN) {
      int result = handle_join(m_recv, server_sock);
      if (result > 0) {
        printf("\nDemande de connexion reçue et traitée\n");
        print_pairs();
      }
    }

    // Check if data is available on stdin
    if (fds[1].revents & POLLIN) {
      char input;
      if (read(STDIN_FILENO, &input, 1) > 0) {
        if (input == 'q' || input == 'Q') {
          quit_pairs();
          running = 0;
        } else {
          if (input == '1') {
            // Créer une enchère
            create_auction();
            print_commands();
          } else if (input == '2') {
            // Faire une offre
            make_bid();
            print_commands();
          } else if (input == '3') {
            // Afficher les enchères
            display_auctions();
            print_commands();
          }
        }
      }
    }

    // Check if there is a new connection on the server socket
    if (fds[2].revents & POLLIN) {
      int client_sock = accept(server_sock, NULL, NULL);
      if (client_sock < 0) {
        perror("Échec de l'acceptation de la connexion");
        continue;
      }
      // Handle the new connection in a separate thread or process
      if (recv_message(client_sock) < 0) {
        perror("Échec de la réception du message du client");
      } else {
        print_pairs();
      }
      // For simplicity, we will just close it immediately here
      close(client_sock);
    }

    // Vérifier si des données sont disponibles sur le socket d'enchères
    if (fds[3].revents & POLLIN) {
      int result = handle_auction_message(auc_sock);
      if (result > 0) {
        printf("> ");
        fflush(stdout);
      }
    }
  }

  // Small delay before closing sockets to avoid reuse issues
  sleep(1);

  // Close sockets
  close(m_recv);
  close(m_send);
  close(server_sock);
  close(auc_sock);
  free_pairs();

  printf("Réseau P2P fermé\n");
  return EXIT_SUCCESS;
}
