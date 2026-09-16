# 부록 B — 진단 찾아보기

이 책의 예제가 실제로 일으킨 진단만 모았다. 코드는 판이 바뀌어도 같은 뜻을 지킨다. 진단의 긴 영문 설명과 고치는 법은 컴파일러가 그 자리에서 말한다.

## <a id="sx1"></a>번역할 때

| **코드** | **뜻** | **보이는 곳** |
|---|---|---|
| `E-ACCESS-MODE` | `access` 로 적은 읽기·쓰기 약속을 몸이 어긴다 | 27장 |
| `E-ACTOR-FIELD` | 액터의 상태 칸을 바깥에서 읽는다 | 25장 |
| `E-ACTOR-STATE-REF` | 액터 상태 칸에 빌림을 둔다 | 25장 |
| `E-ALLOC-AMBIGUOUS` | 맞는 할당기가 둘 이상인데 `using` 으로 고르지 않았다 | 20장 |
| `E-ALLOC-NESTED` | 같은 뿌리의 안쪽 영역이 열린 채 바깥 출처로 깎았다 | 18장 |
| `E-ALLOC-NOCAP` | `alloc` 을 적었는데 할당 권한을 받지 않았다 | 16장 |
| `E-ALLOC-NOSOURCE` | 할당기를 쓰는 호출인데 이 op 안에 맞는 할당기가 하나도 없다 | 20장 |
| `E-ALLOC-OUTLIVES` | 영역의 바이트를 영역 밖에서 태어난 액터에게 건넨다 | 18장 |
| `E-ALLOC-SHARED` | 원자적이지 않은 할당기를 태스크에 건넨다 | 26장 |
| `E-ALLOC-TASK` | 뿌리에서 깎는 op 을 태스크로 띄운다 | 26장 |
| `E-ALLOC-USING-UNUSED` | 할당기를 쓰지 않는 호출에 `using` 을 적었다 | 20장, 34장 |
| `E-ASM-TARGET-UNKNOWN` | `asm` 절의 기계 이름이 대상 목록에 없다 | 30장 |
| `E-ASM-UNBOUND` | 어셈블리 템플릿이 선언하지 않은 피연산자를 부른다 | 30장 |
| `E-ATOMIC-NOCAP` | `atomic` 을 적었는데 `cap atomic` 을 받지 않았다 | 27장 |
| `E-ATOMIC-ORDER` | 그 원자 연산에 뜻이 없는 기억 차례다 | 27장 |
| `E-BLOCK-UNCLOSED` | `do` 로 연 블록을 `end` 로 닫지 않았다 | 2장 |
| `E-BOUND-UNSAT` | 타입 인자가 요구한 트레이트를 갖추지 못했다 | 22장, 23장 |
| `E-BRAND-REUSED` | 같은 브랜드로 저장소를 두 번 열었다 | 35장 |
| `E-CAP-FORGE` | 권한 칸을 가진 액터를 그 권한 없이 띄운다 | 16장, 20장, 25장 |
| `E-CAP-KIND` | 다른 종류의 권한을 건넸다 | 16장 |
| `E-CAP-MISSING` | 권한이 필요한 내장 연산에 권한을 첫 피연산자로 적지 않았다 | 16장 |
| `E-CHAR` | 이 언어에 없는 글자(기호)다 | 6장, 7장, 8장, 9장 |
| `E-CHAR-WIDTH` | 글자 리터럴이 글자 하나가 아니다 | 3장 |
| `E-CLAUSE-ORDER` | op 머리의 절이 정해진 차례를 어겼다 | 2장, 3장, 22장 |
| `E-COMPTIME-ARG` | `comptime` 자리에 실행 값을 주었다 | 22장 |
| `E-CONC-ALONE` | 묶음이 흐름 하나만 만들고 그 흐름이 짝을 기다린다 | 26장 |
| `E-CONC-DEADLOCK` | 묶인 흐름이 모두 받기만 하고 아무도 보내지 않는다 | 26장 |
| `E-CONFIG-TYPE` | 구성이 손잡이의 선택지에 없는 값을 준다 | 31장 |
| `E-CONFIG-UNDEF` | 선언하지 않은 빌드 손잡이를 구성이나 `config` 가 부른다 | 31장 |
| `E-CONTRACT-DEAD` | `requires` 가 이미 배제한 오류를 선언했다 | 14장 |
| `E-CONTRACT-IMPOSSIBLE` | 상수끼리의 호출이 상대의 `requires` 를 어긴다 | 14장, 35장 |
| `E-DEP-MISSING` | `use … from` 의 자리에서 파일을 읽을 수 없다 | 21장 |
| `E-EFFECT` | 선언하지 않은 효과를 낸다 | 15장, 20장, 26장 |
| `E-EFFECT-CALC` | 순수한 `fn` 이 효과를 낸다 | 1장, 5장, 15장, 16장, 18장, 24장 |
| `E-EFFECT-DUP` | 효과 줄에 같은 원자를 두 번 적었다 | 15장, 44장 |
| `E-EFFECT-NO-CAP` | `io` 따위를 적었는데 허락하는 권한이 없다 | 2장, 16장 |
| `E-EFFECT-NONE-MIX` | `none` 을 다른 효과와 함께 적었다 | 15장 |
| `E-EFFECT-PURITY` | `fn` 이 호출자나 액터가 볼 수 있는 상태에 쓴다 | 5장, 15장, 25장 |
| `E-EFFECT-REDUNDANT` | `fn` 에 `effects none` 을 적었다 | 5장 |
| `E-EFFECT-UNDEF` | 목록에 없는 효과 낱말이다 | 15장, 44장 |
| `E-ENS-UNDEF` | `ensures` 가 없는 이름을 가리킨다 — 돌려주는 값은 `ret` | 14장 |
| `E-ENTRY-PARAMS` | 시작점이 권한 아닌 입력을 받는다 | 16장 |
| `E-ENUM-DOT` | 열거의 갈래를 마침표로 닫지 않았다 | 10장 |
| `E-ENUM-INFINITE` | 갈래가 자기 타입을 값으로 품는다 | 10장 |
| `E-ERR-UNDECLARED` | `errors` 절에 없는 오류를 돌려준다 | 11장 |
| `E-ERRORS-STATE` | `errors` 조건이 액터의 상태 칸을 읽는다 | 25장 |
| `E-ESCAPE` | 지역을 가리키는 참조가 op 밖으로 나간다 | 12장 |
| `E-EXCL` | 같은 값에 대한 빌림이 겹친다 | 12장, 26장, 43장 |
| `E-EXPR-APP` | `expr` 섬 안의 부름을 괄호로 묶지 않았다 | 8장 |
| `E-EXPR-CHAIN` | `expr` 섬에서 비교를 이어 썼다 | 3장 |
| `E-EXPR-UNARY` | `expr` 섬에 단항 연산자를 썼다 | 8장 |
| `E-FFI-NOCAP` | C 를 부르는데 `cap c` 를 받지 않았다 | 29장 |
| `E-FFI-NOEFFECT` | C 를 부르는 op 에 효과 줄이 없다 | 29장 |
| `E-FFI-NOUNSAFE` | C 를 부르는데 `unsafe` 표시가 없다 | 29장 |
| `E-FFI-TYPE` | C ABI 가 표현할 수 없는 타입이 경계를 건넌다 | 29장 |
| `E-FIELD-GLUED` | 값 뒤에 점을 붙여 칸을 읽으려 했다 — `field` 로 적는다 | 3장 |
| `E-FN-CAP` | 되부름으로 넘길 op 이 권한을 요구한다 | 29장 |
| `E-FN-NOTEXPORT` | `unsafe_fn` 이 `export extern` 이 아닌 op 을 가리킨다 | 29장 |
| `E-FOLD-OP` | `pipe` 스테이지가 없는 op 을 부른다 | 24장 |
| `E-GROUP-UNCLOSED` | 괄호가 닫히지 않았다 | 3장 |
| `E-GUARD-FALLTHROUGH` | `guard` 의 `else` 가 떠나지 않는다 | 7장 |
| `E-HEAP-NOHOST` | 운영체제 없는 대상에서 자라는 뿌리를 청한다 | 18장 |
| `E-IF-VALUE` | `if` 를 값으로 썼다 | 6장 |
| `E-IMMUTABLE` | `let` 으로 지은 이름에 `set` 했다 | 6장 |
| `E-IR-ARITY` | op 을 인자 수가 맞지 않게 불렀다 | 5장, 48장 |
| `E-IR-UNDEF` | 그 자리에서 알 수 없는 이름이다 | 6장, 7장, 21장, 22장, 25장 |
| `E-ISR-CALLED` | 인터럽트 처리기를 코드에서 불렀다 | 30장 |
| `E-ISR-EFFECT` | 인터럽트 처리기가 `effects device` 를 적지 않았다 | 30장 |
| `E-ISR-PARAMS` | 인터럽트 처리기가 매개변수를 받는다 | 30장 |
| `E-LET-NOVALUE` | `be` 뒤에 값이 없다 | 6장 |
| `E-LOCK-NOTYET` | 흐름끼리 나누는 자물쇠 타입은 아직 짓지 않았다 | 26장 |
| `E-MATCH-INEXHAUSTIVE` | `match` 가 모든 경우를 덮지 않는다 | 7장, 10장, 11장 |
| `E-MATCH-REDUNDANT` | `match` 의 갈래가 영영 돌지 않는다(`_` 뒤의 갈래·겹친 범위) | 11장 |
| `E-METHOD-UNDEF` | 수신자의 타입에 그 이름의 붙은 op 이 없다 | 22장, 23장 |
| `E-MMIO-BYVALUE` | 레지스터 묶음을 값으로 받는다 | 30장 |
| `E-MMIO-PERM` | 읽기 전용 레지스터에 쓴다 | 30장 |
| `E-MREF-SLICE` | `mut ref slice` 를 썼다 | 12장 |
| `E-NAME-BUILTIN` | 내장 op 의 이름을 선언이나 지역 이름으로 썼다 | 5장, 6장 |
| `E-NAME-DUP` | 한 모듈에 같은 이름을 두 번 선언했다 | 21장 |
| `E-NAME-SHADOW` | 살아 있는 이름을 다시 지었다(가림) | 3장, 6장 |
| `E-OPT-UNUSED` | 선언한 빌드 손잡이를 아무 코드도 읽지 않는다 | 31장 |
| `E-OWN-INCOMPLETE` | 완결이 필요한 값을 끝내지 않고 버린다 | 19장, 28장, 36장 |
| `E-OWN-JOIN` | 갈래마다 소유 상태가 다르다 | 19장 |
| `E-OWN-MOVED` | 옮긴 값을 다시 쓴다 | 19장, 25장, 28장, 35장, 36장 |
| `E-PAR-ASSOC` | 모으는 연산이 결합적이지 않다 | 27장 |
| `E-PAR-CARRY` | 나누는 되풀이가 걸음을 넘어 사는 자리에 쓴다 | 27장 |
| `E-PAR-FLOAT` | 부동소수 누적을 나누어 모으려 했다 | 27장 |
| `E-PAR-IDENTITY` | `reduce` 의 시작값이 그 연산의 항등원이 아니다 | 27장 |
| `E-PAR-NOLOOP` | `parallel` 절이 나눌 되풀이를 찾지 못했다 | 27장 |
| `E-PAR-READ` | 나누는 되풀이가 남의 원소를 읽는다 | 27장 |
| `E-PAR-WRITE` | 나누는 되풀이가 남의 원소에 쓴다 | 27장, 45장 |
| `E-PIPE-NO-TERMINAL` | 종결자 뒤에 스테이지가 온다 | 24장 |
| `E-PIPE-STAGE` | `pipe` 스테이지 목록에 없는 낱말이다 | 24장 |
| `E-PROFILE-LEVEL` | 빌드 프로파일이 주지 않는 동시성을 쓴다 | 25장 |
| `E-REGION-ESCAPE` | 영역에서 얻은 바이트를 밖으로 들고 나간다 | 18장 |
| `E-RETURN-PARTIAL` | 어떤 길에서 값을 돌려주지 않는다 | 3장, 5장, 7장 |
| `E-SPAWN-SCOPE` | `task_group` 밖에서 흐름을 만든다 | 26장 |
| `E-TIER-EFFECT` | 선언한 기계 등급이 감당하지 못하는 효과다 | 30장 |
| `E-TOPLEVEL` | 최상위에 올 수 없는 것이 최상위에 있다 | 3장 |
| `E-TRAIT-EFFECT` | 갖춘 op 이 서명보다 많은 효과를 가진다 | 23장 |
| `E-TRAIT-MISSING` | 트레이트의 op 하나가 없다 | 23장 |
| `E-TRAIT-SIG` | 서명에 `fn`·`proc` 을 적었거나 매개변수 수가 다르다 | 23장 |
| `E-TYPE-ARGMUT` | 읽기만 되는 값을 고치는 자리에 넘긴다(`let` 을 `mut_ref` 로) | 12장 |
| `E-TYPE-ARRAY` | `array` 의 길이를 타입 뒤에 적었다 | 9장 |
| `E-TYPE-BITCAST` | `bit_cast` 의 목표가 모든 비트열이 값인 타입이 아니다 | 20장 |
| `E-TYPE-COND` | 조건 자리에 참거짓이 아닌 값이 왔다 | 4장 |
| `E-TYPE-DECL` | 타입 선언에 `be` 를 끼웠다 | 13장 |
| `E-TYPE-FIELD` | 구조체를 만들 때 칸이 빠졌거나 없는 칸을 적었다 | 10장 |
| `E-TYPE-INSTANCE` | 같은 제네릭의 서로 다른 인스턴스를 섞었다 | 35장 |
| `E-TYPE-KIND` | 그 연산에 쓸 수 없는 갈래의 타입이다(예: `bool` 을 `cast`) | 13장 |
| `E-TYPE-LOGICAL` | 논리 연산에 참거짓이 아닌 값을 주었다 | 8장 |
| `E-TYPE-MUT` | `mut` 이 아닌 슬라이스의 원소에 쓴다 | 9장 |
| `E-TYPE-NOMINAL` | 표현이 같아도 이름이 다른 타입을 섞었다 | 13장 |
| `E-TYPE-REF` | 읽기 참조로 쓴다 | 12장 |
| `E-TYPE-RETURN` | 돌려주는 값이 op 의 출력 타입과 다르다 | 11장, 31장 |
| `E-TYPE-SIGN` | 값을 지키는 넓히기가 없는 부호 섞기다 | 4장, 39장, 40장 |
| `E-TYPE-WIDTH` | 값이 선언된 타입에 들어가지 않는다 | 3장, 13장 |
| `E-UNSAFE-UNDECLARED` | `unsafe` 효과를 적었는데 op 에 `unsafe` 표시가 없다 | 29장 |
| `E-VISIBILITY` | 다른 모듈의 감춘 이름에 닿는다 | 21장 |
| `E-VOCAB-REMOVED` | 없앤 낱말이나 철자다 | 3장, 5장, 7장, 15장, 48장 |
| `E-WIDEN-SIGN` | 부호 있는 수를 부호 없는 타입으로 `widen` 했다 | 13장 |
| `W-EFFECT-OVER` | 선언만 하고 내지 않는 효과다(경고) | 15장, 16장 |
| `W-NOT-YET` | 이름은 받지만 아직 뜻이 없는 낱말을 썼다 | 13장 |
| `W-USE-EXTERNAL` | 번역 단위에 없는 모듈을 들여온다(경고) | 32장 |

