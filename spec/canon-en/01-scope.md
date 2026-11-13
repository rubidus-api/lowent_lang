# 1 Scope

## 1.1 What this document specifies

(1) This document defines the ⟦로우엔트|Lowent⟧ programming language. It specifies the following four things.

(2) a) The ⟦표현|representation⟧ of programs written in Lowent — which characters, arranged how, form a
      program.

(3) b) The ⟦구문|syntax⟧ and ⟦제약|constraints⟧ of Lowent programs — which arrangements are
      correct and which are not.

(4) c) The ⟦의미 규칙|semantic rules⟧ for interpreting Lowent programs — what a correct program
      does.

(5) d) The ⟦적합성|conformance⟧ requirements that a ⟦처리기|processor⟧ which translates and executes
      Lowent programs shall meet.

> [!산문]
> This document is the only document that answers *"what is Lowent"*. Someone learning the language and
> someone building a new Lowent processor both need to read only this one document. No place has been left
> where another document must be found and read.

## 1.2 What this document does not specify

(1) The following are outside the scope of this document. Outside the scope means *"not specified here"*,
      not *"forbidden"*.

(2) a) How Lowent programs are made — how to use editors, build tools and package managers.

(3) b) The internal structure of a particular processor — which intermediate representation it uses, how it optimises.

(4) c) The complete list of the standard library — ⟦§9⟧ specifies the *rules* a library shall follow,
      not the list of individual functions.

(5) d) The behaviour of operating systems and hardware — this document states only what it requires of them (⟦§5⟧).

> [!산문]
> *"Not specified"* and *"forbidden"* are easy to confuse. For example, this document does not specify an
> editor, but that does not mean you must not use one; it means **it is not this document's business**.

## 1.3 The purpose of this language

(1) Lowent is a systems programming language designed to lower ⟦의미 엔트로피|semantic entropy⟧.

> [!용어] 의미 엔트로피 (semantic entropy)
> The **uncertainty that remains** about a program's actual behaviour after its whole source has been read.
> Put another way, the **amount of information that must be brought in from outside the source** to know what the code does.

(2) Every rule of this language was chosen to reduce that uncertainty. Specifically, it removes the following four things.

(3) a) **Hidden behaviour** — nothing happens that the source does not state. There is no invisible memory allocation,
      no invisible control flow and no invisible type conversion.

(4) b) **Hidden cost** — how expensive a line is, is written on that line.

(5) c) **Hidden assumptions** — what a function requires is written as a ⟦계약|contract⟧, and
      the processor checks it (⟦§6.4⟧).

(6) d) **Overlapping notation** — the same meaning cannot be written in several ways. Different notation means different meaning.

(7) Low semantic entropy **does not mean that the source corresponds one-to-one to machine code.** There are places where,
      for efficient implementation and optimisation, the processor must have a choice.

(8) This language **marks such places and isolates them from the rest.** Every place where the processor chooses is marked
      (⟦자유 선택|free choice⟧, ⟦§4.3⟧), and each place states the range it may choose from. The processor makes no choice at an unmarked place.

(9) So the person writing a program **can know what is fixed and what is open.** At an open place they
      **may choose for themselves** whether to leave it to the processor or to rewrite it in a fixed form.

> [!산문]
> Entropy is the amount of «what stays unknown». The mere existence of choices does not increase entropy — what increases it
> is **not knowing where** the choices are, and **not knowing how far** what can happen extends.

> [!산문]
> A language close to the machine cannot tie the processor down so that it chooses nothing. Tied that way, it could not use a
> faster path even when it knew one. Conversely, if places of choice are scattered all over the language, every line read raises
> the question «does the processor choose something here too». That question is the entropy.

> [!산문]
> So this language **gathers the places of choice in one list** (annex F), **puts a mark** at each such place in the text, and
> **states the range** of each. On a line without a mark the reader can trust that what is written happens as written; on a line
> with a mark they know exactly what is open. Even if some uncertainty remains, **knowing its place and its size** — that is what this language aims at.

> [!참고]
> The name «Lowent» is short for **low entropy**. The name is the criterion —
> whether a feature goes in is decided by *"does it increase semantic entropy"*.

## 1.4 How to read this document

(1) This document distinguishes the ⟦규범|normative⟧ from the ⟦참고|informative⟧.

> [!용어] 규범 (normative)
> Text that defines the language. Processors and programs shall obey it. Unless marked otherwise,
> the whole body of this document is normative.

> [!용어] 참고 (informative)
> Text that helps understanding. It does not define the language. Boxes marked «Note», «In plain words» and «Caution»
> are informative. Where the normative and the informative appear to disagree, **the normative is right**.

(2) Clauses are numbered. A number is an **address**, and the meaning of a number once given does not change.
      When another document cites this one, it cites by number (for example ⟦§6.4.2⟧).

