# 3. `fn` / `proc` — 프로그램의 기본 단위

[← 목차](README.md) · [← 2. 문법의 기본](02-syntax-basics.md)

프로그램의 기본 단위는 **`fn`**(함수)과 **`proc`**(프로시저) 둘이며, 순수/비순수는 언제나
**명시**한다. 이 둘을 묶는 개념이 **`op`**(operation, 연산)이다 — `fn` 과 `proc` 은 실체이고,
`op` 은 그 둘을 아우르는 상위 개념이다(옛 이름은 `calcop`/`procop`). `op` 은 이제 표면 키워드가
아니며, `add`·`len` 같은 프렐류드 연산도 op(내장 op)이다.

- **`fn`** — **순수**하다(`effects none`). 호출자가 그 순수성에 기댈 수 있다. 순수성은
  *관측적*이다: op 안에 갇힌 지역 변이는 순수로 치지만, `mut` 매개변수를 통해 호출자에게
  보이는 쓰기는 `proc` 이어야 한다. `fn` 에 `effects io` 를 붙이는 것은 구조적으로
  거절된다.
  ★ 그래서 **`fn` 에는 `effects` 절을 적지 않는다.** `effects none .` 을 적으면
  처리기가 `E-EFFECT-REDUNDANT` 로 **거절한다**(2026-08-26). 뜻이 틀려서가 아니라
  **한 뜻에 한 표기**이기 때문이다 — `fn` 이 이미 말한 것을 되풀이하는 자리다.
  고치는 법은 하나다: 그 절을 지운다(`R-DROP-EFFECT`).
- **`proc`** — 효과·지역 상태·반복·스택을 쓴다. 여기서는 `effects` 절이 **좁힌다** —
  절이 없으면 좁히지 않은 것이므로, `proc` 에는 적는 편이 낫다.

앞에 수식어를 붙일 수 있다: `export`(C 에서 부를 수 있게 내보냄), `unsafe`, `extern`(구현이 C
쪽에 있음).

기본 꼴:

```
[수식어] fn|proc <이름>
  input <p> <타입> .        rem 매개변수마다 한 절 — 권한·영역 입력(`cap …`·`region …`)이 데이터보다 먼저
  output <타입> .
  effects <효과들> .           rem 효과 — `proc` 만. `fn` 에는 안 적는다
  [requires/ensures/errors …]  rem 계약
do
  … 본문 …
end
```

**절은 한 차례로만 적는다**(2026-09-13, 정본 §6.4.1 (3a)). 앞에서 뒤로:
`satisfies`·`lowdoc` · `vector`·`priority` · comptime 입력 · 권한·영역 입력 · `using` · 데이터 입력 ·
`output` · `link`·`variadic` · `effects` · `asm` · `access`·`parallel`·`reduce` · `requires` · `ensures` ·
`errors` · `tests`. 어기면 `E-CLAUSE-ORDER` 로 거절되고, 무엇이 무엇 뒤에 왔는지 말해 준다.
`--fmt` 가 입력 아닌 절은 옮겨 준다 — 입력끼리는 옮기지 않는다. 입력의 차례는 부르는 쪽 **인자의
차례**이기도 해서, 옮기면 부르는 자리도 함께 고쳐야 하기 때문이다(그래서 권한은 언제나 먼저 건넨다).

지역과 반환:

- `let <이름> <타입> be <식> .` — **불변** 지역([4장](04-variables.md)).
- `var <이름> <타입> be <식> .` — **가변** 지역. `set <이름> <식> .` 으로 다시 넣는다.
- `return <식> .` — 결과를 돌려준다.

세 가지 실제 op:

```lowent
rem 순수 계산 — 인자 셋을 더한다. 수식은 중위(expr)로도, 접두로도 쓸 수 있다.
fn add3 input a i32 . input b i32 . input c i32 . output i32 . do
  return add (add a b) c .
end
```

```lowent
rem 지역 가변 상태를 쓰는 proc
proc running_total input xs slice u64 . output u64 . effects none . do
  var total u64 be 0 .
  for x xs do                 rem for <이름> <열> do … end — `in` 같은 표지는 없다
    set total (add total x) .
  end
  return total .
end
```

```lowent
rem 구현이 C 에 있는 extern op — 경계는 검사받는다(--emit-h)
unsafe extern proc c_area input k cap c . input p pt . output i64 .
  link lw_c_area . effects unsafe . end
```

## 매개변수

`input <이름> <타입> .` 한 절이 매개변수 하나다. 여럿이면 절을 되풀이한다. 타입 앞뒤에
`mut`·`ref`·`owned`·`option` 등이 붙어 소유·가변·유무를 표현한다.

```lowent
fn compare input a i32 . input b i32 . output i32 . do
  return sub a b .
end
```

```lowent
input s mut slice u64 .      rem 호출자에게 보이는 가변 슬라이스 → 이 op 은 proc 이어야 한다
input given option slice u8 . rem 있을 수도 없을 수도 있는 값
```

## 계약 — `requires` / `ensures` / `errors`

