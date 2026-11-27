# 3 Terms and definitions

(1) In this document the following terms are used only in the meanings given here. They may differ from their everyday meanings or
      from their meanings in other languages; whenever a meaning is in doubt, this clause is the reference.

> [!산문]
> A first-time reader need not read this clause from start to end. When an unknown word turns up in the text,
> come back here. Each term carries the number of the clause that actually uses it.

## 3.1 The parts of a program

> [!용어] 프로그램 (program)
> The set of Lowent source files that are translated together and form one unit of execution. The exact composition is
> specified by ⟦§5.1⟧.

> [!용어] 모듈 (module)
> One named source file. A module writes its own name on its first line and marks what it exports to other
> modules. A module name need not be the same as the file name.

> [!용어] op (op)
> A unit of execution with a name and a contract. It corresponds to a «function» in other languages, but in Lowent
> **its kind depends on whether it has side effects** (⟦§6.4.1⟧): one that produces no effect at all is
> `fn`, one that produces effects is `proc`.

> [!용어] 선언 (declaration)
> A construct that gives a name a meaning. It says what exists.

> [!용어] 정의 (definition)
> A declaration that also supplies the entity itself. The definition of an op includes its body.

## 3.2 Values and types

> [!용어] 값 (value)
> What a computation produces. Every value has exactly one type.

> [!용어] 타입 (type)
> A set of values together with the operations allowed on them. Every Lowent type
> **has a fixed size and representation** — there is no place the processor decides at will (⟦§6.2⟧).

> [!용어] 폭 (width)
> The number of bits an integer type occupies. The width of `u8` is 8. The width of a type with a number in its name
> is **the same on every processor**. Only `usize` · `idiff` are exceptions; their width is chosen by the implementation — at least 16
> bits, usually the address width of the target machine (⟦§6.2.2⟧).

> [!용어] 슬라이스 (slice)
> A reference to a range of values of the same type laid out contiguously. It carries a start position and a **length** together.
> Carrying the length is what makes it differ from a pointer, and is why its bounds can be checked (⟦§6.2.6⟧).

> [!용어] 문자 리터럴 (character literal)
> One character in single quotes (`'a'`). Its value is that character's code, an integer. What does not fit in one
> slot is not a character literal (⟦§6.1.4⟧).

> [!용어] 문자열 리터럴 (string literal)
> A row of bytes in double quotes (`"abc"`). `len` gives **the number of elements** — the prefix decides what an
> element is.

> [!용어] 이스케이프 (escape)
> A notation inside a character or string, starting with a backslash, that stands for one character (`\n`). There are
> **only fourteen** (⟦§6.1.4⟧).

> [!용어] 접두사 (prefix)
> A word placed before a literal that decides what its elements mean. There are **only two**: `u` (UTF-16 code unit) · `U` (code point).
> Without a prefix the elements are bytes.

> [!용어] 텍스트 리터럴 (text literal)
> A string that holds several lines as they are. A tag is written after `text`, and the literal ends at a line on which the same tag
> stands alone from the very first column. Escapes in the body are not processed. It corresponds to what other languages call a heredoc,
> nowdoc or delimited string.

> [!용어] 부동소수점 리터럴 (floating-point literal)
> A number with a decimal point or an exponent written directly in the source (`1.5` · `1e3` · `0x1p3`).
> There shall be digits on both sides of the point — `.5` and `1.` are not floating-point literals (⟦§6.1.4⟧).

> [!용어] 리터럴 (literal)
> A value written directly in the source. `42`, `true`, `"text"` and the like.

> [!용어] 열거 (enum)
> A value that is **exactly one** of a set of named variants. Each variant may carry a value.
> Since the variant name says what the value is, some bit patterns remain unused —
> so an enum has «trap representations».

> [!용어] 덫 표현 (trap representation)
> A bit pattern that is **not** a value of its type. Reading such a bit pattern as a value of that type
> has no meaning, and this language does not allow such a read (⟦§8.7.1⟧). A scalar type with
> no trap representation at all is called `plain`.

> [!용어] 상표 (brand)
> A **name that separates** storage. So that values with the same representation but from different storage do not
> mix, a type made with `newtype` is used as the name of that storage. One brand
> designates one storage (⟦§6.2.9⟧).

## 3.3 Contracts and verification

> [!용어] 계약 (contract)
> The **promise an op writes for itself** about its input and output. It consists of six clauses —
> `requires` (what shall be true on entry) · `ensures` (what is true on exit) · `errors` (when it fails) ·
> `effects` (what effects it produces) · `access` (what it touches, and how) · `tests` (examples).
> The full definition is in ⟦§6.4.3⟧.

> [!용어] 사전 조건 (precondition)
> What the `requires` clause of a contract says. A condition that shall be true at the moment of entering the op.

> [!용어] 사후 조건 (postcondition)
> What the `ensures` clause of a contract says. A condition that is true when the op ends normally.

