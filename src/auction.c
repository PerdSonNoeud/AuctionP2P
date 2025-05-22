#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include "include/auction.h"
#include "include/multicast.h"
#include "include/message.h"
#include "include/utils.h"
#include "include/pairs.h"

struct AuctionSystem auctionSys;
extern struct PairSystem pSystem;

#define AUCTION_TIMEOUT 60  // 60 secondes pour t3s
#define MIN_VALIDATION_COUNT 3  // Minimum number of validations for consensus

// Compteur pour les ventes initiées par ce pair
static uint32_t auction_counter = 0;

// Thread pour surveiller les enchères
pthread_t auction_monitor_thread;
int monitor_running = 0;

// Mutex pour protéger l'accès aux enchères
pthread_mutex_t auction_mutex = PTHREAD_MUTEX_INITIALIZER;

int init_auction_system() {
  auctionSys.auctions = malloc(10 * sizeof(struct Auction));
  if (!auctionSys.auctions) {
    perror("malloc a échoué");
    return -1;
  }

  auctionSys.count = 0;
  auctionSys.capacity = 10;
  
  // Initialiser le compteur d'enchères
  auction_counter = 0;
  
  // Initialiser le mutex
  pthread_mutex_init(&auction_mutex, NULL);
  
  return 0;
}

void cleanup_auction_system() {
  // Arrêter le thread de surveillance
  if (monitor_running) {
    monitor_running = 0;
    pthread_join(auction_monitor_thread, NULL);
  }

  // Libérer le mutex
  pthread_mutex_destroy(&auction_mutex);

  // Libérer la mémoire
  free(auctionSys.auctions);
  auctionSys.auctions = NULL;
  auctionSys.count = 0;
  auctionSys.capacity = 0;
}

// Fonction qui génère un ID unique pour une enchère
unsigned int generate_auction_id() {
  auction_counter++;
  // Concaténation de l'ID du pair et du compteur
  return ((uint32_t)pSystem.my_id << 16) | auction_counter;
}

// Fonction pour trouver une enchère par son ID
struct Auction* find_auction(unsigned int auction_id) {
  for (int i = 0; i < auctionSys.count; i++) {
    if (auctionSys.auctions[i].auction_id == auction_id) {
      return &auctionSys.auctions[i];
    }
  }
  return NULL;
}

unsigned int init_auction(struct Pair *creator, unsigned int initial_price) {
  pthread_mutex_lock(&auction_mutex);
  
  if (auctionSys.count >= auctionSys.capacity) {
    auctionSys.capacity *= 2;
    auctionSys.auctions = realloc(auctionSys.auctions, auctionSys.capacity * sizeof(struct Auction));
  }

  unsigned int auction_id = generate_auction_id();
  
  struct Auction *new_auction = &auctionSys.auctions[auctionSys.count++];
  new_auction->auction_id = auction_id;
  new_auction->creator_id = creator->id;
  new_auction->initial_price = initial_price;
  new_auction->current_price = initial_price;
  new_auction->id_dernier_prop = creator->id; // Au début, le créateur est considéré comme le dernier proposant
  new_auction->last_bid_time = time(NULL);

  pthread_mutex_unlock(&auction_mutex);
  return auction_id;
}

