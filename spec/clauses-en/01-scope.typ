#import "../lib.typ": *

#clause("1", "Scope")[
  #sub("1.1", "What this document specifies")[
    #para("1")[
      This document defines the #t("로우엔트", "Lowent") programming language. It specifies the following four things.
    ]
    #para("2")[
      a) The #t("표현", "representation") of programs written in Lowent — which characters, arranged how, form a
      program.
    ]
    #para("3")[
      b) The #t("구문", "syntax") and #t("제약", "constraints") of Lowent programs — which arrangements are
      correct and which are not.
    ]
    #para("4")[
      c) The #t("의미 규칙", "semantic rules") for interpreting Lowent programs — what a correct program
      does.
    ]
    #para("5")[
      d) The #t("적합성", "conformance") requirements that a #t("처리기", "processor") which translates and executes
      Lowent programs shall meet.
    ]
    #plain[
      This document is the only document that answers #emph["what is Lowent"];. Someone learning the language and
      someone building a new Lowent processor both need to read only this one document. No place has been left
      where another document must be found and read.
    ]
  ]
  #sub("1.2", "What this document does not specify")[
    #para("1")[
      The following are outside the scope of this document. Outside the scope means #emph["not specified here"];,
      not #emph["forbidden"];.
    ]
    #para("2")[
      a) How Lowent programs are made — how to use editors, build tools and package managers.
    ]
    #para("3")[
      b) The internal structure of a particular processor — which intermediate representation it uses, how it optimises.
    ]
    #para("4")[
      c) The complete list of the standard library — #cref("9") specifies the #emph[rules]; a library shall follow,
      not the list of individual functions.
    ]
    #para("5")[
      d) The behaviour of operating systems and hardware — this document states only what it requires of them (#cref("5")).
    ]
    #plain[
      #emph["Not specified"]; and #emph["forbidden"]; are easy to confuse. For example, this document does not specify an
      editor, but that does not mean you must not use one; it means #strong[it is not this document's business];.
    ]
  ]
  #sub("1.3", "The purpose of this language")[
    #para("1")[
      Lowent is a systems programming language designed to lower #t("의미 엔트로피", "semantic entropy").
    ]
    #term("의미 엔트로피", "semantic entropy")[
      The #strong[uncertainty that remains]; about a program's actual behaviour after its whole source has been read.
      Put another way, the #strong[amount of information that must be brought in from outside the source]; to know what the code does.
    ]
    #para("2")[
      Every rule of this language was chosen to reduce that uncertainty. Specifically, it removes the following four things.
    ]
    #para("3")[
      a) #strong[Hidden behaviour]; — nothing happens that the source does not state. There is no invisible memory allocation,
      no invisible control flow and no invisible type conversion.
    ]
    #para("4")[
      b) #strong[Hidden cost]; — how expensive a line is, is written on that line.
    ]
    #para("5")[
      c) #strong[Hidden assumptions]; — what a function requires is written as a #t("계약", "contract"), and
      the processor checks it (#cref("6.4")).
    ]
    #para("6")[
      d) #strong[Overlapping notation]; — the same meaning cannot be written in several ways. Different notation means different meaning.
    ]
    #para("7")[
      Low semantic entropy #strong[does not mean that the source corresponds one-to-one to machine code.]; There are places where,
      for efficient implementation and optimisation, the processor must have a choice.
    ]
    #para("8")[
      This language #strong[marks such places and isolates them from the rest.]; Every place where the processor chooses is marked
      (#t("자유 선택", "free choice"), #cref("4.3")), and each place states the range it may choose from. The processor makes no choice at an unmarked place.
    ]
    #para("9")[
      So the person writing a program #strong[can know what is fixed and what is open.]; At an open place they
      #strong[may choose for themselves]; whether to leave it to the processor or to rewrite it in a fixed form.
    ]
    #plain[
      Entropy is the amount of «what stays unknown». The mere existence of choices does not increase entropy — what increases it
      is #strong[not knowing where]; the choices are, and #strong[not knowing how far]; what can happen extends.
    ]
    #plain[
      A language close to the machine cannot tie the processor down so that it chooses nothing. Tied that way, it could not use a
      faster path even when it knew one. Conversely, if places of choice are scattered all over the language, every line read raises
      the question «does the processor choose something here too». That question is the entropy.
    ]
    #plain[
      So this language #strong[gathers the places of choice in one list]; (annex F), #strong[puts a mark]; at each such place in the text, and
      #strong[states the range]; of each. On a line without a mark the reader can trust that what is written happens as written; on a line
      with a mark they know exactly what is open. Even if some uncertainty remains, #strong[knowing its place and its size]; — that is what this language aims at.
    ]
    #note[
      The name «Lowent» is short for #strong[low entropy];. The name is the criterion —
      whether a feature goes in is decided by #emph["does it increase semantic entropy"];.
    ]
  ]
  #sub("1.4", "How to read this document")[
    #para("1")[
      This document distinguishes the #t("규범", "normative") from the #t("참고", "informative").
    ]
    #term("규범", "normative")[
      Text that defines the language. Processors and programs shall obey it. Unless marked otherwise,
      the whole body of this document is normative.
    ]
    #term("참고", "informative")[
      Text that helps understanding. It does not define the language. Boxes marked «Note», «In plain words» and «Caution»
      are informative. Where the normative and the informative appear to disagree, #strong[the normative is right];.
    ]
    #para("2")[
      Clauses are numbered. A number is an #strong[address];, and the meaning of a number once given does not change.
      When another document cites this one, it cites by number (for example #cref("6.4.2")).
    ]
    #para("2a")[
      A number is a sequence of integers joined by dots (annexes begin with a capital letter). #strong[No other form
      is used]; — in particular, numbers such as `3a` are not made in order to insert something in between. If a clause
      must be inserted, #strong[the following numbers shall be shifted];, and that can be done only before this document
      is widely cited.
    ]
    #note[
      There really is #emph["a last moment at which numbers can be shifted"];. While this document is young, moving numbers is
      cheap, but once other documents and tools start pointing at these numbers it becomes expensive.
      That is why this rule is worth something only if it is kept #strong[now];.
    ]
    #para("3")[
      Paragraphs within each clause are numbered too (1, 2, 3 …). A precise citation gives the clause number and the paragraph
      number together (for example 6.4.2 paragraph 3).
    ]
    #para("4")[
      A term is given with its English form in parentheses, and its meaning, where it first appears. The definitions of all terms
      are gathered in #cref("3").
    ]
    #caution("The meaning of the modal verbs used in this document")[
      «shall» (Korean 「-하여야 한다」 · 「-이어야 한다」) is a #strong[requirement];. Not meeting it means not conforming.
      «may» (「-할 수 있다」) is a #strong[permission];. «should» (「-하는 것이 좋다」) is a #strong[recommendation];;
      not following it still conforms. «shall not» (「-하지 아니한다」) is a #strong[prohibition];.
    ]
  ]
  #sub("1.5", "Grammar notation and the frame of a clause")[
    #para("1")[
      This document writes its grammar as #t("생성 규칙", "production")s. There is one notation, the same throughout the document.
    ]
    #term("생성 규칙", "production")[
      A statement of which sequence of units a unit of the grammar consists of. Its form is `name ::= body`.
    ]
    #tbl("Grammar notation")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Notation*], [*Meaning*],
      [`name ::= body`], [`name` consists of `body`],
      [`A B`], [`A` followed by `B`],
      [`A | B`], [`A` or `B`],
      [`[ A ]`], [`A` absent or once],
      [`{ A }`], [`A` zero or more times],
      [`( A )`], [grouping],
      [`"text"`], [those characters, literally],
      [`"a" … "z"`], [one of the characters from `a` to `z`],
      [`? text ?`], [a meaning stated in words. The words are normative],
      [`name`], [refers to another production],
      )
    ]
    #para("2")[
      One or more is written `A { A }`. No other notation is used.
    ]
    #para("3")[
      Production names are written in lowercase English letters and hyphens (`if-statement`). The same name refers to the same rule throughout the document.
    ]
    #para("3a")[
      The grammar has #strong[two layers];. The #t("어휘 문법", "lexical grammar") specifies how a sequence of characters is divided into tokens (#cref("6.1")).
      The #t("구문 문법", "syntactic grammar") specifies how a sequence of tokens is read as grammatical elements.
    ]
    #term("어휘 문법", "lexical grammar")[
      The rules that divide a sequence of characters into a sequence of tokens. The terminal symbols of its productions are #strong[characters];.
    ]
    #term("구문 문법", "syntactic grammar")[
      The rules that read a sequence of tokens as grammatical elements. The terminal symbols of its productions are #strong[tokens];.
    ]
    #para("3b")[
      `"if"` in the syntactic grammar means one token with that spelling. White space and comments between tokens are discarded at the lexical
      stage and do not appear in the syntactic grammar.
    ]
    #para("4")[
      What a production specifies is #strong[form]; only — which sequence of words is read as that grammatical element. Which of the
      well-formed ones conform is not specified by productions but by constraints and static semantics.
    ]
    #para("5")[
      A condition that cannot be written as a production is written as a numbered paragraph directly below that production.
    ]
    #para("6")[
      A clause that specifies a construct of the language has the following sections #strong[in this order];. A section with nothing to say is omitted.
    ]
    #tbl("The frame of a clause")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Section*], [*What it states*],
      [Syntax], [Productions. They specify form only],
      [Constraints], [Conditions a well-formed program shall meet to conform. A program that violates one does not conform],
      [Static semantics], [What is determined at translation for a conforming program — the type of an expression, what a name refers to],
      [Dynamic semantics], [What happens at execution — the order of evaluation, changes of state, stopping],
      [Diagnostics], [The diagnostic identifier a processor issues when a constraint is violated],
      [Examples], [Conforming examples and rejected examples. Each states which paragraph it tests],
      [Notes], [In plain words · Caution · the reasons for a choice. Not normative (⟦§1.4⟧)],
      )
    ]
    #para("7")[
      A constraint states only the requirement. The diagnostic identifier for violating it is given in the diagnostics section — the diagnostic identifier
      does not define the requirement.
    ]
    #para("8")[
      Paragraph numbers do not restart in each section; they continue through the clause.
    ]
    #para("9")[
      Some clauses have not yet been moved into this frame. Such clauses keep their earlier wording, and their normative force is the same.
    ]
    #note[
      A «grammar shape» box is a form to see at a glance. The definition of the grammar is the productions in the syntax section; where the two
      appear to disagree, the production is right.
    ]
    #plain[
      The sections are separated because #strong[whom they speak to]; differs. Syntax speaks to the one who reads text as a tree (the parser),
      constraints and static semantics to the one who checks whether that tree is correct (the checker), and dynamic semantics to the one who runs the program.
      If the three are mixed in one paragraph, the reader has to separate them, and every reader separates them differently.
    ]
  ]
]
