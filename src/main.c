#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "include/pairs.h"
#include "include/auction.h"

int main(int argc, char *argv[]) {
  if (init_pairs() < 0) {
    fprintf(stderr, "Failed to initialize pairs\n");
    return EXIT_FAILURE;
  }
  fprintf(stderr, "Pairs initialized successfully\n");
}
