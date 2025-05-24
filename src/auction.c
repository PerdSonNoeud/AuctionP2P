#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include "include/auction.h"
#include "include/sockets.h"
#include "include/message.h"

struct AuctionSystem auctionSys;

#define AUCTION_TIMEOUT 60
#define MIN_VALIDATION_COUNT 3  // Minimum number of validations for consensus

int init_auction_system() {
  auctionSys.auctions = malloc(10 * sizeof(struct Auction));
  if (!auctionSys.auctions) {
    perror("malloc a échoué");
    return -1;
  }

  auctionSys.count = 0;
  auctionSys.capacity = 10;
  return 0;
}

void cleanup_auction_system() {
  free(auctionSys.auctions);
  auctionSys.auctions = NULL;
  auctionSys.count = 0;
  auctionSys.capacity = 0;
}

unsigned int init_auction(struct Pair *creator, unsigned int initial_price) {
  if (auctionSys.count >= auctionSys.capacity) {
    auctionSys.capacity *= 2;
    auctionSys.auctions = realloc(auctionSys.auctions, auctionSys.capacity * sizeof(struct Auction));
  }

  struct Auction *new_auction = &auctionSys.auctions[auctionSys.count++];
  new_auction->auction_id = auctionSys.count;
  new_auction->creator_id =  creator->id;
  new_auction->initial_price = initial_price;
  new_auction->current_price = initial_price;
  new_auction->last_bid_time = time(NULL);

  return new_auction->auction_id;
}

void start_auction(unsigned int auction_id) {
  struct Auction *auction = &auctionSys.auctions[auction_id - 1]; // -1 because IDs start at 1
  auction->start_time = time(NULL);

  // Announce the new auction to the network
  int send_sock = setup_multicast_sender();
  if (send_sock < 0) {
    perror("Échec de création du socket d'envoi multicast");
    return;
  }

  // Prepare the new auction announcement message (CODE_NOUVELLE_VENTE = 8)
  uint8_t message[100];
  memset(message, 0, sizeof(message));

  extern struct PairSystem pSystem;
  message[0] = 8; // CODE_NOUVELLE_VENTE
  memcpy(&message[1], &pSystem.my_id, sizeof(pSystem.my_id));
  memcpy(&message[3], &auction->auction_id, sizeof(auction->auction_id));
  memcpy(&message[7], &auction->initial_price, sizeof(auction->initial_price));

  // Send the message to the auction address
  if (send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port, message, sizeof(message)) < 0) {
    perror("Échec de l'envoi de l'annonce de nouvelle vente");
  } else {
    printf("Nouvelle vente %u lancée avec prix initial %u\n", auction_id, auction->initial_price);
  }

  close(send_sock);
}

// Function to check if an auction is finished
int is_auction_finished(unsigned int auction_id) {
  if (auction_id == 0 || (int) auction_id > auctionSys.count) {
    return -1; // Auction does not exist
  }

  struct Auction *auction = &auctionSys.auctions[auction_id - 1];
  time_t now = time(NULL);

  // If no bid for AUCTION_TIMEOUT seconds, the auction is finished
  if (difftime(now, auction->last_bid_time) > AUCTION_TIMEOUT) {
    return 1; // Auction finished
  }

  return 0; // Auction ongoing
}

// Function to validate a bid
int validate_bid(unsigned int auction_id, unsigned short bidder_id, unsigned int bid_price) {
  if (auction_id == 0 || (int) auction_id > auctionSys.count) {
    printf("Erreur: Enchère %u inexistante\n", auction_id);
    return -1;
  }

  struct Auction *auction = &auctionSys.auctions[auction_id - 1];

  // Check that the price is valid (greater than the current price)
  if (bid_price <= auction->current_price) {
    // Send a refusal message (CODE_REFUS_PRIX = 15)
    int send_sock = setup_multicast_sender();
    if (send_sock >= 0) {
      uint8_t message[100];
      memset(message, 0, sizeof(message));

      message[0] = 15; // CODE_REFUS_PRIX
      extern struct PairSystem pSystem;
      memcpy(&message[1], &pSystem.my_id, sizeof(pSystem.my_id));
      memcpy(&message[3], &auction_id, sizeof(auction_id));
      memcpy(&message[7], &bid_price, sizeof(bid_price));

      send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port, message, sizeof(message));
      close(send_sock);
    }
    return -1;
  }

  // Send a validation message (CODE_VALIDATION = 1)
  int send_sock = setup_multicast_sender();
  if (send_sock < 0) {
    perror("Échec de création du socket d'envoi multicast");
    return -1;
  }

  uint8_t message[100];
  memset(message, 0, sizeof(message));

  extern struct PairSystem pSystem;
  message[0] = 1; // CODE_VALIDATION
  memcpy(&message[1], &pSystem.my_id, sizeof(pSystem.my_id));

  // In a real case, we would add verification data here
  // (signature, etc.) and count the validations received

  // To simulate consensus, update immediately
  auction->current_price = bid_price;
  auction->id_dernier_prop = bidder_id;
  auction->last_bid_time = time(NULL);

  send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port, message, sizeof(message));
  close(send_sock);

  return 0;
}
