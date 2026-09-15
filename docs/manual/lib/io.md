# io — 슬라이스 위의 스트림 읽기(mem 리더)

소스: `lib/io.low` · 모듈명 `io`

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 메모리에 이미 있는 바이트열을 **스트림처럼 읽는** 모듈이다.

**언제 쓰나.** 파일을 통째로 읽어 놓고 줄 단위로 훑을 때, 파서에 먹일 커서가 필요할 때 쓴다.

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use io .

var r io.mem_reader be spawn actor io.mem_reader . .
let z u64 be send r attach src .            rem ① 읽을 바이트열을 붙인다(이걸 빼면 아무것도 안 읽힌다)
let line option slice u8 . be send r take_line 4096 .   rem ② 한 줄 — 4096 은 필수 상한이다
```

`option`(있을 수도 없을 수도 있는 값)이 결과인 이유는 입력이 끝났을 수도 있어서다 —
`guard is_some line .` 로 갈라 쓴다.

**읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## `none` 은 실패가 아니다 (2026-08-15)

이 모듈의 `peek`·`take`·`take_until`·`take_line` 은 모자라면 `none` 을 답한다. 그 `none` 은
**부재**다 — *"청한 만큼의 바이트가 남아 있지 않다"*. **실패가 아니다.**

`mem_reader` 는 메모리 위 슬라이스를 읽는다. 핸들러 전부가 산술이고 세상에 닿지 않으므로
(`effects state` 뿐) **실패할 수 있는 자리가 없다.** 그래서 `lib/file.low` 이 2026-08-15 에
세 자리(`result (option T) file_error`)로 옮겨 갈 때 **이 모듈은 따라가지 않았다**: 여기에
`result` 를 붙이면 절대 생기지 않는 `error` 가지를 부르는 쪽마다 만들게 된다.

☞ 세 자리가 필요해지는 자리는 **fd 를 읽는 리더**다(아직 없다 — RFC-0069 §6.5 의 "제안" 목록).
  그것이 지어지면 그쪽이 세 자리로 태어난다.


## 왜 있는가

쉬운 요약: 이미 메모리에 들어온 바이트 덩어리를 "줄 하나 주세요" 처럼 조금씩 꺼내 쓰기
위한 모듈이다. 커널에 직접 닿지 않으므로 권한(cap)이 필요 없다.

이 모듈은 **한 덩어리로 들어온 바이트를 조각(줄·필드·프레임)으로 잘라 읽을 때** 쓴다.
파일이든 stdin 이든 일단 버퍼에 들어오면, "줄 단위로 하나씩 달라" 는 그다음 일이 여기
있다.

IO 의 **mem 절반**이다(RFC-0069 §1·§2). 읽기 표면을 `read(buf) → n` 이 아니라
**peek/take/toss → 뷰**로 잡는다 — 버퍼를 리더가 소유하니 호출자 메모리를 채우는 대신
**빌린 뷰**(복사가 아니라 원본의 한 구간을 가리키는 슬라이스)를 건넨다(복사 0).
`stream_kind` 는 `os_fd | mem` 인데 그 분할이 검증 가능성과 일치한다: mem 은 순수
슬라이스 조작이라 VM ≡ native(오라클 안)이고, os_fd 는 커널 의존이라 밖이다. 그래서
프레이밍 로직 전부(버퍼 관리·`take_line`)를 **커널을 건드리기 전에** 검증해 두고, 커널
읽기(`read_in` — `cap io` 필요)는 이 검증된 로직에 바이트를 공급하는 얇은 리프로 남는다.
`impl/tests/prog/wc.low` 가 그 계획의 실물이다: 파일에서 읽은 바이트를 **한 글자도 안
바뀐** `mem_reader` 가 먹는다.

빌트인이 아닌 이유: 슬라이스 인덱싱뿐이라 Lowent 로 쓸 수 있다. **빌트인 op 증가 0** —
언어가 이미 가진 것(actor·option·slice·subslice·guard·while)만 썼다.

**cap 이 무엇인지 — 그리고 왜 여기엔 없는지.** cap(capability·능력)은 *무엇을 할
권한을 나타내는 값*이다 — 안 들고 있으면 그 일을 못 한다. 다른 언어에서는 어디서든
`print` 를 부를 수 있다. Lowent 에는 그런 **전역 권한이 없다**: "화면에 쓸 권한"·"파일을
열 권한" 은 프로그램이 시작될 때 `main` 의 입력으로 딱 한 번 건네지고, 그 값을 **손에서
손으로 넘겨받은 op 만** 세상에 닿을 수 있다. 그래서 어떤 op 이 바깥에 무슨 짓을 하는지
시그니처만 보면 안다 — 숨은 출력·숨은 파일 접근이 원리적으로 없다.

그러면 이 모듈 자체는 **cap 이 필요 없다.** 이미 메모리에 있는 바이트를 자르기만 하지
커널에 닿지 않기 때문이다. 바이트를 실제로 채워 오는 쪽(`read_in` 은 `cap io`,
`files.read` 는 `cap file_system`)만 권한을 요구한다 — 아래 예제의 `main` 이 그
분업(권한 필요한 한 줄 + 권한 없는 순수 로직)을 보여 준다.

## 설계 의도와 경계

쉬운 요약: 입력의 끝(EOF)은 실패가 아니고, 긴 읽기에는 상한이 필수이고, 못 주면
아무것도 소비하지 않는다.

- **EOF 는 오류가 아니라 `none`.** EOF(end of file — 입력이 끝났다는 사실)는 실패가
  아니다. Go 의 센티널도 Rust 의 `Ok(0)` 애매함도 없다.
- **상한(limit)이 필수 인자다** (`take_until`·`take_line`·`scan_until`) — 기본값이 없어,
  적대적 입력이 무한 읽기를 유발할 수 없다.
- **부분 소비 없음.** `take`·`take_until`·`take_line` 은 못 주면 `none` 이고 커서를
  움직이지 않는다.
- **개행 규약은 LF 만.** CR 은 내용에 남는다. CRLF 를 벗기려면 호출자가
  `remove_suffix line "\r"` 를 쓴다 — 라이브러리가 조용히 바이트를 지우지 않는다
  (지우면 되돌릴 수 없고, 안 지우면 호출자가 고를 수 있다).
- mem 스트림은 실패할 수도 중단될 수도 없으므로 `result` 도 `effects wait` 도 두르지
  않는다 — `option` + `effects state` 뿐이다. os_fd 가 오면 그쪽이 `result`·`io wait` 를
  실제로 그럴 수 있으므로 두른다. API 포크가 아니라 능력의 차이다.
- 위치 지정은 순수 op(`scan_until`)이 원시연산이고, 상태 있는 커서(actor)는 그 위의
  얇은 껍질이다. 같은 소스를 여러 커서가 동시에 훑어도 자유롭다.

## 자료구조

쉬운 요약: 기억해야 하는 것은 "어느 바이트열을 읽는 중인가" 와 "어디까지 읽었나" 둘
뿐이고, 그 둘을 actor 하나가 들고 있다.

struct/enum 은 없다. 상태는 actor `mem_reader` 하나가 든다. actor 는 상태(state)를 품고
메시지로 부리는 객체다 — `spawn actor io.mem_reader` 로 만들고
`send r <핸들러> <인자…>` 로 부린다. 여기서 커서(cursor)는 "다음에 읽을 자리" 를 가리키는
정수이고, 뷰(view)는 **원본을 복사하지 않고 그 일부 구간만 가리키는 슬라이스**다.

```text
export actor mem_reader do
  state do
    src slice u8 .    rem 원본 슬라이스 — 반환되는 뷰는 전부 이것을 가리킨다
    pos u64 .         rem 커서 — 다음에 읽을 자리
  end
