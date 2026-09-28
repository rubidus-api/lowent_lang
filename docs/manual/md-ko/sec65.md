# <a id="mod-regex"></a>`regex` — 역추적하지 않는 Pike VM 정규식

소스

`lib/regex.low`

층

L0 — 순수 계산(호출자의 스크래치)

권한

없음

문자열이 **어떤 꼴인지** 검사하고 찾는다. 입력 검증(숫자만 왔는가), 로그에서 패턴 찾기, 간단한 토큰 나누기에 쓴다. 패턴을 한 번 **컴파일**해 프로그램으로 만들어 두고, 같은 프로그램으로 여러 입력을 맞춘다.

```lowent
use regex as rx .

let n be option u64 rx.compile "a{2,4}b" prog st .
guard is_some n . else return 1 .
let m be option u64 rx.match_at prog "aaab" 0 cl nl mk .
```

프로그램(`slice u64`)도, 컴파일러 상태도, 매처의 작업 공간도 모두 호출자의 슬라이스다. 그래서 `effects none` 이고 숨은 할당이 없으며 재진입이 공짜이고 VM· 네이티브 대조 안에서 돈다.

**왜 Pike VM 이 안전한가.** 흔한 정규식 엔진은 **역추적** 방식이다. 갈림길(`|`·`*`)에서 한 길을 끝까지 가 보고 막히면 되돌아와 다른 길을 간다. 갈림길이 겹겹이 겹치면 시도할 길이 2·4·8… 로 불어나고, `(a*)*b` 같은 패턴 하나가 입력 몇십 바이트에 몇 분씩 걸린다 — 이것이 ReDoS(정규식 서비스 거부)다. Pike VM 은 되돌아가지 않는다. 패턴을 NFA 로 보고 “지금 있을 수 있는 자리” 전부를 **스레드 목록**(OS 스레드가 아니라 프로그램의 몇 번째 명령에 서 있는지를 적은 값들)에 담아, 입력 한 바이트마다 목록 전체를 **한꺼번에 한 걸음씩** 전진시킨다. 같은 자리는 한 번만 담으므로 목록은 명령 수를 넘지 않는다. 그래서 어떤 패턴과 입력에도 **지수 폭발이 원리적으로 없다** — 시작 자리를 정한 매칭 한 번은 O(입력 × 프로그램)이다.

## <a id="sx1"></a>설계와 경계

**받는 문법**

- 리터럴, `.`(개행 제외), `* + ?`(탐욕), `[a-z]`·`[^…]`, `|`, `(…)`, `^ $`(문자열 경계), 이스케이프 `\d \w \s \n \t \r` 와 특수 문자의 리터럴화(`\.` 따위).
- 클래스 안에서도 `\n \t \r \\ \] \-` 리터럴과 `\d \w \s` 집합의 합집합, 범위 `a-z`, 부정 `[^…]`.
- `{m}`·`{m,}`·`{m,n}` 반복(탐욕) — **컴파일할 때 펼친다**. 실행 중 카운터를 두면 스레드마다 사적 상태가 생겨 “스레드 = 명령 위치 하나” 라는 불변이 깨지고 중복 제거가 무너진다. 상한은 64 다.
- 캡처 그룹 `match_caps`, UTF-8 한 글자를 먹는 `\X`, 유니코드 속성 `\p{…}`·`\P{…}`(`L N P Z M` 과 `Lu Ll Lt Lm Lo Nd Nl No`).

