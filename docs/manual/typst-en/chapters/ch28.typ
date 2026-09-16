#import "../../typst-ko/lib.typ": *

= Input, output and files

#chapter-toc()

#prereq(
  ([#chref("capabilities"), Capabilities], [`cap io`, `cap file_system` and `effects io`]),
  ([#chref("ownership"), Ownership], [values needing completion are finished explicitly]),
  ([#chref("errors-design"), Designing failure], [at the boundary, report with `result`]),
)

#deepqa[
  In #chref("ownership"), what decided that a type "needs completion"?
][
  The existence of an op that takes the type as `owned` and returns a `result`. Letting such a value go out of scope without finishing it was rejected with
  `E-OWN-INCOMPLETE`. This chapter shows where that rule is most useful --- files that must be closed once opened.
]

#why[
  Part VIII is about the places a program meets the outside world, and the first is input/output. It is where failure is common --- files are missing, disks
  fill up, reads get cut off. And the most common defects are mixing failure with *the end* and forgetting to close. Lowent uses capabilities to decide who may do
  input/output, ownership to make closing unforgettable, and the shape of answers to separate end from failure. Tools built up over the previous four parts come
  together here.
]

#organizer[
  You will learn to write to standard output with `write_out`, and to open, close, read and write files with the standard library's `files`. You will confirm that a
  file handle is an owned value needing completion, so forgetting to close it is refused at translation. You will also see why a read's answer splits three ways,
  "read · end · failure", and how to trigger failures on purpose to test them.
]

#chapter-questions()

== Standard output

The only channel to standard output is `write_out <cap io> <fd> <bytes>`. File descriptor 1 is standard output and 2 is standard error. It returns the number of
bytes written. It has already appeared in several chapters, so there is nothing new, but one point is worth making. That this op takes the capability as its *first
operand* means `cap io` shows in the head of every op that emits even one byte to standard output. There is no debug print slipped in secretly.

== Reading a whole file

#demo("examples/ch28/lines.low")

- `main` receives three capabilities: output (`cap io`), files (`cap file_system`) and allocation for the buffer (`cap allocator`). From the head alone you know this
  program does not touch the network.
- `files.slurp fs "notes.txt" buf` reads the whole file into `buf` and gives the number of bytes read as a `result u64 files.file_error`. If the file is missing or
  larger than the buffer, it is an error. It does not truncate.
- On error it writes a message and returns exit code 2 --- the shape of handling failure at the boundary (#chref("errors-design")).
- It cuts off the part read with `subslice`, counts line breaks with `pipe` (#chref("pipe")), and writes the contents to standard output as is.

The example reads `notes.txt` from its own folder. It has three lines, so the exit code is 3.

#qa[
  How do you read a file larger than the buffer?
][
  Open it with `files.open` and repeat `files.read`. `read` reads only as much as the buffer, so a file of any size can be read in pieces. A short read is not a
  failure; giving less than asked is normal, and the caller must keep reading. `slurp` does exactly that inside.
]

== What is opened is closed

#demo("examples/ch28/copy.low")

- `files.open fs <path> 1` opens for writing. Modes are 0 read, 1 write (truncate and create), 2 append.
- On success, `ok_value o` is put into an `owned files.handle`.
- It writes with `files.write` and closes with `files.close fs h`. `close` takes an `owned handle` and returns a `result`. Closing really can fail --- on network file
  systems or full disks, failing to flush the last buffer shows up at close.

Because `close` has that shape, `handle` is a type needing completion. Leaving it unclosed is rejected.

#demo("examples/ch28/forgot.low")

A program that forgets to close does not translate. It does not count on the operating system closing file descriptors when the process ends. If you really mean
to discard without closing, write `drop h .` to say so.

#misconception[Carrying a file handle as a plain integer is lighter][
  Holding the integer the kernel gave has the same run-time cost; `files.handle` is a struct holding one integer. The difference is what translation can know. An
  integer can be copied, forgotten or closed twice without anyone noticing. An owned value moves, is rejected when forgotten, and is rejected when closed twice.
  Rules are put onto the same bytes.
]

== End and failure are different answers

`files.read` answers in three positions.

#dtable(
  columns: 2,
  id: "io-read",
  caption: [The answers of `read`],
  [*Answer*], [*Meaning*],
  [`ok (some n)`], [`n` bytes were read],
  [`ok none`], [The *end* of the file. Not a failure],
  [`error e`], [A failure. `e` says which operation failed],
)

At one time this module's `slurp` just stopped its loop when a read failed. A failed read was then reported as "read the whole file", and a line-counting program
built on the module reported a read failure as the success "0 lines". The lesson was that one value must not carry two meanings (end and failure), and so the answer
became three positions.

Nor were three positions mechanically put on every op. Opening has no "end", so `open` is two positions, `result handle file_error`. The reader in the `io` module,
which reads slices in memory, has nowhere to fail, so it uses only `option`. The honest shape differs per op.

== Triggering failure on purpose

File failures rarely happen normally, so code that handles them tends to stay untested. The standard library's host operations have a fault injector, switched on
with an environment variable.

```text
LOW_HOST_FAULT="open:err"        every open fails
LOW_HOST_FAULT="read:err@2"      the second read fails
LOW_HOST_FAULT="read:short@1=4"  the first read is cut to 4 bytes
LOW_HOST_FAULT="close:err"       closing fails
```

The VM and native code use the same injector, so the two back ends must agree. Running `LOW_HOST_FAULT="open:err" lowentc --run main lines.low` sends `lines.low`
down its error branch, writes the message and returns 2. It is off by default.

#realcase[A defect found by a line-counting program][
  The injector made the "0 lines" defect above visible. Failing the first read with `LOW_HOST_FAULT="read:err@1"` made the line count finish successfully. Had the
  failure path never run, this defect would have stayed hidden until a real disk error. If capabilities and ownership stop "forgetting to close" at translation, the
  injector reveals "handling failure wrongly" at run time.
]