```

## op 한눈에

쉬운 요약: 아래 표만 봐도 대부분 된다. 커서를 안 움직이고 훔쳐보는 것(`peek`)과 소비하는
것(`take*`)이 갈리고, 실패는 전부 `none` 이라는 **값**이다(예외도 트랩도 아니다).
effect(그 op 이 무슨 비용을 내는지 — `none` 은 아무 흔적도 안 남긴다는 뜻)는 표 아래에 적었다.

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `scan_until` | fn | `(src slice u8, from u64, delim u8, limit u64) → option u64` | limit 안에 없거나 끝이면 `none` |
| `attach` | actor proc | `(s slice u8) → u64` | 실패 없음(항상 0) |
| `remaining` | actor proc | `() → u64` | 실패 없음 |
| `peek` | actor proc | `(n u64) → option slice u8` | 남은 게 n 미만이면 `none` |
| `take` | actor proc | `(n u64) → option slice u8` | 모자라면 `none`, 커서 불변 |
| `take_rest` | actor proc | `() → slice u8` | 실패 없음(끝이면 빈 뷰) |
| `toss` | actor proc | `(n u64) → u64` | 실패 없음(끝까지만 버림) |
| `take_until` | actor proc | `(delim u8, limit u64) → option slice u8` | 못 찾으면 `none`, 커서 불변 |
| `take_line` | actor proc | `(limit u64) → option slice u8` | LF 를 limit 안에 못 찾으면 `none` |

`scan_until` 은 `effects none`, actor proc 들은 전부 `effects state` 다.

## op 상세

쉬운 요약: 매개변수마다 "왜 이걸 받는지" 를 함께 적었다. 라이브러리가 기본값을 정해 주지
않는 자리(특히 `limit`)는 **호출자만 답을 아는 물음**이라서 인자로 나와 있다.

### scan_until

```lowent
export fn scan_until input src slice u8 . input from u64 . input delim u8 . input limit u64 .
  output option u64 .
