#include <stdint.h>

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