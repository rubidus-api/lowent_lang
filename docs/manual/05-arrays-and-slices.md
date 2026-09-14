# 5. 배열과 슬라이스 — 여러 값을 담기

[← 목차](README.md) · [← 4. 지역 변수](04-variables.md)

- **`array <개수> <타입>`** — 개수가 **컴파일 때 고정**된 묶음(값 그 자체).
- **`slice <타입>`** — 길이가 **런타임 값**인 창(窓). 남의 버퍼를 **빌려** 본다.

읽고 쓰는 도구는 셋:

- `index <슬라이스> <i>` — `i` 번째 원소. (붙임 점 `s.0` 은 없다 — `E-FIELD-GLUED`. 한 뜻에 한 표기다.)
- `len <슬라이스>` — 길이.
- `for <이름> <슬라이스> do … end` — 원소를 하나씩 훑는다. **`in` 같은 표지는 없다.**

슬라이스에 **쓰려면** 그 슬라이스가 `mut` 여야 한다 — 그리고 쓰는 op 은 `proc` 이다
(호출자에게 보이는 변화니까, [3장](03-ops.md)):

```lowent
rem ✓ mut 슬라이스에 값을 넣고, for 로 합을 구한다.
proc fill input xs mut slice u64 . . output u64 . effects none . do
  set (index xs 0) 10 .
  set (index xs 1) 20 .
  var s u64 be 0 .
  for x xs do                 rem for <이름> <슬라이스> — `in` 없음
    set s (add s x) .
  end
  return s .
end
```

> 매개변수의 `mut slice` 뒤에 점이 **둘**(`. .`)인 게 보이는가? 하나는 `slice` 절을,
> 하나는 `input` 절을 닫는다. 슬라이스 타입 자체가 한 절을 이룬다.

**✗ 실수 1 — `mut` 없는 슬라이스에 쓰기:**

```lowent
rem ✗ 잘못 — xs 가 mut 이 아니다
proc f input xs slice u64 . . output u64 . effects none . do
  set (index xs 0) 1 .
  return 0 .
end
```
```
E-TYPE-MUT: 공유(shared) 슬라이스의 원소는 읽기 전용이다 — `mut slice` 로 받아라
```

**✗ 실수 2 — `for` 에 `in` 붙이기:**

```lowent
for x in xs do … end          rem ✗ 잘못
```
```
E-VOCAB-REMOVED: `in` 은 없앤 낱말이다 — `for x xs do …` 로 쓴다
```

**✗ 실수 3 — 범위 밖 접근:** `index xs 5` 인데 길이가 3이면 **실행 중** 잡힌다
(`E-VM-BOUNDS: slice index out of bounds`). 그래서 흔히 `guard`([7장](07-guard-and-option.md))로
길이를 먼저 확인한다.

## `len` 은 얼마나 비싼가 — 루프 조건에 그냥 써도 되나

**써도 된다.** `len s` 는 **호출이 아니고 메모리를 읽지도 않는다** — 슬라이스 값
`{시작 주소, 길이}` 의 **길이 칸**을 꺼내는 것뿐이다. 그래서 `while lt i (len xs) .` 처럼
조건에 그대로 두어도 비용은 칸 하나 읽기이고, 그 값이 매 바퀴 같다면 컴파일러가 걷어낸다.

```lowent
rem ✓ 이대로 좋다 — 미리 담을 이유가 없다
var i u64 be 0 .
while lt i (len xs) . do
  set s (add s (index xs i)) .
  set i (add i 1) .
end
```

**원소에 써도 길이는 변하지 않는다.** 길이는 값의 일부이지 가리키는 메모리의 내용이
아니다. 길이가 달라지는 길은 **그 이름에 다른 슬라이스를 다시 담는 것** 하나뿐이다.

그러면 `let m u64 be len xs .` 처럼 미리 담는 것은 언제 하는가? **속도 때문이 아니라
뜻을 고정하고 싶을 때** 한다. 반대로 재대입이 있는 코드에서 미리 담으면 **낡은 길이**가
된다 — 이쪽이 진짜 함정이다:

```lowent
rem ✗ 함정 — m 은 줄어들기 전의 길이다
let m u64 be len xs .
var i u64 be 0 .
while lt i m . do
  set xs (subslice xs 0 2) .    rem xs 가 짧아졌는데 m 은 그대로다
  set s (add s (index xs i)) .  rem 범위 밖으로 갈 수 있다
  set i (add i 1) .
end
```

☞ 한 줄로: **`len` 은 매번 지금 값을 읽는다. 그것이 안전을 지킨다. 미리 담는 것은
최적화가 아니라 의미의 선택이다.**

## 계약을 쓰면 검사가 사라진다

길이 조건을 `requires` 로 적으면 그 검사는 **op 진입에서 한 번**만 일어나고, 본문의
색인 경계 검사는 **전부 사라진다**. `<` 로 적든 `≤` 로 적든 같다:

```lowent
fn total input a slice u8 . input n u64 . output u64 .
  requires le n (len a) . do        rem ← 이 한 줄이 본문의 경계 검사를 없앤다
  …
```

다만 **색인 자체에 `≤` 를 걸면 안 된다** — `requires le i (len a) .` 는 `i = len a` 를
허용하고 그것은 **한 칸 밖**이다. 색인에는 `lt` 를 쓴다.

## 상수 표는 **문자열 리터럴**이다 — if 사슬로 쓰지 마라

색인 → 상수 를 돌려주는 함수를 `if` 사슬로 쓰기 쉽다. **쓰지 마라.**

```lowent
rem ✘ 이렇게 쓰면 문자마다 비교와 분기가 하나씩 생긴다
fn prefix_char input k u64 . output u8 . do
  if eq k 0 . do return 47 . end
  if eq k 1 . do return 97 . end
  …
end

rem ✔ 문자열 리터럴은 `slice u8` 이고 **그대로 색인된다**
fn prefix_char input k u64 . output u8 . do
  return index "/api/users/" k .
end
```

방출되는 C 에서 앞의 것은 `if … goto` 사슬이 되고, 뒤의 것은 **정적 표 하나 + 색인**이 된다.

★ 이것은 취향이 아니라 **실측된 값**이다. `bench_http` 가 정확히 이 사슬을 쓰고 있었고,
  cachegrind 가 그 자리를 찍었다 — 분기 예측 실패의 큰 몫이 거기였다. 표 조회로 바꾸자
  **예측 실패가 손으로 쓴 C 와 같아졌고**(126 → 57 = C 의 57) 시간이 **×2.67 → ×2.01** 이
  됐다. 답(체크섬)은 한 비트도 안 바뀌었다.

☞ 교훈은 두 겹이다: 사슬이 느리다는 것, 그리고 **언어가 이미 할 수 있는 것을 몰라서
  느리게 썼다**는 것. 그래서 이 절이 여기 있다.

---

[← 목차](README.md) · [다음: 6. 참조 →](06-references.md)