```

`from` 부터 `delim` 을 찾되 **limit 바이트까지만** 본다. 매개변수 넷이 왜 필요한가:

- `src` — 어디서 찾는가. 이 op 은 상태를 안 들기 때문에 볼 대상을 매번 받아야 한다.
- `from` — 어디부터 찾는가. 커서를 라이브러리가 아니라 **호출자가 들고 있어서** 필요하다.
- `delim` — 무엇을 만나면 멈추는가(구분자 바이트 하나). 줄이면 LF=10 이다.
- `limit` — 최대 몇 바이트까지만 볼 것인가. 기본값이 없는 이유는 적대적 입력 방어다:
  상한이 없으면 구분자가 안 나오는 입력 하나로 프로그램을 오래 붙잡을 수 있다.

찾으면 그 인덱스를 `some` 으로, limit 안에 없거나 끝에 닿으면 `none`. 커서 없는 순수
원시연산 — 호출자가 pos 를 값으로 들고 싶을 때 직접 쓴다.

### attach / remaining

`attach s` — 소스를 건다(커서 0). `s` 가 필요한 이유: 리더는 빈손으로 태어나므로 **무엇을
읽을지 먼저 알려 줘야** 한다. 같은 리더에 다시 걸면 처음부터 다시 읽는다 — 리셋
겸용이다. 반환은 0(뜻 있는 답이 없어 고정된 값이다) — 반환을 안 받으면 안 되므로
예제에서도 `let z u64 be …` 로 버리는 변수에 담는다. 이름이 `open` 이 아닌 이유는
주의사항의 이름 충돌 항목을 보라.

`remaining` — 남은 바이트 수. 인자가 없는 이유: 답이 리더의 상태(커서와 원본 길이)에만
달렸기 때문이다. `take_line` 이 `none` 을 낼 때 "끝나서" 인지 "구분자를 못 찾아서" 인지
가르는 데 이것을 쓴다.

### peek / take

`peek n` 은 n 바이트를 **소비하지 않고** 본다(다음에 무엇이 오는지 확인만 하고 커서는
제자리). `take n` 은 소비하고 그 **뷰**를 낸다(복사 0 — 뷰는 원본 슬라이스를 가리킨다).
`n` 이 인자인 이유: 얼마나 원하는지는 프로토콜(호출자)만 알기 때문이다 — 라이브러리는
"한 조각" 이 몇 바이트인지 알 길이 없다. 둘 다 남은 것이 n 미만이면 `none` — 부분으로
속이지 않고, `take` 실패 시 커서도 안 움직인다(그래서 실패 뒤에 다른 길이로 다시 시도해도
안전하다).

### take_rest / toss

`take_rest` 는 남은 전부를 소비하고 뷰를 낸다. 인자가 없는 이유: "남은 전부" 는 리더가
이미 아는 양이다. 이미 끝이면 **빈 뷰**다 — `none` 이 아니다("끝났다" 와 "0 바이트
읽었다" 는 다르고, 여기선 후자다). `toss n` 은 n 바이트를 버린다(`n` = 건너뛸 양 —
헤더처럼 내용이 필요 없는 구간에 쓴다). 남은 것보다 많이 버리라면 끝까지만 버리고 실제
버린 수를 돌려준다.

### take_until / take_line

`take_until delim limit` — 구분자까지 읽는다. **구분자는 소비하되 뷰에는 넣지 않는다**
(즉 돌려받은 줄에는 개행이 안 붙어 있다). limit 안에 못 찾으면 `none` 이고 커서는 안
움직인다. 이미 끝이어도 `none`. `take_line limit` 은 `take_until 10 limit`(LF) 와 같은
동작이다. 매개변수가 왜 필요한가:

- `delim` — 어디서 끊을 것인가. 줄이 아니라 탭·쉼표로 끊고 싶을 때 여기를 바꾼다.
- `limit` — 한 조각이 최대 몇 바이트까지 정당한가. 기본값을 주면 **아무도 안 정한 상한**이
  되어 버린다: 한 줄이 정당하게 길어야 얼마인지는 그 데이터를 아는 호출자만 안다.

## 사용법과 예제

쉬운 요약: 아래 프로그램은 그대로 파일에 붙여 넣고 돌릴 수 있는 꼴이다. 읽는 로직은
권한 없는 순수한 부분에 두고, 커널에 닿는 한 줄만 `main` 이 받은 cap 을 쓴다.

줄 단위 프레이밍. `impl/tests/vm_io.low`·`prog/nl.low` 의 축약이다. 개행 없이 끝난
마지막 조각은 `take_line` 이 못 주므로 `take_rest` 로 거둔다.

```lowent
module lines .

