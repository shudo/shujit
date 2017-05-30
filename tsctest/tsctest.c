#include <stdio.h>
#include "x86tsc.h"

int main(int argc, char **argv) {
  unsigned long long int clock;
  int i;

  tscStart();
  tscEnd();
  clock = tscClock();
  printf("clocks: %llu\n", clock);

  for (i = 0; i < 10; i++) {
    printf("TSC: %llu\n", rdtsc());
  }

  return 0;
}
