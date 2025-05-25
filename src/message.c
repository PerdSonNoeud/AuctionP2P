#include "include/message.h"
#include "include/utils.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/crypto.h>
#include <openssl/rsa.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

void print_message(struct message* msg) {
    if (msg == NULL) {
        printf("Le Message est NULL\n");
        return;
    }
    printf("Message:\n");
    printf("  Code: %d\n", msg->code);
    printf("  ID: %d\n", msg->id);
    printf("  Lmess: %d\n", msg->lmess);
    printf("  Mess: %s\n", msg->mess);
    printf("  Lsig: %d\n", msg->lsig);
    printf("  Sig: %s\n", msg->sig);
}

struct message* init_message(const int code) {
  struct message* msg = malloc(sizeof(struct message));
  if(msg == NULL) {
    perror("Allocation ratée");
    return NULL;
  }

  // Initialize the structure with default values
  msg->code = code;
  msg->id = 0; // uint16_t is primitive type, not a pointer
  msg->lmess = 0;
  msg->mess = NULL; // Initialize at NULL before allocation
  msg->sig = NULL;
  msg->lsig = 0;

  return msg;
}

int message_set_mess(struct message* msg, const char* mess) {
  if(msg == NULL || mess == NULL) {
    perror("Message ou contenu est NULL");
    return -1;
  }

  // Free the previous message if it exists
  if(msg->mess != NULL) {
    free(msg->mess);
  }

  // Allocate memory for the new message
  msg->lmess = strlen(mess);
  msg->mess = malloc(msg->lmess + 1); // +1 for null terminator
  if(msg->mess == NULL) {
    perror("Allocation ratée pour mess");
    return -1;
  }
  strcpy(msg->mess, mess);
  return 0;
}

int message_set_sig(struct message* msg) {
  if(msg == NULL) {
    perror("Message est NULL");
    return -1;
  }

  // Free the previous signature if it exists
  if(msg->sig != NULL) {
    free(msg->sig);
  }
  
  // retrieve public and private keys (assumed to be in PEM format)
  EVP_PKEY *pubkey = convert_public_key_to_evp_pkey("pub-ed25519-key.pem");
  EVP_PKEY *privkey = convert_private_key_to_evp_pkey("priv-ed25519-key.pem") ;

  /* signature */
  if (sign(privkey, (unsigned char*)msg->mess, (unsigned char**)&msg->sig, (size_t*)&msg->lsig) != 1) {
    perror("Allocation ratée pour sig");
    EVP_PKEY_free(privkey);
    EVP_PKEY_free(pubkey);
    return -1;
  }

  if (verify(pubkey, (unsigned char*)msg->mess, (unsigned char*)msg->sig, (size_t)msg->lsig) != 1) {
    perror("Allocation ratée pour sig");
    EVP_PKEY_free(privkey);
    EVP_PKEY_free(pubkey);
    return -1;
  }

  EVP_PKEY_free(privkey);
  EVP_PKEY_free(pubkey);
  return 0;
}

int message_set_ip(struct message* msg, struct in6_addr ip) {
  if(msg == NULL) {
    perror("Message est NULL");
    return -1;
  }
  // Copy the IP address into the message structure
  msg->ip = ip;
  return 0;
}

int message_set_port(struct message* msg, uint16_t port) {
  if(msg == NULL) {
    perror("Message est NULL");
    return -1;
  }
  msg->port = port;
  return 0;
}

int message_set_cle(struct message* msg, const char* cle) {
  if(msg == NULL || cle == NULL) {
    perror("Message ou clé est NULL");
    return -1;
  }
  strncpy(msg->cle, cle, sizeof(msg->cle) - 1);
  msg->cle[sizeof(msg->cle) - 1] = '\0'; // Ensure null termination
  return 0;
}

int message_set_nb(struct message *msg, int nb) {
  if(msg == NULL) {
    perror("Message est NULL");
    return -1;
  }
  msg->nb = nb;
  msg->info = malloc(nb * sizeof(struct info));
  return 0;
}

int init_info(struct info *info, int id, struct in6_addr ip, uint16_t port) {
  if(info == NULL) {
    perror("Info est NULL");
    return -1;
  }
  info->id = 0; // Initialize id
  if (id > 0) {
    info->id = id; // Set id if valid
  }
  info->ip = in6addr_any; // Initialize IP to any address
  if (memcmp(&ip, &in6addr_any, sizeof(struct in6_addr)) != 0) {
    info->ip = ip; // Set IP if valid
  }
  info->port = 0; // Initialize port to 0
  if (port > 0) {
    info->port = port; // Set port if valid
  }
  // TODO : Handle cle
  return 0;
}

int message_set_info(struct message *msg, int index, struct info *info) {
  if(msg == NULL || info == NULL || index < 0) {
    perror("Message, info ou index invalide");
    return -1;
  }
  msg->info[index] = *info; // Copy the info structure
  return 0;
}

int free_message(struct message* msg) {
  if(msg == NULL) {
    perror("Message est NULL");
    return -1;
  }

  // Free the allocated memory for mess and sig
  if(msg->mess != NULL) {
    free(msg->mess);
  }
  if(msg->sig != NULL) {
    free(msg->sig);
  }

  // Free the message structure itself
  free(msg);
  return 0;
}
