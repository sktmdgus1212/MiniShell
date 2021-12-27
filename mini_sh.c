#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <string.h>
#include <pwd.h>

#define FALSE 0
#define TRUE 1

#define EOL	1
#define ARG	2
#define AMPERSAND 3
#define RE_RIGHT 4
#define RE_LEFT 5
#define PIPE 6

#define FOREGROUND 0
#define BACKGROUND 1

static char	input[512];
static char	tokens[1024];
char		*ptr, *tok;

int get_token(char **outptr)
{
	int	type;

	*outptr = tok;
	while ((*ptr == ' ') || (*ptr == '\t')) ptr++;

	*tok++ = *ptr;

	switch (*ptr++) {
		case '\0' : type = EOL; break;
		case '&': type = AMPERSAND; break;
		case '>': type = RE_RIGHT; break;
		case '<': type = RE_LEFT; break;
		case '|': type = PIPE; break;
		default : type = ARG;
			while ((*ptr != ' ') && (*ptr != '&') &&
				(*ptr != '\t') && (*ptr != '\0'))
				*tok++ = *ptr++;
	}
	*tok++ = '\0';
	return(type);
}

int execute_right(int narg, char *arg[1024], int right, int how)
{
	pid_t	pid;
	int fd;
	
	if ((pid = fork()) < 0) {
		// cannot make child process
		fprintf(stderr, "minish : fork error\n");
		return(-1);
	}
	else if (pid == 0) {
		fd = open(arg[right+1], O_RDWR | O_CREAT | S_IROTH, 0644); //file open
			if (fd < 0) {
				//cannot open file
				perror("error");
				exit(-1);
			}
		dup2(fd, STDOUT_FILENO); // copy file discriptor of fd to stdout_fileno
		close(fd);
		arg[right] = NULL;
		arg[right+1] = NULL;
		if (narg != 0){ 
			execvp(*arg, arg); // execute "ls"
			fprintf(stderr, "minish : command not found\n"); 
			exit(127); 
		}

		exit(0);
	}
	if (how == BACKGROUND) {	/* Background execution */
		printf("[%d]\n", pid);
		return 0;
	}		
	/* Foreground execution */
	while (waitpid(pid, NULL, 0) < 0)
		if (errno != EINTR) return -1;
	return 0;
}

int execute_left(int narg, char *arg[1024], int left, int how)
{
	pid_t	pid;
	int fd;

	if ((pid = fork()) < 0) {
		// cannot make child process
		fprintf(stderr, "minish : fork error\n");
		return(-1);
	}
	else if (pid == 0) {
		fd = open(arg[left+1], O_RDONLY); //file open
		if (fd < 0) {
			//cannot open file
			perror("error");
			exit(-1);
		}
		dup2(fd, STDIN_FILENO); // copy file discriptor of fd to stdin_fileno
		close(fd);
		arg[left] = NULL;
		arg[left + 1] = NULL;
		if (narg != 0) {
			execvp(*arg, arg); // execute "ls"
			fprintf(stderr, "minish : command not found\n");
			exit(127);
		}

		exit(0);
	}
	if (how == BACKGROUND) {	/* Background execution */
		printf("[%d]\n", pid);
		return 0;
	}
	/* Foreground execution */
	while (waitpid(pid, NULL, 0) < 0)
		if (errno != EINTR) return -1;
	return 0;
}

