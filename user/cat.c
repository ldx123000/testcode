#include "user_lib.h"
#include "util/types.h"

int main(int argc, char *argv[]){
  printu("===== cat =====\n");
  if ( argc <= 1 ){
    printu("Too few arguments. \n");
    exit(0);
  }

  int fd, n;
  int MAXBUF = 512;
  char buf[MAXBUF];
  for ( int i = 1; i < argc; ++ i ){
    if ( (fd = open(argv[i], 0)) < 0 ){
      printu("cat: cannot open file %s\n", argv[i]);
      exit(0);
    }
    // cat file
    n = read(fd, buf, MAXBUF);
    write(1, buf, n);
    close(fd);
  }
  exit(0);
  return 0;
}
