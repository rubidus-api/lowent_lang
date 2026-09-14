# fmt — 호출자 버퍼에 조립하는 포매팅

소스: `lib/fmt.low` · 모듈명 `fmt`

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 숫자·글자를 **호출자가 준 버퍼에** 찍어 문자열을 만든다.

호출자 버퍼(caller-provided buffer)란 **출력 자리를 부르는 쪽이 준비해 준다**는 뜻이다 —
이 모듈은 종이를 만들어 주지 않고, 건네받은 종이에 글씨만 쓴다. 그 종이가 `mut slice u8`
(고쳐 쓸 수 있는 바이트 자리)이다.

**언제 쓰나.** 출력할 줄을 조립할 때, 로그 한 줄을 만들 때 쓴다.

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use fmt .

let p option u64 . be fmt.put_u64 buf 0 1234 .   rem buf 0 번지부터 "1234" 를 쓴다
guard is_some p . else return 1 .                rem 자리가 모자라면 한 글자도 안 쓰고 none
```

여기서 `0` 은 `pos`(다음에 쓸 자리 — 버퍼 안의 커서)이고, 돌려받는 `option u64`(있을 수도
없을 수도 있는 u64)는 **성공하면 새 `pos`, 자리가 모자라면 `none`** 이다. `option` 은 꺼내기
전에 `guard is_some …` 로 갈라야 한다([7장](../07-guard-and-option.md)).

**읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## 왜 있는가

이 절은 **포매팅이 무엇이고 왜 출력과 갈라져 있는지**를 말한다. 요점 한 줄: 이 모듈은 글자를
만들 뿐, 내보내지는 않는다.

이 모듈은 **값을 사람이 읽을 바이트로 바꿀 때** 쓴다. 메모리 속의 42 는 그냥 정수이고,
화면에 보이는 `42` 는 바이트 두 개(`'4'` `'2'`)다 — 그 변환이 포매팅이다.

**포매팅은 출력이 아니다.** 이 모듈의 op 은 전부 **호출자가 준 버퍼**(호출자가 미리
마련해 둔 바이트 자리, `mut slice u8`)에 바이트를 조립할 뿐이고, 하나의 예외 없이
`effects none` 이다. effect(효과)는 op 이 바깥세상에 무엇을 하는지를 시그니처에 적는
선언인데, `none` 은 "계산만 한다 — 숨은 할당 0, 숨은 stdout 0" 이라는 뜻이고 컴파일러가
그 약속을 검사한다. 실제 출력은 `cap io`(출력에 닿을 권한 — [outbuf](outbuf.md) 참조)를
든 호출자가 `write_out`(또는 `outbuf` 라이터)으로 따로 한다. cap(capability, 능력)은
"이 자원에 닿아도 된다" 는 증표인데, 주변에 떠다니는 것이 아니라 **인자로 건네받아야만**
생긴다. 소스 상단 rem 이 설계 출처(RFC-0069 A1 · `docs/example/format.md`)와 함께 이
분리를 못박는다:
*"포매팅(effects none) vs 출력(effects io) — 누가 무엇을 쓰는지 시그니처에."*

이 분리의 실익은 셋이다: 조립 코드를 `cap` 없이 아무 데서나 부를 수 있고, 시험할 때 화면이
아니라 버퍼만 대조하면 되고, 무엇을 언제 내보낼지를 호출자가 정한다.

빌트인이 아닌 이유는 리프 규칙이다: 숫자→바이트 변환은 커널도 할당도 필요 없이 Lowent 로
쓸 수 있으므로 라이브러리다. **빌트인 op 증가 0.** 부수 효과로 이 파일 전체가 오라클 안에
있다 — VM 과 네이티브가 같은 바이트를 내는 것이 검증된다.

## 설계 의도와 경계

이 절은 **모든 op 이 공유하는 규약 하나**와, 이 모듈이 일부러 안 해 주는 것들을 적는다.

기억할 규약은 하나다: **쓴 자리의 끝(새 pos)을 돌려받아, 다음 호출에 그대로 넘긴다.**

- **규약: 모든 라이터는 `(buf, pos) → 새 pos`.** 성공하면 `some 새pos`, 자리가 모자라면
  `none` 이다. 트랩(즉시 중단)이 아니라 **값으로 거절**한다 — 호출자가 받아서 처리할 수
  있는 답이다. 새 pos 는 "방금 쓴 조각의 바로 다음 자리" 이므로, 그것을 그대로 다음 호출의
  `pos` 로 주면 글자가 이어 붙는다.
- **전량 아니면 무(all-or-nothing).** 자리가 모자라면 **한 바이트도 쓰지 않는다** — 절반
  쓴 버퍼는 조용히 틀린 출력이 되기 때문이다("1234" 대신 "12" 가 남으면 아무도 못 알아챈다).
  그래서 `put_u64`/`put_hex` 는 자릿수를 먼저 세고(`dec_width`/`hex_width`), 뒤집기 없이
  앞에서부터 쓴다.
- 줄바꿈은 **LF 하나**다(`put_nl`) — `lib/io.low` 의 `take_line` 과 같은 규약(LF 만이
  줄 구분자, CRLF 는 라이브러리가 만들지도 지우지도 않는다).
- 안 하는 것: 부동소수점 포매팅, 패딩/정렬(자릿수를 맞춰 빈칸을 채우는 것), 로케일(천 단위
  구분·지역별 표기). 소스에 없는 것은 없는 것이다 — 필요하면 호출자가 `dec_width` 로 폭을
  재서 `put_byte` 로 빈칸을 직접 채운다.

## 자료구조

이 절은 **이 모듈이 무엇을 들고 있는가**를 말한다. 답은 "아무것도" 다.

struct 도 enum 도 없다. 상태는 전부 호출자의 것 — 버퍼(`mut slice u8`)와 커서(`pos u64`,
"다음에 쓸 자리")다. 커서를 모듈이 들고 있지 않기 때문에 호출자가 매번 `pos` 를 넘겨야 하고,
그 대가로 이 모듈은 `effects none` 이며 어디서 몇 개를 동시에 써도 서로 간섭하지 않는다.
실패는 `option u64` 하나로 답한다(`some 새pos` = 성공, `none` = 자리 부족).

## op 한눈에

이 절은 **아홉 op 을 한 표로** 보여 준다. 이름의 `put_` 은 "버퍼에 써 넣는다", `_width` 는
"몇 바이트가 필요한지 세기만 한다" 는 뜻이다.

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `put_byte` | proc | `(buf mut slice u8, pos u64, b u8) → option u64` | 자리 없으면 `none`, 버퍼 불변 |
| `put_str` | proc | `(buf, pos, s slice u8) → option u64` | 전량 못 들어가면 `none`, 버퍼 불변 |
| `dec_width` | fn | `(n u64) → u64` | 실패 없음 |
| `put_u64` | proc | `(buf, pos, n u64) → option u64` | 자리 없으면 `none`, 버퍼 불변 |
| `put_i64` | proc | `(buf, pos, n i64) → option u64` | 자리 없으면 `none` |
| `hex_width` | fn | `(n u64) → u64` | 실패 없음 |
| `put_hex` | proc | `(buf, pos, n u64) → option u64` | 자리 없으면 `none`, 버퍼 불변 |
| `put_bool` | proc | `(buf, pos, b bool) → option u64` | 자리 없으면 `none` |
| `put_nl` | proc | `(buf, pos) → option u64` | 자리 없으면 `none` |

전부 `effects none` 이다. `proc`(버퍼를 고쳐 쓰므로)과 `fn`(세기만 하므로)이 갈리지만,
바깥세상에 닿지 않는다는 점은 아홉 개가 같다.

## op 상세

이 절은 **매개변수 하나하나가 왜 필요한지**까지 적는다.

모든 라이터의 앞 두 매개변수는 같다: `buf` 는 **어디에** 쓸지(호출자의 버퍼), `pos` 는
그 안 **어느 자리부터** 쓸지다. 이 둘이 매번 반복되는 이유는 상태를 모듈이 아니라
호출자가 들기 때문이다 — 그래서 이 모듈은 `effects none` 일 수 있다.

- `buf mut slice u8` — **왜 받나:** 쓸 종이다. 모듈이 할당을 안 하므로 자리를 건네받아야
  하고, 내용을 고쳐야 하므로 `mut` 이다.
- `pos u64` — **왜 받나:** 커서를 모듈이 기억하지 않기 때문이다. "앞 조각 뒤에 이어 쓴다" 를
  표현하는 유일한 수단이 직전 호출이 돌려준 새 pos 를 넘기는 것이다.

### put_byte

```lowent
export proc put_byte output option u64 . input buf mut slice u8 . . input pos u64 .
  input b u8 . effects none .
