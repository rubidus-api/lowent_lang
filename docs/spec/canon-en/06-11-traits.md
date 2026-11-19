## 6.11 Type-associated ops and traits

## 6.11.1 Attaching an op to a type

(1) If a type name and a dot are put in front of an op's name, that op becomes **attached to that type**.

(2) The first input of an attached op is a value of that type.

## 6.11.2 Traits

(1) A ⟦트레이트|trait⟧ is **a list** of ops a type shall provide. It writes the name and
      signature of each op.

> [!산문] What it is for
> When there are several shapes (rectangle · square …), the **method** of finding the area differs by type, but the **promise**
> *"it tells its area"* is the same. A trait gives that promise a name. Then an op that takes *"anything that tells its area"*
> can be written **once** — the same op need not be copied for each type, and a type that does not keep the promise is caught at translation.
> The call site need not know what the type is. This language has no inheritance, so this is the way to handle
> several types under one name.

```lowent 예제: what a trait is for — one op takes several types · 결과: demo() = 44
module ex_trait_why .

rem Each shape finds its area differently. But the promise «it tells its area» is the same.
trait shape do
  area input s self . output u64 .
end .

struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end .

struct square do
  satisfies shape .
  side u64 .
end .

fn rect.area input s rect . output u64 .
do
  return mul field s. w . field s. h . . .
end .

fn square.area input s square . output u64 .
do
  return mul field s. side . field s. side . . .
end .

rem This op takes **any shape** — `requires shape t` pins it to «only types that tell their area».
fn double_area input comptime t type . input s t . output u64 .
  requires shape t. . .
do
  return mul 2 method s. area. . . .
end .

fn demo output u64 .
do
  let r lit rect do w 2 . h 3 . end . .
  let q lit square do side 4 . end . .
  return add double_area rect. r. . double_area square. q. . . .
end .
```

(1a) A trait can write **several** ops. A signature **begins with the op's name** and that op's clauses follow,
      and the next name opens the next signature. It is customary to write one signature per line. The clauses of a signature follow **the same order**
      as an op head (⟦§6.4.1⟧ (3a)) — inputs · output · effects. There is no limit on the number of ops written.

(1b) `fn` · `proc` **are not written** in a signature. What that op can do is decided by the signature's `effects` line.
      - Without an `effects` line, it is an op with **no** effects. It can be provided by an `fn`, or by a `proc` that writes its effects.
        A `proc` always writes an `effects` line (⟦§7.1⟧; without one, `E-EFFECT-MISSING`) — a `proc` that provides an effect-free signature
        writes `effects none .`.
      - With an `effects` line, the providing side is a `proc` that writes those effects **or fewer**, or an `fn` if there are no effects.

(2) Inside a trait, `self` refers to the type that will provide it.

(3) If a type writes a trait with `satisfies`, the processor checks that the ops of that list exist **with exactly those
      signatures**. If even one is missing or mismatched, translation is refused.

(4) There are five kinds of mismatch.

> [!표] Places that fail to provide a trait
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Diagnostic*], [*What went wrong*],
> [`E-TRAIT-UNDEF`], [`satisfies` calls a trait that **is not declared** — a claim that points at nothing checks nothing],
> [`E-TRAIT-MISSING`], [an op of the list is missing],
> [`E-TRAIT-SIG`], [the op exists but **the number of parameters** differs — a trait that does not fit exactly cannot carry a contract],
> [`E-TRAIT-EFFECT`], [the providing side has **effects the trait does not write**. The caller judges by the trait's contract, so doing what is not in that contract makes the judgment wrong],
> [`E-TRAIT-RECV`], [**an op** wrote that it provides a trait — what provides is **a type**],
> )

(5) If an op of a trait writes `via self` in its effects line, the providing side may write **allocation-family effects** —
      `alloc` · `heap` · `lock` · `atomic` — for that op beyond the trait. Writing any other additional effect
      is still `E-TRAIT-EFFECT`. The additional effects are carried by a generic op that calls through `via <type parameter>` up to
      its callers (⟦§7.1.1⟧ (6)).

> [!산문]
> What a trait does is make «this type knows how to do these things» **something the caller can trust**.
> So if only the names match and the effects differ, that trust breaks — only when the effects match too
> does a trait carry a contract.

```lowent 예제: providing a trait
module ex_trait .

rem A trait is the list of ops a type shall provide.
trait shape do
  area input s self . output u64 . effects none .
end .

rem Writing `satisfies` has it checked whether that list is provided.
struct rect do
  satisfies shape .
  w u8 .
  h u8 .
end .

fn rect.area input s rect . output u64 .
do
  return mul widen u64 field s. w . . widen u64 field s. h . . . .
end .
```

