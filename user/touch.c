#include "user_lib.h"
#include "util/types.h"

int main(int argc, char *argv[]){
  printu("===== touch =====\n");

  if ( argc <= 1 ){
    printu("Too few arguments. \n");
    exit(0);
  }

  // int fd;
  // for ( int i = 1; i < argc; ++ i ){
  //   printu("filename: %s\n", argv[i]);
  //   if ( (fd = create(argv[i])) < 0 ){
  //     printu("touch: cannot create file %s\n", argv[i]);
  //     exit(0);
  //   }
  //   close(fd);
  // }
  exit(0);
  return 0;
}