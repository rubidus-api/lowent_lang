#import "../../typst-ko/lib.typ": *

= Regions --- where values live, and memory reclaimed all at once

#chapter-toc()

#prereq(
  ([#chref("references"), Borrowing], [a borrow cannot outlive what it borrows]),
  ([#chref("capabilities"), Capabilities], [`effects alloc` pairs with `cap allocator`]),
)

#deepqa[
  In #chref("references"), what rejected returning a reference to a local from its op? And what were the ways to get a value out of an op?
][
  It was rejected with `E-ESCAPE`, because a local disappears when the op ends and the reference would point at nothing. To get a value out, return the value
  itself rather than a reference, or put it in storage the caller passed. This chapter covers where that "storage" comes from.
]

#why[
  In a language without a garbage collector, someone must decide when memory is given back. C left it to people, who give it back twice or forget. Rust gives
  every value an owner and gives the memory back when the owner disappears. Lowent first puts a coarser unit in place --- the *region*. Values born together
  and dying together go into one region, and the region is reclaimed all at once when it ends. Most memory in systems programs has this shape: temporary parser
  nodes, buffers used while handling one request. That is why Part V covers regions before individual ownership (#chref("ownership")).
]

#organizer[
  You will learn the three places values live (local, static, obtained) and the two roots memory is obtained from (the fixed window and the heap). You will pick
  up how to open a region with a `region <name> <kind> do … end` block and obtain space with `alloc_bytes`, and how to pass a region as a parameter. You will
  see why carrying bytes from a region out of it, or carving from an outer region while an inner one is open, is rejected, and why the heap is refused on
  machines without an operating system.
]

#chapter-questions()

== Three places values live

A stored value lives in one of three places.

#dtable(
  columns: 2,
  id: "regions-where",
  caption: [Where values live],
  [*Kind*], [*Description*],
  [Local], [Born inside an op and gone when the op ends. The most common],
  [Static], [Present for as long as the program lives],
  [Obtained], [Obtained from a root. A region or ownership decides when it is given back],
)

What are usually called the stack and the heap correspond to local and obtained. The different names are because in this language they are *properties of
values*, not the structure of the machine. And where a value lives is written in the source; the processor does not move things behind your back.

There are two roots memory is obtained from, carved and rewound separately.

#dtable(
  columns: 4,
  id: "regions-roots",
  caption: [The two roots],
  [*Root*], [*Carved with*], [*Effect*], [*Character*],
  [Fixed window], [`cap allocator` · regions other than `heap`], [`alloc`], [*Does not grow*. `none` when exhausted],
  [Heap], [`cap heap` · `region <name> heap`], [`heap`], [*Grows*. Only on machines with an operating system],
)

#idx("fixed window")
The fixed window works even on machines without an operating system. There the window is the space between two bounds set by the linker
(#chref("fixed-memory")).

#qa[
  Where do the values made by `lit point do … end` or `some 7` live? Is that allocation?
][
  They live in the processor's *finite pool*. It is the op's implicit frame, so it has no effect and needs no capability. The pool is rewound on every loop
  iteration, and if more values are alive at once than the pool holds, execution stops on the spot. Its size is set by the machine and can be adjusted by
  whoever builds the program. This is why you do not write `alloc` every time you make a small aggregate value.
]

== Opening a region and obtaining space

#idx("region")
A region is opened with `region <name> <kind> do … end`. Inside the block, `alloc_bytes <region> capacity <n>` asks that region for `n` bytes.

#demo("examples/ch18/scratch.low")

- `region work arena do … end` opens a region. `arena` is the kind meaning "carve from the front and give back all at once".
- `alloc_bytes work capacity n` gives an `option mut slice u8`. If there is not enough space, it is `none`. Running out of memory is a *value* too.
- `fill_count`'s head has `effects alloc`. Obtaining space is an effect; a pure `fn` cannot use a region.
- On *every path* out of the block --- reaching the end, `return`, `break` or `continue` of an enclosing loop --- the region is rewound. Even leaving with
  `return s .` reclaims the buffer. There is no code that gives it back piece by piece.

