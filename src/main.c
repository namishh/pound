#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

enum editor_key {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_DOWN
};

struct history {
  char prev_key;
};

struct window_size {
  int rows;
  int columns;
};

typedef enum { NORMAL, INSERT } MODE;

struct cursor {
  int x;
  int y;
};

struct editor_config {
  struct termios orig_termios;
  struct window_size ws;
  struct cursor cur;
  struct history hist;
  MODE mode;
};

struct buffer {
  char *b;
  int len;
};

// buffer methods

void buffer_append(struct buffer *buf, const char *s, int len) {
  char *new = realloc(buf->b, buf->len + len);
  if (new == NULL)
    return;

  memcpy(&new[buf->len], s, len);
  buf->b = new;
  buf->len += len;
}

void buffer_free(struct buffer *buf) { free(buf->b); }

// empty buffer
#define BUFFER_INIT                                                            \
  { NULL, 0 }

#define CTRL_KEY(k) ((k) & 0x1f)

struct editor_config E;

void die(const char *s) {
  write(STDIN_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s);
  exit(1);
}

void disable_raw_mode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void dashboard_insert_line(char line[], struct buffer *b) {
  int len = strlen(line);
  if (len > E.ws.columns) {
    len = E.ws.columns;
  }
  int padding = (E.ws.columns - len) / 2 + 30;
  if (padding) {
    buffer_append(b, "~", 1);
    padding--;
  }
  while (padding--)
    buffer_append(b, " ", 1);

  buffer_append(b, line, len);
  buffer_append(b, "\r\n", 2);
}

char *dashboard_lines[] = {
    "⡆⣸⡟⣼⣯⠏⣾⣿⢸⣿⢸⣿⣿⣿⣿⣿⣿⡟⠸⠁⢹⡿⣿⣿⢻⣿⣿⣿⣿                                ",
    "⡇⡟⣸⢟⣫⡅⣶⢆⡶⡆⣿⣿⣿⣿⣿⢿⣛⠃⠰⠆⠈⠁⠈⠙⠈⠻⣿⢹⡏.  The \x1B[31mPound\x1B[0m Text Editor",
    "⣧⣱⡷⣱⠿⠟⠛⠼⣇⠇⣿⣿⣿⣿⣿⣿⠃⣰⣿⣿⡆⠄⠄⠄⠄⠄⠉⠈⠄⠄  by \x1b[32mchadcat7\x1b[0m        ",
    "⡏⡟⢑⠃⡠⠂⠄⠄⠈⣾⢻⣿⣿⡿⡹⡳⠋⠉⠁⠉⠙⠄⢀⠄⠄⠄⠄⠄⠂⠄                              ",
    "⡇⠁⢈⢰⡇⠄⠄⡙⠂⣿⣿⣿⣿⣱⣿⡗⠄⠄⠄⢀⡀⠄⠈⢰⠄⠄⠄⠐⠄⠄  Original text editor       ",
    "⠄⠄⠘⣿⣧⠴⣄⣡⢄⣿⣿⣿⣷⣿⣿⡇⢀⠄⠤⠈⠁⣠⣠⣸⢠⠄⠄⠄⠄⠄  \x1B[34mantirez/kilo\x1b[0m       ",
    "⢀⠄⠄⣿⣿⣷⣬⣵⣿⣿⣿⣿⣿⣿⣿⣷⣟⢷⡶⢗⡰⣿⣿⠇⠘⠄⠄⠄⠄⠄                             ",
    "⣿⠄⠄⠘⢿⣿⣿⣿⣿⣿⣿⢛⣿⣿⣿⣿⣿⣿⣿⣿⣿⣟⢄⡆⠄⢀⣪⡆⠄⣿                             "};

void draw_rows(struct buffer *b) {
  int y;
  for (y = 0; y < E.ws.rows; y++) {

    if (y == E.ws.rows / 2) {
      for (size_t i = 0;
           i < sizeof(dashboard_lines) / sizeof(dashboard_lines[0]); i++) {
        dashboard_insert_line(dashboard_lines[i], b);
      }
      buffer_append(b, "~", 1);
    } else {
      buffer_append(b, "~", 1);
    }
    // clearing line by line instead of the whole screen
    buffer_append(b, "\x1b[K", 3);
    if (y < E.ws.rows - 1) {
      buffer_append(b, "\r\n", 2);
    }
  }
}

