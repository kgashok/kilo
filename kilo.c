//https://viewsourcecode.org/snaptoken/kilo/02.enteringRawMode.html

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h> 
#include <unistd.h>

struct termios orig_termios;

void disableRawMode() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
  printf("Quitting after disabling raw mode!\n");

}

void enableRawMode() { 
  tcgetattr(STDIN_FILENO, &orig_termios);

  //We use it to register our disableRawMode() function to be called automatically when the program exits, whether it exits by returning from main(), or by calling the exit() function 
  atexit(disableRawMode);

  struct termios raw = orig_termios;
  //ECHO is a bitflag, defined as 00000000000000000000000000001000 in binary. We use the bitwise-NOT operator (~) on this value to get 11111111111111111111111111110111. We then bitwise-AND this value with the flags field, which forces the fourth bit in the flags field to become 0, and causes every other bit to retain its current value. Flipping bits like this is common in C.
  // Step 7
  //There is an ICANON flag that allows us to turn off canonical mode. This means we will finally be reading input byte-by-byte, instead of line-by-line.
  // Step 9
  //Disable Ctrl-C and Ctrl-Z using ISIG
  raw.c_lflag &= ~(ECHO | ICANON | ISIG);

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); 

}

int main() { 
  enableRawMode(); 

  char c; 
  while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
    // is a control character?
    if (iscntrl(c)) {
      printf("%d\n", c);
    } else {
      printf("%d ('%c')\n", c, c);
    }
  } 
  return 0;
}