```

- `b u8` — **왜 받나:** 쓸 바이트 값 자체다. 문자 하나를 넣는 가장 낮은 층이라 값을 그대로
  받는다(예: `'-'` 는 45, LF 는 10).

한 바이트 `b` 를 `buf[pos]` 에 쓴다. `pos` 가 버퍼 끝이면 `none` 이고 버퍼를 건드리지
않는다. 성공하면 `some (pos+1)`.

### put_str

```lowent
export proc put_str output option u64 . input buf mut slice u8 . . input pos u64 .
  input s slice u8 . effects none .
```

- `s slice u8` — **왜 받나:** 옮겨 적을 원본이다. 복사해 가지 않고 읽기만 하므로 `mut` 이
  아니다(문자열 리터럴을 그대로 줄 수 있다).

바이트열 `s` 전체를 쓴다. `pos + len s` 가 `len buf` 를 넘으면 한 바이트도 쓰지 않고 `none`.

### dec_width / hex_width

```lowent
export fn dec_width output u64 .
export input n u64 . fn hex_width output u64 . input n u64 .
```

- `n u64` — **왜 받나:** 폭은 값에 따라 달라진다(9 는 한 자리, 10 은 두 자리). 잴 대상이
  필요하다.

`n` 을 10 진/16 진으로 쓸 때의 자릿수. 최소 1(`0` 도 한 자리). 전량-아니면-무를 지키기
위한 사전 계산이지만 export 되어 있으므로 호출자가 "버퍼가 몇 바이트면 충분한가" 를
미리 잴 때 직접 써도 된다. 버퍼에 아무것도 쓰지 않으므로 `fn` 이고, 실패도 없다.

### put_u64 / put_hex

```lowent
export proc put_u64 output option u64 .
export input buf mut slice u8 . . input pos u64 .
  input n u64 . proc put_hex output option u64 . input buf mut slice u8 . . input pos u64 .
  input n u64 .
