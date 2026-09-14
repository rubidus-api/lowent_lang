# Lowent

> [!WARNING]
> **Lowent is still under active development, and the language design is still changing.**
> Syntax, the standard library, diagnostic codes and command-line flags may change without notice.
> There is no compatibility guarantee yet, and it is not ready for production use.

**A low-entropy systems language.** C's machine model, without undefined behaviour, implicit
integer promotion or hand-managed lifetimes. In their place are four things the compiler
**checks**: the contract (the promises a function writes about its inputs and outputs), the effect
(what it does to the outside world: I/O, allocation, state), the capability (the right to do that,
passed in as a value) and ownership.

The goal is code that people and AI can read and write **without guessing**.

[한국어 README](README.ko.md) · [Manual (EN)](docs/manual/md-en/README.md) · [Specification](docs/spec/canon/)

---

## What it looks like

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

Every statement starts with a word, takes its operands in prefix order and ends with a dot
(`add total x`, not `total + x`; infix maths lives only inside an explicit `expr`). There is almost no
punctuation, so code reads top to bottom and types easily even on a phone keyboard. The whole language
has **43 keywords**.

## What it refuses

Side effects cannot hide. Output needs a capability passed in as a value, and the header has to say so:

```lowent
module hello .

proc main input out cap io . output u8 . effects io .
do
  let n u64 be write_out out 1 "hello, entropy!\n" .
  return 0 .
end
```

A `fn` that quietly does I/O does not compile:

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

A file that is opened and never closed does not compile either (`E-OWN-INCOMPLETE`), and a handle
used after `close` is `E-OWN-MOVED`. Every refusal has a stable code, and `--diag-json` emits one
JSON line per diagnostic for tools and agents.

## Why Lowent

- **Contracts are checked.** `requires` / `ensures` / `errors` sit in the signature. A violated
  contract stops at the boundary, a contract that checks nothing is refused, and a proven contract
  removes the run-time check.
- **Effects are enforced.** `fn` is pure; `proc` declares what it does (`io`, `alloc`, `state`,
  `wait`, …). Doing more than it declares is a compile error.
- **No ambient authority.** There is no global heap and no global I/O. Allocation, files and C calls
  all need a capability passed in as a value, so a signature tells you what a function can touch.
- **Ownership without a GC.** No garbage collector and no manual `free`. Escaping references,
  aliasing violations and use-after-move are rejected at compile time.
- **Integers without surprises.** No implicit promotion and no undefined overflow. Every change of
  width is written: `widen`, `narrow`, `wrap_*`, `sat_*`, `chk_*`.
- **Two back ends that must agree.** A VM runs the program and a C back end compiles it natively.
  They are tested against each other; if they disagree, that is a compiler bug.
- **Down to small boards.** Targets reach 16-bit pointers with no heap, with portable SIMD, inline
  asm, MMIO, interrupt handlers and a C FFI in both directions.
- **What is not done says so.** A feature that is accepted but not implemented yet warns
  (`W-NOT-YET`) instead of being silently ignored.

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

For a project, put a `pkg.low` manifest at the root and use `lowentc run` and `lowentc build`.
Neither of them runs or builds a program that fails the checks.

## Learn more

| Where | What |
|---|---|
| [`docs/manual/`](docs/manual/README.md) | **The Lowent manual**, a book in English and Korean: the language, the standard library (a page per module) and what has been proven. Web, PDF and Markdown. Every example is run on both back ends |
| [`docs/spec/canon/`](docs/spec/canon/) | The normative specification (Korean). When the manual and the specification disagree, the specification wins |
| [`docs/example/`](docs/example/) | Complete examples |
| [`impl/`](impl/README.md) | The compiler `lowentc`, written in C23 |
| [`lib/`](lib/) | The standard library, written in Lowent |
| [`skills/`](skills/) | Guides for AI agents writing Lowent |

## License

MIT, see [`LICENSE`](LICENSE). The vendored [`proven_c_lib`](impl/vendor/proven/VENDORED.md) is MIT too.
