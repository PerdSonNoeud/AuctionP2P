#include "include/utils.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

int nbDigits (int n) {
  if (n < 10) return 1;
  if (n < 100) return 2;
  if (n < 1000) return 3;
  if (n < 10000) return 4;
  if (n < 100000) return 5;
  return 0;
}

int get_buffer_size(struct message *msg) {
  if (msg == NULL) {
    perror("Error: msg is NULL");
    return -1;
  }

  int size = 0;
  // Calculate the size of the resulting buffer
  size += nbDigits(msg->code); // For CODE (max 3 digits)
  size += nbDigits(msg->id) + sizeof(char); // For ID (max 10 digits) + separator
  size += nbDigits(msg->lmess) + sizeof(char); // For LMESS (max 5 digits) + separator
  size += msg->lmess + sizeof(char); // For MESS + separator
  size += nbDigits(msg->lsig) + sizeof(char); // For LSIG (max 5 digits) + separator

  if (msg->lsig > 0) {
    size += msg->lsig + sizeof(char); // For SIG + separator
  }
  // size += sizeof(uint8_t) + sizeof(char); // For NB (max 5 digits) + separator

  // Allocate memory for the buffer
  size += 1; // +1 for ending null character
  return size;
}

int message_to_buffer(struct message *msg, char *buffer, int buffer_size) {
  if (msg == NULL) {
    perror("Error: msg is NULL");
    return -1;
  }

  // Fill the buffer with the serialized data
  int offset = 0;
  // Offset for code
  offset += snprintf(buffer + offset, buffer_size - offset, "%d", msg->code);

  if (msg->code != CODE_DEMANDE_LIAISON && msg->code != CODE_ID_ACCEPTED) {
    // Offset for ID
    offset += snprintf(buffer + offset, buffer_size - offset, "|%d", msg->id);
  }

  if (msg->code == CODE_VALIDATION || msg->code == CODE_CONSENSUS) {
    // Offset for LMESS and MESS
    offset += snprintf(buffer + offset, buffer_size - offset, "|%d", msg->lmess);
    offset += snprintf(buffer + offset, buffer_size - offset, "|%s", msg->mess);
    // Offset for LSIG and SIG
    offset += snprintf(buffer + offset, buffer_size - offset, "|%d", msg->lsig);
    offset += snprintf(buffer + offset, buffer_size - offset, "|%s", msg->sig);
  }

  if (msg->code == CODE_REPONSE_LIAISON || msg->code == CODE_INFO_PAIR ||
      msg->code == CODE_INFO_PAIR_BROADCAST || msg->code == CODE_INFO_SYSTEME) {
    // Offset for PORT
    offset += snprintf(buffer + offset, buffer_size - offset, "|%d", msg->port);
  }

  // TODO : NUMV, PRIX, NB
  return 0;
}

int buffer_to_message(struct message *msg, char *buffer) {
  if(buffer == NULL) {
    perror("Error: buffer is NULL");
    return -1;
  }
  if (msg == NULL) {
    msg = malloc(sizeof(struct message));
    if (msg == NULL) {
      perror("Error: malloc failed");
      return -1;
    }
  }

  // Initialize pointers to NULL to avoid memory issues
  msg->mess = NULL;
  msg->sig = NULL;

  // Copy the buffer to avoid modifying the original
  char *buffer_copy = strdup(buffer);
  if (buffer_copy == NULL) {
    perror("Error: strdup failed");
    return -1;
  }

  char *token;
  char *saveptr;

  // Extract CODE
  token = strtok_r(buffer_copy, SEPARATOR, &saveptr);
  if (token == NULL) {
    perror("Error: invalid buffer format (missing CODE)");
    free(buffer_copy);
    return -1;
  }
  msg->code = atoi(token);

  if (msg->code != CODE_DEMANDE_LIAISON && msg->code != CODE_ID_ACCEPTED) {
    // Extract ID
    token = strtok_r(NULL, SEPARATOR, &saveptr);
    if (token == NULL) {
      perror("Error: invalid buffer format (missing ID)");
      free(buffer_copy);
      return -1;
    }
    msg->id = (uint16_t)atoi(token);
  }

  if (msg->code == CODE_VALIDATION || msg->code == CODE_CONSENSUS) {
    // Extract LMESS
    token = strtok_r(NULL, SEPARATOR, &saveptr);
    if (token == NULL) {
      perror("Error: invalid buffer format (missing LMESS)");
      free(buffer_copy);
      return -1;
    }
    msg->lmess = (uint8_t) atoi(token);

    // Extract MESS
    token = strtok_r(NULL, SEPARATOR, &saveptr);
    if (token == NULL) {
      perror("Error: invalid buffer format (missing MESS)");
      free(buffer_copy);
      return -1;
    }
    if (msg->lmess > 0) {
      msg->mess = malloc(msg->lmess + 1);
      if (msg->mess == NULL) {
        perror("Error: malloc failed for mess");
        free(buffer_copy);
        return -1;
      }
      strncpy(msg->mess, token, msg->lmess);
      msg->mess[msg->lmess] = '\0';
    }

    // Extract LSIG
    token = strtok_r(NULL, SEPARATOR, &saveptr);
    if (token == NULL) {
      perror("Error: invalid buffer format (missing LSIG)");
      free(buffer_copy);
      if (msg->mess) free(msg->mess);
      return -1;
    }
    msg->lsig = (uint8_t) atoi(token);

    // Extract SIG (if present)
    if (msg->lsig > 0) {
      token = strtok_r(NULL, SEPARATOR, &saveptr);
      if (token == NULL) {
        perror("Error: invalid buffer format (missing SIG)");
        free(buffer_copy);
        if (msg->mess) free(msg->mess);
        return -1;
      }
      msg->sig = malloc(msg->lsig + 1);
      if (msg->sig == NULL) {
        perror("Error: malloc failed for sig");
        free(buffer_copy);
        if (msg->mess) free(msg->mess);
        return -1;
      }
      strncpy(msg->sig, token, msg->lsig);
      msg->sig[msg->lsig] = '\0';
    }
  }

  if (msg->code == CODE_REPONSE_LIAISON || msg->code == CODE_INFO_PAIR ||
      msg->code == CODE_INFO_PAIR_BROADCAST || msg->code == CODE_INFO_SYSTEME) {
    // Extract PORT
    token = strtok_r(NULL, SEPARATOR, &saveptr);
    if (token == NULL) {
      perror("Error: invalid buffer format (missing PORT)");
      free(buffer_copy);
      if (msg->mess) free(msg->mess);
      if (msg->sig) free(msg->sig);
      return -1;
    }
    msg->port = (uint16_t) atoi(token);
  }
  // TODO : NUMV, PRIX, NB
  // Release memory
  free(buffer_copy);
  return 0;
}