void start_auction(unsigned int auction_id) {
  pthread_mutex_lock(&auction_mutex);
  
  struct Auction *auction = find_auction(auction_id);
  if (!auction) {
    fprintf(stderr, "Erreur: Enchère %u introuvable\n", auction_id);
    pthread_mutex_unlock(&auction_mutex);
    return;
  }
  
  auction->start_time = time(NULL);
  auction->last_bid_time = time(NULL);

  // Prépare et envoie le message CODE_NOUVELLE_VENTE (CODE=8)
  int send_sock = setup_multicast_sender();
  if (send_sock < 0) {
    perror("Échec de création du socket d'envoi multicast");
    pthread_mutex_unlock(&auction_mutex);
    return;
  }

  struct message *msg = init_message(CODE_NOUVELLE_VENTE);
  if (!msg) {
    perror("Échec de l'initialisation du message");
    close(send_sock);
    pthread_mutex_unlock(&auction_mutex);
    return;
  }

  msg->id = pSystem.my_id;
  msg->numv = auction_id;
  msg->prix = auction->initial_price;
  
  // Convertir le message en buffer
  int buffer_size = get_buffer_size(msg);
  char *buffer = malloc(buffer_size);
  if (!buffer) {
    perror("Échec de l'allocation du buffer");
    free_message(msg);
    close(send_sock);
    pthread_mutex_unlock(&auction_mutex);
    return;
  }
  
  if (message_to_buffer(msg, buffer, buffer_size) < 0) {
    perror("Échec de la conversion du message en buffer");
    free(buffer);
    free_message(msg);
    close(send_sock);
    pthread_mutex_unlock(&auction_mutex);
    return;
  }

  // Envoyer le message au groupe multicast des enchères
  if (send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size) < 0) {
    perror("Échec de l'envoi de l'annonce de nouvelle vente");
  } else {
    printf("Nouvelle vente %u lancée avec prix initial %u\n", auction_id, auction->initial_price);
  }

  free(buffer);
  free_message(msg);
  close(send_sock);

  // Démarrer le thread de surveillance si nécessaire
  if (!monitor_running) {
    monitor_running = 1;
    pthread_create(&auction_monitor_thread, NULL, auction_monitor, NULL);
  }
  
  pthread_mutex_unlock(&auction_mutex);
}

// Fonction pour gérer les messages d'enchère reçus (CODE=9)
int handle_bid(struct message *msg) {
  if (msg->code != CODE_ENCHERE) {
    return -1;
  }

  pthread_mutex_lock(&auction_mutex);
  
  struct Auction *auction = find_auction(msg->numv);
  if (!auction) {
    fprintf(stderr, "Erreur: Enchère %u introuvable. En attente des informations du superviseur.\n", msg->numv);
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  // Vérification que l'enchère est encore en cours
  if (is_auction_finished(auction->auction_id)) {
    fprintf(stderr, "Erreur: L'enchère %u est terminée\n", msg->numv);
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  // Si nous sommes le superviseur de cette enchère, nous devons relayer l'enchère
  if (auction->creator_id == pSystem.my_id) {
    // Vérifie que le prix proposé est supérieur au prix actuel
    if (msg->prix <= auction->current_price) {
      printf("Offre rejetée (superviseur): prix (%u) inférieur ou égal au prix actuel (%u)\n",
              msg->prix, auction->current_price);
      
      // Envoyer un refus (CODE_REFUS_PRIX = 15)
      int send_sock = setup_multicast_sender();
      if (send_sock >= 0) {
        struct message *refuse_msg = init_message(CODE_REFUS_PRIX);
        if (refuse_msg) {
          refuse_msg->id = pSystem.my_id;
          refuse_msg->numv = auction->auction_id;
          refuse_msg->prix = msg->prix;
          
          int buffer_size = get_buffer_size(refuse_msg);
          char *buffer = malloc(buffer_size);
          if (buffer) {
            message_to_buffer(refuse_msg, buffer, buffer_size);
            send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size);
            free(buffer);
          }
          free_message(refuse_msg);
        }
        close(send_sock);
      }
      
      pthread_mutex_unlock(&auction_mutex);
      return -1;
    }

    // Relayer l'enchère (CODE_ENCHERE_SUPERVISEUR = 10)
    int send_sock = setup_multicast_sender();
    if (send_sock >= 0) {
      struct message *relay_msg = init_message(CODE_ENCHERE_SUPERVISEUR);
      if (relay_msg) {
        relay_msg->id = msg->id;
        relay_msg->numv = msg->numv;
        relay_msg->prix = msg->prix;
        
        int buffer_size = get_buffer_size(relay_msg);
        char *buffer = malloc(buffer_size);
        if (buffer) {
          message_to_buffer(relay_msg, buffer, buffer_size);
          printf("Relais de l'offre: enchère %u, offrant %d, prix %u\n", 
                 msg->numv, msg->id, msg->prix);
          send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size);
          free(buffer);
        }
        free_message(relay_msg);
        
        // Mettre à jour les données de l'enchère
        auction->current_price = msg->prix;
        auction->id_dernier_prop = msg->id;
        auction->last_bid_time = time(NULL);
      }
      close(send_sock);
    }
  } else {
    // Si nous ne sommes pas le superviseur, nous vérifions seulement que le prix est supérieur
    // pour information utilisateur, mais nous ne faisons rien d'autre
    if (msg->prix <= auction->current_price) {
      printf("Offre reçue (non-superviseur): prix (%u) inférieur ou égal au prix actuel (%u)\n",
              msg->prix, auction->current_price);
    } else {
      printf("Offre reçue (non-superviseur): prix (%u) supérieur au prix actuel (%u) - en attente du superviseur\n",
              msg->prix, auction->current_price);
    }
  }
  
  pthread_mutex_unlock(&auction_mutex);
  return 0;
}