use io from "../lib/io.low" .

rem 순수한 로직 — 파일도 stdin 도 모른다. 그래서 어느 입력이든 같은 코드가 먹는다.
rem effects state 는 리더 커서 때문이고, io 도 alloc 도 없다.
proc count_lines input src slice u8 . output u64 . effects state . do
  rem 리더를 하나 만든다 — 아직 빈손이라 무엇을 읽을지 모른다.
  var r io.mem_reader be spawn actor io.mem_reader . .
  rem 읽을 바이트열을 건다. 커서가 0 이 된다. 반환 0 은 뜻이 없어 z 에 버린다.
  let z u64 be send r attach src .
  var lines u64 be 0 .            rem 지금까지 센 줄 수
  var going bool be true .        rem 루프를 계속 돌지 — guard 가 아니라 if 로 고른다
  while going . do
    rem 한 줄 꺼낸다. 4096 = "한 줄은 최대 이만큼" 이라는 상한(필수 인자다).
    rem 결과가 some 이면 줄 하나, none 이면 (a) 끝이거나 (b) 4096 안에 개행이 없다.
    let l option slice u8 . be send r take_line 4096 .
    if is_some l . do set lines (add lines 1) . end
    if eq (is_some l) false . do set going false . end   rem none 이면 루프 종료
  end
  rem 개행 없이 끝난 꼬리도 한 줄이다 — 남은 바이트가 있으면 한 줄 더 센다.
  if gt (send r remaining) 0 . do set lines (add lines 1) . end
  return lines .
end

rem entry 패턴: 프로그램의 시작점은 이름이 main 이고, 그 input 은 전부 cap 이다 —
rem 바깥세상에 닿을 권한은 여기서 딱 한 번 건네받아 아래로 넘겨 준다.
rem 필요한 권한만 적으면 된다: 파일을 다루면 input fs cap file_system . 을 적는 식이다.
proc main input out cap io .          rem stdin/stdout 에 닿을 권한
  input al cap allocator .            rem 메모리를 얻을 권한
  output u8 .                         rem 프로세스 종료 코드
  effects alloc io state .            rem 이 proc 이 내는 비용을 정직하게 신고한다
do
  rem 읽을 자리를 마련한다 — 할당도 권한(al)이 있어야 한다.
  let g option mut slice u8 . . be alloc_bytes al capacity 16384 .
  guard is_some g . else return 70 .  rem 못 받았으면 여기서 끝낸다(guard 는 반드시 나간다)
  let buf mut slice u8 . be some_value g .
  rem 채우기는 커널 — 이 한 줄만 cap io 가 필요하다. 0 = stdin.
  let n option u64 . be read_in out 0 buf .
  guard is_some n . else return 71 .
  rem 읽은 만큼만 잘라(subslice) 권한 없는 순수 로직에 넘긴다.
  return (narrow u8 (count_lines (subslice buf 0 (some_value n)))) .
