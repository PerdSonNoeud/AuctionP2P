#ifndef AUCTION_H
#define AUCTION_H

#include <netinet/in.h>
#include <time.h>
#include "pairs.h"
#include "message.h"

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

/**
 * @brief Generate a unique auction ID
 *
 * Creates a unique auction identifier based on the local peer ID and a counter.
 *
 * @return A unique auction identifier
 */
unsigned int generate_auction_id();

/**
 * @brief Find an auction by its ID
 *
 * Searches for an auction with the specified ID in the auction system.
 *
 * @param auction_id The auction identifier to search for
 * @return Pointer to the auction if found, NULL otherwise
 */
struct Auction* find_auction(unsigned int auction_id);

/**
 * @brief Handle a bid message
 *
 * Processes a bid message (CODE=9) received from a peer.
 *
 * @param msg The bid message to handle
 * @return 0 on success, negative value on error
 */
int handle_bid(struct message *msg);

/**
 * @brief Handle a supervisor bid message
 *
 * Processes a bid message relayed by the supervisor (CODE=10).
 *
 * @param msg The supervisor bid message to handle
 * @return 0 on success, negative value on error
 */
int handle_supervisor_bid(struct message *msg);

/**
 * @brief Send an auction end warning
 *
 * Sends a message (CODE=11) to warn that an auction is about to end.
 *
 * @param auction_id The identifier of the auction
 * @return 0 on success, negative value on error
 */
int send_end_warning(unsigned int auction_id);

/**
 * @brief Finalize an auction
 *
 * Finalizes an auction by sending a message (CODE=12) with the winner information.
 *
 * @param auction_id The identifier of the auction
 * @return 0 on success, negative value on error
 */
int finalize_auction(unsigned int auction_id);

/**
 * @brief Quit the auction system
 *
 * Sends a message (CODE=13) to inform that the peer is leaving the auction system.
 *
 * @return 0 on success, negative value on error
 */
int quit_auction_system();

/**
 * @brief Monitoring function for auctions
 *
 * Thread function that periodically checks the status of auctions and 
 * triggers warnings or finalizations as needed.
 *
 * @param arg Thread argument (not used)
 * @return NULL
 */
void *auction_monitor(void *arg);

#endif /* AUCTION_H */
