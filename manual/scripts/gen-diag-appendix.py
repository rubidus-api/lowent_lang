#!/usr/bin/env python3
"""부록 B(진단 찾아보기)를 예제에서 짓는다.

examples/chNN/*.low 의 `rem expect: <코드>` 를 모아, 코드마다 그것을 보이는 장을 잇는다.
뜻은 아래 표가 준다 — 표에 없는 코드가 예제에 나타나면 실패한다(뜻 없는 줄을 싣지 않는다).
결과: typst-ko/appendix/a2-diagnostics.typ · typst-en/appendix/a2-diagnostics.typ
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

MEANING = {
 "E-DOT-MISSING": ("묶음을 닫는 마침표 `.` 가 빠졌다", "a stop `.` that closes a binding is missing"),
 "E-CTRL-NODO": ("블록 `do … end` 가 와야 할 자리에 다른 것이 왔다", "something other than a block `do … end` stands where one belongs"),
 "E-VALUE-DISCARDED": ("문이 값을 셈하고 버린다 --- 아무것도 바뀌지 않는다", "a statement computes a value and drops it, so nothing changes"),
 "E-LOCAL-CAPTURE": ("지역 op 이 감싼 op 의 이름을 읽는다(숨은 잡기는 없다)", "a local op reads a name of the op around it (there is no hidden capture)"),
 "E-LOOP-COUNT": ("`break` · `continue` 뒤의 수가 둘러싼 고리 수를 넘거나 리터럴이 아니다", "the count after `break` / `continue` is not a literal from 1 up to the loops around it"),
 "E-CLOSER-EXTRA": ("아무것도 닫지 않는 마침표", "a stop that closes nothing"),
 "E-LIT-ESCAPE": ("나열 리터럴의 틀 안 자리를 블록 밖으로 내보낸다", "a view of a list literal's frame storage leaves the block that declared it"),
 "E-MATRIX-INDEX": ("행렬의 차원 수만큼 번호를 주지 않았다", "a matrix is indexed with a number of indexes other than its dimensions"),
 "E-TYPE-ROWNAME": ("줄 타입(`slice` · `array` · `matrix`)에 다른 이름을 붙였다", "a row type (`slice`, `array`, `matrix`) is given another name"),
 "E-CAP-OUTPUT": ("op 의 출력이 권한이다", "the output of an op is a capability"),
 "E-ALLOC-NOROOT": ("할당이 깎아 낼 뿌리를 첫 피연산자로 이름 대지 않았다", "an allocation does not name the root it carves from as its first operand"),
 "E-USING-OLD": ("할당기 절을 옛 낱말 `using` 으로 적었다(`use` 로)", "the allocator clause is written with the old word `using` (write `use`)"),
 "E-EFFECT-MISSING": ("`proc` 머리에 `effects` 가 없다", "a `proc` head has no `effects` clause"),
 "E-PIPE-CONTEXT-TYPE": ("`with` 문맥이 참조(주소)를 품은 값이다", "a `with` context is a value that holds a reference (an address)"),
 "E-ACTOR-FAILURE": ("액터가 `failure` 를 두 번 적었다", "an actor declares `failure` twice"),
 "E-ALLOC-NESTED": ("같은 뿌리의 안쪽 영역이 열린 채 바깥 출처로 깎았다", "allocated from an outer source while an inner region of the same root is open"),
 "E-ALLOC-NOCAP": ("`alloc` 을 적었는데 할당 권한을 받지 않았다", "declares `alloc` but receives no allocation capability"),
 "E-ASM-UNBOUND": ("어셈블리 템플릿이 선언하지 않은 피연산자를 부른다", "the assembly template names an undeclared operand"),
 "E-ATOMIC-NOCAP": ("`atomic` 을 적었는데 `cap atomic` 을 받지 않았다", "declares `atomic` but receives no `cap atomic`"),
 "E-ATOMIC-ORDER": ("그 원자 연산에 뜻이 없는 기억 차례다", "a memory ordering that has no meaning for that atomic operation"),
 "E-BOUND-UNSAT": ("타입 인자가 요구한 트레이트를 갖추지 못했다", "the type argument does not satisfy the required trait"),
 "E-CAP-FORGE": ("권한 칸을 가진 액터를 그 권한 없이 띄운다", "spawns an actor holding a capability without holding that capability"),
 "E-CAP-KIND": ("다른 종류의 권한을 건넸다", "a capability of the wrong kind was handed over"),
 "E-CAP-LOCAL": ("받은 권한을 지역 이름에 옮겨 담았다", "a received capability was copied into a local name"),
 "E-FOLD-ORDER": ("fold 단계 op 의 누산값 자리가 결과 타입과 다르다", "a fold stage op declares its accumulator with a type other than its result"),
 "E-FFI-LINK": ("extern op 이 부를 씨 심볼을 적지 않았다", "an `extern` op does not name the C symbol it calls"),
 "W-EXPORT-HIDDEN": ("내보낸 서명이 감춘 타입을 쓴다", "an exported signature names a type the module keeps to itself"),
 "E-MONO-NOTYPE": ("제네릭 부름에 앞자리 타입 인자가 없다", "a generic call gives no leading type argument"),
 "E-VEC-SPLAT": ("splat 을 식 안에 바로 적었다", "`splat` was written inline in an expression"),
 "E-CONTRACT-UNSAT": ("함께 참일 수 없는 전제 둘", "two preconditions that cannot both hold"),
 "E-TYPE-MIX": ("부동소수점 수와 정수 리터럴을 섞어 셈한다", "float and integer literals mixed in one calculation"),
 "E-NAME-SCOPE": ("블록 안에서 지은 이름을 블록 밖에서 읽는다", "a name declared inside a block is read outside it"),
 "E-STMT-ELSE": ("`else` 가 블록 안에 있다", "`else` sits inside the block"),
 "E-TYPE-REFVAL": ("참조를 값처럼 썼다", "a reference is used as if it were a value"),
 "E-TYPE-ARG": ("인자의 타입이 매개변수와 맞지 않는다", "an argument's type does not match the parameter"),
 "E-METHOD-RECV": ("붙은 op 의 수신자가 첫 입력이 아니다", "an attached op's receiver is not its first input"),
 "E-CAP-MISSING": ("권한이 필요한 기본 연산에 권한을 첫 피연산자로 적지 않았다", "a capability-requiring core op was not given its capability as the first operand"),
 "E-CLAUSE-ORDER": ("op 머리의 절이 정해진 차례를 어겼다", "the clauses of an op header are out of the fixed order"),
 "E-COMPTIME-ARG": ("`comptime` 입력 자리에 실행 값을 주었다", "a run-time value in a `comptime` input position"),
 "E-CONC-ALONE": ("묶음이 흐름 하나만 만들고 그 흐름이 짝을 기다린다", "a task group spawns one task that waits for a peer"),
 "E-CONC-DEADLOCK": ("묶인 흐름이 모두 받기만 하고 아무도 보내지 않는다", "every task in the group receives and none sends"),
 "E-CONTRACT-DEAD": ("`requires` 가 이미 배제한 오류를 선언했다", "declares an error that `requires` already excludes"),
 "E-CONTRACT-IMPOSSIBLE": ("상수끼리의 호출이 상대의 `requires` 를 어긴다", "a call with constant arguments breaks the callee's `requires`"),
 "E-EFFECT": ("선언하지 않은 효과를 낸다", "performs an effect its `effects` clause does not declare"),
 "E-EFFECT-CALC": ("순수한 `fn` 이 효과를 낸다", "a pure `fn` performs an effect"),
 "E-EFFECT-DUP": ("효과 줄에 같은 원자를 두 번 적었다", "the same atom twice in an `effects` clause"),
 "E-EFFECT-NO-CAP": ("`io` 따위를 적었는데 허락하는 권한이 없다", "declares `io` (or similar) without a capability that authorises it"),
 "E-EFFECT-NONE-MIX": ("`none` 을 다른 효과와 함께 적었다", "`none` written together with a real effect"),
 "E-EFFECT-PURITY": ("`fn` 이 호출자나 액터가 볼 수 있는 상태에 쓴다", "a `fn` writes state visible to its caller or actor"),
 "E-EFFECT-REDUNDANT": ("`fn` 에 `effects none` 을 적었다", "`effects none` written on a `fn`"),
 "E-EFFECT-UNDEF": ("목록에 없는 효과 낱말이다", "an effect word outside the closed vocabulary"),
 "E-ENTRY-PARAMS": ("시작점이 권한 아닌 입력을 받는다", "the entry point takes a non-capability input"),
 "E-ENUM-DOT": ("열거의 갈래를 마침표로 닫지 않았다", "an enum variant not closed with `.`"),
 "E-ENUM-INFINITE": ("갈래가 자기 타입을 값으로 품는다", "a variant embeds its own type by value"),
 "E-ERR-UNDECLARED": ("`errors` 절에 없는 오류를 돌려준다", "returns an error not in its `errors` clause"),
 "E-ESCAPE": ("지역을 가리키는 참조가 op 밖으로 나간다", "a reference to a local escapes the op"),
 "E-EXCL": ("같은 값에 대한 빌림이 겹친다", "overlapping borrows or owner access to the same value"),
 "E-EXPR-APP": ("`expr` 섬 안의 부름을 괄호로 묶지 않았다", "a call inside an `expr` island is not parenthesised"),
 "E-EXPR-CHAIN": ("`expr` 섬에서 비교를 이어 썼다", "comparisons chained in an `expr` island"),
 "E-EXPR-UNARY": ("`expr` 섬에 단항 연산자를 썼다", "a unary operator in an `expr` island"),
 "E-FFI-NOCAP": ("C 를 부르는데 `cap c` 를 받지 않았다", "calls C without receiving `cap c`"),
 "E-FFI-NOUNSAFE": ("C 를 부르는데 `unsafe` 표시가 없다", "calls C without the `unsafe` mark"),
 "E-FFI-TYPE": ("C ABI 가 표현할 수 없는 타입이 경계를 건넌다", "a type the C ABI cannot express crosses the boundary"),
 "E-GROUP-UNCLOSED": ("괄호가 닫히지 않았다", "an unclosed parenthesis"),
 "E-GUARD-FALLTHROUGH": ("`guard` 의 `else` 가 떠나지 않는다", "the `else` of a `guard` does not leave"),
 "E-HEAP-NOHOST": ("운영체제 없는 대상에서 자라는 뿌리를 청한다", "asks for the growing root on a freestanding target"),
 "E-IF-VALUE": ("`if` 를 값으로 썼다", "`if` used as a value"),
 "E-IMMUTABLE": ("`let` 으로 지은 이름에 `set` 했다", "`set` on a name bound with `let`"),
 "E-ISR-CALLED": ("인터럽트 처리기를 코드에서 불렀다", "an interrupt handler called from code"),
 "E-LET-NOVALUE": ("타입 뒤에 값이 없다", "no value after the type"),
 "E-LET-BE": ("옛 꼴 `let x be …` 를 적었다 --- `be` 는 낱말이 아니다", "the old form `let x be …` --- `be` is not a word of the language"),
 "E-CTRL-DOT": ("머리의 식과 `do` 사이에 마침표를 찍었다", "a stop between a head's expression and `do`"),
 "E-FOR-OLD": ("옛 꼴의 `for`(`count` · `range` · 점화식)를 적었다", "an old `for` form (`count`, `range`, recurrence)"),
 "E-MATCH-INEXHAUSTIVE": ("`match` 가 모든 경우를 덮지 않는다", "a `match` does not cover every case"),
 "E-MATCH-REDUNDANT": ("`match` 의 갈래가 영영 돌지 않는다(`_` 뒤의 갈래·겹친 범위)", "a `match` arm can never run (an arm after `_`, overlapping ranges)"),
 "E-METHOD-UNDEF": ("수신자의 타입에 그 이름의 붙은 op 이 없다", "no op of that name is attached to the receiver's type"),
 "E-MMIO-BYVALUE": ("레지스터 묶음을 값으로 받는다", "an mmio register block taken by value"),
 "E-MMIO-PERM": ("읽기 전용 레지스터에 쓴다", "writes a read-only register"),
 "E-MREF-SLICE": ("`mut ref slice` 를 썼다", "`mut ref slice` used"),
 "E-NAME-DUP": ("한 모듈에 같은 이름을 두 번 선언했다", "the same name declared twice in one module"),
 "E-NAME-SHADOW": ("살아 있는 이름을 다시 지었다(가림)", "re-binds a live name (shadowing)"),
 "E-OPT-UNUSED": ("선언한 빌드 손잡이를 아무 코드도 읽지 않는다", "a declared build option that no code reads"),
 "E-OWN-INCOMPLETE": ("완결이 필요한 값을 끝내지 않고 버린다", "a value that needs completion is dropped implicitly"),
 "E-OWN-JOIN": ("갈래마다 소유 상태가 다르다", "ownership state differs between branches"),
 "E-OWN-MOVED": ("옮긴 값을 다시 쓴다", "uses a value after it was moved"),
 "E-PAR-ASSOC": ("모으는 연산이 결합적이지 않다", "the reduction operator is not associative"),
 "E-PAR-CARRY": ("나누는 되풀이가 걸음을 넘어 사는 자리에 쓴다", "a split loop writes a local that lives across iterations"),
 "E-PAR-READ": ("나누는 되풀이가 남의 원소를 읽는다", "a split loop reads another iteration's element"),
 "E-PAR-WRITE": ("나누는 되풀이가 남의 원소에 쓴다", "a split loop writes another iteration's element"),
 "E-PIPE-NO-TERMINAL": ("종결자 뒤에 스테이지가 온다", "a stage after the terminal"),
 "E-PIPE-STAGE": ("`pipe` 스테이지 목록에 없는 낱말이다", "a word that is not a `pipe` stage"),
 "E-PROFILE-LEVEL": ("빌드 프로파일이 주지 않는 동시성을 쓴다", "uses concurrency the build profile does not provide"),
 "E-REGION-ESCAPE": ("영역에서 얻은 바이트를 밖으로 들고 나간다", "carries region bytes out of the region"),
 "E-REGION-KIND": ("영역의 종류가 닫힌 여덟에 없다", "a region kind outside the closed eight"),
 "E-RETURN-PARTIAL": ("어떤 길에서 값을 돌려주지 않는다", "some path does not return a value"),
 "E-SPAWN-SCOPE": ("`task_group` 밖에서 흐름을 만든다", "spawns a task outside a `task_group`"),
 "E-TIER-EFFECT": ("선언한 기계 등급이 감당하지 못하는 효과다", "an effect the declared machine tier cannot carry"),
 "E-TRAIT-EFFECT": ("갖춘 op 이 서명보다 많은 효과를 가진다", "the implementing op has more effects than the signature"),
 "E-TRAIT-MISSING": ("트레이트의 op 하나가 없다", "an op required by the trait is missing"),
 "E-TRAIT-SIG": ("서명에 `fn`·`proc` 을 적었거나 매개변수 수가 다르다", "`fn`/`proc` in a signature, or a parameter-count mismatch"),
 "E-TYPE-ARRAY": ("고정 길이 `array` 를 둘 자리가 없는 곳에 적었다 · 세 겹 배열", "a fixed-length `array` where its length has nowhere to be kept · three levels of arrays"),
 "E-TYPE-ORDER": ("개수를 받는 타입의 원소 타입을 개수 앞에 적었다(옛 차례)", "the element type of a counted type written before the count (the old order)"),
 "E-TYPE-BITCAST": ("`bit_cast` 의 목표가 모든 비트열이 값인 타입이 아니다", "`bit_cast` target is not a plain scalar"),
 "E-TYPE-COND": ("조건 자리에 참거짓이 아닌 값이 왔다", "a non-boolean in a condition"),
 "E-TYPE-DECL": ("타입 선언에 `be` 를 끼웠다", "`be` in a type declaration"),
 "E-TYPE-INSTANCE": ("같은 제네릭의 서로 다른 인스턴스를 섞었다", "mixes different instances of the same generic"),
 "E-TYPE-MUT": ("`mut` 이 아닌 슬라이스의 원소에 쓴다", "writes an element of a non-`mut` slice"),
 "E-TYPE-NOMINAL": ("표현이 같아도 이름이 다른 타입을 섞었다", "mixes nominally distinct types"),
 "E-TYPE-REF": ("읽기 참조로 쓴다", "writes through a shared `ref`"),
 "E-TYPE-SIGN": ("값을 지키는 넓히기가 없는 부호 섞기다", "mixes signs with no value-preserving widening"),
 "E-TYPE-WIDTH": ("값이 선언된 타입에 들어가지 않는다", "the value does not fit the declared type"),
 "E-VISIBILITY": ("다른 모듈의 감춘 이름에 닿는다", "reaches a non-exported name of another module"),
 "E-VOCAB-REMOVED": ("없앤 낱말이나 철자다", "a removed word or spelling"),
 "W-EFFECT-OVER": ("선언만 하고 내지 않는 효과다(경고)", "an effect declared but never performed (warning)"),
 "W-USE-EXTERNAL": ("번역 단위에 없는 모듈을 들여온다(경고)", "imports a module not in the compilation unit (warning)"),
 "E-ALLOC-AMBIGUOUS": ("맞는 할당기가 둘 이상인데 `use` 로 고르지 않았다", "more than one fitting allocator and none chosen with `use`"),
 "E-ALLOC-USING-UNUSED": ("할당기를 쓰지 않는 호출에 `use` 를 적었다", "`use` on a call that does not draw from an allocator"),
 "E-TRY-NORESULT": ("`try` 를 썼는데 이 op 은 그 오류를 돌려줄 수 없다", "`try` used in an op that cannot return that error"),
 "E-TYPE-COLLECT": ("`collect into` 가 넓은 값을 좁은 그릇에 담는다", "`collect into` would put a wider value into a narrower buffer"),
 "E-TYPE-ARGMUT": ("읽기만 되는 값을 고치는 자리에 넘긴다(`let` 을 `mut_ref` 로)", "a read-only value is passed where it would be written (`mut_ref` of a `let`)"),
 "E-ERRORS-STATE": ("`errors` 조건이 몸통이 바꿀 수 있는 이름을 읽는다", "an `errors` condition reads a name the body can change"),
 "E-ACTOR-FIELD": ("액터의 상태 칸을 바깥에서 읽는다", "an actor's state field is read from outside"),
 "E-ACTOR-STATE-REF": ("액터 상태 칸에 빌림을 둔다", "a borrow is kept in an actor state field"),
 "E-ACTOR-UNINIT": ("갓 띄운 액터의 슬라이스 칸을 세우기 전에 읽는다", "a just-spawned actor's slice state field is read before it is set"),
 "E-MMIO-PLAIN": ("장치 레지스터를 보통의 칸처럼 읽거나 쓴다", "a device register is read or written like an ordinary field"),
 "E-USE-ALIASED": ("`as` 로 바꿔 부른 모듈을 원래 이름으로 부른다", "a module renamed with `as` is called by its original name"),
 "W-RESULT-DISCARD": ("`result` 를 문장으로 버렸다 --- 실패가 자취 없이 사라진다", "a `result` is dropped as a statement --- the failure leaves no trace"),
 "W-ERRORS-UNRAISED": ("`errors` 로 적은 오류를 몸통이 한 번도 내지 않는다", "an `errors` clause names a failure the body never returns"),
 "W-CONFIG-DEPENDS": ("기댐이 안 맞는 손잡이에 구성이 값을 골랐다", "the config picks a value for a knob whose dependency is off"),
 "E-PAR-IDENTITY": ("`reduce` 의 시작값이 그 연산의 항등원이 아니다", "a `reduce` starts from a value that is not the operator's identity"),
 "W-NOT-YET": ("이름은 받지만 아직 뜻이 없는 낱말을 썼다", "a word accepted by name that has no meaning yet"),
 "E-LOCK-NOTYET": ("흐름끼리 나누는 자물쇠 타입은 아직 짓지 않았다", "shared lock types are not built yet"),
 "E-ACCESS-MODE": ("`access` 로 적은 읽기·쓰기 약속을 몸이 어긴다", "the body breaks the read or write promise made with `access`"),
 "E-ALLOC-NOSOURCE": ("할당기를 쓰는 호출인데 이 op 안에 맞는 할당기가 하나도 없다", "a call draws from an allocator but no fitting one is visible in this op"),
 "E-ALLOC-OUTLIVES": ("영역의 바이트를 영역 밖에서 태어난 액터에게 건넨다", "region bytes handed to an actor born outside the region"),
 "E-ALLOC-TASK": ("뿌리에서 깎는 op 을 태스크로 띄운다", "an op that carves from a root is spawned as a task"),
 "E-ALLOC-SHARED": ("원자적이지 않은 할당기를 태스크에 건넨다", "an allocator whose cursor is not atomic is handed to a task"),
 "E-ASM-TARGET-UNKNOWN": ("`asm` 절의 기계 이름이 대상 목록에 없다", "the machine name in an `asm` clause is not a known target"),
 "E-BLOCK-UNCLOSED": ("`do` 로 연 블록을 `end` 로 닫지 않았다", "a block opened with `do` is never closed with `end`"),
 "E-BRAND-REUSED": ("같은 브랜드로 저장소를 두 번 열었다", "a brand opens a second storage"),
 "E-CHAR": ("이 언어에 없는 글자(기호)다", "a character (symbol) this language does not have"),
 "E-CHAR-WIDTH": ("글자 리터럴이 글자 하나가 아니다", "a character literal is not a single character"),
 "E-CONFIG-TYPE": ("구성이 손잡이의 선택지에 없는 값을 준다", "the configuration gives a value the option does not offer"),
 "E-CONFIG-UNDEF": ("선언하지 않은 빌드 손잡이를 구성이나 `config` 가 부른다", "a configuration or `config` names an undeclared build option"),
 "E-DEP-MISSING": ("`use … from` 의 자리에서 파일을 읽을 수 없다", "the file at a `use … from` place cannot be read"),
 "E-ENS-UNDEF": ("`ensures` 가 없는 이름을 가리킨다 --- 돌려주는 값은 `ret`", "`ensures` names something undefined --- the returned value is `ret`"),
 "E-FFI-NOEFFECT": ("C 를 부르는 op 에 효과 줄이 없다", "an op calling C declares no effect"),
 "E-FIELD-GLUED": ("값 뒤에 점을 붙여 칸을 읽으려 했다 --- `field` 로 적는다", "tried to read a field by gluing a dot to a value --- write `field`"),
 "E-FN-CAP": ("되부름으로 넘길 op 이 권한을 요구한다", "an op to be passed as a callback requires a capability"),
 "E-FN-NOTEXPORT": ("`unsafe_fn` 이 `export extern` 이 아닌 op 을 가리킨다", "`unsafe_fn` names an op that is not `export extern`"),
 "E-FOLD-OP": ("`pipe` 스테이지가 없는 op 을 부른다", "a `pipe` stage names an op that does not exist"),
 "E-IR-ARITY": ("op 을 인자 수가 맞지 않게 불렀다", "an op is called with the wrong number of arguments"),
 "E-IR-UNDEF": ("그 자리에서 알 수 없는 이름이다", "a name unknown at that place"),
 "E-ISR-EFFECT": ("인터럽트 처리기가 `effects device` 를 적지 않았다", "an interrupt handler does not declare `effects device`"),
 "E-ISR-PARAMS": ("인터럽트 처리기가 매개변수를 받는다", "an interrupt handler takes parameters"),
 "E-NAME-BUILTIN": ("기본 연산 의 이름을 선언이나 지역 이름으로 썼다", "a core op's name is used for a declaration or local"),
 "E-PAR-FLOAT": ("부동소수점 누적을 나누어 모으려 했다", "a floating-point accumulation declared splittable"),
 "E-PAR-NOLOOP": ("`parallel` 절이 나눌 되풀이를 찾지 못했다", "the `parallel` clause finds no loop to split"),
 "E-TOPLEVEL": ("최상위에 올 수 없는 것이 최상위에 있다", "something that cannot appear at top level is at top level"),
 "E-TYPE-FIELD": ("구조체를 만들 때 칸이 빠졌거나 없는 칸을 적었다", "a field is missing or unknown when building a struct"),
 "E-TYPE-KIND": ("그 연산에 쓸 수 없는 갈래의 타입이다(예: 수를 `cast` 로 `bool` 로)", "a type of the wrong kind for that operation (e.g. `cast` to `bool`)"),
 "E-TYPE-LOGICAL": ("논리 연산에 참거짓이 아닌 값을 주었다", "a non-boolean value given to a logical operation"),
 "E-TYPE-RETURN": ("돌려주는 값이 op 의 출력 타입과 다르다", "the returned value does not match the op's output type"),
 "E-UNSAFE-UNDECLARED": ("`unsafe` 효과를 적었는데 op 에 `unsafe` 표시가 없다", "declares the `unsafe` effect but the op is not marked `unsafe`"),
 "E-WIDEN-SIGN": ("부호 있는 수를 부호 없는 타입으로 `widen` 했다", "`widen` from a signed to an unsigned type"),
}

# ★ 2026-10-31 (W-20261031-1118) — 머리말과 «그 밖의 진단» 은 손으로 쓴 글이다. 전에는 부록에만 있어서, 이 스크립트를 돌리면
#   그 절과 손으로 고친 뜻이 사라졌다(스크립트가 «고장» 으로 남은 까닭). 이제 글은 여기에 있고 스크립트가 그대로 싣는다.
LEAD = {"ko": '앞의 두 표는 이 책의 예제가 실제로 일으킨 진단이다. 예제에는 안 나오지만 만날 수 있는 진단은 맨 끝 «그 밖의 진단» 에 모았다. 코드는 판이 바뀌어도\n같은 뜻을 지킨다. 진단의 긴 영문 설명과 고치는 법은 컴파일러가 그 자리에서 말한다(`--diag-json` 은 기계가 읽는 꼴로).',
        "en": 'The first two tables list the diagnostics this book\'s examples actually trigger. Diagnostics you can meet but the examples do not show are gathered\nat the end under "Other diagnostics". A code keeps its meaning across releases; the compiler explains the details and the repair at the site (`--diag-json` in a\nmachine-readable form).'}
OTHER = {"ko": '== 그 밖의 진단\n\n예제에는 나오지 않지만 쓰다 보면 만날 수 있는 진단이다. 대부분은 `--run` 으로 돌릴 때 *VM(실행기)* 이 멈추며 낸다. VM 은 시험과 대조를 위한\n실행기라 자원을 고정된 크기로 잡아 두고, 그 한도를 넘으면 *조용히 틀린 값을 내는 대신 멈춘다*. 한도는 프로그램의 결함이 아니라 VM 의 크기이므로,\n같은 프로그램을 네이티브로 지으면 대개 문제없이 돈다.\n\n#dtable(\n  columns: 2,\n  id: "a2-other-static",\n  caption: [번역할 때 --- 예제에 없는 것],\n  [*코드*], [*뜻과 고치는 법*],\n  [`E-IR-EXTRA`], [`expr` 섬 뒤에 남는 피연산자가 있다 --- 섬은 식 하나로 끝난다. 괄호로 묶거나 줄을 나눈다],\n  [`E-IR-LIMIT`], [한 번역 단위의 op 이 도구가 나를 수 있는 수보다 많다 --- 모듈을 나눈다],\n  [`E-IR-LOCALS`], [한 op 의 지역 이름이 너무 많다 --- op 을 쪼갠다],\n  [`E-MONO-FIXPOINT`], [제네릭 단형화가 끝나지 않는다(타입이 계속 자란다) --- 스스로를 더 큰 타입으로 부르는 제네릭을 끊는다],\n  [`W-COL0`], [맨 위 선언이 0 열에서 시작하지 않는다 --- 들여쓰기가 틀려 앞 선언의 몸에 들어간 줄일 수 있다],\n  [`W-UNBOUND`], [이름 붙인 `contract` 를 선언했는데 어느 op 도 `satisfies` 로 쓰지 않는다],\n  [`W-RFC-PENDING`], [설계가 아직 RFC 단계인 기능을 썼다 --- 프로그램에 고칠 것은 없다],\n  [`W-CBE-SLOW`], [이 op 이 네이티브에서 느린 길(태그 값)로 낮춰진다 --- 까닭은 `--why-slow` 가 op 마다 말한다],\n  [`E-GROUP-UNCLOSED`], [여는 괄호 `(` 를 닫지 않았다 --- 짝이 되는 `)` 를 적는다],\n  [`E-TOPLEVEL`], [최상위에 올 수 없는 것이 최상위에 있다 --- 문장은 op 의 몸 안에 둔다],\n  [`E-TYPE-DECL`], [타입 선언의 꼴이 틀렸다 --- 구조체는 `struct N … end`, 별칭은 `type N T .`(`be` 없이)],\n)\n\n#dtable(\n  columns: 2,\n  id: "a2-other-runtime",\n  caption: [실행 중에 --- 프로그램의 잘못],\n  [*코드*], [*뜻과 고치는 법*],\n  [`E-VM-ERR`], [오류인 `result` 에서 `ok_value` 로 값을 꺼냈다 --- 먼저 `is_ok` 로 묻거나 `try` 로 넘긴다],\n  [`E-VM-DANGLING`], [빌린 참조가 그 자리의 수명보다 오래 쓰였다(돌려준 뒤에 씀)],\n  [`E-VM-EXCL`], [한 자리를 쓰는 빌림과 다른 빌림이 겹쳐 쓰였다 --- 먼저 빌린 것을 끝내고 다음을 빌린다],\n  [`E-VM-READONLY`], [읽기 전용 참조(`ref`)로 썼다 --- 쓰려면 `mut_ref` 로 빌린다],\n  [`E-VM-FIELD`], [레코드 · 레지스터 블록에 없는 칸을 읽었다],\n  [`E-VM-AWAIT`], [`await` 에 작업 핸들이 아닌 값을 주었다 --- 핸들은 `spawn <op>` 이 준다],\n  [`E-VM-HANDLE-ZERO`], [비워 둔 핸들(0)을 기다리거나, 0 인 채널에 넣고 뺐거나, 띄우지 않은 액터(0)에 말을 보냈다 --- 먼저 `spawn <op>` · `channel <T>` · `spawn actor <이름>` 으로 받은 것을 준다],\n  [`E-VM-CHAN-EMPTY` · `E-VM-CHAN-FULL`], [빈 채널에서 받거나 찬 채널에 보냈는데, 기다려 줄 스케줄러가 없다 --- 영영 풀리지 않는 대기다],\n  [`E-VM-SCHED-LOOP`], [핸들러가 서로 메시지를 끝없이 주고받아 스케줄러가 멈추지 못한다],\n  [`E-VM-FD` · `E-VM-FMODE` · `E-VM-FWHENCE`], [쓸 수 없는 파일 번호(1 · 2 만 쓴다) · 없는 열기 방식(0 · 1 · 2) · 없는 `seek` 기준(0 · 1 · 2)],\n  [`E-VM-FHANDLE` · `E-VM-SHANDLE` · `E-VM-REACTOR`], [열린 파일 · 소켓 · 리액터 핸들이 아닌 값을 넘겼다],\n  [`E-VM-EXTERN`], [몸이 C 에 있는 `extern` op 을 VM 에서 불렀다 --- 네이티브로 지어서 돌린다],\n  [`E-VM-MMIO` · `E-VM-RESERVE`], [절대 주소의 레지스터 블록 · 링커가 주는 저장소를 VM 에서 열었다 --- 실제 기계에서만 뜻이 있다],\n)\n\n#dtable(\n  columns: 2,\n  id: "a2-other-limits",\n  caption: [실행 중에 --- VM 의 한도(프로그램의 결함이 아니다)],\n  [*코드*], [*무엇이 다 찼나*],\n  [`E-VM-STACK` · `E-VM-DEPTH` · `E-VM-OOM`], [피연산자 스택 · 호출 깊이 · 호출 스택을 키울 메모리 --- 재귀가 너무 깊다],\n  [`E-VM-BOXPOOL` · `E-VM-RECPOOL` · `E-VM-STKPOOL` · `E-VM-BSETPOOL`], [`some`·`ok` 로 감싼 값 · 구조체 값 · 스택 컨테이너 · 비트셋을 담는 풀 --- 한꺼번에 살아 있는 값을 줄인다],\n  [`E-VM-CHAN` · `E-VM-MBOX`], [채널 64 개 · 비동기 우편함 4096 칸 --- 다시 쓰지 않으므로 한 실행에서 그만큼만 만든다],\n  [`E-VM-FILEPOOL` · `E-VM-SOCKPOOL`], [동시에 열 수 있는 파일 · 소켓 수 --- 다 쓴 것을 닫는다],\n  [`E-VM-CHAIN-LIMIT`], [VM 안에서 VM 을 부르는 겹침이 너무 깊다],\n  [`E-VM-BUDGET`], [대조(오라클) 실행의 걸음 예산을 다 썼다 --- 평소 `--run` 에서는 나지 않는다],\n)\n',
         "en": '== Other diagnostics\n\nThese do not appear in the examples but can come up. Most are stops by the *VM* (the executor) when running with `--run`. The VM exists for tests\nand cross-checking, so it holds its resources at fixed sizes, and past a limit it *stops rather than quietly giving a wrong value*. A limit is the\nVM\'s size, not a defect of the program --- the same program built natively usually runs fine.\n\n#dtable(\n  columns: 2,\n  id: "a2-other-static",\n  caption: [At translation time --- not in the examples],\n  [*code*], [*meaning and repair*],\n  [`E-IR-EXTRA`], [operands are left over after an `expr` island --- an island ends with one expression. Group with parentheses or split the line],\n  [`E-IR-LIMIT`], [a compilation unit has more ops than the tool can carry --- split the module],\n  [`E-IR-LOCALS`], [one op has too many local names --- split the op],\n  [`E-MONO-FIXPOINT`], [generic monomorphisation does not finish (the type keeps growing) --- break a generic that calls itself at a bigger type],\n  [`W-COL0`], [a top-level declaration does not start at column 0 --- the line may have ended up in the previous declaration\'s body],\n  [`W-UNBOUND`], [a named `contract` is declared but no op names it with `satisfies`],\n  [`W-RFC-PENDING`], [a feature whose design is still at the RFC stage was used --- nothing in the program to fix],\n  [`W-CBE-SLOW`], [this op is lowered to the slow path (tagged values) in native code --- `--why-slow` says why, op by op],\n  [`E-GROUP-UNCLOSED`], [an opening `(` is never closed --- write the matching `)`],\n  [`E-TOPLEVEL`], [something that cannot appear at top level is at top level --- statements go inside an op\'s body],\n  [`E-TYPE-DECL`], [a type declaration in the wrong form --- a struct is `struct N … end`, an alias `type N T .` (no `be`)],\n)\n\n#dtable(\n  columns: 2,\n  id: "a2-other-runtime",\n  caption: [At run time --- a mistake in the program],\n  [*code*], [*meaning and repair*],\n  [`E-VM-ERR`], [`ok_value` was taken from a `result` that is an error --- ask `is_ok` first or pass it on with `try`],\n  [`E-VM-DANGLING`], [a borrowed reference was used after the place it borrowed from ended (use after return)],\n  [`E-VM-EXCL`], [a writing borrow of a place overlapped another borrow --- end the first borrow before taking the next],\n  [`E-VM-READONLY`], [wrote through a read-only reference (`ref`) --- borrow with `mut_ref` to write],\n  [`E-VM-FIELD`], [read a field that the record or register block does not have],\n  [`E-VM-AWAIT`], [`await` was given something that is not a job handle --- handles come from `spawn <op>`],\n  [`E-VM-HANDLE-ZERO`], [waited on an empty handle (0), put into / took from a channel that is 0, or sent to an actor never spawned (0) --- give it what `spawn <op>` / `channel <T>` / `spawn actor <name>` gives first],\n  [`E-VM-CHAN-EMPTY` · `E-VM-CHAN-FULL`], [received from an empty channel or sent to a full one with no scheduler to wait --- a wait that can never end],\n  [`E-VM-SCHED-LOOP`], [handlers keep sending each other messages, so the scheduler cannot settle],\n  [`E-VM-FD` · `E-VM-FMODE` · `E-VM-FWHENCE`], [a file number that cannot be written (only 1 · 2) · an unknown open mode (0 · 1 · 2) · an unknown `seek` origin (0 · 1 · 2)],\n  [`E-VM-FHANDLE` · `E-VM-SHANDLE` · `E-VM-REACTOR`], [the value passed is not an open file / socket / reactor handle],\n  [`E-VM-EXTERN`], [an `extern` op whose body is in C was called in the VM --- build natively to run it],\n  [`E-VM-MMIO` · `E-VM-RESERVE`], [a register block at an absolute address / storage handed over by the linker was opened in the VM --- they mean something only on the real machine],\n)\n\n#dtable(\n  columns: 2,\n  id: "a2-other-limits",\n  caption: [At run time --- a VM limit (not a defect of the program)],\n  [*code*], [*what filled up*],\n  [`E-VM-STACK` · `E-VM-DEPTH` · `E-VM-OOM`], [the operand stack · the call depth · memory to grow the call stack --- recursion is too deep],\n  [`E-VM-BOXPOOL` · `E-VM-RECPOOL` · `E-VM-STKPOOL` · `E-VM-BSETPOOL`], [the pools for values wrapped by `some`·`ok` · struct values · stack containers · bitsets --- keep fewer values alive at once],\n  [`E-VM-CHAN` · `E-VM-MBOX`], [64 channels · 4096 async mailbox slots --- they are not reused, so one run makes at most that many],\n  [`E-VM-FILEPOOL` · `E-VM-SOCKPOOL`], [how many files · sockets can be open at once --- close the ones you are done with],\n  [`E-VM-CHAIN-LIMIT`], [VM activations nested inside VM activations went too deep],\n  [`E-VM-BUDGET`], [the step budget of a cross-checking (oracle) run ran out --- it does not happen in a normal `--run`],\n)\n'}

RUNTIME = [
 ("E-VM-OVERFLOW", "numbers", "정수가 선언된 폭에서 넘쳤다", "integer overflow at the declared width"),
 ("E-VM-DIV0", "numbers", "0 으로 나누었다", "division by zero"),
 ("E-VM-CAST", "numbers", "좁히기·`cast` 의 값이 목표 타입에 들어가지 않는다", "a narrowing or `cast` value does not fit"),
 ("E-VM-SHIFT", "numbers", "옮기는 칸 수가 폭 이상이다", "shift amount not smaller than the width"),
 ("E-VM-BOUNDS", "slices", "색인이 범위 밖이다", "index out of bounds"),
 ("E-VM-NONE", "option-result", "없는 값을 꺼냈다", "took the value out of `none`"),
 ("E-VM-CONTRACT", "contracts", "계약(`requires`·`ensures`·`errors`·범위)이 깨졌다", "a contract was broken"),
 ("E-VM-PANIC", "control", "`panic` 을 불렀다", "the program called `panic`"),
 ("E-VM-ANALYSIS", "proofs-numbers", "지운 검사의 자리가 실제로 범위 밖이다(컴파일러 결함)", "an eliminated check was actually needed (compiler bug)"),
 ("E-TEST-FAIL", "build-test", "시험의 `expect` 가 거짓이다", "an `expect` in a test is false"),
]

def main():
    import chapters  # noqa: F401  (registry reader used by the other checks)
    reg = (ROOT / "typst-ko" / "registry.typ").read_text(encoding="utf-8")
    ids = re.findall(r'"([a-z0-9-]+)"', reg.split("#let chapter-ids")[0].split("#let parts")[1])
    ids = [i for i in ids if not i.startswith("part")]
    by_code = {}
    for f in sorted((ROOT / "examples").glob("ch*/*.low")):
        m = re.search(r"^rem expect: *([A-Z0-9-]+)", f.read_text(encoding="utf-8"), re.M)
        if not m:
            continue
        n = int(f.parent.name[2:])
        by_code.setdefault(m.group(1), [])
        cid = ids[n - 1]
        if cid not in by_code[m.group(1)]:
            by_code[m.group(1)].append(cid)
    missing = [c for c in by_code if c not in MEANING]
    if missing:
        print("gen-diag-appendix: 뜻이 없는 코드:", " ".join(missing), file=sys.stderr)
        return 1
    for lang, dirn, title, lead, head, rt in (
        ("ko", "typst-ko", "부록 B --- 진단 찾아보기",
         LEAD["ko"],
         ("*코드*", "*뜻*", "*보이는 곳*"), "== 실행 중에 멈출 때"),
        ("en", "typst-en", "Appendix B --- Diagnostics index",
         LEAD["en"],
         ("*Code*", "*Meaning*", "*Shown in*"), "== When execution stops")):
        imp = '#import "../lib.typ": *' if lang == "ko" else '#import "../../typst-ko/lib.typ": *'
        out = [imp, "", f"= {title}", "", lead, "",
               "== " + ("번역할 때" if lang == "ko" else "At translation time"), "",
               "#dtable(", "  columns: 3,", '  id: "a2-static",',
               "  caption: [" + ("예제가 보이는 진단" if lang == "ko" else "Diagnostics shown by the examples") + "],",
               f"  [{head[0]}], [{head[1]}], [{head[2]}],"]
        for code in sorted(by_code):
            mean = MEANING[code][0 if lang == "ko" else 1]
            refs = ", ".join(f'#chref("{c}")' for c in by_code[code])
            out.append(f"  [`{code}`], [{mean}], [{refs}],")
        out += [")", "", rt, "", "#dtable(", "  columns: 3,", '  id: "a2-runtime",',
                "  caption: [" + ("실행 중의 진단" if lang == "ko" else "Run-time diagnostics") + "],",
                f"  [{head[0]}], [{head[1]}], [{head[2]}],"]
        for code, cid, ko, en in RUNTIME:
            out.append(f'  [`{code}`], [{ko if lang == "ko" else en}], [#chref("{cid}")],')
        out += [")", "", OTHER[lang]]
        (ROOT / dirn / "appendix" / "a2-diagnostics.typ").write_text("\n".join(out), encoding="utf-8")
    print(f"gen-diag-appendix: 진단 {len(by_code)} · 실행 중 {len(RUNTIME)}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
