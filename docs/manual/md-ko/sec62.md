# <a id="mod-utf16"></a>`utf16` — UTF-16 서로게이트 산술

소스

`lib/utf16.low`

층

L0 — 순수 계산

권한

없음

UTF-16 의 서로게이트 쌍을 코드포인트로 바꾸고 되돌린다. Windows API·Java·JavaScript 처럼 문자열을 16 비트 칸(unit)으로 드는 세계와 값을 주고받을 때 쓴다. `slice u16` 이 곧 UTF-16 문자열이고, 한 원소가 한 칸이다.

```lowent
use utf16 .

let c option u64 . be utf16.decode s 0 .
guard is_some c . else return 1 .
```

`0` 은 “첫 글자” 가 아니라 **0 번 칸**이다. 이 모듈의 위치는 모두 칸 번호이고, 글자 단위 전진은 `next_start` 가 한다(짝이면 두 칸을 뛴다).

**왜 서로게이트가 생겼는가.** UTF-16 은 처음에 “문자 하나 = 16 비트 한 칸” 으로 모든 글자를 담으려 했다. 유니코드가 65,536 자를 넘어 자라자 값 구간 U+D800 … U+DFFF 를 문자로 쓰지 않기로 비워 두고, 그 구간의 두 칸을 조합해 큰 코드포인트를 적기로 했다. 상위 1024 가지 × 하위 1024 가지 = 1,048,576 가지로 U+10000 … U+10FFFF 를 딱 덮는다. 그래서 UTF-16 에서는 **칸 하나를 읽는 것과 문자 하나를 읽는 것이 다르다**.

**왜 라이브러리인가.** `slice u16`·`slice u32` 가 읽기·쓰기·색인을 이미 모두 한다. 권한도 필요 없고 언어에 없는 표현도 필요 없다. 그래서 새 타입도 새 낱말도 내장 연산도 없이 라이브러리가 되었다. 남는 진짜 일은 **서로게이트 산술**과 그 실패 방식뿐이다. UTF-32 라이브러리는 없다 — `slice u32` 의 한 칸이 곧 코드포인트라 필요한 것은 검증 한 줄(`cp_valid`)이다.

## <a id="sx1"></a>설계와 경계

