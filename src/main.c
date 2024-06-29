#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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

#define TAB_STOP 2
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
  int idx;

  int size;
  int rsize;
  unsigned char *hl;
  int hl_open_comment;

  char *render;
  char *chars;
} row;

typedef enum { NORMAL, INSERT } MODE;

struct cursor {
  int x;
  int y;
};

#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)

struct syntax {
  char *filetype;
  char **filematch;
  char **keywords;
  char *singleline_comment_start;
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
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
  int dirty;
  struct syntax *syntax;
};

enum editorHighlight {
  HL_NORMAL = 0,
  HL_NUMBER,
  HL_MATCH,
  HL_STRING,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,

};

char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};

char *C_HL_keywords[] = {"switch", "if",        "#include", "while",   "for",
                         "break",  "continue",  "return",   "else",    "struct",
                         "union",  "typedef",   "static",   "enum",    "class",
                         "case",   "int|",      "long|",    "double|", "float|",
                         "char|",  "unsigned|", "signed|",  "void|",   NULL};

char *Py_HL_keywords[] = {"print",       "if",     "elif",   "else", "for",
                          "while",       "def",    "class",  "in",   "range",
                          "self",        "float|", "str|",   "int|", "list|",
                          "dictionary|", "set|",   "return", "do",   NULL};

char *Py_HL_extensions[] = {".py", NULL};

struct syntax HLDB[] = {
    {"c", C_HL_extensions, C_HL_keywords, "//", "/*", "*/",
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
    {"py", Py_HL_extensions, Py_HL_keywords, "#", "\"\"\"", "\"\"\"",
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

struct buffer {
  char *b;
  int len;
};

void refresh_screen();
char *start_prompt(char *prompt, void (*callback)(char *, int));
void del_row(int at);
void update_syntax(row *r);

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
      rx += (TAB_STOP - 1) - (rx % TAB_STOP);
    rx++;
  }
  return rx;
}

int rtcx(row *r, int rx) {
  int currx = 0;
  int cx;
  for (cx = 0; cx < r->size; cx++) {
    if (r->chars[cx] == '\t') {
      currx += (2 - 1) - (currx % 2);
    }
    currx++;
    if (currx > rx)
      break;
  }
  return cx;
}

void scroll() {

  E.rx = 0;
  if (E.cur.y < E.nrows) {
    E.rx = ctrx(&E.r[E.cur.y], E.cur.x);
  }

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
  int len = snprintf(tab, sizeof(tab), "\x1b[40m\x1b[34m   \x1b[0m %s%s",
                     E.filename ? E.filename : "Pound", E.dirty ? "* " : " ");
  buffer_append(b, tab, len);
  while (len < E.ws.columns) {
    buffer_append(b, " ", 1);
    len++;
  }
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

int syntcol(int hl) {
  switch (hl) {
  case HL_NUMBER:
    return 33;
  case HL_STRING:
    return 32;
  case HL_KEYWORD1:
    return 35;
  case HL_KEYWORD2:
    return 36;
  case HL_MATCH:
    return 44;
  case HL_COMMENT:
    return 30;
  case HL_MLCOMMENT:
    return 30;
  default:
    return 37;
  }
}

void detect() {
  E.syntax = NULL;
  if (E.filename == NULL)
    return;
  char *ext = strrchr(E.filename, '.');
  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
    struct syntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i]) {
      int is_ext = (s->filematch[i][0] == '.');
      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
          (!is_ext && strstr(E.filename, s->filematch[i]))) {
        E.syntax = s;
        int filerow;
        for (filerow = 0; filerow < E.nrows; filerow++) {
          update_syntax(&E.r[filerow]);
        }

        return;
      }
      i++;
    }
  }
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

      char *c = &E.r[filerow].render[E.coloff];
      unsigned char *hl = &E.r[filerow].hl[E.coloff];

      int current_color = -1;

      int j;
      for (j = 0; j < len; j++) {
        if (iscntrl(c[j])) {
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          buffer_append(b, "\x1b[7m", 4);
          buffer_append(b, &sym, 1);
          buffer_append(b, "\x1b[m", 3);
          if (current_color != -1) {
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
            buffer_append(b, buf, clen);
          }
        } else if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            buffer_append(b, "\x1b[0m", 4);
            current_color = -1;
          }
          buffer_append(b, &c[j], 1);
        } else {
          int color = syntcol(hl[j]);
          if (color != current_color) {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            buffer_append(b, buf, clen);
          }
          buffer_append(b, &c[j], 1);
        }
      }
      buffer_append(b, "\x1b[0m", 4);
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

