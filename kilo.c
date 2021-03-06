/*** links ***/

// https://viewsourcecode.org/snaptoken/kilo/02.enteringRawMode.html
// https://viewsourcecode.org/snaptoken/kilo/03.rawInputAndOutput.html
// https://viewsourcecode.org/snaptoken/kilo/04.aTextViewer.html
// https://viewsourcecode.org/snaptoken/kilo/05.aTextEditor.html

/*** includes ***/

// Step 59 - feature test macro
// https://www.gnu.org/software/libc/manual/html_node/Feature-Test-Macros.html
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/

// Step 41
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8

// The CTRL_KEY macro bitwise-ANDs a character with the value 00011111, in
// binary. (In C, you generally specify bitmasks using hexadecimal, since C
// doesn’t have binary literals, and hexadecimal is more concise and readable
// once you get used to it.) In other words, it sets the upper 3 bits of the
// character to 0. This mirrors what the Ctrl key does in the terminal: it
// strips bits 5 and 6 from whatever key you press in combination with Ctrl, and
// sends that. (By convention, bit numbering starts from 0.) The ASCII character
// set seems to be designed this way on purpose. (It is also similarly designed
// so that you can set and clear bit 5 to switch between lowercase and
// uppercase.)
#define CTRL_KEY(k) ((k)&0x1f)

enum editorKey { 
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT, 
  ARROW_UP, 
  ARROW_DOWN, 
  DEL_KEY,
  HOME_KEY, 
  END_KEY,
  PAGE_UP,   
  PAGE_DOWN
};

/*** data ***/

// erow stands for “editor row”, and stores a line
// of text as a pointer to the dynamically-allocated
// character data and a length.
// The typedef lets us refer to the type as erow
// instead of struct erow.
typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;
} erow;

struct editorConfig {
  int cx, cy;
  int rx;
  int rowoff;
  int coloff; 
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
  // printf("Quitting after disabling raw mode!\n");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");

  // We use it to register our disableRawMode() function to be called
  // automatically when the program exits, whether it exits by returning from
  // main(), or by calling the exit() function
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  // ECHO is a bitflag, defined as 00000000000000000000000000001000 in binary.
  // We use the bitwise-NOT operator (~) on this value to get
  // 11111111111111111111111111110111. We then bitwise-AND this value with the
  // flags field, which forces the fourth bit in the flags field to become 0,
  // and causes every other bit to retain its current value. Flipping bits like
  // this is common in C.
  // Step 7
  // There is an ICANON flag that allows us to turn off canonical mode. This
  // means we will finally be reading input byte-by-byte, instead of
  // line-by-line.
  // Step 9
  // Disable Ctrl-C and Ctrl-Z using ISIG
  // Step 10
  // Disable Ctrl-S and Ctrl-Q

  // They are read as 19 and 17 respectively
  // Step 11
  // Disable Ctrl-V and Ctrl-O
  // Step 12
  // Disable Ctrl-M and Ctrl-J
  // They are read as 13 and 10 respectively
  // Step 15 - turn off more flags
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  // Step 15
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  // Step 13
  // turnoff output processing
  // turnoff POST processing of outputs
  // From now on, we’ll have to write out the full "\r\n" whenever we want to
  // start a new line.
  raw.c_oflag = ~(OPOST);
  // Step 16
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  // Step 18 - testing
  // An easy way to make tcgetattr() fail is to
  // give your program a text file or a pipe as
  // the standard input instead of your terminal.
  // To give it a file as standard input,
  // run ./kilo <kilo.c. To give it a pipe,
  // run echo test | ./kilo. Both should result
  // in the same error from tcgetattr(),
  // something like Inappropriate ioctl for device.
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

// editorReadKey()’s job is to wait for one keypress, and return it. Later,
// we’ll expand this function to handle escape sequences, which involves reading
// multiple bytes that represent a single keypress, as is the case with the
// arrow keys.
int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else { 
        switch (seq[1]) { 
          case 'A': return ARROW_UP; 
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT; 
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) { 
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }
    
    return '\x1b';
  } else { 
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++; 
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;
  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    // As you might have gathered from the code, there is no simple “move the
    // cursor to the bottom-right corner” command.
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** row operations ***/ 

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) { 
    if (row->chars[j] == '\t')
      rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
    rx++;
  }
  return rx;
}

