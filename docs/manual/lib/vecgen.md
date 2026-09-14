# vecgen — 제네릭 자가성장 벡터 (`vec t a`) — **두 축**

`lib/vecgen.low` · 모듈 이름 `vecgen` · 계층 L1(저장 — 할당 권한이 필요하다)

## 처음 쓰는 사람에게

**"어떤 타입이든 담는, 자리가 모자라면 스스로 커지는 배열"** 이다.

[growvec](growvec.md) 은 **바이트만** 담는다. `u32` 를 담고 싶으면 그 파일을 통째로 복사해
`u8` 을 `u32` 로 고친 파일을 하나 더 만들어야 했다 — **원소 종류마다 컨테이너 하나씩**이었다.
여기서는 하나의 틀에서 나온다:

```lowent
use vecgen .

rem u32 벡터 — 원소 2개 자리로 연다
let g option (vecgen.vec u32 allocs.bump_bytes) . using bump be vecgen.open u32 2 .
guard is_some g . else return 1 .
var v vecgen.vec u32 allocs.bump_bytes . be some_value g .

guard vecgen.append u32 allocs.bump_bytes v 100 . else return 2 .     rem 자리가 모자라면 스스로 커진다
guard vecgen.append u32 allocs.bump_bytes v 101 . else return 3 .
guard vecgen.append u32 allocs.bump_bytes v 102 . else return 4 .     rem ← 여기서 커졌다

let x option u32 . be vecgen.at u32 allocs.bump_bytes v 0 .           rem some 100 — 옛 내용은 살아 있다
```

**타입을 매번 적는다**(`vecgen.append u32 allocs.bump_bytes v 100`). 추론이 없어서가 아니라 **일부러 안 넣었다** —
아래 «타입을 왜 적나» 를 보라.

## 축이 둘이다 — 원소 타입 × 얼로케이터 타입 (2026-08-15)

전에는 이 틀이 원소만 제네릭이고 얼로케이터는 `allocs.bump_bytes` 에 **못박혀** 있었다.
그래서 *"원소는 u32 이고 얼로케이터는 내가 고른 것"* 이라는, 아주 평범한 요구를 담을 컨테이너가
**없었다**: `vecgen` 을 쓰면 얼로케이터를 잃고, `vecs` 를 쓰면 원소를 u8 로 잃었다.

```
let g option (vecgen.vec u32 allocs.bump_aligned) .
      using ba be vecgen.open u32 2 .    rem 8바이트 정렬 범프로 연다
```

같은 소스에서 `vec#u32#allocs.bump_bytes` 와 `vec#u32#allocs.bump_aligned` 두 인스턴스가 나온다
(`--ir` 에 보인다). vtable 도 간접 호출도 없다 — **정책이 컨테이너 밖에 있고**, 고르는 값이 0 이다.

**대가**: 호출 자리마다 얼로케이터 타입을 한 낱말 더 적는다. 부분 적용 별칭이 없어서 한 축만
가린 얼굴은 못 만든다 — 그러나 **완전적용 별칭은 된다**. 바이트만 쓴다면 `growvec` 을 써라:
`growvec.add_byte g x` 는 타입 인자를 **하나도** 안 적는다.

**호출자-버퍼 컨테이너는 여전히 따로다**: `vecs`(`lib/vec.low`)는 버퍼를 호출자가 들고, 성장하면
새 자리를 **값으로 돌려준다**. 소유 모델이 달라서 접지 않았다 — 축이 겹친다고 계약까지 접으면
축소가 아니라 뭉갬이다.


## 왜 있는가

RFC-0075 §2.2 가 적어 둔 컨테이너 한계의 **마지막 조각**이다.

- `vecs` — 부를 때마다 얼로케이터와 버퍼를 넘겨야 한다(다섯 조각을 들고 다닌다).
- `growvec` — 그 셋을 한 값에 넣었다. 그런데 **바이트만** 담는다.
- `vecgen` — 원소 타입까지 틀에서 뺀다. **하나의 소스에서 `vec u32` 와 `vec u8` 이 나온다.**

## 설계 의도와 경계

