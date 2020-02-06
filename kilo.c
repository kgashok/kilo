/*** links ***/

// https://viewsourcecode.org/snaptoken/kilo/02.enteringRawMode.html
// https://viewsourcecode.org/snaptoken/kilo/03.rawInputAndOutput.html

/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

// Step 41
#define KILO_VERSION "0.0.1"

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

/*** data ***/

struct editorConfig {
  int cx, cy;
  int screenrows;
  int screencols;
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
char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  return c;
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

/*** append buffer ***/

// Step 36 
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

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

// editorDrawRows() will handle drawing each row of the buffer of text being
// edited. For now it draws a tilde in each row, which means that row is not
// part of the file and can’t contain any text.
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    if (y == E.screenrows / 3) {
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

    // Step 40 one at a time 
    abAppend(ab, "\x1b[K", 3);

    // Step 35 bug fixed for last line
    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
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
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  // Step 44
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25l", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*** input ***/

// editorProcessKeypress() waits for a keypress, and then handles it. Later, it
// will map various Ctrl key combinations and other special keys to different
// editor functions, and insert any alphanumeric and other printable keys’
// characters into the text that is being edited.
void editorProcessKeypress() {
  char c = editorReadKey();

  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  }
}

/*** init ***/

void initEditor() {
  // Step 43
  E.cx = 0; 
  E.cy = 0; 
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
}

int main() {
  enableRawMode();
  initEditor();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}