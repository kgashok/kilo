/*** links ***/

// https://viewsourcecode.org/snaptoken/kilo/02.enteringRawMode.html
// https://viewsourcecode.org/snaptoken/kilo/03.rawInputAndOutput.html

/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

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

struct termios orig_termios;

/*** terminal ***/

void die(const char *s) {
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    die("tcsetattr");
  printf("Quitting after disabling raw mode!\n");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
    die("tcgetattr");

  // We use it to register our disableRawMode() function to be called
  // automatically when the program exits, whether it exits by returning from
  // main(), or by calling the exit() function
  atexit(disableRawMode);

  struct termios raw = orig_termios;
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

// editorReadKey()’s job is to wait for one keypress, and return it. Later, we’ll expand this function to handle escape sequences, which involves reading multiple bytes that represent a single keypress, as is the case with the arrow keys.
char editorReadKey() { 
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  return c;
}

/*** output ***/

// The 4 in our write() call means we are writing 4 bytes out to the terminal. The first byte is \x1b, which is the escape character, or 27 in decimal. (Try and remember \x1b, we will be using it a lot.) The other three bytes are [2J.
//
// We are writing an escape sequence to the terminal. Escape sequences always start with an escape character (27) followed by a [ character. Escape sequences instruct the terminal to do various text formatting tasks, such as coloring text, moving the cursor around, and clearing parts of the screen.
//
// We are using the J command (Erase In Display) to clear the screen. Escape sequence commands take arguments, which come before the command. In this case the argument is 2, which says to clear the entire screen. <esc>[1J would clear the screen up to where the cursor is, and <esc>[0J would clear the screen from the cursor up to the end of the screen. Also, 0 is the default argument for J, so just <esc>[J by itself would also clear the screen from the cursor to the end.
void editorRefreshScreen() { 
  write(STDOUT_FILENO, "\x1b[2J", 4);
}


/*** input ***/

// editorProcessKeypress() waits for a keypress, and then handles it. Later, it will map various Ctrl key combinations and other special keys to different editor functions, and insert any alphanumeric and other printable keys’ characters into the text that is being edited.
void editorProcessKeypress() { 
  char c = editorReadKey();

  switch(c) {
    case CTRL_KEY('q'):
      exit(0);
      break;
  }
}
/*** init ***/

int main() {
  enableRawMode();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}