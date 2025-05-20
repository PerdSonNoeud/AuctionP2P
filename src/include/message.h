#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>

/**
 * Message codes for network communication
 */
#define CODE_VALIDATION         1   // Validation of a message by a peer
#define CODE_CONSENSUS          2   // Message validated by consensus (<=3 peers)
#define CODE_CONSENSUS_SUITE    20  // Additional validations (>3 peers)
#define CODE_DEMANDE_LIAISON    3   // Request to join the system
#define CODE_REPONSE_LIAISON    4   // Response with personal address
#define CODE_INFO_PAIR          5   // Send new peer information
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

/**
 * Message structure for network communication
 * Format: CODE | ID | LMESS | MESS | LSIG | SIG
 */
struct message {
    uint8_t code;       // Message type code
    uint16_t id;        // Peer ID
    uint8_t lmess;      // Message length
    char* mess;         // Message content
    uint8_t lsig;       // Signature length
    char* sig;          // Signature content
    __int128 ip;        // IP address
    uint16_t port;      // Port number
    char* cle;          // Key
    uint32_t numv;      // Auction number
    uint32_t prix;      // Price
    uint16_t nb;        // Number of elements
};

/**
 * @brief Display the content of a message
 *
 * @param iMess Pointer to the message to display
 */
void afficher_message(struct message* iMess);

/**
 * @brief Create a new message structure with the given code
 *
 * @param code The code to set in the message (see defines above)
 * @return Pointer to the newly created message
 */
struct message* new_message(const int code);

#endif /* MESSAGE_H */
