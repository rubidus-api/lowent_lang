# tty — 터미널 입력 (cap tty)

`lib/tty.low` · 모듈 이름 `tty` · 계층 L2(권한이 필요한 절반) + 순수 파싱

## 처음 쓰는 사람에게

에디터나 메뉴처럼 **키를 하나씩 받아 반응하는 프로그램**을 만들 때 쓰는 곳이다.

보통 터미널은 사용자가 Enter 를 눌러야 프로그램에 줄을 넘긴다. 그 전까지는 커널이 글자를 모아
두고, 화면에 되비쳐 주고, 백스페이스도 처리해 준다. 그것을 **행 버퍼링과 에코**라 부른다.
그런데 방향키를 누르는 즉시 커서를 옮기려면 이 편의가 방해가 된다 — 그래서 **raw 모드**로
바꿔서 "누른 즉시, 되비침 없이" 받는다.

그리고 방향키는 한 바이트가 아니다. `↑` 를 누르면 터미널은 **`ESC [ A` 세 바이트**를 보낸다.
`Delete` 는 `ESC [ 3 ~` 네 바이트다. 이 바이트열을 "위쪽 화살표" 로 읽어 주는 것이 `parse_key` 다.

```lowent
use tty .

rem 바이트열에서 키 하나를 읽는다.
let p option u64 . be tty.parse_key buf 0 .
guard is_some p . else return none .
let code u64 be tty.key_of (some_value p) .   rem 키 (1001 = ↑)
let used u64 be tty.len_of (some_value p) .   rem 먹은 바이트 수 (3)
```

**중요한 갈림**: 키를 **읽는 것**(커널)은 권한이 필요하고, 바이트를 **키로 해석하는 것**은
그냥 계산이다. 이 모듈은 그 둘을 갈라 뒀다 — `parse_key` 는 `effects none` 이라 화면 없이
테스트되고, 커널이 필요한 셋만 `cap tty` 뒤에 있다.

## 왜 있는가

**raw 모드는 사용자의 터미널 설정을 바꾼다 — 그리고 그 변경은 프로그램이 죽어도 남는다.**

raw 모드로 들어간 채 프로그램이 죽으면 사용자의 셸은 에코가 없는 상태로 남는다(타이핑이 안
보이는 셸을 만나 본 적이 있다면 그것이다). 그런 권한이 ambient 라면 — 즉 아무 코드나 부를 수
있다면 — **아무 라이브러리나 남의 셸을 망가뜨릴 수 있다.**

⇒ RFC-0011 의 답: 들고 있는 자만 행사한다. `cap tty` 를 안 든 리프에서 `tty_raw` 를 부르면
**컴파일 시각에** `E-CAP-MISSING` 이다. 런타임 실수가 아니라 타입 수준의 거절이다.

동시에, 키 파싱까지 권한 뒤에 두면 그 부분은 영영 테스트가 어려워진다(실제 터미널이 있어야
하니까). 그래서 반을 갈랐다 — **순수한 절반이 훨씬 크다.**

## 설계 의도와 경계

**① 반 가르기.** 순수 절반 = 키 파싱(여기) + ANSI 조립·화면 diff·표시 폭([term](term.md)).
권한 절반 = raw 모드 · 읽기 · 화면 크기, 셋뿐이다.

**② 키코드 하나로 글자와 특수키를 구별한다.** 평범한 바이트는 그 값 그대로(1–255), 특수키는
1000 위다. 그래서 `is_char` 는 `lt keycode 1000` 한 번이다. 별도 태그 필드도, 열거형도 필요 없다.

**③ 반환은 (키코드, 길이) 두 값이지만 튜플은 없다** — `키코드 * 16 + 길이` 한 값에 담는다
(길이는 1–8 이라 4비트면 넉넉하다). `key_of` / `len_of` 로 꺼낸다. 길이가 필요한 이유는
**다음 키가 버퍼의 어디서 시작하는지** 알아야 하기 때문이다.

