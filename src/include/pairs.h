#include <stdint.h>
#include <netinet/in.h>

typedef struct Pair
{
    int id;
    struct in6_addr ip;
    int port;
    
} Pair;
