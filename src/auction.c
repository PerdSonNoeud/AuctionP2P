#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "include/auction.h"
#include "include/sockets.h"
#include "include/message.h"
#include "include/utils.h"
#include "include/pairs.h"

struct AuctionSystem auctionSys;
extern struct PairSystem pSystem;

#define AUCTION_TIMEOUT 60     // 60 secondes pour t3s
#define MIN_VALIDATION_COUNT 3 // Minimum number of validations for consensus

// Compteur pour les ventes initiées par ce pair
static uint32_t auction_counter = 0;

// Thread pour surveiller les enchères
pthread_t auction_monitor_thread;
int monitor_running = 0;

// Mutex pour protéger l'accès aux enchères
pthread_mutex_t auction_mutex = PTHREAD_MUTEX_INITIALIZER;

int init_auction_system() {
  // Vérifier que le système n'a pas déjà été initialisé
  if (auctionSys.auctions != NULL) {
    printf("Le système d'enchères est déjà initialisé\n");
    return 0;
  }

  // Allouer la mémoire initiale pour le tableau d'enchères
  auctionSys.auctions = malloc(10 * sizeof(struct Auction));
  if (!auctionSys.auctions) {
    perror("malloc a échoué pour le tableau d'enchères");
    return -1;
  }

  // Initialiser tous les champs à zéro
  memset(auctionSys.auctions, 0, 10 * sizeof(struct Auction));

  // Initialiser les autres champs de la structure
  auctionSys.count = 0;
  auctionSys.capacity = 10;

  // Initialiser le compteur d'enchères
  auction_counter = 0;

  // Initialiser le mutex
  pthread_mutex_init(&auction_mutex, NULL);

  printf("Système d'enchères initialisé avec succès\n");
  return 0;
}

void cleanup_auction_system() {
  // Prendre le verrou avant le nettoyage
  pthread_mutex_lock(&auction_mutex);

  // Arrêter le thread de surveillance
  if (monitor_running) {
    monitor_running = 0;

    // Relâcher le verrou pendant que nous attendons le thread
    pthread_mutex_unlock(&auction_mutex);
    pthread_join(auction_monitor_thread, NULL);
    pthread_mutex_lock(&auction_mutex);
  }

  // Libérer la mémoire des enchères
  if (auctionSys.auctions != NULL) {
    free(auctionSys.auctions);
    auctionSys.auctions = NULL;
  }

  auctionSys.count = 0;
  auctionSys.capacity = 0;

  // Libérer le mutex
  pthread_mutex_unlock(&auction_mutex);
  pthread_mutex_destroy(&auction_mutex);

  printf("Système d'enchères nettoyé avec succès\n");
}

// Fonction qui génère un ID unique pour une enchère
unsigned int generate_auction_id() {
  auction_counter++;
  // Concaténation : ID du pair (16 bits hauts) + compteur (16 bits bas)
  int size = nbDigits(pSystem.my_id) + nbDigits(auction_counter) + 1; // +1 pour le '\0'
  char id_str[size];
  snprintf(id_str, size, "%u%u", pSystem.my_id, auction_counter);
  return (unsigned int) atoi(id_str);
}

// Fonction pour trouver une enchère par son ID
struct Auction *find_auction(unsigned int auction_id) {
  for (int i = 0; i < auctionSys.count; i++)
    if (auctionSys.auctions[i].auction_id == auction_id) return &auctionSys.auctions[i];
  return NULL;
}

