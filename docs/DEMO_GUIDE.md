# Live Demo Guide

## Before the Demo

From the project root, compile and run the suite:

```sh
make
make test
```

Expected verification: strict GCC build succeeds and all 10 integration tests pass. For a clean screen, start a new terminal at the repository root and launch `./build/minish`.

## Five-Minute Walkthrough

### 1. Built-ins and Parent State

```text
help
pwd
cd /tmp
pwd
```

Point out that the second `pwd` reflects the directory change because standalone `cd` runs in the shell process.

### 2. Pipeline and Redirection

```text
printf 'pear\napple\n' | sort
printf 'first line\n' > /tmp/minish-g3.txt
printf 'second line\n' >> /tmp/minish-g3.txt
cat < /tmp/minish-g3.txt
```

Expected output from the pipeline is `apple` then `pear`; the final command prints both file lines.

### 3. Background Process and Safe Job Termination

In this fresh shell, job IDs start at 1:

```text
/usr/bin/sleep 30 &
jobs
kill %1
jobs
```

Expected: the shell prints a job ID and process-group ID, reports it as Running, terminates that owned job, and reports it as Done. `kill` accepts a job-table ID, not an arbitrary PID.

### 4. Cleanup and Tests

Exit the interactive shell. To demonstrate automatic cleanup without waiting 30 seconds, run from the regular terminal:

```sh
printf '/usr/bin/sleep 30 &\nexit\n' | ./build/minish
make test
```

The regression suite also checks a child that ignores `SIGTERM` and a pipe-creation failure under `RLIMIT_NOFILE`; these are automated so the demo need not manipulate process limits live.

## Demo Notes

- Use `/usr/bin/sleep` for predictable availability on Ubuntu.
- If earlier commands used job IDs, start a fresh `minish` session before using the example `%1`.
- Do not use elevated privileges. The shell's termination operations are scoped to child process groups it created and registered.
- The implementation reports Running/Done only. It does not implement stopped-job tracking or `fg`/`bg`.