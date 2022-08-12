#include "user_lib.h"
#include "util/types.h"
#include "util/string.h"

int main(int argc, char *argv[]){
  printu("===== createproc =====\n");
  long nproc = 0;
  if ( argc <= 1 )
    nproc = 2;
  else
    nproc = atol(argv[1]);  // new process number
  printu("Will create %d process(s)...\n", nproc);

  int rounds = 50000000;
  for ( int i = 0; i < nproc; ++ i ){
    int rc = fork();    
    if ( rc == 0 ){ // child
      for ( int i = 0; i < rounds; ++ i );
      exit(0);
    }
  }
  exit(0);
  return 0;
}
