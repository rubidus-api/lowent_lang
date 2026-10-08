#import "../../typst-ko/lib.typ": *

= Allocators and fixed memory

#chapter-toc()

#prereq(
  ([#chref("regions"), Regions], [the fixed window and the heap, the two roots]),
  ([#chref("capabilities"), Capabilities], [capabilities authorise by kind and cost nothing at run time]),
  ([#chref("effects"), Effects], [`via` lets a type argument decide allocation effects]),
)

#deepqa[
  In #chref("regions"), what was rejected when building for a machine without an operating system, and what could still be used?
][
  The `heap` effect, `cap heap` inputs and `region … heap` blocks were all rejected with `E-HEAP-NOHOST`. `alloc`, which carves from the fixed window that does
  not grow, could still be used. This chapter covers the *allocators* that hand out space on top of that fixed window, and on top of bytes someone else lent.
]

#why[
  A region is a single policy: "all at once when the block ends". Real programs want more policies. They want to take one big buffer and cut it from the front,
  to cut with alignment, to run the same code on the heap and on the fixed window. In Lowent an allocator is not a special language device but *an actor that
  satisfies a trait*. So user-made allocators and standard ones fit the same slot. As the last chapter of Part V on memory, it lays out the path of fixed memory
  all the way to machines without an operating system.
]

#organizer[
  You will learn that an allocator is made of three parts: a capability, a policy and state. You will use a bump allocator that cuts borrowed bytes, and confirm
  that running out of memory is a value. You will pick up how to take an allocator as a type parameter and hand it over with `using`, and the default allocators
  that carve straight from a root, with the rule for capability fields. You will also see how the linker sets the size of the fixed window on machines without an
  operating system, and `bit_cast`, which keeps the bits and changes only how they are read.
]

#chapter-questions()

== The three parts of an allocator

#idx("allocator")
#dtable(
  columns: 3,
  id: "fixed-layers",
  caption: [The three parts of an allocator],
  [*Layer*], [*When it exists*], [*What it is*],
  [Capability], [only at translation], [May it touch the root? --- `cap allocator` · `cap heap`],
  [Policy], [at translation (a type)], [By what rule does it carve? --- a *type* satisfying the `byte_allocator` trait],
  [State], [at run time], [The cursor and backing bytes --- a *value* of that type (an actor)],
)

The `byte_allocator` trait of the standard library's `allocs` requires three ops. `reserve n` cuts off `n` bytes and gives an `option`, `grow` enlarges the last
piece in place, and `used` answers how much has been used. An actor (#chref("actors")) is an object that holds its own state and is spoken to only with `send`.
Here it is enough to know that it is made with `spawn actor` and called with `send a reserve 3`.

== Cutting borrowed bytes

The simplest allocator is a *bump* allocator. It keeps one cursor, cuts off as much as requested, and pushes the cursor forward.

```text
    buf (8 bytes)
    [ a  a  a │ b  b │ ·  ·  · ]
      └ the piece reserve 3 gave
                └ the piece reserve 2 gave
                       ▲ cursor (used = 5) --- the next reserve cuts from here

asking reserve 4: only 3 bytes are left → none (running short is a value too)
```

#demo("examples/ch20/borrowed.low")

- `spawn actor allocs.bump_bytes` makes the allocator, and `send a init buf` hands it the bytes to cut. This allocator cannot make memory by itself --- the
  discipline of never allocating secretly.
- `send a reserve 3` cuts off 3 bytes. What comes back is not a copy but a slice pointing at part of the original, so `set (idx pv 0) 65 .` changes the first
  byte of the caller's `buf`. The argument `[65,0,…]` the VM shows is the trace.
- `send a reserve 99` gives `none` because there is not enough space. Not a trap. *Running out of memory is a value*, and the caller checks it.

This op's head has neither `cap allocator` nor `alloc`; its effect is only `state`. The caller lent the bytes, and the allocator merely hands them out. The
capability draws the line --- code without an allocation capability can still allocate fully on bytes someone gave it.

#qa[
  Can a bump allocator take back one piece?
][
  It takes back only the last piece (`release`, trait `freeing_allocator`). For any other piece it answers `false` and changes nothing. Pretending to accept an
  unknown piece would let a double return erase someone else's space. If the shape is taking and releasing over and over, `pool` with generational handles is
  the right tool (#chref("lib-alloc")).
]

== Swapping allocators

Code that uses an allocator need not know which implementation it is. It takes the allocator's *type* as a translation-time parameter and its *value* with a
`using` clause.

#demo("examples/ch20/generic.low")

- `input comptime a type .` is the allocator's type, fixed to a concrete type at translation (#chref("generics")).
#idx("using")
- `using al a .` receives the allocator value of that type under the name `al`. `using` is not an input. The caller does not write it in an argument position but
  in the binding, as `let n using b be u64 two_from .`.
- `requires allocs.byte_allocator a .` is the condition that `a` satisfies the trait (#chref("traits")).
- `effects state via a .` means the effects of the allocator's `reserve` are this op's effects.

Given `bump_bytes`, the same `two_from` uses 3 + 5 = 8; given `bump_aligned`, which aligns start positions to multiples of 8, the second piece starts at 8 and it
uses 13. Because the type is fixed at translation, there is no virtual function table and no indirect call. Swapping costs nothing at run time.

If no source is written, a default is chosen: the binding's `using`, the op's own `using` name, and otherwise the input or binding of matching type if there is
*only one*. With two or more it does not guess and asks you to write it with `E-ALLOC-AMBIGUOUS`. Defaults do not cross op boundaries --- an allocator from the
caller never flows in on its own, so *there is no global allocator*.

#dtable(
  columns: 3,
  id: "fixed-using-lattice",
  caption: [The order that picks an allocator source --- the first match from the top],
  [*Order*], [*Source*], [*Why here*],
  [1], [`using <name>` written on the binding], [what you write always wins],
  [2], [the *only* name of fitting type among this op's `using` clauses], [the allocator the op said it takes],
  [3], [the *only* input or binding of fitting type in this op], [one visible value leaves nothing to confuse],
  [none], [`E-ALLOC-NOSOURCE`], [no global allocator fills the gap],
  [two or more], [`E-ALLOC-AMBIGUOUS`], [no guessing; every candidate is named],
)

Fields are not counted. If an allocator hidden inside a struct were used silently, reading the code would not tell you which buffer shrinks.

With no source at all, the call is refused.

#demo("examples/ch20/nosource.low")

`caller` has no input, no spawned actor and no `using` clause. In another language a global heap would quietly step in here. Lowent asks you to take an
allocator as an input, create one here, or write a `using` clause. Because the call is refused, `caller` never really uses `state` either, so `W-EFFECT-OVER`
comes along. It goes away once the first error is fixed.

The other way round, writing `using` on a call that draws from no allocator is refused too.

#demo("examples/ch20/usingunused.low")

`twice` only doubles a number and takes no allocator. A choice that will never be used misleads the reader into thinking `twice` uses memory. So whatever
you write must be used.

== Default allocators that carve straight from a root

`allocs` also provides two allocators that carve straight from a root, under the same trait.

#dtable(
  columns: 3,
  id: "fixed-defaults",
  caption: [Default allocators],
  [*Actor*], [*State*], [*Effect of `reserve`*],
  [`fixed_bytes`], [`root cap allocator .` · amount used], [`alloc state` --- anywhere],
  [`heap_bytes`], [`root cap heap .` · amount used], [`heap state` --- only with an operating system],
)

#demo("examples/ch20/fixed.low")

`fixed_bytes`'s state has a *capability field*. A capability is not a run-time value, so the field has no size. Instead a rule comes with it: an actor with a
capability field can be spawned only *from an op that holds a capability of the same kind*.

#demo("examples/ch20/forge.low")

`sneaky` tried to spawn `heap_bytes` without receiving `cap heap`. If that were allowed, a heap could be conjured in one line where there is no capability.
Capabilities are handed over, not picked up.

Receive the capability and the same thing works.

#demo("examples/ch20/heapbytes.low")

`main` receives `cap heap` and writes `heap` in its effects line. So it may spawn `heap_bytes`, and it gets 100000 bytes, more than the fixed window
holds. When the heap runs short it chains another chunk. Built for a machine without an operating system, this file is refused with `E-HEAP-NOHOST`
(#chref("regions")).

== Growing and returning a piece --- `grow` and `release`

A bump allocator only moves forward. Even so, *the piece it handed out last* is safe to take back, because nobody has received a place after it.
`grow` enlarges that piece in place, and `release` (trait `freeing_allocator`) takes it back.

#demo("examples/ch20/growrelease.low")

#idx("same_slice")
- `send b grow pv 6` grows `pv` from 4 bytes to 6. What it grows is *the piece itself*, not a size. The implementation checks with the core op
  `same_slice a b` (same start address and same length?) that `pv` is exactly the bytes it just handed out. Pass someone else's buffer of the same length
  and the answer is `none`. Recognising a piece by size alone would let two containers overlap without a sound.
- After `qv` is handed out, `gv` is no longer the last piece. So `release gv` is `false` and changes nothing.
- `release qv` is `true`. The cursor goes back to 6, so `used` is 6. The answer 601 reads "used 6 · first answer false · second answer true".

Both ops are *an optimisation, not a promise*. If a piece cannot grow, the caller receives a new one and copies, and the answer must be the same. For
`fixed_bytes` and `heap_bytes`, which carve straight from a root, `grow` is always `none`. A root does not know whose piece came last.

== Taking a list from an allocator

A list written out as values (#chref("slices")) normally sits in the op's frame. When it is large, or must outlive the op,
take its bytes from an allocator you choose: write `using <allocator>` on the binding, and say with `else` what happens when the bytes do not come.

#demo("examples/ch20/litalloc.low")

- `var xs using bb be mut slice u64 lit array u64 4 … . else return 0 .` asks `bb` for 32 bytes and, if it gets them, fills them like
  any list and binds them to `xs`. If the allocator cannot give the bytes, control goes to `else`, which must leave (binding `else` is
  covered in #chref("option-result")).
- To carry the failure along as an `option`, write the whole type: `let big using bb be option mut slice u64 lit … .` --- then check it
  later with `guard` or `match`.
- Filling works as for a frame list: list the elements, or fill chosen cells with `do … end`.
- The bytes belong to the allocator, so their lifetime follows it. Bytes from the heap may be passed out of the block; bytes
  from a bump allocator backed by a frame array cannot leave that array's block (`E-LIT-ESCAPE`).
- Writing `using` with neither `else` nor `option`, or on a `lit vec`, is `E-LIT-USING`.

=== Given back when the block ends

When the allocator can take pieces back one by one (`freeing_allocator`), bytes taken with `using` are given back automatically
when the block that declared the name ends. The same happens when the block is left by `return`, or by `break` or `continue` in a loop.

#demo("examples/ch20/autorel.low")

- Each time the loop body runs, `t` takes 32 bytes, and they are given back when the body ends. So the loop runs ten times on 64
  backing bytes and `used` is 0 afterwards. Without the give-back, the third round would run out of bytes and go to `else`. The
  answer 45 is the sum of 0 to 9.
- Giving back is `send <allocator> release <piece>`. Whether the bytes are taken back is the allocator's policy --- `bump_bytes`
  takes back only the piece it gave last. So when one block takes two, the inner (later) one is given back first.
- After they are given back, the bytes belong to something else. Carrying them out of the block (`return t`, or storing them in a
  name further out) is `E-LIT-ESCAPE`.
- Bytes from an allocator that cannot take pieces back (`fixed_bytes`, `heap_bytes`) stay with the allocator.
- To give the bytes back before the block ends, write `drop t .`. The block end then does not give them back again. Using `t` after
  the `drop` is `E-OWN-MOVED`, and a `drop` in an inner block (one side of an `if`, say) is `E-OWN-JOIN`.
- Nothing is given back when the program stops with `panic`.

To use the bytes longer than the block, write `keep` after `using`. Then nothing is given back at the end of the block, and the
bytes live as long as the allocator. A helper can hand the buffer it built to its caller.

#demo("examples/ch20/keepbuf.low")

- The `s` that `make_buf` returns is `bb`'s bytes. It can be used while `bb` lives; using it longer is refused at translation.
- Giving the bytes back is up to you: `send bb release …` gives a piece back, or everything goes back when the allocator ends.
  `used` is 16, so the answer is 1608.
- `keep` goes right after `using <allocator>`, on a binding that takes a list or struct literal. Anywhere else it is
  `E-USING-FORM`.

=== Structs in an allocator too

A struct value is built in an allocator's bytes with the same spelling. The binding's type is that struct.

#demo("examples/ch20/structalloc.low")

- `var q using bb be pt lit pt do … end . else return 0 .` asks `bb` for `size_of pt` bytes (16 here). If it gets them, it fills
  them with zeros, lays the struct's layout over them as `view` does, and writes the fields you gave. `used` is 16 while `q` lives,
  so the answer is 1607.
- Fields are read and written as in any struct (`field q y`, `set (field q y) …`). The bytes are given back when the block ends, as
  in the previous section.
- Only a struct with a byte layout is accepted --- every field must be a sized number. A struct with a slice, `owned` or array field
  has no representation in allocator bytes yet and is `E-LIT-UNBUILT`.

== Where the three layers sit in the standard library

#dtable(
  columns: 4,
  id: "fixed-axes",
  caption: [Allocation-related modules --- which of capability, policy and state each one carries],
  [*Module · name*], [*Capability*], [*Policy (type)*], [*State (value)*],
  [`allocs.bump_bytes` · `bump_aligned`], [none --- borrowed bytes], [an actor with `byte_allocator` · `freeing_allocator`], [backing bytes · cursor],
  [`allocs.fixed_bytes` · `heap_bytes`], [capability field `cap allocator` · `cap heap`], [`byte_allocator`], [amount used],
  [`vecgen.vec t a`], [none], [takes the allocator type `a` as a parameter], [element count · buffer --- `open` takes the allocator with `using`],
  [`growvec.gvec`], [none], [a name fixed to `vecgen.vec u8 allocs.bump_bytes`], [same as `vecgen`],
  [`pool.block_pool b`], [none --- borrowed bytes], [not an allocator --- a struct that takes blocks back one by one through generation handles], [blocks · generation array],
)

Only the two actors that reach a root hold a capability. Everything else works on bytes someone handed over. So library code that received no
capability can still build and grow containers, and to find the code that brings new memory into the program you only look at the ops that
received a capability.

== Who sets the size of the fixed window?

On a machine without an operating system, the fixed window is the memory between two symbols set by the linker. Its size is not baked into the executable. The
compiler emits a linker-script fragment, and only the size in that fragment changes per board.

```text
$ lowentc --emit-ldscript --fixed-bytes 4096 fixed.low
.lw_fixed (NOLOAD) : ALIGN(8)
{
    __lw_fixed_start = .;
    . = . + 4096;
    __lw_fixed_end = .;
}
```

To imitate another board on the host, give the VM a window size. `fixed.low` asks for 64 bytes twice, so shrinking the window to 100 bytes with
`lowentc --fixed-bytes 100 --run main fixed.low` makes the second `reserve` return `none` and prints `main() = 2`. You can see on your development machine where a
small machine runs out of memory.

#misconception[Embedded code does not allocate dynamically, so it needs no allocator][
  The real meaning of "no dynamic allocation" is not to use *a growing heap* and *allocation that hides its failures*. Carving from a window fixed once at boot,
  returning shortage as a value, and rewinding all at once do not conflict with that discipline. Lowent's fixed window has its size written by the linker,
  shortage is `none`, and the heap effect is blocked at translation. The grammar keeps the rule.
]

== Same bits, different reading

#idx("bit_cast")
Reading the same bytes as a different type comes in two kinds. `view`, which lays a struct layout over a byte slice, was seen in #chref("named-types"). Keeping the
bit pattern between two scalars of the same width and changing only how it is read is `bit_cast`.

#demo("examples/ch20/bitcast.low")

The −1 of an `i32` has every bit set, so read as a `u32` it is 4294967295. Reading the `f64` 1.0 as a `u64` shows its IEEE 754 representation as is. Unlike
`cast`, it does not move the value, so it never stops.

The target type must be one where *every bit pattern is a valid value*. `bool` and `enum` are not.

#demo("examples/ch20/bitcast_bool.low")

Reading the `u8` value 2 as a `bool` would give a value that is neither true nor false. The path that creates such values is closed.

== Common mistakes

#antipattern[Forgetting `init` on a bump allocator][
  #demo("examples/ch20/mistake_noinit.low")

  `bump_bytes` does not create memory by itself. Until bytes are attached with `send a init buf`, it has nothing to hand out. A just-spawned
  actor's state fields are all zeroes, and zero is not a slice --- reading that field makes the VM and the native build run differently. So
  translation refuses it (`E-ACTOR-UNINIT`). Put the `init` on the line right after spawning the allocator.
]

#antipattern[Attaching the same bytes to two allocators][
  #demo("examples/ch20/mistake_sharedbuf.low")

  The two allocators know nothing of each other. Both cut from the front of `buf`, so `pv` and `qv` become the same place: write 65 and
  then 66, and reading `pv` gives 66. The rule that there is only one write borrow (#chref("references")) must hold across the actor
  boundary too, so this is refused with `E-EXCL` (until 2026-09-16 it passed). Attach separate bytes to each allocator; if one buffer must
  be shared out, cut two non-overlapping pieces with `subslice`.
]

#antipattern[Leaving out `using` when two allocators fit][
  #demo("examples/ch20/mistake_ambiguous.low")

  `s` and `g` are both `bump_bytes`, so the tool cannot guess. A guess might carve from the big buffer what should come from the small one,
  or the other way round, and such a bug stays hidden on a development machine with plenty of memory. So it stops with
  `E-ALLOC-AMBIGUOUS` and asks you to say which.

  #demo("examples/ch20/ambiguous_fixed.low")

  The two calls to `two_from` each carved 3 bytes from the same `g`, so `used` is 6. With the source written on every call, you can read
  which buffer shrinks.
]

#antipattern[Leaving out `via a` in a generic op][
  #demo("examples/ch20/mistake_novia.low")

  `one_from` declares only `effects state .`, but when instantiated with `fixed_bytes`, `reserve` performs `alloc`. `via a` is what makes
  "the allocation effects of type `a` are part of my declaration" true, so each instance gets exact effects. Without it you get `E-EFFECT`,
  and since the effect does not reach the caller, `with_fixed` oddly gets a `W-EFFECT-OVER` as well. Fixing the first error removes the
  second.
]