void editorUpdateRow(erow *row) { 
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
      if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);

    int idx = 0; 
    for (j = 0; j < row->size; j++) { 
        if (row->chars[j] == '\t') {
          row->render[idx++] = ' '; 
          while( idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
          row-> render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) { 
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1); 
  memcpy(E.row[at].chars, s, len); 
  E.row[at].chars[len] = '\0'; 

  E.row[at].rsize = 0; 
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++; 

}

void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
}

/*** editor operations ***/

void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorAppendRow("", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

/*** file i/o ***/ 

// Step 105 
// a function that converts our array of erow
// structs into a single string that is ready
// to be written out to a file.
char *editorRowsToString(int *buflen) { 
    int totlen = 0; 
    int j; 
    for (j = 0; j < E.numrows; j++) 
      totlen += E.row[j].size + 1; 
    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf; 
    for (j  = 0; j < E.numrows; j++) { 
        memcpy(p, E.row[j].chars, E.row[j].size); 
        p += E.row[j].size; 
        *p = '\n'; 
        p++; 
    }

    return buf;

}

void editorOpen(char *filename) { 
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fp = fopen(filename, "r"); 
    if (!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0; 
    ssize_t linelen;
    // Step 65 - read in the whole file
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
      while (linelen >0 && (line[linelen-1] == '\n' || 
                              line[linelen-1] == '\r'))
        linelen--;
      editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
}

void editorSave() { 
    if (E.filename == NULL) return;

    int len; 
    char *buf = editorRowsToString(&len);

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644); 
    ftruncate(fd, len);
    write(fd, buf, len);
    close(fd); 
    free(buf);
}

/*** append buffer ***/

// Step 36 - declaring a buffer
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}  // initializer for a buffer

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

/*** output ***/

// Step 68
void editorScroll() { 
  E.rx = 0;

  // Step 89
  if (E.cy < E.numrows) { 
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff) { 
      E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) { 
    E.rowoff = E.cy - E.screenrows + 1;
  }
  // Step 73
  // Exact parallel to vertical scrolling mode
  // E.cx <= E.cy 
  // E.rowoff <= E.coloff
  // E.screenrows <= E.screencols

  // Step 86 
  if (E.rx < E.coloff) { 
      E.coloff = E.rx; 
  }
  if (E.rx >= E.coloff + E.screencols) {
      E.coloff = E.rx - E.screencols + 1;
  }
}

