#import "../lib.typ": *

#clause("2", "Normative references")[
  #sub("2.1", "External standards this document relies on")[
    #para("1")[
      The following documents form part of this document to the extent that this document cites them. For those with an edition
      given, that edition applies; for those without, the latest edition applies.
    ]
    #tbl("Normative references")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Document*], [*What it is relied on for*],
      [ISO/IEC 10646 (Unicode)], [The character set of source files. A Lowent source is a Unicode string
      encoded in UTF-8 (#cref("5.2")).],
      [IEEE 754-2019], [The representation and operations of floating-point numbers. `f32` · `f64` follow it (#cref("6.2.4")).],
      [ISO/IEC 9899 (C)], [The boundary exchanged with the C language (#cref("7.4")). Lowent does not define the meaning of C;
      it #strong[cites]; the C side's promises.],
      )
    ]
    #note[
      Having C as a normative reference does not mean that Lowent is built on top of C. It is because that standard already fixes
      the promises a Lowent program shall keep #strong[when it meets code written in C inside the same machine];.
    ]
  ]
  #sub("2.2", "What this document does not rely on")[
    #para("1")[
      Lowent does not rely on the following. This is a portability requirement of the language.
    ]
    #para("2")[
      a) A particular operating system — the whole language part of this document holds even in an environment without an operating
      system (#t("프리스탠딩", "freestanding")) (#cref("5.1")).
    ]
    #para("3")[
      b) A particular CPU architecture — but the #strong[minimum requirements]; specified by #cref("5.3") are required.
    ]
    #para("4")[
      c) A run-time support library — the language itself requires no runtime.
    ]
    #para("5")[
      d) A #t("가비지 컬렉터", "garbage collector") — this language has none (#cref("8")).
    ]
    #plain[
      What this list means is this — #strong[a computation written in Lowent runs as it is on a small device without an operating
      system.]; Only outside work such as reading a file needs the environment's permission, and that permission is handed over
      as a capability (#cref("7.2")).
    ]
    #note[
      It is no accident that there are only three normative references. The more there is to rely on, the fewer the places this
      language can run.
    ]
  ]
]
