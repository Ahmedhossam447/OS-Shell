# sadafa

A small Unix shell I wrote in C for my Operating Systems course.

It does what you'd expect from a shell: reads a line, parses it, runs it.
Under the hood it uses `fork`, `execv`, pipes, file descriptors, and
`sigaction` directly — no libraries doing the heavy lifting.

## What it can do

- Runs external programs by searching `$PATH` manually (`execv`, not `execvp`)
- Built-ins: `cd`, `pwd`, `echo`, `history`, `exit`
- Input/output redirection with `<` and `>`
- Pipes between commands, as many stages as you want (`ls | grep .c | wc -l`)
- Background jobs with `&`, and it tells you when they finish
- Ctrl+C kills the running command but not the shell
- Ctrl+Z is ignored (no job control, but it won't break anything)
- Cleaner error handling with a consistent `sadafa: ...` format
- Detects overly long input lines and discards them safely

## Building

You need `gcc` and `make`. On Windows, use WSL — this won't compile natively
because it uses POSIX syscalls.

```bash
make
./myShell
```

That's it. `make clean` if you want to wipe the build artifacts.

## What it looks like

```text
sadafa> pwd
/home/ahmed/os-shell

sadafa> echo hello world
hello world

sadafa> ls | grep .c | wc -l
2

sadafa> sleep 3 &
[bg] 12345
sadafa> pwd
/home/ahmed/os-shell
sadafa> [done] 12345 exit=0

sadafa> cat < myShell.c > /tmp/copy.c

sadafa> history
    1  pwd
    2  echo hello world
    3  ls | grep .c | wc -l

sadafa> exit
```

## Error handling

Errors are printed in a consistent style so they are easy to spot and debug:

```text
sadafa: cd: No such file or directory
sadafa: does-not-exist: No such file or directory
sadafa: not_a_real_command: command not found
```

If the user types a line longer than the input buffer, the shell now rejects
that line, flushes the remaining characters, and keeps running normally:

```text
sadafa: input too long (max 1023 chars); line discarded
```

## Files

```
myShell.c    main loop, exec, pipes, redirection, signals
vector.h     dynamic array header
vector.c     dynamic array implementation
Makefile
```

The `Vector` type is a simple growable array of strings. It's used for
tokens and for command history, which saves re-writing the same
malloc/realloc logic twice.

## A few notes on the design

**PATH lookup is manual** — splits `$PATH` on `:`, tries each directory
with `access(X_OK)`, runs the first hit with `execv`. `execvp` would do
the same thing in one call; I went with the manual version to actually
see how the lookup works.

**Pipes** are set up left-to-right. Each stage's stdout goes into the
next stage's stdin via a `pipe()` + `dup2()`. The shell waits for all
stages before returning to the prompt (unless the whole thing was
backgrounded with `&`).

**Built-ins inside pipes** are a slight annoyance — `cd` in a pipe is
meaningless (the child process can't change the parent's directory), so
in that case the child just exits. `pwd`, `echo`, and `history` still work
inside pipes since they only produce output.

**Signal handling**: the shell installs its own handler for `SIGINT` that
just writes a newline and re-prints the prompt. `SA_RESTART` is set so
`fgets` doesn't get interrupted. Children reset `SIGINT` to default before
`exec`, unless they're backgrounded — in which case they keep ignoring it.
