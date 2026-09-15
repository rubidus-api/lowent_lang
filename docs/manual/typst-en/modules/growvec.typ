#import "../../typst-ko/lib.typ": *

= `growvec` --- self-growing byte vector <mod-growvec>

#modhead(file: "lib/growvec.low", layer: [L1 --- storage (carries an allocator)], caps: [none --- `open` is handed a bump allocator])

A *byte* array that fetches more room by itself when short. Used when you do not know how much will come --- until the file is fully read, until the user presses Enter.
`growvec.gvec` is an *alias* of `vecgen.vec u8 allocs.bump_bytes`, and its ops delegate to #modref("vecgen")[`vecgen`]. For pushing bytes only, this one is shorter because no
type arguments are written --- a short name is interface too.

```lowent
let g option growvec.gvec . using bump be growvec.open 16 .
guard is_some g . else return 1 .
var v growvec.gvec be some_value g .
guard growvec.add_all v "hello" . else return 2 .
guard growvec.add_all v " world" . else return 3 .
let s slice u8 be growvec.view_of v .
```

*Why it exists.* Gathering bytes with #modref("vecs")[`vecs`] means passing allocator, buffer and length on every call, and *rebinding* to the grown buffer is the call site's
job too. Forgetting that one line is not caught by the compiler --- the old buffer is still a valid slice. `growvec` puts the three *in one value* and removes that kind of
defect structurally. There is still no magic --- the allocator was still handed in by someone, and the cost of growth is visible at ops that can return `false`.

#dtable(
  columns: 3,
  id: "mod-growvec-ops",
  caption: [Ops of `growvec`],
  [*op*], [*effects*], [*What it does*],
  [`gvec`], [---], [alias of `vecgen.vec u8 allocs.bump_bytes`],
  [`open cap0` (`using al`)], [state], [Opens an empty vector. `cap0` may be 0 (becomes 8 on the first push). `none` if no place],
  [`count_of g` · `cap_of g`], [none], [Bytes held · current capacity],
  [`add_byte g b`], [state], [One byte --- grows to double + 8 when short. `false` = no new place (the vector stays valid)],
  [`add_all g s`], [state], [A whole byte string. *If it fails midway, the front part is already in*],
  [`reserve_more g more`], [state], [Reserves room for `n + more` ahead],
  [`view_of g`], [none], [A view over only what is held],
)

*`add_all` does not promise all-or-nothing.* Promising it requires reserving room first, and whether to pay that is the caller's choice, so `reserve_more` is separate.

```lowent
proc put_record input v mut growvec.gvec . input rec slice u8 . output bool . effects state . do
  guard growvec.reserve_more v (add (len rec) 1) . else return false .
  guard growvec.add_all v rec . else return false .
  return growvec.add_byte v 10 .
end
```

*The memory side.* A bump allocator never gives back, so once every growth left the old buffer abandoned in the arena, and holding 2,048 bytes used 8,104. Giving the allocator
*in-place growth* (`grow` --- if the last allocation is our buffer, extend it) removed those generations. The remaining factor of two is the growth policy's slack
(capacity ≈ 2n), a normal trade against time. If the size is known ahead, `reserve_more` removes even the slack and takes exactly what is needed
(#modref("allocs")[`allocs`]).

#antipattern[Using a view taken before growth after growth][
  Taking `view_of` and then calling `add_all` may change the buffer, leaving the view on the old one. If it did not grow it is right by accident, which is worse. Take views
  last.
]

#antipattern[Copying a `gvec` and pushing to both][
  After `var b growvec.gvec be a .`, pushing to `a` and `b` separately makes both see the same buffer while each `n` is unaware of the other's change --- they overwrite each
  other. Handle one vector by one name, and pass it as `mut gvec`.
]

#antipattern[Ignoring return values · reading up to `cap_of`][
  Ignoring `false` from `add_byte` lets the vector go on without that byte. Past `n` is garbage --- use `count_of` or `view_of`.
]

*Cautions.* No individual release or shrinking. A vector lives within its allocator's lifetime. It is `effects state`, so the pure layer cannot use it --- for problems solvable
purely, caller buffers (the convention of #modref("fmt")[`fmt`]) are cheaper.
