#import "../../book/lib.typ": *

= Meeting C

#chapter-toc()

#prereq(
  ([#chref("capabilities"), Capabilities], [`cap c` is a capability the entry point cannot receive]),
  ([#chref("contracts"), Contracts], [`requires` is checked on entry]),
  ([#chref("modules"), Modules], [only `export`ed ops become symbols C can call]),
)

#deepqa[
  Why did #chref("capabilities") say `cap c` is a capability the entry point cannot ask for?
][
  Because it is not a capability the runner (the operating system) can hand over. The door into C must be made somewhere entitled to give it and flow in as an
  argument. This chapter covers that door --- `extern` --- in both directions, going out and coming in.
]

#why[
  For a new language to be used in practice, it must meet existing C code. Operating system APIs, decades-old libraries and hardware vendors' SDKs are all C. But
  the moment control passes into C, everything this language kept --- bounds checks, ownership, effects --- can break on the C side. Lowent does not make this place
  comfortable; it makes it *narrow and visible*. And in the other direction, where C calls Lowent, it puts contracts at the door to guard the inside.
]

#organizer[
  You will learn the three things an `extern` op calling a C function must have (the `unsafe` mark, `cap c` and an effects line) and the `link` clause. You will see
  that types crossing the boundary are limited to what the C ABI can express, and that a slice becomes a pointer and a length. You will put `export`ed ops into a C
  program with `--emit-h` and `--no-main` and watch a C caller that breaks a contract stop at the door. You will also see the rules for callbacks and handing over
  ownership.
]

#chapter-questions()

== Calling C --- mark, right, effects line

#demo("examples/ch29/area.low")

An op that calls a C function has *all* three.

#dtable(
  columns: 3,
  id: "ffi-three",
  caption: [What an op calling C must have],
  [*Must have*], [*Without it*], [*Whom it tells*],
  [the `unsafe` mark], [`E-FFI-NOUNSAFE`], [The person --- from here on a person, not the language, is responsible],
  [`input k cap c .`], [`E-FFI-NOCAP`], [The processor --- entering C is a right handed over],
  [an effects line (at least `effects unsafe`)], [`E-FFI-NOEFFECT`], [The caller --- it learns from the head what it takes on],
)

#idx("extern")
An `extern` op's body is in C, so instead of `do` it names the C side with `link "lw_c_area"`. The processor does not make the name up from the op's name. It is
a promise made to another language, so whoever promises writes it. `area_twice` calls that op, so it is itself `unsafe` and receives `cap c`. The mark and the right
travel up the call chain.

#demo("examples/ch29/nounsafe.low")

#demo("examples/ch29/nocap.low")

With only one of the three, the call "could work". But then this language could not say what it has given up.

#qa[
  Inside an op marked `unsafe`, may you do anything?
][
  No. `unsafe` does not mean "anything goes"; it marks that "part of this place cannot be checked by the processor". In the body of an `unsafe` op, bounds checks,
  ownership and borrowing rules still apply. What cannot be checked is *inside* the C function. The mark stays in the source, so the places to audit can be found
  later.
]

== Types that cross the boundary

Only what the C ABI can express crosses the boundary. `option`, `result`, vectors and containers do not exist in C, and the processor does not pretend they do.

#demo("examples/ch29/fftype.low")

*Only slices* are mapped automatically. `slice τ` becomes two arguments, "a pointer to `τ`" and "a count". The width is known too --- `slice u32` is `uint32_t *`, not
a byte pointer. Capabilities are not values, so they do not cross into C. There is one ABI name, `c`. "What C is on this machine" is already decided by the machine
building it.

== C calls Lowent

The other direction. An `export`ed op becomes a symbol callable from C.

#demo("examples/ch29/exported.low")

`--emit-h` emits the header. A header written by hand keeps signatures in two places, and one day they diverge.

```c
long long clamp_add(long long, long long);
long long sum_bytes(const unsigned char *, size_t);
```

You can see `sum_bytes`'s `slice u8` split into a pointer and a length. `--no-main` emits C holding only the entry points of exported ops, without `main` and the
command-line dispatcher, ready to drop into someone else's build. The following C program uses the two.

#raw(read("/examples/ch29/host.c"), lang: "c", block: true)

#raw(read("/build/examples-out/ch29/host.c.out"), block: true)

The first two calls get answers. The third breaks `requires le a 1000 .`. C does not know contracts, but *the place being called into is this side's door*, so the op's
contract is enforced on the arguments and it stops on entry. Lowent answers for inside the door, C for outside.

#misconception[At the FFI boundary every guarantee of the language disappears][
  Going *out* to C, the inside of the C function is outside the checks. But that fact is written in the head with `unsafe`, `cap c` and an effects line. Where C comes
  *in*, the guarantees stand as they are. Incoming arguments that break a contract stop on entry. The boundary is not a hole but a door, and a contract stands at the
  door.
]

== Callbacks and ownership

To let C call a Lowent function back, take the address of an `export extern` op with `unsafe_fn <op>` and pass it as a value. There is no new convention. Two rules
apply.

- `unsafe_fn` may point only at `export extern` ops (`E-FN-NOTEXPORT`). The address of a door C cannot call is not an address.
- An op used as a callback cannot require capabilities (`E-FN-CAP`). It is C that enters the callback, and C has no capabilities to hand over. Work needing
  capabilities is done outside the callback; inside, it only computes.

Passing `owned τ` to an `extern` moves the responsibility to dispose of it to C. This side's obligation ends there, and whether it is later released is not verified.
C functions taking an unfixed number of arguments after the fixed ones can be called with a `variadic .` clause, but contracts do not reach those arguments, and the
opposite direction (C calling our variadics) does not exist.

#realcase[The load carried by a program that adds two numbers][
  The default emission is a *program*. `main` and the command-line dispatcher come along, and the dispatcher holds the tagged path and its pools. In the development
  repository's measurement, emitting one op that adds two numbers by default gave about 197 KB of read-only data and about 1.7 MB of uninitialised data, while
  `--no-main` brought both under 50 bytes. That is why `--no-main` is used when embedding as a library. Targets without an operating system (`--target cortex_m`) do
  not emit the dispatcher in the first place.
]

#recap[
  An `extern` op calling C has all of `unsafe`, `cap c` and an effects line, and names the C symbol with `link`. Only what the C ABI can express crosses the boundary,
  and a slice becomes a pointer and a length. `export`ed ops go into C with `--emit-h` and `--no-main`, and a C caller breaking a contract stops at the door. Callbacks
  use the address of an `export extern` op without capabilities, and passing `owned` hands responsibility to C.
]