// Fonction pour gérer les enchères relayées par le superviseur (CODE=10)
int handle_supervisor_bid(struct message *msg) {
  if (msg->code != CODE_ENCHERE_SUPERVISEUR) {
    return -1;
  }

  pthread_mutex_lock(&auction_mutex);
  
  struct Auction *auction = find_auction(msg->numv);
  if (!auction) {
    // Si l'enchère n'existe pas dans notre système, on l'ajoute
    printf("Réception d'une enchère pour une vente inconnue (ID=%u). Création de l'enchère.\n", msg->numv);
    
    if (auctionSys.count >= auctionSys.capacity) {
      auctionSys.capacity *= 2;
      auctionSys.auctions = realloc(auctionSys.auctions, auctionSys.capacity * sizeof(struct Auction));
      if (!auctionSys.auctions) {
        perror("Échec de la réallocation du tableau d'enchères");
        pthread_mutex_unlock(&auction_mutex);
        return -1;
      }
    }
    
    struct Auction *new_auction = &auctionSys.auctions[auctionSys.count++];
    new_auction->auction_id = msg->numv;
    new_auction->creator_id = (msg->numv >> 16) & 0xFFFF; // Extraction de l'ID du créateur
    new_auction->initial_price = msg->prix;
    new_auction->current_price = msg->prix;
    new_auction->id_dernier_prop = msg->id;
    new_auction->start_time = time(NULL);
    new_auction->last_bid_time = time(NULL);
    
    printf("Nouvelle enchère ajoutée au système - ID: %u, Prix: %u, Créateur: %d\n", 
           new_auction->auction_id, new_auction->current_price, new_auction->creator_id);
    
    pthread_mutex_unlock(&auction_mutex);
    return 0;
  }
  
  // Vérifier si le prix proposé est supérieur au prix actuel
  if (msg->prix <= auction->current_price) {
    printf("Prix proposé (%u) inférieur ou égal au prix actuel (%u). Enchère ignorée.\n", 
           msg->prix, auction->current_price);
    pthread_mutex_unlock(&auction_mutex);
    return 0;
  }
  
  // Mettre à jour les données de l'enchère
  printf("Mise à jour de l'enchère %u: nouveau prix = %u, nouveau proposant = %d\n", 
         auction->auction_id, msg->prix, msg->id);
  
  auction->current_price = msg->prix;
  auction->id_dernier_prop = msg->id;
  auction->last_bid_time = time(NULL);
  
  pthread_mutex_unlock(&auction_mutex);
  return 0;
}

