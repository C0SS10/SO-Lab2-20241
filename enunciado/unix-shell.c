#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

#define MAX_COMMAND_LEN 1024
#define MAX_ARGS 64
#define MAX_PATHS 64
#define MAX_COMMANDS 10

// Variables globales
char *search_paths[MAX_PATHS];
int num_paths = 0;

// Manejo de error
void print_error(){
	const char *error_message = "An error has occurred\n";
	write(STDERR_FILENO, error_message, strlen(error_message));
}

// Manejo de path
void initialize_paths(){
	search_paths[0] = malloc(strlen("/bin") + 1);
	strcpy(search_paths[0], "/bin");
	num_paths = 1;
}


