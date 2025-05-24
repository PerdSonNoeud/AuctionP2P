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
 * @brief Handle incoming auction messages
 *
 * Processes messages related to auctions received from peers.
 *
 * @param auc_sock The socket on which the message was received
 * @param m_send The socket to use for sending responses
 * @return 0 on success, negative value on error
 */
int handle_auction_message(int auc_sock, int m_send);

/**
 * @brief Create a new auction
 *
 * Prompts the user for auction details and initializes a new auction.
 * This function is typically called when the user wants to create an auction.
 *
 * @param m_send The socket to use for sending the auction creation message
 * @return 1 on success, 0 for no auctions, negative value on error
 */
int create_auction(int m_send);

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
 * @brief Create a new auction with a specified ID
 *
 * Initializes a new auction with the given creator, initial price, and auction ID.
 * This function is used primarily for synchronization between peers.
 *
 * @param creator Pointer to the peer creating the auction
 * @param initial_price Starting price for the auction
 * @param specified_id The specific auction ID to use
 * @return The auction identifier on success, 0 on failure
 */
unsigned int init_auction_with_id(struct Pair *creator, unsigned int initial_price, unsigned int specified_id);

/**
 * @brief Start an auction
 *
 * Marks an auction as started and sets its start time.
 *
 * @param m_send The socket to use for sending the auction start message
 * @param auction_id The identifier of the auction to start
 * @return 0 on success, negative value on error
 */
int start_auction(int m_send, unsigned int auction_id);

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
 * @brief Make a bid in an auction
 *
 * Prompts the user for a bid amount and sends it to the auction system.
 * This function is typically called when the user wants to place a bid.
 *
 * @param m_send The socket to use for sending the bid
 * @return 1 on success, 0 for no auctions, negative value on error
 */
int make_bid(int m_send);

/**
 * @brief Validate a bid for an auction
 *
 * Checks if a bid is valid and updates the auction state if it is.
 *
 * @param m_send The socket to use for sending responses
 * @param auction_id The identifier of the auction
 * @param bidder_id The identifier of the peer making the bid
 * @param bid_price The price offered in the bid
 * @return 1 if the bid is valid and accepted, 0 if rejected, negative value on error
 */
int validate_bid(int m_send, unsigned int auction_id, unsigned short bidder_id, unsigned int bid_price);

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
 * @param m_send The socket to use for sending responses
 * @param msg The bid message to handle
 * @return 0 on success, negative value on error
 */
int handle_bid(int m_send, struct message *msg);

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
 * @param m_send The socket to use for sending the warning
 * @param auction_id The identifier of the auction
 * @return 0 on success, negative value on error
 */
int send_end_warning(int m_send, unsigned int auction_id);

/**
 * @brief Finalize an auction
 *
 * Finalizes an auction by sending a message (CODE=12) with the winner information.
 *
 * @param m_send The socket to use for sending the finalization message
 * @param auction_id The identifier of the auction
 * @return 0 on success, negative value on error
 */
int finalize_auction(int m_send, unsigned int auction_id);

/**
 * @brief Quit the auction system
 *
 * Sends a message (CODE=13) to inform that the peer is leaving the auction system.
 *
 * @param m_send The socket to use for sending the quit message
 * @return 0 on success, negative value on error
 */
int quit_auction_system(int m_send);

/**
 * @brief Monitoring function for auctions
 *
 * Thread function that periodically checks the status of auctions and 
 * triggers warnings or finalization as needed.
 *
 * @param m_send A pointer to the socket to use for sending messages
 */
void *auction_monitor(void *m_send);

/**
 * @brief Broadcast all existing auctions to all peers
 *
 * Sends information about all existing auctions to the multicast group
 * to ensure all peers are synchronized.
 *
 * @param m_send The socket to use for sending the auction information
 * @return The number of auctions broadcasted, or negative value on error
 */
int broadcast_all_auctions(int m_send);

/**
 * @brief mark an auction as finished
 * 
 * This function marks an auction as finished by resetting its fields.
 * It is typically called when the auction has ended or been finalized.
 * 
 * @param auction_id The identifier of the auction to mark as finished
 * 
 * 
 */
void mark_auction_finished(unsigned int auction_id);

/**
 * @brief send a rejection message
 * 
 * This function sends a rejection message (CODE=15) to the multicast group
 * when a bid is not accepted. It includes the auction ID and the proposed price.
 *
 * @param m_send The socket to use for sending the rejection message
 */
int send_rejection_message(int m_send, struct message *msg);

/**
 * @brief Display all active auctions
 *
 * Prints the details of all currently active auctions to the console.
 */
void display_auctions();

#endif /* AUCTION_H */
