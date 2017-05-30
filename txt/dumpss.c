/* dump segment registers */

#include <stdio.h>

int main(int argc, char **argv) {
  register unsigned short s asm("ax");

#define PRINT_SS(REG) \
  __asm__("movw %" #REG ",%ax");\
  printf(#REG ": %04x\n", (int)s)

  PRINT_SS(gs);
  PRINT_SS(fs);
  PRINT_SS(es);
  PRINT_SS(ds);
  PRINT_SS(cs);
  PRINT_SS(ss);

  return 0;
}
