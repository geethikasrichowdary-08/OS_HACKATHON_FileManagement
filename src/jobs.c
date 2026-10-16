#include "shell.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void handle_sigchld(int signal_number)
{
    (void)signal_number;
}

static int job_is_done(const Job *job)
{
    for (size_t index = 0; index < job->process_count; index++) {
        if (!job->done[index]) {
            return 0;
        }
    }
    return 1;
}

int jobs_init(Shell *shell)
{
    struct sigaction action = {0};
    action.sa_handler = handle_sigchld;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(SIGCHLD, &action, NULL) != 0) {
        return -1;
    }

    if (shell->interactive) {
        shell->shell_pgid = getpid();
        if (setpgid(shell->shell_pgid, shell->shell_pgid) != 0 &&
            errno != EACCES && errno != EPERM) {
            return -1;
        }
        if (tcsetpgrp(shell->terminal_fd, shell->shell_pgid) != 0) {
            return -1;
        }
        const int ignored_signals[] = {SIGINT, SIGQUIT, SIGTSTP, SIGTTIN,
                                       SIGTTOU};
        for (size_t index = 0;
             index < sizeof(ignored_signals) / sizeof(ignored_signals[0]);
             index++) {
            if (signal(ignored_signals[index], SIG_IGN) == SIG_ERR) {
                return -1;
            }
        }
    } else {
        shell->shell_pgid = getpgrp();
    }
    return 0;
}

int jobs_add(Shell *shell, pid_t pgid, const pid_t *pids, size_t count,
             const char *display)
{
    Job *job = calloc(1, sizeof(*job));
    if (job == NULL) {
        return -1;
    }
    job->pids = malloc(count * sizeof(*job->pids));
    job->done = calloc(count, sizeof(*job->done));
    job->display = strdup(display);
    if (job->pids == NULL || job->done == NULL || job->display == NULL) {
        free(job->pids);
        free(job->done);
        free(job->display);
        free(job);
        return -1;
    }
    memcpy(job->pids, pids, count * sizeof(*pids));
    job->process_count = count;
    job->pgid = pgid;
    job->id = shell->next_job_id++;
    job->next = shell->jobs;
    shell->jobs = job;
    return 0;
}

void jobs_reap(Shell *shell)
{
    int status;
    pid_t completed;
    while ((completed = waitpid(-1, &status, WNOHANG)) > 0) {
        for (Job *job = shell->jobs; job != NULL; job = job->next) {
            for (size_t index = 0; index < job->process_count; index++) {
                if (job->pids[index] == completed) {
                    job->done[index] = true;
                }
            }
        }
    }
}

void jobs_print(Shell *shell)
{
    jobs_reap(shell);
    for (Job *job = shell->jobs; job != NULL; job = job->next) {
        printf("[%u] %s\t%s\n", job->id,
               job_is_done(job) ? "Done" : "Running", job->display);
    }
}

static void free_job(Job *job)
{
    free(job->pids);
    free(job->done);
    free(job->display);
    free(job);
}

void jobs_cleanup(Shell *shell)
{
    sigset_t block_set;
    sigset_t previous_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block_set, &previous_set);

    for (Job *job = shell->jobs; job != NULL; job = job->next) {
        if (!job_is_done(job) && job->pgid > 0 && job->pgid != shell->shell_pgid) {
            (void)kill(-job->pgid, SIGTERM);
        }
    }

    struct timespec pause_time = {.tv_sec = 0, .tv_nsec = 20000000};
    for (int attempt = 0; attempt < 50; attempt++) {
        bool all_done = true;
        for (Job *job = shell->jobs; job != NULL; job = job->next) {
            for (size_t index = 0; index < job->process_count; index++) {
                if (!job->done[index]) {
                    int status;
                    pid_t result = waitpid(job->pids[index], &status, WNOHANG);
                    if (result == job->pids[index] ||
                        (result < 0 && errno == ECHILD)) {
                        job->done[index] = true;
                    } else {
                        all_done = false;
                    }
                }
            }
        }
        if (all_done) {
            break;
        }
        (void)nanosleep(&pause_time, NULL);
    }

    for (Job *job = shell->jobs; job != NULL; job = job->next) {
        if (!job_is_done(job) && job->pgid > 0 && job->pgid != shell->shell_pgid) {
            (void)kill(-job->pgid, SIGKILL);
        }
    }
    for (Job *job = shell->jobs; job != NULL; job = job->next) {
        for (size_t index = 0; index < job->process_count; index++) {
            if (!job->done[index]) {
                int status;
                while (waitpid(job->pids[index], &status, 0) < 0 && errno == EINTR) {
                }
                job->done[index] = true;
            }
        }
    }

    Job *job = shell->jobs;
    while (job != NULL) {
        Job *next = job->next;
        free_job(job);
        job = next;
    }
    shell->jobs = NULL;
    sigprocmask(SIG_SETMASK, &previous_set, NULL);
}

int jobs_terminate(Shell *shell, unsigned int id)
{
    jobs_reap(shell);
    for (Job *job = shell->jobs; job != NULL; job = job->next) {
        if (job->id != id) {
            continue;
        }
        if (job_is_done(job)) {
            fprintf(stderr, "minish: kill: job %u has already finished\n", id);
            return 1;
        }
        if (job->pgid <= 0 || job->pgid == shell->shell_pgid ||
            kill(-job->pgid, SIGTERM) != 0) {
            perror("minish: kill");
            return 1;
        }
        struct timespec pause_time = {.tv_sec = 0, .tv_nsec = 20000000};
        for (int attempt = 0; attempt < 50 && !job_is_done(job); attempt++) {
            for (size_t index = 0; index < job->process_count; index++) {
                if (job->done[index]) {
                    continue;
                }
                int status;
                pid_t result = waitpid(job->pids[index], &status, WNOHANG);
                if (result == job->pids[index] ||
                    (result < 0 && errno == ECHILD)) {
                    job->done[index] = true;
                } else if (result < 0 && errno != EINTR) {
                    perror("minish: waitpid");
                }
            }
            if (!job_is_done(job)) {
                (void)nanosleep(&pause_time, NULL);
            }
        }
        if (!job_is_done(job)) {
            if (kill(-job->pgid, SIGKILL) != 0 && errno != ESRCH) {
                perror("minish: kill");
            }
        }
        for (size_t index = 0; index < job->process_count; index++) {
            if (!job->done[index]) {
                int status;
                pid_t result;
                do {
                    result = waitpid(job->pids[index], &status, 0);
                } while (result < 0 && errno == EINTR);
                if (result == job->pids[index] ||
                    (result < 0 && errno == ECHILD)) {
                    job->done[index] = true;
                } else if (result < 0) {
                    perror("minish: waitpid");
                }
            }
        }
        printf("[%u] Terminated\t%s\n", job->id, job->display);
        return 0;
    }
    fprintf(stderr, "minish: kill: no such job: %u\n", id);
    return 1;
}