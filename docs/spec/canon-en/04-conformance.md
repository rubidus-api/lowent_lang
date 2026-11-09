# 4 Conformance

## 4.1 Conforming programs

(1) A ⟦적합한 프로그램|conforming program⟧ is a program that keeps all the syntax (⟦§6⟧) and
      constraints (⟦§6⟧) of this document.

(2) A conforming program does not rely on behaviour this document does not specify.

(2a) Diagnostics come in two kinds: **those that stop translation** (their names begin with `E-`) and **those that do
      not** (`W-` · `NOTE-`). Only the former decide conformance; a program can conform even if
      the latter are present.

(2b) That is, a conforming program is a program with **no diagnostic that stops translation**.

```lowent 예제: a conforming program
module ex_conform .

rem This program keeps everything this document specifies.
export fn clamp input n u32 . output u8 .
  requires le n. 255 . .
  ensures le ret. 255 . .
do
  return narrow u8 n. . .
end .

proc main output u8 . effects none .
do
  return 0 .
end .
```

> [!산문]
> Called as `clamp 200`, the program above gives 200. Called as `clamp 300` —
> since the contract said *"at most 255"* — it **stops right there** (`E-VM-CONTRACT`).
> Not returning a wrong answer is this language's promise, and that promise is kept at execution.

> [!주의] Conforming and correct are different things
> A conforming program can still give a wrong answer. Conformance is **whether the rules of the language were kept**,
> not **whether it does what was intended**. The latter is handled by contracts (⟦§6.4.3⟧) and tests.

## 4.2 Conforming processors

(1) A ⟦적합한 처리기|conforming processor⟧ shall satisfy all of the following.

(2) a) It shall translate and execute conforming programs with the meaning this document specifies.

(3) b) It shall issue **at least one diagnostic** for a program that does not conform. Whether translation continues after a diagnostic
      is decided by the processor.

(4) c) Where it uses a diagnostic identifier this document specifies, it does not use it with a changed meaning.

(5) d) Where it provides extensions this document does not specify, they do not change the meaning of a conforming program
      that does not use them.

> [!참고]
> b) does not mean *"it shall find every error"*. For one non-conforming program, it is enough that **at least one**
> diagnostic is issued. Since Lowent's principle is not to postpone checks, however,
> real processors usually collect and report all errors.

## 4.3 Implementation-defined behaviour and free choice

(1) The places where this document leaves discretion to the processor are of **two kinds**, and those places are **explicitly marked**.

> [!용어] 구현 정의 (implementation-defined)
> Something the processor decides but **shall document**. Example: the maximum recursion depth. A program can ask
> for the value, and it may differ between processors.

> [!용어] 자유 선택 (free choice)
> A place where **the choice must be left free** for the sake of optimisation or of the processor's circumstances. The processor
> chooses one of two or more behaviours or results this document allows at that place. The processor **need not
> document** which it chose.

(1a) For each place of ⟦자유 선택|free choice⟧ this document states **the range that may be chosen from**. The processor chooses only within that range.

(1b) The processor may choose differently at the same place of the same program at each translation and each execution.

(1c) ⟦자유 선택|free choice⟧ **is not something left unspecified.** Whichever member of the range is chosen, it is behaviour this document specifies.

(1d) A place of ⟦자유 선택|free choice⟧ is marked in the text with a «free choice» box. Annex F gathers all of them, and a place not in annex F
      is not ⟦자유 선택|free choice⟧. Adding a place or widening a range is a revision of this document.

(1e) A conforming program does not rely on **which one** the processor chooses at a ⟦자유 선택|free choice⟧ (⟦§4.1⟧ (2)).

(1f) There are places this document **has not yet decided**. Such places are not discretion — they are not a choice given to the processor
      but something this document must decide later. They are marked in the text with an «open issue» box and gathered in annex E.

> [!용어] 모호 (open issue)
> A place this document **has not yet decided and must decide later**. The possible readings are written side by side.
> Once decided, it becomes a normative paragraph, or, if it is decided to leave it open, a free choice.

(1g) An open issue and a ⟦자유 선택|free choice⟧ are different. An open issue is **undecided**; a ⟦자유 선택|free choice⟧ is **a decision** — a decision to leave it open.
      A conforming program does not rely on the place of an open issue either (⟦§4.1⟧ (2)).

(2) There is no discretion outside these two kinds. In the **safe subset** (⟦§4.4⟧) undefined behaviour
      does not exist — no program in it is executed in a state of «nobody knows what will
      happen».

