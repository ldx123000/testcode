#include "user_lib.h"
#include "util/types.h"

int main(int argc, char *argv[]){
  printu("===== echo =====\n");
  for ( int i = 1; i < argc; ++ i ){
    printu("%s%s", argv[i], ( i == argc - 1 ? "\n" : " "));
  }
  exit(0);
  return 0;
}