> [!용어] 검사 (check)
> Confirming during execution that a condition is true. If the processor **proves that the condition is always true**,
> it removes the check. If it cannot remove it, the check remains (⟦§6.4.6⟧).

> [!용어] 트랩 (trap)
> **Stopping the program at once** when a condition is broken. Lowent does not continue computing in a wrong
> state — it stops rather than return a wrong answer.

> [!용어] 미정의 동작 (undefined behaviour)
> Behaviour the source does not determine. There is none in the **safe subset** (⟦§4.4⟧) — within it, every situation
> this document does not define ends in a diagnostic (⟦§5.4⟧) or a trap. This is where it differs most from
> C.

> [!용어] 안전한 부분집합 (safe subset)
> The set of programs that do not use `unsafe` and `extern`. Within this set there is no undefined behaviour.

> [!용어] 빌드 모드 (build mode)
> The setting that decides whether contract checks that could not be proved stay during execution or are removed. It is written in the
> source as `build <mode> .` (⟦§6.4.7⟧).

> [!용어] 신뢰 경계 (trust boundary)
> The place where the language's guarantee ends and a person's promise begins. `unsafe` and `extern` mark that place
> in the source (⟦§6.9⟧).

## 3.4 Effects and capabilities

> [!용어] 효과 (effect)
> An influence an op has on the outside world. Reading a file, looking at a clock, allocating memory,
> creating another flow of execution and the like are each an effect. An op shall write the effects it produces in its contract,
> and if it produces more effects than written, translation is refused (⟦§7.1⟧).

> [!용어] 순수 (pure)
> Producing no effect at all. A pure op always gives the same result for the same input.

> [!용어] 권한 (capability)
> A **value** that stands for the right to produce an effect. Lowent has no global capability anyone
> may use — to read a file, a file capability shall be **handed over** as an argument (⟦§7.2⟧).

## 3.5 Memory and ownership

> [!용어] 영역 (region)
> A section of memory where values live. For each value, the region it lives in is fixed together with its type (⟦§8.1⟧).

> [!용어] 참조 (reference)
> Something that points at a value elsewhere. Read-only references and write references are distinguished, and for one value
> there can be only one write reference at a time (⟦§8.4⟧).

> [!용어] 소유 (ownership)
> Who is responsible for destroying a value. Lowent tracks owners statically, so
> destroying twice or forgetting to destroy is refused at translation (⟦§8.5⟧).

> [!용어] 탈출 (escape)
> A reference to a local value outliving that value. Lowent refuses this at
> translation (⟦§8.4.1⟧).

## 3.6 Processors and conformance

> [!용어] 처리기 (processor)
> The general name for whatever translates or executes Lowent programs. A compiler, an interpreter, and something that does
> both are all processors. This document does not specify their internals; it specifies only **observable behaviour**.

> [!용어] 번역 (translation)
> Turning a source into a form that can be executed. Errors found during translation are reported as diagnostics.

> [!용어] 진단 (diagnostic)
> A message by which a processor reports an error. It has a stable identifier (for example `E-TYPE-INSTANCE`), and
> the meaning of that identifier does not change across editions (⟦§5.4⟧).

> [!용어] 적합한 프로그램 (conforming program)
> A program that keeps all the syntax and constraints of this document (⟦§4.1⟧).

> [!용어] 적합한 처리기 (conforming processor)
> A processor that translates and executes conforming programs as this document specifies, and issues
> diagnostics for programs that do not conform (⟦§4.2⟧).

## 3.7 Source and notation

> [!용어] 로우엔트 (Lowent)
> The programming language this document defines. The name is short for **low entropy**.

> [!용어] 토큰 (token)
> The smallest piece a source is divided into. Keywords, identifiers, literals and punctuation are tokens.

> [!용어] 낱말 (keyword)
> A name whose meaning the language has fixed. It cannot be used with another meaning. The list is in annex A.

> [!용어] 이름 (identifier)
> A name the author makes. It consists of ASCII letters, digits and underscores — **it contains no dot.**
> A dot is the mark that joins two names when choosing a field of a value or naming a module (⟦§6.1.3⟧).

> [!용어] 주석 (comment)
> From `rem` to the end of the line. The processor treats it as absent.

> [!용어] 폼 (form)
> One unit of prefix notation. A closer marks its end.

> [!용어] 닫개 (closer)
> The mark that says a form has ended. It is **one** full stop `.`. A parenthesis `)` also closes the forms opened inside it,
> and `end` closes only its own `do` (⟦§6.1.6⟧).

> [!용어] 개행 (newline)
> The place where a line changes. It is treated exactly like a space and does **not** close a form (⟦§6.1.6⟧).

> [!용어] 전위 표기 (prefix notation)
> A notation in which the name of an operation comes first and its arguments follow. There are no precedence rules.