int handle_auction_message(int auc_sock, int m_send) {
  struct sockaddr_in6 sender;
  char buffer[UNKNOWN_SIZE];
  memset(buffer, 0, sizeof(buffer));

  int len = receive_multicast(auc_sock, buffer, sizeof(buffer), &sender);
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
    case CODE_NOUVELLE_VENTE: // Code 8 - New auction
      if (pSystem.my_id == msg->id) {
        free_message(msg);
        return 0; // Ignore messages from ourselves
      }
      printf("Nouvelle vente reçue - ID: %d, NUMV: %u, PRIX: %u\n", msg->id,  msg->numv, msg->prix);

      // Créer une structure pour le créateur
      struct Pair creator;
      if (pSystem.my_id == msg->id) {
        // Si le créateur est nous-mêmes, utiliser nos propres informations
        creator.id = pSystem.my_id;
        creator.ip = pSystem.my_ip;
        creator.port = pSystem.my_port;
        creator.active = 1;
      } else {
        creator.id = 0;
        creator.active = 0;
        for (int i = 0; i < pSystem.count; i++) {
          if (pSystem.pairs[i].id == msg->id && pSystem.pairs[i].active) {
            creator = pSystem.pairs[i];
            break;
          }
        }
      }
      if (creator.id == 0 || !creator.active) {
        fprintf(stderr, "Erreur: Créateur de l'enchère %u introuvable (%d)\n", msg->numv, msg->id);
        free_message(msg);
        return -1;
      }

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
                   existing->auction_id, existing->current_price,  existing->creator_id);
          } else {
            printf("ERREUR: Impossible de trouver l'enchère %u après sa création!\n", msg->numv);
            return -1;
          }
        }
      } else {
        printf("L'enchère %u existe déjà dans le système (prix=%u, créateur=%d)\n",
               existing->auction_id, existing->current_price, existing->creator_id);
      }
      break;

    case CODE_ENCHERE: // Code 9 - Enchère d'un pair
      printf("Enchère reçue - ID: %d, NUMV: %u, PRIX: %u\n", msg->id, msg->numv, msg->prix);
      handle_bid(m_send, msg);
      break;

    case CODE_ENCHERE_SUPERVISEUR: // Code 10 - Enchère relayée par le superviseur
      printf("Enchère relayée par le superviseur - ID: %d, NUMV: %u, PRIX: %u\n", msg->id, msg->numv, msg->prix);
      handle_supervisor_bid(msg);
      break;

    case CODE_FIN_VENTE_WARNING: // Code 11 - Avertissement de fin de vente
      printf("Avertissement de fin de vente - ID: %d, NUMV: %u, PRIX: %u\n", msg->id, msg->numv, msg->prix);
      break;

    case CODE_FIN_VENTE: // Code 12 - Fin de vente
      printf("Fin de vente - ID gagnant: %d, NUMV: %u, PRIX final: %u\n", msg->id, msg->numv, msg->prix);
      break;
  }
  return 0;
}

int create_auction(int m_send) {
  if (auctionSys.auctions == NULL) {
    if (init_auction_system() < 0) {
      fprintf(stderr, "Échec de l'initialisation du système d'enchères\n");
      return -1;
    }
  }
  unsigned int initial_price;
  printf("Entrez le prix initial de l'enchère: ");
  if (scanf("%u", &initial_price) != 1) {
    fprintf(stderr, "Erreur de saisie du prix\n");
    while (getchar() != '\n'); // Vider le buffer
    return -1;
  }
  while (getchar() != '\n'); // Vider le buffer d'entrée

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
    return -1;
  }

  printf("Enchère %u créée avec succès\n", auction_id);

  return start_auction(m_send, auction_id);
}

unsigned int init_auction(struct Pair *creator, unsigned int initial_price) {
  printf("Initialisation d'une nouvelle enchère...\n");

  // Vérifier que creator est valide
  if (!creator) {
    fprintf(stderr, "Erreur: Créateur invalide\n");
    return 0;
  }

  pthread_mutex_lock(&auction_mutex);
  // Chercher un slot libre (enchère terminée) avant d'en créer un nouveau
  int free_slot = -1;
  for (int i = 0; i < auctionSys.count; i++)
  {
    if (is_auction_finished(auctionSys.auctions[i].auction_id))
    {
      free_slot = i;
      printf("Réutilisation du slot %d pour une nouvelle enchère\n", i);
      break;
    }
  }

  struct Auction *new_auction;
  if (free_slot >= 0) {
    // Réutiliser un slot existant
    new_auction = &auctionSys.auctions[free_slot];
    memset(new_auction, 0, sizeof(struct Auction));
  } else {
    // Vérifier si nous avons besoin d'augmenter la capacité
    if (auctionSys.count >= auctionSys.capacity) {
      size_t new_capacity = auctionSys.capacity * 2;
      struct Auction *new_auctions = realloc(auctionSys.auctions, new_capacity * sizeof(struct Auction));

      if (!new_auctions) {
        perror("realloc a échoué");
        pthread_mutex_unlock(&auction_mutex);
        return 0;
      }

      auctionSys.auctions = new_auctions;
      auctionSys.capacity = new_capacity;

      // Initialiser les nouveaux éléments à zéro
      memset(&auctionSys.auctions[auctionSys.count], 0, (new_capacity - auctionSys.count) * sizeof(struct Auction));
    }

    // Ajouter la nouvelle enchère à la fin du tableau
    new_auction = &auctionSys.auctions[auctionSys.count];
    auctionSys.count++;
  }

  printf("Capacité actuelle: %d, Nombre d'enchères: %d\n", auctionSys.capacity, auctionSys.count);

  // Générer un nouvel ID d'enchère
  unsigned int auction_id = generate_auction_id();

  new_auction->auction_id = auction_id;
  new_auction->creator_id = creator->id;
  new_auction->initial_price = initial_price;
  new_auction->current_price = initial_price;
  new_auction->id_dernier_prop = creator->id;
  new_auction->start_time = time(NULL);
  new_auction->last_bid_time = time(NULL);

  printf("Enchère %u créée avec succès (count=%d, capacity=%d)\n",
         auction_id, auctionSys.count, auctionSys.capacity);

  pthread_mutex_unlock(&auction_mutex);
  return auction_id;
}

