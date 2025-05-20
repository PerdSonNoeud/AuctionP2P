#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "include/multicast.h"

int setup_multicast_opt(int sock) {
  // Permettre la réutilisation de l'adresse
  int optval = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
    perror("setsockopt(SO_REUSEADDR) a échoué");
    close(sock);
    return -1;
  }

  // Permettre la réutilisation du port
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) < 0) {
    perror("setsockopt(SO_REUSEPORT) a échoué");
    close(sock);
    return -1;
  }

  return 0;
}

int setup_multicast_receiver(const char *addr, int port) {
  int sock = socket(AF_INET6, SOCK_DGRAM, 0);
  if (sock < 0) {
    perror("création du socket a échoué");
    return -1;
  }

  if (setup_multicast_opt(sock) < 0) {
    close(sock);
    return -1;
  }

  // On fait l'adresse
  struct sockaddr_in6 s_addr;
  memset(&addr, 0, sizeof(addr));
  s_addr.sin6_family = AF_INET6;
  s_addr.sin6_addr = in6addr_any;
  s_addr.sin6_port = htons(port);

  // Lier le socket
  if (bind(sock, (struct sockaddr*)&s_addr, sizeof(s_addr)) < 0) {
    perror("bind a échoué");
    close(sock);
    return -1;
  }

  // Rejoindre le groupe multicast
  struct ipv6_mreq group;
  memset(&group, 0, sizeof(group));
  if (inet_pton(AF_INET6, addr, &group.ipv6mr_multiaddr) <= 0) {
    perror("inet_pton a échoué");
    close(sock);
    return -1;
  }
  group.ipv6mr_interface = if_nametoindex("eth0");

  if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &group, sizeof(group)) < 0) {
    perror("setsockopt(IPV6_JOIN_GROUP) a échoué");
    close(sock);
    return -1;
  }

  return sock;
}

int setup_multicast_sender(const char *addr, int port) {
  int sock = socket(AF_INET6, SOCK_DGRAM, 0);
  if (sock < 0) {
    perror("création du socket a échoué");
    return -1;
  }

  sockaddr_in6 gaddr;
  memset(&gaddr, 0, sizeof(gaddr));
  gaddr.sin6_family = AF_INET6;
  inet_pton(AF_INET6, addr, &gaddr.sin6_addr);
  gaddr.sin6_port = htons(port);

  if (setup_multicast_opt(sock) < 0) {
    close(sock);
    return -1;
  }

  // Configurer le socket pour envoyer des paquets multicast
  int ifindex = if_nametoindex("eth0");
  if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifindex, sizeof(ifindex)) < 0) {
    perror("setsockopt(IPV6_MULTICAST_IF) a échoué");
    close(sock);
    return -1;
  }

  if (bind(sock, (struct sockaddr*)&gaddr, sizeof(gaddr)) < 0) {
    perror("bind a échoué");
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
    perror("inet_pton a échoué");
    return -1;
  }
  if (sendto(sock, data, len, 0, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
    perror("sendto a échoué");
    return -1;
  }

  return 0;
}

int receive_multicast(int sock, void *buffer, size_t buffer_size, struct sockaddr_in6 *sender_addr) {
  socklen_t sender_len = sizeof(*sender_addr);

  int received = recvfrom(sock, buffer, buffer_size, 0, (struct sockaddr*)sender_addr, &sender_len);

  if (received < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // Timeout, pas d'erreur fatale
      return -1;
    } else {
      perror("recvfrom a échoué");
      return -1;
    }
  }

  return received;
}
