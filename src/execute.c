#include "shell.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int apply_redirections(const Command *command)
{
    if (command->input_path != NULL) {
        int descriptor = open(command->input_path, O_RDONLY);
        if (descriptor < 0) {
            fprintf(stderr, "minish: %s: %s\n", command->input_path,
                    strerror(errno));
            return -1;
        }
        if (dup2(descriptor, STDIN_FILENO) < 0) {
            perror("minish: dup2 input");
            close(descriptor);
            return -1;
        }
        close(descriptor);
    }
    if (command->output_path != NULL) {
        int flags = O_WRONLY | O_CREAT |
                    (command->append_output ? O_APPEND : O_TRUNC);
        int descriptor = open(command->output_path, flags, 0666);
        if (descriptor < 0) {
            fprintf(stderr, "minish: %s: %s\n", command->output_path,
                    strerror(errno));
            return -1;
        }
        if (dup2(descriptor, STDOUT_FILENO) < 0) {
            perror("minish: dup2 output");
            close(descriptor);
            return -1;
        }
        close(descriptor);
    }
    return 0;
}

static int run_parent_builtin(Shell *shell, const Command *command)
{
    int saved_input = dup(STDIN_FILENO);
    int saved_output = dup(STDOUT_FILENO);
    if (saved_input < 0 || saved_output < 0) {
        perror("minish: dup");
        if (saved_input >= 0) close(saved_input);
        if (saved_output >= 0) close(saved_output);
        return 1;
    }
    int result = 1;
    if (apply_redirections(command) == 0) {
        result = run_builtin(shell, command);
        fflush(stdout);
        fflush(stderr);
    }
    if (dup2(saved_input, STDIN_FILENO) < 0 ||
        dup2(saved_output, STDOUT_FILENO) < 0) {
        perror("minish: restore standard descriptors");
        result = 1;
    }
    close(saved_input);
    close(saved_output);
    return result;
}

static void close_pipe_array(int (*pipes)[2], size_t count)
{
    for (size_t index = 0; index < count; index++) {
        if (pipes[index][0] >= 0) close(pipes[index][0]);
        if (pipes[index][1] >= 0) close(pipes[index][1]);
    }
}

static int wait_for_processes(const pid_t *pids, size_t count)
{
    int final_status = 0;
    for (size_t index = 0; index < count; index++) {
        int status;
        pid_t waited;
        do {
            waited = waitpid(pids[index], &status, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited < 0) {
            perror("minish: waitpid");
            final_status = 1;
        } else if (index + 1 == count) {
            if (WIFEXITED(status)) {
                final_status = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                final_status = 128 + WTERMSIG(status);
            }
        }
    }
    return final_status;
}

int execute_pipeline(Shell *shell, const Pipeline *pipeline)
{
    if (pipeline->count == 1 && !pipeline->background &&
        builtin_is_parent(&pipeline->commands[0])) {
        return run_parent_builtin(shell, &pipeline->commands[0]);
    }

    size_t pipe_count = pipeline->count - 1;
    int (*pipes)[2] = pipe_count == 0 ? NULL : calloc(pipe_count, sizeof(*pipes));
    pid_t *pids = calloc(pipeline->count, sizeof(*pids));
    if ((pipe_count > 0 && pipes == NULL) || pids == NULL) {
        perror("minish: allocation");
        free(pipes);
        free(pids);
        return 1;
    }
    for (size_t index = 0; index < pipe_count; index++) {
        pipes[index][0] = -1;
        pipes[index][1] = -1;
    }
    for (size_t index = 0; index < pipe_count; index++) {
        if (pipe(pipes[index]) != 0) {
            perror("minish: pipe");
            close_pipe_array(pipes, pipe_count);
            free(pipes);
            free(pids);
            return 1;
        }
    }

    size_t started = 0;
    pid_t process_group = 0;
    for (size_t index = 0; index < pipeline->count; index++) {
        pid_t child = fork();
        if (child < 0) {
            perror("minish: fork");
            if (process_group > 0) (void)kill(-process_group, SIGTERM);
            close_pipe_array(pipes, pipe_count);
            (void)wait_for_processes(pids, started);
            free(pipes);
            free(pids);
            return 1;
        }
        if (child == 0) {
            pid_t group = process_group == 0 ? getpid() : process_group;
            if (setpgid(0, group) != 0) {
                perror("minish: setpgid");
                _exit(126);
            }
            signal(SIGCHLD, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);
            if (index > 0 && dup2(pipes[index - 1][0], STDIN_FILENO) < 0) {
                perror("minish: dup2 pipe input");
                _exit(126);
            }
            if (index < pipe_count && dup2(pipes[index][1], STDOUT_FILENO) < 0) {
                perror("minish: dup2 pipe output");
                _exit(126);
            }
            close_pipe_array(pipes, pipe_count);
            if (apply_redirections(&pipeline->commands[index]) != 0) {
                _exit(126);
            }
            if (builtin_is_parent(&pipeline->commands[index])) {
                Shell child_shell = *shell;
                int status = run_builtin(&child_shell, &pipeline->commands[index]);
                fflush(stdout);
                _exit(status < 0 ? 127 : status);
            }
            execvp(pipeline->commands[index].argv[0],
                   pipeline->commands[index].argv);
            fprintf(stderr, "minish: %s: %s\n",
                    pipeline->commands[index].argv[0], strerror(errno));
            _exit(errno == ENOENT ? 127 : 126);
        }

        if (process_group == 0) {
            process_group = child;
        }
        if (setpgid(child, process_group) != 0 && errno != EACCES &&
            errno != ESRCH) {
            perror("minish: setpgid");
        }
        pids[started++] = child;
    }

    close_pipe_array(pipes, pipe_count);
    free(pipes);
    if (pipeline->background) {
        if (jobs_add(shell, process_group, pids, started, pipeline->display) != 0) {
            perror("minish: record job");
            (void)kill(-process_group, SIGTERM);
            (void)wait_for_processes(pids, started);
            free(pids);
            return 1;
        }
        printf("[%u] %ld\n", shell->next_job_id - 1, (long)process_group);
        free(pids);
        return 0;
    }

    if (shell->interactive && tcsetpgrp(shell->terminal_fd, process_group) != 0) {
        perror("minish: tcsetpgrp");
    }
    int status = wait_for_processes(pids, started);
    restore_terminal(shell, shell->shell_pgid);
    free(pids);
    return status;
}

void restore_terminal(Shell *shell, pid_t process_group)
{
    if (shell->interactive &&
        tcsetpgrp(shell->terminal_fd, process_group) != 0) {
        perror("minish: tcsetpgrp");
    }
}