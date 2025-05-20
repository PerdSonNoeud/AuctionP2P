#ifndef UTILS_H
#define UTILS_H

#include "message.h"

/**
 * @brief Convert a message structure to a serialized buffer
 *
 * This function takes a message structure and returns it as a serialized buffer
 * that can be transmitted over the network.
 *
 * @param iMess The message structure to serialize
 * @return A buffer containing the serialized data (must be freed by caller)
 */
char* message_to_buffer(struct message* iMess);

/**
 * @brief Convert a serialized buffer to a message structure
 *
 * This function takes a buffer received from the network and fills the 
 * message structure with the deserialized data.
 *
 * @param iBuffer The buffer to deserialize
 * @param oMess The message structure to populate
 */
void message_to_struct(void* iBuffer, struct message* oMess);

#endif /* UTILS_H */
