#include <netinet/in.h>

int setup_multicast_receiver( char *addr, int port);

int setup_multicast_sender();

int send_multicast(int sock, char *addr, int port, void *data, size_t len);