# 9. `match` — 경우 나누기 (패턴 매칭)

[← 목차](README.md) · [← 8. 나만의 타입](08-types.md)

**`match`** 는 값을 **경우별로** 가른다. `if` 사슬과 달리 **모든 경우를 다뤄야** 한다(망라) —
빠뜨리면 컴파일 에러다. 그것이 `match` 가 주는 것이다: 나중에 변형을 하나 더하면 **컴파일러가
고쳐야 할 곳을 전부 찾아 준다.**

문법은 `match <값> do  case <패턴> . do … end  …  end` 이다. 각 `case` 는 do-블록으로 닫는다.

## 변형과 와일드카드 `_`

```lowent
rem ✓ enum 변형을 가른다. `_` 는 나머지를 전부 덮는 catch-all.
enum color do
  red .                                  rem 갈래마다 점으로 닫는다 — 없으면 셋이 한 갈래로 이어진다
  green .
  blue .
end
fn name output u8 . input c color . do
  match c do
    case red . do return 1 . end
    case _   . do return 0 .   end        rem green·blue 를 한꺼번에
  end
end
```

## 값 — 정수·bool 리터럴

```lowent
rem ✓ 정수는 무한하므로 `_` 가 반드시 필요하다.
fn grade output u8 . input n u8 . do
  match n do
    case 0 . do return 70 . end
    case 1 . do return 80 . end
    case _ . do return 0 .  end
  end
end
```

```lowent
rem ✓ bool 은 유한(true/false)이라 `_` 없이 둘만 다뤄도 망라다.
fn flip output u8 . input b u8 . do
  match b do
    case true  . do return 0 . end
    case false . do return 1 . end
  end
end
```

## 범위 `lo to hi`

```lowent
rem ✓ 폐구간 lo ≤ x ≤ hi. 경계 9·10 이 각각 옳은 팔에 든다.
fn band output u8 . input x u8 . do
  match x do
    case 0 to 9   . do return 1 . end
    case 10 to 19 . do return 2 . end
    case _        . do return 0 . end
  end
end
```

**범위가 타입 전체를 타일하면 `_` 없이도 망라다.** `u8` 은 `[0,255]` 이므로 아래는 빈틈이 없다:

```lowent
rem ✓ 두 범위가 u8 전 도메인을 타일 → `_` 불요. 빈틈이 있으면 E-MATCH-INEXHAUSTIVE,
rem   범위가 겹치면 E-MATCH-REDUNDANT.
fn half output u8 . input b u8 . do
  match b do
    case 0 to 127   . do return 0 . end
    case 128 to 255 . do return 1 . end
  end
end
```

## or-패턴 `a or b`

```lowent
rem ✓ 어느 하나라도 맞으면 그 팔. 변형 or 은 각 가지가 망라에 기여한다.
fn kind output u8 . input c color . do
  match c do
    case red or green . do return 1 . end
    case blue         . do return 2 . end        rem red·green·blue 전부 덮임 — `_` 불요
  end
end
```

페이로드를 가진 변형도 or 로 묶을 수 있다 — 다만 **모든 가지가 같은 이름을 바인딩**해야 한다:

```lowent
rem ✓ add·mul 둘 다 l·r 을 바인딩. 태그가 무엇이든 l·r 을 꺼내 쓴다(이름이 어긋나면 E-MATCH-ORBIND).
enum node do
  lit v u32 .
  add l u32 r u32 .
  mul l u32 r u32 .
end .
fn combine output u32 . input e node . do
  match e do
    case add l r or mul l r . do return add l r . end   rem add|mul + lit = 전 변형 → `_` 불요
    case lit v              . do return v .       end
  end
end
```

## 페이로드 꺼내기 (바인딩)

페이로드 `enum`([8장](08-types.md))은 `case <변형> <이름>…` 으로 딸린 값을 **이름에 묶는다**:

```lowent
rem ✓ AST 평가 — lit 은 값을, add 는 두 자식(아레나 인덱스)을 묶는다.
enum node do
  lit v u32 .
  add l u32 r u32 .
end
fn eval output u32 . input nodes slice node . input i u32 . do
  let n node be index nodes i .
  match n do
    case lit v   . do return v . end
    case add l r . do return add (eval nodes l) (eval nodes r) . end
  end
end
```

## `option` 과 `result`

```lowent
rem ✓ some 는 값을 묶고, none 은 빈 경우. 둘이 유한 도메인이라 `_` 불요.
fn or0 output u32 . input o option u32 . do
  match o do
    case some x . do return x . end
    case none  . do return 0 . end
  end
end
```

`result` 는 `case ok v` / `case error` 로 가른다(같은 결).

## 중첩 패턴

패턴은 겹칠 수 있다 — `result` 안의 `option` 을 한 번에 가른다:

```lowent
rem ✓ ok(some x) → x, ok(none) → 0, error → 99.
enum er do
  bad
end
fn pick output u32 . input r result option u32 er . do
  match r do
    case ok (some x) . do return x .   end
    case ok none     . do return 0 .   end
    case error       . do return 99 .  end
    case _           . do return 255 . end
  end
end
```

> 중첩은 **단락 평가**다 — `ok` 가 아니면 안쪽(`some x`)을 **아예 보지 않는다**. 그래서 `error` 에서
> 값을 잘못 꺼내(panic) 는 일이 없다.

## comptime — 컴파일 시 접기

scrutinee 가 **컴파일타임 상수**(리터럴·`comptime <식>`·`config <옵션>`)면 match 는 **맞는 팔
하나로 접힌다** — 런타임 디스패치가 **0** 이다(죽은 팔도 타입검사는 받는다, `#ifdef` 와 다르다):

```lowent
rem ✓ comptime (add 2 3) = 5 → case 5 하나만 남는다. 런타임 비교 0회(`--ir` 로 확인).
fn choose output u32 . do
  match comptime (add 2 3) do
    case 0 . do return 100 . end
    case 5 . do return 500 . end
    case _ . do return 999 . end
  end
end
```

## 가드 `when`

패턴이 맞은 **뒤에** 조건을 더 본다. **한 이름**(`y`)은 값 전체를 묶는다(그래서 가드가 쓸 수 있다):

```lowent
rem ✓ y 에 값을 묶고(전체 바인딩), 가드로 더 좁힌다.
fn big output u8 . input x u8 . do
  match x do
    case y when gt y 100 . do return 1 . end
    case _               . do return 0 . end       rem 가드가 거짓일 때를 `_` 가 받는다
  end
end
```

> 가드가 붙은 팔은 **망라에 세지 않는다**(가드가 거짓일 수 있으므로) — 그래서 `_`(또는 가드 없는
> 팔)가 여전히 필요하다.

## ✗ 흔한 실수

**빠뜨린 경우 — `E-MATCH-INEXHAUSTIVE`:**

```lowent
rem ✗ blue 를 안 다뤘다
match c do
  case red   . do return 1 . end
  case green . do return 2 . end
end
```
```
E-MATCH-INEXHAUSTIVE: this `match` does not handle every variant … add the missing `case` … or a `case _ .`
```

**같은 경우 두 번 / `_` 뒤의 팔 — `E-MATCH-REDUNDANT`:**

```lowent
rem ✗ `_` 가 이미 다 받았으므로 뒤 팔은 죽은 코드
match c do
  case _   . do return 0 . end
  case red . do return 1 . end
end
```
```
E-MATCH-REDUNDANT: this `case` comes AFTER a `_` … it can never run
```

⟹ 중복 팔은 **경고가 아니라 에러**다(Rust 보다 엄격) — "읽으면 각 경우가 한 번씩" 이 아닌 `match`
는 버그를 숨긴다. `--ir` 로 보면 도구가 **디스패치 비용**(자리·팔 수·최악 비교 횟수)까지 말해 준다.

---

[← 목차](README.md) · [다음: 10. 실수 치트시트 →](10-mistakes-cheatsheet.md)
