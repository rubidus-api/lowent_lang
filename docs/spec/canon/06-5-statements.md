## 6.5 문장과 제어 (Statements and control flow)

(1) ⟦문장|statement⟧ 은 실행되는 것이다. 식은 값을 만들고, 문장은 일을 한다.

## 6.5.1 이름을 짓는 문장

(1) `let` 은 **바뀌지 않는** 이름을 짓는다. 한 번 정해진 값은 바뀌지 아니한다.

(2) `var` 는 **바뀔 수 있는** 이름을 짓는다. `set` 으로 값을 바꾼다.

(3) 이름을 지을 때 타입을 함께 적을 수 있다. 적으면 그 타입이 되고, 값이 그 타입에 맞지
      않으면 번역이 거부된다.

(3a) 타입을 **생략할 수 있다.** 생략하면 값이 타입을 정한다. 값에서 타입을 정할 수 없으면
      번역이 거부되며, 그때는 적어야 한다.

(4) `be` 뒤에는 **값이 있어야 한다.** 값 없이 닫으면 번역이 거부된다(`E-LET-NOVALUE`).
      이름을 짓되 값을 나중에 주는 길은 이 언어에 없다.

> [!주의]
> 이 규칙이 잡는 것은 빈칸을 적는 실수만이 아니다. `let x f64 be .5 .` 이라고 적으면
> `.5` 는 부동소수 리터럴이 아니므로(⟦§6.1.4⟧ (6)) 그 점이 폼을 닫고, `be` 뒤에는
> **아무것도 남지 않는다.** 사람은 값을 적었다고 믿는데 처리기는 값을 못 본 자리이며,
> 그래서 진단이 그 함정을 이름으로 짚는다.

> [!참고]
> 기본이 `let` 인 것이 중요하다. 바뀌지 않는 이름은 읽는 사람이 **한 번만 확인하면**
> 되지만, 바뀔 수 있는 이름은 쓰이는 자리마다 *"여기서는 무슨 값이지"* 를 다시 물어야
> 한다. 그 물음이 곧 의미 엔트로피다(⟦§1.3⟧).

```lowent-거부: `be` 뒤에 값이 없다 · E-LET-NOVALUE
module ex_let_novalue .

fn f output u8 .
do
  let a u8 be .        rem 조용히 0 을 넣지 아니한다
  return a .
end
```

## 6.5.2 조건 — `if`

(1) `if` 는 조건이 참일 때 블록을 실행한다. 조건은 `bool` 이어야 한다(⟦§6.2.3⟧).

(2) `else` 로 거짓일 때의 블록을 적을 수 있다.

(3) 블록은 `do` 로 열고 `end` 로 닫는다.

(4) `if` 는 **문**이다. 값을 내는 식으로 쓸 수 없다(`E-IF-VALUE`) — 갈래마다 값을 정하려면 각 갈래에서 이름에
      `set` 하거나 `return` 한다.

```lowent-거부: `if` 는 값을 내지 아니한다 · E-IF-VALUE
module ex_if_value .

fn pick input a u64 . output u64 .
do
  let x u64 be if gt a 1 . 5 else 6 .   rem 갈래마다 set 하거나 return 한다
  return x .
end
```

## 6.5.3 되풀이 — `while`

(1) `while` 은 조건이 참인 동안 블록을 되풀이한다.

(2) `break` 는 되풀이를 벗어나고, `continue` 는 다음 바퀴로 넘어간다.

(3) 되풀이의 조건도 `bool` 이어야 한다.

```lowent 예제: 조건과 되풀이
module ex_ctl .

export fn count_big input n u32 . output u32 .
  requires le n 100 .
do
  var total u32 be 0 .
  var i u32 be 0 .
  while lt i n . do
    if gt i 5 . do
      set total (add total 1) .
    end
    set i (add i 1) .
  end
  return total .
end
```

(4) `for` 는 **슬라이스의 원소를 차례로** 훑는다 — `for <이름> <슬라이스> do … end`.
      이름은 원소 하나를 가리키며, 그 타입은 **슬라이스의 원소 타입**이다. 훑는 대상이
      슬라이스가 아니면 번역이 거부된다(`E-TYPE-ITER`).

(5) `for` 의 이름은 **블록 안에서만** 산다. 블록이 끝나면 그 이름은 없다.

(6) `break` 와 `continue` 는 `for` 안에서도 `while` 에서와 같이 쓴다.

