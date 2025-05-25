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

/**
 * @brief Join an existing P2P network or create a new one
 *
 * Prompts the user to enter their desired ID and attempts to join an existing
 * P2P network. If no network is found, it creates a new one.
 *
 * @return 0 on success, 1 if no network was found, negative value on error
 */
int join_network() {
  printf("  ➤ Tentative de connexion à un réseau P2P existant...\n");

  // Customize ID (optional)
  printf("  📝 Entrez votre ID souhaité (laissez vide pour défaut %d): ",
         pSystem.my_id);
  char id_str[10];
  if (fgets(id_str, sizeof(id_str), stdin) != NULL) {
    // Remove trailing newline
    id_str[strcspn(id_str, "\n")] = 0;
    if (strlen(id_str) > 0) {
      pSystem.my_id = (unsigned short)atoi(id_str);
    }
  }

  printf("  🔍 Tentative de connexion avec ID=%d...\n", pSystem.my_id);
  // Try to join the P2P network
  if (join_pairs(m_send) < 0) {
    printf("  ℹ️  Aucun réseau P2P trouvé.\n");
    return 1;
  }
  return 0;
}

/**
 * @brief Receive auction information from peers
 *
 * This function waits for auction messages from peers for a limited time
 * and processes them to synchronize the local auction state.
 */
void recv_auction_info() {
  // Attendre explicitement les messages d'enchères pendant quelques secondes
  printf("🔄 Attente des informations d'enchères en cours...\n");
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
        handle_auction_message(auc_sock, m_send);
      }
    }
  }

  printf("✅ Synchronisation terminée.\n");
}

/**
 * @brief Print the current network information
 *
 * Displays the current peer ID, address, and port.
 */
void sync_auctions() {
  printf("\n╭─────────────────────────────────────╮\n");
  printf("│    🔄 Synchronisation des enchères │\n");
  printf("╰─────────────────────────────────────╯\n");
  int count = broadcast_all_auctions(m_send);

  if (count > 0) printf("✅ %d enchères diffusées avec succès\n", count);
  else if (count == 0) printf("ℹ️  Aucune enchère à synchroniser\n");
  else printf("❌ Erreur lors de la synchronisation des enchères\n");
}

/**
 * @brief Print the current network information
 *
 * Displays the current peer ID, address, and port.
 */
void print_commands() {
  printf("\n╭────────────────────────────────────────────╮\n");
  printf("│            📋 Commandes disponibles        │\n");
  printf("├────────────────────────────────────────────┤\n");
  printf("│  [1] 📝 Créer une enchère                  │\n");
  printf("│  [2] 💰 Faire une offre                    │\n");
  printf("│  [3] 📊 Afficher les enchères actives      │\n");
  printf("│  [q] 🚪 Quitter le programme               │\n");
  printf("╰────────────────────────────────────────────╯\n");
  printf("> ");
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
  printf("╔═══════════════════════════════════════════════╗\n");
  printf("║        🌐 Système P2P d'Enchères              ║\n");
  printf("║             Bienvenue !                       ║\n");
  printf("╚═══════════════════════════════════════════════╝\n\n");
  printf("🔍 Recherche de système P2P existant...\n");

  // Initialize the peer system
  if (init_pairs() < 0) {
    fprintf(stderr, "❌ Échec de l'initialisation du système de pairs\n");
    return EXIT_FAILURE;
  }
  // Initialize the auction system
  if (init_auction_system() < 0) {
    fprintf(stderr, "❌ Échec de l'initialisation du système d'enchères\n");
    return EXIT_FAILURE;
  }

  // Configure sender socket to respond to requests
  m_send = setup_multicast_sender();
  if (m_send < 0) {
    fprintf(stderr, "❌ Échec de la création du socket émetteur multicast\n");
    return EXIT_FAILURE;
  }

  // Try to join an existing network
  int ret = join_network();
  if (ret < 0) { // Error handling
    fprintf(stderr, "❌ Échec de la connexion au réseau P2P\n");
    return EXIT_FAILURE;
  } else if (ret == 1) { // No existing network found, create a new one
    printf("\n🆕 Réseau P2P non trouvé, création d'un nouveau réseau...\n");
  } else if (ret == 0) { // Successfully joined an existing network
    printf("\n✅ Réseau P2P trouvé, vous êtes maintenant connecté.\n");
  } else {
    fprintf(stderr, "❌ Erreur lors de la connexion au réseau P2P\n");
    return EXIT_FAILURE;
  }

  // Configure multicast receiver socket for connections
  m_recv = setup_multicast_receiver(pSystem.liaison_addr, pSystem.liaison_port);
  if (m_recv < 0) {
    fprintf(stderr, "❌ Échec de la création du socket récepteur multicast\n");
    close(m_send);
    return EXIT_FAILURE;
  }

  // Setup server socket for TCP connections
  server_sock = setup_server_socket(pSystem.my_port);
  if (server_sock < 0) {
    fprintf(stderr, "❌ Échec de la création du socket serveur TCP\n");
    close(m_recv);
    close(m_send);
    return EXIT_FAILURE;
  }

  // Configure multicast receiver socket for auctions
  auc_sock = setup_multicast_receiver(pSystem.auction_addr, pSystem.auction_port);
  if (auc_sock < 0) {
    fprintf(stderr, "❌ Échec de la création du socket récepteur multicast pour enchères\n");
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
      perror("❌ Erreur lors de l'appel à poll");
      break;
    }

    // Check if data is available on the network socket
    if (fds[0].revents & POLLIN) {
      int result = handle_join(m_recv, server_sock);
      if (result > 0) {
        printf("\n🤝 Demande de connexion reçue et traitée\n");
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
            int result = create_auction(m_send);
            if (result < 0) printf("❌ Échec de la création de l'enchère. Veuillez réessayer.\n");
            else if (result == 0) printf("ℹ️  Aucune enchère active pour créer une nouvelle enchère.\n");
            else printf("✅ Enchère créée avec succès.\n");
            print_commands();
          } else if (input == '2') {
            // Faire une offre
            int result = make_bid(m_send);
            if (result < 0) printf("❌ Échec de l'offre. Veuillez réessayer.\n");
            else if (result == 0) printf("ℹ️  Aucune enchère active pour faire une offre.\n");
            else printf("✅ Offre faite avec succès.\n");
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
        perror("❌ Échec de l'acceptation de la connexion");
        continue;
      }
      // Handle the new connection in a separate thread or process
      if (recv_message(client_sock) < 0) {
        perror("❌ Échec de la réception du message du client");
      } else {
        print_pairs();
      }
      // For simplicity, we will just close it immediately here
      close(client_sock);
    }

    // Vérifier si des données sont disponibles sur le socket d'enchères
    if (fds[3].revents & POLLIN) {
      int result = handle_auction_message(auc_sock, m_send);
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

  printf("👋 Réseau P2P fermé\n");
  return EXIT_SUCCESS;
}