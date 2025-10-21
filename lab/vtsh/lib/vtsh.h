#ifndef VTSH_H
#define VTSH_H

#define MAX_INPUT_SIZE 1024
#define MAX_ARGS 64
#define MAX_COMMANDS 16

typedef struct {
  char* program;
  char** args;
  int argc;
} command_t;

const char* vtsh_prompt();
int parse_input(const char* input, command_t* commands, int* num_commands);
int run_command(command_t* cmd);
void vtsh_run();

#endif  // VTSH_H
