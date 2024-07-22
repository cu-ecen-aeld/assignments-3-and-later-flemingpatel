#include "systemcalls.h"


/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd) {

/*
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/

    int ret;

    if (cmd == NULL) {
        fprintf(stderr, "Error: null command; check arguments\n");
        return false;
    }

    ret = system(cmd);

    if (ret == -1) {
        fprintf(stderr, "Error: system() call failed with error: %s\n", strerror(errno));
        return false;
    } else if (WIFEXITED(ret) && WEXITSTATUS(ret) != 0) {
        fprintf(stderr, "Error: Command '%s' failed with exit status %d\n", cmd, WEXITSTATUS(ret));
        return false;
    } else if (WIFSIGNALED(ret)) {
        fprintf(stderr, "Error: Command '%s' was terminated by signal %d\n", cmd, WTERMSIG(ret));
        return false;
    }

    return true;
}

/**
* @param *command[] - defines the command and its arguments
* @param output_fd - fd to redirect stdout and stderr
* @return true if the command @param *command[] were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool fork_execute_command(char *command[], int output_fd) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return false;
    } else if (pid == 0) {
        if (output_fd >= 0) {
            if (dup2(output_fd, STDOUT_FILENO) < 0 || dup2(output_fd, STDERR_FILENO) < 0) {
                perror("dup2");
                close(output_fd);
                _exit(EXIT_FAILURE);
            }
            close(output_fd);
        }
        execv(command[0], command);

        // if we see this, execv must have failed
        perror("execv");
        _exit(EXIT_FAILURE);
    } else {
        int status;
        while (waitpid(pid, &status, 0) == -1) {
            if (errno != EINTR) {
                perror("waitpid");
                return false;
            }
        }
        if (WIFEXITED(status)) {
            int exit_status = WEXITSTATUS(status);
            if (exit_status == 0) {
                return true;
            } else {
                fprintf(stderr, "Error: Command '%s' failed with exit status %d\n", command[0], exit_status);
                return false;
            }
        } else {
            fprintf(stderr, "Error: child process terminated unexpectedly\n");
            return false;
        }
    }
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...) {
    va_list args;
    va_start(args, count);
    char *command[count + 1];
    int i;
    for (i = 0; i < count; i++) {
        command[i] = va_arg(args,
        char *);
    }
    command[count] = NULL;

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/

    va_end(args);

    return fork_execute_command(command, -1);
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...) {
    va_list args;
    va_start(args, count);
    char *command[count + 1];
    int i;
    for (i = 0; i < count; i++) {
        command[i] = va_arg(args,
        char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/

    va_end(args);

    int fd = open(outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return false;
    }

    bool result = fork_execute_command(command, fd);
    close(fd);
    return result;
}
