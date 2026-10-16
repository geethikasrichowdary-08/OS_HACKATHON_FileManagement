#ifndef MINISH_SHELL_H
#define MINISH_SHELL_H

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct {
    char **argv;
    size_t argc;
    char *input_path;
    char *output_path;
    bool append_output;
} Command;

typedef struct {
    Command *commands;
    size_t count;
    bool background;
    char *display;
} Pipeline;

typedef struct Job {
    unsigned int id;
    pid_t pgid;
    pid_t *pids;
    bool *done;
    size_t process_count;
    char *display;
    struct Job *next;
} Job;

typedef struct {
    Job *jobs;
    unsigned int next_job_id;
    pid_t shell_pgid;
    int terminal_fd;
    bool interactive;
    bool should_exit;
    int exit_status;
} Shell;

int parse_line(const char *line, Pipeline *pipeline, char **error_message);
void free_pipeline(Pipeline *pipeline);
int execute_pipeline(Shell *shell, const Pipeline *pipeline);

int builtin_is_parent(const Command *command);
int run_builtin(Shell *shell, const Command *command);

int jobs_init(Shell *shell);
void jobs_reap(Shell *shell);
void jobs_print(Shell *shell);
int jobs_add(Shell *shell, pid_t pgid, const pid_t *pids, size_t count,
             const char *display);
void jobs_cleanup(Shell *shell);
int jobs_terminate(Shell *shell, unsigned int id);

int apply_redirections(const Command *command);
void restore_terminal(Shell *shell, pid_t process_group);

#endif