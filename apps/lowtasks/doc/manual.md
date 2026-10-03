# `lowtasks` manual

## Usage

```
lowtasks [-j N] < tasks.txt
```

`N` is 1 to 32 (default 2).

## Build

```sh
impl/build/lowentc --emit-c apps/lowtasks/src/lowtasks.low > /tmp/lowtasks.c
cc -O2 -o /tmp/lowtasks /tmp/lowtasks.c -lm                     # Linux
impl/build/lowentc --target win64 --emit-c apps/lowtasks/src/lowtasks.low > lowtasks_win.c
x86_64-w64-mingw32-gcc -O2 -o lowtasks.exe lowtasks_win.c -lm    # Windows (cross)
```

## The task list

One task per line:

```
<name>: <dependency>... -- <program> <argument>...
```

- Names are `[A-Za-z0-9_.-]+` and must be unique. Dependencies must name tasks in the list.
- Arguments are split on spaces and tabs. Wrap an argument in double quotes to keep spaces;
  inside quotes `\"` is a quote and `\\` a backslash.
- The program is run directly (looked up in `PATH`), not through a shell. Use `sh -c "…"` or
  `cmd /c "…"` explicitly when you want one.
- Lines that are empty or start with `#` are ignored. CRLF line ends are accepted.

Before running anything, the list is checked: a malformed line, a bad or duplicate name, an
unknown dependency or a dependency cycle stops with code 1 and runs nothing.

## Running

- A task starts when all its dependencies have succeeded (exit code 0). Among ready tasks, file
  order decides; at most `N` run at once. Completion order is not fixed.
- Standard input of a task is empty; its standard output and standard error are collected
  together, up to 64 KiB per task. When the task ends, `lowtasks` prints:

  ```
  == <name>: exit <code>
  <collected output>
  == (<n> more bytes of output were not kept)      (only if it overflowed)
  ```

  `signal <n>` replaces `exit <code>` for a task killed by a signal (POSIX). A program that cannot
  be started prints `== <name>: could not start` and counts as a failure.
- After a failure, no new task is started. Running tasks are waited for; every task that had not
  started is printed as `== <name>: skipped`.
- `lowtasks` does not kill grandchildren: a task that starts background processes may leave them
  running (RFC-0136 §6).

## Exit codes

| code | meaning |
|---|---|
| 0 | every task succeeded |
| 1 | a task failed or could not start, or the task list was invalid |
| 2 | usage |
| 3 | a limit was reached |
| 70 | the buffers could not be allocated |

## Limits

256 tasks · 2,048 dependencies · 256 KiB of task list · 256 KiB of arguments · 64 KiB of kept
output per task · 32 running tasks. About 17 MiB is allocated once.