// editorDrawRows() will handle drawing each row of the buffer of text being
// edited. For now it draws a tilde in each row, which means that row is not
// part of the file and can’t contain any text.
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) { 
        // Step 60
        if (E.numrows == 0 && y == E.screenrows / 3) {
            // Step 41 
            char welcome[80]; 
            int welcomelen = snprintf(welcome, sizeof(welcome), 
                "Kilo editor -- version %s", KILO_VERSION);
            if (welcomelen > E.screencols) welcomelen = E.screencols;
            // Step 42 - centering 
            int padding = (E.screencols - welcomelen) / 2;
            if (padding) { 
                abAppend(ab, "~", 1); 
                padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
        } else {
        abAppend(ab, "~", 1);
        }
    } else {
        int len = E.row[filerow].rsize - E.coloff;
        if (len < 0) len = 0;
        if (len > E.screencols) len = E.screencols; 
        abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }

    // Step 40 one at a time 
    abAppend(ab, "\x1b[K", 3);

    // Step 92 - room for status bar
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines", E.filename ? E.filename : "[No Name]", E.numrows);
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d%d", E.cy + 1, E.numrows);
  if (len > E.screencols) len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) { 
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

// The 4 in our write() call means we are writing 4 bytes out to the terminal.
// The first byte is \x1b, which is the escape character, or 27 in decimal. (Try
// and remember \x1b, we will be using it a lot.) The other three bytes are [2J.
//
// We are writing an escape sequence to the terminal. Escape sequences always
// start with an escape character (27) followed by a [ character. Escape
// sequences instruct the terminal to do various text formatting tasks, such as
// coloring text, moving the cursor around, and clearing parts of the screen.
//
// We are using the J command (Erase In Display) to clear the screen. Escape
// sequence commands take arguments, which come before the command. In this case
// the argument is 2, which says to clear the entire screen. <esc>[1J would
// clear the screen up to where the cursor is, and <esc>[0J would clear the
// screen from the cursor up to the end of the screen. Also, 0 is the default
// argument for J, so just <esc>[J by itself would also clear the screen from
// the cursor to the end.
void editorRefreshScreen() {
  editorScroll();
  
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  // Step 44
  char buf[32];
  // Step 70 - to fix cursor scrolling on the screen
  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, 
                                            (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}


/*** input ***/

void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
    case ARROW_LEFT:
    case 'h':
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
    case 'l':
      // Step 74 - allow user to go pass the right edge of screen
      // and should be able to confirm horizontal scrolling works!
      //if (E.cx != E.screencols - 1)      
      // Step 76
      if (row && E.cx < row->size) {
        E.cx++;
      } else if (row && E.cx == row->size) {
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
    case 'k':
      if (E.cy != 0) E.cy--;
      break;
    case ARROW_DOWN:
    case 'j':
      // Step 69 - advance past bottom of screen but not file
      if (E.cy < E.numrows) E.cy++;
      //if (E.cy != E.screenrows-1) E.cy++;
      // Step 77
      break;
  }

  row = (E.cy >= E.numrows)? NULL : &E.row[E.cy]; 
  int rowlen = row ? row->size : 0; 
  if (E.cx > rowlen) 
    E.cx = rowlen;

}

// editorProcessKeypress() waits for a keypress, and then handles it. Later, it
// will map various Ctrl key combinations and other special keys to different
// editor functions, and insert any alphanumeric and other printable keys’
// characters into the text that is being edited.
void editorProcessKeypress() {
  int c = editorReadKey();

  switch (c) {
  case '\r':
    /* TODO */
    break;

  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;

  case CTRL_KEY('s'):
    editorSave(); 
    break;

  case HOME_KEY:
    E.cx = 0;
    break;

  case END_KEY:
    // Step 91
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    break;
  
  case BACKSPACE: 
  case CTRL_KEY('h'):
  case DEL_KEY: 
    /* TODO */
    break;

  case PAGE_UP:
  case PAGE_DOWN:
    {
      if (c == PAGE_UP) {
        E.cy = E.rowoff;
      } else if (c == PAGE_DOWN) {
        E.cy = E.rowoff + E.screenrows - 1;
        if (E.cy > E.numrows) E.cy = E.numrows;
      }

      int times = E.screenrows;
      while (times--)
        editorMoveCursor(c == PAGE_UP? ARROW_UP : ARROW_DOWN);
    }
    break;

  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
  /*case 'j':
  case 'k':
  case 'l':
  case 'h':*/
    editorMoveCursor(c);
    break;

  case CTRL_KEY('l'):
  case '\x1b': 
    break;

  default: 
    editorInsertChar(c);
    break;
  }

}

/*** init ***/

void initEditor() {
  // Step 43
  E.cx = 0; 
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0; 
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;


  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
  E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2)    
    editorOpen(argv[1]);

  editorSetStatusMessage("HELP: Ctrl-Q = quit");

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}
