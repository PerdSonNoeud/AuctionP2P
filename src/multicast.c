#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "include/multicast.h"

int setup_multicast_receiver(const char *addr, int port) {
    // TODO créer les sockets pour recevoir 
}

int setup_multicast_sender() {
    // TODO créer les sockets pour envoyer
}

int send_multicast(int sock, const char *addr, int port, const void *data, size_t len) {
    // TODO envoyer les données
}