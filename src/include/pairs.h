#ifndef PAIRS_H
#define PAIRS_H

#include <netinet/in.h>

/**
 * @brief Structure to store peer information
 */
typedef struct {
    unsigned short id;      // Peer identifier
    struct in6_addr ip;     // Peer IPv6 address
    unsigned short port;    // Peer communication port
    int active;             // Peer status (1 = active, 0 = inactive)
} Pair;

/**
 * @brief Structure to manage the P2P system
 */
typedef struct {
    Pair *pairs;                // Array of peers
    int count;                  // Current number of peers
    int capacity;               // Maximum capacity of the peers array
    unsigned short my_id;       // Local peer identifier
    struct in6_addr my_ip;      // Local peer IPv6 address
    unsigned short my_port;     // Local peer communication port
    char liaison_addr[46];      // Multicast liaison address
    int liaison_port;           // Multicast liaison port
    char auction_addr[46];      // Multicast auction address
    int auction_port;           // Multicast auction port
} PairSystem;

/**
 * @brief Initialize the peer system
 *
 * Sets up the peer system with default values and prepares it for use.
 *
 * @return 0 on success, negative value on error
 */
int init_pairs();

/**
 * @brief Join an auction network via multicast
 *
 * Attempts to join an existing auction network by sending a join request
 * and waiting for a response.
 *
 * @return 0 on success, negative value on error
 */
int join_auction();

/**
 * @brief Handle join requests from other peers
 *
 * Processes incoming join requests on the given socket and updates
 * the peer system accordingly.
 *
 * @param sock Socket to use for communication
 * @return 1 if a peer was added, 0 if no peer was added, negative value on error
 */
int handle_join(int sock);

/**
 * @brief Add a new peer to the system
 *
 * Adds a new peer with the specified details to the peer system.
 *
 * @param id Peer identifier
 * @param ip Peer IPv6 address
 * @param port Peer communication port
 * @return 0 on success, negative value on error
 */
int add_pair(unsigned short id, struct in6_addr ip, unsigned short port);

#endif /* PAIRS_H */
