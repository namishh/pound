#ifndef CONFIG
#define CONFIG

#include "termios.h"

struct editorConfig {
  struct termios orig_termios;
};

#endif // !CONFIG
