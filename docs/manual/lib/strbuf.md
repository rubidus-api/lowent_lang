# strbuf — 소유 문자열 버퍼 `str_buf` 와 널종단 `cstr` (`lib/strbuf.low`)

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 문자열을 **조금씩 이어 붙여** 만드는 버퍼다.

**언제 쓰나.** 경로를 조립할 때(디렉터리 + "/" + 이름), 메시지를 여러 조각으로 만들 때 쓴다.

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use strbuf .

var b strbuf.str_buf be strbuf.new .        rem 빈 버퍼를 만든다(바이트는 buf 가 갖는다)
let r result void strbuf.sb_error . be strbuf.append b buf "hello" .   rem buf 에 이어 붙인다
guard is_ok r . else return 1 .             rem 자리가 모자라면 오류가 값으로 온다
```

**꼭 알아 둘 것 하나.** 이 모듈에서 "버퍼" 는 두 조각이다 — 얼마나 썼는지를 기억하는
**상태**(`b`)와 실제 바이트가 들어가는 **자리**(`buf`)다. 둘은 딴 몸이므로 op 을 부를 때
언제나 함께 넘긴다. 왜 그렇게 갈라 놓았는지는 «설계 의도와 경계» 에 적었다.

**읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## 왜 있는가

이 모듈이 [`strings`](strings.md)와 어떻게 갈리는지, 그리고 왜 라이브러리가 스스로
메모리를 안 만드는지를 적는다.

이 모듈은 **문자열을 만들어 갈 때** 쓴다 — 조각을 이어 붙여 경로·메시지 같은 것을
조립한다. [`strings`](strings.md) 가 뷰(복사 없이 남의 바이트를 가리키기만 하는 것 =
읽기)라면 `strbuf` 는 **쓰기**다 — 이어 붙이고, 잘라 넣고, 키운다(RFC-0068 S5).

언어가 이것을 빌트인으로 넣지 않은 이유는 소스 상단에 있다: 소유는
capability(RFC-0011 — 권한을 값으로 들고 다니는 장치) 뒤에 오는 별도 단계이고, 버퍼를
어디서 얻을지는 호출자(`cap allocator`·`region`·정적 버퍼)의 일이다. 라이브러리는
**몰래 할당하지 않는다**(RFC-0043 D1 — ambient 힙 없음: 아무 데서나 쓸 수 있는 전역
메모리 창구가 없다는 뜻이다).

빌트인 op 증가는 **`cstr_of` 하나**다(슬라이스의 생 포인터는 언어에 없는 표현이라 리프).
나머지는 전부 언어가 이미 가진 것으로 지어졌다.

## 설계 의도와 경계

이 모듈의 성격은 "자리가 모자랄 때 무엇을 할지" 를 op 이름으로 고르게 한다는 것이다.
그리고 무엇을 일부러 안 하는지도 함께 적는다.

**넘칠 때 무엇을 할지는 호출자가 이름으로 고른다** — RFC-0052 의
`wrap_add`/`sat_add`/`chk_add` 와 정확히 같은 규율이다. 한 op 이 "상황 봐서 알아서"
하지 않는다:

| op | 범주 | 행동 |
|---|---|---|
| `append` | 전량-아니면-무 | 안 들어가면 **한 바이트도 안 쓰고** 거절한다 |
| `append_trunc` | 되는 만큼 | 쓴 개수를 낸다 — 잘렸다는 사실이 **값으로** 나온다 |
| `append_grow` | 키운다 | 더 큰 자리를 **호출자가 준다**(라이브러리가 할당 안 함) |

**정직하게 안 하는 것:**

- **바이트를 소유하지 않는다.** `str_buf` 구조체는 상태(`len`)만 들고, 바이트 버퍼는
  매 호출마다 인자로 받는다(`lib/out.low`·`file.low`·`fmt.low` 와 같은 규율).
- **자동 성장 없음.** 자리가 모자라도 라이브러리가 새 메모리를 구해 오지 않는다. 성장은
  `append_grow` 에 더 큰 새 버퍼를 **호출자가 주는 것**으로만 표현된다.
- **뷰 → cstr 직행 없음**(D4 비대칭): 뷰는 널을 약속 못 한다. cstr(끝을 0 바이트로
  표시하는 C 방식 문자열)를 만드는 유일한 안전 경로는 str_buf 를 거치는 것이고, 그
  경유가 할당을 보이게 한다.

## 자료구조

타입은 셋이고, 그중 실제로 쓰는 것은 `str_buf` 다. 여기서 가장 중요한 것은 **널 봉인**
규칙이다 — 버퍼가 늘 마지막 1 바이트를 비워 둔다는 약속이다.

```lowent
export newtype cstr unsafe_ptr u8 .

