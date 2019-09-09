// NAME: Andrew Adelman
// EMAIL: amadelman@g.ucla.edu
// ID: 105188652

#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>

void exit_procedure();

// Globals 
pid_t pid;
struct termios default_settings;
int to_shell[2], from_shell[2];

int exit_status = 0; 

void handler() {
  close(to_shell[1]); 
    
  char buf[1024];
  int buf_size;
  
  buf_size = read(from_shell[0], buf, 1024*sizeof(char));

  while(buf_size) { 
    int i;
    char c;
    for(i = 0; i < buf_size; i++) {
      c = buf[i];
      
      switch(c) {
	
      case '\n':
	write(1, "\r\n", 2); // replace literals?
	continue;
      default:
	write(1, &c, 1);
      }
    }
    buf_size = read(from_shell[0], buf, 1024*sizeof(char));
  }
  if(buf_size == 0) {
    close(from_shell[0]);
    exit(0);
  }
} 

void terminal_mode() {
  tcgetattr(0, &default_settings);

  struct termios new_settings;
  tcgetattr(0, &new_settings);

  new_settings.c_iflag = ISTRIP;
  new_settings.c_oflag = 0;
  new_settings.c_lflag = 0;

  if(tcsetattr(0, TCSANOW, &new_settings) == -1) {
    fprintf(stderr, "termios error: %s\n", strerror(errno));
    exit_status = 1;
    exit(1); 
  }  
  atexit(exit_procedure);
} 

void exit_procedure() {
  tcsetattr(0, TCSANOW, &default_settings); // restore input

  int status;
  if (waitpid(pid, &status, 0) == -1) {
    fprintf(stderr, "waitpid error: %s\n", strerror(errno));
    exit_status = 1;
    exit(1); 
  }
  if(WIFEXITED(status)) { // if successful, 
    fprintf(stderr, "SHELL EXIT SIGNAL=%d, STATUS=%d\n",
	    WIFSIGNALED(status), WEXITSTATUS(status));
  }
  exit(exit_status); 
} 
    
void parent_code() {
  close(to_shell[0]); // parent won't be reading from to_shell
  close(from_shell[1]); // parent won't be writing to from_shell

  signal(SIGPIPE, handler);

  struct pollfd pollfds[2];

  pollfds[0].fd = 0;
  pollfds[1].fd = from_shell[0];

  pollfds[0].events = POLLIN + POLLHUP + POLLERR;
  pollfds[1].events = POLLIN + POLLHUP + POLLERR;

  char buf[1024];
  int buf_size;

  int ret;
  
  while(1) {

    ret = poll(pollfds, 2, -1);
    
    if(ret < 0) {
      fprintf(stderr, "poll error: %s\n", strerror(errno));
      exit_status = 1;
      exit(1); 
    }

    if(pollfds[0].revents & POLLIN) {
      buf_size = read(0, buf, 1024*sizeof(char));
      int i;
      char c;
      for(i = 0; i < buf_size; i++) {
	c = buf[i];

	switch(c) {
	  
	case '\4':
	  close(to_shell[1]);
	  break;

	case '\3':
	  if(kill(pid, SIGINT) == -1) {
	    fprintf(stderr, "kill error: %s\n", strerror(errno));
	    exit_status = 1;
	    exit(1); 
	  } 
	  break; 

	case '\r':
	case '\n':
	  write(1, "\r\n", 2);
	  write(to_shell[1], "\n", 1);
	  continue; 

	default:
	  write(1, &c, 1);
	  write(to_shell[1], &c, 1);
	}
      }
      if(buf_size == 0) {
	close(to_shell[1]);
      } 	      
    }
    
    if(pollfds[1].revents & POLLIN) {      
      buf_size = read(from_shell[0], buf, 1024*sizeof(char));
      int i;
      char c;
      for(i = 0; i < buf_size; i++) {
        c = buf[i];

        switch(c) {

        case '\n':
          write(1, "\r\n", 2); 
          continue;
        default:
          write(1, &c, 1);
        }
      }
      if(buf_size == 0) {
	close(from_shell[0]);
      } 
    }
    
    if(pollfds[0].revents & (POLLHUP | POLLERR) ) {
      close(to_shell[1]); 
      buf_size = read(from_shell[0], buf, 1024*sizeof(char));
      
      while(buf_size) { 
	int i;
	char c;
	for(i = 0; i < buf_size; i++) {
	  c = buf[i];
	  
	  switch(c) {
	    
	  case '\n':
	    write(1, "\r\n", 2); 
	    continue;
	  default:
	    write(1, &c, 1);
	  }
	}
	buf_size = read(from_shell[0], buf, 1024*sizeof(char));
      } // while 
      close(from_shell[0]);
      exit(0); 
    } 
      
    if(pollfds[1].revents & (POLLHUP | POLLERR) ) {
      // confirmed okay by Alex 
      close(to_shell[1]); 
      exit(0);
    }
  } // while 
}


