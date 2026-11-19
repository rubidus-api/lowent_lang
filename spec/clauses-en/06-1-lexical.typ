#import "../lib.typ": *

#clause("6", "Language")[
  #para("1")[
    This clause specifies the grammar and meaning of Lowent. It first specifies which pieces a source is divided into
      (#cref("6.1")), and then, in turn, what those pieces mean.
  ]
  #sub("6.1", "Lexical elements")[
    #part("Syntax")
    #syntax("source", "source    ::= { separator | token }
separator ::= whitespace | line-comment | block-comment
token     ::= keyword | name | path | literal | punctuation

punctuation ::= \".\" | \"(\" | \")\" | \",\" | operator
operator    ::= \"+\" | \"-\" | \"*\" | \"/\"")
    #part("Static semantics")
    #para("1")[
      The processor reads a source file from the front and divides it into a sequence of `separator`s and #t("토큰", "token")s. Tokens are of five kinds —
      keywords (`keyword`), names (`name`), paths (`path`, #cref("6.1.3")), literals (`literal`, #cref("6.1.4")) and punctuation (`punctuation`).
    ]
    #para("2")[
      A `separator` divides tokens and #strong[is discarded.]; It has no other meaning.
    ]
    #para("2a")[
      Of the punctuation, a detached dot `.` closes a form and `(` `)` group a form (#cref("6.1.5") · #cref("6.1.6")). An `operator` is an operator
      only inside an `expr` island (#cref("6.3.2")).
    ]
    #para("3")[
      A run of letters, digits and underscores is read #strong[as one piece, without breaking it];, and then its kind is decided. `iffy` is not
      something beginning with the keyword `if` but one name, and `remark` is a name, not a comment.
    ]
  ]
  #sub("6.1.1", "White space and comments")[
    #part("Syntax")
    #syntax("separator", "whitespace      ::= \" \" | tab | carriage-return | newline
blank           ::= \" \" | tab | carriage-return
tab             ::= ? U+0009 ?
carriage-return ::= ? U+000D ?
newline         ::= ? U+000A ?
line-comment    ::= \"rem\" { line-char }
block-comment   ::= \"note\" blank { blank } tag { line-char } newline
                    { comment-line newline }
                    tag { blank } ( newline | end-of-source )
tag             ::= name
line-char       ::= ? a character that is not newline ?
comment-line    ::= ? a line that is not the closing line ?
end-of-source   ::= ? the end of the source ?")
    #para("1")[
      The #strong[closing line]; of a `block-comment` is a line that begins, in its very first column, with the same characters as the opening line's
      `tag`, immediately followed by something that is not a `name-char` (#cref("6.1.3")). Every other line is a `comment-line` — lines that begin with
      an indented `tag`, or with a word longer than `tag`, are such lines.
    ]
    #part("Constraints")
    #para("2")[
      `rem` and `note` open a comment only when what immediately follows is not a `name-char` — `remark` and `notes` are names (#cref("6.1") (3)).
    ]
    #para("3")[
      `note` shall be followed by a `tag`.
    ]
    #para("4")[
      After the `tag` that closes a `block-comment`, only `blank`s and a `newline` may follow.
    ]
    #para("5")[
      An open `block-comment` shall meet its closing line before the source ends.
    ]
    #part("Static semantics")
    #para("6")[
      White space (space · tab · carriage return · line feed) divides tokens. It has no meaning other than dividing tokens.
    ]
    #para("7")[
      From a place beginning with `rem` to the end of that line is a #t("주석", "comment"), and the processor treats it as absent.
      A `block-comment` is a comment too — everything from its opening line to its closing line is treated as absent.
    ]
    #para("8")[
      Unicode characters may be used inside comments.
    ]
    #para("9")[
      `rem` and `note` are not names. A place beginning with either of them is always a comment.
    ]
    #part("Diagnostics")
    #tbl("Diagnostics of comments")[
      #table(columns: (1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Violation*], [*Diagnostic*],
      [(3) — no tag after `note`], [`E-NOTE-TERM`],
      [(4) — other characters after the closing tag], [`E-NOTE-TAIL`],
      [(5) — the source ended without a closing line], [`E-NOTE-UNTERM`],
      )
    ]
    #part("Examples")
    #ex("comments", "rem This whole line is a comment. Hangul may be used too.
let n u32 42 .   rem From here to the end of the line is a comment too.")
    #ex("a multi-line comment", "module ex_note .

note DOC
An explanation spanning several lines.
The processor treats it as absent.
DOC