export enum sb_error do
  no_room
end

export struct str_buf do
  len u64 .
end
```

- `str_buf` — **상태만.** `len` 은 지금까지 쓴 바이트 수다. 버퍼 길이는 두 곳에 적으면
  어긋나므로(§0) 구조체에 없다 — 호출자가 준 `buf` 의 `len` 이 곧 용량이다.
- `sb_error` — 실패 이름 하나(`no_room` = 자리 없음). 실패가 트랩이 아니라 값으로 온다.
- `cstr` — **널종단**(끝을 0 바이트로 표시하는 C 방식) `char*` 를 가리키는 생 포인터의
  newtype(같은 표현에 다른 이름을 붙여 벽을 세운 타입). **FFI**(다른 언어 코드와 주고받는
  경계) 전용이다.
- **널 봉인(F3) 불변식:** 모든 append 는 **1 바이트를 예약**한다 — 내용 용량 =
  `len(buf) − 1`. 쓴 뒤 `buf[len] = 0` 으로 봉인한다(내용 끝 다음 자리에 항상 널을
  둔다는 뜻이다). 그래서 `as_cstr` 가 O(1)·무할당이다(널은 이미 거기 있다). 버퍼는
  **최소 1 바이트**여야 한다. 실용적으로는 **내용 n 바이트에 버퍼 n + 1 바이트**를
  잡는다고 외워 두면 된다.

## op 한눈에

급할 때 이 표만 봐도 된다. `append` 세 형제의 "실패 시" 칸이 서로 다른 것이 이 모듈의
요점이다.

부를 때는 모듈 접두사를 붙인다 — `strbuf.new`·`strbuf.append` 처럼.

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `new` | fn | `() → str_buf` | — |
| `as_str` | fn | `(b str_buf, buf slice u8) → slice u8` | — (뷰, 복사 아님) |
| `room` | fn | `(b str_buf, buf slice u8) → u64` | 빈 버퍼(len 0)면 언더플로 트랩 |
| `as_cstr` | unsafe proc | `(b str_buf, buf mut slice u8) → cstr` | `len+1 > len(buf)` 면 경계 밖 트랩 |
| `append` | proc | `(b mut str_buf, buf mut slice u8, s slice u8) → result void sb_error` | `error no_room`(버퍼 불변) |
| `append_trunc` | proc | `(b mut str_buf, buf mut slice u8, s slice u8) → u64` | — (쓴 개수 < `len s` 로 안다) |
| `append_grow` | proc | `(b mut str_buf, old mut slice u8, new_buf mut slice u8, s slice u8) → result void sb_error` | `error no_room`(원본 불변) |

## op 상세

**매개변수를 왜 받나 — 이 모듈의 규약.** `str_buf` 는 **길이와 상태만** 들고 있고 실제
바이트는 호출자의 `buf` 에 산다. 그래서 거의 모든 op 이 `b`(버퍼 상태)와 `buf`(바이트 자리)를
쌍으로 받는다. 둘을 나눈 이유는 버퍼를 어디에 마련할지(스택·아레나·힙)가 호출자의 결정이기
때문이다. 널종단(`cstr`)이 필요할 때 1바이트를 더 요구하는 것도 같은 정직함이다 — 그 1바이트는
공짜가 아니므로 숨기지 않는다.


op 하나하나를 시그니처와 함께 본다. 매개변수마다 "왜 이것이 필요한가" 를 한 줄로 붙였다 —
거의 모든 op 이 `b`(상태)와 `buf`(바이트 자리)를 함께 받는데, 그 이유가 여기서 반복된다.

#### new
```lowent
export fn new output str_buf .
```
`len 0` 인 빈 상태를 낸다.

- 매개변수가 없다 — 만드는 것은 상태뿐이고 바이트는 손대지 않기 때문이다.
- 버퍼는 따로 마련한다. 상태와 바이트가 처음부터 딴 몸이다.

#### as_str
```lowent
export fn as_str input b str_buf . input buf slice u8 . output slice u8 .
```
지금까지 쓴 바이트의 **뷰**를 낸다 — `subslice buf 0 len` 이다(복사가 아니다).

- `b` — "몇 바이트 썼나" 를 아는 쪽. 길이를 여기서 읽는다.
- `buf` — 그 바이트 자체가 든 자리. 둘이 딴 몸이라 둘 다 필요하다.
- 널은 뷰에 안 든다 — 길이가 진실이다.

#### room
```lowent
export fn room input b str_buf . input buf slice u8 . output u64 .
```
남은 내용 용량 = `len(buf) − 1 − len` 을 낸다. 널 봉인 자리 1 바이트가 빠져 있다.

- `b` — 이미 쓴 길이.
- `buf` — 전체 용량. "이만큼 더 넣을 수 있나" 를 append 전에 물을 때 쓴다.

#### as_cstr
```lowent
export unsafe proc as_cstr input b str_buf . input buf mut slice u8 . . output cstr . effects unsafe .
```
`len` 자리에 널을 봉인하고 base 포인터를 `cstr` 로 낸다 — **O(1)·무할당**.

- `b` — 널을 어디에 쓸지(= 지금까지의 길이)를 정한다.
- `buf` — `mut` 인 이유가 여기 있다: 널 1 바이트를 **실제로 쓴다**. 읽기 전용 뷰로는
  이 op 을 부를 수 없다.
- `cstr_of` 가 `effects unsafe` 라 이 op 도 unsafe 다 — 순수 코드(`effects none` 인 fn)
  에서는 못 부른다.
- 조건: `len + 1 ≤ len(buf)`(append 가 예약해 뒀다). 아니면 널 쓰기가 경계 밖 → 트랩이다.

#### append — 전량-아니면-무
```lowent
export proc append input b mut str_buf . input buf mut slice u8 . . input s slice u8 .
  output result void sb_error .
