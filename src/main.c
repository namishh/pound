#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

enum editor_key {
  ARROW_LEFT = 1000,
  BACKSPACE = 127,
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

typedef struct row {
  int size;
  int rsize;
  char *render;
  char *chars;
} row;

typedef enum { NORMAL, INSERT } MODE;

struct cursor {
  int x;
  int y;
};

struct editor_config {
  struct termios orig_termios;
  struct window_size ws;
  char statusmsg[80];
  time_t statusmsg_time;
  struct cursor cur;
  int rx;
  int nrows;
  char *filename;
  int rowoff;
  int coloff;
  row *r;
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
  write(STDOUT_FILENO, "\x1b[2J", 4);
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
  int padding = (E.ws.columns - len) / 2 + 37;
  if (padding) {
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

int findn(int num) {
  /* math.h included */
  if (num == 0)
    return 1;
  return (int)log10(num) + 1;
}

char *pad_with_zeros(int number, int digits) {
  char *result = (char *)malloc(digits + 1); // +1 for the null terminator
  sprintf(result, "%0*d", digits, number);
  return result;
}

int ctrx(row *r, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (r->chars[j] == '\t')
      rx += (2 - 1) - (rx % 2);
    rx++;
  }
  return rx;
}

void scroll() {

  E.rx = 0;
  if (E.cur.y < E.nrows) {
    E.rx = ctrx(&E.r[E.cur.y], E.cur.x);
  }

  E.rx = E.cur.x;
  // Vertical Scrolling
  if (E.cur.y < E.rowoff) {
    E.rowoff = E.cur.y;
  }
  if (E.cur.y >= E.rowoff + E.ws.rows) {
    E.rowoff = E.cur.y - E.ws.rows + 1;
  }
  // Horizontal Scrolling
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.ws.columns) {
    E.coloff = E.rx - E.ws.columns + 1;
  }
}

char *shorten_path(char *path) {
  char *token = strtok(path, "/");
  char *last_dir = NULL;

  while (token != NULL) {
    last_dir = token;
    token = strtok(NULL, "/");
  }

  return last_dir;
}

char *get_file_extension(char *filepath) {
  if (!filepath)
    return " ";
  char *dot = strrchr(filepath, '.'); // Find the last occurrence of '.'
  if (!dot || dot == filepath)
    return filepath; // No extension found or dot is the first character

  return dot + 1; // Return the extension (skip the dot character)
}

char *get_devicon() {
  // get extension of filename
  // return the devicon for that extension
  char *ext = get_file_extension(E.filename);
  if (strcmp(ext, "c") == 0) {
    return "\x1b[34m   \x1b[0m";
  } else if (strcmp(ext, "Makefile") == 0) {
    return "\x1b[32m   \x1b[0m";
  } else {
    return "\x1b[37m 󰈔 \x1b[0m";
  }
}

void tab_bar(struct buffer *b) {
  char tab[100];
  int len = snprintf(tab, sizeof(tab), "\x1b[40m\x1b[34m   \x1b[0m %s",
                     E.filename ? E.filename : "Pound");
  buffer_append(b, tab, len);
  while (len < E.ws.columns) {
    buffer_append(b, " ", 1);
    len++;
  }
  buffer_append(b, "\x1b[m", 3);
  buffer_append(b, "\r\n", 2);
}

void status_message(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

void message_bar(struct buffer *b) {
  buffer_append(b, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.ws.columns)
    msglen = E.ws.columns;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    buffer_append(b, E.statusmsg, msglen);
}

void status_bar(struct buffer *b) {
  char *normal = "\x1b[044m\x1b[30m NORMAL \x1b[0m";
  char *normal_end = "\x1b[44m \x1b[0m";
  if (E.mode == INSERT) {
    normal = "\x1b[042m\x1b[30m INSERT \x1b[0m";
    normal_end = "\x1b[42m \x1b[0m";
  }
  char status[160], rstatus[10];
  char cwd[100];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    strcpy(cwd, "Too large");
  }
  char *path = shorten_path(cwd);
  char *devicon = get_devicon();
  int len = snprintf(status, sizeof(status),
                     "%s %s%.20s \x1b[30m | \x1b[39m %s \x1b[34m   \x1b[0m "
                     "\x1b[40m %d/%d \x1b[0m%s",
                     normal, devicon, E.filename ? E.filename : "Pound", path,
                     E.cur.y + 1, E.nrows, normal_end);
  int rlen = snprintf(rstatus, sizeof(rstatus), " ");
  if (len > E.ws.columns)
    len = E.ws.columns;

  buffer_append(b, status, len);
  while (len < E.ws.columns) {
    if (E.ws.columns - len == rlen - 46) {
      buffer_append(b, rstatus, rlen);
      break;
    } else {
      buffer_append(b, " ", 1);
      len++;
    }
  }
  buffer_append(b, "\r\n", 2);
}

void draw_rows(struct buffer *b) {
  int y;
  for (y = 0; y < E.ws.rows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.nrows) {
      if (E.nrows == 0 && y == E.ws.rows / 2) {
        for (size_t i = 0;
             i < sizeof(dashboard_lines) / sizeof(dashboard_lines[0]); i++) {
          dashboard_insert_line(dashboard_lines[i], b);
        }
      }
    } else {
      char line_number[findn(E.nrows) + 1];
      char *hex = "\x1b[30m";
      if (y + E.rowoff + 1 == E.cur.y + 1) {
        hex = "\x1b[37m";
      }
      sprintf(line_number, "%s%s\x1b[0m ", hex,
              pad_with_zeros(y + E.rowoff + 1, findn(E.nrows)));
      buffer_append(b, line_number, findn(E.nrows) + 10);
      int len = E.r[filerow].rsize - E.coloff;
      if (len < 0)
        len = 0;
      if (len > E.ws.columns)
        len = E.ws.columns;
      buffer_append(b, &E.r[filerow].render[E.coloff], len);
    }
    // clearing line by line instead of the whole screen
    buffer_append(b, "\x1b[K", 3);
    buffer_append(b, "\r\n", 2);
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

  scroll();

  struct buffer buf = BUFFER_INIT;
  // \xib -> escape character
  // URL - https://vt100.net/docs/vt100-ug/chapter3.html#ED
  buffer_append(&buf, "\x1b[?25l", 6);

  // moving cursor on top
  // H -> Mouse Command
  buffer_append(&buf, "\x1b[H", 3);

  tab_bar(&buf);
  draw_rows(&buf);
  status_bar(&buf);
  message_bar(&buf);

  char bu[32];
  snprintf(bu, sizeof(bu), "\x1b[%d;%dH", E.cur.y - E.rowoff + 2,
           E.rx - E.coloff + findn(E.nrows) + 2);
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
  E.mode = NORMAL;
  E.nrows = 0;
  E.coloff = 0;
  E.rowoff = 0;
  E.rx = 0;
  E.filename = NULL;
  E.cur.x = 0;
  E.cur.y = 0;
  E.r = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;

  if (window_size(&E.ws.rows, &E.ws.columns) == -1)
    die("window_size");
  E.ws.rows -= 3;
}

void move_cursor(int key) {
  row *r = (E.cur.y >= E.nrows) ? NULL : &E.r[E.cur.y];

  switch (key) {
  case ARROW_LEFT:
    if (E.cur.x >= 0) {
      E.cur.x--;
    } else if (E.cur.y > 0) {
      E.cur.y--;
      E.cur.x = E.r[E.cur.y].size;
    }
    break;
  case ARROW_DOWN:
    if (E.cur.y < E.nrows - 1) {
      E.cur.y++;
    }
    break;
  case ARROW_UP:
    if (E.cur.y != 0) {
      E.cur.y--;
    }
    break;
  case ARROW_RIGHT:
    if (r && E.cur.x < r->size) {
      E.cur.x++;
    } else if (r && E.cur.x == r->size) {
      E.cur.y++;
      E.cur.x = 0;
    }
    break;
  }

  r = (E.cur.y >= E.nrows) ? NULL : &E.r[E.cur.y];
  int rowlen = r ? r->size : 0;
  if (E.cur.x > rowlen) {
    E.cur.x = rowlen;
  }
}

void on_keypress_normal() {
  int c = read_key();
  switch (c) {
  case CTRL_KEY('q'):
    die("Exit Pound");
    exit(0);
    break;

  case 'i':
    E.mode = INSERT;
    break;

  case 'h':
    move_cursor(ARROW_LEFT);
    break;
  case 'j':
    move_cursor(ARROW_DOWN);
    break;
  case 'k':
    move_cursor(ARROW_UP);
    break;
  case 'l':
    move_cursor(ARROW_RIGHT);
    break;

  case '0':
    if (E.cur.y < E.nrows)
      E.cur.x = E.r[E.cur.y].size;
    break;

  case '$':
    if (E.cur.y < E.nrows)
      E.cur.x = E.r[E.cur.y].size;
    break;

  case 'G':
    E.cur.y = E.nrows;
    break;
  case 'g':
    if (E.hist.prev_key == 'g') {
      E.cur.y = 0;
    }
    break;
  }

  E.hist.prev_key = c;
}

void update_row(row *r) {
  int tabs = 0;
  int j;
  for (j = 0; j < r->size; j++)
    if (r->chars[j] == '\t')
      tabs++;

  free(r->render);
  r->render = malloc(r->size + tabs * 7 + 1);

  int idx = 0;

  for (j = 0; j < r->size; j++) {
    if (r->chars[j] == '\t') {
      r->render[idx++] = ' ';
      while (idx % 2 != 0)
        r->render[idx++] = ' ';
    } else {
      r->render[idx++] = r->chars[j];
    }
  }

  r->render[idx] = '\0';
  r->rsize = idx;
}

void append_row(char *s, size_t len) {
  E.r = realloc(E.r, sizeof(row) * (E.nrows + 1));

  int at = E.nrows;
  E.r[at].size = len;
  E.r[at].chars = malloc(len + 1);
  memcpy(E.r[at].chars, s, len);
  E.r[at].chars[len] = '\0';

  E.r[at].rsize = 0;
  E.r[at].render = NULL;
  update_row(&E.r[at]);

  E.nrows++;
}

void insert_char_row(row *r, int at, int c) {
  if (at < 0 || at > r->size)
    at = r->size;
  r->chars = realloc(r->chars, r->size + 2);
  memmove(&r->chars[at + 1], &r->chars[at], r->size - at + 1);
  r->size++;
  r->chars[at] = c;
  update_row(r);
}

void insert_char(int c) {
  if (E.cur.y == E.nrows) {
    append_row("", 0);
  }
  insert_char_row(&E.r[E.cur.y], E.cur.x, c);
  E.cur.x++;
}

void editor_open(char *filename) {
  free(E.filename);
  E.filename = filename;
  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  linelen = getline(&line, &linecap, fp);
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    append_row(line, linelen);
  }
  E.cur.x = findn(E.nrows) + 1;
  free(line);
  fclose(fp);
}

void on_keypress_insert() {
  int c = read_key();
  switch (c) {
  // disable special keys
  case '\r':
    /* TODO */
    break;
  case BACKSPACE:
  case CTRL_KEY('h'):
  case DEL_KEY:
    /* TODO */
    break;
  case CTRL_KEY('l'):

  case CTRL_KEY('q'):
    die("Exited");
    exit(0);
    break;

  // escape key
  case '\x1b':
    E.mode = NORMAL;
    break;

  case HOME_KEY:
    E.cur.x = 0;
    break;

  case END_KEY:
    if (E.cur.y < E.nrows)
      E.cur.x = E.r[E.cur.y].size;
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

  default:
    insert_char(c);
    break;
  }

  E.hist.prev_key = c;
}

// taking in arguments
int main(int argc, char *argv[]) {
  setlocale(LC_ALL, "");
  enable_raw_mode();
  init_editor();

  if (argc >= 2) {
    editor_open(argv[1]);
  }

  status_message("HELP: Ctrl-Q = quit");

  while (1) {
    refresh_screen();
    if (E.mode == NORMAL)
      on_keypress_normal();
    else if (E.mode == INSERT)
      on_keypress_insert();
  }

  return 0;
}
