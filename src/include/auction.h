#include <netinet/in.h>
#include <time.h>

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

// TDODO : faire les fonction utils pour es enchères

// RODO faire une structure pour gérer l'ensemble des enchères

// TODO : faire les fonctions pour gérer toutes les enchères