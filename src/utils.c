#include "include/utils.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <openssl/pem.h>


EVP_PKEY *convert_private_key_to_evp_pkey(char *privkey) {
	EVP_PKEY *pkey = NULL;
	FILE *fp = fopen(privkey, "r");
	if(fp == NULL){
		perror("fopen");
		return NULL;
	}
	
	PEM_read_PrivateKey(fp, &pkey, NULL, NULL);    
	if (pkey == NULL) {
		fprintf(stderr, "erreur: PEM_read_PrivateKey\n");
		return NULL;
	}

	fclose(fp);
	return pkey; 
}


EVP_PKEY *convert_public_key_to_evp_pkey(char *pubkey) {
	EVP_PKEY *pkey = NULL; 
	FILE *fp = fopen(pubkey, "r");
	if(fp == NULL){
		perror("fopen");
		return NULL;
	}
	
	PEM_read_PUBKEY(fp, &pkey, NULL, NULL);   
	if (pkey == NULL) {
		fprintf(stderr, "erreur: PEM_read_PUBKEY\n");
		return NULL;
	}

	fclose(fp);
	return pkey; 
}

int generate_ed25519_key(char* public_key_name, char* private_key_name){
	EVP_PKEY *pkey = NULL;
	EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
	int ret = 0;
	FILE *bp_public = NULL;
	FILE* bp_private = NULL;

	// initialise le contexte pour la génération d'une paire de clés
	if(EVP_PKEY_keygen_init(pctx) <= 0){
		fprintf(stderr, "erreur EVP_PKEY_keygen_init\n");
		return ret;
	}

	// engendre une paire de clés ED25519
	EVP_PKEY_generate(pctx, &pkey);
	if(pkey == NULL) {
		fprintf(stderr, "erreur EVP_PKEY_keygen\n");
		goto err;
	}

	// sauvegarde la clé publique
	bp_public = fopen(public_key_name, "w+");
	ret = PEM_write_PUBKEY(bp_public, pkey);
	fclose(bp_public);
	if(ret != 1){
		fprintf(stderr, "erreur PEM_write_PUBKEY\n");
		goto err;
	}
	
	// sauvegarde la clé privée 
	bp_private = fopen(private_key_name, "w+");
	ret = PEM_write_PrivateKey(bp_private, pkey, NULL, NULL, 0, NULL, NULL);
	fclose(bp_private);
	if(ret != 1){
		fprintf(stderr, "PEM_write_PrivateKey error\n");
		goto err;
	}

	ret = 1;

 err:
	EVP_PKEY_free(pkey);
	EVP_PKEY_CTX_free(pctx);
	return ret;
}

int sign(EVP_PKEY *privkey, unsigned char *msg, unsigned char **sig, size_t *slen) {
	EVP_MD_CTX *mdctx = NULL;
	int ret = 0, msg_len = strlen((char *) msg);
	
	*sig = NULL;

	/* crée le Message Digest Context */
	if(!(mdctx = EVP_MD_CTX_create())){
		fprintf(stderr, "erreur EVP_MD_CTX_create\n");
		goto err;
	}

	/* initialise l'operation DigestSign (hachage avec signature) 
	 * avec la clé privée.
	 * 3eme param (fonction de hachage) doit être NULL pour une clé ED25519
	 * puisque celle-ci utilise SHA-512 (pas de choix) 
	 * si RSA, fonction de hachage peut être EVP_sha256() */
	if(1 != EVP_DigestSignInit(mdctx, NULL, NULL, NULL, privkey)){
		fprintf(stderr, "erreur EVP_DigestSignInit\n");
		goto err;
	} 
	
	/* appel une 1ère fois de EVP_DigestSign avec le paramètre 
	 * sig à NULL afin d'obtenir la longueur de la signature */
	if(1 != EVP_DigestSign(mdctx, NULL, slen, msg, msg_len)) {
		fprintf(stderr, "erreur EVP_DigestSign\n");
		goto err;
	} 

	/* alloue la mémoire pour la signature */ 
	if(!(*sig = malloc(sizeof(unsigned char) * (*slen)))) {
		fprintf(stderr, "erreur malloc\n");
		goto err;
	} 

	/* obtention de la signature */
	if(1 != EVP_DigestSign(mdctx, *sig, slen, msg, msg_len)) {
		fprintf(stderr, "erreur EVP_DigestSign\n");
		goto err;
	} 

	/* Succès */
	ret = 1;
	printf("message : %s\n", msg);
	EVP_MD_CTX_free(mdctx);
	return 1;
 
 err:
	if(ret != 1)
		fprintf(stderr, "erreur dans sign()\n");
		 
	/* nettoyage mémoire */
	if(*sig && !ret) OPENSSL_free(*sig); // = free(*sig);
	if(mdctx) EVP_MD_CTX_free(mdctx); //EVP_MD_CTX_destroy(mdctx);

	return 0;
}

int verify(EVP_PKEY *pubkey, unsigned char *msg, unsigned char *sig, size_t slen) {
	EVP_MD_CTX *mdctx;
	int ret = -1;
	
	if(!(mdctx = EVP_MD_CTX_create()))  return 0; //erreur

	if(1 != EVP_DigestVerifyInit(mdctx, NULL, NULL, NULL, pubkey)) {
		perror("erreur EVP_DigestVerifyInit");
	}

	if(1 == EVP_DigestVerify(mdctx, sig, slen, msg, strlen((char *) msg)))
		printf("signature ok\n");
	else {
		printf("document corrompu\n");
		return 0;
	}	
	return ret;
}

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
  offset += snprintf(buffer + offset, buffer_size - offset, "%d", msg->code);
  offset += snprintf(buffer + offset, buffer_size - offset, "|%u", msg->id);
  offset += snprintf(buffer + offset, buffer_size - offset, "|%d", msg->lmess);
  offset += snprintf(buffer + offset, buffer_size - offset, "|%s", msg->mess);
  offset += snprintf(buffer + offset, buffer_size - offset, "|%d", msg->lsig);

  if (msg->lsig > 0) {
    offset += snprintf(buffer + offset, buffer_size - offset, "|%s", msg->sig);
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

  // Extract ID
  token = strtok_r(buffer_copy, SEPARATOR, &saveptr);
  if (token == NULL) {
    perror("Error: invalid buffer format (missing ID)");
    free(buffer_copy);
    return -1;
  }
  msg->id = (uint16_t)atoi(token);

  // Extract LMESS
  token = strtok_r(buffer_copy, SEPARATOR, &saveptr);
  if (token == NULL) {
    perror("Error: invalid buffer format (missing LMESS)");
    free(buffer_copy);
    return -1;
  }
  msg->lmess = (uint8_t)atoi(token);

  // Extract MESS
  token = strtok_r(buffer_copy, SEPARATOR, &saveptr);
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
  token = strtok_r(buffer_copy, SEPARATOR, &saveptr);
  if (token == NULL) {
    perror("Error: invalid buffer format (missing LSIG)");
    free(buffer_copy);
    if (msg->mess) free(msg->mess);
    return -1;
  }
  msg->lsig = (uint8_t)atoi(token);

  // Extract SIG (if present)
  if (msg->lsig > 0) {
    token = strtok_r(buffer_copy, SEPARATOR, &saveptr);
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

  // Release memory
  free(buffer_copy);
  return 0;
}