```

- `n u64` — **왜 받나:** 바이트로 바꿀 수 자체다. 부호 없는 정수만 받는 것이 두 op 의
  경계다(음수는 `put_i64`).

부호 없는 10 진 / 16 진(소문자, `0x` 접두 없음). 폭을 먼저 재고 앞에서부터 채운다.
`0x` 가 필요하면 `put_str buf pos "0x"` 를 앞에 직접 붙인다.

### put_i64

```lowent
export proc put_i64 output option u64 . input buf mut slice u8 . . input pos u64 .
  input n i64 .
```

- `n i64` — **왜 받나:** 부호 있는 값이라 타입이 다르다. u64 로 형이 다르면 `put_u64` 쪽이
  맞다.

부호 있는 10 진. 음수면 `-` 를 먼저 쓰고 크기를 u64 로 다룬다 — i64 최솟값
(-9223372036854775808)은 양수로 뒤집을 수 없으므로 `0 - n` 을 u64 에서 계산한다(2 의
보수라 정확한 크기가 나온다). 이 경계값도 올바르게 찍힌다.

### put_bool

```lowent
export proc put_bool output option u64 . input buf mut slice u8 . . input pos u64 .
  input b bool .
```

- `b bool` — **왜 받나:** 찍을 참/거짓 값이다. 안에서 `put_str` 로 갈라 쓴다.

`"true"` / `"false"` 를 쓴다 — `"1"`/`"0"` 이 아니다. 그래서 필요한 자리도 4 또는 5 바이트다.

### put_nl

```lowent
export proc put_nl output option u64 . input buf mut slice u8 . .
  input pos u64 .
```

값 매개변수가 없다 — 쓸 것이 LF 하나로 정해져 있기 때문이다. LF(10) 한 바이트.
`put_byte buf pos 10` 과 같다.

## 사용법과 예제

이 절은 **여러 조각을 이어 한 줄을 만드는 실제 모양**을 보여 준다. 눈여겨볼 것은 `pos` 가
호출을 타고 흘러가는 방식이다.

조립은 순수하게, 출력은 마지막에 `cap io` 로 한 번. `impl/tests/prog/wc.low` 의 꼬리와
같은 모양이다. 반환된 새 pos 를 다음 호출에 **이어서 넘기는 것**이 핵심 규약이다.

```lowent
module report .

use fmt from "../lib/fmt.low" .

