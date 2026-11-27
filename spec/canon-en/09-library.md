# 9 Library

(1) This clause does not specify a list of individual library functions. What it specifies is **the rules a library shall
      keep**. As long as those rules are kept, any library is within the guarantees of this language.

> [!산문]
> The C standard holds the language and the library in one document and specifies function by function. Lowent does not,
> because libraries change much faster than languages. If a list of functions were written here,
> this document would soon be out of date. Writing **rules** instead does not go out of date.

## 9.1 Libraries are written in the same language

(1) **The portable layer** of the standard library is written in the language this document specifies and has no privileges.
      That part receives the same rules as code the author writes directly.

(1a) But the library as a whole is not one layer. At the places that reach the outside world there are narrow leaves,
      and they cross a **trust boundary** (⟦§4.4⟧) using `unsafe` or `extern`.
      That boundary is kept narrow, and where it is is written in the source.

(1b) Turning names into addresses (DNS) is **a different leaf from making connections**. Both receive `cap net`
      and produce the `io` effect, but there is a reason to separate them: a program that already has an address **never touches**
      name resolution, and tests can measure connections **without a network** by giving addresses by hand.
      And name resolution can give different answers depending on the machine and the moment, and which line that wavering is on
      is visible in the source.

> [!산문]
> For convenience, one connection leaf that also takes names would be better. But then *"how many times does this line touch
> the network"* could not be known from the source alone --- the same question this language repeats in capabilities and effects,
> and it gets the same answer: **name what is hidden.**

(2) That is, library ops also write contracts (⟦§6.4⟧), declare effects (⟦§7.1⟧),
      and receive capabilities as arguments (⟦§7.2⟧).

> [!참고]
> That libraries have no privileges is a matter of checkability. If there were a hidden
> passage only libraries could use, the program's guarantees would break the moment that passage was crossed.

## 9.2 What libraries shall keep

(1) a) **They return failure as a value.** They do not express failure with special values (−1 and the like)
      (⟦§6.2.8⟧).

(2) b) **If destroying can fail, they do not hide it** (⟦§8.5⟧).

(3) c) **They do not make up capabilities themselves.** Capabilities they need are received as arguments.

(4) d) **They declare effects honestly**, including the effects of what they call inside.

(5) e) **For numbers whose base can differ, they write which base.** For values such as rows · columns · indices · page numbers, where counting from 0
      or from 1 can differ, the op's `lowdoc` or the module contract **shall state it**.
      This language's indices are 0-based, so libraries are **0-based by default**, and if an outside convention
      is 1-based (ANSI cursor positioning of terminals and the like), that conversion is done **inside the op** — if the caller had to know
      the outside convention, that would be knowledge outside the source (⟦§1.3⟧).

> [!참고]
> Example: the row and column of `term.goto <buffer> <position> <row> <column>` are **0-based** (the top left of the screen is
> `0 0`). ANSI is 1-based, so the op adds 1 inside before putting it out — that addition is not the caller's
> business.

```lowent 예제: libraries keep the same rules
module ex_lib .

enum parse_error do
  too_short .
end .

rem Failure is returned as a value — no special number (−1 and the like) is used.
export fn first_byte input data slice u8 . output result u8 parse_error .
  errors too_short lt len data. . 1 . .
do
  guard ge len data. . 1 . else return error too_short . . .
  return ok idx data. 0 . . .
end .
```

> [!산문]
> These four are not new rules; they say that **libraries keep the rules already fixed** too.
> If libraries were made exceptions, the program's guarantees would break the moment such a library was used.

## 9.3 What is language and what is library

(1) The core ops and builtin ops of ⟦§6.3.3⟧ are **language**. Their names are fixed. The names of core ops cannot be hidden by
      the author, and builtin ops stand only after `call_builtin`, so they do not meet the author's names.

(1a) Builtin ops are material for building libraries. The standard library provides ops that wrap builtin ops, and application
      programs call those ops — calling builtin ops directly is the business of places that build system libraries and processors.

(2) Everything else is library, used by importing modules.

(3) For a feature to enter the language, the following shall hold — it cannot be expressed as a library,
      or expressing it as one hides a cost.

(4) Between the language and libraries there is one more layer — the ⟦리프|leaf⟧.

