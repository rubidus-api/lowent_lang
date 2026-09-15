#import "../../book/lib.typ": *

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

#demo("examples/ch20/borrowed.low")

- `spawn actor allocs.bump_bytes` makes the allocator, and `send a init buf` hands it the bytes to cut. This allocator cannot make memory by itself --- the
  discipline of never allocating secretly.
- `send a reserve 3` cuts off 3 bytes. What comes back is not a copy but a slice pointing at part of the original, so `set (index pv 0) 65 .` changes the first
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
  in the binding, as `let n u64 using b be two_from .`.
- `requires allocs.byte_allocator a .` is the condition that `a` satisfies the trait (#chref("traits")).
- `effects state via a .` means the effects of the allocator's `reserve` are this op's effects.

Given `bump_bytes`, the same `two_from` uses 3 + 5 = 8; given `bump_aligned`, which aligns start positions to multiples of 8, the second piece starts at 8 and it
uses 13. Because the type is fixed at translation, there is no virtual function table and no indirect call. Swapping costs nothing at run time.

If no source is written, a default is chosen: the binding's `using`, the op's own `using` name, and otherwise the input or binding of matching type if there is
*only one*. With two or more it does not guess and asks you to write it with `E-ALLOC-AMBIGUOUS`. Defaults do not cross op boundaries --- an allocator from the
caller never flows in on its own, so *there is no global allocator*.

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

#recap[
  An allocator is a capability, a policy (a type satisfying a trait) and state (an actor value). A bump allocator cuts borrowed bytes and reports shortage as
  `none`. Allocators are received with `input comptime a type .` and `using al a .`, swapped at no run-time cost, and there is no global allocator.
  `fixed_bytes` and `heap_bytes` are default allocators with capability fields and can be spawned only from ops holding the same kind of capability. The linker
  sets the size of the fixed window. `bit_cast` keeps the bits and never reads them as `bool` or `enum`.
]
