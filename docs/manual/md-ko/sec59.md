# <a id="mod-strbuf"></a>`strbuf` — 소유 문자열 버퍼와 널 종단 `cstr`

소스

`lib/strbuf.low`

층

L0 — 순수 계산(호출자의 버퍼)

권한

없음 · `as_cstr` 만 `unsafe`

문자열을 **조금씩 이어 붙여** 만든다. 디렉터리와 “/” 와 이름으로 경로를 조립하거나, 메시지를 여러 조각으로 만들 때 쓴다. [`strings`](sec58.md#mod-strings) 가 뷰(읽기)라면 `strbuf` 는 쓰기다 — 이어 붙이고, 되는 만큼 넣고, 키운다.

```lowent
use strbuf .

var b be strbuf.str_buf strbuf.new .
let r be result void strbuf.sb_error strbuf.append b buf "hello" .
guard is_ok r . else return 1 .
```

이 모듈에서 “버퍼” 는 두 조각이다 — 얼마나 썼는지 기억하는 **상태** `b` 와 실제 바이트가 들어가는 **자리** `buf`. 둘은 딴 몸이므로 op 을 부를 때 늘 함께 넘긴다. 버퍼를 어디서 얻을지(`cap allocator`·영역·정적 버퍼)는 호출자의 일이고, 라이브러리는 **몰래 할당하지 않는다**. 늘어난 내장 연산은 슬라이스의 생 포인터를 내는 `cstr_of` 하나다.

## <a id="sx1"></a>설계와 경계

**자리가 모자랄 때 무엇을 할지는 호출자가 이름으로 고른다.** `wrap_add`·`sat_add`·`chk_add` 와 같은 규율이다. 한 op 이 상황 봐서 알아서 하지 않는다.

| **op** | **갈래** | **행동** |
|---|---|---|
| `append` | 전량 아니면 무 | 안 들어가면 **한 바이트도 쓰지 않고** 거절한다 |
| `append_trunc` | 되는 만큼 | 쓴 개수를 낸다 — 잘렸다는 사실이 **값**으로 나온다 |
| `append_grow` | 키운다 | 더 큰 자리를 **호출자가 준다** |

*표 50.1 — 이어 붙이기의 세 갈래*

- **바이트를 소유하지 않는다.** `str_buf` 는 상태(`len`)만 들고, 바이트 버퍼는 매 호출마다 인자로 받는다. 용량을 두 곳에 적으면 어긋나므로 구조체에 없다 — 호출자가 준 `buf` 의 `len` 이 곧 용량이다.
- **저절로 자라지 않는다.** 성장은 `append_grow` 에 더 큰 버퍼를 호출자가 주는 것으로만 표현된다.
- **뷰에서 `cstr` 로 바로 가는 길이 없다.** 뷰는 끝의 0 바이트를 약속하지 못한다. `cstr` 를 만드는 유일한 안전한 길이 `str_buf` 를 거치는 것이고, 그 경유가 할당을 보이게 한다.

> **널 봉인**
>
> > 모든 이어 붙이기는 **1 바이트를 남겨 둔다** — 내용 용량은 `len(buf) − 1` 이다. 쓴 뒤 `buf[len] = 0` 으로 봉인하므로 `as_cstr` 가 O(1)·무할당이다. 버퍼는 최소 1 바이트여야 하고, 실용적으로는 **내용 n 바이트에 버퍼 n + 1 바이트**로 외워 둔다.

타입은 셋이다. `str_buf`(상태 `len u64`), `sb_error`(실패 이름 `no_room` 하나 — 실패는 멈춤이 아니라 값이다), `cstr`(널 종단 `char*` 를 가리키는 생 포인터의 `newtype`, 다른 언어와 주고받는 경계 전용).

## <a id="sx2"></a>op 한눈에

| **op** | **시그니처** | **안 될 때** |
|---|---|---|
| `new` | `fn () → str_buf` | — |
| `as_str` | `fn (b str_buf, buf slice u8) → slice u8` | — (뷰, 복사가 아니다) |
| `room` | `fn (b str_buf, buf slice u8) → u64` | 빈 버퍼(길이 0)면 넘침으로 멈춘다 |
| `as_cstr` | `unsafe proc (b str_buf, buf mut slice u8) → cstr` | `len + 1 > len(buf)` 면 경계 밖으로 멈춘다 |
| `append` | `proc (b mut str_buf, buf mut slice u8, s slice u8) → result void sb_error` | `error no_room`(버퍼 그대로) |
| `append_trunc` | `proc (b mut str_buf, buf mut slice u8, s slice u8) → u64` | — (쓴 개수 < `len s` 로 안다) |
| `append_grow` | `proc (b mut str_buf, old mut slice u8, new_buf mut slice u8, s slice u8) → result void sb_error` | `error no_room`(원본 그대로) |

*표 50.2 — `strbuf` 의 op*

## <a id="sx3"></a>op 상세

- **`new`** — `len 0` 인 빈 상태. 매개변수가 없다 — 만드는 것은 상태뿐이다.
- **`as_str`** — 지금까지 쓴 바이트의 뷰, 곧 `subslice buf 0 len`. 널은 뷰에 들지 않는다 — 길이가 진실이다.
- **`room`** — 남은 내용 용량 `len(buf) − 1 − len`. 널 자리 1 바이트가 빠져 있다. 이어 붙이기 전에 “이만큼 더 넣을 수 있나” 를 물을 때 쓴다.
- **`as_cstr`** — `len` 자리에 널을 봉인하고 시작 포인터를 `cstr` 로 낸다. `buf` 가 `mut` 인 까닭은 널 1 바이트를 **실제로 쓰기** 때문이다. `cstr_of` 가 `effects unsafe` 라 이 op 도 `unsafe` 이고, 순수 코드에서는 못 부른다.
- **`append`** — `len + len(s) + 1 ≤ len(buf)` 면 모두 쓰고 봉인하고 `ok`. 아니면 `error no_room` 이고 **한 바이트도 쓰지 않는다**(길이도 버퍼도 그대로). 절반 쓰인 버퍼는 조용히 틀린 내용이고, 그것은 거절보다 나쁘다.
- **`append_trunc`** — 남은 자리만큼만 쓰고 쓴 개수를 돌려준다. 그 수를 `len s` 와 비교하면 잘렸는지 안다. 비교하지 않으면 잘린 줄 모른다.
- **`append_grow`** — 옛 내용을 `new_buf` 로 옮기고 `s` 를 잇고 봉인하는 것까지가 한 동작이다. `new_buf` 매개변수가 곧 “몰래 할당하지 않는다” 의 실물이다. 새 자리조차 모자라면 `error no_room` 이고 **아무것도 옮기지 않는다**. 성장하려면 더 큰 바이트를 가진 자여야 한다.

## <a id="sx4"></a>쓰는 법

세 갈래가 서로 다르게 행동한다. 버퍼는 호출자가 마련한다.

```lowent
module demo .

use strbuf .

proc build input buf mut slice u8 . . output u64 . do
  var b be strbuf.str_buf strbuf.new .
  let r1 be result void strbuf.sb_error strbuf.append b buf "ab" .
  guard is_ok r1 . else return 90 .
  guard eq (field b len) 2 . else return 91 .
  rem 봉인 확인 --- len 자리(buf[2])에 널이 있다
  guard eq (idx buf 2) 0 . else return 92 .
  rem 버퍼 5 = 내용 4 + 널 1 --- 2 + 3 + 1 > 5 이므로 거절되고 버퍼가 더럽혀지지 않는다
  let r2 be result void strbuf.sb_error strbuf.append b (subslice buf 0 5) "xyz" .
  guard is_error r2 . else return 93 .
  guard eq (field b len) 2 . else return 94 .
  rem 같은 상황에서 trunc 는 되는 만큼(2) 쓰고 2 를 돌려준다
  let n be u64 strbuf.append_trunc b (subslice buf 0 5) "xyz" .
  guard eq n 2 . else return 95 .
  return field b len .
end
```

성장은 새 자리를 호출자가 주는 것으로 표현된다.

```lowent
proc grow input small mut slice u8 . . input big mut slice u8 . . output u64 . effects none . do
  var b be strbuf.str_buf strbuf.new .
  let r1 be result void strbuf.sb_error strbuf.append b small "ab" .
  guard is_ok r1 . else return 90 .
  let r2 be result void strbuf.sb_error strbuf.append b small "xyz" .
  guard is_error r2 . else return 91 .
  rem 더 큰 자리를 내가 마련해 넘긴다 --- 라이브러리는 할당하지 않는다
  let r3 be result void strbuf.sb_error strbuf.append_grow b small big "xyz" .
  guard is_ok r3 . else return 92 .
  guard eq (idx big 0) 97 . else return 93 .
  guard eq (idx big 2) 120 . else return 94 .
  return field b len .
end
```

`build` 는 4(“ab” 2 + 되는 만큼 쓴 2)를, `grow` 는 5(“abxyz”)를 낸다. 실제 쓰임은 경로 조립이다 — `append b pathbuf dpath`, `append b pathbuf "/"`, `append b pathbuf name` 으로 잇고 `as_str b pathbuf` 로 뷰를 꺼내 [`files`](sec116.md#mod-files) 에 넘긴다.

## <a id="sx5"></a>반례

> **반례. 순수 코드에서 `as_cstr` 를 부른다**
>
> > ```lowent
> > fn f input b strbuf.str_buf . input buf mut slice u8 . . output u64 . do
> >   let p be strbuf.cstr strbuf.as_cstr b buf .   rem ✗ unsafe proc 을 fn 에서 부른다
> >   return 0 .
> > end
> > ```
> >
> > `E-EFFECT-CALC` 로 번역에서 거절된다. `cstr` 를 만드는 것은 널 종단의 보증이고, 그 보증은 `unsafe` 경계 안에서만 선다. 부르는 쪽을 `unsafe proc` 으로 바꾼다.

> **반례. 널 자리를 잊은 버퍼 크기**
>
> > 버퍼가 정확히 2 바이트면 `"ab"` 조차 들어가지 않는다(2 + 0 + 널 1 > 2). 첫 `append` 부터 `error no_room` 이고 `field b len` 이 0 에서 움직이지 않는다. 내용 n 바이트에는 버퍼 n + 1 바이트가 필요하다.

> **반례. `result` 를 보지 않고 버퍼를 읽는다**
>
> > ```lowent
> > let r be result void strbuf.sb_error strbuf.append b buf "hello" .
> > let v be slice u8 strbuf.as_str b buf .   rem ✗ r 을 보지 않았다
> > ```
> >
> > 멈춤도 오류도 없다. 버퍼가 깨진 것도 아니다 — 거절은 버퍼를 건드리지 않는다. 다만 **기대한 것이 안 들어간 짧은 문자열**이 나온다. `guard is_ok r . else …` 가 먼저다.

> **반례. 상태와 버퍼를 섞어 넘긴다**
>
> > ```lowent
> > let r be result void strbuf.sb_error strbuf.append b buf1 "ab" .
> > let v be slice u8 strbuf.as_str b buf2 .   rem ✗ 길이는 buf1 의 것, 바이트는 buf2 의 것
> > ```
> >
> > 오류 없이 엉뚱한 바이트의 뷰가 나온다. 하나의 `str_buf` 에는 하나의 버퍼(또는 `append_grow` 로 옮긴 그 후계)만 짝지어 쓴다.

## <a id="sx6"></a>주의

- **버퍼는 내용보다 1 바이트 크게 잡는다.** 널 봉인 자리 때문이다.
- **`append` 의 거절은 조용하다.** 멈춤이 아니라 값이므로 `result` 를 보지 않으면 프로그램이 그냥 계속 돈다.
- **성장한 뒤에는 옛 버퍼를 버린다.** `append_grow` 가 `ok` 를 낸 순간 진실은 `new_buf` 에 있다. 이후 호출에 `old` 를 넘기지 않는다.
- **`as_cstr` 는 스냅숏이 아니다.** 시작 포인터를 그대로 내므로 그 뒤 이어 붙이면 가리키는 내용이 바뀐다. 다른 언어를 부르기 직전에 만들고 바로 쓴다.
- **`room` 은 길이 0 인 버퍼에서 멈춘다** (`0 − 1`). 버퍼는 최소 1 바이트다.

---

[← 이전](sec58.md) · [목차로](README.md) · [다음 →](sec60.md)
