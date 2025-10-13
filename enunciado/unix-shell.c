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

void set_paths(char **new_paths, int count) {
    // Liberar paths viejos/usados
    for (int i = 0; i < num_paths; i++) {
        free(search_paths[i]);
    }
    
    // Asignar nuevo paths
    num_paths = 0;
    for (int i = 0; i < count && num_paths < MAX_PATHS; i++) {
        if (new_paths[i] != NULL && strlen(new_paths[i]) > 0) {
            search_paths[num_paths] = malloc(strlen(new_paths[i]) + 1);
            strcpy(search_paths[num_paths], new_paths[i]);
            num_paths++;
        }
    }
}

// Comandos Built-in
int cmd_exit(char **args, int argc) {
    if (argc > 1) {
        print_error();
        return 0;
    }
    exit(0);
}

int cmd_cd(char **args, int argc) {
    if (argc != 2) {
        print_error();
        return 0;
    }
    
    if (chdir(args[1]) != 0) {
        print_error();
    }
    return 0;
}

int cmd_path(char **args, int argc) {
    set_paths(args + 1, argc - 1);
    return 0;
}

// Verificar si el comando es built-in
int is_builtin(const char *cmd) {
    return strcmp(cmd, "exit") == 0 || 
           strcmp(cmd, "cd") == 0 || 
           strcmp(cmd, "path") == 0;
}

// Ejecutar comandos built-in
int execute_builtin(char **args, int argc) {
    const char *cmd = args[0];
    
    if (strcmp(cmd, "exit") == 0) {
        return cmd_exit(args, argc);
    } else if (strcmp(cmd, "cd") == 0) {
        return cmd_cd(args, argc);
    } else if (strcmp(cmd, "path") == 0) {
        return cmd_path(args, argc);
    }
    return 0;
}

// Encontrar ejecutables en paths
char* find_executable(const char *cmd) {
    // Si cmd contiene '/', manejarlo como path
    // (No se manejapor requisitos en el README)
    
    static char full_path[1024];
    if (strchr(cmd, '/') != NULL) {
        if (access(cmd, X_OK) == 0) {
            strcpy(full_path, cmd);
            return full_path;
        }
        return NULL;
    }

    for (int i = 0; i < num_paths; i++) {
        snprintf(full_path, sizeof(full_path), "%s/%s", search_paths[i], cmd);
        
        if (access(full_path, X_OK) == 0) {
            return full_path;
        }
    }
    
    return NULL;
}

// Traducir redirección
int parse_redirection(char **args, int argc, char **redirect_file) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(args[i], ">") == 0) {
            if (i + 1 >= argc || i + 2 < argc) {
                print_error();
                return -1;
            }
            *redirect_file = args[i + 1];
            args[i] = NULL;
            return i;
        }
    }
    return 0;
}

// Executar comando externo
void execute_external(char **args, int argc, char *redirect_file) {
    pid_t pid = fork();
    
    if (pid == 0) {
        // Proceso hijo
        if (redirect_file != NULL) {
            int fd = open(redirect_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {
                print_error();
                exit(1);
            }
            
            // Redirección stdout y stderr a un archivo
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        
        char *executable = find_executable(args[0]);
        if (executable == NULL) {
            print_error();
            exit(1);
        }
        
        // Hacer argumentos NULL-terminated para execv
        args[argc] = NULL;
        
        if (execv(executable, args) == -1) {
            print_error();
            exit(1);
        }
    } else if (pid < 0) {
        print_error();
    }
}

// Traducir y ejecutar linea de comando
void parse_and_execute(char *line, int is_interactive) {
    if (line == NULL || strlen(line) == 0) {
        return;
    }
    
    // Separar por '&' para los comandos paralelos
    char *cmd_copy = malloc(strlen(line) + 1);
    strcpy(cmd_copy, line);
    
    char **commands[MAX_COMMANDS];
    int cmd_counts[MAX_COMMANDS];
    int num_commands = 0;
    
    char *cmd_ptr = cmd_copy;
    char *current_cmd;
    
    // Separar por '&'
    while ((current_cmd = strsep(&cmd_ptr, "&")) != NULL) {
        while (*current_cmd && (*current_cmd == ' ' || *current_cmd == '\t')) {
            current_cmd++;
        }
        
        if (strlen(current_cmd) == 0) continue;
        
        char *end = current_cmd + strlen(current_cmd) - 1;
        while (end > current_cmd && (*end == ' ' || *end == '\t' || *end == '\n')) {
            *end = '\0';
            end--;
        }
        
        if (strlen(current_cmd) == 0) continue;
        
        // Traducir argumentos
        char **args = malloc(sizeof(char*) * MAX_ARGS);
        int argc = 0;
        char *arg_copy = malloc(strlen(current_cmd) + 1);
        strcpy(arg_copy, current_cmd);
        
        char *arg_ptr = arg_copy;
        char *arg;
        
        while ((arg = strsep(&arg_ptr, " \t")) != NULL) {
            if (strlen(arg) > 0) {
                args[argc] = malloc(strlen(arg) + 1);
                strcpy(args[argc], arg);
                argc++;
            }
        }
        free(arg_copy);
        
        if (argc == 0) {
            free(args);
            continue;
        }
        
        commands[num_commands] = args;
        cmd_counts[num_commands] = argc;
        num_commands++;
    }
    free(cmd_copy);
    
    // Ejecutar todos los comandos
    pid_t pids[MAX_COMMANDS];
    int pid_count = 0;
    
    for (int i = 0; i < num_commands; i++) {
        char **args = commands[i];
        int argc = cmd_counts[i];
        char *redirect_file = NULL;
        
        // verificar si hay redirección
        int redir_pos = parse_redirection(args, argc, &redirect_file);
        if (redir_pos == -1) {
            // Error en redirección
            for (int j = 0; j < argc; j++) {
                free(args[j]);
            }
            free(args);
            continue;
        }
        
        if (redir_pos > 0) {
            argc = redir_pos;
        }
        
        // Verificar si se trata de un built-in command
        if (is_builtin(args[0])) {
            execute_builtin(args, argc);
        } else {
            execute_external(args, argc, redirect_file);
            pids[pid_count++] = 1; // Marcador para saber si se hace el fork
        }
        
        // Liberar argumentos
        for (int j = 0; j < cmd_counts[i]; j++) {
            free(args[j]);
        }
        free(args);
    }
    
    // Esperar a todos los procesos hijos
    while (waitpid(-1, NULL, 0) > 0);
}

// Ciclo principal
void shell_loop(FILE *input, int is_interactive) {
    char line[MAX_COMMAND_LEN];
    
    while (1) {
        if (is_interactive) {
            printf("wish> ");
            fflush(stdout);
        }
        
        if (fgets(line, sizeof(line), input) == NULL) {
            // End of file
            if (is_interactive) {
                printf("\n");
            }
            exit(0);
        }
        
        // Remover nueva linea
        line[strcspn(line, "\n")] = '\0';
        
        // Ejecutar comando
        parse_and_execute(line, is_interactive);
    }
}

int main(int argc, char *argv[]) {
    initialize_paths();
    
    FILE *input = stdin;
    int is_interactive = 1;
    
    if (argc > 2) {
        print_error();
        exit(1);
    } else if (argc == 2) {
        input = fopen(argv[1], "r");
        if (input == NULL) {
            print_error();
            exit(1);
        }
        is_interactive = 0;
    }
    
    shell_loop(input, is_interactive);
    
    if (input != stdin) {
        fclose(input);
    }
    
    return 0;
}
