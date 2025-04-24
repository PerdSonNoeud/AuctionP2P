#include "utils.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int nbDigits (int n) {
    if (n < 10) return 1;
    if (n < 100) return 2;
    if (n < 1000) return 3;
    if (n < 10000) return 4;
    if (n < 100000) return 5;
    return 0;
}

char* struct_to_buffer(struct message* iMess) {
    if (iMess == NULL) {
        perror("Error: iMess is NULL");
        return NULL;
    }

    // Calculate the size of the resulting buffer
    size_t buffer_size = 0;
    buffer_size += nbDigits(iMess->code); // For CODE (max 3 digits)
    buffer_size += nbDigits(iMess->id) + sizeof(char); // For ID (max 10 digits) + separator
    buffer_size += nbDigits(iMess->lmess) + sizeof(char); // For LMESS (max 5 digits) + separator
    buffer_size += iMess->lmess + sizeof(char); // For MESS + separator
    buffer_size += nbDigits(iMess->lsig) + sizeof(char); // For LSIG (max 5 digits) + separator

    if (iMess->lsig > 0) {
        buffer_size += iMess->lsig + sizeof(char); // For SIG + separator
    }
    //buffer_size += sizeof(uint8_t) + sizeof(char); // For NB (max 5 digits) + separator

    // Allocate memory for the buffer
    buffer_size += 1; // +1 for ending null character
    char* buffer = malloc(buffer_size);
    if (buffer == NULL) {
        perror("malloc failed");
        return NULL;
    }

    // Fill the buffer with the serialized data
    int offset = 0;
    offset += snprintf(buffer + offset, buffer_size - offset, "%d", iMess->code);
    offset += snprintf(buffer + offset, buffer_size - offset, "|%u", iMess->id);
    offset += snprintf(buffer + offset, buffer_size - offset, "|%d", iMess->lmess);
    printf("sizeof mess: %zu\n", buffer_size);
    offset += snprintf(buffer + offset, buffer_size - offset, "|%s", iMess->mess);
    offset += snprintf(buffer + offset, buffer_size - offset, "|%d", iMess->lsig);

    if (iMess->lsig > 0) {
        offset += snprintf(buffer + offset, buffer_size - offset, "|%s", iMess->sig);
    }
    //offset += snprintf(buffer + offset, buffer_size - offset, "%u", iMess->nb);

    return buffer;
}


void* define_struct() {
    struct message* iMess = malloc(sizeof(struct message));
    if(iMess == NULL) {
        perror("Allocation ratée");
        return NULL;
    }
    switch (iMess->code) {
        /*+++++++++++++++++++++++++++++++++++++++++++++
        | CODE = 1 | ID | LMESS | MESS | LSIG | SIG |
        +++++++++++++++++++++++++++++++++++++++++++++*/
        case 1:
            if(iMess->mess == NULL) {
                perror("Error: message is NULL");
                return NULL;
            }
            iMess->code = 1;
            iMess->id = malloc(sizeof(uint16_t));
            iMess->lmess = malloc(sizeof(uint8_t));
            iMess->mess = malloc(sizeof(char) * iMess->lmess);
            if(iMess->lsig != 0) {
                iMess->lsig = malloc(sizeof(uint8_t));
                iMess->sig = malloc(sizeof(char) * iMess->lsig);
            }
            break;

        case 2:
            /*++++++++++++++++++++++++++++++++++++++++++++++++++
            | CODE = 2 | ID | LMESS | MESS | LSIG | SIG | NB |
            ++++++++++++++++++++++++++++++++++++++++++++++++++*/
            if(iMess->mess == NULL) {
                perror("Error: message is NULL");
                return NULL;
            }
            iMess->code = 2;
            iMess->id = malloc(sizeof(uint16_t));
            iMess->lmess = malloc(sizeof(uint8_t));
            iMess->mess = malloc(sizeof(char) * iMess->lmess);
            if(iMess->lsig != 0) {
                iMess->lsig = malloc(sizeof(uint8_t));
                iMess->sig = malloc(sizeof(char) * iMess->lsig);
            }
            /*+++++++++++++++++++
            | ID | LSIG | SIG |
            +++++++++++++++++++*/
            if(iMess->nb <= 3) {
                for(int i = 0; i < iMess->nb; i++) {
                    iMess->id = malloc(sizeof(uint16_t));
                    iMess->lsig = malloc(sizeof(uint8_t));
                    iMess->sig = malloc(sizeof(char) * iMess->lsig);
                }
            }
            /*+++++++++++++++++++++++
            | CODE = 20 | ID | NB |
            +++++++++++++++++++++++*/
            else {
                iMess->code = 20;
                iMess->id = malloc(sizeof(uint16_t));
                iMess->nb = malloc(sizeof(uint16_t));
            }
            int offset2 = 0;
            break;
        default:
            break;
    }
    return iMess;
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
    //id
    oMess->id = (uint16_t)atoi(token);
    token = strtok ( NULL, SEPARATOR );
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
    mess->lmess = sizeof(char) * 13;
    mess->mess = "Hello world !";
    mess->lsig = 5;
    mess->sig = "32223";
    char* buff = struct_to_buffer(mess); 
    printf("Buffer: %s\n", buff);
    struct message* mess2 = malloc(sizeof(struct message));
    buffer_to_struct(buff, mess2);
    afficher_message(mess2);
    return 0;
}
