#ifndef AUCTION_H
#define AUCTION_H

#include <netinet/in.h>
#include <time.h>
#include "pairs.h"

/**
 * @brief Structure to store auction information
 */
struct Auction {
  unsigned int auction_id;        // Auction identifier
  unsigned short creator_id;      // Creator peer identifier
  unsigned int initial_price;     // Initial auction price
  unsigned int current_price;     // Current auction price (last valid bid)
  unsigned short id_dernier_prop; // Identifier of the peer who made the last bid
  time_t start_time;              // Auction start time
  time_t last_bid_time;           // Last bid timestamp
  // Potential additional fields for supervisor, etc.
};

/**
 * @brief Structure to manage multiple auctions
 */
struct AuctionSystem {
  struct Auction *auctions;    // Array of auctions
  int count;            // Current number of auctions
  int capacity;         // Maximum capacity of the auctions array
};

/**
 * @brief Initialize the auction system
 *
 * Sets up the auction system with default values and prepares it for use.
 *
 * @return 0 on success, negative value on error
 */
int init_auction_system();

/**
 * @brief Clean up resources used by the auction system
 *
 * Frees memory and releases other resources used by the auction system.
 */
void cleanup_auction_system();

/**
 * @brief Create a new auction
 *
 * Initializes a new auction with the given creator and initial price.
 *
 * @param creator Pointer to the peer creating the auction
 * @param initial_price Starting price for the auction
 * @return The auction identifier on success, 0 on failure
 */
unsigned int init_auction(struct Pair *creator, unsigned int initial_price);

/**
 * @brief Start an auction
 *
 * Marks an auction as started and sets its start time.
 *
 * @param auction_id The identifier of the auction to start
 */
void start_auction(unsigned int auction_id);

/**
 * @brief Check if an auction is finished
 *
 * Determines whether an auction has ended based on its timing and state.
 *
 * @param auction_id The identifier of the auction to check
 * @return 1 if the auction is finished, 0 if still active, negative value on error
 */
int is_auction_finished(unsigned int auction_id);

/**
 * @brief Validate a bid for an auction
 *
 * Checks if a bid is valid and updates the auction state if it is.
 *
 * @param auction_id The identifier of the auction
 * @param bidder_id The identifier of the peer making the bid
 * @param bid_price The price offered in the bid
 * @return 1 if the bid is valid and accepted, 0 if rejected, negative value on error
 */
int validate_bid(unsigned int auction_id, unsigned short bidder_id, unsigned int bid_price);

#endif /* AUCTION_H */