int execute_pipe(int narg, char *arg[1024], int exec_pipe, int how)
{
	int p[2];
	pid_t pid1, pid2;
	int i, j;
	
	if(pipe(p) == -1){
		//cannot make pipe
		perror("pipe failed");
		exit(1);
	}

	if ((pid1 = fork()) < 0) {
		// cannot make child process
		fprintf(stderr, "minish : fork error\n");
		return(-1);
	}
	else if (pid1 == 0) {
		
		dup2(p[1], STDOUT_FILENO); // copy p[1] to stdout_fileno
		close(p[0]);
		close(p[1]);
		for(int i = exec_pipe ; i < narg ; i++){
			arg[i] = NULL;
		}
		if (narg != 0) {
			execvp(*arg, arg); // execute "ls"
			fprintf(stderr, "minish : command not found\n");
			exit(127);
		}
	}
	
	if ((pid2 = fork()) < 0) {
		// cannot make child process
		fprintf(stderr, "minish : fork error\n");
		return(-1);
	}
	else if (pid2 == 0) {
		dup2(p[0], STDIN_FILENO); // copy p[0] to stdin_fileno
		close(p[0]);
		close(p[1]);
		
		for(int i = 0 ; i < exec_pipe + 1 ; i++){
			arg[i] = NULL;
		}
		
		for(int j = exec_pipe + 1 ; j < narg ; j++){
			char *temp = arg[j];
			arg[j] = NULL;
			arg[j - (exec_pipe + 1)] = temp;
		}
		if (narg != 0) {
			execvp(*arg, arg); // execute "more"
			fprintf(stderr, "minish : command not found\n");
			exit(127);
		}
	}
	close(p[0]);
	close(p[1]);
	wait(0);
	wait(0);
		
	if (how == BACKGROUND) {	/* Background execution */
		printf("[%d]\n", pid1);
		return 0;
	}
	/* Foreground execution */
	while (waitpid(pid1, NULL, 0) < 0)
		if (errno != EINTR) return -1;
	return 0;
}

int parse_and_execute(char *input)
{
	char	*arg[1024];
	int	type, how;
	int	quit = FALSE;
	int	narg = 0;
	int	finished = FALSE;
	int 	right = -1;
	int 	left = -1;
	int 	pipe = -1;
	struct stat sb;
	ptr = input;
	tok = tokens;
	while (!finished) {
		switch (type = get_token(&arg[narg])) {
		case ARG :
			narg++;
			break;
		case RE_RIGHT:
			if (right < 0) {
				right = narg; // index of ">" location
				narg++;
			}
			break;
		case RE_LEFT:
			if (left < 0) {
				left = narg; // index of "<" location
				narg++;
			}
			break;
		case PIPE:
			if (pipe < 0) {
				pipe = narg; // index of "|" location
				narg++;
			}
			break;
		case EOL :
		case AMPERSAND:
			if (!strcmp(arg[0], "quit")) quit = TRUE;
			else if (!strcmp(arg[0], "exit")) quit = TRUE;
			else if (!strcmp(arg[0], "cd")) { // change directory
				if (!strcmp(arg[1], "~")){ // change "~" to home directory
					arg[1] = getenv("HOME");
				}
				
				//check correct path and change directory
				if(S_ISDIR(sb.st_mode) && stat(arg[1], &sb) == 0){
					chdir(arg[1]);
				}
				else{
					printf("No directory"); 
				}
			}
			else if (!strcmp(arg[0], "type")) {
				if (narg > 1) {
					int	i, fid;
					int	readcount;
					char	buf[512];
					/*  학생들이 프로그램 작성할 것
					fid = open(arg[1], ...);
					if (fid >= 0) {
						readcount = read(fid, buf, 512);
						while (readcount > 0) {
							for (i = 0; i < readcount; i++)
								putchar(buf[i]);
							readcount = read(fid, buf, 512);
						}
					}
					close(fid);
					*/
				}
			}
			else {
				how = (type == AMPERSAND) ? BACKGROUND : FOREGROUND;
				arg[narg] = NULL;
				if (narg != 0) {
					if (right >= 0) { // ">" redirection execute
						execute_right(narg, arg, right, how);
					}
					if (left >= 0) { // "<" redirection execute
						execute_left(narg, arg, left, how);
					}
					if (pipe >= 0) { // "|" pipe execute
						execute_pipe(narg, arg, pipe, how);
					}
				}
			}
			narg = 0;
			if (type == EOL)
				finished = TRUE;
			break; 
		}
	}
	return quit;
}



int main()
{
    	char	*arg[1024];
	int	quit;

	printf("msh # ");
	while (gets(input)) {
		quit = parse_and_execute(input);
		if (quit) break;
		printf("msh # ");
	}
}
