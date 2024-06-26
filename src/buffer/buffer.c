#include "buffer.h"
#include "../common/common.h"
#include <stdlib.h>
#include <unistd.h>

struct termios orig_termios;

void enable_raw_mode() {
  /* NOTE:
     tcgetattr() -> gets all the current attributes of the standard input and
     saves them in raw
  */
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
  atexit(disable_raw_mode);

  struct termios raw = orig_termios;

  // by turning off the ECHO flag, we can prevent characters from being echoed
  // disable CANON mode so we read input byte by byte instead of line by line
  // disabling ISIG  disables Ctrl + C and Ctrl + Z
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // disabling Ctrl + S and Ctrl + Q

  raw.c_cflag |= (CS8);

  raw.c_oflag &= ~(OPOST);

  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  // now setting the edited attributes
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

void disable_raw_mode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    die("tcsetattr");
}

