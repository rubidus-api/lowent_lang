# 예제 — 정수 RPN 계산기 (stateful · region · 계약)

호출자가 준 region 의 스택으로 토큰 slice 를 평가한다. **region·stack·EXCL·계약·result·전파**를 자연스럽게 사용하는 적정 규모 모듈. (MVP 문법)

```
module rpn .

rem 정수 RPN(역폴란드) 계산기.
rem "3 4 + 5 *" = [lit 3][lit 4][plus][lit 5][times] → (3+4)*5 = 35.

type scratch u64 . .

rem 토큰 종류 — 슬라이스로 건네려면 원소가 **바이트 레이아웃**을 가져야 해서(viewable) 종류는 u8 코드다.
rem   (enum 칸은 레이아웃이 없다. 이름은 prelude op(add/sub/…)와 겹치지 않게 — RFC-0002 §8-12(F1).)
let lit u8 be 0 .       rem 리터럴 push (value 사용)
let plus u8 be 1 .
let minus u8 be 2 .
let times u8 be 3 .
let divide u8 be 4 .

enum eval_error .
  underflow .           rem 이항 연산에 피연산자 부족
  div_by_zero .
  bad_expr .            rem 평가 후 스택에 정확히 1개가 안 남음
end .

struct token
  kind  u8 .
  value i64 .
end

rem 두 피연산자에 연산 적용 — 순수(effects none). divide 만 0 검사.
fn apply
  input k u8 .
  input a i64 .
  input b i64 .
  output result i64 eval_error . .
  errors div_by_zero .           rem 실행-유도 오류도 **이름은** 절에 적는다(조건은 안 적어도 된다)
do
  if eq k plus . do return ok add a b . . . end
  if eq k minus . do return ok sub a b . . . end
  if eq k times . do return ok mul a b . . . end
  guard ne b 0 . else return error div_by_zero . . .     rem 여기 도달 = divide
  return ok div a b . . .
end

rem RPN 평가 — scratch region 에 깊이 len(tokens) 의 i64 스택.
rem 비용 가시: effects alloc(스택 backing) + access sequential. 숨은 할당 0.
rem errors 절은 조건 없이 이름만 — underflow/bad_expr/div_by_zero 는 *실행-유도* 오류(본문 return error 가 진실).
rem   입력-결정 오류였다면 조건까지 적어 오라클화했을 것(cf. parse_header). RFC-0006 F2.
proc eval
  input temp region scratch . .
  input tokens slice token . .
  output result i64 eval_error . .
  effects alloc .
  access tokens sequential .
  requires gt (len tokens) 0 .
  errors underflow .
  errors div_by_zero .
  errors bad_expr .
do
  let s stack i64 . be stack_new temp capacity len tokens . . .
  var depth u64 be 0 .
  for t tokens do
    let k u8 be field t kind . .
    if eq (field t kind) lit . do
      push s field t value . .
      set depth (add depth 1) .
    end
    else do
      guard ge depth 2 . else return error underflow . . .
      let b i64 be value_or (pop s) 0 .            rem depth≥2 보장 → none 은 오지 않는다
      let a i64 be value_or (pop s) 0 .
      let r i64 be try apply k a b . .              rem apply 의 div_by_zero 를 그대로 전파
      push s r .
      set depth (sub depth 1) .
    end
  end
  guard eq depth 1 . else return error bad_expr . . .
  let top i64 be value_or (pop s) 0 .
  return ok top . .
end
```

## 보이는 Lowent 특징

```text
비용 가시(P2)    apply=effects none(순수) · eval=effects alloc(스택 backing 명시). 숨은 alloc 0.
명시 region      스택이 어디 사는지(temp region scratch)가 시그니처에 — 호출자가 수명·backing 소유(GC 없음).
EXCL/소유        let s stack = 스택 값(push/pop 이 채운다), 바인딩 자체는 불변(재배열 불가).
계약(SPARK식)    requires(비어있지 않음) + 세 실패 모드(result eval_error). 시그니처만으로 실패 파악(P5).
전파             try apply … 한 줄로 div_by_zero 위로(`?`/`!` 없이 키워드로 가시).
총체 pop         pop 은 option 을 낸다 — depth 로 가드한 뒤 value_or 로 받는다(빈 스택이 트랩이 아니다).
```

## 이 코드가 두드린 스펙 공백 (후속 RFC로 정리됨)

```text
F1  enum 변형 ↔ prelude op 이름 충돌 → 변형을 plus/minus/times/divide 로 명명(add/sub/… 회피).
    규칙: 변형 이름은 prelude op·예약어와 겹치면 에러. → RFC-0002 §8-12.
F2  errors 절 when 은 입력-결정 오류용 → 실행-유도 오류(underflow/bad_expr)는 errors 절 생략, 본문 return error.
    → RFC-0006 F2(when 선택화·errors 절 생략 허용).
    (2026-09-13: 지금은 실행-유도 오류도 errors 절에 **이름**은 적는다 — 빠뜨리면 E-ERR-UNDECLARED. 조건은 안 적어도 된다.
     토큰 종류는 enum 대신 u8 코드다 — `slice token` 은 원소가 바이트 레이아웃을 가져야 하는데 enum 칸은 없다.)
```