```lowent-거부: writing that it provides and not providing · E-TRAIT-MISSING
module ex_trait_bad .

trait shape do
  area input s self . output u64 . effects none .
end .

struct rect do
  satisfies shape .     rem it wrote that it provides
  w u8 .
  h u8 .
end .

rem `rect.area` was not made
```

```lowent 예제: a trait with several ops
module ex_trait_many .

rem One line per signature — it begins with the name, then inputs · output · effects.
trait shape do
  area input s self . output u64 .
  perimeter input s self . output u64 .
  grow input s self . input k u64 . output self .
  checked_area input s self . output u64 . effects panic .
end .

struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end .

rem A signature without an effects line is provided with an `fn`.
fn rect.area input s rect . output u64 .
do
  return mul field s. w . field s. h . . .
end .

fn rect.perimeter input s rect . output u64 .
do
  return mul 2 add field s. w . field s. h . . . .
end .

fn rect.grow input s rect . input k u64 . output rect .
do
  return lit rect do w add field s. w . k. . . h add field s. h . k. . . end . .
end .

rem A signature that writes effects is provided with a `proc` that writes those effects.
proc rect.checked_area input s rect . output u64 . effects panic .
do
  if eq field s. w . 0 . do panic "empty rect" . end .
  return mul field s. w . field s. h . . .
end .
```

```lowent-거부: trying to provide an effect-free signature with a proc without an effects line · E-TRAIT-EFFECT
module ex_trait_proc_noeff .

trait shape do
  area input s self . output u64 .
end .

struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end .

proc rect.area input s rect . output u64 .
do
  return mul field s. w . field s. h . . .
end .
```

> [!산문]
> A trait is a way of writing *"this type can do these things"* **in a form that is checked**.
> Written as a comment, it stays the same even when the code changes, but with `satisfies`, translation is refused the moment
> it fails to provide.

> [!주의] It is not inheritance
> A trait **makes no hierarchy** among types. That a type provides a trait is
> only the fact *"it has those ops"*; it inherits nothing from another type.
> This language has no inheritance.

## 6.11.3 Calling an attached op — method

(1) `method` is the form that calls an op attached to a value. Its shape is
      `method <value> <step> … <name> <argument> … .`.

(2) Steps and the name are told apart **by following the type**. Going down one step at a time from the receiver's type,
      if that type has an attached op of that name, **that is the name**, and everything
      after it is arguments. So `method o inner area` calls `area` attached to `inner` of `o`,
      and `method s plus 5` calls `plus` attached to `s` with `5`.

(3) The receiver need not be a name. It may be the result of another form, and then the receiver's type is
      that form's output type — so `method (method x grow 2) area` holds.

(4) If the receiver's type cannot be decided, the processor refuses the program. Attached ops
      are found **by type**, so if the type is not known, the op to call is not decided either.

```lowent 예제: calling an attached op
module ex_method .

struct rect do
  w u64 .
  h u64 .
end .

fn rect.area input s rect . output u64 .
do
  return mul field s. w . field s. h . . .
end .

export fn twice_area input s rect . output u64 .
do
  return mul 2 method s. area. . . .
end .
```

> [!참고]
> The infix notation (`s..area`) **does not exist**. It was the only place in the language read from right to
> left, and there was no way to write a multi-step receiver. Unified as a form,
> a call is **always name first**.

## 6.11.4 Where attached ops go wrong

(1) `method` requires both **a receiver and a name** (`E-METHOD-RECV`).

(2) If the receiver's type has no attached op of that name, it is refused (`E-METHOD-UNDEF`) —
      it shall be declared as `fn <type>.<name>`.

> [!주의]
> An attached op is **a device that divides the namespace**, not inheritance (⟦§6.11.2⟧). So calling a name that does not exist
> does not search upward; it is refused right there.

```lowent-거부: that type has no attached op of that name · E-METHOD-UNDEF
module ex_method_undef .

struct p do
  x u8 .
end .

fn f input s p . output u8 .
do
  return method s. nosuch. . .
end .
```

## 6.11.5 Choosing at run time — `object` and `dyn`

