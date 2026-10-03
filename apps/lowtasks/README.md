# `lowtasks` — run tasks with dependencies, a bounded number at a time

```sh
lowtasks -j 4 < build.tasks
```

```
# build.tasks
gen:   -- python3 gen.py
lib:   gen -- cc -c lib.c
app:   gen -- cc -c app.c
link:  lib app -- cc -o app app.o lib.o
```

Each line names a task, the tasks it waits for, and the program to run with its arguments —
no shell in between. `lowtasks` starts every task whose dependencies have succeeded, in file
order, up to `-j N` at once, and prints each task's collected output when it finishes. If a task
fails, nothing new is started, the running tasks are waited for, and the tasks that depended on
the failure are reported as skipped.

The task list is read from standard input, so the same binary works where the file-system
leaves are not available: it builds and runs on Linux and on Windows (checked on Windows 11 with
`cmd /c` tasks). Cycles, unknown dependencies and duplicate names are refused before anything runs.

Details: [`doc/manual.md`](doc/manual.md).
