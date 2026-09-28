#import "../../typst-ko/lib.typ": *

= Containers and sorting — `sortlib`, `sortgen`, `hashmap`, `vecgen`, `spsc`

#chapter-toc()

#prereq(
  ([#chref("generics"), Generics], [comparison is brought by the type, not a value]),
  ([#chref("fixed-memory"), Allocators and fixed memory], [allocators are handed over with `using`]),
  ([#chref("lib-map"), A map of the standard library], [buffers belong to the caller]),
)

#deepqa[
  In #chref("generics"), C's `qsort` takes a comparison function as a value. What did it say Lowent's `sortgen` takes it by?
][
  The *type* brings the comparison. If the type being sorted satisfies the `ordered` trait and provides `less`, `sort_by` is monomorphised for that type and the
  comparison is built in as a direct call. This chapter tours that sort together with the standard library's containers.
]

#why[
  Sorting, searching, hash maps and growing arrays go into almost every program. In other languages such containers usually allocate on the heap secretly and grow
  quietly when full. Lowent's containers come in two kinds: *fixed containers* whose backing slice the caller hands over (`hashmap`, `sortlib`), and *generic containers*
  that receive an allocator with `using` and grow themselves (`vecgen`, `mapgen`). Either way, where memory comes from is visible in the head.
]

#organizer[
  You will learn to sort and search `u64` slices with `sortlib.sort` and `searchlib.bsearch`, and to sort structs by a type-defined criterion with `sortgen.sort_by`.
  You will see how `hashmap` works over the caller's slice (empty slots and tombstones). You will watch `vecgen` receive an allocator and grow, with its effects
  depending on the allocator's type. You will also see `spsc`, which passes values between flows without locks, and where the other containers fit.
]

#chapter-questions()

== Sorting and finding `u64`

#demo("examples/ch34/sorted.low")

- `alloc_bytes al capacity 40` gets 40 bytes, and `view_array u64` views them as a slice of five `u64` elements. Allocation happens only here.
- `sortlib.sort xs` sorts in place in ascending order. It is `effects none`.
- `searchlib.bsearch xs 19` gives the position of 19 in the sorted slice as an `option`, or `none` if absent. `lower_bound` gives the insertion position.

The smallest value 3 and 19's position 2 give 32. `bsearch` *trusts* that its input is sorted. Given an unsorted slice it gives a wrong answer. That trust is a
precondition written in the module document.

== The type brings the criterion

#demo("examples/ch34/rows.low")

`score` declares that it knows order with `satisfies sortgen.ordered .`, and `score.less` gives the criterion "higher score first". `sortgen.sort_by score rs` sorts by that
criterion. The ids for scores 95, 80 and 70 are 2, 3 and 1 in order.

To sort descending or by several keys, write `less` that way. Instead of adding a mode argument, the type brings the meaning. `sort_by` is insertion sort, so it is
*stable* (the order of equal scores does not change) and fast on nearly sorted input. For large arrays there is `sort_fast` (a generic quicksort). Scalars like `u64`
cannot satisfy traits, so wrap them in a one-field struct. The layout stays 8 bytes.

#qa[
  What happens if `less a a` is true?
][
  Sorting may not stop or may give a wrong order. `less` must be a *strict weak ordering* --- in particular nothing may come before itself. The mistake of writing `lt`
  where `le` belongs, or the reverse, happens here. A trait checks that an op *exists*, not that it keeps mathematical properties. Those properties are a contract
  written in the module document.
]

== A hash map over the caller's slice

#demo("examples/ch34/table.low")

`hashmap` is a `u64 → u64` open-addressing hash map. Its backing is a `mut slice u64` held by the caller, laid out as `[key0+1, value0, key1+1, value1, …]`. `size` is
the slot count (`len / 2`). Initially everything must be 0 (empty), and `alloc_bytes` gives zero-filled bytes, so they are used as is.

- `put slots 7 700` inserts, and `put` again with the same key changes the value. When full it is `false`.
- `lookup` gives an `option`.
- `del` does not return a slot to empty; it covers it with a *tombstone*. Returning it to empty would break the probe chain continuing behind it, and later keys could
  not be found.

The price of this design is written in the document too. The two largest keys cannot be used, because they mark empty (0) and tombstone, and it does not grow
automatically. Byte-string keys are handled by `strmap`, and a growing generic hash map by `mapgen`.

== A growing generic vector

#demo("examples/ch34/growing.low")

- `allocs.heap_bytes` is spawned and used as an allocator. Only `main`, holding `cap heap`, can spawn it (#chref("fixed-memory")).
- `vecgen.open u32 4` opens a vector with element type `u32` and initial capacity 4. The allocator is handed over with the binding's `using hb`.
- `vecgen.append … v x` asks the allocator for more and grows when out of room. On failure it is `false`. The vector already holds its own allocator, so `append` does not
  take `using`. Writing it is rejected with `E-ALLOC-USING-UNUSED`.
- `vecgen.at … v 50` gives element 50 as an `option`. 50 × 2 = 100.

The effect of `vecgen.append` is `state via a`. Here `a` is `heap_bytes`, so this instance's effect is `heap state`, and `heap` shows in `main`'s head too. Opening the
same code with a bump over borrowed bytes shows only `state`. The allocator's type decides whether the container runs on a machine without an operating system.

#misconception[A growing vector is hidden allocation after all][
  Allocation happens but is not hidden. `append`'s effects line carries the allocator's effect as is, and `heap` or `alloc` shows in the calling op's head. When the
  allocator runs short, `append` gives `false` --- running out of memory is a value. Hidden allocation is allocation invisible in the head that stops the program when it
  fails.
]

== A ring buffer passing values between flows

`spsc` is a lock-free single-producer, single-consumer ring buffer. One flow puts and another takes. The caller hands over the control and backing slots, and the ops
that put and take receive `cap atomic` and move the cursors with atomic operations (#chref("parallel-atomic")). It is used where interrupt handlers and ordinary code
exchange values (#chref("hardware")).

Lock-free data structures are notorious for being wrong only in rare orderings. `spsc`'s correctness was confirmed by *borrowing* the proof of an algorithm already proven
in a weak-memory model. Multi-producer multi-consumer queues and seqlocks have no proof to borrow, so they were left out (#chref("proofs-locks")).

== Other containers

#dtable(
  columns: 2,
  id: "containers-more",
  caption: [Other container modules],
  [*Module*], [*In one line*],
  [`strmap`], [Byte string → `u64` hash map. Key bytes are kept in the caller's slice too],
  [`mapgen`], [Generic hash map `table k v`. Rehashes itself],
  [`vecs` · `growvec`], [Growing byte vectors. `growvec` is a short name for `vecgen.vec u8`],
  [`nodelist`], [Fixed-size intrusive list],
  [`soa`], [An experiment laying out an array of structs as per-field arrays],
)

== Common mistakes

#antipattern[Binary-searching a slice that was never sorted][
  #demo("examples/ch34/mistake_unsorted.low")

  42 sits in slot 0, yet `bsearch` returns 99 (not found). Binary search compares with the middle value and discards half, and if the slice
  is not sorted the answer may be in the discarded half. It neither stops nor warns, which makes it the hardest kind of bug to find. With
  luck it even succeeds (in the same slice it finds 19). A binary search must always be preceded by `sortlib.sort`, and where code that
  keeps the order is scattered, use `lower_bound` to find the insertion point and keep the slice sorted.
]

#antipattern[Writing `less` non-strictly, like `ge`][
  #demo("examples/ch34/mistake_lessge.low")

  All three scores are 70. `strict_score`, written with `gt`, keeps the insertion order 1, 2, 3 and returns 123. `loose_score`, written with
  `ge`, treats equal scores as "coming first" too, swaps them, and returns 321. The stable sort's promise that "equal items keep their
  order" is broken. With another sorting algorithm it might even never finish or produce a wrong order. The trait checks only that `less`
  *exists*, so the property "nothing comes before itself" is for the writer to keep.
]

#antipattern[Writing `using` again for a container that already carries its allocator][
  #demo("examples/ch34/mistake_usingappend.low")

  `vecgen.open` takes the allocator and stores it inside the vector. The later `append` uses the allocator the vector carries, so the
  binding's `using hb` means nothing. A meaningless mark becomes false information --- "this call carves from `hb`" --- so it is rejected
  with `E-ALLOC-USING-UNUSED`. Write `using` only where the allocator is *first received* (`open`).
]

#misconception[Any `u64` can be a key in `hashmap`][
  #demo("examples/ch34/hashmap_topkeys.low")

  The result 4 means only the third `put` succeeded. The two largest keys (`18446744073709551615`·`18446744073709551614`) are used to mark
  empty slots and tombstones, so they cannot be stored and `put` returns `false`. This cost is stated at the top of the module's
  documentation. If keys may use the full range, put them in a struct and use `mapgen`.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "lib-containers-glance",
  caption: [Shapes of containers and sorting --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`sortlib.sort xs` · `searchlib.bsearch xs k`], [sort `u64` in place · search sorted input (`option`)], [no allocation --- search trusts the order],
  [`struct score do satisfies sortgen.ordered . … end` + `fn score.less`], [the type brings the sort order], [a type instead of a mode argument --- `less` must be strict],
  [`sortgen.sort_by score rs` · `sort_fast`], [stable insertion sort · quicksort for large arrays], [the choice is in the name],
  [`hashmap.put slots k v` · `lookup` · `del`], [a `u64 → u64` map on the caller's slice], [`false` when full --- deletion leaves a tombstone],
  [`let vo using hb be … vecgen.open u32 4 .`], [open a growing vector with an allocator], [`using` only where it is first received],
  [`vecgen.append u32 allocs.heap_bytes v x`], [push while growing --- `false` on failure], [effects follow the allocator type (`state via a`)],
  [`spsc`], [lock-free single-producer single-consumer ring buffer], [atomic operations --- a borrowed proof],
)

#recap[
  `sortlib` and `searchlib` sort and search `u64` slices in place, and with `sortgen` a type satisfying `ordered` brings the criterion. `hashmap` and `strmap` are fixed
  containers over the caller's slice that delete with tombstones. `vecgen` and `mapgen` receive an allocator with `using` and grow, their effects following the allocator
  type. `spsc` passes values between flows with atomic operations, and its correctness was confirmed with a borrowed proof.
]