(1) A value of type `object <trait>` holds a value of **any type** that satisfies the trait with `satisfies`. Which type it is is decided by the value at run time,
      and the processor does not know it when it translates. The value is made with `object <trait> <value> .` — it **copies** that value in.
      The type of `<value>` must be written: a name with a written type, or `lit <type> …` (the type is never guessed · `E-DYN-LIT`).
      If that type does not satisfy the trait, it is `E-DYN-NOTSAT`.

(2) `dyn <value> <op name>. <arguments> .` **chooses at run time** the op that the type held in `<value>` provides, and calls it. `<value>` must be a name written
      with type `object <trait>` (`E-DYN-RECV`), and the op must be one listed in the trait (`E-DYN-OP`). The trait's signature decides the result type and the arguments.

(3) Choosing an op at run time is the effect `dyn` (⟦§7.1⟧). An op that calls `dyn` must be a `proc` with `dyn` in its effects line
      (`E-DYN-EFFECT`), and the effect spreads to its callers like any other. So the signature shows which ops choose at run time.
      The effects of what the chosen op does do not go beyond the effects the trait signature lists (⟦§6.11.2⟧ (1b) · `E-TRAIT-EFFECT`).

(4) A trait that can be used with `object` must keep the following for every op — otherwise it is `E-DYN-TRAIT`.
      - The first input is `self` (the value the choice is made by). `self` does not appear in the other inputs or the output — its size is settled only at run time.
      - There is no `comptime` input, and no `via self` in the effects line.
      - The other inputs and the output are one-word types (the limit of this version).

(5) If no type satisfies the trait, it is `E-DYN-NOIMPL`. In this version the types that satisfy it must be in the trait's own module (`E-DYN-MODULE`).
      There is no way to get the original type back from a value of `object <trait>` — its fields cannot be read with `field` (`E-DYN-FORM`).
      If the shape of `dyn` or `object` is wrong (no stop after the op name · two values), it is `E-DYN-FORM`. The processor gives the type of those values the name
      `<trait>__object`, so if the author makes the same name it is `E-DYN-NAME`. An op that crosses to C (`link` · `extern`) does not take or give `object` (`E-DYN-FFI`),
      and neither does an actor's handler (`E-DYN-ACTOR`) — the limits of this version.

(6) `object <trait>` is a **value type** with a fixed size. Its value holds the number of the type it holds and the fields of **all** the types that
      satisfy the trait side by side — its size is that of a struct holding the number and those fields side by side — the sum of the fields plus the number and alignment padding, not the largest such type. So, like a struct, it can be an element of a list
      literal (⟦§6.2.6⟧ (6) · `lit slice object <trait> … .` · `lit array <n> object <trait> … .`), and when `for` walks such a list the element's name is `object <trait>` too.
      In this version every field of a type that satisfies the trait is a one-word sized number, float or `bool` — the other types' fields must be filled with zero (`E-DYN-FIELD`).

> [!산문] What it is for
> Calling through a trait bound (⟦§6.11.2⟧) has the processor build the op separately for each type at translation, with no indirect call — the fastest.
> An enum with variants (⟦§6.6⟧) has **the author close** the set of types. `object` is for the place where neither works — where the type of the value is settled at run time
> and the caller does not write down the set. In exchange it pays for choosing (an indirect call cannot be inlined), and that fact shows in the word `dyn` and in the effect.

```lowent 예제: chosen at run time — one op takes a rect and a square · 결과: demo() = 28
module ex_dyn .

trait shape do
  area input s self . output u64 .
end .

struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end .

struct square do
  satisfies shape .
  side u64 .
end .

fn rect.area input s rect . output u64 .
do
  return mul field s. w . field s. h . . .
end .

fn square.area input s square . output u64 .
do
  return mul field s. side . field s. side . . .
end .

rem this op does not know the type of the value it gets — `dyn` chooses at run time
proc twice input o object shape . output u64 . effects dyn .
do
  return mul 2 dyn o. area. . . .
end .

proc demo output u64 . effects dyn .
do
  let r rect lit rect do w 2 . h 3 . end . .
  let q square lit square do side 4 . end . .
  let a object shape object shape r. . .
  let b object shape object shape q. . .
  return add twice a. . dyn b. area. . . .
end .
```

```lowent-거부: chooses at run time without `dyn` in its effects line · E-DYN-EFFECT
module ex_dyn_effect .

trait shape do
  area input s self . output u64 .
end .

struct rect do
  satisfies shape .
  w u64 .
end .

fn rect.area input s rect . output u64 .
do
  return field s. w . .
end .

proc f input o object shape . output u64 . effects none .
do
  return dyn o. area. . .
end .
```