(2a) A number is a sequence of integers joined by dots (annexes begin with a capital letter). **No other form
      is used** — in particular, numbers such as `3a` are not made in order to insert something in between. If a clause
      must be inserted, **the following numbers shall be shifted**, and that can be done only before this document
      is widely cited.

> [!참고]
> There really is *"a last moment at which numbers can be shifted"*. While this document is young, moving numbers is
> cheap, but once other documents and tools start pointing at these numbers it becomes expensive.
> That is why this rule is worth something only if it is kept **now**.

(3) Paragraphs within each clause are numbered too (1, 2, 3 …). A precise citation gives the clause number and the paragraph
      number together (for example 6.4.2 paragraph 3).

(4) A term is given with its English form in parentheses, and its meaning, where it first appears. The definitions of all terms
      are gathered in ⟦§3⟧.

> [!주의] The meaning of the modal verbs used in this document
> «shall» (Korean 「-하여야 한다」 · 「-이어야 한다」) is a **requirement**. Not meeting it means not conforming.
> «may» (「-할 수 있다」) is a **permission**. «should» (「-하는 것이 좋다」) is a **recommendation**;
> not following it still conforms. «shall not» (「-하지 아니한다」) is a **prohibition**.

## 1.5 Grammar notation and the frame of a clause

(1) This document writes its grammar as ⟦생성 규칙|production⟧s. There is one notation, the same throughout the document.

> [!용어] 생성 규칙 (production)
> A statement of which sequence of units a unit of the grammar consists of. Its form is `name ::= body`.

> [!표] Grammar notation
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Notation*], [*Meaning*],
> [`name ::= body`], [`name` consists of `body`],
> [`A B`], [`A` followed by `B`],
> [`A | B`], [`A` or `B`],
> [`[ A ]`], [`A` absent or once],
> [`{ A }`], [`A` zero or more times],
> [`( A )`], [grouping],
> [`"text"`], [those characters, literally],
> [`"a" … "z"`], [one of the characters from `a` to `z`],
> [`? text ?`], [a meaning stated in words. The words are normative],
> [`name`], [refers to another production],
> )

(2) One or more is written `A { A }`. No other notation is used.

(3) Production names are written in lowercase English letters and hyphens (`if-statement`). The same name refers to the same rule throughout the document.

(3a) The grammar has **two layers**. The ⟦어휘 문법|lexical grammar⟧ specifies how a sequence of characters is divided into tokens (⟦§6.1⟧).
      The ⟦구문 문법|syntactic grammar⟧ specifies how a sequence of tokens is read as grammatical elements.

> [!용어] 어휘 문법 (lexical grammar)
> The rules that divide a sequence of characters into a sequence of tokens. The terminal symbols of its productions are **characters**.

> [!용어] 구문 문법 (syntactic grammar)
> The rules that read a sequence of tokens as grammatical elements. The terminal symbols of its productions are **tokens**.

(3b) `"if"` in the syntactic grammar means one token with that spelling. White space and comments between tokens are discarded at the lexical
      stage and do not appear in the syntactic grammar.

(4) What a production specifies is **form** only — which sequence of words is read as that grammatical element. Which of the
      well-formed ones conform is not specified by productions but by constraints and static semantics.

(5) A condition that cannot be written as a production is written as a numbered paragraph directly below that production.

(6) A clause that specifies a construct of the language has the following sections **in this order**. A section with nothing to say is omitted.

> [!표] The frame of a clause
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Section*], [*What it states*],
> [Syntax], [Productions. They specify form only],
> [Constraints], [Conditions a well-formed program shall meet to conform. A program that violates one does not conform],
> [Static semantics], [What is determined at translation for a conforming program — the type of an expression, what a name refers to],
> [Dynamic semantics], [What happens at execution — the order of evaluation, changes of state, stopping],
> [Diagnostics], [The diagnostic identifier a processor issues when a constraint is violated],
> [Examples], [Conforming examples and rejected examples. Each states which paragraph it tests],
> [Notes], [In plain words · Caution · the reasons for a choice. Not normative (⟦§1.4⟧)],
> )

(7) A constraint states only the requirement. The diagnostic identifier for violating it is given in the diagnostics section — the diagnostic identifier
      does not define the requirement.

(8) Paragraph numbers do not restart in each section; they continue through the clause.

(9) Some clauses have not yet been moved into this frame. Such clauses keep their earlier wording, and their normative force is the same.

> [!참고]
> A «grammar shape» box is a form to see at a glance. The definition of the grammar is the productions in the syntax section; where the two
> appear to disagree, the production is right.

> [!산문]
> The sections are separated because **whom they speak to** differs. Syntax speaks to the one who reads text as a tree (the parser),
> constraints and static semantics to the one who checks whether that tree is correct (the checker), and dynamic semantics to the one who runs the program.
> If the three are mixed in one paragraph, the reader has to separate them, and every reader separates them differently.