> [!용어] 중위 표기 (infix notation)
> A notation in which the operator symbol comes between its arguments. It can be used only inside an `expr` island.

> [!용어] 섬 (island)
> The place where infix notation is switched on. It begins with `expr`.

> [!용어] BOM (byte order mark)
> A mark some editors put at the start of a file (U+FEFF). It cannot appear in a Lowent source.

## 3.8 Computation and statements

> [!용어] 식 (expression)
> Something that produces a value.

> [!용어] 문장 (statement)
> Something that is executed. An expression makes a value; a statement does work.

> [!용어] 기본 연산 (core op)
> An operation whose meaning the language fixes and which is called anywhere by its name alone. It shares one namespace with the author's names, so it cannot be hidden by them.

> [!용어] 빌트인 연산 (builtin op)
> An operation whose meaning the language fixes and which is called only after `call_builtin`. It has its own namespace, so it does not collide with the author's names.
> It is for building system libraries and processors; ordinary programs use the libraries that wrap it.

> [!용어] 단락 평가 (short-circuit)
> Not computing the second part when the first part alone decides the result. `and` and `or` do this.

> [!용어] 넓히기 (widening)
> A type conversion that loses no value. It may happen automatically.

> [!용어] 좁히기 (narrowing)
> A type conversion that may lose a value. It shall be written in the source, and traps if the value does not fit.

> [!용어] 트레이트 (trait)
> A list of ops a type shall provide. If a type is written as providing it, the processor checks.
> It makes no hierarchy among types — this language has no inheritance.

> [!용어] 액터 (actor)
> A unit of execution with its own state. The state exists only inside the actor.

## 3.9 Environments and conformance

> [!용어] 번역 환경 (translation environment)
> The place where a source is turned into a form that can be executed.

> [!용어] 실행 환경 (execution environment)
> The place where the result actually runs.

> [!용어] 시작점 (entry point)
> The op at which an executed program starts. Its name is `main`.

> [!용어] 종료 상태 (exit status)
> The value the entry point returns. 0 means success.

> [!용어] 라이브러리 모듈 (library module)
> A translation unit without an entry point. Other programs take it and use it.

> [!용어] 적합성 (conformance)
> Whether what this document specifies has been kept. Used of programs and of processors.

> [!용어] 구문 (syntax)
> The rules about which arrangements of characters are programs.

> [!용어] 제약 (constraints)
> Rules that shall be kept even when the syntax is right. Violating one produces a diagnostic.

> [!용어] 의미 규칙 (semantic rules)
> The rules about what a correct program does.

> [!용어] 표현 (representation)
> Which bits a value is laid out as inside the machine.

> [!용어] 옥텟 (octet)
> 8 bits. In this language a byte is always an octet.

> [!용어] 투스 컴플리먼트 (two's complement)
> A way of representing signed integers. It is the only way this language requires.

> [!용어] 자리 (place)
> Somewhere a value **can be written**. A local variable, a field of a struct, one slot of a row are places.
> What can stand on the left of `set` is exactly a place — something that can only be read is not a place.

> [!용어] 한정 (qualification)
> **Fixing which name is meant** by putting in front of a name the place where it lives.
> In this language qualification is written with a dot **attached** to the name, and it has only three meanings: a module,
> a variant, and a declaration belonging to a type. Reading the inside of a value is not qualification.

> [!용어] 목적지 (destination)
> The place where a value is made and put. If the three conditions of ⟦§8.3.1⟧ hold, the value is made here
> **directly**, without an intermediate temporary.

> [!용어] 임시값 (temporary)
> A value in a nameless place, made when there is no destination or the conditions do not hold. It lives in the smallest
> enclosing scope, and when the scope ends the temporaries are destroyed **in the reverse order of their making**.

> [!용어] 가비지 컬렉터 (garbage collector)
> A mechanism that finds unused memory during execution and gives it back. **This language has none.**

> [!용어] 스테이지 (stage)
> A place inside a `pipe` of ⟦§6.12⟧ that takes one element and passes it on. The vocabulary is closed
> (seven), so no other operation can be a stage.

> [!용어] 종결자 (terminal)
> The place that ends a `pipe` and gives its result. A `pipe` has **exactly one** terminal.

> [!용어] 문맥 (context)
> A value attached with `with` to a stage or terminal of a `pipe`, computed once before the traversal and handed to the op as its last argument
> (⟦§6.12⟧ (7)). Only a value without references can be a context.

> [!용어] 로컬 op (local op)
> An op declared directly below an op body and visible only inside that op (⟦§6.10.5⟧). It does not see outer names, so
> it is not a closure.

> [!용어] 최적화 (optimisation)
> The processor's discretion to make a program faster or smaller without changing its meaning.
> **Being discretion, a program shall not rely on it** — a property that must be relied on is written
> as meaning, not as optimisation (the fusion of ⟦§6.12⟧ is an example).
