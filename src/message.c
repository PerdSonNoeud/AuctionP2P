#include "include/message.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void afficher_message(struct message* iMess) {
    if (iMess == NULL) {
        printf("Le Message est NULL\n");
        return;
    }
    printf("Message:\n");
    printf("  Code: %d\n", iMess->code);
    printf("  ID: %d\n", iMess->id);
    printf("  Lmess: %d\n", iMess->lmess);
    printf("  Mess: %s\n", iMess->mess);
    printf("  Lsig: %d\n", iMess->lsig);
    printf("  Sig: %s\n", iMess->sig);
}

struct message* init_code_validation(const int code);


struct message* new_message(const int code) {
  struct message* iMess = malloc(sizeof(struct message));
  if(iMess == NULL) {
    perror("Allocation ratée");
    return NULL;
  }

  // Initialize the structure with default values
  iMess->id = 0; // uint16_t is primitive type, not a pointer
  iMess->lmess = 0;
  iMess->mess = NULL; // Initialize at NULL before allocation
  iMess->lsig = 0;
  iMess->sig = NULL; // Same here
  iMess->code = code;

  switch (iMess->code) {
    /*
    * +++++++++++++++++++++++++++++++++++++++++++++
    * | CODE = 1 | ID | LMESS | MESS | LSIG | SIG |
    * +++++++++++++++++++++++++++++++++++++++++++++
    */
    case CODE_VALIDATION:

      break;

    case CODE_CONSENSUS:
      /*
      * ++++++++++++++++++++++++++++++++++++++++++++++++++
      * | CODE = 2 | ID | LMESS | MESS | LSIG | SIG | NB |
      * ++++++++++++++++++++++++++++++++++++++++++++++++++
      */
      if (iMess->lmess > 0) {
        iMess->mess = malloc(sizeof(char) * iMess->lmess);
        if (iMess->mess == NULL) {
          perror("Allocation mémoire pour message échouée");
          free(iMess);
          return NULL;
        }
      }
      /*
      * +++++++++++++++++++
      * | ID | LSIG | SIG |
      * +++++++++++++++++++
      */
      if(iMess->lsig > 0) {
        iMess->sig = malloc(sizeof(char) * iMess->lsig);
        if (iMess->sig == NULL) {
          perror("Allocation mémoire pour signature échouée");
          free(iMess->mess);
          free(iMess);
          return NULL;
        }
      }
      break;

    default:
      /*
      * For code = CODE_CONSENSUS_SUITE, we don't need to allocate anything since NB is a primitive type (int)
      * +++++++++++++++++++++++
      * | CODE = 20 | ID | NB |
      * +++++++++++++++++++++++
      */
      break;
  }
  return iMess;
}