errors no_room .
```
들어가면 전부 쓰고, 안 들어가면 한 바이트도 안 쓴다.

- `b` — 상태. 쓴 만큼 `len` 이 늘어야 하므로 `mut` 이다.
- `buf` — 바이트가 실제로 들어갈 자리. 상태와 딴 몸이라 따로 받는다.
- `s` — 이어 붙일 내용.
- `len + len(s) + 1 ≤ len(buf)` 면 전부 쓰고 봉인하고 `ok`. 아니면 `error no_room` —
  **한 바이트도 안 쓴다**(길이도, 버퍼도 불변이다). 반쯤 쓰인 버퍼는 조용히 틀린
  내용이고, 그것은 거절보다 나쁘다.

#### append_trunc — 되는 만큼
```lowent
export proc append_trunc input b mut str_buf . input buf mut slice u8 . . input s slice u8 .
  output u64 .
```
남은 자리만큼만 쓰고 **쓴 개수를 돌려준다.**

- `b`·`buf`·`s` — `append` 와 같다.
- 반환값을 `len s` 와 비교하면 잘렸는지 안다 — "조용한 절단" 과 다른 점이 이것이다
  (사실이 값으로 나온다). 비교를 안 하면 잘린 줄 모른 채 지나간다.

#### append_grow — 키운다
```lowent
export proc append_grow input b mut str_buf . input old mut slice u8 . .
  input new_buf mut slice u8 . . input s slice u8 .
  output result void sb_error .
errors no_room .
```
옛 내용을 새 자리로 옮기고 거기에 이어 붙인다.

- `b` — 상태(옮긴 뒤 길이가 늘어난다).
- `old` — 지금까지의 바이트가 든 옛 자리. 옮길 원본이므로 필요하다.
- `new_buf` — 더 큰 새 자리. **라이브러리가 할당하지 않으므로 호출자가 마련해 넘긴다** —
  이 매개변수가 곧 "몰래 할당하지 않는다" 규율의 실물이다.
- `s` — 이어 붙일 내용.
- 옛 내용을 `new_buf` 로 옮기고 `s` 를 잇고 봉인하는 것까지가 한 동작이다. 새 자리조차
  모자라면 `error no_room` — **아무것도 안 옮긴다**(원자적). 성장하려면 더 큰 바이트를
  *가진 자* 여야 한다: 빌린 버퍼만 든 코드는 새 자리를 못 만들므로 이 op 을 유용하게
  부를 수 없다.

## 사용법과 예제

기본 흐름은 "상태를 만들고(`new`) → 버퍼와 함께 이어 붙이고(`append`) → 뷰로 읽는다
(`as_str`)" 이다. 아래 예제는 `impl/tests/vm_strbuf.low` 의 통과 코드를 근거로 한다.

`lib/strbuf.low` 의 모듈 이름은 `strbuf` 다.

```lowent
use strbuf from "../../lib/strbuf.low" .   rem 경로 직접 지정
use strbuf .                               rem from-생략 — 예약(std) 해소
```

세 범주가 서로 다르게 행동한다(픽스처 `vm_strbuf.low` 의 `fits`·`atomic_refuses`·
`trunc_writes_part` 를 한 op 으로 줄인 것). 버퍼는 호출자가 마련한다 — 여기서는 인자로
받는다:

```lowent
module demo .