> [!산문]
> In plain words the two kinds are these. **Implementation-defined** is «it may differ between processors, but the processor
> writes down what it chose». **Free choice** is «any of these may be chosen, and the choice need not be written down».

> [!산문]
> Free choice exists so that the processor can take a faster path. For example in `add f x. . g y. . .`
> the answer is the same whichever of `f` and `g` is computed first. If the order were nailed down, the processor could not use
> a faster order even when it knew one. So that order is left open.

> [!산문]
> Leaving something open does not mean anything at all may happen. Each place **states the range that may be chosen from**,
> and nothing outside it happens. And such places are exactly the list in annex F — at a place not on the list
> the processor chooses nothing at will. Ranges are fixed narrowly, place by place.

> [!산문]
> The one thing a person writing a program needs to remember: **do not rely on which one is chosen.** If the order matters,
> write it in a form whose order is fixed (the box in ⟦§6.3.1⟧ says how).

## 4.4 The safe subset and trust boundaries

(1) The ⟦안전한 부분집합|safe subset⟧ is the set of programs that do not use `unsafe` and `extern`.
      The guarantees of the previous clause are about this set.

(2) `unsafe` and `extern` mark a ⟦신뢰 경계|trust boundary⟧ (⟦§6.9⟧).
      At that place the language's guarantee ends and **a promise written by a person** begins.

(3) If the promises beyond the boundary are true, the behaviour of the whole program is as this document specifies. If a promise is false,
      this document guarantees nothing — what happens then is not determined.

(4) So boundaries **shall be narrow**. The wider they are, the larger the area this document's guarantees do not reach.

> [!산문]
> This clause exists for the sake of honesty. One cannot call a function written in another language and say *"our language
> has no undefined behaviour"* — this document does not know what that function will do.
> What can be said is **where the language's guarantee stops and where a person's promise begins**,
> and `unsafe` and `extern` **leave that place in the source**. Being able to find it later is
> as important as the guarantee.

## 4.5 Environmental limits

(1) A processor may have resource limits. But when it hits a limit it **shall not silently
      truncate** — it shall issue a diagnostic or trap.

(2) A processor shall document the limits it supports (implementation-defined).

(3) The diagnostics issued when a limit is hit are grouped into families — when a table inside the processor overflows,
      `E-IR-…` · `E-TYPE-LIMIT` · `E-PARSE-LIMIT` are issued; when a resource runs out during execution, `E-VM-…`. This document
      does not list them one by one; **what is required is (1) and (2)** — not truncating silently, and
      documenting the limits.

> [!산문]
> The names of the diagnostics are not nailed into the norm one by one because they are **the processor's internal
> affairs**. Splitting one table into two changes the names too, and if the norm had to be revised each time, that
> norm would be no more than a shadow following the processor. What the norm shall fix is **what is promised**,
> and the promise is two things: «say so when it overflows» and «write down how far it goes».

> [!주의]
> Writing down a limit and **keeping** a limit are different. Writing it down but truncating silently on overflow
> violates (1); keeping it but not writing it down means the user learns it existed **only after hitting it**.
> Both are needed.

> [!참고]
> *"Not truncating silently"* is the point of this clause. Every processor has limits.
> The problem is going past a limit and **carrying on without a word**; the program's result then
> differs from what the source said — and that is semantic entropy (⟦§1.3⟧).

## 4.6 The edition of this document and the edition of the language

(1) This document has **an edition number separate** from the language. The «document edition» on the cover is this document's edition,
      and the «language edition» is the edition of the language this document describes.

(2) A rise in the last digit of the document edition means **the text grew or was corrected**, not that the language changed.
      When the language changes, the language edition rises, and the middle digit or higher of this document rises with it.

## 4.7 What is in this document but a processor cannot yet do

(1) There may be things this document specifies that a processor **cannot do yet**. Then
      the processor **does not treat them as if they did not exist.**

(2) At such a place the processor shall say *"this word is in the specification, and what cannot do it is the
      processor"*. That is, it is not an error of the program.

(3) This distinction is normative because **what a person does next differs**. Told *"unknown name"*,
      a person goes looking for a typo that is not there; told *"not yet"*, they look for another way or
      wait. One diagnostic decides half a person's day.

(4) A processor shall make such places **countable** — asked which ops passed every static check but cannot be
      lowered to execution, it shall answer.

> [!산문]
> This is not a clause that «excuses incompleteness». It is the opposite — a clause that says **show** the
> incompleteness. Between a processor that silently cannot do something and one that says it cannot, there is
> a difference in trust.