end
```

## 반례 — 이렇게 쓰면 안 된다

쉬운 요약: 아래 넷은 초보자가 실제로 자주 밟는다. **증상**을 함께 적었으니, 그 증상이
보이면 여기부터 의심하면 된다. 컴파일 오류로 잡히는 것(①③)과 조용히 답만 틀리는
것(②④)이 갈린다 — 무서운 쪽은 후자다.

**① limit 생략.** `send r take_line .` — 증상: **컴파일 오류**(인자 수 불일치)다. 상한은
기본값 없는 필수 인자다. "충분히 크게" 라고 아무 값이나 넣는 것도 상한을 무의미하게
만든다: 프레임의 정당한 최대 길이를 넣는다.

**② `take_line` 의 `none` 을 EOF 로 단정.** `none` 은 (a) 이미 끝이거나 (b) limit 안에
LF 가 없다는 뜻이다. 꼬리가 남았는지는 `remaining` 으로 구별한다 — `nl.low`·`wc.low` 가
그렇게 한다. 증상: 꼬리를 안 거두면 개행 없이 끝난 마지막 줄이 조용히 사라진다(컴파일은
통과한다).

**③ 루프 계속 여부를 `guard` 로 고르기.** `guard` 는 반드시 나가야 한다 — 증상은
컴파일 오류 **E-GUARD-FALLTHROUGH** 다. 한 번 더 돌지 말지는 `if` 로 고른다(위 예제의
`if eq (is_some l) false …` 패턴).

**④ CRLF 입력에서 뷰를 그대로 비교.** `take_line` 은 CR 을 지우지 않으므로 뷰는
`"data\r"` 다. 증상: `eq` 비교가 **항상 `false`** 다 — 눈으로 출력을 봐도 똑같아 보이는데
비교만 안 맞으면 이것을 의심한다(길이가 1 더 크다). 비교 전에 호출자가 `\r` 을 벗겨야
한다.

**⑤ 리더에 소스를 안 걸고 읽기.** `spawn` 만 하고 `attach` 를 빼먹으면 리더는 무엇을
읽어야 할지 모른다. 증상은 컴파일 오류가 아니라 **읽기가 곧장 `none`** — 줄이 하나도 안
나온다. 입력은 분명 있는데 결과가 0 줄이면 `attach` 부터 확인한다.

## 주의사항

쉬운 요약: 뷰는 원본을 빌린 것이니 원본이 살아 있어야 하고, `none` 은 "끝" 과 "못 찾음"
둘 다를 뜻하니 `remaining` 으로 갈라야 한다 — 이 둘이 이 모듈에서 제일 흔한 함정이다.

- **뷰를 오래 들고 있지 않는다.** `take_line` 이 준 슬라이스는 **복사가 아니다.** 원본
  버퍼를 다시 채우거나 버리면, 들고 있던 뷰는 이미 다른 내용을 가리킨다. 증상: 출력이
  뒤죽박죽이 된다. 오래 쓸 값이면 다른 버퍼로 복사해 둔다.
- **`none` 을 무조건 "끝" 으로 읽지 않는다.** 위 반례 ② 와 같은 함정이다. 상한을 넘긴
  긴 줄도 `none` 이라, 로그 한 줄이 유난히 길면 그때부터 조용히 아무것도 안 읽는 것처럼
  보인다.
- **`attach` 가 `open` 이 아닌 이유 — 이름 충돌이다.** 처음 이름은 `open` 이었는데
  `files` 모듈의 export `open` 과 겹쳐 효과 해석이 엉뚱한 쪽을 집었다. 두 모듈을 한
  프로그램에서 같이 쓰는 순간 터지는 종류의 충돌이고, 초보자도 자기 코드에서 만난다 —
  메시지·op 이름은 함께 쓸 모듈들과 겹치지 않게 짓는다.
- 반환된 뷰는 **원본 슬라이스를 빌린 것**이다. 원본이 유효한 동안만 유효하다.
- actor proc 은 전부 `effects state` 이므로 부르는 op 도 `state` 를 선언해야 한다
  (안 하면 E-EFFECT).
- 커서를 값으로 들고 싶으면(재진입·역추적) actor 대신 `scan_until` + 호출자 pos 를 쓴다.
- `attach` 를 다시 부르면 처음부터다 — 리셋 겸용이다.
