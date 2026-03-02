#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <wait.h>
#include <errno.h>
#include <string.h>

void write_to_log_file(const char *message){
    FILE *file = fopen("shell.log", "a");
    if(file != NULL){
        fprintf(file, "%s", message);
        fclose(file);
    }
    else{
        perror("Error in opening the file\n");
    }
}

void reap_child_zombie(){
    int status;
    pid_t pid;

    while((pid = waitpid(-1, &status, WNOHANG)) > 0){
        write_to_log_file("Child terminated\n");
    }
}

void on_child_exit(int signal){
    int savedError = errno; // waitpad might overwrite the error of opening file

    reap_child_zombie();

    errno = savedError;
}

#define MAX_VAR 100

typedef struct{
    char* key;
    char* value;
}tableOfExport;

static tableOfExport table[MAX_VAR];
int tableCount = 0;

char* my_getenv(char* key) {
    for (int i = 0; i < tableCount; i++) {
        if (strcmp(table[i].key, key) == 0) {
            return table[i].value;
        }
    }
    return NULL; 
}

void my_setenv(const char* key, const char* value) {
    for (int i = 0; i < tableCount; i++) {
        if (strcmp(table[i].key, key) == 0) {
            free(table[i].value); 
            table[i].value = strdup(value);
            return;
        }
    }
    table[tableCount].key = strdup(key);
    table[tableCount].value = strdup(value);
    tableCount++;
}

int change_directory(char **args) {
    char *path = args[1];
    char resolved_path[1024];

    // 1. Handle "cd" or "cd ~"
    if (path == NULL || strcmp(path, "~") == 0) {
        path = my_getenv("HOME");
        if (!path) return -1;
    } 
    // 2. Handle "cd ~/subdir"
    else if (strncmp(path, "~/", 2) == 0) {
        char *home = my_getenv("HOME");
        if (!home) return -1;
        
        // Combine HOME + the part after "~/":
        snprintf(resolved_path, sizeof(resolved_path), "%s/%s", home, path + 2);
        path = resolved_path;
    }

    // 3. Perform the system call
    if (chdir(path) != 0) {
        perror("cd"); // This prints "cd: [error message]"
        return -1;
    }

    // 4. Update PWD in your custom table (since setenv is forbidden)
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        my_setenv("PWD", cwd);
    }
    return 0;
}


void setup_environment(){
    char cwd[1024];
    if(getcwd(cwd, sizeof(cwd)) != NULL){
        printf("shell in %s\n", cwd);
    }
}

char* read_input(){
    static char buffer[1024];
    if(fgets(buffer, sizeof(buffer), stdin) == NULL){
        return NULL;
    }
    buffer[strcspn(buffer, "\n")] = 0;
    return buffer;
}

#define MAX_ARG 64

int parse_input(char* input, char** args){
    char* token = strtok(input, " ");
    int i = 0;

    while(token != NULL && i < MAX_ARG-1){
        args[i] = token;
        i++;
        token = strtok(NULL, " ");

    }
    args[i] = NULL;
    return i; //number of arguments
}

#define EXIT_TYPE 5

void execute_command(char **args){
    int backGround = 0;
    int count = 0;

    while(args[count] != NULL)count++;

    if(count > 0 && strcmp(args[count-1], "&") == 0){ // check is it back ground command?
        backGround = 1;
        args[count-1] = NULL;
    }

    pid_t pid = fork();
    if(pid == 0){ //child
        execvp(args[0], args);
        printf("ERROR\n");
        exit(1); //it is executed if error
    }
    else if(pid > 0){
        if(!backGround)waitpid(pid, NULL, 0);
    }
}

void execute_echo(char **args){
    int i = 1; //args[0] = "echo"

    while(args[i] != NULL){
        printf("%s", args[i]);

        if(args[i+1] != NULL){
            printf(" ");
        }
        i++;
    }
    printf("\n");
}


char* copy_array(const char* s){
    size_t size = strlen(s) + 1;
    char *p = malloc(size);

    if(p){
        memcpy(p, s, size);
    }
    return p;
}

void strip_quotes(char *str) {
    size_t len = strlen(str);
    if (len >= 2 && str[0] == '"' && str[len - 1] == '"') {
        memmove(str, str + 1, len - 2);
        str[len - 2] = '\0';
    }
}

void execute_export(char **args) {
    if (args[1] == NULL) {
        printf("ERROR\n");
        return;
    }

    char combined[1024] = "";
    for (int i = 1; args[i] != NULL; i++) {
        strcat(combined, args[i]);
        if (args[i+1] != NULL) strcat(combined, " "); 
    }

    char *combined_copy = strdup(combined);
    char *key = strtok(combined_copy, "=");
    char *value = strtok(NULL, "=");
    strip_quotes(value);

    if (key == NULL || value == NULL) {
        printf("ERROR");
        free(combined_copy);
        return;
    }

    int found = 0;
    for (int i = 0; i < tableCount; i++) {
        if (strcmp(table[i].key, key) == 0) {
            free(table[i].value);
            table[i].value = strdup(value);
            found = 1;
            break;
        }
    }

    if (!found) {
        if (tableCount < MAX_VAR) {
            table[tableCount].key = strdup(key);
            table[tableCount].value = strdup(value);
            tableCount++;
        } else {
            printf("Error: Export table is full.\n");
        }
    }

    my_setenv(key, value);
    
    printf("Exported: %s=%s\n", key, value);
    free(combined_copy);
}

int evaluate_expression(char **args){
    if(args[0] == NULL)return 0;

    if(strcmp(args[0], "cd") == 0){
        change_directory(args);
        return 1;
    }
    else if(strcmp(args[0], "export") == 0){
        execute_export(args);
        return 1;
    }
    else if(strcmp(args[0], "echo") == 0){
        execute_echo(args);
        return 1;
    }
    else if(strcmp(args[0], "exit") == 0){
        return EXIT_TYPE;
    }

    execute_command(args);
    return 1;
}

void env_cleanup() {
    for (int i = 0; i < tableCount; i++){
        free(table[i].key);
        free(table[i].value);
    }
}

void handle_variables(char *input) {
    char *dollar = strchr(input, '$');
    if (!dollar) return;

    char var_name[64];
    int i = 0;
    char *start = dollar + 1;
    while (*start != ' ' && *start != '\0' && i < 63) {
        var_name[i++] = *start++;
    }
    var_name[i] = '\0';

    char *value = NULL;
    for (int j = 0; j < tableCount; j++) {
        if (strcmp(table[j].key, var_name) == 0) {
            value = table[j].value;
            break;
        }
    }

    if (value) {
        char buffer[1024];
        strncpy(buffer, input, dollar - input);
        buffer[dollar - input] = '\0';
        
        strcat(buffer, value);
        strcat(buffer, start);
        strcpy(input, buffer);
    }
}

int main() {
    signal(SIGCHLD, on_child_exit);

    char input_buffer[1024];
    char *args[MAX_ARG];
    
    while(1) {
        printf("my_shell$ ");
        if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) break;
        input_buffer[strcspn(input_buffer, "\n")] = 0;

        handle_variables(input_buffer);
        
        int count = parse_input(input_buffer, args);
        
        if (count > 0) {
            int type = evaluate_expression(args);
            if (type == EXIT_TYPE) break;;
        }
    }
    env_cleanup();
    return 0;
}
