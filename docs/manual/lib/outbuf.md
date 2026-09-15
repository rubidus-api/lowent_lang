# outbuf — 버퍼링 출력, flush 망각은 컴파일 오류

소스: `lib/out.low` · 모듈명 `outbuf`

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 출력을 **모아 두었다가 한 번에** 내보내는 버퍼다.

**언제 쓰나.** 한 줄씩 쓰면 그때마다 시스템 호출이 나가 느리다. 모아서 한 번에 내보낼 때 쓴다.

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use outbuf .

rem ① 라이터를 연다. 1 = 표준 출력. 아직 아무것도 안 나간다.
var p owned outbuf.pending be outbuf.open 1 .
rem ② 조각을 쌓는다. out = cap io(쓸 권한 — 권한이 먼저 온다), buf = 쌓을 자리(호출자 것).
let w result (owned outbuf.pending) outbuf.io_error . be outbuf.write out p buf "hi\n" .
guard is_ok w . else return 1 .
set p (ok_value w) .                        rem ③ 새 상태를 이어받는다 — 빠뜨리면 컴파일 오류
rem ④ ★ 완결. 남은 것을 내보낸다. 잊으면 컴파일이 거절한다(E-OWN-INCOMPLETE).
let r result void outbuf.io_error . be outbuf.finish out p buf .
```

**읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## 왜 있는가

쉬운 요약: 짧은 출력을 여러 번 내보내면 그때마다 운영체제를 부르느라 느리다. 그것을
버퍼에 모았다가 한 번에 내보내는 도구이고, 마지막에 반드시 `finish` 를 부르게 만들어
"모아 둔 것을 안 내보내고 끝내는" 사고를 컴파일 단계에서 막는다.

이 모듈은 **작은 출력 조각이 여러 번 나올 때** 쓴다 — 조각을 버퍼에 모아 두었다가 한
번에 내보내 시스템 호출(syscall) 수를 아낀다. flush(플러시)는 그렇게 모인 바이트를
실제 출력으로 내보내 버퍼를 비우는 일이다.

버퍼링 출력이다(RFC-0069 §3 · A6). `write_out` 은 호출마다 fflush 하므로 조각마다 부르면
조각마다 syscall 이다 — 이 라이터는 바이트를 호출자 버퍼에 쌓았다가 가득 찰 때만 비워
syscall 수를 **바이트 수에서 버퍼 수로** 줄인다.

빌트인이 아닌 이유는 리프 규칙(SPEC-002 부록 P)이다: *"Lowent 로 쓸 수 있으면
라이브러리다."* 버퍼링·언제 비울지 판단·완결 규율은 전부 Lowent 로 쓸 수 있고, 세상에
닿는 것은 이미 있는 `write_out` 하나다. RFC-0069 §7 은 리프 3 개를 예산 잡았지만 규칙을
적용하니 **0 개**였다. **빌트인 op 증가 0.**

**cap 이 왜 필요한가.** cap(capability·능력)은 *무엇을 할 권한을 나타내는 값*이다 — 안
들고 있으면 그 일을 못 한다. 다른 언어에서는 어느 함수 안에서든 `print` 를 부를 수 있다.
Lowent 에는 그런 **전역 권한이 없다**: "표준 출력에 쓸 권한"(`cap io`)은 프로그램이
시작될 때 `main` 의 입력으로 딱 한 번 건네지고, 그 값을 **넘겨받은 op 만** 출력할 수 있다.
그래서 어떤 op 이 화면에 무언가를 쓰는지 시그니처만 보면 알고, 라이브러리가 뒤에서 몰래
찍는 일이 원리적으로 없다.

그 규율이 이 라이터에도 그대로 적용된다. 라이터는 `cap io` 를 **품지 않는다** — cap 은
값이 아니라 정적 토큰이라 구조체에 담을 수 없다. 그래서 실제로 바이트가 나가는
지점(`flush`·`write`·`finish`)마다 호출자가 권한을 대야 한다 ⇒ **라이터가 몰래 flush 할
수 없다.** 반대로 말하면, 시그니처에 `cap io` 가 없는 op 은 아무리 라이브러리를 불러도
화면에 한 글자도 못 찍는다.

## 설계 의도와 경계

쉬운 요약: 쓰는 동안은 `owned pending` 을 값으로 계속 이어받고, 끝은 반드시 `finish` 다
— 안 부르면 컴파일이 안 된다.

- **소유되는 것은 버퍼가 아니라 *아직 안 나간 바이트* 다.** 소유 값(owned)은 "정해진
  op 으로 반드시 끝내야 하는 값" 이다 — 조용히 버리면 컴파일 오류. 버퍼는 호출자 것으로
  두고(`lib/fmt.low` 과 같은 규율), `owned` 로 지키는 것은 flush 되지 않은
  상태(`pending`)다. 잃으면 안 되는 바로 그것이 소유된다.
- **flush 망각이 컴파일 오류다.** `finish` 가 `owned pending` 을 받고 `result` 를
  내므로, 그런 값을 조용히 버리면 **E-OWN-INCOMPLETE**(RFC-0058)다. 어노테이션이 아니라
  시그니처에서의 추론이다. Zig 는 flush 를 잊으면 출력이 통째로 사라지고, Rust 의
  BufWriter 는 Drop 에서 flush 하되 오류를 버린다 — 여기는 컴파일이 안 된다.
- **부분 쓰기는 실패다**(Go 의 "short write 는 에러" 규율) — 쓴 수가 요청과 다르면
  `write_failed`.
- 안 지은 것(소스가 명시): 벡터 쓰기(writev)·`write_splat` · 실패한 flush 뒤의 재시도 ·
  `wait`(중단 가능 출력).

## 자료구조

쉬운 요약: 라이터가 기억하는 것은 "버퍼에 몇 바이트나 쌓였나" 와 "어디로 내보내나" 둘
뿐이다. 바이트가 실제로 쌓이는 자리(버퍼)는 호출자 것이라 여기 안 들어 있다.

```lowent
export enum io_error do
  write_failed
