#ifndef UTILS_H
#define UTILS_H

#include "message.h"

/**
 * @brief Count the number of digits in an unsigned integer
 *
 * This function counts the number of digits in a given unsigned integer.
 *
 * @param n The unsigned integer to count digits for
 * @return The number of digits in the integer
 */
int nbDigits(int n);

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

/**
 * @brief Affiche le nombre de descripteurs de fichiers ouverts par le processus
 * 
 * Cette fonction affiche le nombre de descripteurs de fichiers (sockets, fichiers, etc.)
 * actuellement ouverts par le processus.
 */
void print_open_files();

#endif /* UTILS_H */
