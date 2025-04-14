#include "utils.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


void remplir_buffer(void* iMess, int iSize, int* iOffset, void* oBuffer) {
    printf("remplir_buffer: iMess: %p, iSize: %d, iOffset: %d, oBuffer: %p\n", iMess, iSize, *iOffset, oBuffer);
    if(iMess == NULL || oBuffer == NULL) {
        perror("Error: iMess or oBuffer is NULL");
        return;
    }
    memcpy(oBuffer + *iOffset, iMess, iSize);
    *iOffset += iSize;
    memcpy(oBuffer + *iOffset, SEPARATOR, sizeof(SEPARATOR));
    *iOffset += sizeof(SEPARATOR);
    printf("Buffer: %s\n", (char*)oBuffer);
}

void* struct_to_buffer(struct message* iMess) {
    if(iMess == NULL) {
        return NULL;
    }
    void* buffer = NULL;
    switch (iMess->code) {
        case 1:
            if(iMess->mess == NULL) {
                perror("Error: message is NULL");
                return NULL;
            }
            if(iMess->lsig == 0) {
                buffer = malloc(sizeof(iMess->code) + sizeof(iMess->id) + sizeof(iMess->lmess) + sizeof(iMess->mess) + 4*sizeof(char));
            }
            else {
                buffer = malloc(sizeof(iMess->code) + sizeof(iMess->id) + sizeof(iMess->lmess) + sizeof(iMess->mess) + sizeof(iMess->lsig) + sizeof(iMess->sig)+ 6*sizeof(char));
            }
            int offset = 0;
            remplir_buffer(&iMess->code, sizeof(uint8_t), &offset, buffer);
            remplir_buffer(&iMess->id, sizeof(iMess->id), &offset, buffer);
            remplir_buffer(&iMess->lmess, sizeof(iMess->lmess), &offset, buffer);
            remplir_buffer(iMess->mess, iMess->lmess, &offset, buffer);
            if(iMess->lsig != 0) {
                remplir_buffer(&iMess->lsig, sizeof(iMess->lsig), &offset, buffer);
                remplir_buffer(iMess->sig, iMess->lsig, &offset, buffer);
            }
            break;
        
        default:
            break;
    }
    return buffer;
}

void buffer_to_struct(void* iBuffer, struct message* oMess) {
    if(iBuffer == NULL) {
        perror("Error: iBuffer or oMess is NULL");
        return;
    }
    if (oMess == NULL) {
        oMess = malloc(sizeof(struct message));
    }

    
    char * token = strtok ( iBuffer, SEPARATOR );
    //code
    oMess->code = (uint8_t)atoi(token);
    token = strtok ( NULL, SEPARATOR );
    printf("code: %d\n", oMess->code);
    //id
    oMess->id = (uint16_t)atoi(token);
    token = strtok ( NULL, SEPARATOR );
    printf("id: %d\n", oMess->id);
    //lmess
    oMess->lmess = (uint8_t)atoi(token);
    token = strtok ( NULL, SEPARATOR );
    //mess
    oMess->mess = malloc(oMess->lmess);
    memcpy(oMess->mess, token, oMess->lmess);
    token = strtok ( NULL, SEPARATOR );
    //lsig
    oMess->lsig = (uint8_t)atoi(token);
    token = strtok ( NULL, SEPARATOR );
    //sig
    if(oMess->lsig != 0) {
        oMess->sig = malloc(oMess->lsig);
        memcpy(oMess->sig, token, oMess->lsig);
    }
    else {
        oMess->sig = NULL;
    }
    token = strtok ( NULL, SEPARATOR );
}

int main(int argc, char const *argv[])
{
    struct message* mess = malloc(sizeof(struct message));
    mess->code = 1;
    mess->id = 1234;
    mess->lmess = 5;
    mess->mess = "Hello";
    mess->lsig = 0;
    mess->sig = NULL;
    void* buff = struct_to_buffer(mess);
    struct message* mess2 = malloc(sizeof(struct message));
    buffer_to_struct(buff, mess2);
    afficher_message(mess2);
    return 0;
}