fn f output u8 . do return 1 . end .",
      out: "f() = 1")
    #part("Notes")
    #note[
      The word that opens a comment is `rem` because it is a #strong[word];. This language has no comment marks made of
      symbols (`//` · `/* */`) — unifying the vocabulary as words reduces the reading rules to one.
    ]
    #note[
      A `block-comment` has the same shape as a text literal (#cref("6.1.4")) but is #strong[a comment, not a value];. The rule for the closing line is the same.
      A one-line comment is written with `rem`.
    ]
  ]
  #sub("6.1.2", "Keywords")[
    #part("Syntax")
    #syntax("keyword", "keyword ::= \"actor\" | \"break\" | \"case\" | \"continue\" | \"contract\" | \"cycle\" | \"do\"
          | \"drop\" | \"else\" | \"end\" | \"enum\" | \"expect\" | \"export\" | \"expr\"
          | \"extern\" | \"false\" | \"fn\" | \"for\" | \"guard\" | \"if\" | \"keep\"
          | \"let\" | \"lit\" | \"match\" | \"module\" | \"newtype\" | \"next\" | \"none\"
          | \"proc\" | \"range\" | \"repeat\" | \"return\" | \"satisfies\" | \"send\" | \"set\"
          | \"spawn\" | \"state\" | \"step\" | \"struct\" | \"test\" | \"trait\" | \"true\"
          | \"try\" | \"type\" | \"unsafe\" | \"use\" | \"var\" | \"while\"")
    #part("Constraints")
    #para("1")[
      A #t("낱말", "keyword") is a name whose meaning the language has fixed, and it cannot be used with another meaning. A spelling the same as a keyword cannot be a name.
    ]
    #para("2")[
      The list of keywords is fixed. The production above is all of it, and it is the same as the list in annex A.
    ]
    #para("2a")[
      There is no keyword that is only reserved and not used. For every keyword there is a construct in this document that uses it.
    ]
    #para("3")[
      A type declaration begins with its own keyword — `struct` · `enum` · `type` · `newtype`. Like other declarations (`fn` · `proc` · `actor` · `trait` ·
      `test` · `module`), it has no word in front.
    ]
    #para("4")[
      `def` is not a keyword. It can be an author's name. But the old heads `def struct` · `def enum` · `def type` · `def newtype` are
      recognised by that shape and refused.
    ]
    #para("5")[
      `export` comes before the keyword of a declaration: `export struct rect do … end .`.
    ]
    #part("Diagnostics")
    #tbl("Diagnostics of keywords")[
      #table(columns: (1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Violation*], [*Diagnostic*],
      [(1) — a keyword was used as a name], [`E-NAME-KEYWORD`],
      [(4) — a type declaration began with `def`], [`E-DEF-OLD`],
      )
    ]
    #part("Examples")
    #rejected("a type declaration began with the old head `def`", "module ex_defold .

def struct pt do
  x u32 .
end .", "E-DEF-OLD")
    #ex("`def` can be an author's name", "module ex_defname .

struct pt do
  x u32 .
end .

fn twice input def u64 . output u64 . do
  return add def. def. . .
end .",
      out: "twice(4) = 8")
    #part("Notes")
    #note[
      Keywords are a #strong[budget];. Adding a keyword makes the language larger, so what can be expressed with existing keywords is not given a new keyword.
    ]
    #plain[
      The word «budget» may be unfamiliar. It means this — the keywords are counted and managed, and each time one is
      added, it is weighed whether it is worth that much. The number of keywords in this language is kept at a size a person can
      hold on one screen and memorise.
    ]
  ]
  #sub("6.1.3", "Identifiers")[
    #part("Syntax")
    #syntax("name", "name       ::= name-start { name-char }
name-start ::= \"a\" … \"z\" | \"A\" … \"Z\" | \"_\"
name-char  ::= name-start | \"0\" … \"9\"
path       ::= name \".\" path-part { \".\" path-part }
path-part  ::= name-char { name-char }")
    #para("1")[
      The dots of a `path` are #strong[attached]; to the characters before and after them. A dot separated by a space is not part of a `path` but punctuation (#cref("6.1.5")).
    ]
    #part("Constraints")
    #para("2")[
      A `name` cannot have the same spelling as a keyword (#cref("6.1.2")).
    ]
    #para("3")[
      Unicode characters cannot be used in names (#cref("5.2")). The characters that make up a name are only the ASCII characters the production above specifies.
    ]
    #para("4")[
      #strong[A name that is made has no dot.]; A name with a dot attached cannot be declared.
    ]
    #para("5")[
      A spelling the same as the name of a core op (#cref("6.3.3")) cannot be used as a parameter or local name. The names of builtin ops
      are in their own namespace and so are not covered by this rule.
    ]
    #part("Static semantics")
    #para("6")[
      An #t("이름", "identifier") is a name the author makes. Modules · ops · types · variables have names.
    ]
    #para("7")[
      A dot appears #strong[only when pointing.]; A `path` is #strong[a route that points at a declared name];, and its meaning is only the three that #cref("6.1.5") (5) specifies —
      the name of a module, the name of a variant, the name of a declaration belonging to a type. A dot is not part of a name but #strong[the mark that joins names];.
    ]
    #para("8")[
      Choosing a field inside a value is not written as a `path`. That is the `field` form (#cref("6.2.10")).
    ]
    #part("Diagnostics")
    #tbl("Diagnostics of identifiers")[
      #table(columns: (1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Violation*], [*Diagnostic*],
      [(2) — a keyword was used as a name], [`E-NAME-KEYWORD`],
      [(3) — a name contains a character that is not ASCII], [`E-NAME-ASCII`],
      [(4) — a name with a dot attached was declared], [`E-NAME-DOTTED`],
      [(5) — the name of a core op was used as a local name], [`E-NAME-BUILTIN`],
      [(8) — a field of a value was written with an attached dot (`p.x`)], [`E-FIELD-GLUED`],
      )
    ]
    #part("Examples")
    #rejected("a name with a dot attached cannot be made", "module ex_dotted .

fn f output u8 .
do
  let a.b u8 1 .
  return 1 .
end .", "E-NAME-DOTTED")
    #part("Notes")
    #plain[
      This language #strong[has no]; nested namespaces. To put a hierarchy into a name, write it with underscores
      (`http_header_parse`). If dots were allowed in names, a reader could not tell whether `a.b` is #emph["field b of a"]; or #emph["the name
      a.b"]; — and that is semantic entropy (#cref("1.3")).
    ]
    #plain[
      Using a keyword as a name is refused #strong[at the place of declaration]; — it does not wait for a use.
      The diagnostic points at the name that is the problem.
    ]
    #caution("Same names do not merely confuse; they are wrong")[
      Some languages allow an inner name to #strong[hide]; an outer name (shadowing). A hidden name makes a person reading
      the source #strong[think of a different value];, and that is semantic entropy
      (#cref("1.3")). So this language #strong[forbids hiding entirely]; — none of the names of core ops,
      the names in the module, the names of parameters, and the names alive in an outer block
      can be hidden (#cref("6.4.8")).
    ]
  ]
  #sub("6.1.4", "Literals")[
    #part("Syntax")
    #syntax("literal", "literal        ::= number | char-literal | string-literal | text-literal

number         ::= [ sign ] ( float | hex | bin | dec )
sign           ::= \"+\" | \"-\"
digit          ::= \"0\" … \"9\"
hex-digit      ::= digit | \"a\" … \"f\" | \"A\" … \"F\"
bin-digit      ::= \"0\" | \"1\"
dec            ::= digit { [ \"_\" ] digit }
hex            ::= \"0\" ( \"x\" | \"X\" ) hex-digit { [ \"_\" ] hex-digit }
bin            ::= \"0\" ( \"b\" | \"B\" ) bin-digit { [ \"_\" ] bin-digit }
float          ::= dec \".\" dec [ dec-exponent ]
                 | dec dec-exponent
                 | hex [ \".\" hex-digit { [ \"_\" ] hex-digit } ] hex-exponent
dec-exponent   ::= ( \"e\" | \"E\" ) [ sign ] dec
hex-exponent   ::= ( \"p\" | \"P\" ) [ sign ] dec

char-literal   ::= [ prefix ] single-quote ( char-char | escape ) single-quote
string-literal ::= [ prefix ] double-quote { string-char | escape } double-quote
                 | raw-string
raw-string     ::= \"r\" double-quote { raw-char } double-quote
raw-char       ::= ? one byte that is neither a double quote nor a line feed ?
prefix         ::= \"u\" | \"U\"
escape         ::= backslash ( simple-escape
                             | \"x\" hex-digit hex-digit
                             | \"u\" hex-digit hex-digit hex-digit hex-digit
                             | \"U\" hex-digit hex-digit hex-digit hex-digit hex-digit hex-digit hex-digit hex-digit )
simple-escape  ::= backslash | double-quote | single-quote
                 | \"a\" | \"b\" | \"f\" | \"n\" | \"r\" | \"t\" | \"v\" | \"0\"
single-quote   ::= ? U+0027 ?
double-quote   ::= ? U+0022 ?
backslash      ::= ? U+005C ?
char-char      ::= ? one character that is not a single quote, a backslash or a line feed ?
string-char    ::= ? one byte that is not a double quote, a backslash or a line feed ?

text-literal   ::= \"text\" blank { blank } tag [ blank { blank } processor ] { blank } [ line-comment ] newline
                   { text-line newline }
                   tag { blank } ( newline | end-of-source )
processor      ::= \"u\" | \"U\"
text-line      ::= ? a line that is not the closing line ?")
    #para("1")[
      A `sign` is a sign only when it is #strong[attached]; to the number. `-5` is one `number`, and `- 5` is not. #strong[Outside]; an `expr` island (#cref("6.3.2"))
      a sign attached to a number is part of the value; #strong[inside]; an island `-` and `+` are operators — so `g 10 -3` in a form position is
      two arguments.
    ]
    #para("2")[
      Without a point or an exponent it is read as `dec`; with a point or an exponent it is read as `float`. There shall be digits #strong[both before and after];
      the point for a `float` — `.5` and `1.` are not `float`s, and that point is read as the mark that closes a form (#cref("6.1.6")).
    ]
    #para("3")[
      A `prefix` is a prefix only when it is #strong[attached]; to the quote. If there is a space between, it is one name and one literal.
    ]
    #para("4")[
      The #strong[closing line]; of a `text-literal` is a line that begins, in its very first column, with the same characters as the opening line's `tag`,
      immediately followed by something that is not a `name-char` (#cref("6.1.3")). Every other line is a `text-line` — lines that begin with an indented `tag`,
      and lines that begin with `tag` but continue as a longer word (`DOCS` when `tag` is `DOC`), are such lines.
    ]
    #part("Constraints")
    #para("5")[
      An underscore comes only #strong[between]; digits. It cannot come at the very start or end of a number, directly after a radix mark, or directly after another underscore.
    ]
    #para("6")[
      After a radix mark (`0x` · `0b`) or an exponent mark there shall be at least one digit of that radix.
    ]
    #para("7")[
      A `number` cannot be directly followed by a letter or an underscore.
    ]
    #para("8")[
      The value of an integer literal shall be within the range of the type of the place where it is used. A floating-point literal too shall be within the range
      the type of its place can represent.
    ]
    #para("9")[
      A `float` shall be written in a floating-point place and an integer literal in an integer place. The two do not
      convert into each other — in an `f64` place write `3.0`, not `3`.
    ]
    #para("10")[
      A `char-literal` shall fit in #strong[one]; element as decided by the prefix. Neither an empty one nor one exceeding one element can be written.
    ]
    #para("11")[
      The `escape`s are #strong[only the fourteen]; the production above specifies. No other character can be written after a backslash.
    ]
    #tbl("Escapes — a closed set of fourteen")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Written*], [*Value*], [*Meaning*],
      [`\\`], [0x5C], [the backslash itself],
      [`\"`], [0x22], [double quote],
      [`\'`], [0x27], [single quote],
      [`\a`], [0x07], [alert (bell)],
      [`\b`], [0x08], [backspace],
      [`\f`], [0x0C], [form feed],
      [`\n`], [0x0A], [line feed],
      [`\r`], [0x0D], [carriage return],
      [`\t`], [0x09], [horizontal tab],
      [`\v`], [0x0B], [vertical tab],
      [`\0`], [0x00], [zero byte],
      [`\xNN`], [0x00–0xFF], [#strong[two]; hexadecimal digits],
      [`\uXXXX`], [code point], [#strong[four]; hexadecimal digits],
      [`\UXXXXXXXX`], [code point], [#strong[eight]; hexadecimal digits],
      )
    ]
    #para("12")[
      The value written by `\uXXXX` and `\UXXXXXXXX` shall be a code point. Surrogate positions (D800 to DFFF) and values above 10FFFF
      cannot be written.
    ]
    #para("13")[
      The `prefix`es are only `u` and `U`. No other word can be attached to a quote. The `r` of a raw string (13a) attaches only to a double quote —
      there is no `r'…'`.
    ]
    #term("날 문자열", "raw string")[
      A string literal that holds its body exactly as written. A backslash does not open an escape, and it ends at the closing double quote.
    ]
    #para("13a")[
      The body of a #t("날 문자열", "raw string") `r"…"` #strong[has no escapes.]; A backslash is just a byte too, and the literal ends at the closing
      double quote. So a raw string cannot contain a double quote — if one is needed, use an ordinary string (`\"`). Its elements are bytes (`slice u8`),
      and it is not used together with `u` · `U`. It is the place to hold, exactly as written, text with many backslashes, such as regular expressions, paths and format strings.
    ]
    #para("14")[
      After the `tag` that closes a `text-literal`, only `blank`s and a `newline` may follow. The dot that closes the statement is written on the #strong[next line];.
    ]
    #para("15")[
      On the line that opens a `text-literal`, only a `processor` and a comment may follow the `tag`. `text` shall be followed by a `tag`.
    ]
    #part("Static semantics")
    #para("16")[
      A #t("리터럴", "literal") is a value written directly in the source.
    ]
    #para("17")[
      Integer literals are written in decimal (`dec`) · hexadecimal (`hex`) · binary (`bin`). Underscores do not count in the value (`1_000_000`).
    ]
    #para("18")[
      #strong[A leading 0 is not octal.]; `0755` is 755. This language has no octal notation.
    ]
    #para("19")[
      An integer literal #strong[has no type by itself.]; The place where it is used decides its type.
    ]
    #para("19-1")[
      The precision in which the processor holds an integer literal at translation is #strong[128 bits]; — up to 2#super[128]−1 unsigned, and down to −2#super[127] signed.
      A literal beyond 64 bits (the end of `u64` · the end of `i64`) fits only in a 128-bit place (#cref("6.2.2") (3b)); in other places translation is refused.
      A literal beyond 128 bits is refused everywhere (#cref("6.1.8") (3)).
    ]
    #para("19a")[
      An expression made only of literals --- arithmetic between literals, a shift whose left side is a literal (`shl 1 n.`) --- has no type either. If the place that takes
      that expression (a return · a binding · the other side of an arithmetic operation · an argument of a call · `set`) decides an integer type, #strong[the expression is computed in that type.]; If it overflows,
      or the shift count is the width or more, it stops like any other computation (#cref("6.3.4") · #cref("6.3.5")). In a shift only the left side takes that type --- the right side is the shift count.
    ]
    #para("20")[
      A #t("부동소수점 리터럴", "floating-point literal") is a number written as a `float` — `1.5` · `1e3` · `1.5e-3`. In hexadecimal exponent notation
      what follows `p` is a power of 2 (`0x1p3` is 8.0).
    ]
    #para("21")[
      There is no literal that writes `inf` or `nan`. Such values are obtained by computation.
    ]
    #para("22")[
      True and false are written with the keywords `true` · `false`. They are of type `bool` and do not convert to and from integers (#cref("6.2.3")).
    ]
    #para("23")[
      The value of a #t("문자 리터럴", "character literal") is the code of that character, an integer.
    ]
    #para("24")[
      The value of a #t("문자열 리터럴", "string literal") is a row of elements. `len` gives #strong[the number of elements];.
    ]
    #para("25")[
      An `escape` beginning with a backslash is called an #t("이스케이프", "escape"). `\xNN` consumes #strong[exactly]; two hexadecimal digits, `\uXXXX` four,
      and `\UXXXXXXXX` eight — the number of digits does not depend on the value.
    ]
    #para("26")[
      `\uXXXX` and `\UXXXXXXXX` #strong[write a code point.]; Which elements that code point is carried as is decided by the prefix —
      UTF-8 bytes without a prefix, UTF-16 code units with `u`, and the code point as it is with `U`.
    ]
    #para("27")[
      A #t("접두사", "prefix") decides #strong[what the elements]; of the literal are. Without a prefix they are bytes; `u` makes them UTF-16 code units and
      `U` code points. Character literals are the same — `u'…'` is one code unit and `U'…'` one code point.
    ]
    #tbl("What the prefix decides — `len` is always the number of elements")[
      #table(columns: (auto, auto, auto, auto, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Written*], [*Element*], [*`len "한" .`*], [*`len "ab" .`*], [*`len "😀" .`*],
      [`"…"` (none)], [byte], [3], [2], [4],
      [`u"…"`], [UTF-16 code unit], [1], [2], [2],
      [`U"…"`], [code point], [1], [2], [1],
      )
    ]
    #para("28")[
      A #t("텍스트 리터럴", "text literal") is a string that holds several lines as they are. Its value is the `text-line`s joined with the line feeds between
      them. #strong[The one line feed directly before the closing line is not part of the value]; — to leave a line feed at the end of the body, put
      one empty line before the closing line.
    ]
    #para("29")[
      In the body of a text literal #strong[escapes are not processed.]; The bytes written are the value.
    ]
    #para("30")[
      The `processor` decides the elements of the value — `text TAG u` is a sequence of UTF-16 code units (`slice u16`), and `text TAG U` is
      a sequence of code points (`slice u32`). Without a `processor` it is a sequence of bytes (`slice u8`).
    ]
    #part("Diagnostics")
    #tbl("Diagnostics of literals")[
      #table(columns: (1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Violation*], [*Diagnostic*],
      [(5) — an underscore is somewhere other than between digits], [`E-NUM-SEP`],
      [(6) — no digit after a radix mark or exponent mark], [`E-NUM-EMPTY`],
      [(7) — a letter directly follows a number], [`E-NUM-SUFFIX`],
      [(8) — a literal is outside the range of the type of its place], [`E-TYPE-WIDTH`],
      [(9) — an integer literal was written in a floating-point place], [`E-TYPE-LET`],
      [(19-1) — an integer literal beyond 64 bits is in a place that is not 128 bits], [`E-LIT-RANGE`],
      [(10) — a character literal is empty], [`E-CHAR-EMPTY`],
      [(10) — a character literal exceeds one element], [`E-CHAR-WIDTH`],
      [(11) · (12) — an escape is outside the set or is not a code point], [`E-STR-ESCAPE`],
      [(13) — the prefix is not `u` · `U`, or `r` was attached to a single quote], [`E-STR-PREFIX`],
      [(14) — other characters after the closing tag], [`E-TEXTLIT-TAIL`],
      [(15) — something that is neither a processor nor a comment after the tag on the opening line], [`E-TEXTLIT-OPENER`],
      [(15) — no tag after `text`], [`E-TEXTLIT-TERM`],
      )
    ]
    #part("Examples")
    #ex("character literals", "module ex_char .

fn letter output u32 . do return widen u32 'a' . . end .
fn tab    output u32 . do return widen u32 '\\t' . . end .
fn quote  output u32 . do return widen u32 '\\x27' . . end .
fn hangul output u32 . do return widen u32 U'한' . . end .
fn emoji  output u32 . do return widen u32 U'😀' . . end .",
      out: "letter() = 97 · tab() = 9 · quote() = 39 · hangul() = 54620 · emoji() = 128512")
    #ex("a code point is written, and the prefix decides the encoding", "module ex_escape .

fn bytes  output usize . do return len  \"\\U0001F600\" . . end .
fn units  output usize . do return len u\"\\U0001F600\" . . end .
fn points output usize . do return len U\"\\U0001F600\" . . end .

rem Control characters with names.
fn bell   output u32 . do return widen u32 '\\a' . . end .
fn vtab   output u32 . do return widen u32 '\\v' . . end .
fn quote  output u32 . do return widen u32 '\\'' . . end .",
      out: "bytes() = 4 · units() = 2 · points() = 1 · bell() = 7 · vtab() = 11 · quote() = 39")
    #rejected("what is not a code point cannot be written", "module ex_surro .

fn f output u32 .
do
  return widen u32 '\\uD800' . .
end .", "E-STR-ESCAPE")
    #rejected("there are no octal escapes", "module ex_octal .

fn f output u32 .
do
  return widen u32 '\\101' . .
end .", "E-STR-ESCAPE")
    #rejected("an escape outside the set is refused", "module ex_esc .

fn f output usize .
do
  return len \"a\\qb\" . .
end .", "E-STR-ESCAPE")
    #rejected("there are only two prefixes — there is no `u8`", "module ex_pfx .

fn f output usize .
do
  return len u8\"ab\" . .
end .", "E-STR-PREFIX")
    #ex("a raw string — backslashes stay as they are", "module ex_raw_string .

rem `\\\\` in a raw string is two bytes — in an ordinary string it is one backslash
fn raw_len output usize .
do
  return len r\"\\\\s192kf%d\" . .
end .

fn escaped_len output usize .
do
  return len \"\\\\s192kf%d\" . .
end .

rem `r\"\\n\"` is not a line feed but two bytes, a backslash and `n`
fn second output u8 .
do
  return idx r\"\\n\" 1 . .
end .

rem A backslash directly before the closing double quote is just a byte too
fn ends_in_backslash output u8 .
do
  return idx r\"a\\\" 1 . .
end .",
      out: "raw_len() = 10 · escaped_len() = 9 · second() = 110 · ends_in_backslash() = 92")
    #rejected("the r of a raw string does not attach to a single quote", "module ex_raw_char .

fn f output u8 .
do
  return r'x' .
end .", "E-STR-PREFIX")
    #ex("a text literal — several lines as they are", "module ex_textlit .

fn doc output usize .
do
  return len text DOC
line one
line two
DOC
  . .
end .

rem Escapes are not processed in the body — `a\\nb` is four bytes.
fn raw output usize .
do
  return len text RAW
a\\nb
RAW
  . .
end .",
      out: "doc() = 17 · raw() = 4")
    #ex("literals", "let dec u32 42 .
let hex u32 0x2A .
let big u32 1_000_000 .
let flag bool true .")
    #ex("floating-point literals", "module ex_float .

fn plain output f64 . do return 1.5 . end .
fn expo  output f64 . do return 1e3 . end .
fn small output f64 . do return 1.5e-3 . end .
fn under output f64 . do return 1_000.5 . end .
fn hexp  output f64 . do return 0x1p3 . end .
fn negat output f64 . do return -1.5 . end .",
      out: "plain() = 1.5 · expo() = 1000.0 · small() = 0.0015 · under() = 1000.5 · hexp() = 8.0 · negat() = -1.5")
    #rejected("an integer literal does not become a floating-point number", "module ex_intfloat .

fn f output f64 .
do
  let x f64 3 .     rem 3 is an integer literal — it shall be written 3.0
  return x. .
end .", "E-TYPE-LET")
    #rejected("a literal outside the range of its type", "module ex_lit .

proc p output u8 . effects none .
do
  let x u8 300 .     rem 300 is outside the range of u8 (0 to 255)
  return 0 .
end .", "E-TYPE-WIDTH")
    #part("Notes")
    #plain[
      The single quote itself is written `'\''` (`'\x27'` is the same value).
      And `U'한'` is 54620 because `U` means a #strong[code point];. Written without a prefix as
      `'한'`, that character is three bytes and does not fit in one slot, so it is refused.
    ]
    #plain[
      Note that `u"😀"` is 2 — this character is written as #strong[two units]; in UTF-16
      (a surrogate pair). The same character is 1 with `U`. That is why the phrase #emph["number of characters"]; is not used
      in this language. It always says first #strong[in which elements it counts];.
    ]
    #plain[
      The same `\U0001F600` gives three different numbers — and #strong[none of them is wrong.];
      The escape says only #emph["which character"];; #emph["in how many pieces it is carried"]; is said by the prefix.
      Because the two are kept apart, the escape need not know the encoding.
    ]
    #plain[
      D800–DFFF are #strong[positions]; UTF-16 uses when it splits a large character into two pieces, not characters.
      If they were accepted, that literal would point at no character at all.
    ]
    #plain[
      In C, `\101` is octal 65, that is `A`. This language has no octal notation at all
      (`0755` is 755), so it does not revive octal for escapes alone — that place would then be
      the #strong[only exception]; in the language.
    ]
    #plain[
      `doc()` is 17: the two lines plus the one line feed between them — no line feed is attached after
      the last line. `raw()` being 4 is what #emph["exactly as written"]; means: four bytes `a` · backslash ·
      `n` · `b`, not one line feed.
    ]
    #note[
      The tag position after `text` is open to someday take the name of a #strong[processor that makes the value];
      (for example another encoding or a hexadecimal byte string). The processor of this edition has #strong[only the two]; prefixes above,
      and other names are refused. If they were passed silently, `hex"41"` would become a nameless
      something.
    ]
    #caution("Without a digit before the point it is not a floating-point number")[
      C · Python · JavaScript read `.5` as 0.5. Lowent does not —
      #strong[a leading dot is the mark that closes a form.]; If the closer and a literal compete for the same character,
      #emph["what is this dot"]; can be known only by looking around it, and that is semantic entropy
      (#cref("1.3")).
      So always write `0.5`. Likewise `1.` is not a floating-point number either; write
      `1.0`.
    ]
    #caution("An integer literal is not truncated silently")[
      `let x u8 300 .` is not translated, because 300 is outside the range of `u8` (0 to 255).
      Some languages accept it by truncating it to 44, and then the 300 written in the source and the actual value 44
      differ — one reads the source and still does not know the value.
    ]
    #note[
      There are no octal escapes, because this language has no octal notation at all ((18)).
    ]
    #note[
      The diagnostics for unclosed literals (a line or the source ending without a quote or a closing line) are gathered in #cref("6.1.7").
    ]
  ]
  #sub("6.1.5", "Dots and forms")[
    #para("1")[
      In the grammar of Lowent the full stop `.` is a #strong[closing mark];. One dot closes the innermost open
      #t("폼", "form").
    ]
    #para("2")[
      A form is prefix notation — the name comes first and the arguments follow. #strong[A name in a use position opens a form,
      and a dot closes it.]; `add a. b. .` adds `a` and `b`: `a.` and `b.` are forms closed at once
      without arguments, that is, the values of those names, and the last dot closes `add`.
    ]
    #para("2a")[
      What is known to be a value from its characters alone — number · character · string literals and `true` · `false` · `none` —
      does not open a form and takes no dot.
    ]
    #para("2b")[
      A name in the following positions does not open a form and takes no dot: positions that #strong[make]; a name (the name of a declaration ·
      the name of `let` · `var` · the name of an input · a loop variable · the name of a field), the position of a #strong[type];, and the #strong[word positions];
      fixed by core ops and statements (`cast <type> …` · `field … <member>` · `send … <handler>` · the words of
      pipe stages and the like). These positions are specified by the grammar of annex A.10 and the tables of annex D. The arguments of an op the user declares
      are #strong[all forms]; — even when a type is passed (`max_of u64. a. b. .`).
    ]
    #para("3")[
      When a form comes inside a form, the inner form first closes with its own dot. `add a. mul b. c. . .` multiplies `b` and `c`
      and then adds `a`. So the start and end of a form are determined #strong[by the characters alone]; — the tree stands without knowing
      how many arguments an op takes. The number of arguments is compared with the declaration after the tree stands (`E-IR-ARITY`).
    ]
    #para("3a")[
      Parentheses `( … )` enclose one form. They do not change the meaning — they are written to group for readability or to make something
      stand out. A form inside parentheses closes with its own dot (`add a. (mul b. c. .) .`). Parentheses are not closers.
    ]
    #para("4")[
      The number of dots is a #strong[checksum]; — if the number of open forms and the number of dots do not match, the processor issues a
      diagnostic. So a program with a missing dot is never silently read with a different meaning.
    ]
    #ex("prefix notation and dots", "let total u32 add 1 2 . .
let mixed u32 add 1 mul 2 3 . . .")
    #para("5")[
      A dot attached #strong[after]; a name (`a.`) is a closing dot — the same text as one separated by a space (`a .`). A dot attached #strong[between];
      names does not close but means #t("한정", "qualification") — and qualification has #strong[only three]; meanings:
      the name of a module (`allocs.byte_allocator`), the name of a variant (`err.too_short`), and
      the name of a declaration belonging to a type (`fn pt.twice`). All three are #strong[routes that point at a declared
      name];, not operations on values. A dot attached #strong[before]; a name (`.name`) is text that does not exist (`E-VOCAB-REMOVED`).
    ]
    #para("6")[
      Looking inside a value — field access and method calls — #strong[cannot]; be written with an attached dot.
      That is a form, and like every other form #strong[the name comes first];:
      `field` and `method` (#cref("6.2.10") and #cref("6.11.3")). This is to give one meaning one spelling
      — if the same character meant four things, what is being read would be decided only by knowing where the name
      came from.
    ]
    #plain[
      It looks different from the familiar notation (`1 + 2`), but the rule is one — #strong[name first, arguments after.];
      If infix notation is needed, it can be used inside an `expr` island, and its rules are in #cref("6.3.4").
    ]
  ]
  #sub("6.1.6", "Newline is not a closer")[
    #para("1")[
      A #t("개행", "newline") does not close a form. It is treated exactly like a space.
    ]
    #para("2")[
      A form ends at its own closer. The closer is one dot `.`. Parentheses are not closers (#cref("6.1.5") (3a)).
    ]
    #para("2a")[
      `do … end` are #strong[brackets that pair with each other];. `end` closes only its own `do` and does not close forms outside the block.
      So each statement inside a block shall be closed by its own dot, and if `end` is met while one is still unclosed,
      translation is refused (`E-DOT-MISSING`).
    ]
    #para("2b")[
      #strong[What has a block is closed by its own dot too.]; Statements whose body is a block (`if` · `while` · the four loops ·
      `match` · `case` · `region` · `borrow` · `task_group` · `pipe`) and declarations (`fn` · `proc` · `struct` ·
      `enum` · `trait` · `actor` · `state` · `contract` · `test`) end at the dot after `end` —
      `if c. do … end .` · `fn f … do … end .`. A chain continued with `else` is one statement, and there is one dot at the very end
      of the chain (`if a. do … end else do … end .`). If that dot is missing, translation is refused (`E-DOT-MISSING`).
    ]
    #para("2c")[
      A #strong[value]; that holds a block — `lit t do … end .` · `pipe xs. do … end .` — is closed by its own dot too, and the statement that uses
      that value is again closed by its own dot (`let x lit t do … end . .`).
    ]
    #para("2d")[
      A block cannot stand alone without the head that opens it (`E-BLOCK-NOHEAD`).
    ]
    #para("2e")[
      #strong[There is no dot that closes nothing.]; A doubled dot after a form already closed (`if c. . do`), a dot directly after the name in an op head
      (`fn f . input …`), and a dot in the middle of a head clause (`input a . u64 .`) do not conform (`E-CLOSER-EXTRA`).
      A type ends by its own grammar and so takes no dot (the dot in `input b slice u8 .` closes the clause).
      A head clause is closed by one dot, and the word after the dot is the word of the next clause or `do`. An `asm` clause separates its items (`reg a .`)
      with dots. Clause words (`vector` · `output` · `effects` …) cannot be the names of inputs (`E-NAME-CLAUSE`).
    ]
    #rejected("a dot that closes nothing", "module ex_extra_dot .

fn head input data slice u8 . . output u8 .
  requires ge len data. . 1 . .
do
  return idx data. 0 . .
end .", "E-CLOSER-EXTRA")
    #diagram("end closes only the block, and the full stop closes what has the block", "if eq a. 0 . do return 1 . end .      ← the last dot closes the if statement
                 └──────────┘ │       ← └┘ is the block of if, │ is the full stop of if

let p lit pt do x 1 . end . .         ← the first dot closes lit, the second closes let
      └─────────────────┘ │ │")
    #rejected("a statement ending in a block is closed by its own dot too", "module ex_dot_after_end .

fn f input a u64 . output u64 . do
  if eq a. 0 . do return 1 . end
  return a. .
end .", "E-DOT-MISSING")
    #rejected("a statement that uses a block as a value is closed by its own dot", "module ex_dot_missing .

struct pt do
  x u64 .
end .

fn f output u64 . do
  let p lit pt do x 1 . end
  return field p. x . .
end .", "E-DOT-MISSING")
    #rejected("a block without a head", "module ex_block_nohead .

fn f input a u64 . output u64 . do
  do
    return 1 .
  end .
  return a. .
end .", "E-BLOCK-NOHEAD")
    #plain[
      Why does `end` close only its own `do`? If `end` closed outer statements too, then to know what one `end` finished one would have to
      trace back #strong[who held]; that block. So there are only two rules:
      #strong[`do … end` pair like brackets, and what a name opened a dot closes.]; Statements whose body is a block and
      statements that use a block as a value end by the same rule (2026-10-09).
    ]
    #para("3")[
      So the meaning is the same wherever a line is broken. There is no mark for continuing
      a line — no line-end continuation mark, no comma at the end of a line, no indentation rule.
    ]
    #para("4")[
      A semicolon `;` is #strong[not]; a closer either. The closer has #strong[one]; spelling.
    ]
    #ex("a long form is one form even when its lines are broken", "module ex_newline .

rem A newline is white space. A form ends at its own closer `.`.
fn poly input a u64 . input b u64 . output u64 . do
  return add mul a. 2 .
             mul b. 3 . . .
end .",
      out: "poly(5, 4) = 22")
    #plain[
      Wherever a line is broken, the meaning is the same. There is no separate #emph[way]; of continuing to learn —
      no line-end `\\` as in C, no comma at the end of a line, no indentation rule.
    ]
    #note[
      #emph[There was a time when a newline was a closer.]; Before 2026-08-27 a newline closed forms. So there were three ways of continuing a line
      to learn, and breaking a line in the wrong place silently changed the meaning. That rule was removed. The closer is
      only `.`, and a newline is white space.
    ]
    #rejected("a semicolon is not a closer", "module ex_semi .

fn twice input a u64 . output u64 . do return mul a. 2 ; end .", "E-VOCAB-REMOVED")
    #plain[
      The semicolon was once the #strong[third spelling]; of the closer — when the processor met it, it produced
      a dot, literally. If three names point at the same thing, a reader has to know all three,
      so pure synonyms were removed (#cref("1.3")).
    ]
    #plain[
      Even with these rules, code as actually written #strong[tends to write the dots every time];. Writing them always is easier to read
      than weighing, line by line, whether a newline closes. The newline closer was
      less a permission #emph["you need not write the dot"]; than the grammar's acknowledgement of
      #strong[the fact that a line is a statement];.
    ]
  ]
  #sub("6.1.7", "What is not closed")[
    #part("Constraints")
    #para("1")[
      What is opened shall be closed. String literals and character literals shall meet their closing quote #strong[on the same line];, text literals and
      `note` blocks shall meet their closing line (#cref("6.1.4") (4) · #cref("6.1.1") (1)), blocks shall meet `end`, and groups `)`.
    ]
    #part("Diagnostics")
    #tbl("What is not closed, and its diagnostic")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Diagnostic*], [*What is not closed*],
      [`E-STR-UNTERM`], [a string was not closed up to the end of the file],
      [`E-STR-NEWLINE`], [a #strong[line feed]; came inside a string — a string is one line (several lines are a text literal)],
      [`E-CHAR-UNTERM` · `E-CHAR-NEWLINE`], [a character literal was not closed],
      [`E-NOTE-UNTERM`], [a note block was not closed],
      [`E-NOTE-TERM`], [no tag after `note`],
      [`E-TEXTLIT-UNTERM` · `E-TEXTLIT-TERM`], [the closing tag of a text literal is missing or does not match],
      [`E-TEXTLIT-TAIL` · `E-NOTE-TAIL`], [other characters came after the tag that closes a text literal or note block — only the tag stands on the closing line],
      [`E-TEXTLIT-OPENER`], [on the line that opens a text literal, something that is not a processor came after the tag],
      [`E-BLOCK-UNCLOSED`], [a block was not closed],
      [`E-GROUP-UNCLOSED`], [a group was not closed],
      )
    ]
    #part("Notes")
    #note[
      These diagnostics usually occur #strong[at the end of the source];. An opening mark cannot tell what is wrong until it is closed.
    ]
  ]
  #sub("6.1.8", "What a source shall have")[
    #part("Constraints")
    #para("1")[
      A source shall be #strong[complete UTF-8];.
    ]
    #para("2")[
      Every character of a source shall belong to some `token` or `separator` (#cref("6.1")). A character that belongs to none cannot appear.
    ]
    #para("3")[
      An integer literal shall be of a size the processor can hold at translation.
    ]
    #para("4")[
      The depth of nesting forms and blocks shall be within the limit the processor can follow. The processor does not silently truncate a program
      beyond the limit and translate it as a different program (#cref("4.5")).
    ]
    #part("Diagnostics")
    #tbl("Diagnostics of the source")[
      #table(columns: (1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Violation*], [*Diagnostic*],
      [(1) — there are bytes that are not UTF-8], [`E-LEX-UTF8`],
      [(2) — there is a character that belongs to no token], [`E-CHAR`],
      [(3) — an integer literal is too large], [`E-LIT-RANGE`],
      [(4) — nested too deeply], [`E-NEST-DEPTH`],
      )
    ]
    #part("Examples")
    #rejected("a character that belongs to no word", "module ex_stray_char .

fn f output u8 . do return 1 @ . end .", "E-CHAR")
    #part("Notes")
    #caution("")[
      `..` does not exist in this language (`E-DOT-DOUBLE`). It is a spelling that once existed, and #strong[a discarded spelling is not revived
      with a new meaning]; — people reading old text read it with the old meaning.
    ]
    #note[
      The constraints on the shape of number literals (the place of underscores · digits after a radix mark · letters attached after a number) are in #cref("6.1.4") (5) to (7).
      Writing only `0x` is refused, not 0 — 0 is written `0x0`. There are no number suffixes (`3u8`) and no octal notation (`0o7`).
    ]
  ]
]
