# Lowent

> [!WARNING]
> **Lowent is under active development, and the language design is still changing.**
> Syntax, the standard library, diagnostic codes and command-line flags can change without notice. There is no compatibility promise yet, and it is not ready for production use.

[한국어](README.ko.md)

**Manual:** [read on the web](https://rubidus-api.github.io/lowent_lang/manual/html-en/index.html) · [PDF](https://rubidus-api.github.io/lowent_lang/manual/pdf-en/lowent-manual-en.pdf) · [한국어판 웹](https://rubidus-api.github.io/lowent_lang/manual/html-ko/index.html) · [한국어판 PDF](https://rubidus-api.github.io/lowent_lang/manual/pdf-ko/lowent-manual-ko.pdf)  
**Specification** (Korean): [web](https://rubidus-api.github.io/lowent_lang/spec/html/) · [PDF](https://rubidus-api.github.io/lowent_lang/spec/pdf/lowent-spec.pdf)

Lowent is a **low-entropy** systems programming language. The idea is simple: you should be able to read a function's signature and know what it takes, what it returns, and what it is allowed to do to the outside world. Code that leaves little to guess is code that people and AI can both read and change without breaking it.

```lowent
module stats .

rem Pure: no I/O, no allocation, no hidden state. The contract is checked.
fn mean input xs slice u8 . output u64 .
  requires gt (len xs) 0 .
do
  var total u64 be 0 .
  for x xs do
    set total (add total (widen u64 x)) .
  end
  return div total (len xs) .
end
```

```text
$ lowentc --run mean stats.low '[3,4,8]'
mean([3,4,8]) = 5
$ lowentc --run mean stats.low '[]'
  0:0 E-VM-CONTRACT: `requires` violated at entry — the caller broke the contract
```

Lowent keeps C's machine model: you still control layout, pointer width and what every line costs. What it drops are C's traps. In their place are four promises the compiler actually checks: the contract, the effect (what a function does to the world), the capability (the right to do it) and ownership. Each is explained below.

---

## The problem

Systems languages have been carrying the same baggage for decades.

1. **Code does things it never says.** In C, integers widen and narrow behind your back, and overflow is undefined behavior. To know what a single `+` does, you have to remember the promotion rules.
2. **Signatures don't tell the truth.** `int parse(const char *s)` might open a file, touch the network or grab memory from a global heap. Nothing in the signature says so, and the only way to find out is to read the whole body.
3. **Memory makes you pick your poison.** A garbage collector brings pauses and a runtime. Manual management brings leaks, double frees and use-after-free.
4. **Docs, tests and code drift apart.** A comment that says "must be positive" is checked by no one, and it quietly turns false the day the code changes.
5. **Syntax asks you to guess.** Operator precedence, symbols whose meaning depends on context, three spellings for the same thing. People get used to it. AI models writing code trip over exactly these spots.

## How Lowent approaches it

### 1. Integers do only what you write

The only conversion that happens on its own is widening that can't lose a value, like `u8` into `i16`. Anything that could lose information, narrowing or a change of sign, has to be written out with `narrow` and its relatives, or the compiler refuses it. Overflow is never undefined: you choose what happens, whether that's stop (the default), wrap around (`wrap_*`) or clamp (`sat_*`).

```lowent
module overflow .

fn bump input a u8 . output u8 .
do
  return add a 1 .
end

fn bump_wrap input a u8 . output u8 .
do
  return wrap_add a 1 .
end

fn bump_sat input a u8 . output u8 .
do
  return sat_add a 1 .
end
```

```text
$ lowentc --run bump overflow.low 255
  0:0 E-VM-OVERFLOW: integer overflow at the declared width (use wrap_*/sat_*, or prove the range)
$ lowentc --run bump_wrap overflow.low 255
bump_wrap(255) = 0
$ lowentc --run bump_sat overflow.low 255
bump_sat(255) = 255
```

Where the compiler can prove a value stays in range, it removes the overflow check. Safety you can prove costs nothing at run time.

### 2. The signature is the contract

Functions come in two kinds. A `fn` is pure: same input, same output, no trace left behind. A `proc` may touch the world, but it has to declare its effect (what it does to the outside world: I/O, allocation, state) and receive a capability (the right to do that, passed in as a value) for each thing it touches. There is no global stdout and no global heap to reach for.

```lowent
module hello .

proc main input out cap io . output u8 . effects io .
do
  let n u64 be write_out out 1 "hello, entropy!\n" .
  return 0 .
end
```

A `fn` that sneaks in some I/O doesn't compile:

```lowent-거부: a pure fn does I/O · E-EFFECT-CALC
module leak .

fn shout input out cap io . output u64 .
do
  return write_out out 1 "hi\n" .
end
```

```text
$ lowentc --check leak.low
  leak.low:4:1 E-EFFECT-CALC: this fn is declared pure but performs `io` — make it a `proc` with `effects …`, or remove the effect
```

So the header alone tells you "this never touches the network" or "this never allocates." Code review, security audits and AI-assisted edits can all start from the signature instead of the whole body.

### 3. Memory safety without a GC or manual frees

Lifetimes come from regions (memory released all at once when a block ends) and ownership. Borrowing follows one rule the compiler enforces: one writer or many readers. References that escape, overlapping writes and use-after-move are all compile-time errors.

Lowent goes one step further: a value whose cleanup **can fail** is never cleaned up silently. Closing a file can fail (a full disk can refuse the last buffer), so a program that opens a file and never closes it doesn't compile.

```lowent-거부: a file is opened and never closed · E-OWN-INCOMPLETE
module forgot .

use files .

proc leak input fs cap file_system . output u8 . effects io .
do
  let o result files.handle files.file_error be files.open fs "notes.txt" 0 .
  guard is_ok o . else return 1 .
  var h owned files.handle be ok_value o .
  return 0 .
end
```

For small boards with no operating system, there is a separate path that allocates only from a fixed window that never grows. Build for such a target and any code that needs a heap is rejected.

### 4. Contracts are documentation, checks and optimization hints at once

You write `requires` (what the caller must guarantee), `ensures` (what the function promises on return) and `errors` (which failures happen, and when) right in the signature. If a value is known at compile time, the check happens then; otherwise it runs at the boundary. A proven contract removes the checks inside the body, and a contract that checks nothing at all is rejected. A written promise gets no chance to go stale.

### 5. One meaning, one spelling

Every statement starts with a word, takes its operands in prefix order and ends with a period: `add total x`, not `total + x`. Long arithmetic can go infix inside `expr`, with exactly the same meaning. There are **43 keywords**, and the set is closed. No second spelling for the same thing.

This buys some unusual properties. The whole grammar can be highlighted with plain regular expressions, so an editor's highlighter is exact rather than approximate. There's almost no punctuation, so it's comfortable to type even on a phone keyboard. Every diagnostic carries a stable code, and `--diag-json` emits one JSON line per diagnostic for tools and AI agents.

## Design principles

- **Lean.** A small, orthogonal core with a generous standard library around it. Every new feature has to answer one question: does it follow from the existing principles, or does it add a new axis? If it adds a new axis, it stays out.
- **Costs are visible.** No hidden allocation, no hidden control flow, no hidden concurrency. `--why-slow` tells you which functions are stuck on the slow path, and why.
- **Local reasoning.** You should be able to understand, write and verify a function from its signature and contract alone.
- **Two back ends keep each other honest.** The compiler runs the same intermediate representation on a VM and also emits C. If the two ever disagree, that's a compiler bug, not yours. Every compiler change is checked against tens of thousands of cases this way.
- **Built for real hardware.** Targets go down to boards with 16-bit pointers and no heap. Portable SIMD, inline assembly, MMIO registers, interrupt handlers and a two-way C FFI are part of the language.
- **Unfinished features say so.** A feature whose name is reserved but whose meaning isn't implemented yet raises `W-NOT-YET` instead of being silently ignored.

## Where it stands today

Lowent is not just a design document. It grows together with a working compiler (compiler 1.3.0, language revision 1.3).

- **The compiler, `lowentc`,** is written in C23. Its only external dependency is one vendored library (`proven_c_lib`, MIT). Native code goes out as C and is built by your system's C compiler. Targets: `x86_64`, `arm64`, `riscv64`, `cortex_m` and `mips_be`.
- **Caught at compile time:** effect and capability violations, borrow and lifetime errors, use-after-move, unfinished resources, integer conversions that could lose a value, and contradictory or dead contracts.
- **Caught at run time:** bounds checks where nothing was proven, overflow, and contract violations. The VM and native code stop at the same place with the same diagnostic.
- **Proven:** memory safety for the sequential fragment (no use-after-free, no dangling references) and freedom from data races are machine-checked in Coq, with no axioms and no `admit`. These proofs are about the language's **model**. Whether the implementation follows the model is checked by the regression suite and bounded model checking. What has *not* been proven gets its own chapter in the manual.
- **Scale:** 2,065 regression checks and 64 standard library modules. Every example in the manual is run on both back ends and must produce the same output.

## What you give up

Lowent makes some deliberate trade-offs. You should know them before you pick it.

- **It looks unfamiliar.** Prefix notation and period-terminated statements feel odd at first. Writing `add a (mul b c)` instead of `a + b * c` takes some getting used to.
- **It's wordy.** Effects, capabilities and width conversions are all written out, so code runs longer than the equivalent C. In exchange, the reader has less to guess.
- **No inheritance, no lambdas, no exceptions.** Familiar tools are missing. The next section shows what to use instead.
- **No dynamic dispatch yet.** `dyn` is reserved but not implemented. Polymorphism is done with compile-time generics, which generate code for every combination used, so binaries can grow.
- **A small ecosystem.** There is no package registry or language server (LSP) yet, and few libraries beyond the standard one.
- **No compatibility promise.** A language revision may break existing code. The changelog and the formatter (`--fmt`) help with migration.

## Living without inheritance, lambdas and exceptions

| If you're used to | In Lowent you use | Why |
|---|---|---|
| Class inheritance | `enum`s that carry data plus `match`; **traits** (one promise kept by many types); functions attached to a type | You never climb a parent chain to find what something means, and `match` makes the compiler catch a missing case |
| Virtual methods | Compile-time generics, specialized for each type used | No vtables and no indirect calls; the cost is visible at the call site |
| Lambdas | Named functions passed to `pipe` | The name documents the intent, and nothing is captured behind your back |
| Exceptions | `result` and `option` return failure as a value; the `errors` clause lists failures in the signature | Failure never hides behind control flow, and unhandled failures show up |
| Garbage collection | Regions, ownership, borrowing and allocators | No pauses, no runtime, and you can see in the code when memory goes away |
| A global heap and global I/O | Capabilities passed in as arguments | What a function can touch is right there in its signature |
| Threads and locks | Tasks, channels, actors and parallel loops | The discipline carries the weight a memory model would otherwise put on you |

Here is what replaces inheritance in practice. First, an `enum` that carries data:

```lowent
module shapes .

enum shape do
  circle r u32 .
  rect w u32 h u32 .
  dot .
end

fn area input s shape . output u32 .
do
  match s do
    case circle r . do return mul 3 (mul r r) . end
    case rect w h . do return mul w h . end
    case dot . do return 0 . end
  end
end
```

When many types should keep the same promise, use a trait. Which function gets called is decided at compile time.

```lowent
module why .

trait shape do
  area input s self . output u64 .
end

struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end

fn rect.area input s rect . output u64 .
do
  return mul (field s w) (field s h) .
end

fn double_area input comptime t type . input s t . output u64 .
  requires shape t .
do
  return mul 2 (method s area) .
end
```

Instead of a lambda, give the condition a name and pass it to `pipe`. The data flows through once, with no intermediate arrays.

```lowent
module lambda_fixed .

fn over2 input a u8 . output bool .
do
  return gt a 2 .
end

fn count_big input xs slice u8 . output u64 .
do
  return pipe xs do
    filter over2 .
    count .
  end .
end
```

## Where it's headed

These items are on the plan today. No dates are promised.

- **Documentation:** rewriting the hardest chapters of the manual and the specification with plainer explanations and diagrams.
- **A more expressive `pipe`:** local functions that carry outside values with them, so more of today's `while` loops can become pipelines.
- **Dynamic dispatch and an optional runtime:** giving `dyn` a meaning, plus a small dynamic runtime that only programs asking for it pull in.
- **Async I/O:** adding io_uring and IOCP back ends and a work-stealing scheduler to today's reactor (poll, epoll and POSIX AIO).
- **Wider proofs:** extending the proofs to weak memory ordering (atomics).
- **A web edition:** making the manual and the specification readable directly in the browser.

## The goal

Where Lowent wants to end up is **code you can trust from its signature alone**: a language where a function's signature says everything the function can do, and the compiler stops you the moment that stops being true. Code like that can be read without guesswork, changed one function at a time by an AI without collateral damage, and run under the same rules from a tiny microcontroller up to a server.

Safety is stated in grades, never oversold: what is proven, what the compiler stops, what the runtime stops, and what nothing stops yet. The work of this project is to keep moving that line forward.

## Quick start

All you need is a C23 compiler (gcc or clang) and a POSIX shell. There are no other dependencies.

```sh
cd impl
make                  # → build/lowentc
make check            # regression suite

build/lowentc --check hello.low              # check only
build/lowentc --run main hello.low           # run on the VM
build/lowentc --emit-c hello.low > hello.c   # emit C for a native build
cc -O2 -o hello hello.c -lm -lpthread && ./hello main
```

For a project, put a `pkg.low` manifest at the root and use `lowentc run` and `lowentc build`. Neither will run or build a program that fails the checks.

## Learn more

| Where | What |
|---|---|
| [`docs/manual/`](docs/manual/README.md) | **The Lowent manual**, a book in English and Korean: the language, the standard library (one page per module) and what has been proven, with its limits. Read it on the [web](https://rubidus-api.github.io/lowent_lang/manual/html-en/index.html) or as a [PDF](https://rubidus-api.github.io/lowent_lang/manual/pdf-en/lowent-manual-en.pdf) (Korean: [web](https://rubidus-api.github.io/lowent_lang/manual/html-ko/index.html) · [PDF](https://rubidus-api.github.io/lowent_lang/manual/pdf-ko/lowent-manual-ko.pdf)), or as Markdown on GitHub; every example is run on both back ends |
| [`docs/spec/canon/`](docs/spec/canon/) | The normative specification (in Korean), also as [web](https://rubidus-api.github.io/lowent_lang/spec/html/) and [PDF](https://rubidus-api.github.io/lowent_lang/spec/pdf/lowent-spec.pdf). When the manual and the specification disagree, the specification wins |
| [`docs/example/`](docs/example/) | Complete examples |
| [`impl/`](impl/README.md) | The compiler, `lowentc` (C23) |
| [`lib/`](lib/) | The standard library, written in Lowent |
| [`skills/`](skills/) | Guides for AI agents writing Lowent |

## License

MIT, see [`LICENSE`](LICENSE). The vendored [`proven_c_lib`](impl/vendor/proven/VENDORED.md) is MIT as well.
