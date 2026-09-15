#import "../../typst-ko/lib.typ": *

= Building and testing — packages, configuration, tests, cross-checks

#chapter-toc()

#prereq(
  ([#chref("first-program"), A first program], [the modes of `lowentc` and its two back ends]),
  ([#chref("modules"), Modules], [there is no search path]),
  ([#chref("tasks-channels"), Tasks and channels], [`schedule explore_interleavings` tests]),
)

#deepqa[
  In #chref("first-program"), whose defect was it when the VM and native code give different output? And what does a quiet `--check` *not* guarantee?
][
  The compiler's, because the two back ends start from the same intermediate representation. A quiet `--check` goes only as far as the head's promises and the body
  not disagreeing; it does not guarantee that tests pass or that the program is right. This chapter covers the tools filling the gap --- tests, configuration and
  cross-checks --- and how to build a project.
]

#why[
  This is the last chapter of Part VIII. Earlier chapters ran `--check` and `--run` on one file. Real projects have a manifest, configurations that differ per build,
  tests and dependencies. Lowent's tools apply the same principle to all of them --- what is written is checked, defaults are visible in the source, and nothing
  changes quietly. And the compiler itself is verified by the same principle.
]

#organizer[
  You will learn to build a project with the `pkg.low` manifest and `lowentc run` and `build`. You will see that `build option` and `config` build different programs
  per build while switched-off branches are still checked. You will also see what `test` blocks look like when they pass and fail, how cross-checking the two back ends
  and contracts verifies the compiler, and how dependencies are pinned by content hash.
]

#chapter-questions()

== The manifest and the project

#idx("pkg.low")
Put `pkg.low` at the project root and the entry point in `src/main.low`.

```lowent
package name "greeter" .
package version "0.1.0" .
package license "MIT" .
```

The manifest is a few flat declarations. The keys are a closed set and `version` is checked against semantic versioning. This file exists so that tools and people can
know a project's identity from it alone without reading all the source, so here too what is written is checked. If it cannot be trusted when read, it may as well not
exist.

```text
$ lowentc run
hello from a package
main() = 0
$ lowentc build
built …/out/greeter
$ ./out/greeter main
hello from a package
```

`lowentc run` finds the manifest and runs the entry point on the VM, and `lowentc build` emits C and builds an executable in `out/`. Builds are cached by the content
hash of the emitted C, so unchanged code is not compiled again. The manifest is found by walking upwards from the first file's folder.

== Dependencies are pinned by hash

`lowentc add <name> <place>` writes a dependency into the manifest *with a hash pin*. `--lock-write` records in a lock file what is being built against now, and
`--lock <file>` refuses to build if the bytes differ from that hash. Even with the same version number, changed bytes are a different dependency.

Checking authenticity is a separate layer. `lowentc key new`, `sign` and `verify` make and check ed25519 detached signatures. `verify` splits its answer into four
layers --- integrity (do hash and pin match), authenticity (signed with a trusted key), transport and access --- because green in one layer does not mean green in
another. The only environment variable the tool reads is `HOME`, and the precedence of settings (command line > project > user > global) is written in the help. This
is to reduce places where behaviour is changed secretly outside the source.

== A different program per build --- `build option` and `config`

#demo("examples/ch31/knobs.low")

#idx("build option")
`build option <name> <kind> …` declares a knob. There are three kinds: `bool`, `int` and `choice`. `config <name>` reads its value as a *translation-time constant*.
Without a configuration file, the declaration's default is used.

The same source is built with a different configuration. A configuration file is a set of `name value` lines.

```text
smp false
maxcpu 8
```

#demo("examples/ch31/knobs_small.low")

With `smp` switched off, `tick_rate` gives 100 and `cpus` gives 8. Once the value is fixed, switched-off branches do not remain in the output. The cost is zero.

There is a decisive difference from C's `#ifdef`. *Switched-off branches are still parsed and type-checked.* In C, code for combinations nobody switches on rots unread,
and in kernel-scale projects "that option combination does not even build" happens. Here what folds away is *code emission*, not checking.

A knob declared but read by no code is rejected.

#demo("examples/ch31/unused.low")

Such a knob appears in the configuration, a user switches it off, and nothing happens. It looks like a decision but is not one. A configuration file giving a value not
among the choices (`E-CONFIG-TYPE`) or naming a knob that does not exist (`E-CONFIG-UNDEF`) is rejected too.

#realcase[A knob that could not be switched off][
  The specification's example makes the `hz` knob depend on `smp` (`depends smp`). The rule is that if the dependent is on while what it depends on is off, no such
  build exists, so it is rejected. Running that example while writing this book, a `choice` knob always had a value and read as "on", so every configuration switching
  `smp` off was rejected with `E-CONFIG-DEPENDS`. The development repository's test looked only at running with that configuration, not at checking, and missed the
  discrepancy. That is why this chapter's example leaves out `depends`. The lesson that a green light says nothing when what the test sees differs from what users do
  applies here too.
]

== Tests

`expect` inside a `test <name> do … end` block is an assertion. `--test` runs every test.

#demo("examples/ch31/tests_clause.low")

When a test fails, it looks like this.

#demo("examples/ch31/failing.low")

`E-TEST-FAIL` is a different diagnostic from a contract violation. A contract violation is *code breaking its own promise*; a test failure is *a test saying the code is
wrong*. What to fix differs. Here `narrow_wrap` must become `narrow_sat`. And the processor does not optimise `expect` away. A vanished test is a test that did not run.

Concurrent code uses `test <name> schedule explore_interleavings do … end` to run every possible ordering of flows and see whether the answers agree
(#chref("tasks-channels")). When there are many cases, `limit <number>` sets a ceiling.

#qa[
  If there are tests, must contracts be written too?
][
  They catch different things. A test checks answers on inputs the author picked; a contract checks that promises hold on every call. And contracts become *material
  for making tests*. The development repository's contract cross-check tool uses `requires`, `ensures` and `errors` as the verdict without writing expected output
  separately, and generates boundary-value inputs. Write contracts honestly and tests grow for free.
]

== How the compiler is verified

#idx("back-end cross-check")
The proofs (Part X) are about a *model*. Whether the actual compiler follows that model must be confirmed separately. There is one principle --- *make the same thing in
two ways, and treat a different answer as a defect.*

#dtable(
  columns: 3,
  id: "build-oracles",
  caption: [Ways of checking the compiler against something],
  [*Method*], [*What is compared with what*], [*What it catches*],
  [Back-end cross-check], [VM run ↔ native run], [Places where the two back ends answer differently],
  [Contract cross-check], [Written contracts ↔ actual run results], [Places where contracts differ from the facts],
  [Analysis self-check], [Indices believed "safe" ↔ actual indices], [Places where the analysis is wrong (`E-VM-ANALYSIS`)],
  [Certificate recheck], [Grounds for removing a check ↔ an independent checker's arithmetic], [Places where rules were misapplied],
  [One-line changes], [A program ↔ the program with one fact removed], [Broken relations, even without knowing the right answer],
)

Such checks show that defects *exist* but cannot show that they *do not*. Two implementations giving the same answer are not thereby both right. Absence is the job of
proofs. What you can run yourself in the public repository is `make check` in `impl/`. It covers unit tests, checking the whole standard library, VM/native
cross-checks over many ops, and the diagnostic codes of programs that must be rejected, all at once. This book's verification script applies the same cross-check to
every example --- and while this book was written, that cross-check exposed several discrepancies in the compiler.

#misconception[A green light means it was checked][
  A green light speaks only about *what it saw*. An operation not on the cross-check list shows neither green nor red. So the development repository makes the
  cross-check *count* and report opcodes it has never seen. "The knob that could not be switched off" above is the same: the test looked at running, not checking, so it
  was green. What is not counted is not managed.
]

== Asking where it is slow

Native emission lowers ops by two paths. Ops whose types are settled lower naturally to C integers and arrays; the rest stay on a slow path running on tagged values.
`--why-slow` names the ops left on the slow path and why.

```text
$ lowentc --why-slow bounds.low
why-slow: 0 / 1 op(s) still on the tagged path
```

A model whose performance is not visible in signatures becomes knowledge outside the source. So the tool says it. `--no-fast` is a contrast switch lowering every op by
the slow path, used to see whether both paths give the same answer.

#recap[
  `pkg.low` is a checked manifest, and `lowentc run` and `build` run and build the project. Dependencies are pinned by content hash, and authenticity is checked
  separately by signatures. `build option` and `config` build different programs per build while switched-off branches are still checked, and unread knobs are
  rejected. A failing `test` block is a different diagnostic from a contract violation. The compiler is verified against two back ends, contracts, analysis,
  certificates and one-line changes, and `--why-slow` says where it is slow.
]