```lowent 예제: 슬라이스를 훑기 · 결과: 원소를 모두 더한다
module ex_for .

export fn total_of input xs slice u8 . output u64 .
do
  var acc u64 be 0 .
  for x xs do
    set acc (add acc (widen u64 x)) .
  end .
  return acc .
end .
```

> [!주의]
> `in` 은 이 언어의 낱말이 **아니다**. `for x in xs` 라고 적으면 거부된다
> (`E-VOCAB-REMOVED`). 훑을 대상은 이름 바로 뒤에 온다.

## 6.5.4 빠져나가는 조건 — `guard`

(1) `guard` 는 조건이 참이 아니면 **그 자리에서 빠져나간다**. `else` 뒤에 오는 것은
      **모든 길이 빠져나가야** 한다.

(1a) `else` 뒤에는 한 문장이 올 수도 있고 블록이 올 수도 있다. 블록이면 그 블록의 **모든
      길**이 빠져나가야 한다 — 빠져나가는 문장은 `return`, `break`, `continue`, `panic` 이다.

(2) `else` 의 어떤 길이 빠져나가지 않고 아래로 이어지면 번역이 거부된다.

(3) `guard` 를 지나면 그 조건은 **참임이 보장된다.** 처리기는 그 뒤의 코드에서 그 사실을
      쓴다.

```lowent-거부: `else` 가 빠져나가지 않는다 · E-GUARD-FALLTHROUGH
module ex_guard_bad .

proc p input n u32 . output u32 . effects none .
do
  guard le n 5 . else set n 0 .   rem 빠져나가지 않고 아래로 이어진다
  return n .
end
```

> [!주의] `guard` 는 `if not` 의 다른 이름이 아니다
> `guard` 가 하는 말은 *"이 조건이 아니면 여기서 끝"* 이다. 그래서 `guard` 아래의 코드는
> 조건이 참인 세계에서만 산다. `else` 가 빠져나가지 않으면 그 약속이 깨지고, **아래
> 코드가 참이라고 잘못 믿게 된다** — 그래서 언어가 거부한다.

```lowent 예제: guard
module ex_guard .

export fn safe_head input data slice u8 . . output u8 .
do
  guard ge (len data) 1 . else return 0 .
  return index data 0 .
end
```

```lowent 예제: 타입은 생략할 수 있고, `guard else` 는 블록이어도 된다 · 결과: inferred() = 7 · guarded(3) = 1 · guarded(9) = 9
module ex_infer_guard .

rem 타입을 적지 않아도 된다 — 값이 정한다.
fn inferred output u64 .
do
  let a be 7 .
  return a .
end

rem `else` 가 블록이어도 된다. 규칙은 "모든 길이 빠져나가는가" 다.
fn guarded input n u8 . output u8 .
do
  guard gt n 5 . else do
    let x u8 be 1 .
    return x .
  end
  return 9 .
end
```

```lowent-거부: `else` 의 길이 빠져나가지 않으면 거부된다 · E-GUARD-FALLTHROUGH
module ex_guard_fall .

fn f input n u8 . output u8 .
do
  guard gt n 5 . else do
    let x u8 be 1 .
  end
  return 9 .
end
```

## 6.5.5 돌아가기 — `return`

(1) `return` 은 op 을 끝내고 값을 돌려준다. 돌려주는 값의 타입은 `output` 에 적은 것과
      같아야 한다.

(2) 값을 돌려주지 않는 op(`output void`)은 `return` 만 적어 일찍 끝낼 수 있다.

(2a) 값을 돌려주지 않는 op 은 `return` 없이 **몸의 끝까지 진행해도** 된다. 돌려줄 값이
      없으므로 끝나는 자리가 곧 돌아가는 자리다.

(3) **값을 돌려주는 op** 은 모든 길이 값을 돌려주고 끝나야 한다. 값 없이 끝나는 길이
      있으면 적합하지 아니하다.

(4) 다음 문장은 값을 돌려주고 끝나는 것으로 본다 — `return`, 두 갈래가 모두 값을 돌려주는
      `if`⋯`else`, 그리고 **모든 갈래가 값을 돌려주는** `match`. `match` 가 모든 경우를
      덮는지는 따로 검사된다(⟦§6.6⟧).

(5) `guard`·`while`·`for` 는 빠져나가는 길이 있으므로 그 자체로는 값을 돌려주는 것으로
      보지 아니한다.

