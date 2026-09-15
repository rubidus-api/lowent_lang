#import "../../typst-ko/lib.typ": *

= A first program --- build, run, get rejected

#chapter-toc()

#prereq(
  ([#chref("intro"), What Lowent sets out to do], [knowing what an op can do from its head alone]),
)

#deepqa[
  What did #chref("intro") read from the head `proc main input out cap io . output u8 . effects io .`? And what did it read from
  *what was not written*?
][
  That the op is impure (`proc`), receives an `io` capability under the name `out`, returns a `u8`, and performs the `io` effect.
  From what was not written: the op does not allocate, has no file-system capability and starts no threads. This chapter actually
  builds and runs a program with that head.
]

#why[
  The first step in learning a language is knowing *what the tool in your hand does for you*. Lowent's tool, `lowentc`, has no
  default mode, runs the same program two ways (VM and native), and answers wrong programs with diagnostics that carry codes. With
  these three in your fingers before the grammar, you can run and get rejected by every new rule in later chapters yourself. That is
  why this chapter comes before the description of the surface syntax (#chref("surface")).
]

#organizer[
  You will build `lowentc` and run the smallest program once on the VM and once natively. You will see why the returned value
  becomes the exit code, how a contract stops execution, and how to run `test` blocks. You will also learn how to read the
  diagnostics the compiler produces when it rejects a program, and how far `--fmt` will fix the shape for you.
]

#chapter-questions()

== Building the tool

The compiler is called `lowentc`. After getting the repository, build it in `impl/`. All you need is a C compiler that knows C23
(gcc or clang) and `make`.

```sh
cd impl
make            # build/lowentc
make check      # unit tests and standard-library checks
```

When `make` finishes there is an `impl/build/lowentc`. Commands in this book are written as `lowentc`, assuming it is on your path.

#dtable(
  columns: 2,
  id: "first-modes",
  caption: [Common `lowentc` modes],
  [*Mode*], [*What it does*],
  [`--check`], [Checks only (contracts, effects, ownership, capabilities). Emits nothing],
  [`--run OP args…`], [Runs one op on the VM. Slice arguments are given as `[3,4,8]`],
  [`--emit-c`], [Writes native C source to standard output],
  [`--test`], [Runs `test` blocks],
  [`--fmt`], [Prints the canonical form],
  [`--diag-json`], [Writes diagnostics as one JSON object per line (for tools and AI)],
)

#qa[
  Why is there no default mode? Couldn't `lowentc hello.low` just build it?
][
  It could, but then *what the command did* would not be visible on the command line. Whether it only checked, ran, or wrote files
  would have to be remembered. This is #chref("intro")'s "one meaning, one spelling" applied to the tool. If you want something
  shorter, subcommand names such as `lowentc check` and `lowentc run` do the same work.
]

== hello, entropy

The smallest program that prints.

#demo("examples/ch01/hello.low")

The first line, `module hello .`, is the module declaration every file opens with. The `rem` on the second line is a line comment;
in this book it tells the verification script what to do.

Read the head one clause at a time.

- `proc main` --- an impure op named `main`. Output leaves a trace outside, so it cannot be a `fn`.
- `input out cap io .` --- receives an `io` capability named `out`. It is needed to write to standard output.
- `output u8 .` --- returns a `u8`.
- `effects io .` --- declares that it performs input/output.

The body lies between `do` and `end`. `write_out out 1 "…"` takes the capability `out` as its first argument, writes bytes to file
descriptor `1` (standard output), and returns *the number of bytes written*. `hello, entropy!\n` is 16 bytes, so it returns 16. That
is narrowed to fit a `u8` with `narrow u8`. Narrowing carries a check that stops execution if the value does not fit
(#chref("numbers")).

The last line of the output, `main() = 16`, is the line where the VM shows the op's return value.

== Running it two ways

`--run` ran it on the virtual machine inside the compiler. To run the same program natively, emit C and build it with a C compiler.

```sh
lowentc --emit-c hello.low > hello.c
cc -O2 -o hello hello.c -lm
./hello main
```

The native executable takes the name of the op to run as its first argument. Running `main` prints the same line, and the return
value 16 comes out as *the process exit code* (see it with `echo $?`). That is why `main`'s `output` is `u8` --- an exit code is one
byte.

#realcase[When the two results differ, whose fault is it?][
  The VM and native code start from the same intermediate representation. If they produce different output, it is a defect in the
  *compiler*, not the program. The development repository runs hundreds of ops on both back ends with boundary-value arguments and
  compares them byte for byte, and this book's verification script also runs every example carrying `rem run:` twice and compares.
  One discrepancy surfaced while writing this book --- `arg`, which reads program arguments, counts positions differently on the VM
  and in native code. So examples that read arguments are only checked in this edition, and their run results are not shown.
]

