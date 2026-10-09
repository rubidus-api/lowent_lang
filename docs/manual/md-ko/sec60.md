# <a id="mod-fmt"></a>`fmt` — 호출자의 버퍼에 조립하는 포매팅

소스

`lib/fmt.low`

층

L0 — 순수 계산(호출자의 버퍼)

권한

없음

수와 글자를 **호출자가 준 버퍼에** 찍어 문자열을 만든다. 출력할 줄이나 로그 한 줄을 조립할 때 쓴다. 이 모듈은 종이를 만들지 않고 건네받은 종이 (`mut slice u8`)에 글씨만 쓴다.

```lowent
use fmt .

let p option u64 fmt.put_u64 buf. 0 1234 . .
guard is_some p. . else return 1 . .
```

`0` 은 `pos`(다음에 쓸 자리)이고, 돌려받는 `option u64` 는 **성공하면 새 `pos`, 자리가 모자라면 `none`** 이다.

**포매팅은 출력이 아니다.** 모든 op 이 호출자의 버퍼에 바이트를 조립할 뿐이고 하나의 예외 없이 `effects none` 이다. 실제 출력은 `cap io` 를 든 호출자가 `write_out` 이나 [`outbuf`](sec116.md#mod-outbuf) 로 따로 한다. 그래서 조립 코드는 권한 없이 아무 데서나 부를 수 있고, 시험은 화면이 아니라 버퍼를 대조하면 되며, 무엇을 언제 내보낼지는 호출자가 정한다. 수를 바이트로 바꾸는 일은 커널도 할당도 필요 없어 라이브러리다 — 기본 연산은 하나도 늘지 않았고, 파일 전체가 VM·네이티브 대조 안에 있다.

## <a id="sx1"></a>설계와 경계

- **규약은 하나다 — 모든 라이터는 `(buf, pos) → 새 pos`.** 성공하면 `some 새pos`, 모자라면 `none` 이다. 멈추지 않고 값으로 거절한다. 새 `pos` 를 그대로 다음 호출에 주면 글자가 이어 붙는다.
- **전량 아니면 무.** 자리가 모자라면 한 바이트도 쓰지 않는다 — “1234” 대신 “12” 가 남으면 아무도 알아채지 못한다. 그래서 `put_u64`·`put_hex` 는 자릿수를 먼저 세고(`dec_width`·`hex_width`) 앞에서부터 쓴다.
- **줄바꿈은 LF 하나다**(`put_nl`). [`io`](sec115.md#mod-io) 의 `take_line` 과 같은 규약이고, CRLF 는 만들지도 지우지도 않는다.
- **하지 않는 것** — 부동소수 포매팅, 폭 맞춤과 정렬, 로케일(천 단위 구분). 필요하면 `dec_width` 로 폭을 재서 `put_byte` 로 빈칸을 직접 채운다.
- **상태가 없다.** 구조체도 열거도 없다. 버퍼와 커서는 모두 호출자의 것이라, 어디서 몇 개를 동시에 써도 서로 간섭하지 않는다.

## <a id="sx2"></a>op 한눈에

| **op** | **시그니처** | **안 될 때** |
|---|---|---|
| `put_byte` | `proc (buf mut slice u8, pos u64, b u8) → option u64` | 자리가 없으면 `none`, 버퍼 그대로 |
| `put_str` | `proc (buf, pos, s slice u8) → option u64` | 모두 들어가지 않으면 `none`, 버퍼 그대로 |
| `dec_width` | `fn (n u64) → u64` | 실패 없음 |
| `put_u64` | `proc (buf, pos, n u64) → option u64` | 자리가 없으면 `none`, 버퍼 그대로 |
| `put_i64` | `proc (buf, pos, n i64) → option u64` | 자리가 없으면 `none` |
| `hex_width` | `fn (n u64) → u64` | 실패 없음 |
| `put_hex` | `proc (buf, pos, n u64) → option u64` | 자리가 없으면 `none`, 버퍼 그대로 |
| `put_bool` | `proc (buf, pos, b bool) → option u64` | 자리가 없으면 `none` |
| `put_nl` | `proc (buf, pos) → option u64` | 자리가 없으면 `none` |

*표 50.1 — `fmt` 의 op — 모두 `effects none`*

이름의 `put_` 은 “버퍼에 써 넣는다”, `_width` 는 “몇 바이트가 필요한지 세기만 한다” 는 뜻이다. 버퍼를 고치는 것은 `proc`, 세기만 하는 것은 `fn` 이지만 아홉 모두 바깥에 닿지 않는다.

## <a id="sx3"></a>op 상세

모든 라이터의 앞 두 매개변수는 같다. `buf` 는 **어디에** 쓸지(할당하지 않으므로 받아야 하고, 고치므로 `mut`), `pos` 는 그 안 **어느 자리부터** 쓸지(커서를 모듈이 기억하지 않으므로 직전 호출이 준 새 `pos` 를 넘기는 것이 “이어 쓴다” 의 유일한 표현)다.

- **`put_byte`** — 바이트 `b` 하나를 `buf[pos]` 에 쓴다(`'-'` 는 45, LF 는 10). 성공하면 `some (pos+1)`.
- **`put_str`** — 바이트열 `s` 전체를 쓴다. `s` 는 읽기만 하므로 문자열 리터럴을 그대로 줄 수 있다. `pos + len s` 가 `len buf. .` 를 넘으면 `none`.
- **`dec_width` · `hex_width`** — `n` 을 10 진·16 진으로 쓸 때의 자릿수. 최소 1(`0` 도 한 자리). 전량 아니면 무를 위한 사전 계산이지만, “버퍼가 몇 바이트면 충분한가” 를 미리 잴 때 직접 써도 된다.
- **`put_u64` · `put_hex`** — 부호 없는 10 진·16 진(소문자, `0x` 없음). `0x` 가 필요하면 `put_str buf pos "0x"` 를 앞에 붙인다.
- **`put_i64`** — 부호 있는 10 진. 음수면 `-` 를 먼저 쓰고 크기를 `u64` 에서 다룬다. i64 최솟값(−9223372036854775808)은 양수로 뒤집을 수 없으므로 `0 − n` 을 `u64` 에서 계산한다. 이 경계값도 옳게 찍힌다.
- **`put_bool`** — `"true"`·`"false"` 를 쓴다. `"1"`·`"0"` 이 아니므로 4 나 5 바이트가 필요하다.
- **`put_nl`** — LF 한 바이트. `put_byte buf pos 10` 과 같다.

## <a id="sx4"></a>쓰는 법

조립은 순수하게, 출력은 마지막에 `cap io` 로 한 번. 돌려받은 새 `pos` 를 **다음 호출에 이어서 넘기는 것**이 핵심이다.

```lowent
module report .

use fmt .

proc main
  input out cap io .
  input al  cap allocator .
  output u8 .
  effects alloc io .
do
  let g option mut slice u8 alloc_bytes al. capacity 64 . .
  guard is_some g. . else return 70 . .
  let buf mut slice u8 some_value g. . .

  rem 조립 --- 여기까지 한 바이트도 밖으로 나가지 않았다
  let p1 option u64 fmt.put_str buf. 0 "answer=" . .
  guard is_some p1. . else return 71 . .
  let p2 option u64 fmt.put_u64 buf. some_value p1. . 42 . .
  guard is_some p2. . else return 72 . .
  let p3 option u64 fmt.put_str buf. some_value p2. . " hex=" . .
  guard is_some p3. . else return 73 . .
  let p4 option u64 fmt.put_hex buf. some_value p3. . 255 . .
  guard is_some p4. . else return 74 . .
  let p5 option u64 fmt.put_nl buf. some_value p4. . . .
  guard is_some p5. . else return 75 . .

  rem 출력 --- 조립된 앞부분(0 … p5)만 내보낸다
  let w u64 write_out out. 1 subslice buf. 0 some_value p5. . . . .
  return 0 .
end .
```

출력은 `answer=42 hex=ff` 와 줄바꿈 하나다. 조각마다 종료 코드를 달리 두면 어디서 모자랐는지 바로 안다. 버퍼 크기를 미리 재는 것도 같은 도구로 된다.

```lowent
fn need_for input n u64 . output u64 . do
  let w u64 fmt.dec_width n. . .
  return add add 7 w. . 1 . .
end .
```

## <a id="sx5"></a>반례

> **반례. `effects none` op 안에서 출력한다**
>
> > ```lowent
> > proc bad input out cap io . output u64 . effects none . do
> >   return write_out out. 1 "x" . .
> > end .
> > ```
> >
> > 선언이 거짓말이 되어 `E-EFFECT`(`fn` 이면 `E-EFFECT-CALC`)로 거절된다. `effects io` 로 바꿔도 `cap io` 입력이 없으면 `E-EFFECT-NO-CAP` 이다.

> **반례. 돌려받은 `pos` 를 버리고 옛 `pos` 를 다시 쓴다**
>
> > ```lowent
> > let p1 option u64 fmt.put_str buf. 0 "answer=" . .
> > let p2 option u64 fmt.put_u64 buf. 0 42 . .        rem ✗ pos 는 some_value p1 이어야 한다
> > ```
> >
> > 번역은 통과하고 결과가 `answer=42` 대신 `42swer=` 처럼 겹쳐 나온다. 출력 앞부분이 뭉개져 있으면 `pos` 를 이어 넘겼는지부터 본다.

> **반례. `none` 을 무시하고 진행한다**
>
> > 자리가 모자라면 그 조각은 **아예 쓰이지 않은 것**이다. `guard is_some … else return …` 없이 진행하면 중간 조각이 빠진 채 나간다. 긴 값일 때만 출력이 이상하면 버퍼 부족이다. 전량 아니면 무는 op 하나의 보장이지 이어진 호출 전체의 보장이 아니다. 거꾸로 `guard` 없이 `some_value` 로 꺼내면, 자리가 모자란 순간 `E-VM-NONE` 으로 멈춘다 — 짧은 값으로만 시험하면 드러나지 않는다.

> **반례. `write_out` 에 버퍼 전체를 준다**
>
> > `subslice buf. 0 some_value p5. . .` 대신 `buf` 를 주면 쓰지 않은 뒷부분까지 나간다. 줄 끝에 정체 모를 바이트가 붙는다. 마지막 `pos` 가 곧 **쓴 길이**다.

## <a id="sx6"></a>주의

- **모자람은 정상 경로다.** 오류가 아니라 예상된 답이므로 `guard` 로 받는다. 상한이 필요하면 미리 잰다 — `u64` 10 진 최대 20 자리, 16 진 최대 16 자리, `i64` 는 부호 1 자리를 더한다.
- **`pos` 는 색인이자 길이다.** 조립이 끝난 뒤의 `pos` 는 지금까지 쓴 바이트 수와 같아서 `subslice buf. 0 pos. .` 가 곧 결과다.
- **버퍼를 다시 쓰려면 `pos` 를 0 으로 되돌리기만 하면 된다.** 지난 내용은 지울 필요가 없다 — 그때의 `pos` 까지만 내보내면 옛 바이트는 보이지 않는다.
- **CRLF 는 직접 쓴다.** 필요한 규약이면 `put_str buf pos "\r\n"` 을 쓴다.
- **모든 op 이 `effects none` 이다.** `fn` 안에서도 액터 핸들러 안에서도 부를 수 있다. 출력할 때만 `cap io` 와 `effects io` 가 필요하다.
- **접두사·부호·폭 맞춤은 호출자 몫이다.** `0x`·`+`·천 단위 쉼표·빈칸 채우기를 `put_str`·`put_byte` 로 직접 붙인다.

---

[← 이전](sec59.md) · [목차로](README.md) · [다음 →](sec61.md)