(6) 몸이 기계 명령인 op(⟦§6.9⟧)은 값을 `out reg` 로 내므로 이 조항의 대상이
      아니다.

> [!산문]
> 이 규칙이 없으면 값 없이 끝나는 길에서 처리기가 **0 을 대신 넣게 된다.** 그 0 은
> 소스 어디에도 없으므로, 프로그램을 다 읽고도 결과를 알 수 없다(⟦§1.3⟧).
> 그래서 이 언어는 그런 길을 아예 번역하지 않는다.

```lowent 예제: 값을 안 내는 op 은 `return` 없이 끝나도 된다
module ex_void .

proc keep input n u8 . output void . effects none .
do
  let x u8 be n .
end
```

> [!산문]
> 돌려줄 값이 없으므로 **끝나는 자리가 곧 돌아가는 자리**다. 값을 돌려주는 op 이었다면
> 같은 모양이 거부된다(⟦§6.5.5⟧ 문단 3).

## 6.5.6 블록과 들여쓰기

(1) 블록은 `do` 로 열고 `end` 로 닫는다. 들여쓰기는 **뜻이 없다** — 읽는 사람을 위한 것이다.

> [!산문]
> 들여쓰기로 블록을 나누는 언어도 있다. 이 언어가 그러지 않는 이유는, 눈에 안 보이는
> 글자(공백과 탭)가 프로그램의 뜻을 바꾸면 **보이는 것과 뜻이 갈릴** 수 있기 때문이다.
> `do` 와 `end` 는 눈에 보인다.

## 6.5.7 실패를 말하는 세 가지 길

(1) 실패를 말하는 길은 셋뿐이며, 쓰임이 서로 다르다.

> [!표] 세 실패 채널
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*무엇*], [*무엇을 말하나*], [*부르는 쪽이 하는 일*],
> [`result t e`], [고칠 수 있는 실패], [어느 쪽인지 묻고 다룬다. 안 다루면 위로 넘긴다],
> [`option t`], [값이 없음], [있는지 묻고 꺼내거나, 대신 쓸 값을 준다],
> [`panic` 효과], [계약이 깨졌다], [다룰 수 없다. 프로그램이 멈춘다],
> )

(2) `result` 의 오류 타입에 들어가는 변형은 그 op 의 `errors` 절이 적은 것과 **같아야**
      한다. 적지 않은 오류를 돌려주거나, 적어 놓고 안 돌려주는 것은 적합하지 아니하다.

(3) `panic` 은 되돌아 풀리지 아니한다. 멈추는 자리에서 프로그램이 끝난다 — 중간에 잡아
      이어 가는 길은 없다.

```lowent 예제: 세 채널이 각기 다른 모양으로 답한다 · 결과: halve(100) = ok 50 · halve(250) = err too_big · lookup(2) = some 20 · lookup(7) = none · strict(10) = 11 · strict(250) → 트랩
module ex_channels .

enum io_error
  too_big
end

rem ① 고칠 수 있는 실패 — result.
fn halve input a u8 . output result u8 io_error .
  errors too_big gt a 200 .
do
  guard le a 200 . else return error too_big .
  return ok (div a 2) .
end

rem ② 값이 없음 — option.
fn lookup input k u8 . output option u8 . .
do
  guard lt k 3 . else return none .
  return some (mul k 10) .
end

rem ③ 계약이 깨짐 — 부르는 쪽이 약속을 어기면 멈춘다.
fn strict input a u8 . output u8 .
  requires le a 200 .
do
  return add a 1 .
end
```

> [!산문]
> 셋을 가르는 물음은 *"부르는 쪽이 무엇을 할 수 있는가"* 다. 파일이 없는 것은 다른 파일을
> 열어 볼 수 있으니 `result` 다. 찾는 것이 목록에 없는 것은 그냥 없는 것이니 `option`
> 이다. 계약이 깨진 것은 부르는 쪽이 **이미 약속을 어긴 것**이라 고칠 수 있는 일이
> 아니다 — 그래서 멈춘다.

## 6.5.8 실패를 다루기 — `try` 와 소비형

(1) `result` 를 돌려주는 op 을 부를 때, `try` 는 성공하면 값을 꺼내고 실패하면 **그 오류를
      그대로 위로 넘긴다**.

