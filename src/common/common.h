#ifndef COMMON
#define COMMON

#include "termios.h"

struct editor_config {
  struct termios orig_termios;
};

void die(const char *s);

#endif // !COMMON