use strbuf from "../../lib/strbuf.low" .

proc build input buf mut slice u8 . . output u64 . do
  var b strbuf.str_buf be strbuf.new .                  rem 상태(len 0)만 만든다
  let r1 result void strbuf.sb_error . be strbuf.append b buf "ab" .   rem 2 바이트 이어 붙인다
  guard is_ok r1 . else return 90 .                     rem 실패면 error no_room 이다
  guard eq (field b len) 2 . else return 91 .           rem 상태가 2 로 늘었다
  rem 봉인 확인 — len 자리(buf[2])에 널이 있다(cstr 수출이 O(1)인 이유)
  guard eq (index buf 2) 0 . else return 92 .
  rem 안 들어가면 거절되고 버퍼가 안 더럽혀진다 (버퍼 5 = 내용 4 + 널 1)
  let r2 result void strbuf.sb_error . be strbuf.append b (subslice buf 0 5) "xyz" .
  guard is_error r2 . else return 93 .                  rem 2 + 3 + 널 1 > 5 → no_room
  guard eq (field b len) 2 . else return 94 .           rem 길이 불변 — 한 바이트도 안 썼다
  rem 같은 상황에서 trunc 는 되는 만큼(2) 쓰고 2 를 돌려준다
  let n u64 be strbuf.append_trunc b (subslice buf 0 5) "xyz" .
  guard eq n 2 . else return 95 .                       rem 달란 3, 쓴 2 — 비교로 절단을 안다
  return field b len .                                  rem 4 (= "ab" 2 + trunc 로 쓴 2)
end
```

성장은 새 자리를 호출자가 주는 것으로 표현된다(`vm_strbuf.low` 의 `grow_moves` 기반):

```lowent
proc grow input small mut slice u8 . . input big mut slice u8 . . output u64 . effects none . do
  var b strbuf.str_buf be strbuf.new .
  let r1 result void strbuf.sb_error . be strbuf.append b small "ab" .   rem 작은 자리에 2 바이트
  guard is_ok r1 . else return 90 .
  let r2 result void strbuf.sb_error . be strbuf.append b small "xyz" .  rem 여긴 더 안 들어간다
  guard is_error r2 . else return 91 .                  rem 예상대로 no_room
  rem ★ 더 큰 자리를 내가 마련해 넘긴다 — 라이브러리는 할당하지 않는다
  let r3 result void strbuf.sb_error . be strbuf.append_grow b small big "xyz" .
  guard is_ok r3 . else return 92 .
  guard eq (index big 0) 97 . else return 93 .          rem 'a' — 옛 내용이 옮겨졌다
  guard eq (index big 2) 120 . else return 94 .         rem 'x' — 새 내용이 이어졌다
  return field b len .                                  rem 5 ("abxyz")
end
```

실전 예는 `impl/tests/prog/walk.low` 에 있다 — 디렉터리 경로를
`strbuf.append b pathbuf dpath` → `strbuf.append b pathbuf "/"` → `strbuf.append b pathbuf name`
으로 이어 붙이고 `strbuf.as_str b pathbuf` 로 뷰를 꺼내 파일 API 에 넘긴다.

## 반례 — 이렇게 쓰면 안 된다

실제로 자주 밟는 잘못된 코드들이다. 각 반례에 **증상**을 적었다. 이 모듈의 실패는
트랩보다 `error no_room` 쪽이 많다 — 반환값을 안 보면 조용히 짧은 문자열이 남는다.

**✗ 순수 코드에서 `as_cstr`:**

```lowent
fn f input b strbuf.str_buf . input buf mut slice u8 . . output u64 . do
  let p strbuf.cstr be strbuf.as_cstr b buf .   rem ✗ unsafe proc 을 fn 에서 부른다
  return 0 .
