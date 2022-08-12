/*
 * The application of lab3_challenge_shell.
 * mysh() keeps running until user types "exit". 
 */

#include "user_lib.h"
#include "util/types.h"
#include "sh.h"

int main(int argc, char *argv[]) {
  // mysh();

  int fd;
  // int n;
  // int MAXBUF = 512;
  // char buf[MAXBUF];

  // printu("filename: %s\n", argv[i]);
  if ( (fd = open("1.txt", 0)) < 0 ){
    // printu("cat: cannot open file %s\n", argv[i]);
    // exit(0);
  }

  fd = open("ramdisk0:/ramfile", 0);



  printu("open success\n");

  exit(0);
  return 0;
}
