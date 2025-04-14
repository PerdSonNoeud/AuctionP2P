#include "message.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void afficher_message(struct message* iMess) {
    if (iMess == NULL) {
        printf("Message is NULL\n");
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