**④ 불완전한 시퀀스를 지어내지 않는다.** `ESC [` 까지만 왔으면 `none` 이다 — "아마 방향키겠지"
라고 추측하지 않는다. 호출자가 더 읽어서 다시 부른다.

**단 하나의 예외**: `ESC` 하나만 오고 뒤가 **없으면** 그것은 ESC 키다(1010). 뒤가 안 올 수도
있는 상황에서 영원히 기다릴 수는 없기 때문이다 — 이 판단은 호출자가 버퍼를 어떻게 채웠느냐에
달려 있고, 문서로 말한다.

**안 지은 것(정직히)**: 마우스 보고 · bracketed paste · 터미널 질의 응답(DA/DSR) 파싱 ·
시그널(SIGWINCH) 연동. 각각 별도 슬라이스다.

## 자료구조

없다. 상수 op(키 코드)과 순수 함수뿐이다.

**키 코드**

| op | 값 | 키 |
|---|---|---|
| — | 1–255 | 그 바이트 자체(제어문자 포함 — Ctrl-C 는 3) |
| `key_up` | 1001 | ↑ |
| `key_down` | 1002 | ↓ |
| `key_right` | 1003 | → |
| `key_left` | 1004 | ← |
| `key_home` | 1005 | Home |
| `key_end` | 1006 | End |
| `key_delete` | 1007 | Delete |
| `key_pageup` | 1008 | PageUp |
| `key_pagedown` | 1009 | PageDown |
| `key_esc` | 1010 | ESC |

**포장 형식**: `packed = keycode * 16 + len`. 직접 산술하지 말고 `key_of` / `len_of` 를 쓴다
(형식이 바뀌어도 코드가 안 깨진다).

## op 한눈에

| op | 계층 | 하는 일 |
|---|---|---|
| `key_up` … `key_esc` | 순수 | 키 코드 상수 |
| `parse_key` | 순수 | 바이트열 → (키코드, 먹은 길이) |
| `key_of` / `len_of` | 순수 | 포장 값에서 꺼내기 |
| `is_char` | 순수 | 평범한 글자인가(특수키가 아닌가) |
| `tty_raw`(내장) | **cap tty** | raw 모드 진입/복귀 |
| `tty_read`(내장) | **cap tty** | 지금 온 바이트를 버퍼로 |
| `tty_size`(내장) | **cap tty** | 화면 크기 |

`tty_raw` / `tty_read` / `tty_size` 는 이 모듈의 op 이 아니라 **내장 op** 이다 —
`cap tty` 를 든 리프에서 직접 부른다(감쌀 껍질이 아무 것도 더할 게 없다).

## op 상세

### `parse_key` — 키 하나 (핵심)

```lowent
proc parse_key output option u64 . input buf slice u8 . input at u64 . effects none .
```

- `buf` — 터미널에서 읽어 둔 바이트열.
- `at` — 여기서부터 본다. `buf` 길이 이상이면 `none`.
- 반환 — `some (키코드 * 16 + 길이)`, 또는 `none`.

**`none` 이 뜻하는 것은 하나다: "바이트가 모자라거나 모르는 시퀀스다."** 앞의 것이면 더 읽고
다시 부르고, 뒤의 것이면 한 바이트를 버리고 넘어간다. 이 둘을 구별하고 싶다면 호출자가 "더 올
바이트가 있는가" 를 알고 있으므로 그쪽에서 판단한다.

인식하는 시퀀스:

| 입력 | 결과 |
|---|---|
| ESC 아닌 한 바이트 | 그 값, 길이 1 |
| `ESC` (뒤 없음) | 1010(ESC), 길이 1 |
| `ESC O A`–`D`,`H`,`F` | 방향키·Home·End, 길이 3 (응용 커서 모드) |
| `ESC [ A`–`D`,`H`,`F` | 방향키·Home·End, 길이 3 |
| `ESC [ 1 ~` | Home, 길이 4 |
| `ESC [ 3 ~` | Delete, 길이 4 |
| `ESC [ 4 ~` | End, 길이 4 |
| `ESC [ 5 ~` / `6 ~` | PageUp / PageDown, 길이 4 |
| 그 밖의 `ESC [ …` | `none` |

