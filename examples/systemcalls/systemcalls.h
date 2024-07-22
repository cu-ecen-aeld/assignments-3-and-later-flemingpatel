#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

bool do_system(const char *command);

// helper
bool fork_execute_command(char *command[], int output_fd);

bool do_exec(int count, ...);

bool do_exec_redirect(const char *outputfile, int count, ...);
