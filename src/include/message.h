#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

/**
 * Message codes for network communication
 */
#define CODE_VALIDATION         1   // Validation of a message by a peer
#define CODE_CONSENSUS          2   // Message validated by consensus (<=3 peers)
#define CODE_CONSENSUS_SUITE    20  // Additional validations (>3 peers)

#define CODE_DEMANDE_LIAISON    3   // Request to join the system
#define CODE_REPONSE_LIAISON    4   // Response with personal address
#define CODE_INFO_PAIR          5   // Send new peer information
#define CODE_ID_ACCEPTED        50  // Added the new peer's ID
#define CODE_ID_CHANGED         51  // Changed the new peer's ID

#define CODE_INFO_PAIR_BROADCAST 6  // Broadcast new peer information
#define CODE_INFO_SYSTEME       7   // System information (auction address + peers)
#define CODE_NOUVELLE_VENTE     8   // Initiate a new auction
#define CODE_ENCHERE            9   // Bid from a peer
#define CODE_ENCHERE_SUPERVISEUR 10 // Bid relayed by the supervisor
#define CODE_FIN_VENTE_WARNING  11  // Auction ending warning
#define CODE_FIN_VENTE          12  // Final auction closure
#define CODE_QUIT_SYSTEME       13  // Leave the system
#define CODE_REFUS_CONCURRENT   14  // Reject concurrent bid
#define CODE_REFUS_PRIX         15  // Reject bid with invalid price
#define CODE_ANNUL_SUPERVISEUR  16  // Cancellation due to supervisor disappearance
#define CODE_ANNUL_DEMANDE      17  // Cancel own request
#define CODE_RETRAIT_PAIRS      18  // Remove absent peers

#define SEPARATOR "|"

struct info {
  uint16_t id;          // Peer ID
  struct in6_addr ip;   // Peer IP address
  uint16_t port;        // Peer port number
  char cle[60];         // Key
};

/**
 * Message structure for network communication
 * Format: CODE | ID | LMESS | MESS | LSIG | SIG
 */
struct message {
  uint8_t code;         // Message type code
  uint16_t id;          // Peer ID
  uint8_t lmess;        // Message length
  char* mess;           // Message content
  uint8_t lsig;         // Signature length
  char* sig;            // Signature content
  struct in6_addr ip;   // IP address
  uint16_t port;        // Port number
  char cle[60];         // Key
  uint32_t numv;        // Auction number
  uint32_t prix;        // Price
  int nb;               // Number of elements
  struct info *info;    // Array of peer information
};

/**
 * @brief Display the content of a message
 *
 * @param msg Pointer to the message to display
 */
void print_message(struct message* msg);

/**
 * @brief Create a new message structure with the given code
 *
 * @param code The code to set in the message (see defines above)
 * @return Pointer to the newly created message
 */
struct message* init_message(const int code);

/**
 * @brief Set the message in the message structure
 *
 * @param msg Pointer to the message to modify
 * @param mess The message to set
 * @return 0 on success, -1 on error
 */
int message_set_mess(struct message* msg, const char* mess);

/**
 * @brief Set the signature in the message structure
 *
 * @param msg Pointer to the message to modify
 * @param sig The signature to set
 * @return 0 on success, -1 on error
 */
int message_set_sig(struct message* msg, const char* sig);

/**
 * @brief Set the ip address in the message structure
 *
 * @param msg Pointer to the message to modify
 * @param ip The ip address to set
 * @return 0 on success, -1 on error
 */
int message_set_ip(struct message* msg, struct in6_addr ip);

/**
 * @brief Set the port in the message structure
 *
 * @param msg Pointer to the message to modify
 * @param port The port to set
 * @return 0 on success, -1 on error
 */
 int message_set_port(struct message* msg, uint16_t port);

/**
* @brief Set the key in the message structure
*
* @param msg Pointer to the message to modify
* @param cle The key to set
* @return 0 on success, -1 on error
*/
int message_set_cle(struct message* msg, const char* cle);

/**
 * @brief Set the auction number in the message structure
 *
 * @param msg Pointer to the message to modify
 * @param numv The auction number to set
 * @return 0 on success, -1 on error
 */
int message_set_nb(struct message *msg, int nb);

/**
 * @brief Initialize an info structure
 *
 * @param info Pointer to the info structure to initialize
 * @param id Peer ID
 * @param ip Peer IPv6 address
 * @param port Peer communication port
 * @return 0 on success, -1 on error
 */
int init_info(struct info *info, int id, struct in6_addr ip, uint16_t port);

/**
 * @brief Set the info in the message structure
 *
 * @param msg Pointer to the message to modify
 * @param index Index of the info to set
 * @param info Pointer to the info structure to set
 * @return 0 on success, -1 on error
 */
int message_set_info(struct message *msg, int index, struct info *info);

/**
 * @brief Free the memory allocated for a message structure
 *
 * @param msg Pointer to the message to free
 * @return 0 on success, -1 on error
 */
int free_message(struct message* msg);

#endif /* MESSAGE_H */
