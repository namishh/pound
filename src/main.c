#include "buffer/buffer.h"
#include <unistd.h>

int main() {
  enable_raw_mode();
  char c;
  while (read(STDIN_FILENO, &c, 1) == 1)
    ;
  return 0;
}
