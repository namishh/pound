#include "buffer/buffer.h"
#include "common/common.h"
#include <unistd.h>
#include <ctype.h> // iscntrl


struct editor_config E;

int main() {
  enable_raw_mode();
  while(1) {
      char c = '\0';
    read(STDIN_FILENO, &c, 1);
    if (iscntrl(c)) {
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }
    if (c == 'q') break;

  }

  return 0;
}
