# <a id="mod-utf8"></a>`utf8` — UTF-8 코드포인트 순회와 검증

소스

`lib/utf8.low`

층

L0 — 순수 계산

권한

없음

UTF-8 바이트열을 **코드포인트 단위로** 훑고 검사한다. 코드포인트는 문자 하나에 유니코드가 붙인 번호다(`'한'` = U+D55C = 54620). UTF-8 에서 한 코드포인트는 1 … 4 바이트를 먹는다 — 번호 하나가 바이트 하나가 아니다. 한글과 이모지가 섞인 문자열의 글자 수를 세거나, 입력이 올바른 UTF-8 인지 볼 때 쓴다.

```lowent
use utf8 .

let c option u64 utf8.decode s 0 .
guard is_some c else return 1 .
```

`0` 은 “첫 글자” 가 아니라 **바이트 0 번지**다. 이 모듈의 위치는 모두 바이트 오프셋이고, 글자 단위 전진은 `next_start` 가 한다.

**왜 검증이 필요한가.** UTF-8 은 가변 길이라 아무 바이트열이나 올바르지 않다. 시퀀스가 도중에 잘리거나, 이어지는 바이트가 엉뚱하거나, 같은 글자를 필요보다 긴 바이트로 적은 **과길이 인코딩**일 수 있다. 과길이가 특히 위험하다 — 같은 코드포인트를 두 바이트열로 쓸 수 있게 되면 “이 문자열에 `/` 가 있는가” 같은 필터가 거짓말을 한다. 보안 필터 우회의 고전적인 통로다.

**왜 라이브러리인가.** `str` 은 바이트이고(9장), UTF-8 을 타입 불변식으로 강제하지 않았다. 인코딩은 위층의 선택이고 이 모듈이 그 층이다. Rust 는 `str` 에 UTF-8 을 강제해 글자 순회를 공짜로 얻는 대신 모든 생성 경로에 검증을 물린다. 여기서는 검증을 **원할 때만** 치르고, 그 덕에 바이트 자르기 (`subslice`)는 실패할 수 없다. 기본 연산은 하나도 늘지 않았다.

## <a id="sx1"></a>설계와 경계