(2) `try` 를 쓰는 op 은 자기도 그 오류를 돌려줄 수 있어야 한다.

(3) `result` 와 `option` 을 소비하는 방법은 다음으로 닫혀 있다 — `try`(꼬리를 붙인 것도
      포함한다, (4)) · 묻는 것(`is_ok` `is_error` `is_some` `is_none`) · 꺼내는 것
      (`ok_value` `error_value` `some_value`) · 대신 쓸 값을 주는 것(`value_or`).

(4) `try` 뒤에 꼬리를 붙여 **채널을 바꿀** 수 있다. 값은 그대로이고 담는 그릇만 바뀐다.

> [!표] 채널 전환
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*모양*], [*무엇에서 무엇으로*], [*무엇을 잃거나 얻나*],
> [`try <식> else_none`], [`result` → `option`], [오류를 **버린다** — 왜 실패했는지 더는 말하지 않는다],
> [`try <식> else_error <갈래>`], [`option` → `result`], [없음에 **이름을 붙인다** — 왜 없는지를 말한다],
> )

(5) 채널 전환은 **타입을 바꾼다.** `try <식> else_none` 을 적은 자리의 타입은 `option`
      이지 그 안의 값이 아니다. 값 타입을 적어 놓고 채널 전환을 쓰면 적합하지 아니하다.

> [!참고]
> 이것이 규범인 까닭은 한때 그렇지 아니하였기 때문이다. 처리기가 꼬리 낱말을 못 보아
> `output u64` 라 적은 op 이 **`some 7` 을 돌려주었고**, 그러고도 `--check` 는 초록이었다.
> 채널이 바뀌는 자리를 규범이 말하지 아니하면, 도구가 그 자리를 잊어도 아무도 모른다.

(6) 꺼내는 것은 **부분 연산**이다(⟦§6.2.8⟧). 없는 쪽을 꺼내면 트랩한다.

(7) `value_or` 의 기본값은 **값이 없을 때만 평가된다**(지연평가). 그러므로 기본값 자리에는
      비싼 계산이나 실패할 수 있는 계산을 적어도 된다 — 값이 있으면 그것은 돌지 아니한다.

> [!참고]
> `value_or (some 7) (div 1 0)` 은 **7** 이다. 값이 있으므로 기본값 쪽은 돌지 않는다.
> 값이 없을 때에만 그 자리가 평가되며, 그때는 트랩이 실제로 일어난다.
> ☞ 2026-08-29 까지는 그렇지 아니하였다 — 도구가 기본값을 **먼저** 평가하여, 값이 있는
> 프로그램이 없어도 될 실패로 끝났다. 정본(⟦§6.2.8⟧ · DECISION-0003 C1)이 지연평가로
> 정해 둔 자리였고, 도구를 정본에 맞추어 고쳤다. 값은 달라지지 아니하며 달라지는 것은
> **어떤 트랩과 효과가 일어날 수 있는가** 이다.

> [!산문]
> 실패를 확인하는 코드를 매번 손으로 쓰면 길어지고, 길어지면 빼먹는다. `try` 는 그
> 되풀이를 한 낱말로 줄이되 **실패가 위로 간다는 사실은 소스에 남긴다** — 조용히
> 무시되는 실패가 없다.

## 6.5.9 멈추기 — `panic`

(1) `panic` 은 프로그램을 즉시 멈춘다(⟦§5.4⟧). 이것은 효과이므로, 쓰는 op 은
      자기 계약에 그 효과를 적어야 한다.

(2) 순수한 op(`fn`)은 `panic` 을 쓸 수 없다. 다만 계약이 깨졌을 때 처리기가 일으키는
      트랩은 이와 별개이며, 그것은 op 이 한 일이 아니다.

## 6.5.10 나누어 도는 되풀이 — `parallel`

(1) 되풀이에 `parallel <조각> split .` 절을 붙이면, 그 되풀이를 **조각을 나누어** 여럿이
      함께 돌 수 있다고 밝히는 것이다.

(2) 처리기는 그 말을 믿지 아니하고 **확인한다.** 확인하는 것은 되풀이의 각 걸음이 서로에게
      기대지 않는다는 것이며, 조건은 셋이다.