== Common mistakes

#antipattern[Opening in read mode and writing --- without checking the byte count][
  #demo("examples/ch28/mistake_readmode.low")

  `files.open fs "notes.txt" 0` is read mode. Writing there makes `files.write` return a failure --- a short write with the stream's error
  flag set is a failure, not a value --- and this example leaves with exit code 2. The same spot used to answer `ok 0` ("0 bytes written"),
  so code that asked only `is_ok w` passed it as a success. Check the mode (0 read · 1 write · 2 append), and look at *the number of bytes
  written* as well as success. A short write means the remaining bytes must be written again.
]

#antipattern[Writing with a handle that was already closed][
  #demo("examples/ch28/mistake_afterclose.low")

  `files.close` takes an `owned handle`, so ownership passes the moment it is called. Afterwards `h` is a handle that no longer exists, so
  this is `E-OWN-MOVED`. In C, `fwrite` after `fclose` is undefined behaviour, and if the same number was reused by a newly opened file, the
  bytes land in the wrong file. Ownership stops that at translation time.
]

#antipattern[Asking only about failure in the answer of a read][
  #demo("examples/ch28/mistake_eof.low")

  The answer of `files.read` has three places. `is_ok r` only says "not a failure". The end-of-file `ok none` is not a failure either, so it
  passes, and taking `some_value` out of it stops the program. Split all three places.

  #demo("examples/ch28/eof_fixed.low")

  It reads the 34-byte file in three pieces with a 16-byte buffer and stops at the `ok none` of the fourth read. On failure it closes and
  returns 3; if closing fails, it returns 4. The bound on `rounds` makes the loop end even for a source that never reports its end.
]

#misconception[`slurp` of a file larger than the buffer reads just the beginning][
  #demo("examples/ch28/slurp_small.low")

  `slurp` of a 34-byte file into an 8-byte buffer returns an error, not the first 8 bytes. This prevents the bug of trusting truncated content
  as the whole file --- the tail of a configuration file silently disappearing. If a file may be larger than the buffer, read it in pieces
  with `files.open` and `files.read`.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "io-files-glance",
  caption: [I/O and file syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`write_out out 1 "…"`], [write to standard output (1) · standard error (2) --- returns the count], [the capability comes first --- no hidden printing],
  [`use files .` + `input fs cap file_system .`], [the file module and its capability], [the head shows whether files are reached],
  [`files.open fs "notes.txt" 0`], [open --- 0 read · 1 write · 2 append · `result handle file_error`], [opening has no "end" --- two places],
  [`var h owned files.handle be ok_value o .`], [hold the handle as owned], [forgetting it: `E-OWN-INCOMPLETE` · writing after close: `E-OWN-MOVED`],
  [`files.read fs h buf`], [`ok (some n)` read · `ok none` end · `error e` failure], [one value never carries two meanings],
  [`files.write fs h bytes`], [returns the count as a `result`], [writes can be short --- check the count],
  [`files.close fs h`], [takes `owned`, returns `result` --- completion], [closing can fail too],
  [`files.slurp fs path buf`], [read it whole --- an error if larger than the buffer], [never truncates],
  [`LOW_HOST_FAULT="read:err@2"`], [inject failures on purpose (environment variable)], [exercise the failure paths],
)

#recap[
  Standard output is `write_out <cap io> <fd> <bytes>`, with the capability as first operand. The `files` module takes `cap file_system` to open, close, read and write
  files. A file handle needs completion because `close` takes it `owned` and returns a `result`, so forgetting to close is refused at translation. `read` answers in
  three positions --- read, end, failure --- and failure paths are exercised on purpose with the `LOW_HOST_FAULT` injector.
]