**① 단형화다 — 런타임 다형이 아니다.** `vec u32` 는 컴파일 시각에 `vec#u32` 라는 **구체
struct** 가 된다. vtable 없음 · 원소 크기 필드 없음 · 간접 호출 없음. 크기와 오프셋이 전부
그 자리에서 **상수**로 나온다. 대가는 인스턴스마다 코드가 생기는 것이고, 그것은 `--ir` 에
`vec#u32` 로 **보인다**. 감춘 비용이 아니라 **보이는 비용**이다.

**② 저장은 바이트다.** 얼로케이터가 바이트를 주기 때문이다. 원소로 보는 것은 `view_array` 가
하고, 몇 바이트가 필요한지는 **`size_of t`** 가 답한다. 이 op 이 없으면 제네릭 코드는 최대
폭(8바이트)으로 잡는 수밖에 없고 — `vec u8` 이 **8배**를 쓴다. 비용이 보이려면 비용을 **물을
수 있어야** 한다.

**③ 권한은 여전히 남이 준 것이다.** `open` 이 얼로케이터를 받는다. 컨테이너가 권한을 만들어
내지 않는다 — 들고 다닐 뿐이다(RFC-0043 D1).

**원소는 스칼라도 되고 구조체도 된다**(2026-07-26). `vec pt` 처럼 **레이아웃 있는 구조체**를
담으면 `at` 이 그 구조체를 그대로 돌려준다(필드까지 살아 있다). 조건은 하나 — **viewable** 이어야
한다: 모든 필드가 바이트 레이아웃을 갖고(슬라이스·소유 핸들이 없고) 총 크기가 255 이하다.
레이아웃이 없는 타입에는 배열도 없다.

**안 지은 것(정직히)**

- **중첩**(`vec (vec u8)`) — 타입 인자는 **낱말 하나**다.
- **소유 있는 원소**(`vec files.handle`) — 지금은 인스턴스 **안**에서 걸린다. 진단이 호출
  자리를 가리켜야 하는데 아직 아니다(RFC-0084 §10).
- 축소(shrink) · 개별 반납(범프가 안 한다).

### 타입을 왜 적나

`vecgen.append u32 allocs.bump_bytes v x` 의 `u32` 는 **없어도 되는 글자가 아니다**. 이것이 있어서:

- 어떤 인스턴스가 생겼는지 **소스에 보인다** — 바이너리에 코드가 몇 벌 생기는지가 보인다.
- 읽는 사람이 `v` 의 원소 타입을 **선언까지 되짚어 가지 않아도** 안다.

추론은 나중에 덧붙일 수 있지만 그 반대는 어렵다. 그래서 첫 판은 **적는 쪽**이다(RFC-0084 §9).

## 자료구조

```lowent
export struct vec
  input comptime t type .   rem ← 타입 파라미터 둘(원소 · 얼로케이터). 인스턴스에서는 이 줄들이 사라진다
  input comptime a type .
  al a .                    rem 어디서 자리를 받아 오는가
  store mut slice u8 . .    rem 소유하는 저장소 — **바이트**다
  n u64 .                   rem 담긴 **원소** 개수(바이트가 아니다)
end
```

| 불변식 | |
|---|---|
| `n * size_of t <= len store` | 담긴 개수는 용량을 넘지 않는다 |
| 원소 `0 .. n` | 유효한 데이터 |
| 그 뒤 | **쓰레기다** |
| `store` | 커질 때마다 **다른 버퍼로 바뀐다** |

**세는 단위를 섞지 않는다**: `open` 의 `cap0`, `cap_of`, `count_of`, `at`, `set_at` 은 전부
**원소 개수**다. 바이트는 이 모듈 밖으로 안 나온다.

## op 한눈에

모든 op 의 **첫 인자가 타입**이다(원소 타입 · 얼로케이터 타입). `open` 만 다르다 — 얼로케이터를 위치 인자가 아니라 `using` 으로 받고, 그 타입은 출처에서 채워지므로 원소 타입만 적는다(RFC-0112 D8).

