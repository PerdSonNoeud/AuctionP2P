#include "include/utils.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h> // Pour getpid()

int nbDigits (int n) {
  if (n < 0) n = -n; // Traiter les nombres négatifs
  
  if (n < 10) return 1;
  if (n < 100) return 2;
  if (n < 1000) return 3;
  if (n < 10000) return 4;
  if (n < 100000) return 5;
  if (n < 1000000) return 6;
  if (n < 10000000) return 7;
  if (n < 100000000) return 8;
  if (n < 1000000000) return 9;
  return 10; // Pour int 32-bit max
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
  
  // Add space for auction-specific fields if needed
  if (msg->code == CODE_NOUVELLE_VENTE || msg->code == CODE_ENCHERE || 
      msg->code == CODE_ENCHERE_SUPERVISEUR || msg->code == CODE_FIN_VENTE_WARNING || 
      msg->code == CODE_FIN_VENTE || msg->code == CODE_REFUS_PRIX) {
    size += nbDigits(msg->numv) + sizeof(char); // For NUMV + separator
    size += nbDigits(msg->prix) + sizeof(char); // For PRIX + separator
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
  
  if (buffer == NULL) {
    perror("Error: buffer is NULL");
    return -1;
  }

  // Initialiser le buffer
  memset(buffer, 0, buffer_size);

  // S'assurer que mess et sig existent, même vides
  char empty_str[] = "";
  char *mess = msg->mess ? msg->mess : empty_str;
  char *sig = msg->sig ? msg->sig : empty_str;
  
  // Mettre à jour les longueurs si non définies
  if (msg->lmess == 0 && mess != empty_str) {
    msg->lmess = strlen(mess);
  }
  
  if (msg->lsig == 0 && sig != empty_str) {
    msg->lsig = strlen(sig);
  }

  // Fill the buffer with the serialized data
  int offset = 0;
  offset += snprintf(buffer + offset, buffer_size - offset, "%d", msg->code);
  offset += snprintf(buffer + offset, buffer_size - offset, "|%u", msg->id);
  offset += snprintf(buffer + offset, buffer_size - offset, "|%d", msg->lmess);
  offset += snprintf(buffer + offset, buffer_size - offset, "|%s", mess);
  offset += snprintf(buffer + offset, buffer_size - offset, "|%d", msg->lsig);
  offset += snprintf(buffer + offset, buffer_size - offset, "|%s", sig);
  
  // Add auction-specific fields if needed
  if (msg->code == CODE_NOUVELLE_VENTE || msg->code == CODE_ENCHERE || 
      msg->code == CODE_ENCHERE_SUPERVISEUR || msg->code == CODE_FIN_VENTE_WARNING || 
      msg->code == CODE_FIN_VENTE || msg->code == CODE_REFUS_PRIX) {
    offset += snprintf(buffer + offset, buffer_size - offset, "|%u", msg->numv);
    offset += snprintf(buffer + offset, buffer_size - offset, "|%u", msg->prix);
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
    fprintf(stderr, "Error: invalid buffer format (missing CODE): '%s'\n", buffer);
    free(buffer_copy);
    return -1;
  }
  msg->code = atoi(token);
  // printf("Parsed CODE: %d\n", msg->code);

  // Extract ID
  token = strtok_r(NULL, SEPARATOR, &saveptr);
  if (token == NULL) {
    fprintf(stderr, "Error: invalid buffer format (missing ID): '%s'\n", buffer);
    free(buffer_copy);
    return -1;
  }
  msg->id = (uint16_t)atoi(token);
  // printf("Parsed ID: %d\n", msg->id);

  // Extract LMESS
  token = strtok_r(NULL, SEPARATOR, &saveptr);
  if (token == NULL) {
    fprintf(stderr, "Error: invalid buffer format (missing LMESS): '%s'\n", buffer);
    free(buffer_copy);
    return -1;
  }
  msg->lmess = (uint8_t)atoi(token);
  // printf("Parsed LMESS: %d\n", msg->lmess);

  // Extract MESS
  token = strtok_r(NULL, SEPARATOR, &saveptr);
  if (token == NULL) {
    fprintf(stderr, "Error: invalid buffer format (missing MESS): '%s'\n", buffer);
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
    // printf("Parsed MESS: '%s'\n", msg->mess);
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
    // printf("Parsed MESS (with calculated length): '%s'\n", msg->mess);
  }

  // Extract LSIG
  token = strtok_r(NULL, SEPARATOR, &saveptr);
  if (token == NULL) {
    fprintf(stderr, "Error: invalid buffer format (missing LSIG): '%s'\n", buffer);
    free(buffer_copy);
    if (msg->mess) free(msg->mess);
    return -1;
  }
  msg->lsig = (uint8_t)atoi(token);
  // printf("Parsed LSIG: %d\n", msg->lsig);

  // Extract SIG
  token = strtok_r(NULL, SEPARATOR, &saveptr);
  if (token == NULL && msg->lsig > 0) {
    fprintf(stderr, "Error: invalid buffer format (missing SIG): '%s'\n", buffer);
    free(buffer_copy);
    if (msg->mess) free(msg->mess);
    return -1;
  }
  
  if (token != NULL) {
    if (msg->lsig > 0) {
      msg->sig = malloc(msg->lsig + 1);
      if (msg->sig == NULL) {
        perror("Error: malloc failed for sig");
        free(buffer_copy);
        if (msg->mess) free(msg->mess);
        return -1;
      }
      strncpy(msg->sig, token, msg->lsig);
      msg->sig[msg->lsig] = '\0';
      // printf("Parsed SIG: '%s'\n", msg->sig);
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
      // printf("Parsed SIG (with calculated length): '%s'\n", msg->sig);
    }
  }
  
  // Extract auction-specific fields if needed
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
      // printf("Parsed NUMV: %u\n", msg->numv);
      
      // Extract PRIX
      token = strtok_r(NULL, SEPARATOR, &saveptr);
      if (token == NULL) {
        // Pas d'erreur critique si PRIX est manquant
        printf("Warning: missing PRIX field\n");
      } else {
        msg->prix = (uint32_t)atoi(token);
        // printf("Parsed PRIX: %u\n", msg->prix);
      }
    }
  }

  // Release memory
  free(buffer_copy);
  return 0;
}

// Fonction pour afficher le nombre de descripteurs de fichiers ouverts par le processus
void print_open_files() {
  pid_t pid = getpid();
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/fd", pid);
  
  DIR *dir = opendir(path);
  if (dir == NULL) {
    perror("Impossible d'ouvrir le répertoire /proc/PID/fd");
    return;
  }
  
  int count = 0;
  struct dirent *entry;
  
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] != '.') {
      count++;
    }
  }
  
  closedir(dir);
  printf("[INFO] Descripteurs de fichiers ouverts: %d\n", count);
}