`main` receives `cap allocator`. The `alloc` effect of `fill_count` spreads up to it, so a capability authorising that effect is needed
(#chref("capabilities")). A region block is entitlement to obtain space inside the op that opened it, but the effect still spreads to the caller.

A region's kind is one of a closed eight --- `stack`, `frame`, `arena`, `static`, `heap`, `mmap`, `disk`, `device`. Any other word is rejected.

#demo("examples/ch18/kinds.low")

A kind word leaves in the code *what the region is for*. In this edition only `heap` actually behaves differently --- it carves from the growing
root. The other seven all carve and rewind in the fixed window, so `carve_stack` and `carve_static` get 32 and 64 the same way. The kind is still
written, and the list kept closed, for two reasons: the code need not change when the realisation is later tailored to a machine, and a word that
could be anything would say nothing. Someone reading `region t arena` knows "carve from the front, give back all at once".

```text
fixed window:
    [ a 16 ][ b 32 ][ c 8 ][ ··········· empty ··········· ]
                           ▲ cursor --- the next alloc_bytes carves from here
on reaching end:
    [ ···················································· ]
    ▲ the cursor rewinds to where the region opened --- a · b · c vanish at once
```

This picture is why nothing leaks even though no value is given back one by one: giving back is moving one cursor.

== Receiving a region

A region can also be passed as an argument. It is received as `input <name> region <type> .`, and the type is the name by which that region is called.

#demo("examples/ch18/param.low")

`sum_squares` opens no region itself; it carves inside the region `r` the caller opened. So the lifetime of the buffer it received is decided by the caller's
region. What is obtained from a region cannot outlive the region; when the region ends, what came from it ends too.

== Nothing is carried out of a region

Putting bytes obtained from a region into a name outside the region is rejected.

#demo("examples/ch18/escape.low")

When the region closes at `end`, those bytes are rewound, and `keep` outside would point at nothing. The places that carry things out are `return`, assignment
to a name outside the region, and assignment to a field or element of such a name. Values that do not carry the region's bytes --- integers and booleans such
as `len buf` or a sum --- may be carried out. That is what `fill_count` did when it returned a sum.

#misconception[A region is just people managing lifetimes after all][
  People *decide* the lifetime, but translation *enforces* it. Paths by which a value from a region leaks out, and paths that point into a closed region, are
  blocked at translation. What people do is write "these values die together" as a block, and the block is visible. C's `free` calls are scattered and
  unchecked.
]

== One cursor per root

Carving with the name of an outer region while a region of the same root is open inside is rejected.

#demo("examples/ch18/nested.low")

The fixed window has a single place it carves from (a cursor). When the inner region ends and rewinds the cursor, bytes obtained in between with the outer
name would be rewound with it. A different root --- carving with a fixed-window capability inside a heap region --- has its own cursor and is fine.

```text
            ▼ where outer opened
                        ▼ where inner opened
            [ a ][ ··· ][ b ][ ✘ c ]
                        ▲ when inner ends the cursor rewinds to here → c vanishes too
   a = outer's · b = inner's · c = carved under the outer name while inner was open (✘ E-ALLOC-NESTED)
```

For the same reason, an op spawned as a task cannot obtain memory directly from a root (`E-ALLOC-TASK`), because a cursor is not shared between flows. Sharing
an allocator between flows needs an allocator that moves its cursor atomically (#chref("parallel-atomic")).

== The growing root

#idx("heap")
`region <name> heap` carves from the growing root.

#demo("examples/ch18/heap.low")

The effect is `heap`, not `alloc`, and its partner capability is `cap heap`. A heap region is also reclaimed all at once when the block is left; it never moves
bytes it has already handed out.

When building for a machine without an operating system, the heap cannot be requested through any door.

#demo("examples/ch18/heap_mcu.low")

`--target cortex_m` is a microcontroller without an operating system. The `heap` effect, `cap heap` inputs and `region … heap` blocks are all rejected. On the
same machine, `alloc` carving from the fixed window is still available. Which root code stands on is written in its head, so whether a library runs on a
machine without an operating system is answered by translation.

== A stack on a region

A region hands out more than bytes. `stack_new <region> capacity <n>` makes a stack of `n` elements in that region. Use it for "take out
what went in last first", as when walking a tree or graph with a loop.

#demo("examples/ch18/stack.low")

- `let work be stack u64 stack_new temp capacity 8 .` makes the stack. It takes space from the region, so it is `effects alloc`.
- `push work x .` puts a value in.
- `while pop work into d . do … end` takes values out one at a time, binding each to `d`, as long as there is one. When the stack is empty the
  loop ends --- the grammar has no way to stop by popping an empty stack.

Putting in the digits 7 and 4 of 47 brings them out as 4 and 7, giving 74. The stack is reclaimed together with the region, so there is no
code to give it back.

== Common mistakes

#antipattern[Taking the buffer out with `some_value` without asking whether space was granted][
  #demo("examples/ch18/mistake_nocheck.low")

  16 bytes are granted, but a million bytes do not fit in the fixed window. `alloc_bytes` then gives `none`, and `some_value`, used
  without asking, stops with `E-VM-NONE`. It is the same mistake as not comparing C's `malloc` result with `NULL`, except that Lowent
  stops at the point of taking the value out instead of using space that does not exist. Ask first, as the examples in this chapter do
  with `guard is_some g . else return 0 .`. Running out of memory is a *value* to handle as well.
]

