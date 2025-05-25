#ifndef UTILS_H
#define UTILS_H

#include "message.h"
#include <openssl/evp.h>

/**
 * @brief Retrieve private key from a PEM file
 *
 * @param privKey File containing the private key in PEM format
 * @return The EVP_PKEY structure containing the private key, or NULL on error
 */
EVP_PKEY *convert_private_key_to_evp_pkey(char *privkey);

/**
 * @brief Retrieve public key from a PEM file
 *
 * @param pubKey File containing the public key in PEM format
 * @return The EVP_PKEY structure containing the public key, or NULL on error
 */
EVP_PKEY *convert_public_key_to_evp_pkey(char *pubkey);

/**
 * @brief Generate a new ED25519 key pair and save it to files
 *
 * This function generates a new ED25519 key pair and saves the public key
 * to `public_key_name` and the private key to `private_key_name`.
 *
 * @param public_key_name Name of the file to save the public key
 * @param private_key_name Name of the file to save the private key
 * @return 1 on success, 0 on error
 */
int generate_ed25519_key(char* public_key_name, char* private_key_name);

/**
 * @brief Sign a message using the provided private key
 *
 * This function signs a message with the given private key and returns
 * the signature.
 *
 * @param privkey The private key to use for signing
 * @param msg The message to sign
 * @param sig Pointer to store the signature
 * @param slen Pointer to store the length of the signature
 * @return 1 on success, 0 on error
 */
int sign(EVP_PKEY *privkey, unsigned char *msg, unsigned char **sig, size_t *slen);

/**
 * @brief Verify a signature using the provided public key
 *
 * This function verifies a signature against a message using the given public key.
 *
 * @param pubkey The public key to use for verification
 * @param msg The message that was signed
 * @param sig The signature to verify
 * @param slen The length of the signature
 * @return 1 if the signature is valid, 0 if invalid, -1 on error
 */
int verify(EVP_PKEY *pubkey, unsigned char *msg, unsigned char *sig, size_t slen);

/**
 * @brief Count the size of the buffer we need to allocate
 *
 * This function calculates the size of the buffer needed to store
 * a serialized message.
 *
 * @param msg The message structure to serialize
 * @return The size of the buffer needed, or -1 on error
 */
int get_buffer_size(struct message *msg);

/**
 * @brief Convert a message structure to a serialized buffer
 *
 * This function takes a message structure and returns it as a serialized buffer
 * that can be transmitted over the network.
 *
 * @param msg The message structure to serialize
 * @param buffer The buffer to fill with the serialized data
 * @param buffer_size The size of the buffer
 * @return 0 on success, -1 on error
 */
int message_to_buffer(struct message *msg, char *buffer, int buffer_size);

/**
 * @brief Convert a serialized buffer to a message structure
 *
 * This function takes a buffer received from the network and fills the 
 * message structure with the deserialized data.
 *
 * @param msg The message structure to populate
 * @param buffer The buffer to deserialize
 * @return 0 on success, -1 on error
 */
int buffer_to_message(struct message* msg, char *buffer);

#endif /* UTILS_H */
