//https://viewsourcecode.org/snaptoken/kilo/02.enteringRawMode.html

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h> 
#include <unistd.h>

struct termios orig_termios;

void die(const char *s) {
  perror(s);
  exit(1);
}
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
  // Step 10
  //Disable Ctrl-S and Ctrl-Q
  //They are read as 19 and 17 respectively
  // Step 11 
  //Disable Ctrl-V and Ctrl-O
  // Step 12
  //Disable Ctrl-M and Ctrl-J
  //They are read as 13 and 10 respectively
  // Step 15 - turn off more flags
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  // Step 15 
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  // Step 13 
  // turnoff output processing
  // turnoff POST processing of outputs
  //From now on, we’ll have to write out the full "\r\n" whenever we want to start a new line.
  raw.c_oflag = ~(OPOST);
  // Step 16 
  raw.c_cc[VMIN] = 0; 
  raw.c_cc[VTIME] = 1;

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); 
}

int main() { 
  enableRawMode(); 

  while (1) { 
    char c = '\0'; 
    read(STDIN_FILENO, &c, 1);
    // is a control character?
    if (iscntrl(c)) {
      // because OPOST has been enabled
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\n", c, c);
    }
    if (c == 'q') break;
  } 
  return 0;
}