end

rem 아직 안 나간 바이트의 상태. 스칼라만 담는다(버퍼는 호출자 것).
export struct pending do
  pos u64 .    rem 버퍼에 쌓인 바이트 수
  fd  u64 .    rem 어디로 비울지 — 1=stdout · 2=stderr
end
```

`pending` 은 `open` 이후 항상 `owned` 로 다뤄지며 `finish` 만이 완결한다. 여기서
**소유 값(owned)**은 "정해진 op 으로 반드시 끝내야 하는 값" 이다 — 그냥 버리면 컴파일이
거절한다. 그래서 `pending` 을 손에 들고 있는 한 컴파일러가 `finish` 를 잊지 않게 감시해
준다.

## op 한눈에

쉬운 요약: 네 개뿐이다. `open` 으로 열고, `write` 로 조각을 쌓고(가득 차면 알아서 나간다),
`finish` 로 끝낸다. `flush` 는 "지금 당장 내보내라" 를 직접 시킬 때만 쓴다.

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `open` | fn | `(d u64) → pending` | 실패 없음(effects none) |
| `flush` | proc | `(p owned pending, buf mut slice u8, out cap io) → result (owned pending) io_error` | `error write_failed` |
| `write` | proc | `(p owned pending, buf, out cap io, s slice u8) → result (owned pending) io_error` | `error write_failed` |
| `finish` | proc | `(p owned pending, buf, out cap io) → result void io_error` | `error write_failed` |

`open` 만 `effects none`, 나머지는 `effects io` + `errors write_failed` 다.

## op 상세

쉬운 요약: 세 op(`flush`/`write`/`finish`)이 매개변수를 똑같이 셋 받는다. 아래에 각각
**왜 받는지**를 적었다.

- `p` (`owned pending`) — 아직 안 나간 바이트의 상태. 소유 값이라 호출 때마다 **소비되고**
  새 상태가 반환으로 나온다. 그래서 `set p (ok_value w)` 로 매번 이어받아야 한다.
- `buf` (`mut slice u8`) — 바이트가 실제로 쌓이는 자리. 라이브러리가 메모리를 안
  잡으므로(할당은 권한이 필요한 일이다) 쌓을 자리는 호출자가 준다. **라이터 하나에 버퍼
  하나**를 붙박이로 쓴다.
- `out` (`cap io`) — 바이트가 세상에 나가는 순간의 권한. 라이터가 cap 을 품을 수 없어
  나가는 지점마다 호출자가 댄다. 이 인자가 없으면 이 op 은 아무것도 내보내지 못한다.

### open

```lowent
export fn open input d u64 . output pending .
```

fd `d`(1=stdout, 2=stderr) 로 비우는 라이터를 연다. `d` 가 필요한 이유: 같은 라이터
코드로 표준 출력에도 표준 오류에도 쓸 수 있어야 하기 때문이다. 여기서는 아직 세상에 안
닿으므로 `cap io` 도 필요 없다(`effects none`). 다만 이 값을 `finish` 로 끝내지 않고
버리면 컴파일 오류다 — 여는 순간 갚아야 할 빚이 생긴다고 보면 된다.

### flush

```lowent
export proc flush input out cap io . input p owned pending . input buf mut slice u8 . .
  output result (owned pending) io_error . effects io .
