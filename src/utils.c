#include "include/utils.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <unistd.h> // Pour getpid()

int nbDigits (int n) {
  if (n == 0) return 1;
  int count = 0;
  while (n != 0) {
    n /= 10;
    count++;
  }
  return count;
}

int get_buffer_size(struct message *msg) {
  if (msg == NULL) {
    perror("Error: msg is NULL");
    return -1;
  }

  int size = 0;
  // Calculate the size of the resulting buffer
  size += nbDigits(msg->code); // For CODE (max 3 digits)
  if (msg->code != CODE_DEMANDE_LIAISON && msg->code != CODE_ID_ACCEPTED &&
       msg->code != CODE_INFO_PAIR_BROADCAST && msg->code != CODE_INFO_PAIR) {
    size += nbDigits(msg->id) + sizeof(char); // For ID (max 10 digits) + separator
  }

  if (msg->code == CODE_VALIDATION || msg->code == CODE_CONSENSUS) {
    size += nbDigits(msg->lmess) + sizeof(char); // For LMESS (max 5 digits) + separator
    size += msg->lmess + sizeof(char); // For MESS + separator
    size += nbDigits(msg->lsig) + sizeof(char); // For LSIG (max 5 digits) + separator
    if (msg->lsig > 0) {
      size += msg->lsig + sizeof(char); // For SIG + separator
    }
  }

  if (msg->code == CODE_REPONSE_LIAISON || msg->code == CODE_INFO_SYSTEME) {
    // For IP address (16 bytes for IPv6) + separator
    size += INET6_ADDRSTRLEN + sizeof(char);
    size += nbDigits(msg->port) + sizeof(char); // For PORT (max 5 digits) + separator
  }

  if (msg->code == CODE_INFO_PAIR || msg->code == CODE_INFO_PAIR_BROADCAST || msg->code == CODE_INFO_SYSTEME) {
    int max = 1;
    if (msg->code == CODE_INFO_SYSTEME) {
      max = msg->nb;
      size += nbDigits(max) + sizeof(char); // For NB (max 10 digits) + separator
    }
    for (int i = 0; i < max; i++) {
      size += nbDigits(msg->info[i].id) + sizeof(char); // For ID in info
      size += INET6_ADDRSTRLEN + sizeof(char); // For IP in info
      size += nbDigits(msg->info[i].port) + sizeof(char); // For PORT in info
      size += strlen(msg->info[i].cle) + sizeof(char); // For cle in info
    }
  }

  if (msg->code == CODE_NOUVELLE_VENTE || msg->code == CODE_ENCHERE ||
      msg->code == CODE_ENCHERE_SUPERVISEUR || msg->code == CODE_FIN_VENTE_WARNING ||
      msg->code == CODE_FIN_VENTE || msg->code == CODE_REFUS_PRIX) {
    size += nbDigits(msg->numv) + sizeof(char); // For NUMV + separator
    size += nbDigits(msg->prix) + sizeof(char); // For PRIX + separator
  } else if (msg->code == CODE_ANNUL_SUPERVISEUR || msg->code == CODE_ANNUL_DEMANDE) {
    size += nbDigits(msg->numv) + sizeof(char); // For NUMV + separator
  }

  size += 1; // +1 for ending null character
  return size;
}

