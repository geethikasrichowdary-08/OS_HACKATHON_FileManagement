git status --short# Mini Linux Shell with Background Job Management

**OS Hackathon | Team G-3**

`minish` is a small, modular command interpreter written in C for Ubuntu/Linux. It demonstrates POSIX process creation, pipelines, redirection, built-ins, process groups, and background-job cleanup. It is an educational shell, not a replacement for Bash or a security boundary.

## Features

- Parses quoted and escaped words, pipelines (`|`), input (`<`), truncate output (`>`), append output (`>>`), and a trailing background marker (`&`).
- Runs external programs through `fork()`, `execvp()`, and `waitpid()`.
- Implements `cd`, `pwd`, `help`, `exit`, `jobs`, and the shell-scoped `kill %job-id` built-in.
- Connects pipeline stages with `pipe()` and `dup2()`; applies redirections in each command process.
- Tracks each background pipeline as a process group, reaps completed children, and terminates only job process groups owned by this shell.
- On job termination or shell exit, sends `SIGTERM`, allows a bounded grace period, then uses `SIGKILL` for remaining owned children.
- Builds with strict GCC warnings and includes an automated Python integration suite.

## Ubuntu Setup and Build

Required tools: GCC, GNU Make, and Python 3 (for tests). On Ubuntu:

```sh
sudo apt update
sudo apt install build-essential python3
```

Build and test from the repository root:

```sh
make
make test
```

Run interactively or feed commands from a file/pipe:

```sh
./build/minish
printf 'pwd\nhelp\nexit\n' | ./build/minish
```

The default compiler flags use C11, POSIX.1-2008 interfaces, `-Wall -Wextra -Wpedantic -Werror`, and optimization. Override `CC`, `CFLAGS`, or `LDFLAGS` on the `make` command line when needed.

## Quick Usage

```text
minish:/home/student$ pwd
/home/student
minish:/home/student$ printf 'hello\n' | tr a-z A-Z
HELLO
minish:/home/student$ cat < input.txt | sort > sorted.txt
minish:/home/student$ printf 'another line\n' >> sorted.txt
minish:/home/student$ /usr/bin/sleep 10 &
[1] 12345
minish:/home/student$ jobs
[1] Running        /usr/bin/sleep 10 &
minish:/home/student$ kill %1
[1] Terminated     /usr/bin/sleep 10 &
minish:/home/student$ exit 0
```

Job IDs are local to one `minish` session. `kill` accepts only `%job-id`; it cannot target arbitrary PIDs. `exit [0-255]` exits the shell and cleans up its recorded background jobs.

## Project Layout

```text
include/shell.h       Shared shell, command, pipeline, and job types
src/main.c            Input loop, prompt, parsing/execution coordination
src/parser.c          Tokenization, quote handling, syntax checks
src/builtins.c        Built-in command implementations
src/execute.c         Fork/exec, process groups, pipes, redirection, waits
src/jobs.c            SIGCHLD setup, job table, reaping, termination, cleanup
tests/test_shell.py   Automated integration tests
docs/                 Project report, architecture, demo, verified results
presentation/         Reproducible 10-slide PowerPoint generator
```

## Documentation

- [Six-module project report](docs/PROJECT_REPORT.md)
- [Architecture and process diagrams](docs/ARCHITECTURE.md)
- [Live-demo guide](docs/DEMO_GUIDE.md)
- [Verified test results](docs/TEST_RESULTS.md)
- [10-slide PowerPoint deck](presentation/minish_team_g3.pptx)
- [Presentation generator](presentation/generate_deck.py)

To generate the PowerPoint, install the optional Python dependency and run:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install python-pptx
.venv/bin/python presentation/generate_deck.py
```

This writes `presentation/minish_team_g3.pptx`. The shell build and tests do not require `python-pptx`.

## Scope and Limitations

This parser does not implement variable or command substitution, globbing, semicolon-separated commands, command lists, heredocs, file-descriptor-specific redirection, or shell scripts with control-flow syntax. A background marker is supported only at the end of a single parsed pipeline. Jobs are reported as `Running` or `Done`; suspended-job tracking and `fg`/`bg` are not implemented. Pipeline exit status is the status of its final process. These constraints are intentional for a focused undergraduate POSIX-process demonstration.

The shell does not attempt to kill processes outside process groups created and recorded for its own jobs. Never run it with elevated privileges as a way to manage unrelated processes.