end
```

증상: **컴파일 에러 E-EFFECT-CALC** — 실행까지 가지 않는다.

```
E-EFFECT-CALC: effects unsafe 를 effects none 안에서 부를 수 없다
```

cstr 를 만드는 것은 언제나 널종단 보증이고, 그 보증은 unsafe 경계 안에서만 선다(D4).
부르는 쪽을 `unsafe proc` 으로 바꾼다.

**✗ 널 봉인 자리를 잊은 버퍼 크기:**

```lowent
proc f input buf mut slice u8 . . output u64 . effects none . do
  var b strbuf.str_buf be strbuf.new .
  rem buf 가 정확히 2 바이트라면 "ab" 조차 안 들어간다 (2 + 0 + 널 1 > 2)
  let r result void strbuf.sb_error . be strbuf.append b buf "ab" .
```

증상: 첫 append 부터 **`error no_room`** 이다 — 아무것도 안 넣었는데 거절만 온다.
알아채는 법: `is_ok` 가 처음부터 false 이고 `field b len` 이 0 에서 안 움직인다.
내용 n 바이트에는 버퍼 **n + 1** 바이트가 필요하다.

**✗ `result` 를 안 보고 버퍼를 읽기:**

```lowent
let r result void strbuf.sb_error . be strbuf.append b buf "hello" .
let v slice u8 be strbuf.as_str b buf .   rem ✗ r 을 안 보고 읽는다
```

증상: 트랩도 에러도 없다. 버퍼 내용이 깨진 것도 아니다(거절은 버퍼를 안 건드린다) —
다만 **기대한 것이 안 들어간 짧은 문자열**이 나온다. 알아채는 법: 출력 문자열이 예상보다
짧거나 조각 하나가 통째로 비어 있다. `guard is_ok r . else …` 가 먼저다.

**✗ 상태와 버퍼를 섞어 넘기기:**

```lowent
let r result void strbuf.sb_error . be strbuf.append b buf1 "ab" .
let v slice u8 be strbuf.as_str b buf2 .   rem ✗ 길이는 buf1 것인데 바이트는 buf2 것이다
```

증상: 에러 없이 **엉뚱한 바이트의 뷰**가 나온다 — `b` 의 길이만큼 `buf2` 의 앞부분을
읽어 오기 때문이다. 알아채는 법: 쓰레기 문자나 옛 내용이 섞여 나온다. 하나의 `str_buf`
에는 하나의 버퍼만 짝지어 쓴다.

## 주의사항

초보자가 자주 걸리는 함정을 모았다. 대부분 "상태와 바이트가 딴 몸" 이라는 설계와
"널 자리 1 바이트" 에서 나온다.

- **op 은 모듈 접두사로 부른다** — `strbuf.new`·`strbuf.append`. 옛 `new` 같은
  `sb_` 접두사 이름은 없다(타입 `str_buf`·`sb_error`·`cstr` 는 그대로다).
- **`b` 와 `buf` 는 짝이다.** 상태(`len`)와 바이트를 따로 드는 대가로, 다른 버퍼를 섞어
  넘기면 어긋난다 — `as_str b 다른buf` 는 엉뚱한 바이트의 뷰다. 하나의 `str_buf` 에는
  하나의 버퍼(또는 grow 로 옮긴 그 후계)만 쓴다.
- **버퍼는 내용보다 1 바이트 크게 잡는다.** 널 봉인 자리 때문이다. 이것을 잊으면 "왜
  딱 맞는데 안 들어가지" 로 헤매게 된다.
- **`append` 의 거절은 조용하다.** 트랩이 아니라 값(`error no_room`)이므로, `result` 를
  안 보면 프로그램이 그냥 계속 돈다.
- **grow 뒤에는 옛 버퍼를 버린다.** `append_grow` 가 `ok` 를 낸 순간 진실은
  `new_buf` 에 있다 — 이후 호출에 `old` 를 넘기면 안 된다.
- **`as_cstr` 는 스냅숏이 아니다.** base 포인터를 그대로 내므로, 그 뒤 append 하면
  가리키는 내용이 바뀐다. FFI 호출 직전에 만들고 바로 쓴다.
- `room` 은 빈 버퍼(길이 0)에서 `0 − 1` 언더플로로 트랩한다 — 버퍼는 최소 1 바이트.

---

[← 목차](../README.md)
