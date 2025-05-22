#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include "include/multicast.h"

int setup_sock_opt(int sock) {
  // Allow address reuse
  int optval = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
    perror("setsockopt(SO_REUSEADDR) a échoué");
    close(sock);
    return -1;
  }

  // Allow port reuse
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

  if (setup_sock_opt(sock) < 0) {
    close(sock);
    return -1;
  }

  // Create the address
  struct sockaddr_in6 s_addr;
  memset(&s_addr, 0, sizeof(s_addr));
  s_addr.sin6_family = AF_INET6;
  s_addr.sin6_addr = in6addr_any;
  s_addr.sin6_port = htons(port);

  // Bind the socket
  if (bind(sock, (struct sockaddr*)&s_addr, sizeof(s_addr)) < 0) {
    perror("bind a échoué");
    close(sock);
    return -1;
  }

  // Join the multicast group
  struct ipv6_mreq group;
  memset(&group, 0, sizeof(group));
  if (inet_pton(AF_INET6, addr, &group.ipv6mr_multiaddr) <= 0) {
    perror("inet_pton a échoué");
    close(sock);
    return -1;
  }
  group.ipv6mr_interface = if_nametoindex("eth0");
  if (group.ipv6mr_interface == 0) {
    perror("Impossible de trouver une interface réseau valide");
    close(sock);
    return -1;
  }

  if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &group, sizeof(group)) < 0) {
    perror("setsockopt(IPV6_JOIN_GROUP) a échoué");
    close(sock);
    return -1;
  }

  printf("  Multicast receveur PORT: %d, interface index: %u\n", port, group.ipv6mr_interface);

  return sock;
}

int setup_multicast_sender() {
  int sock = socket(AF_INET6, SOCK_DGRAM, 0);
  if (sock < 0) {
    perror("création du socket a échoué");
    return -1;
  }

  if (setup_sock_opt(sock) < 0) {
    close(sock);
    return -1;
  }

  return sock;
}

int setup_unicast_socket(int port) {
  // Socket pour recevoir les réponses unicast
  int unicast_sock = socket(AF_INET6, SOCK_DGRAM, 0);
  if (unicast_sock < 0) {
    perror("Échec de création du socket unicast");
    return -1;
  }

  // Configuration du socket unicast
  struct sockaddr_in6 local_addr;
  memset(&local_addr, 0, sizeof(local_addr));
  local_addr.sin6_family = AF_INET6;
  local_addr.sin6_addr = in6addr_any;
  local_addr.sin6_port = htons(port); // Utiliser notre port pour la réception

  // Options pour réutiliser l'adresse/port
  if (setup_sock_opt(unicast_sock) < 0) {
    close(unicast_sock);
    return -1;
  }

  // Associer le socket à notre adresse/port
  if (bind(unicast_sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
    perror("bind a échoué pour le socket unicast");
    close(unicast_sock);
    return -1;
  }

  printf("  Socket unicast configuré pour la réception sur port %d\n", port);
  return unicast_sock;
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

int send_unicast(int sock, struct sockaddr_in6 *dest_addr, const void *data, size_t len) {
  if (sendto(sock, data, len, 0, (struct sockaddr*)dest_addr, sizeof(*dest_addr)) < 0) {
    perror("sendto a échoué");
    return -1;
  }

  return 0;
}

int receive_multicast(int sock, char *buffer, size_t buffer_size, struct sockaddr_in6 *sender_addr) {
  socklen_t sender_len = sizeof(*sender_addr);

  int received = recvfrom(sock, buffer, buffer_size, 0, (struct sockaddr*)sender_addr, &sender_len);

  if (received < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // Timeout, not a fatal error
      return -1;
    } else {
      perror("recvfrom a échoué");
      return -1;
    }
  }

  return received;
}
