/*
 * The supporting functions for shell. 
 * 
 */

#include "user_lib.h"
#include "util/types.h"
#include "util/string.h"
#include "kernel/syscall.h"

#define MAXLEN 256
#define MAXARGS 10

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};


void panic(char *);
struct cmd * parsecmd(char *);

//
// Fork a child process to run the input command.
// 
void runcmd(struct cmd * cmd){
  struct execcmd * ecmd;

  if ( cmd == NULL )
    exit(0);

  switch (cmd->type){
  case EXEC:
    ecmd = (struct execcmd *)cmd;
    if ( ecmd->argv[0] == 0 )
      exit(0);
    int i = 0;
    while ( ecmd->argv[i] ){
      printu("argv[%d]=%s\n",i, ecmd->argv[i]);
      ++ i;
    }
    exec(ecmd->argv[0], ecmd->argv);
    printu("exec %s failed\n", ecmd->argv[0]);
    break;

  default:
    panic("Unknown command!\n");
    break;
  }
  exit(0);
}

void panic(char * s){
  printu("%s\n", s);
  exit(-1);
}

//
// Initialize a command
// 
struct cmd * execcmd(){
  struct execcmd * cmd;

  cmd = naive_malloc();
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd *)cmd;
}

int gettoken(char ** ps, char * es, char ** q, char ** eq){
  char whitespace[6] = " \t\r\n\v";
  char symbols[8] = "<|>&;()";
  char * s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if(*s == '>'){
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;
  
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int peek(char ** ps, char * es, char * toks){
  char whitespace[6] = " \t\r\n\v";
  char symbols[8] = "<|>&;()";
  char * s;

  s = *ps;
  while ( s < es && strchr(whitespace, *s) )
    ++ s;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd * parseline(char **, char *);
struct cmd * parsepipe(char **, char *);
struct cmd * parseexec(char **, char *);
struct cmd * nulterminate(struct cmd *);

struct cmd * parsecmd(char * s){
  char * es;
  struct cmd * cmd;
  
  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if ( s != es ){
    printu("leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  // cmd done
  return cmd;
}

struct cmd * parseline(char ** ps, char * es){
  struct cmd * cmd;
  cmd = parsepipe(ps, es);
  // parse line
  return cmd;
}

struct cmd * parsepipe(char ** ps, char * es){
  struct cmd * cmd;
  cmd = parseexec(ps, es);
  // pipe
  return cmd;
}

struct cmd * parseexec(char ** ps, char * es){
  char * q, * eq;
  int tok, argc;
  struct execcmd * cmd;
  struct cmd * ret;

  ret = execcmd();
  cmd = (struct execcmd *)ret;

  argc = 0;
  while ( ! peek(ps, es, "|)&;") ){
    if ( (tok = gettoken(ps, es, &q, &eq)) == 0 )
      break;
    if ( tok != 'a' )
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc++] = eq;
    if ( argc >= MAXARGS )
      panic("too many args");
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

//
// NUL-terminate all the counted strings.
//
struct cmd*
nulterminate(struct cmd *cmd)
{
  struct execcmd *ecmd;

  if(cmd == 0)
    return 0;
  
  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for ( int i = 0; ecmd->argv[i]; ++ i )
      *ecmd->eargv[i] = 0;
    break;
  }
  return cmd;
}

int isExit(char * s){
  char whitespace[6] = " \t\r\n\v";
  char * es = s + strlen(s);
  while ( s < es && strchr(whitespace, *s) )
    s++;
  if ( s[0]=='e' && s[1]=='x' && s[2]=='i' && s[3]=='t' )
    return 1;
  return 0;
}

void mysh(){
  char buf[MAXLEN+1];

  while( 1 ){
    printu("mysh$ ");
    getlineu(buf, MAXLEN);

    if ( isExit(buf) == 1 ) // exit
      break;

    int rc = fork();
    if ( rc == 0 ){ // child
      runcmd(parsecmd(buf));
    }else{
      wait(-1);
    }
  }
  return;
}