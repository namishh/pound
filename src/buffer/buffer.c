#include "buffer.h"
#include <unistd.h>

void enable_raw_mode() {
  struct termios raw;

  /* NOTE:
     tcgetattr() -> gets all the current attributes of the standard input and
     saves them in raw
  */
  tcgetattr(STDIN_FILENO, &raw);

  // by turning off the ECHO flag, we can prevent characters from being echoed
  raw.c_cflag &= ~(ECHO);

  // now setting the edited attributes
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disableRawMode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }
