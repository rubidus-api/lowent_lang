# 6 언어 (Language)

(1) 이 조항은 로우엔트의 문법과 의미를 정한다. 먼저 소스가 어떤 조각으로 나뉘는지를 정하고
      (⟦§6.1⟧), 그 조각들이 무엇을 뜻하는지를 차례로 정한다.

## 6.1 어휘 (Lexical elements)

### 구문

```구문: source
source    ::= { separator | token }
separator ::= whitespace | line-comment | block-comment
token     ::= keyword | name | path | literal | punctuation

punctuation ::= "." | "(" | ")" | "," | operator
operator    ::= "+" | "-" | "*" | "/"
```

### 정적 의미

(1) 처리기는 소스 파일을 앞에서부터 읽어 `separator` 와 ⟦토큰|token⟧ 의 나열로 가른다. 토큰은 다섯 갈래다 —
      낱말(`keyword`), 이름(`name`), 경로(`path`, ⟦§6.1.3⟧), 리터럴(`literal`, ⟦§6.1.4⟧), 구두점(`punctuation`).

(2) `separator` 는 토큰을 가르고 **버려진다.** 그 밖의 뜻은 없다.

(2a) 구두점 가운데 떨어진 점 `.` 은 폼을 닫고 `(` `)` 는 폼을 묶는다(⟦§6.1.5⟧ · ⟦§6.1.6⟧). `operator` 는 `expr` 섬
      안에서만 연산자다(⟦§6.3.2⟧).

(3) 영문자 · 숫자 · 밑줄이 이어진 것은 **끊지 아니하고 한 덩이로** 읽은 뒤에 그 갈래를 정한다. `iffy` 는 낱말 `if` 로
      시작하는 것이 아니라 이름 하나이고, `remark` 는 주석이 아니라 이름이다.

## 6.1.1 공백과 주석

### 구문

```구문: separator
whitespace      ::= " " | tab | carriage-return | newline
blank           ::= " " | tab | carriage-return
tab             ::= ? U+0009 ?
carriage-return ::= ? U+000D ?
newline         ::= ? U+000A ?
line-comment    ::= "rem" { line-char }
block-comment   ::= "note" blank { blank } tag { line-char } newline
                    { comment-line newline }
                    tag { blank } ( newline | end-of-source )
tag             ::= name
line-char       ::= ? newline 이 아닌 글자 ?
comment-line    ::= ? 닫는 줄이 아닌 줄 ?
end-of-source   ::= ? 소스의 끝 ?
```

(1) `block-comment` 의 **닫는 줄**은 줄의 맨 첫 칸에서 여는 줄의 `tag` 와 같은 글자열로 시작하고, 그 바로 뒤가
      `name-char`(⟦§6.1.3⟧) 가 아닌 줄이다. 그 밖의 줄은 `comment-line` 이다 — 들여 쓴 `tag` 와 `tag` 보다 긴 낱말로
      시작하는 줄이 그렇다.

### 제약

(2) `rem` 과 `note` 는 그 바로 뒤가 `name-char` 가 아닐 때에만 주석을 연다 — `remark` 와 `notes` 는 이름이다(⟦§6.1⟧ (3)).

(3) `note` 다음에는 `tag` 가 있어야 한다.

(4) `block-comment` 를 닫는 `tag` 뒤에는 `blank` 와 `newline` 만 올 수 있다.

(5) 열린 `block-comment` 는 소스가 끝나기 전에 닫는 줄을 만나야 한다.

### 정적 의미

(6) 공백(스페이스 · 탭 · 캐리지 리턴 · 줄바꿈)은 토큰을 가른다. 토큰을 가르는 것 말고 다른 뜻은 없다.

(7) `rem` 으로 시작하는 자리부터 그 줄의 끝까지는 ⟦주석|comment⟧ 이며, 처리기는 그것을 없는 것으로 다룬다.
      `block-comment` 도 주석이다 — 여는 줄부터 닫는 줄까지를 없는 것으로 다룬다.

(8) 주석 안에는 유니코드 문자를 쓸 수 있다.

(9) `rem` 과 `note` 는 이름이 아니다. 이 둘로 시작하는 자리는 언제나 주석이다.

### 진단

> [!표] 주석의 진단
> #table(columns: (1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*어긴 것*], [*진단*],
> [(3) — `note` 뒤에 태그가 없다], [`E-NOTE-TERM`],
> [(4) — 닫는 태그 뒤에 다른 글자가 있다], [`E-NOTE-TAIL`],
> [(5) — 닫는 줄 없이 소스가 끝났다], [`E-NOTE-UNTERM`],
> )

### 예제

```lowent 예제: 주석
rem 이 줄은 전부 주석이다. 한글도 쓸 수 있다.
let n be u32 42 .   rem 여기서부터 줄 끝까지도 주석이다.
```

```lowent 예제: 여러 줄 주석 · 결과: f() = 1
module ex_note .

note DOC
여러 줄에 걸친 설명이다.
처리기는 이것을 없는 것으로 다룬다.
DOC

fn f output u8 . do return 1 . end
```

### 참고

> [!참고]
> 주석을 여는 낱말이 `rem` 인 것은 그것이 **낱말**이기 때문이다. 이 언어에는 기호로
> 된 주석 표시(`//`·`/* */`)가 없다 — 어휘를 낱말 하나로 통일하면 읽는 규칙이 하나로
> 줄어든다.

> [!참고]
> `block-comment` 는 텍스트 리터럴(⟦§6.1.4⟧)과 같은 모양이되 **값이 아니라 주석**이다. 닫는 줄의 규칙이 같다.
> 한 줄짜리 주석은 `rem` 으로 적는다.

## 6.1.2 낱말 (Keywords)

### 구문

```구문: keyword
keyword ::= "actor" | "be" | "break" | "case" | "continue" | "contract" | "def"
          | "do" | "drop" | "else" | "end" | "enum" | "expect" | "export"
          | "expr" | "extern" | "false" | "fn" | "for" | "guard" | "if"
          | "let" | "lit" | "match" | "module" | "newtype" | "none" | "proc"
          | "return" | "satisfies" | "send" | "set" | "spawn" | "state" | "struct"
          | "test" | "trait" | "true" | "try" | "type" | "unsafe" | "use"
          | "var" | "while"
```

### 제약

(1) ⟦낱말|keyword⟧ 은 언어가 뜻을 정해 둔 이름이며, 다른 뜻으로 쓸 수 없다. 낱말과 같은 철자는 이름이 될 수 없다.

(2) 낱말의 목록은 고정되어 있다. 위의 생성 규칙이 그 전부이며 부록 A 의 목록과 같다.

(2a) 예약만 해 두고 쓰지 아니하는 낱말은 없다. 낱말마다 그것을 쓰는 구문이 이 문서에 있다.

(3) 타입을 짓는 낱말은 `def` 하나다. 타입 선언은 `def struct` · `def enum` · `def type` · `def newtype` 으로 시작한다.

(4) `def` 뒤에는 그 넷만 온다. `fn` · `proc` · `actor` · `trait` · `test` · `module` 은 `def` 없이 적는다.

(5) `export` 는 `def` 앞에 온다: `export def struct rect do … end`.

### 진단

> [!표] 낱말의 진단
> #table(columns: (1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*어긴 것*], [*진단*],
> [(1) — 낱말을 이름으로 지었다], [`E-NAME-KEYWORD`],
> [(3) — `def` 없이 `struct` · `enum` · `type` · `newtype` 으로 시작했다], [`E-VOCAB-REMOVED`],
> [(4) — `def` 뒤에 그 넷이 아닌 것이 왔다], [`E-DEF-HEAD`],
> )

### 예제

```lowent-거부: 타입 선언은 `def` 로 시작한다 · E-VOCAB-REMOVED
module ex_nodef .

struct pt do
  x u32 .
end
```

```lowent-거부: `def` 는 타입만 짓는다 · E-DEF-HEAD
module ex_defop .

def fn f output u8 .
do
  return 1 .
end
```

### 참고

> [!참고]
> 낱말은 **예산**이다. 새 낱말을 더하는 것은 언어가 커지는 일이므로, 기존 낱말로 표현할 수 있는 것에는 새 낱말을 주지 아니한다.

> [!산문]
> 「예산」이라는 말이 낯설 수 있다. 뜻은 이렇다 — 낱말 수를 세어서 관리하고, 하나
> 늘릴 때마다 그만한 값어치가 있는지 따진다는 것이다. 이 언어의 낱말 수는 사람이 한
> 화면에 담아 외울 수 있는 크기로 유지된다.

## 6.1.3 이름 (Identifiers)

### 구문

```구문: name
name       ::= name-start { name-char }
name-start ::= "a" … "z" | "A" … "Z" | "_"
name-char  ::= name-start | "0" … "9"
path       ::= name "." path-part { "." path-part }
path-part  ::= name-char { name-char }
```

(1) `path` 의 점은 앞뒤 글자에 **붙어** 있다. 빈칸으로 떨어진 점은 `path` 의 일부가 아니라 구두점이다(⟦§6.1.5⟧).

### 제약

(2) `name` 은 낱말(⟦§6.1.2⟧)과 같은 철자일 수 없다.

(3) 이름에 유니코드 문자를 쓸 수 없다(⟦§5.2⟧). 이름을 이루는 글자는 위의 생성 규칙이 정한 ASCII 글자뿐이다.

(4) **짓는 이름에는 점이 없다.** 점이 붙은 이름을 선언할 수 없다.

(5) 내장 op 의 이름(⟦§6.3.3⟧)과 같은 철자를 매개변수나 지역 이름으로 쓸 수 없다.

### 정적 의미

(6) ⟦이름|identifier⟧ 은 저자가 짓는 이름이다. 모듈 · op · 타입 · 변수가 이름을 갖는다.

(7) 점은 **가리킬 때만** 나타난다. `path` 는 **선언된 이름을 가리키는 경로**이며 그 뜻은 ⟦§6.1.5⟧ (5) 가 정한 셋뿐이다 —
      모듈의 이름, 변형의 이름, 타입에 딸린 선언의 이름. 점은 이름의 일부가 아니라 **이름들을 잇는 표시**다.

(8) 값 안의 필드를 고르는 것은 `path` 로 적지 아니한다. 그것은 `field` 폼이다(⟦§6.2.10⟧).

### 진단

> [!표] 이름의 진단
> #table(columns: (1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*어긴 것*], [*진단*],
> [(2) — 낱말을 이름으로 지었다], [`E-NAME-KEYWORD`],
> [(3) — 이름에 ASCII 가 아닌 글자가 있다], [`E-NAME-ASCII`],
> [(4) — 점이 붙은 이름을 선언했다], [`E-NAME-DOTTED`],
> [(5) — 내장 op 의 이름을 지역 이름으로 지었다], [`E-NAME-BUILTIN`],
> [(8) — 값의 필드를 붙임 점으로 적었다(`p.x`)], [`E-FIELD-GLUED`],
> )

### 예제

```lowent-거부: 점이 붙은 이름은 지을 수 없다 · E-NAME-DOTTED
module ex_dotted .

fn f output u8 .
do
  let a.b u8 be 1 .
  return 1 .
end
```

### 참고

> [!산문]
> 여러 겹의 이름 공간은 이 언어에 **없다.** 이름 안에 계층을 담고 싶으면 밑줄로 적는다
> (`http_header_parse`). 점을 이름에 허용하면 `a.b` 가 *"a 의 필드 b"* 인지 *"a.b 라는
> 이름"* 인지 읽는 사람이 알 수 없게 된다 — 그것이 곧 의미 엔트로피다(⟦§1.3⟧).

> [!산문]
> 낱말을 이름으로 쓰면 **선언한 그 자리에서** 거부된다 — 쓰기를 기다리지 않는다.
> 진단은 어느 이름이 문제인지 짚는다.

> [!주의] 이름이 같으면 헷갈리는 것이 아니라 틀린다
> 어떤 언어는 안쪽 이름이 바깥 이름을 **가리도록**(shadowing) 허용한다. 가려진 이름은
> 소스를 읽은 사람이 **다른 값을 떠올리게** 만들고, 그것이 곧 의미 엔트로피다
> (⟦§1.3⟧). 그래서 이 언어는 가림을 **전면 금지한다** — 내장 연산의 이름,
> 모듈에 있는 이름, 매개변수의 이름, 그리고 바깥 블록에 살아 있는 이름 중
> 어느 것도 가릴 수 없다(⟦§6.4.8⟧).

## 6.1.4 리터럴 (Literals)

### 구문

```구문: literal
literal        ::= number | char-literal | string-literal | text-literal

number         ::= [ sign ] ( float | hex | bin | dec )
sign           ::= "+" | "-"
digit          ::= "0" … "9"
hex-digit      ::= digit | "a" … "f" | "A" … "F"
bin-digit      ::= "0" | "1"
dec            ::= digit { [ "_" ] digit }
hex            ::= "0" ( "x" | "X" ) hex-digit { [ "_" ] hex-digit }
bin            ::= "0" ( "b" | "B" ) bin-digit { [ "_" ] bin-digit }
float          ::= dec "." dec [ dec-exponent ]
                 | dec dec-exponent
                 | hex [ "." hex-digit { [ "_" ] hex-digit } ] hex-exponent
dec-exponent   ::= ( "e" | "E" ) [ sign ] dec
hex-exponent   ::= ( "p" | "P" ) [ sign ] dec

char-literal   ::= [ prefix ] single-quote ( char-char | escape ) single-quote
string-literal ::= [ prefix ] double-quote { string-char | escape } double-quote
prefix         ::= "u" | "U"
escape         ::= backslash ( simple-escape
                             | "x" hex-digit hex-digit
                             | "u" hex-digit hex-digit hex-digit hex-digit
                             | "U" hex-digit hex-digit hex-digit hex-digit hex-digit hex-digit hex-digit hex-digit )
simple-escape  ::= backslash | double-quote | single-quote
                 | "a" | "b" | "f" | "n" | "r" | "t" | "v" | "0"
single-quote   ::= ? U+0027 ?
double-quote   ::= ? U+0022 ?
backslash      ::= ? U+005C ?
char-char      ::= ? 작은따옴표도 역슬래시도 줄바꿈도 아닌 글자 하나 ?
string-char    ::= ? 큰따옴표도 역슬래시도 줄바꿈도 아닌 바이트 하나 ?

text-literal   ::= "text" blank { blank } tag [ blank { blank } processor ] { blank } [ line-comment ] newline
                   { text-line newline }
                   tag { blank } ( newline | end-of-source )
processor      ::= "u" | "U"
text-line      ::= ? 닫는 줄이 아닌 줄 ?
```

(1) `sign` 은 숫자에 **붙어** 있어야 부호다. `-5` 는 `number` 하나이고 `- 5` 는 아니다. `expr` 섬(⟦§6.3.2⟧) **밖**에서
      숫자에 붙은 부호는 값의 일부이고, 섬 **안**에서 `-` 와 `+` 는 연산자다 — 그러므로 폼 자리의 `g 10 -3` 은
      인자 둘이다.

(2) 점도 지수도 없으면 `dec` 로 읽고, 점이나 지수가 있으면 `float` 로 읽는다. 점의 **앞뒤 모두**에 숫자가 있어야
      `float` 다 — `.5` 와 `1.` 은 `float` 가 아니며, 그 점은 폼을 닫는 표시로 읽힌다(⟦§6.1.6⟧).

(3) `prefix` 는 따옴표에 **붙어** 있어야 접두사다. 사이에 빈칸이 있으면 이름 하나와 리터럴 하나다.

(4) `text-literal` 의 **닫는 줄**은 줄의 맨 첫 칸에서 여는 줄의 `tag` 와 같은 글자열로 시작하고, 그 바로 뒤가
      `name-char`(⟦§6.1.3⟧) 가 아닌 줄이다. 그 밖의 줄은 `text-line` 이다 — 들여 쓴 `tag` 와, `tag` 로 시작하되 더 긴
      낱말(`tag` 가 `DOC` 일 때 `DOCS`)로 시작하는 줄이 그렇다.

### 제약

(5) 밑줄은 자릿수 **사이**에만 온다. 수의 맨 앞, 맨 뒤, 진법 표시 바로 뒤, 밑줄 바로 뒤에는 올 수 없다.

(6) 진법 표시(`0x` · `0b`)와 지수 표시 뒤에는 그 진법의 자릿수가 하나 이상 있어야 한다.

(7) `number` 바로 뒤에 영문자나 밑줄이 이어질 수 없다.

(8) 정수 리터럴의 값은 그것이 쓰이는 자리의 타입 범위 안에 있어야 한다. 부동소수 리터럴도 그 자리의 타입이
      나타낼 수 있는 범위 안에 있어야 한다.

(9) 부동소수 타입의 자리에는 `float` 를, 정수 타입의 자리에는 정수 리터럴을 적어야 한다. 둘은 서로 바뀌지
      아니한다 — `f64` 자리에는 `3` 이 아니라 `3.0` 을 적는다.

(10) `char-literal` 은 접두사가 정한 원소 **하나**에 들어가야 한다. 빈 것도, 원소 하나를 넘는 것도 적을 수 없다.

(11) `escape` 는 위의 생성 규칙이 정한 **열넷**뿐이다. 그 밖의 글자를 역슬래시 뒤에 적을 수 없다.

> [!표] 이스케이프 — 닫힌 집합 열넷
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*적는 것*], [*값*], [*뜻*],
> [`\\`], [0x5C], [역슬래시 자신],
> [`\"`], [0x22], [큰따옴표],
> [`\'`], [0x27], [작은따옴표],
> [`\a`], [0x07], [경보(bell)],
> [`\b`], [0x08], [한 칸 뒤로(backspace)],
> [`\f`], [0x0C], [쪽 넘김(form feed)],
> [`\n`], [0x0A], [줄 바꿈],
> [`\r`], [0x0D], [줄 처음으로],
> [`\t`], [0x09], [가로 탭],
> [`\v`], [0x0B], [세로 탭],
> [`\0`], [0x00], [영 바이트],
> [`\xNN`], [0x00–0xFF], [십육진 **두 자리**],
> [`\uXXXX`], [코드포인트], [십육진 **네 자리**],
> [`\UXXXXXXXX`], [코드포인트], [십육진 **여덟 자리**],
> )

(12) `\uXXXX` 와 `\UXXXXXXXX` 가 적는 값은 코드포인트여야 한다. 서러게이트 자리(D800 ~ DFFF)와 10FFFF 를 넘는
      값은 적을 수 없다.

(13) `prefix` 는 `u` 와 `U` 둘뿐이다. 그 밖의 낱말을 따옴표에 붙여 적을 수 없다.

(14) `text-literal` 을 닫는 `tag` 뒤에는 `blank` 와 `newline` 만 올 수 있다. 문장을 닫는 점은 **다음 줄**에 적는다.

(15) `text-literal` 을 여는 줄에서 `tag` 뒤에는 `processor` 와 주석만 올 수 있다. `text` 다음에는 `tag` 가 있어야 한다.

### 정적 의미

(16) ⟦리터럴|literal⟧ 은 소스에 직접 적은 값이다.

(17) 정수 리터럴은 십진(`dec`) · 십육진(`hex`) · 이진(`bin`)으로 적는다. 밑줄은 값에 들지 아니한다(`1_000_000`).

(18) **앞의 0 은 팔진이 아니다.** `0755` 는 755 다. 팔진 표기는 이 언어에 없다.

(19) 정수 리터럴은 **그 자체로는 타입이 없다.** 쓰이는 자리가 타입을 정한다.

(20) ⟦부동소수 리터럴|floating-point literal⟧ 은 `float` 로 적은 수다 — `1.5` · `1e3` · `1.5e-3`. 십육진 지수 표기의
      `p` 뒤는 2 의 거듭제곱이다(`0x1p3` 은 8.0).

(21) `inf` 나 `nan` 을 적는 리터럴은 없다. 그런 값은 계산으로 얻는다.

(22) 참 · 거짓은 낱말 `true` · `false` 로 적는다. 이 둘은 `bool` 타입이며 정수와 서로 바뀌지 아니한다(⟦§6.2.3⟧).

(23) ⟦문자 리터럴|character literal⟧ 의 값은 그 글자의 부호이며 정수다.

(24) ⟦문자열 리터럴|string literal⟧ 의 값은 원소의 줄이다. `len` 은 **원소의 수**를 낸다.

(25) 역슬래시로 시작하는 `escape` 를 ⟦이스케이프|escape⟧ 라 한다. `\xNN` 은 십육진 두 자리를, `\uXXXX` 는 네 자리를,
      `\UXXXXXXXX` 는 여덟 자리를 **정확히** 먹는다 — 자리 수가 값에 따라 달라지지 아니한다.

(26) `\uXXXX` 와 `\UXXXXXXXX` 는 **코드포인트를 적는다.** 그 코드포인트가 어떤 원소로 실리는가는 접두사가 정한다 —
      접두사가 없으면 UTF-8 바이트, `u` 면 UTF-16 코드 유닛, `U` 면 코드포인트 그대로다.

(27) ⟦접두사|prefix⟧ 는 리터럴의 **원소가 무엇인지**를 정한다. 접두사가 없으면 바이트, `u` 는 UTF-16 코드 유닛,
      `U` 는 코드포인트다. 문자 리터럴도 같다 — `u'…'` 는 코드 유닛 하나, `U'…'` 는 코드포인트 하나다.

> [!표] 접두사가 정하는 것 — `len` 은 언제나 원소의 수다
> #table(columns: (auto, auto, auto, auto, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*적은 것*], [*원소*], [*`len "한"`*], [*`len "ab"`*], [*`len "😀"`*],
> [`"…"` (없음)], [바이트], [3], [2], [4],
> [`u"…"`], [UTF-16 코드 유닛], [1], [2], [2],
> [`U"…"`], [코드포인트], [1], [2], [1],
> )

(28) ⟦텍스트 리터럴|text literal⟧ 은 여러 줄을 그대로 담는 문자열이다. 값은 `text-line` 들을 그 사이의 줄바꿈과 함께
      이은 것이다. **닫는 줄 바로 앞의 줄바꿈 하나는 값에 들지 아니한다** — 본문 끝에 줄바꿈을 남기려면 닫는 줄 앞에
      빈 줄을 하나 둔다.

(29) 텍스트 리터럴의 본문에서는 **이스케이프를 풀지 아니한다.** 적힌 바이트가 곧 값이다.

(30) `processor` 는 값의 원소를 정한다 — `text TAG u` 는 UTF-16 코드 유닛의 나열(`slice u16`), `text TAG U` 는
      코드포인트의 나열(`slice u32`)이다. `processor` 가 없으면 바이트의 나열(`slice u8`)이다.

### 진단

> [!표] 리터럴의 진단
> #table(columns: (1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*어긴 것*], [*진단*],
> [(5) — 밑줄이 자릿수 사이가 아닌 곳에 있다], [`E-NUM-SEP`],
> [(6) — 진법 표시나 지수 표시 뒤에 자릿수가 없다], [`E-NUM-EMPTY`],
> [(7) — 수 바로 뒤에 글자가 이어진다], [`E-NUM-SUFFIX`],
> [(8) — 리터럴이 그 자리의 타입 범위를 벗어난다], [`E-TYPE-WIDTH`],
> [(9) — 부동소수 자리에 정수 리터럴을 적었다], [`E-TYPE-LET`],
> [(10) — 문자 리터럴이 비었다], [`E-CHAR-EMPTY`],
> [(10) — 문자 리터럴이 원소 하나를 넘는다], [`E-CHAR-WIDTH`],
> [(11) · (12) — 이스케이프가 집합 밖이거나 코드포인트가 아니다], [`E-STR-ESCAPE`],
> [(13) — 접두사가 `u` · `U` 가 아니다], [`E-STR-PREFIX`],
> [(14) — 닫는 태그 뒤에 다른 글자가 있다], [`E-TEXTLIT-TAIL`],
> [(15) — 여는 줄의 태그 뒤에 처리기도 주석도 아닌 것이 있다], [`E-TEXTLIT-OPENER`],
> [(15) — `text` 뒤에 태그가 없다], [`E-TEXTLIT-TERM`],
> )

### 예제

```lowent 예제: 문자 리터럴 · 결과: letter() = 97 · tab() = 9 · quote() = 39 · hangul() = 54620 · emoji() = 128512
module ex_char .

fn letter output u32 . do return widen u32 'a' . end
fn tab    output u32 . do return widen u32 '\t' . end
fn quote  output u32 . do return widen u32 '\x27' . end
fn hangul output u32 . do return widen u32 U'한' . end
fn emoji  output u32 . do return widen u32 U'😀' . end
```

```lowent 예제: 코드포인트를 적고, 인코딩은 접두사가 정한다 · 결과: bytes() = 4 · units() = 2 · points() = 1 · bell() = 7 · vtab() = 11 · quote() = 39
module ex_escape .

fn bytes  output u64 . do return len  "\U0001F600" . end
fn units  output u64 . do return len u"\U0001F600" . end
fn points output u64 . do return len U"\U0001F600" . end

rem 이름이 있는 제어 문자.
fn bell   output u32 . do return widen u32 '\a' . end
fn vtab   output u32 . do return widen u32 '\v' . end
fn quote  output u32 . do return widen u32 '\'' . end
```

```lowent-거부: 코드포인트가 아닌 것은 적을 수 없다 · E-STR-ESCAPE
module ex_surro .

fn f output u32 .
do
  return widen u32 '\uD800' .
end
```

```lowent-거부: 팔진 이스케이프는 없다 · E-STR-ESCAPE
module ex_octal .

fn f output u32 .
do
  return widen u32 '\101' .
end
```

```lowent-거부: 이스케이프 집합 밖은 거부된다 · E-STR-ESCAPE
module ex_esc .

fn f output u64 .
do
  return len "a\qb" .
end
```

```lowent-거부: 접두사는 둘뿐이다 — `u8` 은 없다 · E-STR-PREFIX
module ex_pfx .

fn f output u64 .
do
  return len u8"ab" .
end
```

```lowent 예제: 텍스트 리터럴 — 여러 줄을 그대로 · 결과: doc() = 17 · raw() = 4
module ex_textlit .

fn doc output u64 .
do
  return len text DOC
line one
line two
DOC
  .
end

rem 본문에서는 이스케이프를 풀지 않는다 — `a\nb` 는 네 바이트다.
fn raw output u64 .
do
  return len text RAW
a\nb
RAW
  .
end
```

```lowent 예제: 리터럴
let dec be u32 42 .
let hex be u32 0x2A .
let big be u32 1_000_000 .
let flag be bool true .
```

```lowent 예제: 부동소수 리터럴 · 결과: plain() = 1.5 · expo() = 1000.0 · small() = 0.0015 · under() = 1000.5 · hexp() = 8.0 · negat() = -1.5
module ex_float .

fn plain output f64 . do return 1.5 . end
fn expo  output f64 . do return 1e3 . end
fn small output f64 . do return 1.5e-3 . end
fn under output f64 . do return 1_000.5 . end
fn hexp  output f64 . do return 0x1p3 . end
fn negat output f64 . do return -1.5 . end
```

```lowent-거부: 정수 리터럴은 부동소수가 되지 아니한다 · E-TYPE-LET
module ex_intfloat .

fn f output f64 .
do
  let x be f64 3 .     rem 3 은 정수 리터럴이다 — 3.0 이라고 적어야 한다
  return x .
end
```

```lowent-거부: 타입의 범위를 벗어난 리터럴 · E-TYPE-WIDTH
module ex_lit .

proc p output u8 . effects none .
do
  let x be u8 300 .     rem 300 은 u8 의 범위(0~255) 밖이다
  return 0 .
end
```

### 참고

> [!산문]
> 작은따옴표 자신은 `'\''` 로 적는다(`'\x27'` 도 같은 값이다).
> 그리고 `U'한'` 이 54620 인 것은 `U` 가 **코드포인트**를 뜻하기 때문이다. 접두사 없이
> `'한'` 이라 적으면 그 글자는 바이트 셋이라 한 칸에 안 들어가므로 거부된다.

> [!산문]
> `u"😀"` 이 2 인 것에 주의한다 — 이 글자는 UTF-16 에서 **두 유닛**으로 적히기 때문이다
> (서러게이트 쌍). 같은 글자가 `U` 에서는 1 이다. 그래서 *"글자 수"* 라는 말은 이 언어에서
> 쓰지 아니한다. 언제나 **어느 원소로 세는가**를 먼저 말한다.

> [!산문]
> 같은 `\U0001F600` 이 셋 다 다른 수를 낸다 — 그런데 **틀린 것이 하나도 없다.**
> 이스케이프는 *"어떤 글자인가"* 만 말하고, *"몇 조각으로 실리는가"* 는 접두사가 말한다.
> 둘을 갈라 두었기 때문에 이스케이프가 인코딩을 몰라도 된다.

> [!산문]
> D800~DFFF 는 UTF-16 이 큰 글자를 두 조각으로 나눌 때 쓰는 **자리**이지 글자가 아니다.
> 받아 주면 그 리터럴은 아무 글자도 가리키지 않게 된다.

> [!산문]
> 씨(C)에서 `\101` 은 팔진으로 65, 곧 `A` 다. 이 언어에는 팔진 표기가 아예 없으므로
> (`0755` 는 755 다) 이스케이프에만 팔진을 되살리지 아니한다 — 그러면 그 자리가
> 언어에서 **유일한 예외**가 된다.

> [!산문]
> `doc()` 이 17 인 것은 두 줄과 그 사이의 줄바꿈 하나를 더한 값이다 — 마지막 줄 뒤에는
> 줄바꿈이 붙지 아니한다. `raw()` 가 4 인 것이 *"원문 그대로"* 의 뜻이다: `a`·역슬래시·
> `n`·`b` 네 바이트이며, 줄바꿈 하나가 아니다.

> [!참고]
> `text` 다음의 태그 자리는 언젠가 **값을 만드는 처리기**의 이름을 받도록 열려 있다
> (예컨대 다른 인코딩이나 십육진 바이트열). 이 판의 처리기는 위의 접두사 **둘뿐**이며,
> 그 밖의 이름은 거부된다. 조용히 통과시키면 `hex"41"` 이 이름 없는 무언가가 되기
> 때문이다.

> [!주의] 점 앞에 숫자가 없으면 부동소수가 아니다
> 씨(C)·파이썬·자바스크립트는 `.5` 를 0.5 로 읽는다. 로우엔트는 그러지 아니한다 —
> **앞선 점은 폼을 닫는 표시**이기 때문이다. 닫개와 리터럴이 같은 글자를 다투면
> *"이 점이 무엇인가"* 를 앞뒤를 봐야 알게 되고, 그것이 곧 의미 엔트로피다
> (⟦§1.3⟧).
> 그러므로 반드시 `0.5` 라고 적는다. 마찬가지로 `1.` 도 부동소수가 아니며 `1.0` 이라
> 적는다.

> [!주의] 정수 리터럴이 조용히 잘리지 않는다
> `let x be u8 300 .` 은 번역되지 아니한다. 300 은 `u8` 의 범위(0~255) 밖이기 때문이다.
> 어떤 언어는 이것을 44 로 잘라서 받아들이는데, 그러면 소스에 적힌 300 과 실제 값 44 가
> 달라진다 — 소스를 읽고도 값을 모르게 되는 것이다.

> [!참고]
> 팔진 이스케이프는 없다. 이 언어에는 팔진 표기 자체가 없기 때문이다((18)).

> [!참고]
> 닫히지 않은 리터럴(따옴표나 닫는 줄 없이 줄이나 소스가 끝난 것)의 진단은 ⟦§6.1.7⟧ 이 모은다.

## 6.1.5 점과 form

(1) 로우엔트의 문법에서 마침표 `.` 는 **닫는 표시**다. 하나의 ⟦폼|form⟧ 이
      끝났음을 알린다.

(2) 폼은 전위 표기다 — 이름이 먼저 오고 인자가 뒤에 온다. `add a b` 는 `a` 와 `b` 를
      더한다.

(3) 폼 안에 폼이 올 때는 괄호로 감싼다. `add a (mul b c)` 는 `b` 와 `c` 를 곱한 뒤
      `a` 를 더한다.

(4) 점의 개수는 **검사합**이다 — 열린 폼의 수와 점의 수가 맞지 않으면 처리기가 진단을
      낸다. 그래서 괄호를 잘못 닫은 프로그램이 조용히 다른 뜻으로 읽히는 일이 없다.

```lowent 예제: 전위 표기와 점
let total be u32 add 1 2 .
let mixed be u32 add 1 (mul 2 3) .
```

(5) 점은 **떨어져 있을 때만** 닫는다. 이름에 **붙은** 점은 닫지 않고
      ⟦한정|qualification⟧ 을 뜻한다 — 그리고 한정의 뜻은 **셋뿐**이다:
      모듈의 이름(`allocs.byte_allocator`), 변형의 이름(`err.too_short`), 그리고
      타입에 딸린 선언의 이름(`fn pt.twice`). 이 셋은 모두 **선언된 이름을 가리키는
      경로**이지, 값에 대한 연산이 아니다.

(6) 값의 안을 들여다보는 것 — 필드 접근과 메서드 호출 — 은 붙임 점으로 적을 수
      **없다**. 그것은 폼이며, 다른 모든 폼과 같이 **이름이 먼저 온다**:
      `field` 와 `method` 다 (⟦§6.2.10⟧ 과 ⟦§6.11.3⟧). 한 뜻에 한 철자를
      주기 위해서다 — 같은 글자가 네 가지를 뜻하면, 무엇을 읽고 있는지는 이름이
      어디서 왔는지를 알아야만 정해진다.

> [!산문]
> 익숙한 표기(`1 + 2`)와 달라 보이지만 규칙은 하나다 — **이름이 먼저, 인자가 뒤.**
> 중위 표기가 필요하면 `expr` 섬 안에서 쓸 수 있고, 그 규칙은 ⟦§6.3.4⟧ 에 있다.

## 6.1.6 개행은 닫지 아니한다 (Newline is not a closer)

(1) ⟦개행|newline⟧ 은 폼을 닫지 아니한다. 사이띄개와 똑같이 다룬다.

(2) 폼은 자기 닫개에서 끝난다. 닫개는 점 `.` 하나다. 괄호 `( … )` 는 그 안에 열린 폼을 함께
      닫는다.

(2a) `do … end` 는 **서로 짝인 괄호**다. `end` 는 자기 `do` 만 닫고, 블록 밖의 폼은 닫지 아니한다.
      그러므로 블록 안의 문장은 저마다 자기 점으로 닫혀 있어야 하며, 닫히지 않은 채 `end` 를
      만나면 번역이 거부된다(`E-DOT-MISSING`).

(2b) 블록을 **몸으로 갖는** 구문 — 부록 A 의 표에서 닫개가 `end` 인 머리(`fn` · `proc` · `if` ·
      `while` · `for` · `match` · `case` · `struct` · `enum` · `region` · `borrow` · `pipe` · `else` …) — 은
      그 블록이 끝나면 끝난다. 뒤에 점을 적으면 닫을 것이 없어 거부된다(`E-DOT-STRAY`). 블록을
      여는 `do` 바로 뒤의 점도 같다.

(2c) 블록을 품은 **값**을 쓰는 문장 — `let x be lit t do … end .` · `return pipe xs do … end .` —
      은 블록을 몸으로 갖지 아니하므로, 여느 문장처럼 **자기 점**으로 닫는다. 괄호 안이면
      `)` 가 닫는다.

(2d) 블록은 그것을 여는 머리 없이 홀로 설 수 없다(`E-BLOCK-NOHEAD`).

(2e) **점은 하나다.** 아무것도 닫지 않는 점 — 이미 닫힌 폼 뒤에 겹친 점(`def type bytes slice u8 . .`), op 머리의 이름 바로
      뒤의 점(`fn f . input …`), 머리 절 가운데의 점(`input a . u64 .`) — 은 적합하지 아니하다(`E-CLOSER-EXTRA`).
      타입은 제 문법의 인자 수로 끝나므로 점을 갖지 아니한다(`input b slice u8 .` 의 점은 절을 닫는다).
      머리 절은 점 하나로 닫히고, 점 뒤의 낱말은 다음 절의 낱말이거나 `do` 다. `asm` 절은 제 항목(`reg a .`)을
      점으로 나눈다. 절 낱말(`vector` · `output` · `effects` …)은 입력의 이름이 될 수 없다(`E-NAME-CLAUSE`).

```lowent-거부: 아무것도 닫지 않는 점 · E-CLOSER-EXTRA
module ex_extra_dot .

fn head input data slice u8 . . output u8 .
  requires ge (len data) 1 .
do
  return idx data 0 .
end
```

```도해: end 는 블록만 닫고, 마침표는 문장을 닫는다
if eq a 0 . do return 1 . end    ← if 문: 블록을 몸으로 갖는다 --- end 에서 끝난다
└───────────────────────────┘ if 문

let p be lit pt do x 1 . end .        ← let 문: 블록을 값으로 쓴다 --- 자기 . 으로 끝난다
         └─────────────────┘ │        ← └┘ 는 lit 의 블록, │ 는 let 의 마침표
└────────────────────────────────┘ let 문
```

```lowent-거부: 몸으로 갖는 블록의 `end` 뒤에는 점이 없다 · E-DOT-STRAY
module ex_dot_after_end .

fn f input a u64 . output u64 . do
  if eq a 0 . do return 1 . end .
  return a .
end
```

```lowent-거부: 블록을 값으로 쓰는 문장은 자기 점으로 닫는다 · E-DOT-MISSING
module ex_dot_missing .

def struct pt do
  x u64 .
end

fn f output u64 . do
  let p be lit pt do x 1 . end
  return field p x .
end
```

```lowent-거부: 머리 없는 블록 · E-BLOCK-NOHEAD
module ex_block_nohead .

fn f input a u64 . output u64 . do
  do
    return 1 .
  end
  return a .
end
```

> [!산문]
> 왜 `end` 가 자기 `do` 만 닫는가. `end` 가 바깥 문장까지 닫으면, 한 `end` 가 무엇을 끝냈는지
> 알려면 그 블록을 **누가 품었는지**를 거슬러 올라가 봐야 한다 — `let … be lit T do … end` 에서
> 그 `end` 는 `lit` 의 블록과 `let` 문장을 함께 끝냈다. 이제 규칙은 둘뿐이다: **`do … end` 는
> 괄호처럼 짝을 이루고, 문장은 자기 점으로 끝난다.** 블록을 몸으로 갖는 구문만 C 의
> `if (…) { }` 처럼 블록에서 끝난다(2026-09-25).

(3) 그러므로 줄을 어디서 나누어도 뜻이 같다. 줄을 이어 쓰는 표시를 두지
      아니한다 — 행끝 이음표도, 줄 끝의 쉼표도, 들여쓰기 규칙도 없다.

(4) 세미콜론 `;` 도 닫개가 **아니다.** 닫개의 철자는 **하나**다.

```lowent 예제: 긴 폼은 줄을 나눠도 한 폼이다 · 결과: poly(5, 4) = 22
module ex_newline .

rem 개행은 공백이다. 폼은 자기 닫개 `.` 에서 끝난다.
fn poly input a u64 . input b u64 . output u64 . do
  return add (mul a 2)
             (mul b 3) .
end
```

> [!산문]
> 줄을 어디서 나누든 뜻이 같다. 이어 쓰는 *방법*을 따로 배울 것이 없다 —
> C 의 행끝 `\\` 도, 줄 끝의 쉼표도, 들여쓰기 규칙도 없다.

> [!참고]
> *개행이 닫개였던 때가 있었다.* 2026-08-27 이전에는 개행이 폼을 닫았다. 그래서 줄을 이어 쓰는 방법을 셋
> 배워야 했고, 잘못 나누면 뜻이 조용히 바뀌었다. 그 규칙을 없앴다. 닫개는
> 오직 `.` 이고, 개행은 공백이다.

```lowent-거부: 세미콜론은 닫개가 아니다 · E-VOCAB-REMOVED
module ex_semi .

fn twice input a u64 . output u64 . do return mul a 2 ; end
```

> [!산문]
> 세미콜론은 한때 닫개의 **세 번째 철자**였다 — 처리기가 그것을 만나면 글자 그대로
> 점을 냈다. 같은 것을 가리키는 이름이 셋이면 읽는 사람이 셋을 다 알아야 하므로,
> 순수한 동의어는 없앴다(⟦§1.3⟧).

> [!산문]
> 규칙이 이러해도, 실제로 쓰는 코드는 **점을 꼬박꼬박 적는 편**이다. 개행이 닫는지
> 아닌지를 줄마다 따지는 것보다 늘 적는 쪽이 읽기 쉽기 때문이다. 개행 닫개는
> *"점을 안 적어도 된다"* 는 허락이라기보다, **줄이 곧 문장이라는 사실**을 문법이
> 인정하는 것에 가깝다.

## 6.1.7 닫히지 않은 것

### 제약

(1) 연 것은 닫아야 한다. 문자열 리터럴과 문자 리터럴은 **같은 줄에서** 닫는 따옴표를 만나야 하고, 텍스트 리터럴과
      `note` 뭉치는 닫는 줄을 만나야 하며(⟦§6.1.4⟧ (4) · ⟦§6.1.1⟧ (1)), 블록은 `end` 를, 묶음은 `)` 를 만나야 한다.

### 진단

> [!표] 닫히지 않은 것과 그 진단
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*진단*], [*무엇이 안 닫혔는가*],
> [`E-STR-UNTERM`], [글월이 파일 끝까지 닫히지 아니하였다],
> [`E-STR-NEWLINE`], [글월 안에 **줄바꿈**이 들어왔다 — 글월은 한 줄이다(여러 줄은 텍스트 리터럴이다)],
> [`E-CHAR-UNTERM` · `E-CHAR-NEWLINE`], [낱글자가 닫히지 아니하였다],
> [`E-NOTE-UNTERM`], [적바림 뭉치가 닫히지 아니하였다],
> [`E-NOTE-TERM`], [`note` 뒤에 태그가 없다],
> [`E-TEXTLIT-UNTERM` · `E-TEXTLIT-TERM`], [텍스트 리터럴의 맺음말이 없거나 어긋났다],
> [`E-TEXTLIT-TAIL` · `E-NOTE-TAIL`], [텍스트 리터럴이나 적바림 뭉치를 닫는 태그 뒤에 다른 글자가 왔다 — 닫는 줄에는 태그만 선다],
> [`E-TEXTLIT-OPENER`], [텍스트 리터럴을 여는 줄에서 태그 뒤에 처리기가 아닌 것이 왔다],
> [`E-BLOCK-UNCLOSED`], [블록이 닫히지 아니하였다],
> [`E-GROUP-UNCLOSED`], [묶음이 닫히지 아니하였다],
> )

### 참고

> [!참고]
> 이 진단들은 대개 **소스의 끝에서** 난다. 여는 표시는 그것이 닫히기 전에는 무엇이 잘못인지 알 수 없기 때문이다.

## 6.1.8 소스가 갖추어야 하는 것

### 제약

(1) 소스는 **온전한 UTF-8** 이어야 한다.

(2) 소스의 글자는 모두 어느 `token` 이나 `separator` 에 속해야 한다(⟦§6.1⟧). 어디에도 속하지 않는 글자가 올 수 없다.

(3) 정수 리터럴은 처리기가 번역할 때 담을 수 있는 크기여야 한다.

(4) 폼과 블록을 겹쳐 쓰는 깊이는 처리기가 따라갈 수 있는 한도 안이어야 한다. 처리기는 한도를 넘은 프로그램을
      조용히 잘라 다른 프로그램으로 번역하지 아니한다(⟦§4.5⟧).

### 진단

> [!표] 소스의 진단
> #table(columns: (1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*어긴 것*], [*진단*],
> [(1) — UTF-8 이 아닌 바이트가 있다], [`E-LEX-UTF8`],
> [(2) — 어느 토큰에도 속하지 않는 글자가 있다], [`E-CHAR`],
> [(3) — 정수 리터럴이 너무 크다], [`E-LIT-RANGE`],
> [(4) — 너무 깊이 겹쳐 썼다], [`E-NEST-DEPTH`],
> )

### 예제

```lowent-거부: 어느 낱말에도 속하지 않는 글자 · E-CHAR
module ex_stray_char .

fn f output u8 . do return 1 @ . end
```

### 참고

> [!주의]
> `..` 는 이 언어에 없다(`E-DOT-DOUBLE`). 한때 있던 철자이며, **버린 철자를 새 뜻으로
> 되살리지 아니한다** — 옛 글을 읽는 사람이 옛 뜻으로 읽기 때문이다.

> [!참고]
> 수 리터럴의 모양에 대한 제약(밑줄의 자리 · 진법 표시 뒤의 자릿수 · 수 뒤에 붙은 글자)은 ⟦§6.1.4⟧ (5) ~ (7) 에 있다.
> `0x` 만 적으면 0 이 아니라 거부된다 — 0 은 `0x0` 이라 적는다. 수의 접미사(`3u8`)도 팔진 표기(`0o7`)도 없다.