op 의 시그니처에 **약속**을 적는다. 어기면 잡힌다.

- `requires <조건> .` — **진입 전제**(호출자가 지켜야 할 것).
- `ensures <조건> .` — **반환 후 보장**. 결과는 `ret` 로 가리킨다.
- `errors <이름> .` — 이 op 이 낼 수 있는 오류 사례(`result` 반환과 함께).

```lowent
fn clamped input a u8 . output u8 .
  requires le a 200 .        rem 호출자는 a ≤ 200 을 지켜야 한다
  ensures  le ret 200 .      rem 우리는 결과 ≤ 200 을 보장한다
do
  return a .
end
```

상수 인자면 계약은 **컴파일 때** 검사된다. 예컨대 `clamped 250` 은 `--check` 에서 거절되고
(`E-CONTRACT-IMPOSSIBLE`), 그래도 실행하면 진입에서 멈춘다(`E-VM-CONTRACT: requires violated at entry`). `requires static …`(정적으로 증명 가능해야
함), `requires assume …`(증명 없이 가정) 변형도 있다.

## `cap` — 능력

Lowent 에는 **주변 권한이 없다.** 무언가를 할 힘(출력, 할당, 파일 읽기, 인자 접근 …)은
**값처럼 매개변수로 건네받아야** 한다. 꼴은 `input <이름> cap <종류> .`

| `cap` | 무엇을 여는가 |
|---|---|
| `cap io` | 입출력 — `write_out`/표준입력 |
| `cap args` | 프로그램 인자 — `arg a 0` |
| `cap env` | 환경 변수 |
| `cap allocator` | **고정 창**에서 깎아 쓰는 할당(자라지 않는다 — 운영체제 없는 기계에서도 된다) |
| `cap heap` | **자라는 뿌리**(힙)에서 받는 할당 — 운영체제가 있는 기계에서만 |
| `cap file_system` | 파일 시스템 |
| `cap net` | 소켓 |
| `cap clock` | 시계(시각·마감) |
| `cap random` | 운영체제 엔트로피 |
| `cap tty` | 터미널(raw 모드·키 읽기) |
| `cap atomic` | 원자적 연산 |
| `cap mmio` | 메모리 사상 I/O 레지스터 |
| `cap machine` | 인라인 어셈블리 |
| `cap c` | C FFI 경계 |

```lowent
proc main input out cap io . input a cap args . output u8 . effects io . do
  rem arg 는 있을 수도 없을 수도 있으니 option 이다 — guard 로 갈라 받는다.
  let who option slice u8 be arg a 0 .       rem args 능력이 있어야 인자를 본다
  guard is_some who . else return narrow u8 (write_out out 1 "no name\n") .
  return narrow u8 (write_out out 1 (some_value who)) .  rem io 능력이 있어야 출력한다
end
```

능력이 없으면 그 힘을 쓸 수 없다 — 검사기가 `E-CAP-…` 로 막는다. 받아 두기만 하고 쓰는 자리에
건네지 않아도 거절된다(`E-CAP-MISSING`) — `write_out out 1 …` 처럼 **첫 피연산자로 적는다.**

## `effect` — 효과

op 이 세상에 어떤 자국을 남기는지 `effects <효과들> .` 로 **선언**한다. 효과 어휘는 **닫혀
있다**(오타를 조용히 `none` 으로 처리하지 않고 거절한다):

```
none  alloc  heap  io  wait  lock  atomic  unsafe  device
page_fault  blocking  cancel  detach  panic  state  concurrent
```

이 중 일부(`io concurrent alloc heap state panic unsafe atomic`)는 강제하는 실물 기본연산이 이미
있고, 나머지는 선언으로 받아들이되 아직 기계적으로 강제하지는 않는다.

효과는 **호출을 따라 전파된다.** 순수한 `fn`(효과 `none`)이 `io` 를 내는 `proc` 을
부르면 거절된다(`E-EFFECT-CALC` / `E-EFFECT`). 이 규칙 덕에, 시그니처만 봐도 그 op 이
**무엇을 할 수 있고 무엇을 못 하는지**가 드러난다 — 저엔트로피 언어가 노리는 바로 그 성질이다.

## 내장 op — 그리고 `neg` 하나만 단항이다

`add`·`sub`·`mul`·`div`·`mod` 는 **전위 표기**이고 인자를 **둘** 받는다. 산술에서 인자를
하나만 받는 것은 **`neg`** 뿐이다.

```lowent
return add a b .          rem 둘
return neg a .            rem 하나 — 부호를 뒤집는다
return abs (neg x) .
```

`expr` 안에서는 같은 것을 중위로 쓸 수 있다 — `expr a + b` 는 `add a b` 와 **같은 것**이다.
다른 표기이지 다른 연산이 아니다.

⚠ `neg` 는 **부호 있는 타입**의 일이다. 무부호 값에 쓰면 넘침이고, 그 자리는 검사가 잡는다.

---

[← 목차](README.md) · [다음: 4. 지역 변수 →](04-variables.md)