| op | effects | 하는 일 |
|---|---|---|
| `open t cap0` (`using al`) | state | 빈 벡터를 연다(원소 `cap0` 개 자리) |
| `count_of t g` | none | 담긴 원소 수 |
| `cap_of t g` | none | 지금 용량(원소 수) |
| `append t g x` | state | 원소 하나 — 모자라면 스스로 커진다 |
| `at t g i` | none | i 번째 원소 → `option t` |
| `set_at t g i x` | state | i 번째를 고친다(범위 밖 = false) |
| `reserve_more t g n` | state | 앞으로 n 개 더 들어갈 자리를 미리 |
| `view_of t g` | none | 담긴 만큼만 보는 `slice t` |

## op 상세

### `open` — 연다

```lowent
proc open input comptime t type . input comptime a type . using al a . input cap0 u64 .
     output option (vec t a) . effects state via a . requires allocs.byte_allocator a .
```

- `t` — 원소 타입. **크기 있는 스칼라**(`u8`·`u16`·`u32`·`u64`·`i*`·`f32`·`f64`) 또는
  **viewable 구조체**(모든 필드에 바이트 레이아웃이 있고 총 ≤ 255바이트).
- `a` — 얼로케이터 타입(`byte_allocator` 를 갖춘 것). 부르는 쪽은 적지 않는다 — `using` 에 건넨 출처의 타입에서 채워진다.
- `al` — **이미 초기화된** 얼로케이터(`using` 절 — 부르는 쪽이 `using bump be vecgen.open u32 2 .` 로 건넨다). 벡터가 필드로 들고 다닌다.
- `cap0` — 처음 받을 **원소 개수**. 실제로 요청하는 바이트는 `cap0 * size_of t` 다.
- 반환 — 첫 자리를 못 받으면 `none`.

### `append` — 하나 민다

```lowent
proc append input comptime t type . input comptime a type . input g mut vec t a . input x t . output bool . effects state .
```

자리가 있으면 `O(1)`. 없으면 **두 배 + 8 원소**의 새 자리를 받아 전부 옮긴다(`O(n)`).
연속 `n` 번의 총비용은 `O(n)` 이다(분할상환).

`false` = 새 자리를 못 받았다. **트랩이 아니라 값이다** — 그 원소만 안 들어갔고 벡터는
여전히 유효하다.

### `at` · `set_at`

```lowent
proc at     input comptime t type . input g vec t . input i u64 . output option t . effects none .
proc set_at input comptime t type . input g mut vec t . input i u64 . input x t . output bool . effects state .
```

둘 다 **범위 밖이면 값으로 답한다**(`none` / `false`). `set_at` 은 벡터를 조용히 늘리지
않는다 — 늘리려면 `append` 다.

### `reserve_more` — 미리 확보

```lowent
proc reserve_more input comptime t type . input g mut vec t . input more u64 . output bool . effects state .
```

`n + more` 개가 들어갈 자리를 확보한다. 이걸 먼저 부르면 뒤따르는 `append` 가 **도중에
실패하지 않는다**. 넣을 개수를 미리 알면 항상 이 순서가 낫다(성장도 한 번으로 몰린다).

### `view_of` — 담긴 만큼

```lowent
proc view_of input comptime t type . input g vec t . output slice t . effects none .
```

용량 전체가 아니라 **쓴 만큼**이다. 반환은 `slice t` — 그대로 [sortgen](sortgen.md) 이나
[searchlib](searchlib.md) 에 넘길 수 있다.

## 사용법과 예제

### ① 크기를 모르는 수열 모으기

```lowent
module collect .

use vecgen .
use allocs .

rem 짝수만 모아서 벡터로 돌려준다. 몇 개가 될지는 미리 모른다.
export proc evens input al allocs.bump_bytes . input src slice u32 . output option (vecgen.vec u32 allocs.bump_bytes) . effects state . do
  let g option (vecgen.vec u32 allocs.bump_bytes) . using al be vecgen.open u32 8 .
  guard is_some g . else return none .
  var v vecgen.vec u32 allocs.bump_bytes . be some_value g .
  var i u64 be 0 .
  while lt i (len src) . do
    if eq (mod (widen u64 (index src i)) 2) 0 . do
      guard vecgen.append u32 allocs.bump_bytes v (index src i) . else return none .
    end
    set i (add i 1) .
  end
  return some v .
end
```