rem entry 패턴: main 의 input 은 전부 cap — 프로그램이 시작할 때 실행기가 건네주는
rem 권한이다. 여기서는 출력(cap io)과 메모리 할당(cap allocator)만 받는다.
rem 안 쓰는 권한은 안 받는 것이 규율이다.
proc main
  output u8 .                 rem 종료 코드 — 0 이 성공
  input out cap io .          rem 표준 입출력에 닿을 권한
  input al  cap allocator .   rem 버퍼를 얻을 권한
  effects alloc io .          rem 이 op 이 하는 일의 선언(안 맞으면 컴파일 오류)
do
  rem ① 조립할 자리(버퍼)를 얻는다. 실패는 none 이므로 guard 로 걸러 나간다.
  let g option mut slice u8 . . be alloc_bytes al capacity 64 .   rem 64 바이트 요청
  guard is_some g . else return 70 .                              rem 못 얻으면 종료 코드 70
  let buf mut slice u8 . be some_value g .                        rem 여기서 실제 버퍼를 꺼낸다

  rem ② 조립 — 여기까지 한 바이트도 밖으로 안 나갔다.
  rem    매번 some_value 로 새 pos 를 꺼내 다음 호출에 잇는다.
  let p1 option u64 . be fmt.put_str buf 0 "answer=" .            rem 0 번지부터 7 바이트 → p1 = 7
  guard is_some p1 . else return 71 .                             rem 자리 부족이면 여기서 끝
  let p2 option u64 . be fmt.put_u64 buf (some_value p1) 42 .     rem 7 번지에 "42" → p2 = 9
  guard is_some p2 . else return 72 .
  let p3 option u64 . be fmt.put_str buf (some_value p2) " hex=" . rem 9 번지에 " hex=" → p3 = 14
  guard is_some p3 . else return 73 .
  let p4 option u64 . be fmt.put_hex buf (some_value p3) 255 .    rem 16 진 소문자 "ff" → p4 = 16
  guard is_some p4 . else return 74 .
  let p5 option u64 . be fmt.put_nl buf (some_value p4) .         rem LF 하나 → p5 = 17 (= 쓴 길이)
  guard is_some p5 . else return 75 .                             rem 종료 코드를 조각마다 달리 두면
  rem                                                                  어디서 모자랐는지 바로 안다

  rem ③ 출력 — cap io 를 든 자만 한다. 조립된 앞부분(0..p5)만 내보낸다.
  rem    버퍼는 64 바이트지만 쓴 것은 17 바이트뿐이므로 subslice 로 잘라 준다.
  let w u64 be write_out out 1 (subslice buf 0 (some_value p5)) .  rem 1 = stdout
  return 0 .
end
```

출력: `answer=42 hex=ff` 와 줄바꿈 하나.

버퍼 크기를 미리 재는 패턴도 같은 도구로 된다:

```lowent
rem "answer=" + n + LF 한 줄이 몇 바이트인가 — 버퍼를 잡기 전에 미리 센다.
fn need_for output u64 . input n u64 . do
  let w u64 be fmt.dec_width n .        rem 숫자가 차지할 자릿수(0 도 1)
  return add (add 7 w) 1 .              rem "answer=" 7 바이트 + 숫자 + LF 1 바이트
end
```

## 반례 — 이렇게 쓰면 안 된다

이 절은 **초보자가 실제로 하는 실수**와, 그것을 **무엇으로 알아채는지**(증상)를 적는다.
증상은 셋 중 하나다: 컴파일 에러(E-코드) · 실행 중 트랩 · 조용히 틀린 출력. ①은 첫째,
②③은 셋째라 특히 조심해야 한다.

**① `effects none` op 안에서 출력하기.** fmt 를 흉내내며 `write_out` 을 부르면 선언이
거짓말이 되어 컴파일이 거절된다. 증상: **컴파일 에러** — `proc` 이면 **E-EFFECT**(선언에
io 가 없다), `fn` 이면 **E-EFFECT-CALC** 다. 프로그램이 아예 안 만들어지므로 놓칠 수 없다.

```lowent
rem 틀림: effects none 을 선언하고 io 를 한다 — E-EFFECT
proc bad output u64 . input out cap io . effects none . do
  return write_out out 1 "x" .