#antipattern[Returning a buffer obtained from a region][
  #demo("examples/ch18/mistake_returnbuf.low")

  The bytes `make_buf` returns are rewound at `end`, so the moment the caller receives them the next allocation may hand that space to
  someone else. It has the same shape as returning the address of a local array in C, and it is rejected with `E-REGION-ESCAPE`. As the
  diagnostic says, move the block outward: the caller opens the region and passes it, and the receiving op carves from it.

  #demo("examples/ch18/returnbuf_fixed.low")
]

#antipattern[Writing an op that uses a region as a `fn`][
  #demo("examples/ch18/mistake_fnregion.low")

  Everything is rewound when the block ends, so it looks as if nothing is left outside. Taking space is still the `alloc` effect: the
  result can depend on whether the window has room (`none`), and it overlaps with other code using the same window. Hence
  `E-EFFECT-CALC`. Write it as `proc … effects alloc .`.
]

#antipattern[Handing region bytes to an actor born outside the region][
  #demo("examples/ch18/mistake_outlives.low")

  The actor `a` was born before the region, so it lives on after the region closes. Whether it keeps the slice it receives in its state cannot be
  known at translation. If it does, it reads the old place even after the region's `end` has given those bytes to someone else. So the check is
  conservative and refuses with `E-ALLOC-OUTLIVES`. Create the actor inside the region too, and both go away together.

  #demo("examples/ch18/outlives_fixed.low")
]

#misconception[Space taken inside a loop is given back every round][
  #demo("examples/ch18/loop_region.low")

  A region is rewound when its *block* ends, not when a round of the loop ends. In `count_outer` the region is outside the loop, so 4096
  bytes pile up each round, and with this edition's default fixed window (65536 bytes) every request after the sixteenth gets `none`.
  `count_inner` opens the region inside the round and rewinds it every time, so all hundred requests succeed. For a buffer used only
  within one round, open the region inside the loop.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "regions-glance",
  caption: [Region syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`region work arena do … end`], [open a region --- rewound all at once on every way out of the block], [no `free` --- the lifetime is the block],
  [`alloc_bytes work capacity n`], [request `n` bytes from the region --- `option mut slice u8`], [running short is a value too],
  [`effects alloc` · `effects heap`], [take from the fixed window · take from the growing heap], [the root in use is visible in the head],
  [`input al cap allocator .` · `cap heap`], [allocation capabilities received by the entry point], [the pair that allows the effect],
  [`input temp region scratch .`], [receive a region the caller opened], [the caller decides how long the buffer lives],
  [`stack`·`frame`·`arena`·`static`·`heap`·`mmap`·`disk`·`device`], [the eight closed region kinds], [words that mean something --- others are `E-REGION-KIND`],
  [storing into an outside name · returning], [rejected (`E-REGION-ESCAPE`)], [never point at reclaimed bytes],
  [carving with an outer name while an inner region is open], [rejected (`E-ALLOC-NESTED`)], [one cursor per root],
  [`--target cortex_m` + `heap`], [rejected (`E-HEAP-NOHOST`)], [a machine without an OS has no heap],
  [`send` region bytes to an actor from outside], [`E-ALLOC-OUTLIVES`], [the actor may keep the bytes and outlive the region --- create the actor inside],
)

#recap[
  Values live in one of local, static and obtained, and the roots they are obtained from are the fixed window that does not grow (`alloc`) and the heap that
  does (`heap`). `region <name> <kind> do … end` opens a region, rewound all at once on every path out of the block. Regions can be passed as parameters. Bytes
  from a region cannot be carried out, and an outer region cannot be carved from while an inner region of the same root is open. On machines without an
  operating system the heap is rejected.
]