- **하는 것** — 코드포인트와 UTF-16 칸 사이의 변환, 그리고 틀린 것의 거절. 짝 없는 상위 서로게이트(D800 … DBFF 뒤에 하위가 오지 않음), 짝 없는 하위 서로게이트(DC00 … DFFF 가 혼자 옴), 범위 밖 코드포인트(> U+10FFFF)는 `none` 이다.
- **[`utf8`](sec61.md#mod-utf8) 과 짝이 맞는다.** `utf8` 이 UTF-8 안의 서로게이트를 거절하므로, 서로게이트는 **오직 UTF-16 안에서, 오직 짝으로만** 나타난다. 두 라이브러리가 함께 그 불변을 지킨다.
- **하지 않는 것** — 정규화(NFC·NFD), 대소문자, 조합 문자, 바이트 순서(BOM·빅/리틀 엔디언). `slice u16` 은 이미 기계 차례다.
- **모두 `effects none` 이다.** `put` 은 `proc` 이지만 자기 상태가 아니라 호출자가 건넨 버퍼에만 쓰므로 역시 `effects none` 이다.

BMP(U+0000 … U+FFFF, 서로게이트 구간 제외)의 코드포인트는 한 칸, 값 그대로다. U+10000 … U+10FFFF 는 두 칸이다 — `v = c − 0x10000` 으로 20 비트를 만들고 상위 칸 `hi = 0xD800 + v/1024`, 하위 칸 `lo = 0xDC00 + v%1024` 로 가른다. 되돌리는 산술은 `c = 0x10000 + (hi − 0xD800) × 1024 + (lo − 0xDC00)` 이다.

| **칸 값(10 진)** | **정체** | **뜻** |
|---|---|---|
| 55296 … 56319 | 상위 서로게이트 | 두 칸짜리 문자의 **앞** 조각 — 뒤에 하위가 와야 한다 |
| 56320 … 57343 | 하위 서로게이트 | 두 칸짜리 문자의 **뒤** 조각 — 혼자 오면 틀렸다 |
| 그 밖 | 평범한 BMP 칸 | 그 값이 곧 코드포인트다 |

*표 50.1 — 칸 하나의 정체*

코드포인트 상한은 1114111(U+10FFFF)이다. 서로게이트 값 자체는 코드포인트가 아니고 `cp_valid` 가 거절한다.

## <a id="sx2"></a>op 한눈에

| **op** | **시그니처** | **안 될 때** |
|---|---|---|
| `is_high` | `fn (u u64) → bool` | 실패 없음 |
| `is_low` | `fn (u u64) → bool` | 실패 없음 |
| `cp_valid` | `fn (c u64) → bool` | 실패 없음(거짓이 곧 답) |
| `units` | `fn (c u64) → u64` | 실패 없음(1 또는 2) |
| `unit_hi` | `fn (c u64) → option u64` | 유효하지 않은 코드포인트면 `none` |
| `unit_lo` | `fn (c u64) → option u64` | 무효 코드포인트나 BMP(칸 하나)면 `none` |
| `put` | `proc (dst mut slice u16, at u64, c u64) → option u64` | 무효 코드포인트나 자리 부족이면 `none` |
| `decode` | `fn (s slice u16, at u64) → option u64` | 범위 밖이거나 짝이 맞지 않으면 `none` |
| `next_start` | `fn (s slice u16, at u64) → option u64` | 끝이거나 망가진 자리면 `none` |
| `count_chars` | `fn (s slice u16) → option u64` | 망가진 자리를 만나면 `none` |
| `is_valid` | `fn (s slice u16) → bool` | 실패 없음 |

*표 50.2 — `utf16` 의 op*

실무에서 가장 많이 쓰는 것은 `put`(쓰기)과 `decode` + `next_start`(순회)다.

## <a id="sx3"></a>op 상세

- **`is_high` · `is_low`** — 칸 하나의 값이 상위(D800 … DBFF)·하위(DC00 … DFFF) 서로게이트인가. 값 범위만으로 끝나는 판정이다.
- **`cp_valid`** — 코드포인트로서 유효한가. `c > 1114111` 이거나 서로게이트 구간(55296 … 57343)이면 `false`. 바깥에서 들어온 수를 쓰기 전에 한 번 거르는 문이다.
- **`units`** — 코드포인트 하나가 먹는 칸 수. `c ≥ 65536` 이면 2, 아니면 1. 버퍼 크기를 미리 셈할 때 쓴다. **유효성은 보지 않는다.**
- **`unit_hi`** — 상위 칸. BMP 면 `some c`, 두 칸이면 `some (0xD800 + v/1024)`, 유효하지 않으면 `none`.
- **`unit_lo`** — 하위 칸. BMP 면 `none` — 여기서는 오류가 아니라 “칸이 하나뿐” 이라는 뜻이다. 두 칸이면 `some (0xDC00 + v%1024)`. 무효 코드포인트도 `none`.
- **`put`** — 코드포인트를 `dst` 의 `at` 부터 써 넣고 **다음에 쓸 위치**(`some (at+1)` 또는 `some (at+2)`)를 돌려준다. `decode`·`next_start` 와 같은 위치 축이다. 몇 칸 썼는지가 필요하면 `units c` 가 답한다. 유효하지 않거나 `at + units c > len dst` 면 `none` 이고 **한 칸도 쓰지 않는다**. 스스로 메모리를 만들지 않으므로 쓸 자리를 밖에서 받는다.
- **`decode`** — `s` 의 `at` 에서 코드포인트 하나를 읽는다. 평범한 칸이면 그 값, 상위 서로게이트면 바로 다음 칸이 하위인지 확인하고 합친다. `at ≥ len s`, 하위가 혼자 옴, 상위 뒤에 하위가 오지 않음, 상위로 끝남(잘림)이면 `none`. 짝이 두 칸이라 슬라이스 전체를 받는다. `at` 은 문자의 **시작** 칸이어야 한다.
- **`next_start`** — 다음 코드포인트의 자리. `decode` 가 성공해야 하고, 상위 서로게이트에서 시작했으면 `some (at+2)`, 아니면 `some (at+1)`. 짝을 두 칸으로 건너뛰는 셈을 호출자가 하지 않게 해 준다.
- **`count_chars`** — 전체를 훑어 코드포인트 수를 센다. 망가진 자리를 만나면 **수를 내지 않는다** — 틀린 수를 내느니 내지 않는다. O(칸 수).
- **`is_valid`** — `count_chars` 가 `some` 인가. 짝의 반쪽만 떼어 보면 무효로 보이므로, 유효성은 부분으로 나눠 묻지 않는다.

## <a id="sx4"></a>쓰는 법

쓰기는 `put` 을 부르고 돌려받은 다음 위치에서 이어 쓴다. 읽기는 `decode` 로 값을 읽고 `next_start` 로 전진한다. 어느 쪽이든 커서는 호출자가 든다.

```lowent
module ex_utf16 .

use utf16 as u .

proc round_trip input buf mut slice u16 . . output u64 . effects none . do
  guard ge (len buf) 4 . else return 90 .
  rem '가'(U+AC00) --- BMP 라서 한 칸. put 은 다음에 쓸 위치를 돌려준다
  let a option u64 . be u.put buf 0 44032 .
  guard is_some a . else return 1 .
  guard eq (some_value a) 1 . else return 2 .

  rem U+1F4A9 --- BMP 밖이라 두 칸(서로게이트 짝)으로 갈라진다
  let b option u64 . be u.put buf (some_value a) 128169 .
  guard is_some b . else return 3 .
  guard eq (some_value b) 3 . else return 4 .
  guard eq (index buf 1) 55357 . else return 5 .
  guard eq (index buf 2) 56489 . else return 6 .

  rem 다시 하나로 읽는다 --- decode 가 짝을 합친다
  let rb option u64 . be u.decode buf 1 .
  guard is_some rb . else return 7 .
  guard eq (some_value rb) 128169 . else return 8 .

  rem 'A' + 이모지(짝) + '가' = 칸 넷, 문자 셋
  set (index buf 0) 65 .
  set (index buf 3) 44032 .
  let n option u64 . be u.count_chars (subslice buf 0 4) .
  guard is_some n . else return 9 .
  guard eq (some_value n) 3 . else return 10 .
  return 42 .
end
```

상위 칸 `buf[1]` 은 55357(0xD83D), 하위 칸 `buf[2]` 는 56489(0xDCA9)다. 순회는 `next_start` 로 한다.

```lowent
var i u64 be 0 .
while lt i (len s) . do
  let c option u64 . be u.decode s i .
  guard is_some c . else return 80 .
  let nx option u64 . be u.next s i .
  guard is_some nx . else return 81 .
  set i (some_value nx) .
end
```

BMP 면 +1, 짝이면 +2 — 직접 세지 않는다. `none` 이면 멈춘다(끝인지 망가짐인지는 먼저 `is_valid` 를 재면 갈린다).

## <a id="sx5"></a>반례

> **반례. 짝이 맞지 않는 입력을 `decode` 로 통과시키려 한다**
>
> > ```lowent
> > set (index buf 0) 55357 .          rem ✗ 상위 0xD83D 뒤에
> > set (index buf 1) 65 .             rem   하위가 아니라 'A' 가 온다
> > let a option u64 . be u.decode buf 0 .
> > ```
> >
> > `none` 이다. 하위 서로게이트가 혼자 오거나(`decode buf 2`, 값 56489), 상위 서로게이트로 끝나도(`decode (subslice buf 0 4) 3`) 마찬가지다. 셋 다 `guard is_some` 자리에서 바로 걸린다. 조용히 통과시키면 그 뒤가 모두 틀리므로 **거절이 곧 이 라이브러리의 값**이다. 온전한 짝은 여전히 통과한다.

> **반례. 두 칸짜리를 마지막 한 칸에 넣으려 한다**
>
> > `u.put buf (sub (len buf) 1) 128169` 는 `none` 이고 **그 칸은 더럽혀지지 않는다**. 반쯤 쓰다 말았나 걱정하지 않아도 된다.

> **반례. 서로게이트 값을 코드포인트로 쓰려 한다**
>
> > `u.cp_valid 55357` 은 `false` 이고, 그래서 `put`·`unit_hi`·`unit_lo` 가 모두 `none` 이다. 이 `none` 들을 검사 없이 `some_value` 로 꺼내면 실행 중 `E-VM-NONE` 으로 멈춘다 — 번역은 통과하므로 `guard` 가 먼저다.

## <a id="sx6"></a>주의

- **`len s` 는 칸 수다.** 문자 수는 `count_chars` 만이 안다. 이모지 하나가 칸 둘을 먹는다.
- **짝 한가운데서 자르지 않는다.** 상위와 하위 사이를 `subslice` 로 가르면 양쪽 조각이 모두 무효가 된다. 자를 자리는 `next_start` 가 준 값이어야 한다.
- **커서는 `next_start` 로 민다.** 1 씩 밀면 짝의 하위 칸을 문자 시작으로 읽어 `none` 이 나오거나 문자 수가 부푼다.
- **비용.** `decode`·`next_start`·`put` 은 O(1), `count_chars`·`is_valid` 는 O(칸 수)다. 반복 조건 안에서 매번 세지 않는다.
- **`unit_lo` 의 `none` 은 두 뜻이 겹친다**(무효 코드포인트 · BMP). 구분이 필요하면 `cp_valid` 를 먼저 묻는다. 조합해 쓸 때는 실패가 `none` 하나로 정리되는 `put` 이 낫다.
- **바이트 직렬화는 밖이다.** 파일이나 네트워크에서 온 **바이트**를 `slice u16` 으로 만드는 일은 호출자 몫이다.

---

[← 이전](sec61.md) · [목차로](README.md) · [다음 →](sec63.md)