errors write_failed .
```

flush(플러시)는 **모아 둔 바이트를 실제로 내보내고 버퍼를 비우는 일**이다. 쌓인
바이트(`buf[0..pos]`)를 `write_out` 으로 내보낸다 — **여기서만 세상에 닿는다.**
성공하면 pos 0 으로 리셋된 새 `owned pending` 을 `ok` 로 돌려준다. 부분 쓰기면
`error write_failed`(이때 pending 은 소비되어 사라진다 — 재시도는 안 지었다).

보통은 직접 부를 일이 없다 — `write` 가 가득 찰 때 알아서 부르고, 마지막 꼬리는 `finish`
가 처리한다. 직접 쓰는 경우는 "지금 이 줄이 당장 화면에 보여야 한다" 같은 때다.

### write

```lowent
export proc write input out cap io . input p owned pending . input buf mut slice u8 . .
  input s slice u8 .
  output result (owned pending) io_error . effects io .
errors write_failed .
```

`s`(쌓을 조각 — 이번에 출력할 바이트열)의 바이트를 버퍼에 쌓는다. 가득 차면 그때 스스로
비운다(내부에서 `flush`). 그래서 버퍼가 작아도 동작은 맞고, 크기는 속도에만 영향을 준다.
소유를 소비하고 새 상태를 `ok` 로 돌려주므로, 호출자는 `set p (ok_value r)` 로 이어받는다
— 이 한 줄을 빠뜨리는 것이 가장 흔한 실수다.

### finish

```lowent
export proc finish input out cap io . input p owned pending . input buf mut slice u8 . .
  output result void io_error . effects io .
errors write_failed .
```

**완결.** 남은 꼬리를 비우고 `pending` 을 소비한다. 여기서는 새 `pending` 을 안 돌려준다
(`result void`) — 끝났으니 더 쓸 수 없다는 뜻이다. 이 선언 하나가 `pending` 을 "완결이
필요한 타입" 으로 만들고, 그래서 안 부르면 컴파일이 안 된다.

## 사용법과 예제

쉬운 요약: 아래는 "line 1" ~ "line 5" 를 찍는 완전한 프로그램이다. 그대로 따라 하면 되는
뼈대이니, 처음에는 `main` 의 모양(권한을 받는 자리)과 소유 값을 이어받는 `set p …` 줄만
눈에 익히면 된다.

`impl/tests/prog/emit.low` 의 모양이다. 포매팅(`fmt`, 순수 — 바이트를 조립만 한다)과
버퍼링 출력(`outbuf`, `cap io` — 실제로 내보낸다)의 분업을 그대로 보인다.

```lowent
module emit .