> [!표] 나누어 돌 수 있는 되풀이의 조건
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*조건*], [*어기면*], [*무엇이 어긋났는가*],
> [자기 몫만 읽는다], [`E-PAR-READ`], [남의 자리를 읽으면 그 값이 아직 옛것인지 새것인지가 **누가 먼저 도느냐**에 달린다],
> [자기 몫만 쓴다], [`E-PAR-WRITE`], [두 걸음이 같은 자리에 쓰면 남는 값이 차례에 달린다],
> [걸음을 넘어 사는 자리에 쓰지 아니한다], [`E-PAR-CARRY`], [그런 자리는 걸음들을 **묶는다**. 모아야 한다면 `reduce <쌓는 자리> <연산> .` 로 밝힌다],
> )

(3) `parallel` 이 조각을 이름 부르는데 그 조각의 길이로 도는 되풀이가 없으면 거부된다
      (`E-PAR-NOLOOP`) — 나눌 것이 무엇인지 알 수 없기 때문이다.

(4) 모으는 연산은 **묶음의 차례를 바꿔도 답이 같아야** 한다. 그렇지 않은 연산은 거부된다
      (`E-PAR-ASSOC`). 부동소수의 덧셈은 그렇지 않으므로 따로 막는다(`E-PAR-FLOAT`).

> [!산문]
> 나누어 도는 것을 **밝히는 말**로만 두면, 그 말이 틀렸을 때 프로그램은 대개 잘 돌다가
> 어느 날 다르게 돈다. 그래서 이 언어는 밝힌 말을 확인한다 — 확인할 수 없으면 나누지
> 않는 것이 아니라 **번역을 거절한다.** 「빠르지만 가끔 틀림」은 이 언어가 파는 물건이
> 아니다.

> [!주의]
> 나누어 돌지 **않아도 답은 같다.** 위 조건은 나눌 수 있음의 조건이지 뜻을 바꾸는 절이
> 아니다. 그러므로 나누어 돈 답과 하나씩 돈 답은 언제나 같다 — `E-PAR-FLOAT` 이 있는
> 까닭이 바로 그것이다.

## 6.5.11 몸이 갖추어야 하는 것

(1) 값을 내놓겠다고 적은 op 은 **모든 길에서** 값을 내놓아야 한다(`E-RETURN-PARTIAL`).
      어느 한 길이 값 없이 끝에 닿으면 적합하지 아니하다.

(2) 선언의 머리(`fn`·`proc`·`on`·`test` …)에는 `do … end` 몸이 있어야 한다
      (`E-STMT-NODO`).

(3) 맨 바깥 자리에 오는 것은 **선언의 머리**로 시작해야 한다(`E-TOPLEVEL`).

(4) 폼 안에 올 수 없는 것이 오면 거부된다(`E-FORM-UNEXPECTED`). 머리 자리에 op 이 아닌
      이름이 오는 것도 그렇다(`E-HEAD-NOT-AN-OP`).

> [!산문]
> (1) 이 없으면 「값을 내놓는다」는 선언이 **어떤 길에서만** 참인 말이 된다. 부르는 쪽은
> 그 말을 믿고 값을 쓰는데, 그 길로 가면 값이 없다. 선언은 **모든 길에 대한 약속**이므로
> 한 길이라도 어기면 그것은 약속이 아니다.

```lowent-거부: 어떤 길에서 값이 없다 · E-RETURN-PARTIAL
module ex_partial .

fn f input a u8 . output u8 .
do
  if gt a 5 . do return 1 . end
end                      rem `a` 가 5 이하인 길에는 값이 없다
```

## 6.5.12 오류는 적은 것만 난다

(1) op 이 내는 오류는 `errors` 절이 적은 것 안에 있어야 한다(`E-ERR-UNDECLARED`).

(2) `errors` 절이 그 op 의 오류 타입에 **없는 이름**을 적는 것도 거부된다
      (`E-ERR-UNDEF`).

> [!주의]
> 두 규칙은 같은 문장의 양쪽이다 — **적은 것과 내는 것이 같아야 한다.** 한쪽이 넘치면
> 부르는 쪽이 못 본 실패가 오고, 다른 쪽이 넘치면 오지 않을 실패를 다루게 된다
> (⟦§6.4.12⟧ (4) 와 같은 까닭이다).

```lowent-거부: 적지 않은 오류를 낸다 · E-ERR-UNDECLARED
module ex_err_undeclared .

enum e .
  bad .
end .

fn f output result u8 e .
do
  return error bad .   rem `errors bad …` 를 적지 않았다
end
```
