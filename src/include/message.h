#include <stdint.h>

#define CODE_VALIDATION         1   // Validation d'un message par un pair
#define CODE_CONSENSUS          2   // Message validé par consensus (<=3 pairs)
#define CODE_CONSENSUS_SUITE    20  // Suite des validations (>3 pairs)
#define CODE_DEMANDE_LIAISON    3   // Demande pour rejoindre le système
#define CODE_REPONSE_LIAISON    4   // Réponse avec adresse perso
#define CODE_INFO_PAIR          5   // Envoi des infos du nouveau pair
#define CODE_INFO_PAIR_BROADCAST 6  // Diffusion des infos du nouveau pair
#define CODE_INFO_SYSTEME       7   // Infos sur le système (adresse enchères + pairs)
#define CODE_NOUVELLE_VENTE     8   // Initiation d'une vente
#define CODE_ENCHERE            9   // Enchère d'un pair
#define CODE_ENCHERE_SUPERVISEUR 10 // Enchère retransmise par le superviseur
#define CODE_FIN_VENTE_WARNING  11  // Avertissement de fin de vente
#define CODE_FIN_VENTE          12  // Fin définitive de la vente
#define CODE_QUIT_SYSTEME       13  // Quitter le système
#define CODE_REFUS_CONCURRENT   14  // Refus enchère concurrente
#define CODE_REFUS_PRIX         15  // Refus enchère prix invalide
#define CODE_ANNUL_SUPERVISEUR  16  // Annulation suite à disparition superviseur
#define CODE_ANNUL_DEMANDE      17  // Annulation de sa propre demande
#define CODE_RETRAIT_PAIRS      18  // Retrait de pairs absents

#define SEPARATOR "|"
/*
+++++++++++++++++++++++++++++++++++++++++++++
| CODE | ID | LMESS | MESS | LSIG | SIG 
+++++++++++++++++++++++++++++++++++++++++++++
*/
struct message {
    uint8_t code;
    uint16_t id;
    uint8_t lmess;
    char* mess;
    uint8_t lsig;
    char* sig;
    __int128 ip;
    uint16_t port;
    char* cle;
    uint32_t numv;
    uint32_t prix;
    uint16_t nb;
};

void afficher_message(struct message* iMess);