int main(int argc, char* argv[]) {

  // PROCESS ARGUMENTS
  char* prog = "";
  int shell_opt = 0;
  static struct option long_options[] =
    {
     {"shell",    required_argument, 0, 's'},
     {0,          0,                 0,   0}
    };

  int i; // getopt_long return value
  while((i = getopt_long(argc, argv, "s:", long_options, NULL)) != -1) {

    switch (i) {

    case 's':
      shell_opt = 1;
      prog = optarg;
      break;

    case '?':
      fprintf(stderr, "error: argument %s: incorrect option argument.\n",
	      argv[optind-1]);
      exit_status = 1;
      exit(1); 

    default:
      fprintf(stderr, "error: argument %s: unrecognized argument.\n",
	      argv[optind-1]);
      exit_status = 1;
      exit(1); 
    }
  } 

  terminal_mode(); // automatically restores default terminal settings

  if(shell_opt) {

    // create pipes 
    if(pipe(to_shell) == -1) {
      fprintf(stderr, "pipe error: %s\n", strerror(errno));
      exit_status = 1;
      exit(1); 
    }
    if(pipe(from_shell) == -1) {
      fprintf(stderr, "pipe error: %s\n", strerror(errno));
      exit_status = 1;
      exit(1); 
    } 

    pid = fork();
    
    if(pid == -1) {
      fprintf(stderr, "fork error: %s\n", strerror(errno));
      exit_status = 1;
      exit(1); 
    }
    
    if (pid == 0) {
      // Initialize input pipe to child's stdin
      close(to_shell[1]); // child won't be writing to to_shell
      close(0); // close stdin
      dup(to_shell[0]); // dup to_shell[0] to fd 0
      close(to_shell[0]); // close duplicate fd
    
      // Initialize output pipe from child's stdout/stderr
      close(from_shell[0]);
      close(1); // close stdout
      close(2); // close stderr
      dup(from_shell[1]); // dup from_shell[1] to fd 1
      dup(from_shell[1]);  // dup from_shell[1] to fd 2
      close(from_shell[1]); // close duplicate fd
      
      if(execl(prog, prog, (char*) NULL) == -1) { 
	fprintf(stderr, "exec error: %s\n", strerror(errno));
	exit_status = 1; 
	exit(1);
      }          
    } else {
      parent_code();
    }
  } else { // no shell 
    
    char buf[1024];
    int buf_size;
    
    buf_size = read(0, buf, 1024*sizeof(char));
    int i;
    char c;
    while(buf_size){ 
      for(i = 0; i < buf_size; i++) {
	c = buf[i];
	
	switch(c) {
	  
	case '\4':
	  break;
	  
	case '\r':
	case '\n':
	  write(1, "\r\n", 2);
	  continue; 

	default:
	  write(1, &c, 1);
	} // switch
      } // for
      if(c == '\4') {
	exit(0); 
      }
      buf_size = read(0, buf, 1024*sizeof(char));
    } // while
  } // else 
  return 0;
} 