*표 50.1 — 예제가 보이는 진단*

## <a id="sx2"></a>실행 중에 멈출 때

| **코드** | **뜻** | **보이는 곳** |
|---|---|---|
| `E-VM-OVERFLOW` | 정수가 선언된 폭에서 넘쳤다 | 4장 |
| `E-VM-DIV0` | 0 으로 나누었다 | 4장 |
| `E-VM-CAST` | 좁히기·`cast` 의 값이 목표 타입에 들어가지 않는다 | 4장 |
| `E-VM-SHIFT` | 옮기는 칸 수가 폭 이상이다 | 4장 |
| `E-VM-BOUNDS` | 색인이 범위 밖이다 | 9장 |
| `E-VM-NONE` | 없는 값을 꺼냈다 | 11장 |
| `E-VM-CONTRACT` | 계약(`requires`·`ensures`·`errors`·범위)이 깨졌다 | 14장 |
| `E-VM-PANIC` | `panic` 을 불렀다 | 7장 |
| `E-VM-ANALYSIS` | 지운 검사의 자리가 실제로 범위 밖이다(컴파일러 결함) | 40장 |
| `E-TEST-FAIL` | 시험의 `expect` 가 거짓이다 | 31장 |

*표 50.2 — 실행 중의 진단*

---

[← 이전](sec53.md) · [목차로](README.md) · [다음 →](sec55.md)
