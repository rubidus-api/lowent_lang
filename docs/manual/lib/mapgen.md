# mapgen — 제네릭 해시맵 (`table k v`)

`lib/mapgen.low` · 모듈 이름 `mapgen` · 계층 L1(저장 — 할당 권한이 필요하다)

## 처음 쓰는 사람에게

**"키로 값을 찾는 표"** 다. 그리고 키 타입과 값 타입을 **내가 고른다**.

```lowent
use mapgen .

let mo option (mapgen.table u32 u64) . using bump be mapgen.open u32 u64 4 .   rem 쌍 4개쯤 담을 준비
guard is_some mo . else return 1 .
var m mapgen.table u32 u64 . be some_value mo .

guard mapgen.insert u32 u64 m 7 900 . else return 2 .
let x option u64 . be mapgen.lookup u32 u64 m 7 .                    rem some 900
guard mapgen.erase u32 u64 m 7 . else return 3 .                     rem 지운다
```

op 을 부를 때마다 **키 타입과 값 타입을 이 순서로 적는다**(`u32 u64`). 이유는
[vecgen](vecgen.md) 의 «타입을 왜 적나» 와 같다 — 어떤 인스턴스가 생겼는지 소스에 보이게 하려는
것이다.

[hashmap](hashmap.md) 과의 차이: `hashmap` 은 `u64 → u64` 로 **못 박혀** 있고 저장소를
호출자가 들고 다니며 `rehash` 도 직접 부른다. `mapgen` 은 타입이 열려 있고, 저장소와
얼로케이터를 **한 값이 소유**하며, 꽉 차면 **스스로** 다시 뿌린다.

## 왜 있는가

`vec t` 가 타입 파라미터 **하나**로 길을 냈다. `table k v` 는 그 길이 **여럿에도 서는지** 묻는
자리다 — 접기가 인자를 개수만큼 먹어야 하고, `make m k v do … end` 처럼 파스가 갈라지는
자리도 인자 개수만큼 봉합돼야 한다. 그게 안 되면 이 모듈은 아예 못 선다.

그래서 `table` 은 편의 자료구조이면서 동시에 **제네릭 기계의 두 번째 증인**이다.

## 설계 의도와 경계

**⓪ 이름이 `map` 이 아니라 `table` 인 이유**: `map` 은 **빌트인 op** 이다(고차 프렐류드).
같은 낱말이 두 가지를 뜻하면 문장이 어떻게 묶이는지가 흔들린다 — 이 언어는 그것을 금지한다.
제네릭이라는 이유로 그 검사를 빠져나가고 있었고(RFC-0084 §10), 그 구멍을 닫자 **이 모듈이
첫 번째로 걸렸다**. 게이트가 자기 저자를 먼저 잡은 셈이다.

**① 빈칸을 키 값으로 예약하지 않는다.** 상태 배열(`flags`)을 따로 둔다 — 0 빈칸 · 1 참 ·
2 무덤. 흔한 지름길은 "키 0 = 빈칸" 인데, 그러면 **키 0 을 못 담는다**. 못 담는 게 아니라
담긴 척하고 조용히 틀린 답을 준다. 그런 함정은 두지 않는다.

**② 무덤(tombstone)을 남긴다.** 지운 자리를 0 으로 되돌리면 그 자리를 **지나서** 놓인 키를
영영 못 찾는다(선형 탐침의 사슬이 끊긴다). 그래서 지운 자리는 2 이고, 탐침은 거기서
**멈추지 않되** 넣을 자리로는 쓴다.

**③ 부하율 ≤ 0.5.** 선형 탐침은 꽉 찰수록 군집이 생겨 비용이 급히 오른다. 절반을 넘기 전에
슬롯을 두 배로 늘리고 **전부 다시 뿌린다**.

**④ 해시는 분포용이지 암호용이 아니다.** 곱셈-xorshift 한 판(splitmix 계열). 적대적 입력에
대한 방어가 아니다 — 그런 게 필요하면 키를 미리 섞어서 넣는다.

**⑤ 순회는 반복자가 아니라 자리(slot)다.** 해시 표에는 "다음 원소" 라는 자연스러운 순서가
없다 — 있는 것은 자리뿐이고 그 중 일부만 차 있다. 그래서 상태를 들고 다니는 반복자 객체를
지어내는 대신 `next_used` 가 **다음으로 찬 자리**를 준다. 무효화될 객체가 없다.

**안 지은 것(정직히)**: 정수 아닌 키(`f32`·구조체·바이트열 — 각각 다른 해시가 필요하다) ·
축소 · 개별 반납. 바이트열 키는 [strmap](strmap.md) 이 이미 한다.

## 자료구조

```lowent
export struct table do
  input comptime k type .   rem ← 타입 파라미터 둘. 블록 맨 앞에 연달아 온다
  input comptime v type .
  al allocs.bump_bytes .
  keys mut slice u8 . .     rem 키 배열의 바이트
  vals mut slice u8 . .     rem 값 배열의 바이트
  flags mut slice u8 . .    rem 0 빈칸 · 1 참 · 2 무덤
  n u64 .                   rem 담긴 **쌍**의 수
end
```

