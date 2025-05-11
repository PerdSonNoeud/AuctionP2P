#include <netinet/in.h>

// Structure pour stocker les informations d'un pair
typedef struct {
    unsigned short id;
    struct in6_addr ip;
    unsigned short port;
    int active;
} Pair;

// Structure pour le système
typedef struct {
    Pair *pairs;
    int count;
    int capacity;
    unsigned short my_id;
    struct in6_addr my_ip;
    unsigned short my_port;
    char liaison_addr[46];
    int liaison_port;
    char auction_addr[46];
    int auction_port;
} PairSystem;

// Fonctions
int init_pairs();
int join_auction();
int handle_join(int sock);
int add_pair(unsigned short id, struct in6_addr ip, unsigned short port);