int start_auction(int m_send, unsigned int auction_id) {
  pthread_mutex_lock(&auction_mutex);

  struct Auction *auction = find_auction(auction_id);
  if (!auction) {
    fprintf(stderr, "Erreur: Enchère %u introuvable\n", auction_id);
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  auction->start_time = time(NULL);
  auction->last_bid_time = time(NULL);

  pthread_mutex_unlock(&auction_mutex);

  struct message *msg = init_message(CODE_NOUVELLE_VENTE);
  if (!msg) {
    perror("Échec de l'initialisation du message");
    return -1;
  }

  msg->id = pSystem.my_id;
  msg->numv = auction_id;
  msg->prix = auction->initial_price;

  if (message_set_mess(msg, "Nouvelle enchère") < 0 || message_set_sig(msg, "") < 0)  {
    perror("Échec de l'initialisation des champs du message");
    free_message(msg);
    return -1;
  }

  // Convertir le message en buffer
  int buffer_size = get_buffer_size(msg);
  char *buffer = malloc(buffer_size);
  if (!buffer) {
    perror("Échec de l'allocation du buffer");
    free_message(msg);
    return -1;
  }

  memset(buffer, 0, buffer_size);

  if (message_to_buffer(msg, buffer, buffer_size) < 0) {
    perror("Échec de la conversion du message en buffer");
    free(buffer);
    free_message(msg);
    return -1;
  }

  // Envoyer plusieurs fois le message au groupe multicast des enchères
  printf("Diffusion de la nouvelle enchère %u (prix initial %u) à tous les pairs...\n",
         auction_id, auction->initial_price);

  for (int i = 0; i < 2; i++) {
    if (send_multicast(m_send, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size) < 0) {
      perror("Échec de l'envoi de l'annonce de nouvelle vente");
    }
    usleep(200000);
  }

  printf("Nouvelle vente %u lancée avec prix initial %u\n", auction_id, auction->initial_price);

  // Libérer les ressources
  free(buffer);
  free_message(msg);

  if (!monitor_running) {
    monitor_running = 1;
    int *m_send_ptr = &m_send;
    if (pthread_create(&auction_monitor_thread, NULL, auction_monitor, m_send_ptr) != 0) {
      perror("Échec de la création du thread de surveillance");
      monitor_running = 0;
    }
  }
  return 0;
}

// Fonction pour gérer les messages d'enchère reçus (CODE=9)
int handle_bid(int m_send, struct message *msg) {
  pthread_mutex_lock(&auction_mutex);

  struct Auction *auction = find_auction(msg->numv);
  if (!auction) {
    unsigned short supervisor_id = (msg->numv >> 16) & 0xFFFF;
    if (supervisor_id == pSystem.my_id) {
      fprintf(stderr, "Enchère reçue pour notre vente inconnue ID=%u\n", msg->numv);
    }
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  // Vérification que l'enchère est encore en cours
  if (is_auction_finished(auction->auction_id)) {
    fprintf(stderr, "Erreur: L'enchère %u est terminée\n", msg->numv);
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  unsigned short supervisor_id = auction->creator_id;

  // Si nous sommes le superviseur de cette enchère, nous devons relayer l'enchère
  if (supervisor_id == pSystem.my_id) {
    // Vérifie que le prix proposé est supérieur au prix actuel
    if (msg->prix <= auction->current_price) {
      printf("Offre rejetée (superviseur): prix (%u) inférieur ou égal au prix actuel (%u)\n",
             msg->prix, auction->current_price);

      pthread_mutex_unlock(&auction_mutex);
      send_rejection_message(m_send, msg);

      return -1;
    }

    // Mettre à jour les données de l'enchère
    auction->current_price = msg->prix;
    auction->id_dernier_prop = msg->id;
    auction->last_bid_time = time(NULL);

    printf("Prix de l'enchère %u mis à jour: %u (offrant: %d)\n",
           auction->auction_id, auction->current_price, auction->id_dernier_prop);

    unsigned int auction_id = auction->auction_id;
    unsigned int prix = msg->prix;
    unsigned short id = msg->id;

    pthread_mutex_unlock(&auction_mutex);

    // Relayer l'enchère (CODE_ENCHERE_SUPERVISEUR = 10)
    struct message *relay_msg = init_message(CODE_ENCHERE_SUPERVISEUR);
    if (relay_msg == NULL) {
      perror("Échec de l'initialisation du message relayé");
      return -1;
    }
    relay_msg->id = id;
    relay_msg->numv = auction_id;
    relay_msg->prix = prix;

    int buffer_size = get_buffer_size(relay_msg);
    char *buffer = malloc(buffer_size);
    if (buffer == NULL) {
      perror("Échec de l'allocation du buffer pour le message relayé");
      free_message(relay_msg);
      return -1;
    }
    if (message_to_buffer(relay_msg, buffer, buffer_size)) {
      perror("Échec de la conversion du message relayé en buffer");
      free(buffer);
      free_message(relay_msg);
      return -1;
    }
    free_message(relay_msg);

    printf("Relais de l'offre: enchère %u, offrant %d, prix %u\n", msg->numv, msg->id, msg->prix);
    // Code = 10 - Envoi de l'enchère relayée
    if (send_multicast(m_send, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size) < 0) {
      perror("Échec de l'envoi de l'enchère relayée");
      free(buffer);
      return -1;
    }
    free(buffer);
    return 0;
  }
  // Si nous ne sommes pas le superviseur, mettons quand même à jour l'enchère locale
  // si l'offre vient de nous-mêmes
  if (msg->id == pSystem.my_id && msg->prix > auction->current_price) {
    printf("Mise à jour locale de l'enchère %u: prix = %u (offrant: nous)\n",
           auction->auction_id, msg->prix);
    auction->current_price = msg->prix;
    auction->id_dernier_prop = msg->id;
    auction->last_bid_time = time(NULL);
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
  pthread_mutex_lock(&auction_mutex);
  fflush(stdout);

  struct Auction *auction = find_auction(msg->numv);
  if (!auction) {
    // Si l'enchère n'existe pas dans notre système, on l'ajoute
    printf("Réception d'une enchère pour une vente inconnue (ID=%u). Création de l'enchère.\n", msg->numv);

    if (auctionSys.count >= auctionSys.capacity) {
      size_t new_capacity = auctionSys.capacity * 2;
      struct Auction *new_auctions = realloc(auctionSys.auctions, new_capacity * sizeof(struct Auction));

      if (!new_auctions) {
        perror("Échec de la réallocation du tableau d'enchères");
        pthread_mutex_unlock(&auction_mutex);
        return -1;
      }

      auctionSys.auctions = new_auctions;
      auctionSys.capacity = new_capacity;

      // Initialiser les nouveaux éléments à zéro
      memset(&auctionSys.auctions[auctionSys.count], 0,
             (new_capacity - auctionSys.count) * sizeof(struct Auction));
    }

    struct Auction *new_auction = &auctionSys.auctions[auctionSys.count];
    memset(new_auction, 0, sizeof(struct Auction));

    new_auction->auction_id = msg->numv;
    new_auction->creator_id = (msg->numv >> 16) & 0xFFFF; // Extraction de l'ID du créateur (ancienne méthode)
    new_auction->initial_price = msg->prix;
    new_auction->current_price = msg->prix;
    new_auction->id_dernier_prop = msg->id;
    new_auction->start_time = time(NULL);
    new_auction->last_bid_time = time(NULL);

    auctionSys.count++;

    printf("Nouvelle enchère ajoutée au système - ID: %u, Prix: %u, Créateur: %d, Dernier proposant: %d\n",
           new_auction->auction_id, new_auction->current_price, new_auction->creator_id, new_auction->id_dernier_prop);

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
  unsigned int ancien_prix = auction->current_price;
  unsigned short ancien_proposant = auction->id_dernier_prop;

  auction->current_price = msg->prix;
  auction->id_dernier_prop = msg->id;
  auction->last_bid_time = time(NULL);

  printf("Mise à jour de l'enchère %u: prix %u → %u, proposant %d → %d\n",
         auction->auction_id, ancien_prix, msg->prix, ancien_proposant, msg->id);

  pthread_mutex_unlock(&auction_mutex);
  return 0;
}

// Fonction pour envoyer un avertissement de fin de vente (CODE=11)
int send_end_warning(int m_send, unsigned int auction_id)
{
  pthread_mutex_lock(&auction_mutex);

  struct Auction *auction = find_auction(auction_id);
  if (!auction)
  {
    fprintf(stderr, "Erreur: Enchère %u introuvable\n", auction_id);
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  // Vérifie que nous sommes le superviseur de cette enchère
  unsigned short supervisor_id = (auction->auction_id >> 16) & 0xFFFF;
  if (supervisor_id != pSystem.my_id)
  {
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  // Envoyer le message d'avertissement (CODE_FIN_VENTE_WARNING = 11)
  struct message *warning_msg = init_message(CODE_FIN_VENTE_WARNING);
  if (!warning_msg)
  {
    perror("Échec de l'initialisation du message");
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  warning_msg->id = pSystem.my_id;
  warning_msg->numv = auction_id;
  warning_msg->prix = auction->current_price;

  int buffer_size = get_buffer_size(warning_msg);
  char *buffer = malloc(buffer_size);
  if (!buffer)
  {
    perror("Échec de l'allocation du buffer");
    free_message(warning_msg);
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  message_to_buffer(warning_msg, buffer, buffer_size);
  send_multicast(m_send, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size);

  printf("Avertissement de fin de vente pour l'enchère %u envoyé (prix actuel: %u)\n",
         auction_id, auction->current_price);

  free(buffer);
  free_message(warning_msg);

  pthread_mutex_unlock(&auction_mutex);
  return 0;
}

// Fonction pour finaliser une vente (CODE=12)
int finalize_auction(int m_send, unsigned int auction_id)
{
  pthread_mutex_lock(&auction_mutex);

  struct Auction *auction = find_auction(auction_id);
  if (!auction)
  {
    fprintf(stderr, "Erreur: Enchère %u introuvable\n", auction_id);
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  unsigned short supervisor_id = (auction->auction_id >> 16) & 0xFFFF;
  if (supervisor_id != pSystem.my_id)
  {
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  // Envoyer le message de fin (CODE_FIN_VENTE = 12)
  struct message *final_msg = init_message(CODE_FIN_VENTE);
  if (!final_msg)
  {
    perror("Échec de l'initialisation du message");
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  // Pour l'ID, on prend soit l'ID du dernier proposant s'il y en a un,
  // soit l'ID du superviseur s'il n'y a pas eu d'enchère
  final_msg->id = (auction->id_dernier_prop != auction->creator_id) ? auction->id_dernier_prop : auction->creator_id;
  final_msg->numv = auction_id;
  final_msg->prix = auction->current_price;

  int buffer_size = get_buffer_size(final_msg);
  char *buffer = malloc(buffer_size);
  if (!buffer)
  {
    perror("Échec de l'allocation du buffer");
    free_message(final_msg);
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  message_to_buffer(final_msg, buffer, buffer_size);
  send_multicast(m_send, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size);

  printf("Fin de la vente pour l'enchère %u: gagnant ID=%u, prix final=%u\n",
         auction_id, final_msg->id, final_msg->prix);

  free(buffer);
  free_message(final_msg);

  pthread_mutex_unlock(&auction_mutex);
  return 0;
}

// Fonction pour quitter le système d'enchères (CODE=13)
int quit_auction_system(int m_send) {
  struct message *quit_msg = init_message(CODE_QUIT_SYSTEME);
  if (!quit_msg)
  {
    perror("Échec de l'initialisation du message");
    return -1;
  }

  quit_msg->id = pSystem.my_id;

  int buffer_size = get_buffer_size(quit_msg);
  char *buffer = malloc(buffer_size);
  if (!buffer)
  {
    perror("Échec de l'allocation du buffer");
    free_message(quit_msg);
    return -1;
  }

  message_to_buffer(quit_msg, buffer, buffer_size);
  send_multicast(m_send, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size);

  printf("Message de départ envoyé (ID=%u)\n", pSystem.my_id);

  free(buffer);
  free_message(quit_msg);

  return 0;
}

// Fonction pour vérifier si une enchère est terminée
int is_auction_finished(unsigned int auction_id)
{
  struct Auction *auction = find_auction(auction_id);
  if (!auction)
  {
    return 1; // Si l'enchère n'existe pas, elle est considérée comme terminée
  }

  // Si l'enchère a été explicitement marquée comme terminée
  if (auction->auction_id == 0 || auction->last_bid_time == 0)
  {
    return 1;
  }

  time_t now = time(NULL);

  // Si aucune enchère depuis AUCTION_TIMEOUT secondes, l'enchère est terminée
  if (difftime(now, auction->last_bid_time) > AUCTION_TIMEOUT)
  {
    return 1;
  }

  return 0; // Enchère en cours
}

int make_bid(int m_send) {
  unsigned int auction_id;
  unsigned int price;

  if (auctionSys.auctions == NULL) {
    fprintf(stderr, "Erreur: Système d'enchères non initialisé\n");
    return -1;
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
    return 0; // No active auctions
  }

  printf("\nEntrez l'ID de l'enchère: ");
  scanf("%u", &auction_id);
  while (getchar() != '\n'); // Vider le buffer d'entrée

  // Vérifier que l'enchère existe
  struct Auction *auction = find_auction(auction_id);
  if (!auction) {
    fprintf(stderr, "Erreur: Enchère %u introuvable\n", auction_id);
    return -1;
  }

  // Vérifier que l'enchère n'est pas terminée
  if (is_auction_finished(auction_id)) {
    fprintf(stderr, "Erreur: L'enchère %u est terminée\n", auction_id);
    return -1;
  }

  printf("Prix actuel: %u\n", auction->current_price);
  printf("Entrez votre prix: ");
  scanf("%u", &price);
  while (getchar() != '\n'); // Vider le buffer d'entrée

  // Vérifier que le prix est supérieur au prix actuel
  if (price <= auction->current_price) {
    fprintf(stderr, "Erreur: Le prix doit être supérieur au prix actuel\n");
    return -1;
  }

  struct message *msg = init_message(CODE_ENCHERE); // Code 9 - Enchérir une vente
  if (!msg) {
    perror("Échec de l'initialisation du message");
    return -1;
  }
  msg->id = pSystem.my_id;
  msg->numv = auction_id;
  msg->prix = price;

  int buffer_size = get_buffer_size(msg);
  char *buffer = malloc(buffer_size);
  if (!buffer) {
    perror("Échec de l'allocation du buffer");
    free_message(msg);
    return -1;
  }

  if (message_to_buffer(msg, buffer, buffer_size) < 0) {
    perror("Échec de la conversion du message en buffer");
    free(buffer);
    free_message(msg);
    return -1;
  }
  free_message(msg);

  if (send_multicast(m_send, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size) < 0) {
    perror("Échec de l'envoi de l'enchère");
    free(buffer);
    return -1;
  }
  free(buffer);
  if (is_auction_finished(auction_id)) {
    printf("Attention: L'enchère pourrait être terminée\n");
  }
  return 1;
}

// Fonction pour valider une enchère
int validate_bid(int m_send, unsigned int auction_id, unsigned short bidder_id, unsigned int bid_price)
{
  struct Auction *auction = find_auction(auction_id);
  if (!auction)
  {
    printf("Erreur: Enchère %u inexistante\n", auction_id);
    return -1;
  }

  // Vérifier que le prix est valide (supérieur au prix actuel)
  if (bid_price <= auction->current_price) {
    // Envoyer un message de refus (CODE_REFUS_PRIX = 15)
    struct message *refuse_msg = init_message(CODE_REFUS_PRIX);
    if (refuse_msg) {
      refuse_msg->id = pSystem.my_id;
      refuse_msg->numv = auction_id;
      refuse_msg->prix = bid_price;

      int buffer_size = get_buffer_size(refuse_msg);
      char *buffer = malloc(buffer_size);
      if (buffer) {
        message_to_buffer(refuse_msg, buffer, buffer_size);
        send_multicast(m_send, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size);
        free(buffer);
      }
      free_message(refuse_msg);
    }
    return -1;
  }

  // Envoyer un message de validation (CODE_VALIDATION = 1)
  struct message *valid_msg = init_message(CODE_VALIDATION);
  if (!valid_msg)
  {
    perror("Échec de l'initialisation du message");
    return -1;
  }

  valid_msg->id = pSystem.my_id;

  int buffer_size = get_buffer_size(valid_msg);
  char *buffer = malloc(buffer_size);
  if (!buffer)
  {
    perror("Échec de l'allocation du buffer");
    free_message(valid_msg);
    return -1;
  }

  message_to_buffer(valid_msg, buffer, buffer_size);
  send_multicast(m_send, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size);

  free(buffer);
  free_message(valid_msg);

  // Dans un cas réel, on compterait les validations reçues
  // Pour simuler le consensus, on met à jour directement
  auction->current_price = bid_price;
  auction->id_dernier_prop = bidder_id;
  auction->last_bid_time = time(NULL);

  return 0;
}

// Fonction exécutée par le thread de surveillance des enchères
void *auction_monitor(void *m_send_ptr) {
  while (monitor_running) {
    pthread_mutex_lock(&auction_mutex);
    
    for (int i = 0; i < auctionSys.count; i++) {
      struct Auction *auction = &auctionSys.auctions[i];
      
      // Ignorer les enchères déjà terminées
      if (auction->last_bid_time == 0 || auction->auction_id == 0) {
        continue;
      }

      unsigned short supervisor_id = (auction->auction_id >> 16) & 0xFFFF;
      // Seul le superviseur gère les timeouts
      if (supervisor_id == pSystem.my_id) {
        time_t now = time(NULL);
        double time_since_last_bid = difftime(now, auction->last_bid_time);
        
        if (time_since_last_bid > AUCTION_TIMEOUT) {
          printf("Timeout détecté pour l'enchère %u (%.0fs depuis dernière offre)\n", 
                 auction->auction_id, time_since_last_bid);
          
          // Finaliser l'enchère
          unsigned int auction_id = auction->auction_id;
          pthread_mutex_unlock(&auction_mutex);

          int m_send = *((int *)m_send_ptr);
          finalize_auction(m_send, auction_id);
          mark_auction_finished(auction_id);
          
          pthread_mutex_lock(&auction_mutex);
        }
      }
    }
    
    pthread_mutex_unlock(&auction_mutex);
    sleep(2); // Vérifier toutes les 2 secondes
  }
  
  return NULL;
}

// Fonction pour marquer une enchère comme terminée
void mark_auction_finished(unsigned int auction_id) {
  pthread_mutex_lock(&auction_mutex);

  struct Auction *auction = find_auction(auction_id);
  if (!auction) {
    pthread_mutex_unlock(&auction_mutex);
    return;
  }

  // Marquer l'enchère comme terminée en réinitialisant ses champs temporels
  auction->last_bid_time = 0;  // Marqueur principal de fin
  
  printf("Enchère %u marquée comme terminée\n", auction_id);

  pthread_mutex_unlock(&auction_mutex);
}

// Fonction pour créer une enchère avec un ID spécifique (pour la synchronisation)
unsigned int init_auction_with_id(struct Pair *creator, unsigned int initial_price, unsigned int specified_id) {
  pthread_mutex_lock(&auction_mutex);

  // Vérifier que creator est valide
  if (!creator) {
    fprintf(stderr, "Erreur: Créateur invalide\n");
    pthread_mutex_unlock(&auction_mutex);
    return 0;
  }

  // Vérifier si l'enchère existe déjà
  for (int i = 0; i < auctionSys.count; i++) {
    if (auctionSys.auctions[i].auction_id == specified_id) {
      // L'enchère existe déjà, on ne fait rien
      printf("L'enchère %u existe déjà, synchronisation ignorée\n", specified_id);
      pthread_mutex_unlock(&auction_mutex);
      return specified_id;
    }
  }

  // Vérifier si nous avons besoin d'augmenter la capacité
  if (auctionSys.count >= auctionSys.capacity) {
    size_t new_capacity = auctionSys.capacity * 2;
    struct Auction *new_auctions = realloc(auctionSys.auctions, new_capacity * sizeof(struct Auction));

    if (!new_auctions) {
      perror("Échec de la réallocation du tableau d'enchères");
      pthread_mutex_unlock(&auction_mutex);
      return 0;
    }

    auctionSys.auctions = new_auctions;
    auctionSys.capacity = new_capacity;

    // Initialiser les nouveaux éléments à zéro
    memset(&auctionSys.auctions[auctionSys.count], 0,
           (new_capacity - auctionSys.count) * sizeof(struct Auction));
  }

  // Ajouter la nouvelle enchère à la fin du tableau
  struct Auction *new_auction = &auctionSys.auctions[auctionSys.count];

  new_auction->auction_id = specified_id;
  new_auction->creator_id = creator->id;
  new_auction->initial_price = initial_price;
  new_auction->current_price = initial_price;
  new_auction->id_dernier_prop = creator->id;
  new_auction->start_time = time(NULL);
  new_auction->last_bid_time = time(NULL);

  // Incrémenter le compteur d'enchères APRÈS avoir initialisé tous les champs
  auctionSys.count++;

  printf("Enchère %u synchronisée avec succès (créateur: %d, prix: %u)\n",
         specified_id, creator->id, initial_price);

  pthread_mutex_unlock(&auction_mutex);
  return specified_id;
}

// Fonction pour diffuser toutes les enchères existantes
int broadcast_all_auctions(int m_send)
{
  pthread_mutex_lock(&auction_mutex);

  if (auctionSys.count == 0)
  {
    printf("Aucune enchère à diffuser\n");
    pthread_mutex_unlock(&auction_mutex);
    return 0;
  }

  // Copier les données des enchères pour éviter de garder le mutex verrouillé
  // pendant les opérations réseau
  struct Auction *auctions_copy = malloc(auctionSys.count * sizeof(struct Auction));
  if (!auctions_copy)
  {
    perror("Échec de l'allocation mémoire pour la copie des enchères");
    pthread_mutex_unlock(&auction_mutex);
    return -1;
  }

  // Copier seulement les informations nécessaires
  int count = auctionSys.count;
  for (int i = 0; i < count; i++)
  {
    auctions_copy[i].auction_id = auctionSys.auctions[i].auction_id;
    auctions_copy[i].creator_id = auctionSys.auctions[i].creator_id;
    auctions_copy[i].initial_price = auctionSys.auctions[i].initial_price;
  }

  // Libérer le mutex après avoir copié les données
  pthread_mutex_unlock(&auction_mutex);

  printf("Diffusion de %d enchères existantes...\n", count);

  int success_count = 0;

  for (int i = 0; i < count; i++)
  {
    struct message *auction_msg = init_message(CODE_NOUVELLE_VENTE);
    if (!auction_msg)
    {
      perror("Échec de l'initialisation du message");
      continue;
    }

    auction_msg->id = pSystem.my_id;
    auction_msg->numv = auctions_copy[i].auction_id;
    auction_msg->prix = auctions_copy[i].initial_price;

    if (message_set_mess(auction_msg, "Synchronisation d'enchère") < 0 ||
        message_set_sig(auction_msg, "") < 0)
    {
      perror("Échec de l'initialisation des champs du message");
      free_message(auction_msg);
      continue;
    }

    int auction_buffer_size = get_buffer_size(auction_msg);
    char *auction_buffer = malloc(auction_buffer_size);

    if (!auction_buffer)
    {
      perror("Échec de l'allocation du buffer");
      free_message(auction_msg);
      continue;
    }

    memset(auction_buffer, 0, auction_buffer_size);

    if (message_to_buffer(auction_msg, auction_buffer, auction_buffer_size) < 0)
    {
      perror("Échec de la conversion du message en buffer");
      free(auction_buffer);
      free_message(auction_msg);
      continue;
    }

    printf("Diffusion de l'enchère %u (prix=%u)...\n",
           auctions_copy[i].auction_id, auctions_copy[i].initial_price);

    // Envoyer plusieurs fois pour augmenter les chances de réception
    int send_success = 0;
    for (int j = 0; j < 2; j++) { // Réduire à 2 envois au lieu de 3
      if (send_multicast(m_send, pSystem.auction_addr, pSystem.auction_port,
                         auction_buffer, auction_buffer_size) >= 0) {
        send_success = 1;
      }
      usleep(200000); // Augmenter légèrement l'intervalle à 200ms
    }

    if (send_success) {
      success_count++;
    }

    free(auction_buffer);
    free_message(auction_msg);
  }

  free(auctions_copy);

  printf("Diffusion terminée : %d/%d enchères diffusées avec succès\n",
         success_count, count);

  return success_count;
}

int send_rejection_message(int m_send, struct message *original_msg) {
  struct message *reject_msg = init_message(CODE_REFUS_PRIX);
  if (!reject_msg) {
    return -1;
  }

  reject_msg->id = pSystem.my_id;
  reject_msg->numv = original_msg->numv;
  reject_msg->prix = original_msg->prix;

  // Envoyer le message de rejet
  int buffer_size = get_buffer_size(reject_msg);
  char *buffer = malloc(buffer_size);
  if (buffer)
  {
    message_to_buffer(reject_msg, buffer, buffer_size);
    send_multicast(m_send, pSystem.auction_addr, pSystem.auction_port, buffer, buffer_size);
    free(buffer);
  }

  free_message(reject_msg);
  return 0;
}

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