int is_separator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void update_syntax(row *r) {
  r->hl = realloc(r->hl, r->rsize);
  memset(r->hl, HL_NORMAL, r->rsize);

  if (E.syntax == NULL)
    return;

  char **keywords = E.syntax->keywords;
  char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;

  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;

  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (r->idx > 0 && E.r[r->idx - 1].hl_open_comment);

  int i = 0;
  while (i < r->rsize) {
    char c = r->render[i];
    unsigned char prev_hl = (i > 0) ? r->hl[i - 1] : HL_NORMAL;

    if (scs_len && !in_string && !in_comment) {
      if (!strncmp(&r->render[i], scs, scs_len)) {
        memset(&r->hl[i], HL_COMMENT, r->rsize - i);
        break;
      }
    }

    if (mcs_len && mce_len && !in_string) {
      if (in_comment) {
        r->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&r->render[i], mce, mce_len)) {
          memset(&r->hl[i], HL_MLCOMMENT, mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (!strncmp(&r->render[i], mcs, mcs_len)) {
        memset(&r->hl[i], HL_MLCOMMENT, mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) {
        r->hl[i] = HL_STRING;
        if (c == '\\' && i + 1 < r->rsize) {
          r->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == in_string)
          in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (c == '"' || c == '\'' || c == '\'') {
          in_string = c;
          r->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }
    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {

      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
          (c == '.' && prev_hl == HL_NUMBER)) {
        r->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }

    if (prev_sep) {
      int j;
      for (j = 0; keywords[j]; j++) {
        int klen = strlen(keywords[j]);
        int kw2 = keywords[j][klen - 1] == '|';
        if (kw2)
          klen--;
        if (!strncmp(&r->render[i], keywords[j], klen) &&
            is_separator(r->render[i + klen])) {
          memset(&r->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL) {
        prev_sep = 0;
        continue;
      }
    }

    prev_sep = is_separator(c);
    i++;
  }
  int changed = (r->hl_open_comment != in_comment);
  r->hl_open_comment = in_comment;
  if (changed && r->idx + 1 < E.nrows)
    update_syntax(&E.r[r->idx + 1]);
}

void init_editor() {
  E.mode = NORMAL;
  E.nrows = 0;
  E.coloff = 0;
  E.rowoff = 0;
  E.rx = 0;
  E.dirty = 0;
  E.filename = NULL;
  E.cur.x = 0;
  E.cur.y = 0;
  E.r = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.syntax = NULL;
  if (window_size(&E.ws.rows, &E.ws.columns) == -1)
    die("window_size");
  E.ws.rows -= 3;
}

char *start_prompt(char *prompt, void (*callback)(char *, int)) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';

  while (1) {
    status_message(prompt, buf);
    refresh_screen();

    int c = read_key();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0)
        buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      status_message("");
      if (callback)
        callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r' && buflen != 0) {
      if (buflen != 0) {
        status_message("");
        if (callback)
          callback(buf, c);
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
    if (callback)
      callback(buf, c);
  }
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

void update_row(row *r) {
  int tabs = 0;
  int j;
  for (j = 0; j < r->size; j++)
    if (r->chars[j] == '\t')
      tabs++;

  free(r->render);
  r->render = malloc(r->size + tabs * (TAB_STOP - 1) + 1);

  int idx = 0;

  for (j = 0; j < r->size; j++) {
    if (r->chars[j] == '\t') {
      r->render[idx++] = ' ';
      while (idx % TAB_STOP != 0)
        r->render[idx++] = ' ';
    } else {
      r->render[idx++] = r->chars[j];
    }
  }

  r->render[idx] = '\0';
  r->rsize = idx;

  update_syntax(r);
}

void append_row(int at, char *s, size_t len) {

  if (at < 0 || at > E.nrows)
    return;
  E.r = realloc(E.r, sizeof(row) * (E.nrows + 1));
  memmove(&E.r[at + 1], &E.r[at], sizeof(row) * (E.nrows - at));
  for (int j = at + 1; j <= E.nrows; j++)
    E.r[j].idx++;

  E.r[at].idx = at;

  E.r[at].size = len;
  E.r[at].chars = malloc(len + 1);
  memcpy(E.r[at].chars, s, len);
  E.r[at].chars[len] = '\0';

  E.r[at].rsize = 0;
  E.r[at].render = NULL;
  E.r[at].hl = NULL;
  E.r[at].hl_open_comment = 0;
  update_row(&E.r[at]);

  E.nrows++;
  E.dirty++;
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

void insert_new_line() {
  if (E.cur.y == E.nrows) {
    append_row(E.nrows, "", 0);
  } else {
    row *r = &E.r[E.cur.y];
    append_row(E.cur.y + 1, &r->chars[E.cur.x], r->size - E.cur.x);
    r = &E.r[E.cur.y];
    r->size = E.cur.x;
    r->chars[r->size] = '\0';
    update_row(r);

    E.cur.y++;
    // Auto-indenting
    if (E.cur.y > 0) {
      row *prev_row = &E.r[E.cur.y - 1];
      int indent = 0;
      while (indent < prev_row->size && prev_row->chars[indent] == ' ') {
        insert_char_row(&E.r[E.cur.y], indent, ' ');
        indent++;
      }
      E.cur.x = indent;
    }
  }
}

void row_del_char(row *r, int at) {
  if (at < 0 || at >= r->size)
    return;
  memmove(&r->chars[at], &r->chars[at + 1], r->size - at);
  r->size--;
  update_row(r);
  E.dirty++;
}

void free_row(row *r) {
  free(r->render);
  free(r->chars);
  free(r->hl);
}

void del_row(int at) {
  if (at < 0 || at >= E.nrows)
    return;
  free_row(&E.r[at]);
  memmove(&E.r[at], &E.r[at + 1], sizeof(row) * (E.nrows - at - 1));
  E.nrows--;
  E.dirty++;
}

void insert_char(int c) {
  if (E.cur.y == E.nrows) {
    append_row(E.nrows, "", 0);
  }
  insert_char_row(&E.r[E.cur.y], E.cur.x, c);
  E.cur.x++;
  E.dirty++;
}

void append_string(row *r, char *s, size_t len) {
  r->chars = realloc(r->chars, r->size + len + 1);
  memcpy(&r->chars[r->size], s, len);
  r->size += len;
  r->chars[r->size] = '\0';
  update_row(r);
  E.dirty++;
}

void del_char() {
  if (E.cur.y == E.nrows)
    return;
  if (E.cur.x == 0 && E.cur.y == 0)
    return;
  row *r = &E.r[E.cur.y];
  if (E.cur.x > 0) {
    row_del_char(r, E.cur.x - 1);
    E.cur.x--;
  } else {
    E.cur.x = E.r[E.cur.y - 1].size;
    append_string(&E.r[E.cur.y - 1], r->chars, r->size);
    del_row(E.cur.y);
    E.cur.y--;
  }
}

char *rts(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.nrows; j++)
    totlen += E.r[j].size + 1;
  *buflen = totlen;
  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.nrows; j++) {
    memcpy(p, E.r[j].chars, E.r[j].size);
    p += E.r[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

void save() {
  if (E.filename == NULL) {

    E.filename = start_prompt("Save as: %s", NULL);
    if (E.filename == NULL) {
      status_message("Save aborted");
      return;
    }
    detect();
  }
  int len;
  char *buf = rts(&len);
  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);

  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        status_message("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  status_message("Can't save! I/O error: %s", strerror(errno));
}

void editor_open(char *filename) {
  free(E.filename);
  E.filename = filename;
  detect();
  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  linelen = getline(&line, &linecap, fp);
  if (linelen != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    append_row(E.nrows, line, linelen);
  }

  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    append_row(E.nrows, line, linelen);
  }
  E.cur.x = findn(E.nrows) + 1;
  E.dirty = 0;
  free(line);
  fclose(fp);
}

void normal_d() {
  int c = read_key();
  switch (c) {
  case 'd':
    del_row(E.cur.y);
    status_message("");
    break;
  default:
    status_message("%c is undefined", c);
    break;
  }
}

void vim_prompt() {
  char *cmd = start_prompt(":%s", NULL);
  if (cmd == NULL) {
    status_message("Command aborted");
    return;
  }
  // check if cmd is a number
  if (isdigit(cmd[0])) {
    int line = atoi(cmd);
    if (line > 0 && line <= E.nrows) {
      E.cur.y = line - 1;
    } else {
      status_message("Invalid line number");
    }
  } else if (strcmp(cmd, "w") == 0) {
    save();
  } else if (strcmp(cmd, "q") == 0) {
    if (E.dirty) {
      status_message("No write since last change (add ! to override)");
    } else {
      die("Exit Pound");
      exit(0);
    }
  } else if (strcmp(cmd, "w") == 0) {
    save();
  } else if (strcmp(cmd, "q!") == 0) {
    die("Exit Pound");
    exit(0);
  } else if (strcmp(cmd, "wq") == 0 || strcmp(cmd, "x") == 0) {
    save();
    die("Exit Pound");
    exit(0);
  } else {
    status_message("Command not found: %s", cmd);
  }
}

void f_mode() {
  int c = read_key();
  // jump to the next occurence of the character
  for (int i = E.cur.x; i < E.r[E.cur.y].size; i++) {
    if (E.r[E.cur.y].chars[i] == c) {
      E.cur.x = i;
      break;
    }
  }
}

void search_callback(char *query, int key) {
  static int last_match = -1;
  static int direction = 1;

  static int saved_hl_line;
  static char *saved_hl = NULL;
  if (saved_hl) {
    memcpy(E.r[saved_hl_line].hl, saved_hl, E.r[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl = NULL;
  }

  if (key == '\r' || key == '\x1b') {
    last_match = -1;
    direction = 1;
    return;
  } else if (key == CTRL_KEY('B')) {
    direction = 1;
  } else if (key == CTRL_KEY('N')) {
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }
  if (last_match == -1)
    direction = 1;
  int current = last_match;
  int i;
  for (i = 0; i < E.nrows; i++) {
    current += direction;
    if (current == -1)
      current = E.nrows - 1;
    else if (current == E.nrows)
      current = 0;

    row *r = &E.r[current];
    char *match = strstr(r->render, query);
    if (match) {
      last_match = current;
      E.cur.y = current;
      E.cur.x = rtcx(r, match - r->render);
      E.rowoff = E.nrows;

      saved_hl_line = current;
      saved_hl = malloc(r->rsize);
      memcpy(saved_hl, r->hl, r->rsize);

      memset(&r->hl[match - r->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

void search() {
  int saved_cx = E.cur.x;
  int saved_cy = E.cur.y;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;

  char *query = start_prompt("/%s", search_callback);

  if (query) {
    free(query);
  } else {
    E.cur.x = saved_cx;
    E.cur.y = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }
}

void on_keypress_normal() {
  int c = read_key();
  switch (c) {
  case CTRL_KEY('x'):
    die("Exit Pound");
    exit(0);
    break;

  case 'f':
    f_mode();
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

  case '/':
    search();
    break;

  case 'a':
    E.cur.x++;
    E.mode = INSERT;
    break;
  case 'A':
    if (E.cur.y < E.nrows)
      E.cur.x = E.r[E.cur.y].size; // move to end of the line
    E.mode = INSERT;
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
    E.cur.y = 0;
    break;
  case 'o':
    append_row(E.cur.y + 1, "", 0);
    E.cur.y++;
    E.cur.x = 0;
    E.mode = INSERT;
    break;

  case '{':
    while (E.cur.y > 0 && E.r[E.cur.y].size == 0)
      E.cur.y--;
    while (E.cur.y > 0 && E.r[E.cur.y].size > 0)
      E.cur.y--;
    break;

  case '}':
    while (E.cur.y < E.nrows && E.r[E.cur.y].size == 0)
      E.cur.y++;
    while (E.cur.y < E.nrows && E.r[E.cur.y].size > 0)
      E.cur.y++;
    break;

  case 'x':
    move_cursor(ARROW_RIGHT);
    del_char();
    break;

  case ':':
    vim_prompt();
    break;

  case 'd':
    normal_d();
    break;

    E.hist.prev_key = c;
  }
}
void on_keypress_insert() {
  int c = read_key();
  switch (c) {
  // disable special keys
  case '\r':
    insert_new_line();
    break;
  case BACKSPACE:
  case CTRL_KEY('h'):
  case DEL_KEY:
    del_char();
    break;
  case CTRL_KEY('l'):
    break;

  case CTRL_KEY('x'):
    die("Exited");
    exit(0);
    break;

  case CTRL_KEY('s'):
    save();
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

  case '{':
    insert_char(c);
    insert_char('}');
    E.cur.x--;
    break;

  case '<':
    insert_char(c);
    insert_char('>');
    E.cur.x--;
    break;

  case '[':
    insert_char(c);
    insert_char(']');
    E.cur.x--;
    break;

  case '(':
    insert_char(c);
    insert_char(')');
    E.cur.x--;
    break;

  case '\'':
    insert_char(c);
    insert_char('\'');
    E.cur.x--;
    break;

  case '"':
    insert_char(c);
    insert_char('"');
    E.cur.x--;
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

  status_message("HELP: :q = quit");

  while (1) {
    refresh_screen();
    if (E.mode == NORMAL)
      on_keypress_normal();
    else if (E.mode == INSERT)
      on_keypress_insert();
  }

  return 0;
}
