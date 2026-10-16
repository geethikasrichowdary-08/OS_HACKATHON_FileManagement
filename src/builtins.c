#include "shell.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int is_name(const Command *command, const char *name)
{
    return command->argc > 0 && strcmp(command->argv[0], name) == 0;
}

int builtin_is_parent(const Command *command)
{
    return is_name(command, "cd") || is_name(command, "pwd") ||
           is_name(command, "help") || is_name(command, "exit") ||
           is_name(command, "jobs") || is_name(command, "kill");
}

static int builtin_cd(const Command *command)
{
    if (command->argc > 2) {
        fprintf(stderr, "minish: cd: expected at most one path\n");
        return 2;
    }
    const char *path = command->argc == 2 ? command->argv[1] : getenv("HOME");
    if (path == NULL) {
        fprintf(stderr, "minish: cd: HOME is not set\n");
        return 1;
    }
    if (chdir(path) != 0) {
        fprintf(stderr, "minish: cd: %s: %s\n", path, strerror(errno));
        return 1;
    }
    return 0;
}

static int builtin_pwd(const Command *command)
{
    if (command->argc != 1) {
        fprintf(stderr, "minish: pwd: does not accept arguments\n");
        return 2;
    }
    char *directory = getcwd(NULL, 0);
    if (directory == NULL) {
        perror("minish: pwd");
        return 1;
    }
    puts(directory);
    free(directory);
    return 0;
}

static int builtin_exit(Shell *shell, const Command *command)
{
    if (command->argc > 2) {
        fprintf(stderr, "minish: exit: expected zero or one status\n");
        return 2;
    }
    int status = shell->exit_status;
    if (command->argc == 2) {
        char *end = NULL;
        errno = 0;
        long parsed = strtol(command->argv[1], &end, 10);
        if (errno != 0 || end == command->argv[1] || *end != '\0' ||
            parsed < 0 || parsed > 255) {
            fprintf(stderr, "minish: exit: status must be an integer from 0 to 255\n");
            return 2;
        }
        status = (int)parsed;
    }
    shell->exit_status = status;
    shell->should_exit = true;
    return status;
}

static int builtin_kill(const Command *command, Shell *shell)
{
    if (command->argc != 2 || command->argv[1][0] != '%') {
        fprintf(stderr, "minish: kill: usage: kill %%job-id\n");
        return 2;
    }
    char *end = NULL;
    errno = 0;
    unsigned long id = strtoul(command->argv[1] + 1, &end, 10);
    if (errno != 0 || end == command->argv[1] + 1 || *end != '\0' ||
        id == 0 || id > UINT_MAX) {
        fprintf(stderr, "minish: kill: invalid job id\n");
        return 2;
    }
    return jobs_terminate(shell, (unsigned int)id);
}

int run_builtin(Shell *shell, const Command *command)
{
    if (is_name(command, "cd")) {
        return builtin_cd(command);
    }
    if (is_name(command, "pwd")) {
        return builtin_pwd(command);
    }
    if (is_name(command, "help")) {
        puts("Mini Linux Shell (minish)\n"
             "Built-ins: cd [path], pwd, help, jobs, kill %job-id, exit [0-255]\n"
             "Operators: command | command, < file, > file, >> file, command &\n"
             "Quotes and backslash escapes group words; expansions are not supported.");
        return 0;
    }
    if (is_name(command, "jobs")) {
        if (command->argc != 1) {
            fprintf(stderr, "minish: jobs: does not accept arguments\n");
            return 2;
        }
        jobs_print(shell);
        return 0;
    }
    if (is_name(command, "kill")) {
        return builtin_kill(command, shell);
    }
    if (is_name(command, "exit")) {
        return builtin_exit(shell, command);
    }
    return -1;
}