end
```

`effects io` 로 바꿔도 `cap io` 입력이 없으면 **E-EFFECT-NO-CAP** 이다 — io 는 건네받는
권리이지 주변 권한이 아니다.

**② 반환된 pos 를 버리고 옛 pos 를 재사용.** 같은 자리에 덮어써서 앞 조각이 사라진다.

```lowent
rem ✗ 두 번 다 0 번지에 쓴다 — 앞의 "answer=" 가 지워진다
let p1 option u64 . be fmt.put_str buf 0 "answer=" .
let p2 option u64 . be fmt.put_u64 buf 0 42 .        rem pos 가 some_value p1 이어야 한다
```
증상: **조용히 틀린 출력** — 컴파일은 통과하는데 결과가 `answer=42` 대신 `42swer=` 처럼
겹쳐 나온다. 컴파일러가 못 잡는 논리 오류다. 알아채는 법: 출력 앞부분이 뭉개져 있으면
`pos` 를 이어 넘겼는지부터 본다. 항상 `some_value 직전결과` 를 다음 pos 로 넘긴다.

**③ `none` 을 무시하고 버퍼 전체를 내보내기.** 버퍼가 모자라면 그 조각은 **아예 안
쓰인 것**이다. 증상: `guard is_some … else return …` 없이 진행하면 **조용히 틀린 출력** —
중간 조각이 빠진 채(그리고 뒤 조각은 앞 pos 자리에 겹친 채) 나간다. 알아채는 법: 긴 값일
때만 출력이 이상하면 버퍼 부족이다. 전량-아니면-무는 op 단위 보장이지, 시퀀스 전체 보장이
아니다.

**④ `none` 을 `guard` 없이 `some_value` 로 꺼내기.** ③의 반대 실수다.

```lowent
rem ✗ 자리가 모자라면 여기서 터진다
let p option u64 . be fmt.put_u64 buf pos n .
let q u64 be some_value p .
```
증상: 컴파일은 통과하고, 자리가 모자란 순간 실행 중 **E-VM-NONE 트랩(패닉)** 으로 멈춘다
(`some_value of none`). 짧은 값으로 시험하면 안 걸리므로 더 위험하다.

**⑤ `write_out` 에 버퍼 전체를 주기.** `subslice buf 0 (some_value p5)` 대신 `buf` 를 주면
안 쓴 뒷부분(할당 직후의 쓰레기 바이트)까지 나간다. 증상: **조용히 틀린 출력** — 줄 끝에
정체불명의 바이트가 붙는다. 마지막 pos 가 곧 **쓴 길이**라는 점을 기억한다.

## 주의사항

이 절은 **알고 있으면 하루를 아끼는 것들**이다. 대부분 "버퍼는 남의 것" 과 "pos 를 이어
넘긴다" 두 가지에서 나온다.

- 버퍼가 모자랄 가능성은 정상 경로다. 오류가 아니라 예상된 답이므로 `guard` 로 받는다.
  상한이 필요하면 `dec_width`/`hex_width` 로 미리 잰다(u64 10 진 최대 20 자리, 16 진 최대
  16 자리, i64 는 부호 1 자리 추가).
- **`pos` 는 인덱스이자 길이다.** 조립이 끝난 시점의 마지막 pos 는 "지금까지 쓴 바이트 수"
  와 같다 — 그래서 `subslice buf 0 pos` 가 곧 결과 문자열이다.
- **버퍼를 재사용하려면 pos 를 0 으로 되돌리기만 하면 된다.** 지난 내용을 지울 필요는 없다.
  앞에서부터 덮어쓰고 그때의 pos 까지만 내보내면 남은 옛 바이트는 보이지 않는다.
- `put_nl` 은 LF 만 쓴다. CRLF 가 필요한 프로토콜이면 `put_str buf pos "\r\n"` 을 직접
  쓴다 — 라이브러리는 변환하지 않는다.
- 모든 op 이 `effects none` 이므로 fn 안에서도, 액터 핸들러 안에서도 자유롭게 부를 수
  있다. 출력할 때만 `cap io` 와 `effects io` 가 필요해진다.
- **`put_u64` 는 접두사도 부호도 안 붙인다.** `0x`·`+`·천 단위 쉼표가 필요하면 호출자가
  `put_str`/`put_byte` 로 직접 붙인다. 자릿수 맞춤(패딩)도 마찬가지로 호출자 몫이다.
- op 이름에 모듈 접두사가 없다 — 밖에서는 `fmt.put_u64` 처럼 항상 모듈 한정으로 부르므로
  다른 모듈의 이름과 겹치지 않는다.
</content>
</invoke>