Slice arguments are given in brackets. The next op computes the mean of a byte slice.

#demo("examples/ch01/stats.low")

`requires gt (len xs) 0 .` is a *contract*. The caller promises not to pass an empty slice. Because of that promise, the body's
`div total (len xs)` need not worry about dividing by zero. The line `arg0 (written) = [3,4,8]` below the result is the VM showing
the final state of the slice argument.

== How a contract stops execution

What happens if a contract is broken? The next op only accepts values up to 100.

#demo("examples/ch02/twice.low")

Give it a value that breaks the promise, as in `lowentc --run twice twice.low 200`, and the VM stops before entering the body with
`E-VM-CONTRACT: requires violated at entry`. If the argument is written as a constant in a call (`twice 250`), there is no need to run
at all --- `--check` rejects it with `E-CONTRACT-IMPOSSIBLE`. A contract is not a comment.

The `test twice_works` block in the same file is a test. `expect` is the assertion, and `--test` runs every `test` block. The last
two lines of output are its result. `--check` does not run tests --- passing the check does not mean the tests passed, so running
`--check` on a file with tests produces a `W-TEST-NOT-RUN` warning to say so.

#misconception[If `--check` is quiet, the program is correct][
  What `--check` guarantees stops at *the promises written in the head and the body not contradicting each other*. Promises that were
  not written cannot be checked, tests were not run, and contract violations that only show at run time only show when you run.
  "The check passed", "the tests passed" and "it is correct" are three different statements.
]

== Getting rejected

A good part of the time spent learning Lowent is time spent being rejected by the compiler. Rejection is not punishment but
conversation. Every diagnostic carries a stable code, and that code keeps its meaning across releases.

#demo("examples/ch01/twice_bad.low")

A diagnostic line has the shape `file:line:column code: explanation`. In this example `fn shout` is declared pure but its body does
input/output with `write_out`. The compiler rejects it with `E-EFFECT-CALC` and names two ways to fix it --- make it a `proc` with
`effects`, or remove the effect.

With `--diag-json` the same diagnostic comes out as one line of JSON. The rule code (`rule`), location (`span`) and the name of the
repair (`repair`) are separate fields, so an editor or an AI can tell what is wrong without picking the prose apart.

```text
{"rule":"E-EFFECT-CALC","sev":"error","phase":"effect", … ,"repair":"R-CALC-TO-PROC"}
```

#qa[
  Do I have to memorise diagnostic codes?
][
  No. Codes are names to search for and refer to. The appendices of this book collect the diagnostics you will meet often, and each
  chapter shows in the text the codes that come out when its rules are broken.
]

== `--fmt` fixes the shape

The clauses of an op head are written in one fixed order. Inputs come before the output. A wrong order is rejected.

#demo("examples/ch02/messy.low")

The long explanation in the diagnostic is the order table itself. The whole of it is covered in #chref("surface"); for now remember
one thing: `--fmt` moves *clauses that are not inputs* (output, effects, contracts and so on) into place. It does not reorder inputs
among themselves. The order of inputs is also the order of arguments at every call site, so moving them would change every call.

The canonical form `--fmt` prints puts one clause per line and shows every parenthesis. Nobody has to write in that shape. The
canonical form is the yardstick a machine uses to compare *whether two pieces of code mean the same*.

== Arguments and capabilities

Reading program arguments is a capability too. The next program greets its first argument.

#demo("examples/ch02/greet.low")

`input a cap args .` receives the argument capability, and `arg a 0` reads the first argument. There may be no argument, so the
result is `option slice u8`. `guard is_some who . else …` leaves on the spot when there is no value, and below it you may trust that
the value exists (#chref("option-result")). Capability inputs come before data inputs, and the two capabilities take argument
positions in the order written.

Note that the single line `effects io .` covers both capabilities. `args` is a capability that can be read without an effect. How
capabilities pair with effects is tabulated in #chref("capabilities").

#recap[
  `lowentc` always takes an explicit mode. `--run` runs the program on the VM, `--emit-c` plus a C compiler runs it natively, and the
  two must agree. `main`'s return value is the exit code. Contracts stop execution before running or on entry, and `test` blocks run
  only under `--test`. Rejections carry stable codes, and `--fmt` fixes clause order only for clauses that are not inputs.
]