**속성은 먹은 뒤에 돌아본다.** 속성 집합은 구간이 수백 … 수천이라 바이트 오토마톤으로 펼치면 명령이 수천 개가 된다 — 프로그램 버퍼가 호출자의 것인 이 설계에서는 감당하지 못한다. 대신 코드포인트 하나를 먹은 **뒤에** 그 속성인지 돌아본다. UTF-8 은 자기 동기적이라 뒤로 읽을 수 있고, 그 판정은 바이트를 먹지 않으므로 선형성의 근거가 그대로다. 표는 [`unicode`](sec63.md#mod-unicode) 가 든다. 모르는 속성 이름은 **거절**한다 — 넓은 표로 대신 답하면 조용히 틀린 매치가 된다.

**짓지 않은 것** — 클래스 안의 속성(`[\p{L}0-9]`, 클래스는 바이트 비트맵이다), 대소문자 무시(`(?i)`, 폴딩 표가 필요하다), 역참조와 룩어라운드(이것은 짓지 않을 것에 가깝다 — 그 순간 선형성이 죽는다). 잘못된 패턴과 모자란 버퍼는 모두 **값으로**(`none`) 거절된다.

## <a id="sx2"></a>자료구조

**프로그램 버퍼**(`slice u64`) 하나에 프로그램이 통째로 산다. `prog[0]` 은 명령 수, `prog[1]` 은 클래스 수, 그 뒤로 명령이 하나에 세 워드 `[op a b]` 로 이어진다. **코드는 앞에서 자라고, 문자 클래스 비트맵(256 비트 = 4 워드)은 꼬리에서 거꾸로 자란다** — 둘이 만나면 컴파일이 `none` 이다. 클래스 k 의 비트맵은 `prog[len − 4·(k+1) .. len − 4·k)` 에 있다.

| **코드 · 이름** | **a · b** | **뜻** |
|---|---|---|
| 0 CHAR | 바이트 값 | 그 바이트면 먹고 전진 |
| 1 ANY | — | 개행(10) 아닌 아무 바이트를 먹는다 |
| 2 CLASS | 클래스 번호 k | 비트맵 k 에 있는 바이트를 먹는다 |
| 3 SPLIT | 갈래 1 · 갈래 2 | 두 갈래를 동시에 진행(갈래 1 우선 = 탐욕) |
| 4 JMP | 목표 | 무조건 이동 |
| 5 MATCH | — | 여기 닿으면 매치 |
| 6 BOL · 7 EOL | — | `^` 은 위치 0 에서만, `$` 는 `len s` 에서만 통과 |
| 8 SAVE | 슬롯 번호 | 캡처 자리 기록(바이트를 먹지 않는다) |
| 9 CPPROP | 속성 코드 | **직전에 먹은** 코드포인트가 그 속성인가(바이트를 먹지 않는다) |

*표 50.1 — 명령 코드*

속성 코드는 0=L · 1=N · 2=P · 3=Z · 4=M · 5=Lu · 6=Ll · 7=Nd · 8=Lt · 9=Lm · 10=Lo · 11=Nl · 12=No 이고, **부정은 +16** 이다(`\P{L}` = 16). 부정 자리를 넉넉히 잡아 둔 덕에 표가 8 개에서 13 개로 늘 때 인코딩이 한 비트도 바뀌지 않았다.

**컴파일러 상태** `st` 는 네 칸(명령 수 · 클래스 수 · 패턴 위치 · 실패 표시)이고 `compile` 이 초기화하므로 길이 4 이상만 보장하면 된다. **매처 스크래치**는 현재·다음 스레드 목록 `clist`·`nlist` 와 중복 방지 도장 `marks`, 그리고 `search` 가 더 받는 시작 위치 배열 `cstart`·`nstart` 다. 모두 `slice u64` 이고 각각 **길이가 명령 수 이상**이어야 한다.

## <a id="sx3"></a>op 한눈에

| **op** | **시그니처** | **안 될 때** |
|---|---|---|
| `compile` | `proc (pat slice u8, prog mut slice u64, st mut slice u64) → option u64` | 문법 오류나 버퍼 부족이면 `none` |
| `match_at` | `proc (prog slice u64, s slice u8, at u64, clist, nlist, marks) → option u64` | 매치 없음 · 빈 프로그램 · 스크래치 부족이면 `none` |
| `find` | `proc (prog slice u64, s slice u8, clist, nlist, marks) → option u64` | 어디에도 매치가 없으면 `none` |
| `search` | `proc (prog slice u64, s slice u8, clist, nlist, marks, cstart, nstart) → option u64` | 어디에도 매치 없음 · 빈 프로그램 · 스크래치 부족이면 `none` |
| `test_at` | `proc (prog slice u64, s slice u8, clist, nlist, marks) → bool` | 실패 없음(거짓이 곧 답) |
| `class_has` | `fn (prog slice u64, k u64, b u64) → bool` | 실패 없음 |

*표 50.2 — `regex` 의 op — 모두 `effects none`*

`rx_` 로 시작하는 이름들은 컴파일러와 매처의 내부이고 밖에서 보이지 않는다. 스크래치 매개변수는 모두 `mut slice u64` 다.

## <a id="sx4"></a>op 상세

- **`compile pat prog st`** — `len prog ≥ 8`·`len st ≥ 4` 여야 한다. 성공하면 `some <명령 수>` 이고, 이 값이 곧 매처 스크래치의 최소 길이다. 닫히지 않은 그룹 `(ab`, 원자 자리에 온 `) | * + ?`, 닫히지 않은 `[…]`, 뒤집힌 범위 `[z-a]`, 패턴 끝의 홀로 선 `\`, 잘못된 셈(`a{4,2}`·`a{}`·`a{2`·64 초과), 코드와 비트맵이 만남(버퍼 부족)이면 `none`. 빈 패턴은 유효하다 — `MATCH` 하나로 컴파일되어 어디서나 빈 매치다.
- **`match_at prog s at clist nlist marks`** — `at` 에서 시작하는 매치의 **가장 긴 끝**(탐욕)을 `some end`(끝은 배타적)로 준다. 시작점이 물음의 일부라 `at` 을 받는다. `clist` 는 지금 위치의 스레드 목록, `nlist` 는 다음 위치의 목록이고(한 바이트마다 둘을 맞바꾼다), `marks` 는 “이 명령은 이번 위치에 이미 담았다” 는 도장이다. 도장이 없으면 같은 자리가 여러 번 들어가 목록 길이의 상한이 깨진다. 빈 매치도 매치다(`some at`). 비용은 O(`len s` × 명령 수).
- **`find`** — 매치가 **시작되는 가장 왼쪽 위치**. `at = 0, 1, …, len s` 로 `match_at` 을 다시 시작하는 반복이라 상한은 O(n²·m) 이지만, 시도마다 선형이라 지수 폭발은 없다. 끝 위치가 필요하면 돌려받은 시작에서 `match_at` 을 한 번 더 부른다.
- **`search`** — `find` 와 같은 물음을 **한 번 훑어** 답한다. 재시작 반복을 없앤 대신 “이 스레드가 어디서 출발했는가” 를 스레드마다 들고 다녀야 해서 `cstart`·`nstart` 두 스크래치가 더 든다. 아직 매치가 없는 동안에만 매 위치에서 시작 스레드를 새로 넣으므로 비용은 O(입력 × 명령 수)이고, 더 왼쪽 시작이 늘 이기므로 왼쪽 우선이 지켜진다. `find` 와 **같은 답**이다 — 개발 저장소의 시험이 둘을 맞대 본다.
- **`test_at`** — 어딘가에 매치가 있는가. 위치 없이 예·아니오만 필요할 때 쓴다.
- **`class_has prog k b`** — 컴파일된 프로그램의 클래스 `k` 가 바이트 `b` 를 담는가. 비트맵이 프로그램 버퍼의 꼬리에 살아서 `prog` 를 받는다. `k` 의 범위는 검사하지 않는다.

## <a id="sx5"></a>쓰는 법

**이스케이프는 두 겹이다.** Lowent 문자열 리터럴의 이스케이프는 열넷으로 닫혀 있고(3장) `\d` · `\w` 같은 것은 그 안에 없다(그 밖은 `E-STR-ESCAPE`). 정규식의 `\d` 를 소스에 쓰려면 **`"\\d"`** 로 쓴다 — 리터럴이 `\\` 를 백슬래시 하나로 만들고, 그 `\d` 두 바이트를 정규식 컴파일러가 숫자 클래스로 읽는다.

```lowent
module ex_regex .

use regex as rx .

proc demo input prog mut slice u64 . . input st mut slice u64 . .
  input cl mut slice u64 . . input nl mut slice u64 . . input mk mut slice u64 . .
  output u64 . effects none .
do
  guard ge (len prog) 32 . else return 90 .

  rem "ab.d*" 를 "abcdddx" 에: . 이 c 를, 탐욕 d* 가 ddd 를 먹어 끝 = 6
  let n be option u64 rx.compile "ab.d*" prog st .
  guard is_some n . else return 1 .
  let m be option u64 rx.match_at prog "abcdddx" 0 cl nl mk .
  guard is_some m . else return 2 .
  guard eq (some_value m) 6 . else return 3 .

  rem 정규식으로는 [a-c]+z\d --- 소스에는 "\\d"
  let n2 be option u64 rx.compile "[a-c]+z\\d" prog st .
  guard is_some n2 . else return 4 .
  let m2 be option u64 rx.match_at prog "abz7" 0 cl nl mk .
  guard is_some m2 . else return 5 .
  guard eq (some_value m2) 4 . else return 6 .

  rem "ab$" 는 끝에서만 --- find 가 가장 왼쪽 시작을 준다
  let n3 be option u64 rx.compile "ab$" prog st .
  guard is_some n3 . else return 7 .
  let f be option u64 rx.find prog "xxab" cl nl mk .
  guard is_some f . else return 8 .
  guard eq (some_value f) 2 . else return 9 .
  let g be option u64 rx.find prog "abx" cl nl mk .
  guard eq (is_some g) false . else return 10 .
  return 42 .
end
```

`search` 와 셈 반복도 같은 모양이다.

```lowent
proc demo_search input prog mut slice u64 . . input st mut slice u64 . .
  input cl mut slice u64 . . input nl mut slice u64 . . input mk mut slice u64 . .
  input cs mut slice u64 . . input ns mut slice u64 . .
  output u64 . effects none .
do
  guard ge (len prog) 32 . else return 90 .
  let n be option u64 rx.compile "b+c" prog st .
  guard is_some n . else return 1 .
  let a be option u64 rx.find prog "xxbbbc" cl nl mk .
  guard is_some a . else return 2 .
  let b be option u64 rx.search prog "xxbbbc" cl nl mk cs ns .
  guard is_some b . else return 3 .
  guard eq (some_value a) (some_value b) . else return 4 .
  guard eq (some_value b) 2 . else return 5 .

  let n3 be option u64 rx.compile "a{2,4}b" prog st .
  guard is_some n3 . else return 9 .
  let m3 be option u64 rx.match_at prog "aaab" 0 cl nl mk .
  guard is_some m3 . else return 10 .
  guard eq (some_value m3) 4 . else return 11 .
  rem 하한 미달은 매치가 아니다
  let f be option u64 rx.match_at prog "ab" 0 cl nl mk .
  guard eq (is_some f) false . else return 12 .
  return 42 .
end
```

호출자는 예컨대 `prog` 32 칸, `st` 4 칸, `cl`·`nl`·`mk`(`search` 를 쓰면 `cs`·`ns` 까지) 각 32 칸을 마련한다 — `compile` 이 돌려준 명령 수보다 크면 된다. `{m,n}` 은 펼쳐지므로 명령 수가 반복 횟수만큼 는다.

역추적 엔진이라면 `(a*)*b` 에 `a` 64 개를 주면 2^64 갈래를 헤매지만, Pike VM 은 스레드 목록이라 선형으로 끝난다.

```lowent
let n be option u64 rx.compile "(a*)*b" prog st .
guard is_some n . else return 1 .
let f be option u64 rx.match_at prog
  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" 0 cl nl mk .
guard eq (is_some f) false . else return 2 .
```

## <a id="sx6"></a>반례

| **부름** | **결과** |
|---|---|
| `rx.compile "(ab" prog st` | `none` — 닫히지 않은 그룹 |
| `rx.compile "*a" prog st` | `none` — 앞이 없는 반복 |
| `rx.compile "abcdefghij" tiny st`(`tiny` 8 칸) | `none` — 코드와 비트맵이 만난다 |
| `rx.match_at prog s 0 short short short` | `none` — 스크래치가 명령 수보다 짧다 |
| `rx.compile "a{4,2}" prog st` · `"a{}"` · `"a{2"` · `"a{65}"` | `none` — 잘못된 셈(상한 64 는 펼치는 방식이라 폭발을 막는다) |
| `rx.compile "\d" prog st` | 실행 전에 `E-STR-ESCAPE` — `"\\d"` 로 쓴다 |

*표 50.3 — 값으로 거절되는 것과 번역에서 거절되는 것*

## <a id="sx7"></a>주의

- **`prog` 는 컴파일할 때와 같은 길이로 매칭에 넘긴다.** 클래스 비트맵이 버퍼의 꼬리에 살고 그 주소가 `len prog` 기준이다. `subslice` 로 잘라 넘기면 명령은 살아도 비트맵 주소가 어긋나 `[…]`·`\d\w\s` 가 조용히 엉뚱한 바이트를 본다. 프로그램은 자르지 말고 통째로 든다.
- **프로그램 버퍼 하나에 패턴 하나.** 다시 `compile` 하면 앞 프로그램은 덮인다. 두 패턴을 번갈아 쓰려면 버퍼를 둘 마련한다.
- **`find` 냐 `search` 냐는 비용의 선택이다.** 답은 같다. 긴 입력과 잦은 탐색이면 `search`, 작은 입력에서 스크래치를 아끼려면 `find` 다.
- **`marks` 는 매번 스스로 초기화된다.** 호출 사이에 보존할 것이 없다.
- **빈 매치에 주의한다.** `a*` 같은 패턴은 어디서나 길이 0 으로 성공하고, `find` 는 늘 `some 0` 을 낸다. “한 글자라도 먹었는가” 가 필요하면 끝을 시작과 비교한다.
- **매칭 단위는 바이트다.** UTF-8 입력에서 `.`·`[^…]` 는 코드포인트가 아니라 바이트 하나를 먹는다. 코드포인트 하나는 `\X`, 유니코드 범주는 `\p{…}` 로 묻는다.

---

[← 이전](sec64.md) · [목차로](README.md) · [다음 →](sec66.md)