use fmt    from "../lib/fmt.low" .
use outbuf from "../lib/out.low" .

rem entry 패턴: 프로그램의 시작점은 이름이 main 이고, 그 input 은 전부 cap 이다 —
rem 바깥세상에 닿을 권한은 여기서 딱 한 번 건네받아 필요한 op 에 넘겨 준다.
rem 필요한 권한만 적으면 된다: 파일을 다루면 input fs cap file_system . 을 한 줄 더 적는다.
proc main
  input out cap io .          rem 표준 출력에 닿을 권한 — 이게 없으면 한 글자도 못 찍는다
  input al  cap allocator .   rem 버퍼를 얻을 권한 — 메모리도 그냥 생기지 않는다
  output u8 .                 rem 프로세스 종료 코드(0 = 정상)
  effects alloc io .          rem 이 proc 이 내는 비용 신고 — 빠뜨리면 E-EFFECT
do
  rem 출력 버퍼(작아도 된다 — 차면 스스로 비운다).
  let g option mut slice u8 . . be alloc_bytes al capacity 16 .
  guard is_some g . else return 70 .   rem 할당 실패면 여기서 끝낸다(guard 는 반드시 나간다)
  let buf mut slice u8 . be some_value g .
  rem 한 줄을 조립할 자리 — 위 buf 와는 다른 버퍼여야 한다(용도가 섞이면 안 된다).
  let ng option mut slice u8 . . be alloc_bytes al capacity 32 .
  guard is_some ng . else return 71 .
  let nb mut slice u8 . be some_value ng .

  rem 라이터를 연다(1 = stdout) — 이제 p 는 finish 로 끝내야만 하는 소유 값이다.
  var p owned outbuf.pending be outbuf.open 1 .

  var i u64 be 1 .
  while le i 5 . do
    rem 순수한 포매팅(fmt) → 그 조각을 버퍼링 라이터에 넘긴다.
    rem fmt 는 "다음에 쓸 자리" 를 돌려주므로, 그 값을 다음 호출의 위치로 이어 준다.
    let a option u64 . be fmt.put_str nb 0 "line " .   rem 0 부터 쓴다 → 다음 자리 a
    guard is_some a . else return 72 .                 rem none = 자리 부족(nb 가 작다)
    let b option u64 . be fmt.put_u64 nb (some_value a) i .   rem a 자리부터 숫자
    guard is_some b . else return 73 .
    let c option u64 . be fmt.put_nl nb (some_value b) .      rem 줄바꿈까지 → 총 길이 c
    guard is_some c . else return 74 .
    rem 조립한 만큼만(subslice nb 0 c) 라이터에 쌓는다 — 아직 화면에 안 나간다.
    rem 소유를 값으로 이어받는다: 소비하고 새 상태를 돌려받아 그 자리에 다시 넣는다.
    rem set 은 사용이 아니라 재초기화라 루프 안에서도 소유가 유지된다.
    let w result (owned outbuf.pending) outbuf.io_error .
      be outbuf.write out p buf (subslice nb 0 (some_value c)) .
    guard is_ok w . else return 75 .   rem 쓰기 실패(write_failed)면 종료
    set p (ok_value w) .               rem ★ 이 줄을 빠뜨리면 다음 write 에서 컴파일 오류다
    set i (add i 1) .
  end

  rem ★ 완결. 남은 꼬리를 내보낸다 — 이 두 줄을 지우면 컴파일이 안 된다(E-OWN-INCOMPLETE).
  let f result void outbuf.io_error . be outbuf.finish out p buf .
  guard is_ok f . else return 76 .
  return 0 .
