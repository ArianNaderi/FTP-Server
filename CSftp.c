#define _GNU_SOURCE
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include "dir.h"
#include "usage.h"
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include "Thread.h"
#include <ctype.h>
#define BSIZE 1024
#define BACKLOG 10
int threads = 0;
#define maxThreads 4

//commandList
typedef enum commandList {
  USER,
  QUIT,
  TYPE,
  MODE,
  STRU,
  RETR,
  PASV,
  NLST
} commandList;
//commandListString
static const char *commandListString[] = {
  "USER",
  "QUIT",
  "TYPE",
  "MODE",
  "STRU",
  "RETR",
  "PASV",
  "NLST"
};
//Port
typedef struct Port {
  int p1;
  int p2;
} Port;
//Mode        
typedef enum Mode {
  NORMAL,
  PASSIVE
} Mode;
//Command
typedef struct Command {
  char command[6];
  char arg[BSIZE];
} Command;
//ServerState
typedef struct ServerState {
  bool logged_in;
  int connection;
  char *message;
  int sock_pasv;
  Mode mode;
} ServerState;

//lookup matching char
int lookup(char *x,const char **list,int num) {
  int i;
  for(i=0; i<num; i++) {
    if (strcmp(x,list[i])==0) {
      return i;
    }
  }
  return -1;
}

// Make Port
void makePort(Port *port) {
  srand(time(NULL));
  port->p1=128+(rand()%64);
  port->p2=rand()%0xff;
}

// Gets socket ip
void getSocketIP(int sock, int *ip) {
  socklen_t addr_size = sizeof(struct sockaddr_in);
  struct sockaddr_in addr;
  getsockname(sock, (struct sockaddr *) &addr, &addr_size);
  char* host = inet_ntoa(addr.sin_addr);
  sscanf(host, "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]);
}

// Make Socket with given port char
int makeSocket(char* port) {
  int sc;
  int ad;
  struct addrinfo hints, *result, *rp;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_protocol = 0;
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;
  ad = getaddrinfo(NULL,port,&hints,&result);
  if (ad!=0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ad));
    exit(EXIT_FAILURE);
  }
  for (rp = result; rp != NULL; rp=rp->ai_next){
    sc = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    // Unsuccessful bind case
    if (sc==-1){
      continue;
    }
    if (bind(sc,rp->ai_addr,rp->ai_addrlen) == 0){
      break;
    }
    close(sc);
  }
  freeaddrinfo(result);
  if (rp == NULL) {
    fprintf(stderr, "Failed to bind\n");
    exit(EXIT_FAILURE);
  }
  if (listen(sc, BACKLOG) == -1){
    fprintf(stderr, "Failed to listen\n");
    exit(EXIT_FAILURE);
  }
  return(sc);
}

// Respond to Client!
void respondToClient(ServerState *ss){
  write(ss->connection, ss->message, strlen(ss->message));
}

// Parse CMD!
void parseCommand(char *buff, Command *cmd){
  sscanf(buff, "%s %s", cmd->command, cmd->arg);
}

//USER:
void cmdUser(Command *cmd, ServerState *ss){
  if (strcmp(cmd->arg, "cs317")==0){
    ss->logged_in=true;
    ss->message = "230 Login Was Succesfull!!\n";
  }
  else{
    ss->message = "500 Username Is Invalid\n";
  }
  respondToClient(ss);
}

//QUIT:
void cmdQuit(Command *cmd, ServerState *ss) {
  ss->message = "221 Bye.\n";
  respondToClient(ss);
  close(ss->connection);
  ss->connection = -1;
  threads--;
  cancelThread((void *) (size_t) pthread_self());
}

//TYPE:
void cmdType(Command *cmd, ServerState *ss){
  if (ss->logged_in){
    if (strcmp(cmd->arg, "A") == 0){
      ss->message = "200 Using ASCII Mode To Transfer Files\n";
    }
    else if (strcmp(cmd->arg, "I") == 0){
      ss->message = "200 Using Binary Mode To Transfer Files\n";
    }
    else{
      ss->message = "504 Mode Unknown\n";
    }
  }
  else{
    ss->message = "530 Login With USER\n";
  }
  respondToClient(ss);
}

//MODE:
void cmdMode(Command *cmd, ServerState *ss){
  if (ss->logged_in){
    ss->message = "504 Only Stream Mode Supported\n";
  }
  else{
    ss->message = "530 Login With USER\n";
  }
  respondToClient(ss);
}

//STRU:
void cmdStru(Command *cmd, ServerState *ss){
  if (ss->logged_in){
    ss->message = "504 Only File Structure Type Supported.\n";
  }
  else{
    ss->message = "530 Please login with USER.\n";
  }
  respondToClient(ss);
}