`ESC` 뒤가 `[` 도 `O` 도 아니면 **ESC 키 + 길이 1** 로 답한다(Alt-키 조합을 ESC+글자로 보내는
터미널이 있는데, 그 경우 ESC 를 하나 먹고 다음 호출이 글자를 읽는다).

### `key_of` · `len_of` · `is_char`

```lowent
fn key_of  output u64  . input packed u64 .
fn len_of  output u64  . input packed u64 .
fn is_char output bool . input keycode u64 .
```

`is_char` 에는 **키코드**를 넣는다(포장 값이 아니다). `is_char (key_of p)` 순서다.

### 내장 op — `cap tty` 가 필요하다

```lowent
tty_raw  <on bool>          → bool          rem true = raw 진입, false = 복귀
tty_read <dst mut slice u8> → option u64    rem 읽은 바이트 수(0 = 지금은 없음)
tty_size                    → option u64    rem 행 << 32 | 열
```

`tty_read` 의 `some 0` 은 **에러가 아니다** — "지금은 온 게 없다" 다. 바쁜 대기를 돌지 말고
블로킹 읽기를 원한다면 raw 모드 설정에서 그렇게 잡힌다(현재 구현은 VMIN=1 이라 최소 1바이트를
기다린다).

`tty_size` 의 반환에서 행은 `div v 4294967296`, 열은 `mod v 4294967296` 이다.

## 사용법과 예제

### ① 최소 키 루프

```lowent
module keydemo .

use tty .

proc main output u8 . input t cap tty . input al cap allocator . effects alloc . do
  rem ★ 할당도 실패할 수 있다 — `alloc_bytes` 의 답은 **option** 이다. raw 로 들어가기 **전에** 받는다
  rem   (들어간 뒤에 실패해 돌아가면 단말이 raw 로 남는다).
  let g option mut slice u8 . . be alloc_bytes al capacity 32 .
  guard is_some g . else return 1 .
  let buf mut slice u8 . be some_value g .
  guard tty_raw t true . else return 1 .      rem ★ 리프는 **cap 을 인자로** 받는다
  var going bool be true .
  var last u64 be 0 .
  while going . do
    let n option u64 . be tty_read t buf .
    guard is_some n . else do
      set going false .
      continue .
    end
    var off u64 be 0 .
    while lt off (some_value n) . do
      let p option u64 . be tty.parse_key (subslice buf 0 (some_value n)) off .
      guard is_some p . else do
        rem 모르는 시퀀스 · 모자란 바이트 — 이번 덩어리는 여기서 끊는다.
        set off (some_value n) .
        continue .
      end
      let code u64 be tty.key_of (some_value p) .
      set last code .
      rem 'q' 로 끝낸다.
      if eq code 113 . do set going false . end
      set off (add off (tty.len_of (some_value p))) .
    end
  end
  let r bool be tty_raw t false .
  return narrow u8 last .
end
```

☞ **터미널 리프는 `cap tty` 를 인자로 받는다** — `tty_raw t true`, `tty_read t buf`.
권한을 op 이 가졌다는 것만으로는 안 되고 **그 자리에 건네야** 한다(`E-CAP-MISSING` 이 그렇게 말한다).
*권리는 가진다고 행사되는 것이 아니라 쓰는 자리에서 보여야 한다.*

**`tty_raw false` 를 반드시 부른다.** 이것을 빠뜨리면 프로그램이 끝난 뒤 사용자의 셸이 망가진
채 남는다. 지금은 언어에 `defer` 가 없으므로 **모든 나가는 길에서 부른다**(위 예에서 루프는
언제나 아래로 빠져나오게 짜여 있다 — 그것이 이 형태로 쓴 이유다).

### ② 파싱만 테스트하기 (터미널 없이)