- **잘못된 UTF-8 은 값으로 답한다.** `none` 이 곧 “여기서부터 UTF-8 이 아니다” 다. 멈추지도, 치환 문자(U+FFFD)를 넣지도 않는다 — 치환은 되돌릴 수 없는 손실이라 호출자가 고를 수 없게 만든다.
- **거절하는 것** — 과길이 인코딩, 서로게이트(U+D800 … DFFF), U+10FFFF 초과, 잘린 시퀀스, 선두 자리의 이어지는 바이트. 서로게이트는 UTF-16 이 큰 문자를 두 칸에 나눠 담을 때 쓰는 부품이라 UTF-8 안에는 자리가 없다([`utf16`](sec62.md#mod-utf16)).
- **부분 답을 주지 않는다.** `count_chars` 는 무효 바이트를 만나면 `none` 이다.
- **짓지 않은 것** — 정규화, 로케일 비교, 글자 묶음(grapheme cluster, [`term`](sec66.md#mod-term)), 대소문자 변환, 인코딩(코드포인트 → 바이트). 순회와 검증만이다.

| **코드포인트 범위** | **바이트 수** | **선두 바이트** | **이어지는 바이트** |
|---|---|---|---|
| U+0000 … U+007F | 1 | `0xxxxxxx` | 없음 |
| U+0080 … U+07FF | 2 | `110xxxxx` | `10xxxxxx` × 1 |
| U+0800 … U+FFFF | 3 | `1110xxxx` | `10xxxxxx` × 2 |
| U+10000 … U+10FFFF | 4 | `11110xxx` | `10xxxxxx` × 3 |

*표 50.1 — UTF-8 의 바이트 배치*

표에서 세 가지가 나온다. 선두 바이트만 보면 길이를 안다(`seq_len`). `10xxxxxx` 가 선두 자리에 오면 글자의 시작이 아니다. 길이마다 담을 수 있는 최소 코드포인트(2 바이트는 128 이상, 3 바이트는 2048 이상, 4 바이트는 65536 이상)보다 작은 값이 나오면 과길이다 — `decode` 가 이 문턱으로 거른다.

## <a id="sx2"></a>op 한눈에

| **op** | **시그니처** | **안 될 때** |
|---|---|---|
| `is_cont` | `(b u8) → bool` | 실패 없음 |
| `seq_len` | `(b u8) → u64` | 선두 바이트가 아니면 `0` |
| `decode` | `(s slice u8, at u64) → option u64` | 무효·잘림·범위 밖이면 `none` |
| `next_start` | `(s slice u8, at u64) → option u64` | 무효·잘림·끝이면 `none` |
| `count_chars` | `(s slice u8) → option u64` | 무효 바이트가 하나라도 있으면 `none` |
| `is_valid` | `(s slice u8) → bool` | 실패 없음 |

*표 50.2 — `utf8` 의 op — 모두 `fn` · `effects none`*

## <a id="sx3"></a>op 상세

이 모듈은 상태를 들지 않으므로 어디를 어디서부터 읽을지는 모두 호출자가 준다.

- **`is_cont`** — 이어지는 바이트인가(`10xxxxxx`, 곧 `eq (bit_and b 192) 128`). 앞뒤 문맥이 필요 없는 판정이라 바이트 하나를 받는다.
- **`seq_len`** — 선두 바이트로 그 코드포인트의 바이트 길이(1 … 4)를 안다. 길이 정보는 선두 바이트의 앞쪽 비트에만 있다. **0 은 길이가 아니라 오류 신호**다 — 그대로 더하면 커서가 움직이지 않아 반복이 끝나지 않는다.
- **`decode`** — 바이트 오프셋 `at` 에서 코드포인트 하나를 읽어 **값**을 낸다. `at` 이 끝을 넘음, 선두가 이어지는 바이트, 시퀀스가 슬라이스 밖으로 잘림, 이어지는 바이트가 `10xxxxxx` 가 아님, 과길이, 서로게이트, U+10FFFF 초과면 `none` 이다. 한 글자가 최대 4 바이트에 걸치므로 슬라이스 전체를 받고, `len s` 가 잘림의 판정 근거다. `at` 은 반드시 글자의 **시작**이어야 한다 — `next_start` 가 준 위치만 넣는 것이 안전한 습관이다. 길이는 주지 않는다.
- **`next_start`** — 다음 코드포인트의 **시작 위치**. 무효면 `none` 이다 — 건너뛰며 뭉개지 않는다. 선두·길이·잘림만 보고 값 범위 검사는 `decode` 의 몫이다. 돌려받은 값을 그대로 다음 `at` 으로 쓰는 것이 순회 한 걸음이다.
- **`count_chars`** — 전체를 훑어 코드포인트 수를 낸다. 무효 바이트를 만나면 `none`. “전체” 가 계약이라 시작 위치를 받지 않는다. 비용은 O(바이트 수)다.
- **`is_valid`** — 전체가 올바른 UTF-8 인가. “검증은 따로 된 op 이지 타입 불변식이 아니다” 의 실물이고, 구현은 `is_some (count_chars s)` 다. 글자 수까지 필요하면 `count_chars` 를 한 번만 부르는 편이 낫다.

## <a id="sx4"></a>쓰는 법

먼저 검증하고, `decode` 로 값을 읽고, `next_start` 로 전진한다. `"한"` 은 바이트 3, 글자 1, 값 54620(U+D55C)이다.

```lowent
module cpdump .

use utf8 .
use fmt .

proc main
  input out cap io .
  input al  cap allocator .
  output u8 .
  effects alloc io .
do
  let s slice u8 "a한😀" .
  guard utf8.is_valid s else return 65 .

  let g option mut slice u8 alloc_bytes al capacity 128 .
  guard is_some g else return 70 .
  let buf mut slice u8 some_value g .
  var pos u64 0 .

  var i u64 0 .
  while lt i (len s) do
    let c option u64 utf8.decode s i .
    guard is_some c else return 66 .
    let a option u64 fmt.put_str buf pos "U+" .
    guard is_some a else return 71 .
    let b option u64 fmt.put_hex buf (some_value a) (some_value c) .
    guard is_some b else return 72 .
    let d option u64 fmt.put_nl buf (some_value b) .
    guard is_some d else return 73 .
    set pos (some_value d) .
    let nx option u64 utf8.next_start s i .
    guard is_some nx else return 67 .
    set i (some_value nx) .
  end

  let w u64 write_out out 1 (subslice buf 0 pos) .
  return 0 .
end
```

출력은 `U+61` · `U+d55c` · `U+1f600` 세 줄이다. 한 바퀴에서 `i` 는 `a`(1 바이트) → `한`(3 바이트) → `😀`(4 바이트) 를 지나며 0 → 1 → 4 → 8 로 뛴다. **`add i 1` 로는 전진하지 않는다** — 둘째 바퀴에서 글자 한가운데를 가리키게 된다. 글자 수만 필요하면 `let n option u64 utf8.count_chars s .` 한 줄이다.

## <a id="sx5"></a>반례

> **반례. `none` 을 치환 문자로 때우고 계속 간다**
>
> > `next_start` 가 실패했을 때 `set i (add i 1)` 로 한 바이트 건너뛰면 무효 입력이 조용히 통과하고, 그 위의 비교와 필터가 거짓 전제 위에 선다. 오류는 없다 — **조용히 틀린 답**이고 글자 수만 슬쩍 는다. 무효 입력을 일부러 넣어 `is_valid` 가 `false` 인데도 뒤쪽 처리가 도는지 본다. 고치는 법은 `none` 에서 멈추는 것뿐이다.

> **반례. 바이트 오프셋을 글자 번호로 여긴다**
>
> > `decode s 1` 은 “둘째 글자” 가 아니라 “바이트 1 에서 읽기” 다. `"한글"` 의 바이트 1 은 시퀀스 한가운데라 `none` 이다. `is_valid` 는 `true` 인데 특정 위치만 `none` 이면 그 위치가 글자 경계가 아니다.

> **반례. `count_chars` 의 `none` 을 0 으로 다룬다**
>
> > `none` 은 “글자 없음” 이 아니라 “무효 UTF-8” 이다. 빈 슬라이스는 `some 0` 이다. 둘을 합치면 깨진 입력과 글자가 없는 입력을 영영 구별할 수 없다. 검사 없이 `some_value` 로 꺼내면 실행 중 `E-VM-NONE` 으로 멈춘다.

> **반례. 검증하지 않은 입력을 `subslice` 로 자른 뒤 올바르다고 여긴다**
>
> > 바이트 자르기는 늘 성공하지만 시퀀스 한가운데를 자를 수 있다. 원본은 멀쩡한데 조각에서만 `is_valid` 가 `false` 면 자른 자리가 글자 한가운데였다. 자르는 그 자리에서는 아무도 경고하지 않고, 한참 뒤 출력이 깨져야 드러난다. 글자 경계로 자르려면 `next_start` 가 준 위치에서만 자른다.

## <a id="sx6"></a>주의

- **`len s` 는 바이트 수다.** 글자 수는 `count_chars` 만이 안다. “최대 10 글자” 를 `len` 으로 재면 어긋난다(`"한글"` 은 `len` 6, 글자 2).
- **커서는 `next_start` 로 민다.** 1 씩 밀면 이어지는 바이트를 선두로 읽어 `none` 이 나오거나 글자 수가 부푼다.
- **`decode` 와 `next_start` 의 `none` 은 판정 범위가 다르다.** `next_start` 는 구조만 보고 값 범위(과길이·서로게이트·상한)는 보지 않는다. 엄밀한 순회는 둘을 함께 쓰거나 먼저 `is_valid` 로 전체를 검증한다.
- **`count_chars`·`is_valid` 는 O(바이트 수)다.** 반복 조건 안에서 매번 부르면 전체가 O(n²) 가 된다 — 한 번 세어 들고 다닌다.
- **코드포인트 단위다, 사람이 세는 글자 단위가 아니다.** 조합 문자와 이모지 시퀀스는 여러 코드포인트로 센다(국기 이모지 하나가 코드포인트 둘).
- **모두 `effects none` 이다.** `fn`·계약·`comptime` 어디서든 부를 수 있다.

---

[← 이전](sec60.md) · [목차로](README.md) · [다음 →](sec62.md)
