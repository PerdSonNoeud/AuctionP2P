#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include "include/auction.h"
#include "include/multicast.h"
#include "include/message.h"

AuctionSystem auctionSys;

#define AUCTION_TIMEOUT 60
#define MIN_VALIDATION_COUNT 3  // Nombre minimum de validations pour le consensus

int init_auction_system() {
    auctionSys.auctions = malloc(10 * sizeof(Auction));
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

unsigned int init_auction(Pair *creator, unsigned int initial_price) {
    if (auctionSys.count >= auctionSys.capacity) {
        auctionSys.capacity *= 2;
        auctionSys.auctions = realloc(auctionSys.auctions, auctionSys.capacity * sizeof(Auction));
    }

    Auction *new_auction = &auctionSys.auctions[auctionSys.count++];
    new_auction->auction_id = auctionSys.count;
    new_auction->creator_id =  creator->id;
    new_auction->initial_price = initial_price;
    new_auction->current_price = initial_price;
    new_auction->last_bid_time = time(NULL);

    return new_auction->auction_id;
}

void start_auction(unsigned int auction_id) {
    Auction *auction = &auctionSys.auctions[auction_id - 1]; // -1 car les IDs commencent à 1
    auction->start_time = time(NULL);

    // Annoncer la nouvelle vente au réseau
    int send_sock = setup_multicast_sender();
    if (send_sock < 0) {
        perror("Échec de création du socket d'envoi multicast");
        return;
    }

    // Préparer le message d'annonce de nouvelle vente (CODE_NOUVELLE_VENTE = 8)
    uint8_t message[100];
    memset(message, 0, sizeof(message));

    extern PairSystem pSystem;
    message[0] = 8; // CODE_NOUVELLE_VENTE
    memcpy(&message[1], &pSystem.my_id, sizeof(pSystem.my_id));
    memcpy(&message[3], &auction->auction_id, sizeof(auction->auction_id));
    memcpy(&message[7], &auction->initial_price, sizeof(auction->initial_price));

    // Envoyer le message à l'adresse d'enchères
    if (send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port,
                     message, sizeof(message)) < 0) {
        perror("Échec de l'envoi de l'annonce de nouvelle vente");
    } else {
        printf("Nouvelle vente %u lancée avec prix initial %u\n",
               auction_id, auction->initial_price);
    }

    close(send_sock);
}

// Fonction pour vérifier si une enchère est terminée
int is_auction_finished(unsigned int auction_id) {
    if (auction_id == 0 || (int) auction_id > auctionSys.count) {
        return -1; // Enchère inexistante
    }

    Auction *auction = &auctionSys.auctions[auction_id - 1];
    time_t now = time(NULL);

    // Si aucune enchère depuis AUCTION_TIMEOUT secondes, l'enchère est terminée
    if (difftime(now, auction->last_bid_time) > AUCTION_TIMEOUT) {
        return 1; // Enchère terminée
    }

    return 0; // Enchère en cours
}

// Fonction pour valider une enchère
int validate_bid(unsigned int auction_id, unsigned short bidder_id, unsigned int bid_price) {
    if (auction_id == 0 || (int) auction_id > auctionSys.count) {
        printf("Erreur: Enchère %u inexistante\n", auction_id);
        return -1;
    }
    
    Auction *auction = &auctionSys.auctions[auction_id - 1];
    
    // Vérifier que le prix est valide (supérieur au prix actuel)
    if (bid_price <= auction->current_price) {
        // Envoyer un message de refus (CODE_REFUS_PRIX = 15)
        int send_sock = setup_multicast_sender();
        if (send_sock >= 0) {
            uint8_t message[100];
            memset(message, 0, sizeof(message));
            
            message[0] = 15; // CODE_REFUS_PRIX
            extern PairSystem pSystem;
            memcpy(&message[1], &pSystem.my_id, sizeof(pSystem.my_id));
            memcpy(&message[3], &auction_id, sizeof(auction_id));
            memcpy(&message[7], &bid_price, sizeof(bid_price));
            
            send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port, 
                         message, sizeof(message));
            close(send_sock);
        }
        return -1;
    }
    
    // Envoyer un message de validation (CODE_VALIDATION = 1)
    int send_sock = setup_multicast_sender();
    if (send_sock < 0) {
        perror("Échec de création du socket d'envoi multicast");
        return -1;
    }
    
    uint8_t message[100];
    memset(message, 0, sizeof(message));
    
    extern PairSystem pSystem;
    message[0] = 1; // CODE_VALIDATION
    memcpy(&message[1], &pSystem.my_id, sizeof(pSystem.my_id));
    
    // Dans un cas réel, on ajouterait ici des données pour la vérification
    // (signature, etc.) et on ferait un comptage des validations reçues
    
    // Pour simuler un consensus, on met à jour immédiatement
    auction->current_price = bid_price;
    auction->id_dernier_prop = bidder_id;
    auction->last_bid_time = time(NULL);
    
    send_multicast(send_sock, pSystem.auction_addr, pSystem.auction_port, 
                 message, sizeof(message));
    close(send_sock);
    
    return 0;
}

