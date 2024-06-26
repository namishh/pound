#include "common.h"
#include "stdio.h"
#include "stdlib.h"

void die(const char *s) {
  perror(s);
  exit(1);
}