end
```

돌려 보면 `line 1` 부터 `line 5` 까지가 한 번에 나온다 — 다섯 줄을 다섯 번 내보낸 것이
아니라 버퍼가 찰 때와 `finish` 때만 실제로 나갔다.

## 반례 — 이렇게 쓰면 안 된다

쉬운 요약: 넷 중 셋은 **컴파일러가 잡아 준다**(①②③) — 초보자가 이 모듈에서 데이터를
잃을 일은 거의 없다는 뜻이다. 조용히 틀리는 것은 ④ 하나뿐이니 그것만 조심한다.

**① `finish` 생략.** `open`/`write` 로 얻은 `owned pending` 을 반환 전에 완결하지
않으면 증상은 컴파일 오류 **E-OWN-INCOMPLETE** 다. 남은 바이트가 있는데 조용히 버릴 수
없다 — 이것이 이 라이브러리의 존재 이유다.

**② `cap io` 없이 비우기.** `flush`/`write`/`finish` 는 `cap io` 파라미터가 필수다.
증상은 컴파일 오류다: 이를 부르는 op 이 `effects io` 를 선언하고도 어떤 cap 도 안 받으면
**E-EFFECT-NO-CAP**, `effects io` 자체를 빼먹으면 **E-EFFECT** 다. 고치는 법은 대개
"내 proc 시그니처에도 `input out cap io .` 를 적고, 부르는 쪽에서 넘겨준다" 이다 —
권한은 위에서 아래로만 흐른다.

**③ 이동한 `p` 재사용.** `write p …` 뒤에 `set p (ok_value w)` 없이 옛 `p` 를 다시
넘기는 경우다. 증상: 컴파일 오류(이동된 값의 사용 — E-EXCL-MOVED 계열). 소유는 호출마다
소비되고 반환값으로만 이어지므로, `write` 뒤에는 항상 `set p …` 가 붙는다고 외워 두면 된다.

**④ `write`/`finish` 사이에 버퍼를 다른 용도로 재사용.** `pending.pos` 는 그 버퍼에
쌓인 바이트 수를 가리킨다. 증상: 컴파일은 통과하지만 안 나간 바이트가 오염되어 엉뚱한
내용이 출력된다 — 라이터 하나에 버퍼 하나를 붙박이로 쓴다.

## 주의사항

쉬운 요약: "출력이 안 보인다" 면 대개 `finish` 를 안 불렀거나 버퍼를 딴 데 같이 썼기
때문이다. 앞의 것은 컴파일러가 막아 주니, 실제로 남는 함정은 뒤의 것이다.

- **출력이 하나도 안 보일 때.** 버퍼에만 쌓이고 아직 안 나간 것이다. `finish` 를 부르는
  경로가 정말 실행되는지 본다 — 중간에서 `return` 으로 빠져나가면 그쪽 경로에서도 완결이
  필요하다(그래서 컴파일러가 잡는다).
- **버퍼는 라이터 전용으로 둔다.** 조립용 버퍼(`fmt` 가 쓰는 것)와 라이터의 버퍼를 같은
  것으로 쓰면 안 나간 바이트를 덮어쓴다 — 위 예제가 `buf` 와 `nb` 를 따로 잡은 이유다.
- op 이름에 접두사가 없는 이유: 밖에서는 `outbuf.open` 처럼 항상 모듈 한정으로 부르므로
  `files.open` 과 겹쳐도 모호하지 않다. 한정 없이 불리는 이름(액터 메시지 등)은 사정이
  다르다 — io 의 `attach` 가 그 흉터다(io 매뉴얼 주의사항 참조).
- `flush` 실패(`write_failed`)는 pending 을 소비한다 — 그 시점 버퍼에 있던 바이트는
  잃는다. 재시도 로직은 아직 없다(소스가 "안 지은 것" 으로 명시).
- fd 는 열림이 검사되지 않는 정수다. 1·2 이외를 넣는 것은 리프(`write_out`)의 규약에
  달렸다.
- 입력 전체가 이미 한 버퍼에 있어 출력을 한 번에 낼 수 있으면 라이터가 필요 없다 —
  `write_out` 한 번이면 된다(`prog/nl.low` 가 그 경우다). 라이터는 조각이 여러 번 올 때
  syscall 을 줄이는 도구다.
