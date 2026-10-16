#include "shell.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_prompt(const Shell *shell)
{
    if (!shell->interactive) {
        return;
    }

    char *directory = getcwd(NULL, 0);
    if (directory != NULL) {
        printf("minish:%s$ ", directory);
        free(directory);
    } else {
        fputs("minish$ ", stdout);
    }
    fflush(stdout);
}

int main(void)
{
    Shell shell = {
        .jobs = NULL,
        .next_job_id = 1,
        .shell_pgid = 0,
        .terminal_fd = STDIN_FILENO,
        .interactive = isatty(STDIN_FILENO),
        .should_exit = false,
        .exit_status = 0,
    };

    if (jobs_init(&shell) != 0) {
        perror("minish: initialization");
        return EXIT_FAILURE;
    }

    char *line = NULL;
    size_t capacity = 0;
    while (!shell.should_exit) {
        jobs_reap(&shell);
        print_prompt(&shell);

        errno = 0;
        ssize_t length = getline(&line, &capacity, stdin);
        if (length < 0) {
            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            }
            if (ferror(stdin)) {
                perror("minish: input");
                shell.exit_status = EXIT_FAILURE;
            }
            break;
        }

        Pipeline pipeline = {0};
        char *error_message = NULL;
        int parsed = parse_line(line, &pipeline, &error_message);
        if (parsed < 0) {
            fprintf(stderr, "minish: syntax: %s\n",
                    error_message != NULL ? error_message : "invalid command");
            free(error_message);
            shell.exit_status = 2;
            continue;
        }
        free(error_message);

        if (parsed > 0) {
            int status = execute_pipeline(&shell, &pipeline);
            if (status >= 0) {
                shell.exit_status = status;
            }
        }
        free_pipeline(&pipeline);
    }

    free(line);
    jobs_cleanup(&shell);
    return shell.exit_status;
}