#misconception[Spawning another allocator gives you another window][
  #demo("examples/ch20/shared_window.low")

  `fixed_bytes` carves from the *root*. There is one root and one cursor (#chref("regions")). Once `f1` takes 40000 bytes, `f2` gets `none`
  even though it has used nothing yet, because the default window (65536 bytes) has too little room left. An allocator's `used` is the
  amount *that allocator* has used, not what is left in the whole window. If you need separate budgets, take one large piece from the window
  and attach non-overlapping parts of it to several `bump_bytes`.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "fixed-memory-glance",
  caption: [Allocator syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`var a be allocs.bump_bytes spawn actor allocs.bump_bytes .`], [spawn an allocator (its state)], [state is an actor value --- there is no global allocator],
  [`send a init buf`], [attach the bytes to hand out], [an allocator never creates memory behind your back],
  [`send a reserve 3` · `send a used`], [request a piece (`option`) · amount used], [shortage is a value, not a trap],
  [`input comptime a type .`], [receive the allocator's type (policy) at translation time], [swapping costs nothing at run time],
  [`using al a .`], [receive an allocator value of that type --- not an input], [it does not sit among the call's arguments],
  [`let n using g be u64 two_from .`], [say which allocator this call carves from], [with two or more, nothing is guessed],
  [`effects state via a .` · `requires allocs.byte_allocator a .`], [inherit the type's effects · trait condition], [exact effects per instance],
  [`allocs.fixed_bytes` · `allocs.heap_bytes`], [default allocators carving straight from a root], [only an op holding that kind of capability may spawn one --- `E-CAP-FORGE`],
  [`send b grow pv 6` · `send b release qv`], [grows the last piece · takes it back], [checks identity with `same_slice`, not size],
  [no source · an unused `using`], [`E-ALLOC-NOSOURCE` · `E-ALLOC-USING-UNUSED`], [no global allocator, and no empty choice],
  [`var xs using bb be mut slice u64 lit array u64 4 … . else …`], [takes a list from the allocator you choose], [goes to `else` if it runs out --- which must leave],
  [`bit_cast u32 x`], [keep the bits, change only how they are read], [never read as `bool` or `enum`],
)

#recap[
  An allocator is a capability, a policy (a type satisfying a trait) and state (an actor value). A bump allocator cuts borrowed bytes and reports shortage as
  `none`. Allocators are received with `input comptime a type .` and `using al a .`, swapped at no run-time cost, and there is no global allocator.
  `fixed_bytes` and `heap_bytes` are default allocators with capability fields and can be spawned only from ops holding the same kind of capability. The linker
  sets the size of the fixed window. `bit_cast` keeps the bits and never reads them as `bool` or `enum`.
]