> [!용어] 리프 (leaf)
> The thinnest piece that touches the operating system or hardware. It cannot be written in Lowent, so the processor
> provides it. A leaf **shall be thin** — what can be written in Lowent is not a leaf but
> a library.

(5) Whether a piece qualifies as a leaf is decided by the following question — *"can this be written in Lowent."*
      If it can, it is a library. **Convenience does not qualify something as a leaf.**

> [!표] The three layers
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Layer*], [*Who makes it*], [*Qualification*],
> [language], [this document], [cannot be expressed as a library, or expressing it as one hides a cost],
> [leaf], [the processor], [touches the operating system · hardware. Cannot be written in Lowent],
> [library], [anyone], [everything else. Written in Lowent],
> )

```도해: the three layers — reading one file
 library   files.open, files.read, files.slurp   Lowent code: owned handle, result
    |
 leaf      file_open, file_read, file_close      from the processor, needs cap file_system
    |
 OS        open, read, close                     the kernel
 language  add, len, idx, narrow ...           fixed words (6.3.3), used at every layer
```

> [!산문]
> For example, «opening a file and receiving an integer handle» touches the kernel, so it is a leaf. Wrapping that handle
> in **a value that cannot be forgotten** can be written in Lowent, so it is library.
> «A convenience function that reads a whole file» is library too — being convenient does not make it a leaf.

> [!주의] Convenience is not a reason to enter the language
> *"It is convenient to write it this way"* is the business of libraries. Adding a word to the language makes that word something every
> program has to memorise (⟦§6.1.2⟧). So the language accepts only **what cannot be expressed**.

## 9.4 How ripe is it — the ladder of maturity

(1) Each library module writes its **maturity**. There are four rungs, and a higher rung **includes all** the duties of the lower
      rungs.

> [!표] The rungs of maturity and their duties
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Rung*], [*The duties of that rung*],
> [`experimental`], [the maturity **is written**. Nothing more — that is why it is an experiment],
> [`incubating`], [＋ a decision document that owns it **actually exists**],
> [`standard`], [＋ there is a description for people to read, and a regression witness calls that module **by name**],
> [`deprecated`], [＋ what to switch to is written],
> )

(2) A module that does not keep the duties of the rung it writes does not conform. **Raising** a rung
      comes after meeting the duties; if they are not met, it shall stay on the lower rung.

> [!참고]
> **The value of the ladder is not in the upper rungs but in the lower ones.** Writing `experimental` is
> writing *"do not rely on this yet"* **in the source**, and that one line answers for the user *"may I design
> relying on this"*. Without rungs every module looks the same, and then
> **the least ripe is read as if it were the ripest**.

> [!참고]
> A rung is **the size of a promise**, not a ranking of quality. A well-made experimental module can be better than a sloppy standard
> module — what differs is *"how much it has said it will not change"*.

## 9.5 What enters the standard library

(1) Things enter the library **by evidence**, not **by existing**. The following six are the measures
      that decide entry.

> [!표] The library charter
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Measure*], [*What it means*],
> [if it can be written in this language, write it in this language], [something becomes a leaf inside the processor only when it **uses an outside authority** or is work this language cannot express. **Convenience is not a ground**],
> [the signature is the ledger of costs and authority], [effects · capabilities · the caller's storage · allocation · ownership · completion · machine constraints **shall be visible at the boundary**],
> [separate computation from authority], [building a shape and putting it out, parsing and opening a file, the next step of random numbers and obtaining entropy, protocol encoding and carrying are **each different things**],
> [divide places along several axes], [authority · storage · execution tier (⟦§10.8⟧) · profile (⟦§10.7⟧) **are not squeezed into one ladder**],
> [keep what is inside small], [up to a portable base and thin, audited host adapters. Large outside dependencies · policy-heavy facilities · surfaces still in research are not brought in],
> [each module has evidence], [it declares its support boundary and has unit · negative · oracle · failure · profile gates],
> )

(2) This document **does not write a list** of libraries. What exists is decided by a ledger that is counted again from the source
      each time, and if this document copied it, **they would part from the moment of copying.**

> [!산문]
> If the charter doubled as a list, something still a proposal would look **as if it already existed** just because it was in the table.
> That really happened — three APIs that did not exist were read as fact because they were in the table.
> So this clause specifies only **what enters**, and leaves **what has entered**
> to the counting side.
