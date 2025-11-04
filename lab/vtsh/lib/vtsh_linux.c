#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "vtsh.h"

#define INPUT_BUFFER_SIZE 1024
#define STACK_SIZE (1024 * 1024)

const char* vtsh_prompt() {
  return "vtsh> ";
}

static int child_func(void* arg) {
  command_t* cmd = (command_t*)arg;
  execvp(cmd->program, cmd->args);
  fprintf(stderr, "Command not found: %s\n", cmd->program);
  exit(127);
}

int parse_input(const char* input, command_t* commands, int* num_commands) {
  char input_copy[MAX_INPUT_SIZE];
  strncpy(input_copy, input, sizeof(input_copy) - 1);
  input_copy[sizeof(input_copy) - 1] = '\0';

  *num_commands = 0;

  if (strchr(input_copy, ';') == NULL) {
    commands[*num_commands].argc = 0;
    commands[*num_commands].args = malloc(MAX_ARGS * sizeof(char*));
    char* arg_token = strtok(input_copy, " \t");
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
    return 0;
  }

  char* token = strtok(input_copy, ";");
  while (token != NULL && *num_commands < MAX_COMMANDS) {
    while (*token == ' ' || *token == '\t')
      token++;
    char* end = token + strlen(token) - 1;
    while (end > token && (*end == ' ' || *end == '\t')) {
      *end = '\0';
      end--;
    }
    if (strlen(token) == 0) {
      token = strtok(NULL, ";");
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
    token = strtok(NULL, ";");
  }

  return 0;
}

int execute_command(const command_t* cmd) {
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  void* stack = mmap(
      NULL,
      STACK_SIZE,
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS,
      -1,
      0
  );
  if (stack == MAP_FAILED) {
    perror("mmap");
    return -1;
  }

  pid_t pid = clone(child_func, (char*)stack + STACK_SIZE, SIGCHLD, (void*)cmd);
  if (pid == -1) {
    perror("clone");
    munmap(stack, STACK_SIZE);
    return -1;
  }

  int status;
  waitpid(pid, &status, 0);
  clock_gettime(CLOCK_MONOTONIC, &end);

  long seconds = end.tv_sec - start.tv_sec;
  long nanoseconds = end.tv_nsec - start.tv_nsec;
  if (nanoseconds < 0) {
    seconds--;
    nanoseconds += 1000000000;
  }

  printf("Execution time: %ld.%09lds\n", seconds, nanoseconds);
  munmap(stack, STACK_SIZE);
  return WEXITSTATUS(status);
}

int execute_commands_sequence(command_t* commands, int num_commands) {
  for (int i = 0; i < num_commands; i++) {
    printf("Executing: %s\n", commands[i].program);
    int result = execute_command(&commands[i]);
    printf("Exit code: %d\n", result);
  }
  return 0;
}

void free_commands(command_t* commands, int num_commands) {
  for (int i = 0; i < num_commands; i++) {
    free(commands[i].program);
    for (int j = 0; j < commands[i].argc; j++)
      free(commands[i].args[j]);
    free(commands[i].args);
  }
}

void vtsh_run() {
  char input[INPUT_BUFFER_SIZE];
  command_t commands[MAX_COMMANDS];
  int num_commands;

  while (true) {
    printf("%s", vtsh_prompt());
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) == NULL) {
      printf("\n");
      break;
    }

    input[strcspn(input, "\n")] = '\0';
    if (strlen(input) == 0)
      continue;

    if (strcmp(input, "exit") == 0) {
      printf("bye!\n");
      break;
    }

    if (parse_input(input, commands, &num_commands) == 0) {
      execute_commands_sequence(commands, num_commands);
      free_commands(commands, num_commands);
    }
  }
}
