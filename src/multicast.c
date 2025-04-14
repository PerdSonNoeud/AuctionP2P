#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "include/multicast.h"

int setup_multicast_receiver(const char *addr, int port) {
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    // On fait l'adresse 
    struct sockaddr_in6 addr_struct;
    memset(&addr_struct, 0, sizeof(addr_struct));
    addr_struct.sin6_family = AF_INET6;
    addr_struct.sin6_port = htons(port);
    addr_struct.sin6_addr = in6addr_any;
    
    // Lier le socket
    if (bind(sock, (struct sockaddr*)&addr_struct, sizeof(addr_struct)) < 0) {
        perror("bind failed");
        close(sock);
        return -1;
    }
    
    // Rejoindre le groupe multicast
    struct ipv6_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    if (inet_pton(AF_INET6, addr, &mreq.ipv6mr_multiaddr) <= 0) {
        perror("inet_pton failed");
        close(sock);
        return -1;
    }
    mreq.ipv6mr_interface = 0; 
    
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt(IPV6_JOIN_GROUP) failed");
        close(sock);
        return -1;
    }
    
    return sock;
}

int setup_multicast_sender() {
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    int ttl = 1; 
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &ttl, sizeof(ttl)) < 0) {
        perror("setsockopt(IPV6_MULTICAST_HOPS) failed");
        close(sock);
        return -1;
    }
    
    return sock;
}

int send_multicast(int sock, const char *addr, int port, const void *data, size_t len) {
    
    struct sockaddr_in6 dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin6_family = AF_INET6;
    dest.sin6_port = htons(port);
    
    if (inet_pton(AF_INET6, addr, &dest.sin6_addr) <= 0) {
        perror("inet_pton failed");
        return -1;
    }
    
    if (sendto(sock, data, len, 0, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
        perror("sendto failed");
        return -1;
    }
    
    return 0;
}

int receive_multicast(int sock, void *buffer, size_t buffer_size, struct sockaddr_in6 *sender_addr) {
    socklen_t sender_len = sizeof(*sender_addr);
    
    int received = recvfrom(sock, buffer, buffer_size, 0, 
                          (struct sockaddr*)sender_addr, &sender_len);
    if (received < 0) {
        perror("recvfrom failed");
        return -1;
    }
    
    return received;
}