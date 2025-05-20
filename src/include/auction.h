#include <netinet/in.h>
#include <time.h>
#include "pairs.h"

typedef struct {
    unsigned int auction_id;        // id vente
    unsigned short creator_id;      // ide du pair créateur de la vente
    unsigned int initial_price;     // Prix initial de la vente
    unsigned int current_price;     // Prix actuel de la vente (dernière enchère validée)
    unsigned short id_dernier_prop;  // L'identifiant du pair ayant fait la dernière enchère
    time_t start_time;              // Début temsp de la vente
    time_t last_bid_time;           // temps Dernière enchère
    // ajoute des champs pour le supervasor peut etre ect 
} Auction;

typedef struct {
    Auction *auctions;
    int count;
    int capacity;
} AuctionSystem;

// Fonctions de gestion des enchères
int init_auction_system();
void cleanup_auction_system();
unsigned int init_auction(Pair *creator, unsigned int initial_price);
void start_auction(unsigned int auction_id);
int is_auction_finished(unsigned int auction_id);
int validate_bid(unsigned int auction_id, unsigned short bidder_id, unsigned int bid_price);

// TDODO : faire les fonction utils pour es enchères

// RODO faire une structure pour gérer l'ensemble des enchères

// TODO : faire les fonctions pour gérer toutes les enchères