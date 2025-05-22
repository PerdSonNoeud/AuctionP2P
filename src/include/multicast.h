#ifndef MULTICAST_H
#define MULTICAST_H

#include <netinet/in.h>
#include <stdlib.h>

/**
 * @brief Set up a multicast receiver socket
 *
 * Creates and configures a socket to receive multicast packets
 * from the specified multicast group and port.
 *
 * @param addr Multicast group address
 * @param port Multicast port number
 * @return Socket file descriptor on success, negative value on error
 */
int setup_multicast_receiver(const char *addr, int port);

/**
 * @brief Set up a multicast sender socket
 *
 * Creates and configures a socket to send multicast packets.
 *
 * @return Socket file descriptor on success, negative value on error
 */
int setup_multicast_sender();

/**
 * @brief Send data to a multicast group
 *
 * Send the specified data to the given multicast group and port.
 *
 * @param sock Socket to use for sending
 * @param addr Multicast group address
 * @param port Multicast port number
 * @param data Pointer to the data to send
 * @param len Length of the data to send
 * @return Number of bytes sent on success, negative value on error
 */
int send_multicast(int sock, const char *addr, int port, const void *data, size_t len);

/**
 * @brief Send data directly to a specific address (unicast)
 *
 * Send the specified data to the given sockaddr_in6 address.
 *
 * @param sock Socket to use for sending
 * @param dest_addr Destination address (sockaddr_in6 structure)
 * @param data Pointer to the data to send
 * @param len Length of the data to send
 * @return Number of bytes sent on success, negative value on error
 */
int send_unicast(int sock, struct sockaddr_in6 *dest_addr, const void *data, size_t len);

/**
 * @brief Receive data from a multicast group
 *
 * Receive data from the configured multicast group through the given socket.
 *
 * @param sock Socket to use for receiving
 * @param buffer Buffer to store received data
 * @param buffer_size Size of the buffer
 * @param sender_addr Structure to store sender's address information
 * @return Number of bytes received on success, negative value on error
 */
int receive_multicast(int sock, char *buffer, size_t buffer_size, struct sockaddr_in6 *sender_addr);

#endif /* MULTICAST_H */
