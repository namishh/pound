#ifndef BUFFER
#define BUFFER

#include <stdio.h>

/* NOTE:
    the termios library is used to the change the _attributes_ of the terminal
    driver. in simpler words, it allows to customise the way to your terminal
    behaves when it recieves input or output
*/

#include <termios.h>
#include <unistd.h>

void enable_raw_mode();
void disable_raw_mode();

#endif // RAW