void enable_raw_mode() {
  /* NOTE:
     tcgetattr() -> gets all the current attributes of the standard input and
     saves them in raw
  */
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");

  atexit(disable_raw_mode);

  struct termios raw = E.orig_termios;

  // by turning off the ECHO flag, we can prevent characters from being echoed
  // disable CANON mode so we read input byte by byte instead of line by line
  // disabling ISIG  disables Ctrl + C and Ctrl + Z
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP |
                   IXON); // disabling break conditions, ISTRIP causes the 8th
                          // bit of each input byte to be stripped, meaning it
                          // will set it to 0, disables Ctrl + S and Ctrl + Q

  raw.c_cflag |= (CS8);

  raw.c_oflag &= ~(OPOST);

  // read will return nothing after such amount of time.
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  // now setting the edited attributes
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

void refresh_screen() {
  struct buffer buf = BUFFER_INIT;
  // \xib -> escape character
  // URL - https://vt100.net/docs/vt100-ug/chapter3.html#ED
  buffer_append(&buf, "\x1b[?25l", 6);

  // moving cursor on top
  // H -> Mouse Command
  buffer_append(&buf, "\x1b[H", 3);

  draw_rows(&buf);

  char bu[32];
  snprintf(bu, sizeof(bu), "\x1b[%d;%dH", E.cur.y + 1, E.cur.x + 1);
  buffer_append(&buf, bu, strlen(bu));

  buffer_append(&buf, "\x1b[?25h", 6);

  write(STDOUT_FILENO, buf.b, buf.len);
  buffer_free(&buf);
}

int read_key() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '1':
            return HOME_KEY;
          case '4':
            return END_KEY;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
  return c;
}
int cursor_position(int *rows, int *cols) {
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  printf("\r\n");
  char c;
  while (read(STDIN_FILENO, &c, 1) == 1) {
    if (iscntrl(c)) {
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }
  }
  read_key();

  return -1;
}

// NOTE: this may not work on all machines
int window_size(int *rows, int *cols) {
  // ioctl(), TIOCGWINSZ, and struct winsize come from <sys/ioctl.h>.
  // ioctl() will place the number of columns wide and the number of rows high
  // the terminal is into the given winsize

  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    return cursor_position(rows, cols);
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

void init_editor() {
  if (window_size(&E.ws.rows, &E.ws.columns) == -1)
    die("window_size");
}

void move_cursor(int key) {
  switch (key) {
  case ARROW_LEFT:
    if (E.cur.x != 0) {
      E.cur.x--;
      break;
    }
  case ARROW_DOWN:
    if (E.cur.y != E.ws.rows - 1) {
      E.cur.y++;
      break;
    }
  case ARROW_UP:
    if (E.cur.y != 0) {
      E.cur.y--;
      break;
    }
  case ARROW_RIGHT:
    if (E.cur.x != E.ws.columns - 1) {
      E.cur.x++;
      break;
    }
  }
}

void on_keypress() {
  int c = read_key();
  switch (c) {
  case CTRL_KEY('q'):
    refresh_screen();
    exit(0);
    break;

  case 'i':
    if (E.mode == NORMAL) {
      E.mode = INSERT;
    }
    break;

  // escape key
  case '\x1b':
    E.mode = NORMAL;
    break;

  case 'h':
    if (E.mode == NORMAL) {
      move_cursor(ARROW_LEFT);
    }
    break;
  case 'j':
    if (E.mode == NORMAL) {
      move_cursor(ARROW_DOWN);
    }
    break;
  case 'k':
    if (E.mode == NORMAL) {
      move_cursor(ARROW_UP);
    }
    break;
  case 'l':
    if (E.mode == NORMAL) {
      move_cursor(ARROW_RIGHT);
    }
    break;
  case 'G':
    if (E.mode == NORMAL) {
      int times = E.ws.rows;
      while (times--)
        move_cursor(ARROW_DOWN);
    }
    break;
  case 'g':
    if (E.mode == NORMAL && E.hist.prev_key == 'g') {
      int times = E.ws.rows;
      while (times--)
        move_cursor(ARROW_UP);
    }
    break;

  case HOME_KEY:
    E.cur.x = 0;
    break;
  case END_KEY:
    E.cur.x = E.ws.columns - 1;
    break;

  case PAGE_UP:
  case PAGE_DOWN: {
    int times = E.ws.rows;
    while (times--)
      move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;

  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    move_cursor(c);
    break;
  }

  E.hist.prev_key = c;
}

int main() {
  setlocale(LC_ALL, "");
  E.mode = NORMAL;
  E.cur.x = 0;
  E.cur.y = 0;
  enable_raw_mode();
  init_editor();

  while (1) {
    refresh_screen();
    on_keypress();
  }

  return 0;
}
