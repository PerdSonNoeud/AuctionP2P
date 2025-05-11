#include <netinet/in.h>

int setup_multicast_receiver( const char *addr, int port);

int setup_multicast_sender();

int send_multicast(int sock,const char *addr, int port,  const void *data, size_t len);

int receive_multicast(int sock, void *buffer, size_t buffer_size, struct sockaddr_in6 *sender_addr);