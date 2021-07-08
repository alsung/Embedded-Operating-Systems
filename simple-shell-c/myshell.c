#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>

extern const char * const * get_args();

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define BUFFER_SIZE 4096

// input: argument list, first arg is command
// output: void 
// calls execvp() which runs the command (first arg in array)
// if errors, will be written to stderr
void handleChild(char** args) {
	int status = execvp(args[0], args);
	
	if (status == -1) {
		write(STDERR_FILENO, strerror(errno), 50);
		write(STDERR_FILENO, "\n", 2);
		exit(0);
	}
}

// input: NULL
// output: void
// handles parent process after fork(), waits until child process terminates
void handleParent() {
	int waitStatus = 0;
	wait(&waitStatus); // wait for child process to terminate
}

int main()
{
	int         i;
	char **     args;

	pid_t process_id; 	// init to zero
	int in_fd = 0;		// init file descriptor input file
	int out_fd = 0; 	// init file descriptor output file

	// FLAGS
	int in_flag = 0; 	// flag for '<'
	int out_flag = 0; 	// flag for '>'
	int ampersand = 0; 	// flag for '>&'
	int redir_app = 0; 	// flag for '>>'

	while (1) {
		printf ("shell> ");  // prompt
		args = get_args();   // place cmd line args into array
		
		// parse args for special characters, open appropriate fd
		// turn on flags for '>>', '>&'
		for (i = 0; args[i] != NULL; i++) {
			printf ("Argument %d: %s\n", i, args[i]);
			if ( !strcmp(args[i], "<") ) {
				in_flag = 1;
				in_fd = open(args[i+1], O_RDONLY);
				printf("in_fd: %d", in_fd);
				fflush(stdout);
			}
			if ( !strcmp(args[i], ">>") ) {
				printf("here");
				fflush(stdout);
				redir_app = 1;
				out_flag = 1;
				out_fd = open(args[i+1], O_WRONLY | O_APPEND);
				printf("out_fd: %d", out_fd);
				fflush(stdout);		
			}
			else if ( !strcmp(args[i], ">") || !strcmp(args[i], ">&") ) {
				out_flag = 1;
				out_fd = open(args[i+1], O_RDWR | O_CREAT | O_TRUNC);
				printf("out_fd: %d", out_fd);
				fflush(stdout);
			}
			if ( !strcmp(args[i], ">&") ) {
				ampersand = 1;
			}			
		}

		if (args[0] == NULL) {
			printf ("No arguments on line!\n");
		} else if ( !strcmp (args[0], "exit")) {
			printf ("Exiting...\n");
		    	break;
		}
		else {
			// handle cd: change current working directory using chdir()
			if ( !strcmp(args[0], "cd") ) {
				chdir(args[1]);
			}
			else {
				process_id = fork();
				//printf("after fork: %d\n", process_id);
				//fflush(stdout);
				
				if ( process_id == 0 ) {
					// This is the child				

					char quote = '"';
					
					// HANDLE ECHO AND STRING ARG
					// handles the edge case for echo command with a string surrounded 
					// by quotes. We first strip the string of its first and last chars
					// then split the string by ' ', and save the tokens into a new array
					// to be sent to execvp in handleChild 

					if ( !strcmp(args[0], "echo") && (args[1][0] == quote) && 
						(args[1][strlen(args[1])-1] == quote) ) {
						
						args[1]++;
						args[1][strlen(args[1])-1] = 0;

						char delim[] = " ";
						int idx = 1;
						char *ptr = strtok(args[1], delim);
						char *argslist[100];

						for (i = 0; i < 100; i++) {
							argslist[i] = NULL;
						}
						argslist[0] = "echo";					

						while (ptr != NULL) {
							argslist[idx++] = ptr;
							ptr = strtok(NULL, delim);
						}
						
						handleChild(argslist);
					}				

					else {
						// handle input redirection
						if (in_flag) {
							close(in_fd);
							for (i = 0; args[i] != NULL; i++) {
								if ( (!strcmp(args[i], "<")) || (!strcmp(args[i], ">")) || 
									(!strcmp(args[i], ">&")) ) {
									args[i] = args[i+2];
								}
							}
							
						}

						// handle output redirection
						if (out_flag) {
							// handle '>&' case
							// both stdout and stderr are redirected to out file
							if (ampersand) {
								dup2(STDERR_FILENO, STDOUT_FILENO);
							}
							dup2(out_fd, STDOUT_FILENO);
					
							//write(out_fd, "hello", 5);
							
							close(out_fd);

							// remove special characters and file names from arg list
							// build final arg list to be sent to execvp() for execution
							for (i = 0; args[i] != NULL; i++) {
								if ( (!strcmp(args[i], ">")) || (!strcmp(args[i], "<")) || 
									(!strcmp(args[i], ">&")) || (!strcmp(args[i], ">>")) ) {
									args[i] = args[i+2];
								}
							}
						}
						
						// printf("\nfinal arglist\n");
						// fflush(stdout);
						// for (i = 0; args[i] != NULL; i++) {
						// 	printf ("Argument %d: %s\n", i, args[i]);
						// 	fflush(stdout);
						// }

						handleChild(args);
					}

					// Pipe? 

					// Multiple commands? More Forks! dup()			
				}

				if ( process_id > 0) {
					// This is the parent
					
					handleParent();
					
					// prompt
					// handle graceful exit
					
				}
				in_flag = 0;
				out_flag = 0;
				ampersand = 0;
			}
		}
	}
}