| 불변식 | |
|---|---|
| `len flags` | **2의 거듭제곱** (마스크 탐침의 조건) |
| `n * 2 < len flags` | 부하율 ≤ 0.5 |
| `flags[i] == 1` | 그 자리의 키·값이 유효하다 |
| `flags[i] == 2` | 지워진 자리 — **탐침은 지나간다** |

**세 배열의 길이는 슬롯 수로 같다.** 키 배열은 `슬롯 * size_of k` 바이트, 값 배열은
`슬롯 * size_of v` 바이트다 — 그래서 `table u8 u16` 은 `table u32 u64` 보다 훨씬 적게 쓴다.

## op 한눈에

모든 op 이 **키 타입 · 값 타입**을 이 순서로 먼저 받는다. `open` 은 얼로케이터를 위치 인자가 아니라 `using` 으로 받는다(RFC-0112 D8).

| op | effects | 하는 일 |
|---|---|---|
| `open k v want` (`using al`) | state | 빈 맵을 연다(쌍 `want` 개를 담을 준비) |
| `insert k v m key val` | state | 넣는다(있으면 덮어쓴다) |
| `lookup k v m key` | none | 값을 찾는다 → `option v` |
| `has k v m key` | none | 있는가 |
| `erase k v m key` | state | 지운다(있었으면 true) |
| `count_of k v m` | none | 담긴 쌍의 수 |
| `slots_of k v m` | none | 지금 슬롯 수 |
| `next_used k v m from` | none | `from` 부터 **처음 찬 자리** → `option u64` |
| `key_at k v m at` | none | 그 자리의 키 → `option k` |
| `val_at k v m at` | none | 그 자리의 값 → `option v` |

## op 상세

### `open`

```lowent
proc open input comptime k type . input comptime v type .
     using al allocs.bump_bytes . input want u64 .
     output option (table k v) . effects state .
```

- `al` — 자리를 받아 올 범프 얼로케이터. `using` 절이라 부르는 쪽은 `let mo … using bump be mapgen.open u32 u64 4 .`
  로 건넨다(그 op 안에 출처가 하나뿐이면 적지 않아도 그것이 기본값이다). 맵이 필드로 들고 다닌다.

- `k`·`v` — **정수 스칼라**여야 한다(`u8`…`u64`, `i8`…`i64`). 키는 `widen u64` 로 해시된다.
- `want` — 담을 **쌍의 수**. 슬롯은 `(want+1)*2` 를 2의 거듭제곱으로 올린 값(최소 8)이다.
- 세 배열 중 하나라도 못 받으면 `none`.

### `insert` — 넣는다

```lowent
proc insert input comptime k type . input comptime v type . input m mut table k v .
     input key k . input val v . output bool . effects state .
```

- 이미 있는 키면 **값을 덮어쓰고** 개수는 안 늘어난다.
- 부하율이 절반을 넘게 되면 **먼저** 슬롯을 두 배로 늘려 다시 뿌린다(`O(슬롯)`).
- `false` = 새 자리를 못 받았다. 트랩이 아니라 값이다.

### `lookup` · `has`

```lowent
proc lookup input comptime k type . input comptime v type . input m table k v . input key k .
     output option v . effects none .
proc has    … output bool . effects none .
```

`effects none` 이다 — 찾기는 아무것도 안 바꾼다. 그래서 오라클(VM ≡ 네이티브)이 그대로 돌린다.

### `erase` — 지운다

```lowent
proc erase input comptime k type . input comptime v type . input m mut table k v . input key k .
     output bool . effects state .
```

**무덤을 남긴다.** 같은 키를 다시 넣으면 그 자리를 다시 쓴다. 무덤은 `regrow` 때 사라진다.

### `count_of` · `slots_of`

담긴 쌍의 수와 슬롯 수다. **`count_of` 는 무덤을 안 센다** — 지운 것은 없는 것이다.

### `next_used` · `key_at` · `val_at` — 순회

```lowent
proc next_used input comptime k type . input comptime v type . input m table k v . input from u64 .
     output option u64 . effects none .
proc key_at … input at u64 . output option k . effects none .
proc val_at … input at u64 . output option v . effects none .
```

- `next_used` 는 **자리 번호**를 준다. 시작은 `0`, 다음은 **앞 자리 + 1** 이다. `none` 이 끝이다.
- `key_at`/`val_at` 은 **찬 자리가 아니면 `none`** 이다 — 자리 번호를 믿지 않는다.
- **순서는 약속하지 않는다.** 해시 순서이고, 다시 뿌리면 바뀐다. 순서가 필요하면 꺼내서
  [sortgen](sortgen.md) 으로 정렬한다.
- ☞ **순회 중에 `insert` 하지 않는다**: 다시 뿌려지면 자리 번호의 뜻이 바뀐다. `erase` 는
  안전하다(무덤만 남는다).

## 사용법과 예제