// Fonction pour envoyer un avertissement de fin de vente (CODE=11)
int send_end_warning(unsigned int auction_id) {
  pthread_mutex_lock(&auction_mutex);
  
  struct Auction *auction = find_auction(auction_id);
  if (!auction) {
    fprintf(stderr, "Erreur: Enchère %u introuvable\n", auction_id);
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }
  
  // Vérifie que nous sommes le superviseur de cette enchère
  if (auction->creator_id != pSystem.my_id) {
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  // Envoyer le message d'avertissement (CODE_FIN_VENTE_WARNING = 11)
  int send_sock = setup_multicast_sender();
  if (send_sock < 0) {
    perror("Échec de création du socket d'envoi multicast");
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  struct message *warning_msg = init_message(CODE_FIN_VENTE_WARNING);
  if (!warning_msg) {
    perror("Échec de l'initialisation du message");
    close(send_sock);
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  warning_msg->id = pSystem.my_id;
  warning_msg->numv = auction_id;
  warning_msg->prix = auction->current_price;
  
  int buffer_size = get_buffer_size(warning_msg);
  char *buffer = malloc(buffer_size);
  if (!buffer) {
    perror("Échec de l'allocation du buffer");
    free_message(warning_msg);
    close(send_sock);
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }
  
  message_to_buffer(warning_msg, buffer, buffer_size);
  send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size);
  
  printf("Avertissement de fin de vente pour l'enchère %u envoyé (prix actuel: %u)\n", 
         auction_id, auction->current_price);
  
  free(buffer);
  free_message(warning_msg);
  close(send_sock);
  
  pthread_mutex_unlock(&auction_mutex);
  return 0;
}

// Fonction pour finaliser une vente (CODE=12)
int finalize_auction(unsigned int auction_id) {
  pthread_mutex_lock(&auction_mutex);
  
  struct Auction *auction = find_auction(auction_id);
  if (!auction) {
    fprintf(stderr, "Erreur: Enchère %u introuvable\n", auction_id);
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }
  
  // Vérifie que nous sommes le superviseur de cette enchère
  if (auction->creator_id != pSystem.my_id) {
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  // Envoyer le message de fin (CODE_FIN_VENTE = 12)
  int send_sock = setup_multicast_sender();
  if (send_sock < 0) {
    perror("Échec de création du socket d'envoi multicast");
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  struct message *final_msg = init_message(CODE_FIN_VENTE);
  if (!final_msg) {
    perror("Échec de l'initialisation du message");
    close(send_sock);
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  // Pour l'ID, on prend soit l'ID du dernier proposant s'il y en a un,
  // soit l'ID du superviseur s'il n'y a pas eu d'enchère
  final_msg->id = (auction->id_dernier_prop != auction->creator_id) ? 
                  auction->id_dernier_prop : auction->creator_id;
  final_msg->numv = auction_id;
  final_msg->prix = auction->current_price;
  
  int buffer_size = get_buffer_size(final_msg);
  char *buffer = malloc(buffer_size);
  if (!buffer) {
    perror("Échec de l'allocation du buffer");
    free_message(final_msg);
    close(send_sock);
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }
  
  message_to_buffer(final_msg, buffer, buffer_size);
  send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size);
  
  printf("Fin de la vente pour l'enchère %u: gagnant ID=%u, prix final=%u\n", 
         auction_id, final_msg->id, final_msg->prix);
  
  free(buffer);
  free_message(final_msg);
  close(send_sock);
  
  pthread_mutex_unlock(&auction_mutex);
  return 0;
}

// Fonction pour quitter le système d'enchères (CODE=13)
int quit_auction_system() {
  // Envoyer le message de départ (CODE_QUIT_SYSTEME = 13)
  int send_sock = setup_multicast_sender();
  if (send_sock < 0) {
    perror("Échec de création du socket d'envoi multicast");
    return -1;
  }

  struct message *quit_msg = init_message(CODE_QUIT_SYSTEME);
  if (!quit_msg) {
    perror("Échec de l'initialisation du message");
    close(send_sock);
    return -1;
  }

  quit_msg->id = pSystem.my_id;
  
  int buffer_size = get_buffer_size(quit_msg);
  char *buffer = malloc(buffer_size);
  if (!buffer) {
    perror("Échec de l'allocation du buffer");
    free_message(quit_msg);
    close(send_sock);
    return -1;
  }
  
  message_to_buffer(quit_msg, buffer, buffer_size);
  send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size);
  
  printf("Message de départ envoyé (ID=%u)\n", pSystem.my_id);
  
  free(buffer);
  free_message(quit_msg);
  close(send_sock);
  
  return 0;
}

// Fonction pour vérifier si une enchère est terminée
int is_auction_finished(unsigned int auction_id) {
  struct Auction *auction = find_auction(auction_id);
  if (!auction) {
    return -1; // L'enchère n'existe pas
  }

  time_t now = time(NULL);

  // Si aucune enchère depuis AUCTION_TIMEOUT secondes, l'enchère est terminée
  if (difftime(now, auction->last_bid_time) > AUCTION_TIMEOUT) {
    return 1; // Enchère terminée
  }

  return 0; // Enchère en cours
}

// Fonction pour valider une enchère
int validate_bid(unsigned int auction_id, unsigned short bidder_id, unsigned int bid_price) {
  struct Auction *auction = find_auction(auction_id);
  if (!auction) {
    printf("Erreur: Enchère %u inexistante\n", auction_id);
    return -1;
  }

  // Vérifier que le prix est valide (supérieur au prix actuel)
  if (bid_price <= auction->current_price) {
    // Envoyer un message de refus (CODE_REFUS_PRIX = 15)
    int send_sock = setup_multicast_sender();
    if (send_sock >= 0) {
      struct message *refuse_msg = init_message(CODE_REFUS_PRIX);
      if (refuse_msg) {
        refuse_msg->id = pSystem.my_id;
        refuse_msg->numv = auction_id;
        refuse_msg->prix = bid_price;
        
        int buffer_size = get_buffer_size(refuse_msg);
        char *buffer = malloc(buffer_size);
        if (buffer) {
          message_to_buffer(refuse_msg, buffer, buffer_size);
          send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size);
          free(buffer);
        }
        free_message(refuse_msg);
      }
      close(send_sock);
    }
    return -1;
  }

  // Envoyer un message de validation (CODE_VALIDATION = 1)
  int send_sock = setup_multicast_sender();
  if (send_sock < 0) {
    perror("Échec de création du socket d'envoi multicast");
    return -1;
  }

  struct message *valid_msg = init_message(CODE_VALIDATION);
  if (!valid_msg) {
    perror("Échec de l'initialisation du message");
    close(send_sock);
    return -1;
  }
  
  valid_msg->id = pSystem.my_id;
  
  int buffer_size = get_buffer_size(valid_msg);
  char *buffer = malloc(buffer_size);
  if (!buffer) {
    perror("Échec de l'allocation du buffer");
    free_message(valid_msg);
    close(send_sock);
    return -1;
  }
  
  message_to_buffer(valid_msg, buffer, buffer_size);
  send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size);
  
  free(buffer);
  free_message(valid_msg);
  close(send_sock);

  // Dans un cas réel, on compterait les validations reçues
  // Pour simuler le consensus, on met à jour directement
  auction->current_price = bid_price;
  auction->id_dernier_prop = bidder_id;
  auction->last_bid_time = time(NULL);

  return 0;
}

// Fonction exécutée par le thread de surveillance des enchères
void *auction_monitor(void *arg) {
  while (monitor_running) {
    pthread_mutex_lock(&auction_mutex);
    
    time_t now = time(NULL);
    
    // Parcourir toutes les enchères
    for (int i = 0; i < auctionSys.count; i++) {
      struct Auction *auction = &auctionSys.auctions[i];
      
      // Si nous sommes le superviseur de cette enchère
      if (auction->creator_id == pSystem.my_id) {
        double elapsed = difftime(now, auction->last_bid_time);
        
        // Si aucune enchère depuis AUCTION_TIMEOUT secondes
        if (elapsed > AUCTION_TIMEOUT && elapsed <= AUCTION_TIMEOUT + 5) {
          // Envoyer l'avertissement de fin
          send_end_warning(auction->auction_id);
        }
        // Si aucune enchère depuis 2*AUCTION_TIMEOUT secondes
        else if (elapsed > 2 * AUCTION_TIMEOUT) {
          // Finaliser la vente
          finalize_auction(auction->auction_id);
        }
      }
    }
    
    pthread_mutex_unlock(&auction_mutex);
    
    // Attendre 5 secondes avant la prochaine vérification
    sleep(5);
  }
  
  return NULL;
}