### ② 미리 자리를 잡아 "전량 아니면 무" 로

```lowent
guard vecgen.reserve_more u16 allocs.bump_bytes v (len src) . else return false .
var i u64 be 0 .
while lt i (len src) . do
  guard vecgen.append u16 allocs.bump_bytes v (index src i) . else return false .   rem 이제 실패하지 않는다
  set i (add i 1) .
end
```

### ③ 두 인스턴스가 같은 얼로케이터를 쓸 때

```lowent
let a option (vecgen.vec u8 allocs.bump_bytes)  . using bump be vecgen.open u8 4 .    rem  4 바이트
let b option (vecgen.vec u32 allocs.bump_bytes) . using bump be vecgen.open u32 4 .    rem 16 바이트
rem 합쳐서 20 바이트 — 자리 계산이 **인스턴스마다 다르다**. 그것이 size_of 가 있는 이유다.
```

## 반례 — 이렇게 쓰면 안 된다

### ✘ 타입을 빼먹는다

```lowent
rem ✘ 타입 인자가 없으면 그 op 은 **존재하지 않는다**(틀은 부를 수 없다)
guard vecgen.append v allocs.bump_bytes 7 . else return 1 .
rem ✔
guard vecgen.append u32 allocs.bump_bytes v 7 . else return 1 .
```

### ✘ 커진 뒤에 옛 뷰를 쓴다

```lowent
rem ✘ 위험하다
let s slice u32 . be vecgen.view_of u32 allocs.bump_bytes v .
guard vecgen.append u32 allocs.bump_bytes v 9 . else return 1 .   rem ← 여기서 store 가 바뀔 수 있다
return index s 0 .                            rem s 는 **옛 버퍼**를 본다
```

**뷰는 마지막에 꺼낸다.** 더 넣을 일이 남아 있으면 꺼내지 않는다.

### ✘ 반환값을 안 본다

`append` 의 `false` 는 OOM 이다. 안 보면 **조용히 짧아진 벡터**가 된다 — 값을 안 보면
없던 일이 된다.

### ✘ `cap_of` 만큼 읽는다

용량 뒤는 쓰레기다. `count_of` 또는 `view_of` 를 쓴다.

### ✘ 레이아웃 없는 구조체를 담는다

```lowent
rem ✘ 슬라이스 필드가 있는 구조체는 **바이트 레이아웃이 없다** — 배열도 없다
struct holder
  buf mut slice u8 . .
end
let g option (vecgen.vec holder allocs.bump_bytes) . using al be vecgen.open holder 4 .   rem E-IR-UNDEF
```

담을 수 있는 구조체는 **viewable** 뿐이다(스칼라 필드만, 총 ≤ 255바이트).

### ✘ 중첩을 기대한다

```lowent
rem ✘ 타입 인자는 낱말 하나다
let g option (vecgen.vec (vecgen.vec u8 allocs.bump_bytes)) . be … .
```

## 주의사항

- **원소 타입은 스칼라 또는 viewable 구조체다.** 슬라이스·`option`·소유 핸들 필드는 안 된다.
- **인스턴스마다 코드가 생긴다.** `vec u8`·`vec u16`·`vec u32` 를 다 쓰면 세 벌이다.
  그것이 단형화의 가격이고, `--ir` 에서 **셀 수 있다**.
- **개별 반납은 없다.** 범프 얼로케이터는 통째로만 되돌린다 — 성장 횟수를 줄이려면 `open` 의
  `cap0` 이나 `reserve_more` 를 쓴다.
- **벡터의 수명은 얼로케이터의 수명 안이다.**
- `growvec` 은 **그대로 남아 있다**(바이트 전용). 새 코드는 `vecgen` 를 쓰고, 기존 코드를 굳이
  옮기지 않는다 — 공개 타입 이름을 조용히 바꾸는 것보다 잠시 공존하는 편이 낫다.