```lowent
proc arrow_up output u64 . effects none . do
  var buf mut slice u8 . be alloc_bytes stack capacity 8 .
  set (index buf 0) 27 .
  set (index buf 1) 91 .
  set (index buf 2) 65 .
  let p option u64 . be tty.parse_key (subslice buf 0 3) 0 .
  guard is_some p . else return 1 .
  guard eq (tty.key_of (some_value p)) 1001 . else return 2 .
  guard eq (tty.len_of (some_value p)) 3 . else return 3 .
  return 42 .
end
```

`effects none` 이라 오라클(`--run` VM ≡ 네이티브)이 그대로 돌린다. 실제로 골든 게이트가 이렇게
검증한다 — **터미널 없이 키 파싱 전부가 검증된다**는 것이 반 가르기의 배당금이다.

### ③ 화면 크기에 맞춰 그리기

```lowent
let sz option u64 . be tty_size t .          rem t = 이 op 이 받은 cap tty
guard is_some sz . else return 1 .
let rows u64 be div (some_value sz) 4294967296 .
let cols u64 be mod (some_value sz) 4294967296 .
rem 이제 term.fit_width 로 각 행을 cols 칸에 맞춘다.
```

## 반례 — 이렇게 쓰면 안 된다

### ✘ raw 모드를 복귀시키지 않는다

```lowent
rem ✘ 중간에서 나가면 셸이 망가진 채 남는다
guard tty_raw t true . else return 1 .
guard something . else return 2 .        rem ← 여기서 나가면 복귀가 없다
let r bool be tty_raw t false .
```

나가는 길마다 `tty_raw false` 를 넣거나, 실패할 수 있는 부분을 **raw 진입 전에** 끝낸다.

### ✘ `none` 을 "에러" 로 읽고 죽는다

`parse_key` 의 `none` 은 대개 "바이트가 더 필요하다" 다. 여기서 프로그램을 끝내면 방향키를
누를 때마다 죽는 프로그램이 된다(읽기 경계가 시퀀스 가운데를 자를 수 있다).

### ✘ 먹은 길이를 무시하고 1씩 전진한다

```lowent
rem ✘ ESC [ A 를 ESC / [ / A 세 개의 키로 읽게 된다
set off (add off 1) .
```

반드시 `tty.len_of` 만큼 전진한다.

### ✘ 포장 값을 직접 비교한다

```lowent
rem ✘ 길이가 섞여 있어 안 맞는다
if eq (some_value p) 1001 . do … end
rem ✔
if eq (tty.key_of (some_value p)) 1001 . do … end
```

### ✘ `cap tty` 없이 부른다

```lowent
proc draw output u64 . effects io . do
  let r bool be tty_raw true .   rem ✘ E-CAP-MISSING — 이 리프에는 권한이 없다
```

권한은 **매개변수로 흘러 들어온다**. 만들어 낼 수 없다는 것이 요점이다.

## 주의사항

- **터미널마다 방향키가 다르다.** `ESC [ A` 와 `ESC O A` 둘 다 인식하는 이유다. 그래도 못 잡는
  터미널이 있으면 그 시퀀스는 `none` 으로 나온다 — 조용히 다른 키로 답하지 않는다.
- **UTF-8 입력**: 한글을 입력하면 여러 바이트가 온다. `parse_key` 는 그것을 **바이트 하나씩**
  돌려준다(각각 1–255). 코드포인트로 모으려면 [utf8](utf8.md) 로 조립한다.
- **Ctrl 조합은 제어문자다**: Ctrl-A = 1, Ctrl-C = 3, Ctrl-D = 4. `is_char` 는 이들에 `true` 다
  (1000 미만이므로) — "글자" 는 "특수키가 아니다" 라는 뜻이지 "출력 가능하다" 는 뜻이 아니다.
- **시그널은 연동돼 있지 않다.** raw 모드에서 Ctrl-C 는 시그널이 아니라 바이트 3 으로 온다.
  프로그램이 직접 끝내야 한다.
- **화면 크기 변화(SIGWINCH)를 안 알려 준다.** 필요하면 매 프레임 `tty_size` 를 다시 묻는다.