int message_to_buffer(struct message *msg, char *buffer, int buffer_size) {
  if (msg == NULL) {
    perror("Error: msg is NULL");
    return -1;
  }

  if (buffer == NULL) {
    perror("Error: buffer is NULL");
    return -1;
  }

  // Initialiser le buffer
  memset(buffer, 0, buffer_size);

  // Fill the buffer with the serialized data
  int offset = 0;
  // Offset for code
  offset += snprintf(buffer + offset, buffer_size - offset, "%d", msg->code);

  if (msg->code != CODE_DEMANDE_LIAISON && msg->code != CODE_ID_ACCEPTED &&
      msg->code != CODE_INFO_PAIR_BROADCAST && msg->code != CODE_INFO_PAIR) {
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

  if (msg->code == CODE_REPONSE_LIAISON || msg->code == CODE_INFO_SYSTEME) {
    // Offset for IP address
    char ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &msg->ip, ip_str, sizeof(ip_str));
    offset += snprintf(buffer + offset, buffer_size - offset, "|%s", ip_str);
    // Offset for PORT
    offset += snprintf(buffer + offset, buffer_size - offset, "|%d", msg->port);
  }

  if (msg->code == CODE_INFO_PAIR || msg->code == CODE_INFO_PAIR_BROADCAST || msg->code == CODE_INFO_SYSTEME) {
    int max = 1;
    if (msg->code == CODE_INFO_SYSTEME) {
      max = msg->nb;
      offset += snprintf(buffer + offset, buffer_size - offset, "|%d", msg->nb);
    }
    for (int i = 0; i < max; i++) {
      offset += snprintf(buffer + offset, buffer_size - offset, "|%d", msg->info[i].id);
      char info_ip_str[INET6_ADDRSTRLEN];
      inet_ntop(AF_INET6, &msg->info[i].ip, info_ip_str, sizeof(info_ip_str));
      offset += snprintf(buffer + offset, buffer_size - offset, "|%s", info_ip_str);
      offset += snprintf(buffer + offset, buffer_size - offset, "|%d", msg->info[i].port);
      // TODO : Handle cle
      // offset += snprintf(buffer + offset, buffer_size - offset, "|%s", msg->info[i].cle);
    }
  }

  // Add auction-specific fields if needed
  if (msg->code == CODE_NOUVELLE_VENTE || msg->code == CODE_ENCHERE ||
      msg->code == CODE_ENCHERE_SUPERVISEUR || msg->code == CODE_FIN_VENTE_WARNING ||
      msg->code == CODE_FIN_VENTE || msg->code == CODE_REFUS_PRIX) {
    offset += snprintf(buffer + offset, buffer_size - offset, "|%u", msg->numv);
    offset += snprintf(buffer + offset, buffer_size - offset, "|%u", msg->prix);
  } else if (msg->code == CODE_ANNUL_SUPERVISEUR || msg->code == CODE_ANNUL_DEMANDE) {
    offset += snprintf(buffer + offset, buffer_size - offset, "|%u", msg->numv);
  }
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

  // Initialize fields to defaults
  msg->code = 0;
  msg->id = 0;
  msg->lmess = 0;
  msg->mess = NULL;
  msg->lsig = 0;
  msg->sig = NULL;
  memset(&msg->ip, 0, sizeof(struct in6_addr));
  msg->port = 0;
  memset(msg->cle, 0, sizeof(msg->cle));
  msg->numv = 0;
  msg->prix = 0;
  msg->nb = 0;

  // Debug mode - désactivé pour réduire la verbosité
  // printf("Analyse du buffer: '%s'\n", buffer);

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

  if (msg->code != CODE_DEMANDE_LIAISON && msg->code != CODE_ID_ACCEPTED &&
      msg->code != CODE_INFO_PAIR_BROADCAST && msg->code != CODE_INFO_PAIR) {
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
    } else if (strlen(token) > 0) {
      // Si LMESS est 0 mais qu'il y a un message, on l'affecte quand même
      msg->lmess = strlen(token);
      msg->mess = malloc(msg->lmess + 1);
      if (msg->mess == NULL) {
        perror("Error: malloc failed for mess");
        free(buffer_copy);
        return -1;
      }
      strcpy(msg->mess, token);
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
    } else if (strlen(token) > 0) {
      // Si LSIG est 0 mais qu'il y a une signature, on l'affecte quand même
      msg->lsig = strlen(token);
      msg->sig = malloc(msg->lsig + 1);
      if (msg->sig == NULL) {
        perror("Error: malloc failed for sig");
        free(buffer_copy);
        if (msg->mess) free(msg->mess);
        return -1;
      }
      strcpy(msg->sig, token);
    }
  }

  if (msg->code == CODE_REPONSE_LIAISON || msg->code == CODE_INFO_SYSTEME) {
    // Extract IP address
    token = strtok_r(NULL, SEPARATOR, &saveptr);
    if (token == NULL) {
      perror("Error: invalid buffer format (missing IP)");
      free(buffer_copy);
      if (msg->mess) free(msg->mess);
      if (msg->sig) free(msg->sig);
      return -1;
    }
    if (inet_pton(AF_INET6, token, &msg->ip) <= 0) {
      perror("Error: invalid IP address format");
      free(buffer_copy);
      if (msg->mess) free(msg->mess);
      if (msg->sig) free(msg->sig);
      return -1;
    }
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

  if (msg->code == CODE_INFO_PAIR || msg->code == CODE_INFO_PAIR_BROADCAST || msg->code == CODE_INFO_SYSTEME) {
    int max = 1; // Default to 1 info
    if (msg->code == CODE_INFO_SYSTEME) {
      // More than one info is expected
      token = strtok_r(NULL, SEPARATOR, &saveptr);
      if (token == NULL) {
        perror("Error: invalid buffer format (missing NB)");
        free(buffer_copy);
        if (msg->mess) free(msg->mess);
        if (msg->sig) free(msg->sig);
        return -1;
      }
      msg->nb = atoi(token);
      max = msg->nb;
    }
    msg->info = malloc(sizeof (struct info) * max); // Allocate for at least one info
    for (int i = 0; i < max; i++) {
      // Extract info ID
      token = strtok_r(NULL, SEPARATOR, &saveptr);
      if (token == NULL) {
        perror("Error: invalid buffer format (missing info ID)");
        free(buffer_copy);
        if (msg->mess) free(msg->mess);
        if (msg->sig) free(msg->sig);
        return -1;
      }
      msg->info[i].id = (uint16_t) atoi(token);
      // Extract info IP
      token = strtok_r(NULL, SEPARATOR, &saveptr);
      if (token == NULL) {
        perror("Error: invalid buffer format (missing info IP)");
        free(buffer_copy);
        if (msg->mess) free(msg->mess);
        if (msg->sig) free(msg->sig);
        return -1;
      }
      if (inet_pton(AF_INET6, token, &msg->info[i].ip) <= 0) {
        perror("Error: invalid info IP address format");
        free(buffer_copy);
        if (msg->mess) free(msg->mess);
        if (msg->sig) free(msg->sig);
        return -1;
      }
      // Extract info PORT
      token = strtok_r(NULL, SEPARATOR, &saveptr);
      if (token == NULL) {
        perror("Error: invalid buffer format (missing info PORT)");
        free(buffer_copy);
        if (msg->mess) free(msg->mess);
        if (msg->sig) free(msg->sig);
        return -1;
      }
      msg->info[i].port = (uint16_t) atoi(token);
      // Extract info cle
      // TODO : Handle cle
    }
  }

  if (msg->code == CODE_NOUVELLE_VENTE || msg->code == CODE_ENCHERE ||
      msg->code == CODE_ENCHERE_SUPERVISEUR || msg->code == CODE_FIN_VENTE_WARNING ||
      msg->code == CODE_FIN_VENTE || msg->code == CODE_REFUS_PRIX) {
    // Extract NUMV
    token = strtok_r(NULL, SEPARATOR, &saveptr);
    if (token == NULL) {
      // Pas d'erreur critique si NUMV est manquant
      printf("Warning: missing NUMV field\n");
    } else {
      msg->numv = (uint32_t)atoi(token);
      // Extract PRIX
      token = strtok_r(NULL, SEPARATOR, &saveptr);
      if (token == NULL) {
        // Pas d'erreur critique si PRIX est manquant
        printf("Warning: missing PRIX field\n");
      } else {
        msg->prix = (uint32_t)atoi(token);
      }
    }
  } else if (msg->code == CODE_ANNUL_SUPERVISEUR || msg->code == CODE_ANNUL_DEMANDE) {
    token = strtok_r(NULL, SEPARATOR, &saveptr);
    if (token == NULL) {
      // Pas d'erreur critique si NUMV est manquant
      printf("Warning: missing NUMV field\n");
    } else {
      msg->numv = (uint32_t)atoi(token);
    }
  }
  // Release memory
  free(buffer_copy);
  return 0;
}
