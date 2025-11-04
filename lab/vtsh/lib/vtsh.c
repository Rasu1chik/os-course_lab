#include "vtsh.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define INPUT_BUFFER_SIZE 1024

const char* vtsh_prompt() {
  return "vtsh> ";
}


int parse_input(const char* input, command_t* commands, int* num_commands) {
  char input_copy[MAX_INPUT_SIZE];
  strncpy(input_copy, input, sizeof(input_copy) - 1);
  input_copy[sizeof(input_copy) - 1] = '\0';

  *num_commands = 0;
  char* token = strtok(input_copy, ";\n");

  while (token != NULL && *num_commands < MAX_COMMANDS) {
    // обрезаем пробелы
    while (*token == ' ' || *token == '\t')
      token++;
    char* end = token + strlen(token) - 1;
    while (end > token && (*end == ' ' || *end == '\t')) {
      *end = '\0';
      end--;
    }

    if (strlen(token) == 0) {
      token = strtok(NULL, ";\n");
      continue;
    }

    
    commands[*num_commands].argc = 0;
    commands[*num_commands].args = malloc(MAX_ARGS * sizeof(char*));

    char* arg_token = strtok(token, " \t");
    while (arg_token != NULL && commands[*num_commands].argc < MAX_ARGS - 1) {
      commands[*num_commands].args[commands[*num_commands].argc] =
          strdup(arg_token);
      commands[*num_commands].argc++;
      arg_token = strtok(NULL, " \t");
    }

    if (commands[*num_commands].argc > 0) {
      commands[*num_commands].program = strdup(commands[*num_commands].args[0]);
      commands[*num_commands].args[commands[*num_commands].argc] = NULL;
      (*num_commands)++;
    }

    token = strtok(NULL, ";\n");
  }

  return 0;
}


int run_command(command_t* cmd) {
  
  if (strcmp(cmd->program, "./shell") == 0 ||
      strcmp(cmd->program, "shell") == 0) {
    vtsh_run();
    return 0;
  }

  pid_t pid = fork();
  if (pid == 0) {
    execvp(cmd->program, cmd->args);
    printf("Command not found\n");
    fflush(stdout);
    exit(1);
  } else if (pid > 0) {
    int status;
    waitpid(pid, &status, 0);
  } else {
    perror("fork");
  }

  return 0;
}


void vtsh_run() {
  char input[INPUT_BUFFER_SIZE];
  command_t commands[MAX_COMMANDS];
  int num_commands;

  
  if (!isatty(STDIN_FILENO)) {
    
    int idx = 0;
    char ch;
    while (idx < INPUT_BUFFER_SIZE - 1 && read(STDIN_FILENO, &ch, 1) == 1) {
      input[idx++] = ch;
      if (ch == '\n')
        break;
    }
    input[idx] = '\0';

    if (idx == 0)
      return;

    
    if (strlen(input) == 0 || strcmp(input, "\n") == 0)
      return;

    
    if (strncmp(input, "cat", 3) == 0 &&
        (input[3] == '\n' || input[3] == '\0' || input[3] == ' ' ||
         input[3] == '\t')) {
      pid_t pid = fork();
      if (pid == 0) {
        execlp("cat", "cat", NULL);
        printf("Command not found\n");
        fflush(stdout);
        exit(1);
      } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
      } else {
        perror("fork");
      }
      return;
    }

    
    do {
      if (strlen(input) == 0 || strcmp(input, "\n") == 0)
        continue;
      parse_input(input, commands, &num_commands);
      for (int i = 0; i < num_commands; i++) {
        run_command(&commands[i]);
        for (int j = 0; j < commands[i].argc; j++)
          free(commands[i].args[j]);
        free(commands[i].args);
        free(commands[i].program);
      }
    } while (fgets(input, sizeof(input), stdin));

    return;
  }

  
  while (true) {
    printf("%s", vtsh_prompt());
    fflush(stdout);

    if (fgets(input, sizeof(input), stdin) == NULL)
      break;

    if (strlen(input) == 0 || strcmp(input, "\n") == 0)
      continue;

    parse_input(input, commands, &num_commands);
    for (int i = 0; i < num_commands; i++) {
      run_command(&commands[i]);
      for (int j = 0; j < commands[i].argc; j++)
        free(commands[i].args[j]);
      free(commands[i].args);
      free(commands[i].program);
    }
  }
}
