# sadafa — a custom Unix shell

A small Unix shell written in C for an Operating Systems course project.
Implements process creation with `fork`/`execv`, built-in commands,
I/O redirection, pipes, background execution, and signal handling with
`sigaction`.

## Features

- Custom prompt (`sadafa> `) and command tokenizer
- Dynamic array (`Vector`) used for tokens and command history
- **Built-in commands**: `cd`, `pwd`, `echo`, `history`, `exit`
- **External commands** via manual `$PATH` lookup + `execv`
- **I/O redirection**: `<` (stdin) and `>` (stdout, create/truncate)
- **Pipes**: multi-stage pipelines (e.g. `ls | grep .c | wc -l`)
- **Background execution**: `command &` with completion notifications
- **Signal handling**:
  - Shell ignores `SIGINT` (Ctrl+C) and `SIGTSTP` (Ctrl+Z)
  - On Ctrl+C at the prompt, a newline + fresh prompt is printed
  - Foreground children receive default `SIGINT` → killable by Ctrl+C
  - Background children inherit `SIG_IGN` → immune to Ctrl+C

## Build

Requires `gcc` and `make` on a POSIX system (Linux, macOS, WSL).

```bash
make
```

Produces an executable named `myShell` in the project root.

To clean build artifacts:

```bash
make clean
```

## Run

```bash
./myShell
```

You should see the prompt:

```
sadafa>
```

## Usage examples

```text
sadafa> pwd
/home/user/projects/os-shell

sadafa> echo hello world
hello world

sadafa> ls | grep .c | wc -l
2

sadafa> sleep 5 &
[bg] 12345
sadafa> pwd
/home/user/projects/os-shell
sadafa> [done] 12345 exit=0

sadafa> cat < myShell.c > /tmp/copy.c

sadafa> history
    1  pwd
    2  echo hello world
    3  ls | grep .c | wc -l

sadafa> exit
```

## Project structure

```
.
├── Makefile
├── myShell.c       # shell main logic (prompt loop, exec, pipes, signals, ...)
├── vector.h        # dynamic array interface
├── vector.c        # dynamic array implementation
└── README.md
```

## Platform notes

This shell uses POSIX APIs (`fork`, `execv`, `pipe`, `sigaction`, etc.) and
will not compile natively on Windows. On Windows, use WSL.
