#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#define MAXINPUT 2048
volatile sig_atomic_t foregroundOnly = 0; 

// catchSIGSTP and catchSIGINT modeled after functions in lecture 3.3
void catchSIGSTP(int signo) {
	if (foregroundOnly == 0) {
		char * message = "\nEntering foreground-only mode (& is now ignored)\n:"; 
		write(STDOUT_FILENO, message, 51); 
		foregroundOnly = 1;
	} 
	else {
		char * message = "\nExiting foreground-only mode\n:"; 
		write(STDOUT_FILENO, message, 31);  
		foregroundOnly = 0; 
	}
}
void catchSIGINT(int signo) {
	char * message = "terminated by signal 2\n";
	write(STDOUT_FILENO, message, 24);
	exit(2); 
}

void main() { 

	// sigaction code from lecture 3.3
	struct sigaction SIGINT_action = {0}; 
	SIGINT_action.sa_handler = SIG_IGN;
	sigfillset(&SIGINT_action.sa_mask); 
	SIGINT_action.sa_flags = 0; 
	sigaction(SIGINT, &SIGINT_action, NULL); 

	struct sigaction SIGSTP_action = {0};
	SIGSTP_action.sa_handler = catchSIGSTP; 
	sigfillset(&SIGSTP_action.sa_mask); 
	SIGSTP_action.sa_flags = SA_RESTART; 
	sigaction(SIGTSTP, &SIGSTP_action, NULL);  
	int run = 1; 
	
	int exitStatus; 
	char * array[512];
	int i; 

	// loop until user exits
	while (run == 1) {
		int i; 
		int background = 0; 
		int inputCount = 0; 
		int buffSize = 2056; 
		int redirectIn = 0; 
		int redirectOut = 0; 
		char * inputFile; 
		char * outputFile; 
		
		char input[MAXINPUT];
		char buffer[buffSize];  

		// clear array
		for (i = 0; i < 512; i++) {
			array[i] = NULL; 
		}

		// print prompt
		printf(":"); 
		fflush(stdout);

		// get user input 
		fgets(input, MAXINPUT, stdin); 

		// get pid, convert to string
		// faw.cprogramming.com/cgi-bin/smartfaq.cgi?id=1043284385&answer=1043808026
		int pidInt = getpid(); 
		int length = snprintf(NULL, 0, "%d", pidInt); 
		char pid[length]; 
		snprintf(pid, length + 1, "%d", pidInt); 
		char *p = input; 

		// replace all instances of $$ with string version of pid
		// code from stackoverflow.com/questions/32413667/replace-all-occurrences-of-a-substring-in-a-string-in-c/32413923
		while ((p=strstr(p, "$$"))){
			strncpy(buffer, input, p-input); 
			buffer[p-input] = '\0';
			strcat(buffer, pid); 
			strcat(buffer, p+2);
			strcpy(input, buffer); 
			p++;   
		}		
	
		// change the newline at the end of the user input. This allows the strtok to work
		// later in the code
		for (i=0; i<MAXINPUT; i++) {
			if (input[i] == '\n') {
				input[i] = '\0'; 
			}
		}

		// strtok the user input, looking for special characters and putting anything else 
		// into an array of input words
		char * currentWord = strtok(input, " "); 
		while (currentWord != NULL) {
	
			// if "<" is found in input, the next word is the name of the input file
			if (strcmp(currentWord, "<") == 0) {
				currentWord = strtok(NULL, " "); 
				inputFile = strdup(currentWord);  
				currentWord = strtok(NULL, " "); 
				redirectIn = 1; 
			}
	
			// if ">" is found in input, the next word is the name of the output file
			else if (strcmp(currentWord, ">") == 0) {
		 		currentWord = strtok(NULL, " "); 
				outputFile = strdup(currentWord); 
				currentWord = strtok(NULL, " "); 
				redirectOut = 1; 
			}
	
			// indicates background command
			else if (strcmp(currentWord, "&") == 0) {
				background = 1; 
				currentWord = strtok(NULL, " ");  
			}
			
			// place the word in an array for execution
			else {
				array[inputCount] = strdup(currentWord); 
				inputCount++; 
				currentWord = strtok(NULL, " "); 
			}	
		}

		pid_t spawnPid = -5; 

		// if there is no user input, continue to the next prompt
		array[inputCount] = NULL; 
		if (inputCount == 0) {
			continue; 
		}

		// change directory
		// geeksforgeeks.org/chdir-in-c-language-with-examples/
		else if (strcmp(array[0], "cd") == 0) {
			if (array[1] != NULL){
				if (chdir(array[1]) == -1) {
					printf("not a valid directory name\n"); 
					fflush(stdout); 
				}
			}
			else { 
				chdir(getenv("HOME"));   
			}
		}

		// check exit status - code from lecture 3.1
		else if (strcmp(array[0], "status") == 0) {
			if (WIFEXITED(exitStatus)) {
				printf("exit value %d\n", WEXITSTATUS(exitStatus)); 		
				fflush(stdout); 

			}
			else {
				printf("terminated by signal %d\n", WTERMSIG(exitStatus));
				fflush(stdout);  
			}

		}

		// exit the loop to exit the shell
		else if (strcmp(array[0], "exit")== 0) {
			run = 0; 			
		}

		// ignore the comment line in the same manner as the empty input set
		// stackoverflow.com/questions/46080417/checking-the-first-letter-
		// of-a-string-in-c
		else if(0 == memcmp(input, "#", 1)) {
			continue; 
		} 

		// execute a command
		else {
			// fork off a child process 
			spawnPid = fork(); 

			int sourceFD, targetFD, result; 

			switch(spawnPid) {
				case -1: {perror("Hull Breach!\n"); exit(1); break; }
				
				// instructions for the parent process
				case 0:{
						// file redirection from lecture 3.4
						// if input redirection was indicated by the user input, redirect to the named file
					       if (redirectIn == 1) {
						       sourceFD = open(inputFile, O_RDONLY); 
						       if (sourceFD == -1) {printf("cannot open %s for input\n", inputFile); exit (1); } 
						       result = dup2(sourceFD, 0); 
						       if (result == -1) {perror("target dup2()"); exit(2); }
						       fcntl(sourceFD, F_SETFD, FD_CLOEXEC); 
					       }
						// if output redirection was indicated by the user input, redirect to the named file
					       if (redirectOut == 1) {
						       targetFD = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
						       if (targetFD == -1) {printf("cannot open %s for output\n", outputFile); exit(1); }
						       result = dup2(targetFD, 1); 
						       if (result == -1) {perror("target dup2()"); exit(2); }
						       fcntl(targetFD, F_SETFD, FD_CLOEXEC); 
					       }

						// resetting sigint behavior - stackoverflow.com/questions/55430508/
						// how-to-change-signal-handler-in-child-process
						SIGINT_action.sa_handler = catchSIGINT; 
						sigaction(SIGINT, &SIGINT_action, NULL); 
						
						// execute the command using the array of user input
					       exitStatus = execvp(array[0], array);

						// if it failed to execute, print error message and exit process
					       if (exitStatus == -1) {
						       printf("%s: no such file or directory\n", array[0]); 
						       fflush(stdout); 
						       exit(1); 
					       }
				       }
				
				// instructions for the child process
				default: {
						 if (background == 1 && foregroundOnly == 0) {
							 pid_t childPid = waitpid(spawnPid, &exitStatus, WNOHANG); 
							 printf("background pid is %d\n", spawnPid); 
							 fflush(stdout); 
						 }
			
						// backgdround commands not allowed
						 else {
							 pid_t childPid = waitpid(spawnPid, &exitStatus, 0); 
						 }
					 }	
			}
		}
	
		// the -1 value of pid waits for any child process to complete
		// linux.die.net/man/2/waitpid
		// tutorialspoint.com/unix_system_calls/waitpid.htm
		while ((spawnPid = waitpid(-1, &exitStatus, WNOHANG)) > 0) {
			if (WIFEXITED(exitStatus)) {
				printf("background pid %d is done: exit value %d\n", spawnPid, WEXITSTATUS(exitStatus)); 
			}
			else {
				printf("background pid %d is done: terminated by signal %d\n", spawnPid, WTERMSIG(exitStatus)); 
			}
		}
	} 
} 
