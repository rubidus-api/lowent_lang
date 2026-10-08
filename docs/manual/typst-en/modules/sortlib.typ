#import "../../typst-ko/lib.typ": *

= `sortlib` --- in-place quicksort <mod-sortlib>

#modhead(file: "lib/sort.low", layer: [L0 --- pure computation], caps: [none])

Sorts a `u64` slice *in place* in ascending order. Used before binary search (#modref("searchlib")[`searchlib`]) and to present results in order. The one public op is
`sort`.

```lowent
use sortlib .

sortlib.sort s .
```

#aside[Maturity `standard` --- the repository's first `standard`][
  The current surface (just `sort`) is *kept*. Changing names, arguments or meaning needs a separate compatibility decision. That does not mean it will not grow --- a
  comparator and other element types do not exist yet, and would arrive *by addition*. And *performance is not promised* --- today it is quicksort. Other element types are
  #modref("sortgen")[`sortgen`]'s job.
]

Sorting is *pure computation* almost every tool needs, and it reaches nothing in the world. So it is a library, not a core op --- if it can be written in Lowent, it is a
library. It was built with zero new core ops.

#dtable(
  columns: 3,
  id: "mod-sortlib-ops",
  caption: [Ops of `sortlib`],
  [*op*], [*Shape*], [*Failure*],
  [`sort`], [`proc (s mut slice u64) → void`, `effects none` --- in-place ascending], [none (empty and one-element slices do nothing)],
  [`qsort` (internal)], [`proc (s mut slice u64, lo0 u64, hi0 u64) → void`], [not exported --- calling from outside is `E-VISIBILITY`],
)

*Depth is bounded by O(log n) --- the point of this implementation.* Naive Lomuto partitioning on already sorted input always picks an end pivot, so recursion gets O(n) deep
and can exceed the native depth limit. So it *recurses on the smaller side and loops on the larger*. Only halves or smaller pile up in recursion, so it cannot exceed log n.
This is for resource safety, not correctness --- it may slow down, but it does not blow up. The internal `qsort` handles the half-open range `[lo0, hi0)` and does nothing if
`hi0 > len s` or `lo0 >= hi0` --- even a reversed range never touches out of bounds, so VM and native give the same answer (nothing).

*Why no new array is returned.* That would need allocation, which this layer does not do. The same is why no comparator is taken --- if another order is needed, transform
the values beforehand.

A self-verifying pattern --- is it ascending *and* is the sum preserved. It catches order defects and element loss or duplication together.

```lowent
proc sort_verify input s mut slice u64 . output u64 . effects none . do
  var pre u64 0 .
  var i u64 0 .
  while lt i (len s) do
    set pre (wrap_add pre (idx s i)) .
    set i (add i 1) .
  end
  sortlib.sort s .
  var post u64 0 .
  var sorted u64 1 .
  var j u64 0 .
  while lt j (len s) do
    set post (wrap_add post (idx s j)) .
    if gt j 0 do
      if gt (idx s (sub j 1)) (idx s j) do set sorted 0 . end
    end
    set j (add j 1) .
  end
  if ne pre post do set sorted 0 . end
  return sorted .
end
```

#antipattern[Sorting a subrange with the internal op][
  `sortlib.qsort s 0 (len s)` is `E-VISIBILITY`. Cut a subrange with `subslice` and give it to `sort` --- `sortlib.sort (subslice s 2 7) .`
]

#antipattern[Passing an immutable slice or expecting a return value][
  Passing `input s slice u64` is a type error --- in-place sorting needs `mut slice u64`. If the original must survive, sort a copy. `let r … sortlib.sort s` is a compile
  error too --- it is `output void`, and the result is `s` itself.
]

*Cautions.* The input is destroyed. It is not stable (the original order of equal values is not guaranteed --- unobservable with `u64` today, but that changes once key
extraction exists). Time is O(n log n) on average and O(n²) worst case; extra memory is only O(log n) recursion frames. *What is checked* --- sortedness and permutation
(tests), VM/native agreement on arbitrary inputs (differential testing).