### ① 빈도 세기

```lowent
module freq .

use mapgen .
use allocs .

rem 값마다 몇 번 나왔는지 센다.
export proc count_all input al allocs.bump_bytes . input src slice u32 . output option (mapgen.table u32 u64) . effects state . do
  let mo option (mapgen.table u32 u64) . using al be mapgen.open u32 u64 16 .
  guard is_some mo . else return none .
  var m mapgen.table u32 u64 . be some_value mo .
  var i u64 be 0 .
  while lt i (len src) . do
    let key u32 be index src i .
    let cur option u64 . be mapgen.lookup u32 u64 m key .
    var next u64 be 1 .
    if is_some cur . do set next (add (some_value cur) 1) . end
    guard mapgen.insert u32 u64 m key next . else return none .
    set i (add i 1) .
  end
  return some m .
end
```

`lookup` → 없으면 1, 있으면 +1 → `insert` 가 이 자료구조의 기본 관용구다.

### ② 작은 맵은 작은 바이트를 쓴다

```lowent
let mo option (mapgen.table u8 u16) . using bump be mapgen.open u8 u16 3 .
rem 슬롯 8 ⇒ 키 8바이트 + 값 16바이트 + 상태 8바이트 = 32바이트.
rem 같은 개수를 table u32 u64 로 열면 8*4 + 8*8 + 8 = 104바이트다.
```

**산술이 인스턴스마다 다르다** — `size_of k`·`size_of v` 가 컴파일 시각에 박히기 때문이다.

### ③ 전부 훑기

```lowent
var at u64 be 0 .
var total u64 be 0 .
var going bool be true .
while going . do
  let nx option u64 . be mapgen.next_used u32 u64 m at .
  guard is_some nx . else do
    set going false .
    continue .
  end
  let s u64 be some_value nx .
  let v option u64 . be mapgen.val_at u32 u64 m s .
  guard is_some v . else return none .
  set total (add total (some_value v)) .
  set at (add s 1) .            rem ★ **앞 자리 + 1** — 안 그러면 제자리를 맴돈다
end
```

### ④ 집합(set)으로 쓰기

값 타입을 `u8` 로 두고 1 만 넣으면 집합이다. 전용 자료구조는 아직 없다.

## 반례 — 이렇게 쓰면 안 된다

### ✘ 타입 순서를 바꾼다

```lowent
rem ✘ table u32 u64 를 열고 u64 u32 로 부르면 **다른 인스턴스**를 부르는 것이다
let x option u32 . be mapgen.lookup u64 u32 m 7 .
```

타입 검사가 잡지만, 순서는 **선언 순서 그대로**(키·값)라는 것을 기억한다.

### ✘ `lookup` 의 `none` 을 "에러" 로 읽는다

`none` 은 "그 키가 없다" 다. 흔한 정상 경우다 — 위 빈도 세기가 바로 그것을 쓴다.

### ✘ 지운 뒤 개수가 그대로일 거라 여긴다

`erase` 는 `count_of` 를 줄인다. 다만 **슬롯은 안 줄어든다**(축소 없음) — 무덤이 남아 있고,
그것은 다음 `regrow` 때 정리된다.

### ✘ 순회하면서 넣는다

```lowent
rem ✘ insert 가 다시 뿌리면 **자리 번호의 뜻이 바뀐다** — 건너뛰거나 두 번 본다
let nx option u64 . be mapgen.next_used u32 u64 m at .
guard mapgen.insert u32 u64 m 999 1 . else return 1 .
```

모아서 나중에 넣거나, 넣을 것을 [vecgen](vecgen.md) 에 담아 두었다가 순회가 끝난 뒤 넣는다.
`erase` 는 순회 중에도 안전하다.

### ✘ `at` 을 1 씩 올리지 않는다

`set at (some_value nx)` 로 두면 **같은 자리를 영원히** 다시 찾는다. `add s 1` 이다.

### ✘ 정수 아닌 키를 쓴다

```lowent
rem ✘ f32 키는 안 된다 — widen u64 가 뜻 없는 비트를 만든다
let mo option (mapgen.table f32 u64) . using al be mapgen.open f32 u64 8 .
```

바이트열 키는 [strmap](strmap.md), 부동소수 키는 아직 없다.

## 주의사항

- **키·값은 정수 스칼라다.** 구조체·슬라이스는 안 된다(RFC-0084 §10 의 한계와 같다).
- **인스턴스마다 코드가 생긴다.** `table u32 u64` 와 `table u8 u16` 을 다 쓰면 두 벌이다.
- **성장은 통째로 다시 뿌린다** — 그 순간 `O(슬롯)` 이 든다. 개수를 대충이라도 알면 `open` 의
  `want` 를 넉넉히 준다.
- **개별 반납은 없다.** 범프 얼로케이터는 통째로만 되돌린다 — 성장할 때마다 옛 배열 세 개가
  남는다. `want` 를 제대로 주는 것이 실질적인 이득이다.
- **맵의 수명은 얼로케이터의 수명 안이다.**