//RETR:
void cmdRetr(Command *cmd, ServerState *ss){
  if (ss->logged_in && ss->mode == PASSIVE){
    FILE *file;
    file = fopen(cmd->arg, "r");
    char line[150];
    if (file == NULL){
      ss->message = "550 Failed To Open The File.\n";
    }
    else{
      char buff[255];
      sprintf(buff, "150 Opening ASCII mode data connection for %s.\n", cmd->arg);
      ss->message = buff;
      respondToClient(ss);
      struct sockaddr_in client_addr;
      socklen_t client_size = sizeof(client_addr);
      int pasv_connection = accept(ss->sock_pasv, (struct sockaddr*) &client_addr, &client_size);
      while(fgets(line, 150, file) != NULL) {
        dprintf(pasv_connection, "%s", line);
      }
      fclose(file);
      close(pasv_connection);
      close(ss->sock_pasv);
      ss->message = "226 Transfer complete.\n";
    }
  }
  else{
    ss->message = "530 Login With USER\n";
  }
  respondToClient(ss);
}

//PASV:
void cmdPasv(Command *cmd, ServerState *ss){
  if (ss->logged_in){
    int ip[4];
    char buff[255];
    char *response = "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d).\n";
    Port *p = malloc(sizeof(Port));
    makePort(p);
    getSocketIP(ss->connection, ip);
    close(ss->sock_pasv);
    char *pStr;
    asprintf(&pStr, "%d", (256 * p->p1) + p->p2);
    ss->sock_pasv = makeSocket(pStr);
    sprintf(buff, response, ip[0], ip[1], ip[2], ip[3], p->p1, p->p2);
    ss->message = buff;
    ss->mode = PASSIVE;
  }
  else{
    ss->message = "530 Login With USER.\n";
  }
  respondToClient(ss);
}

//NLST:
void cmdNlst(Command *cmd, ServerState *state){
  if (state->logged_in && state->mode == PASSIVE){
    if (cmd->arg[0] == '\0') {
      state->message = "150 Here's The Directory Listing\n";
      respondToClient(state);
      struct sockaddr_in client_addr;
      socklen_t client_size = sizeof(client_addr);
      int pasv_connection = accept(state->sock_pasv, (struct sockaddr*) &client_addr, &client_size);
      listFiles(pasv_connection, ".");
      close(pasv_connection);
      close(state->sock_pasv);
      state->message ="226 Directory\n";
    }
    else{
      state->message ="501 No Parameters Required\n";
    }
  }
  else if (!state->logged_in){
    state->message ="530 Please Login with USER\n";
  }
  else{
    state->message ="425 Not In PASSIVE Mode\n";
  }
  respondToClient(state);
}

//Executes the commands above
void runCommand(Command *cmd, ServerState *ss){
  char c[sizeof(cmd->command)];
  strcpy(c, cmd->command);
  char *d;
  for (d = c;*d != '\0';++d){
    *d = toupper(*d);
  }
  switch(lookup(c,commandListString,sizeof(commandListString)/sizeof(char *))) {
    case USER:
      cmdUser(cmd,ss);
      break;
    case QUIT:
      cmdQuit(cmd,ss);
      break;
    case TYPE:
      cmdType(cmd,ss);
      break;
    case MODE:
      cmdMode(cmd,ss);
      break;
    case STRU:
      cmdStru(cmd,ss);
      break;
    case RETR:
      cmdRetr(cmd,ss);
      break;
    case PASV:
      cmdPasv(cmd,ss);
      break;
    case NLST:
      cmdNlst(cmd,ss);
      break;
    default:
      ss->message = "500 Unknown Command\n";
      respondToClient(ss);
      break;
  }
}
//Handles incoming CMDs
void *Handler(void *h){
  unsigned int i;
  i = (uintptr_t) h;
   // 'Hello and Welcome'
  char welcome_msg[BSIZE] = "220 Hello and Welcome.\n";
  write(i, welcome_msg, strlen(welcome_msg));
  char buffer[BSIZE];
  memset(buffer,0,BSIZE);
  Command *cmd = malloc(sizeof(Command));
  ServerState *state = malloc(sizeof(ServerState));
  state->logged_in =false;
  state->connection =i;
  state->mode =NORMAL;
  int input;
  while ((input = read(i, buffer, BSIZE)) && state->connection !=-1){
    if (!(input > BSIZE)) {
      parseCommand(buffer, cmd);
      runCommand(cmd, state);
      memset(buffer,0,BSIZE);
      memset(cmd,0,sizeof(Command));
    }
  }
  return 0;
}

int main(int argc, char **argv){
    if (argc != 2){
      usage(argv[0]);
      return -1;
    }
    int connection;
    int socket = makeSocket(argv[1]);
    struct sockaddr_in client_addr;
    socklen_t client_size = sizeof(client_addr);
    while(1){
      if (threads<maxThreads){
        printf("Waiting For Connections....\n");
        connection = accept(socket,(struct sockaddr *) &client_addr,&client_size);
        if (connection==-1){            
          printf("Failed To Connect\n");
          continue;
        }
        runThread(createThread(Handler,(void *) (size_t) connection),NULL);
        threads++;}
      else{
        connection=accept(socket, (struct sockaddr *) &client_addr, &client_size);
        char message[BSIZE] = "10068 Too many users!\n";
        write(connection,message,strlen(message));
        //Close The Connection
        close(connection);
      }
    }
    return 0;
}