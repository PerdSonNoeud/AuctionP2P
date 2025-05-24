#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <poll.h>
#include "include/auction.h"
#include "include/sockets.h"

// Global variables
int running = 1;
int m_recv = -1;
int m_send = -1;
int server_sock = -1;

extern struct PairSystem pSystem;  // Declare pSystem as external

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
  printf("  Entrez votre ID souhaité (laissez vide pour défaut %d): ", pSystem.my_id);
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
    return -1;
  }

  // Configure sender socket to respond to requests
  m_send = setup_multicast_sender();
  if (m_send < 0) {
    fprintf(stderr, "  Échec de la création du socket émetteur multicast\n");
    return -1;
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

  // Configure multicast receiver socket
  m_recv = setup_multicast_receiver(pSystem.liaison_addr, pSystem.liaison_port);
  if (m_recv < 0) {
    fprintf(stderr, "  Échec de la création du socket récepteur multicast\n");
    close(m_send);
    return -1;
  }

  // Setup server socket for TCP connections
  server_sock = setup_server_socket(pSystem.my_port);
  if (server_sock < 0) {
    fprintf(stderr, "  Échec de la création du socket serveur TCP\n");
    close(m_recv);
    close(m_send);
    return -1;
  }

  // Print network information
  print_network_info();

  // Configuration for poll
  struct pollfd fds[2];

  // Monitor network socket
  fds[0].fd = m_recv;
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
          running = 0;
        }
      }
    }
  }

  // Small delay before closing sockets to avoid reuse issues
  sleep(1);

  // Close sockets
  close(m_recv);
  close(m_send);
  close(server_sock);
  free_pairs();

  printf("Réseau P2P fermé\n");
  return EXIT_SUCCESS;
}
