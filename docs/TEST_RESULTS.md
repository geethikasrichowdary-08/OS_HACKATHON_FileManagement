# Verified Test Results

**Run date (UTC):** 2026-10-16
**Platform:** Ubuntu 24.04 container
**Compiler:** GCC 13.3.0
**Python test runtime:** Python 3.14.2

## Commands

```sh
make -B
make test
```

`make -B` forced compilation of all five C source files with `-std=c11 -Wall -Wextra -Wpedantic -Werror -O2`. The compiler and linker completed successfully. The full suite then reported **10 tests passed** (`Ran 10 tests in 2.425s`, `OK`).

## Coverage

| Behavior | Test outcome |
| --- | --- |
| Parent built-ins, `cd`, `pwd`, `help` | PASS |
| Quote handling and a two-stage pipeline | PASS |
| `<`, `>`, and `>>` redirection | PASS |
| Background completion, reaping, and `jobs` state | PASS |
| `kill %job-id` targets a recorded job | PASS |
| `kill` escalates when a job ignores `SIGTERM` | PASS |
| `exit` cleans up a background child | PASS |
| `exit` escalates for a `SIGTERM`-resistant child | PASS |
| Pipe creation fails cleanly under descriptor limit; later input still runs | PASS |
| Syntax errors and missing executable diagnostics | PASS |

The descriptor-exhaustion test sets `RLIMIT_NOFILE` to four in the child test process. This leaves standard descriptors available and causes `pipe()` to fail with `EMFILE`; the test verifies the shell prints the error and executes the next input line. The test checks behavior, not only the diagnostic.

## Scope of Verification

These are integration tests for the documented feature set, not a formal proof of race freedom or full POSIX shell conformance. Interactive terminal behavior is implemented but the automated tests are non-interactive. Stopped jobs, terminal resize behavior, and job-control built-ins `fg`/`bg` are outside the supported scope.