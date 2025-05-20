#include "message.h"

/**
 * @brief Create a new struct message with the given code.
 *
 * @param code The code to set in the message, @see message.h for possible codes.
 **/
struct message* new_message(const int code);

/**
 * @brief Convert a struct message to a buffer.
 * This function takes a struct message and returns it as a buffer with the info in message.
 *
 * @param iMess The struct message to convert
 * @return The buffer containing the serialized data
 **/
char* message_to_buffer(struct message* iMess);

/**
 * @brief Convert a buffer to a struct message.
 * This function takes a buffer and fills the struct message with the data from the buffer.
 *
 * @param iBuffer The buffer to convert
 * @param oMess The struct message to fill
 **/
void message_to_struct(void* iBuffer, struct message* oMess);