# regex — Pike VM 정규식

소스: `lib/regex.low` · 검증: `impl/tests/vm_regex.low`

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 문자열이 **어떤 꼴인지** 검사하고 찾는 정규식 엔진이다.

**언제 쓰나.** 입력 검증(숫자만 왔는가), 로그에서 패턴 찾기, 간단한 토큰 나누기에 쓴다.

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use regex as rx .

rem ① 패턴을 한 번 컴파일해 prog 에 담는다(st 는 컴파일용 스크래치 4칸).
let n option u64 . be rx.compile "a{2,4}b" prog st .
guard is_some n . else return 1 .          rem 패턴이 틀렸으면 none 이다
rem ② 그 prog 로 몇 번이든 맞춰 본다. 0 번째부터 맞춰 끝 위치를 받는다.
let m option u64 . be rx.match_at prog "aaab" 0 cl nl mk .
```

**읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## 왜 있는가

이 모듈은 "문자열이 이 꼴에 맞는가"를 물을 때 쓴다. **정규식(regular expression)**은 그
꼴을 적는 작은 언어다 — `[a-c]+z\d` 는 "a~c 가 한 번 이상, 그 다음 z, 그 다음 숫자 하나"
라는 뜻이다. 패턴을 한 번 **컴파일**해 프로그램으로 만들어 두고, 같은 프로그램으로 여러
입력을 매칭한다.

정규식 매칭은 바이트 슬라이스에서 바이트 슬라이스로 가는 **순수 계산**이다(RFC-0075 §5 ·
3차 슬라이스 2). capability 도, 언어에 없는 표현도 필요 없다 — 프로그램은 `slice u64`,
스크래치도 전부 호출자의 슬라이스다. 그래서 빌트인이 아니라 라이브러리다: `effects none` ·
숨은 할당 0 · **모든 스크래치는 호출자의 것**(재진입·오라클 안).

**왜 Pike VM 이 안전한가 — 초보자 눈높이.** 흔한 정규식 엔진은 **역추적**(backtracking)
방식이다: 갈림길(`|`·`*`)에서 한 길을 끝까지 가 보고, 막히면 되돌아와 다른 길을 간다.
갈림길이 겹겹이 겹치면 시도할 길의 수가 2·4·8… 로 불어나고, `(a*)*b` 같은 패턴 하나가
입력 몇십 바이트에 몇 분씩 걸린다 — 이것이 **ReDoS**(정규식 서비스 거부: 패턴+입력 한 쌍으로
프로그램을 세워 버리는 공격)다. 이 라이브러리의 **Pike VM** 은 되돌아가지 않는다. 패턴을
**NFA**(비결정적 유한 오토마톤 — "지금 있을 수 있는 자리"가 동시에 여럿인 상태 기계)로
보고, 그 가능한 자리 전부를 **스레드 목록**(OS 스레드가 아니다 — "프로그램의 몇 번째
명령에 서 있는가"를 적은 pc 값들의 배열)에 담아, 입력 한 바이트마다 목록 전체를 **한꺼번에
한 걸음씩** 전진시킨다. 같은 자리는 한 번만 담으므로 목록은 명령 수를 넘지 않는다 —
그래서 어떤 패턴·입력 조합에도 **지수 폭발이 원리적으로 없다.** anchored 매칭 한 번 =
O(입력 × 프로그램). 이 범위는 prov rfc-0009 를 그대로 받은 것이다.

## 설계 의도와 경계

**하는 것(이 슬라이스의 표면)**:

- 리터럴 · `.`(개행 제외) · `* + ?`(탐욕) · `[a-z]`/`[^…]` · `|` · `(…)` ·
  `^ $`(문자열 경계) · 이스케이프 `\d \w \s \n \t \r` + 특수문자 리터럴화(`\.` 등)
- 클래스 안에서도 `\n \t \r \\ \] \-` 리터럴과 `\d \w \s` 집합 합집합, 범위 `a-z`, 부정 `[^…]`
- **`{m}` `{m,}` `{m,n}` 반복**(탐욕) — **컴파일 시 펼친다**(새 옵코드 0). 런타임 카운터를
  두면 스레드마다 사적 상태가 생겨 Pike VM 의 "스레드=pc 하나" 불변식이 깨지고 `marks`
  중복 제거가 무너진다 — 그래서 안 둔다. 상한 64. 잘못된 셈(상한<하한 · 숫자 없음 ·
  안 닫힘)은 컴파일이 `none`.
- **단일 패스 무anchored 탐색** `search` — 입력을 **한 번만** 훑는다: O(입력 × 명령 수).
  `find`(재시작 루프, 상한 O(n²·m))는 스크래치가 둘 덜 필요해 작은 입력용으로 남아
  있고, 둘은 **같은 답**을 낸다(골든이 맞대 본다).

- **캡처 그룹** `match_caps` — `(…)` 가 어디에 매치됐는지 준다(SAVE, 옵코드 8).
- **유니코드 코드포인트** `\X` — UTF-8 한 글자를 바이트 오토마톤으로 먹는다.
- **유니코드 속성** `\p{…}` / `\P{…}` — `L`·`N`·`P`·`Z`·`M` 과 세부 범주
  `Lu`·`Ll`·`Lt`·`Lm`·`Lo` · `Nd`·`Nl`·`No`(L 과 N 의 하위가 전부 있다).
  ★ 속성 집합은 구간이 수백~수천이라 **바이트 오토마톤으로 펼치면 명령이 수천 개**가 된다
  (RE2 의 길 — 프로그램 버퍼가 **호출자 것**인 이 설계에선 감당이 안 된다). 대신 코드포인트
  하나를 먹은 **뒤에 돌아본다**(CPPROP, 옵코드 9): UTF-8 은 자기동기적이라 뒤로 읽을 수 있고,
  그 판정은 **바이트를 안 먹으므로** 스레드 보조 — 즉 선형성의 근거 — 가 그대로다.
  표는 [unicode](unicode.md) 가 들고 있다.
  ☞ 모르는 이름(`\p{Lt}`·`\p{So}`…)은 **거절**한다. 넓은 표로 대신 답하면 조용히 틀린
    매치가 되고, 그건 컴파일 에러보다 훨씬 나쁘다.

**정직하게 안 지은 것**(각각 별도 슬라이스다):

- **클래스 안의 속성**(`[\p{L}0-9]`) — 클래스는 바이트 비트맵이라 코드포인트가 안 들어간다.
- **대소문자 무시 매칭**(`(?i)`) — 케이스 폴딩 표가 필요하다.
- **역참조 · 룩어라운드** — 이건 슬라이스가 아니라 영구 거절에 가깝다. 그 순간 선형이 죽는다.

잘못된 패턴·모자란 버퍼는 전부 **값으로**(`none`) 거절된다. 트랩 없음.

## 자료구조

먼저 낱말 하나: **스크래치(scratch) 버퍼**란 op 이 일하는 동안만 쓰는 작업 공간이다.
이 라이브러리는 아무것도 할당하지 않으므로, 그 작업 공간마저 호출자가 슬라이스로 빌려준다 —
그 대가로 재진입이 공짜고, 오라클(VM≡native 차등 검증)이 그대로 돌릴 수 있다.

### 프로그램 버퍼 (`slice u64`)

`compile` 이 만드는 프로그램은 호출자가 준 `slice u64` 하나에 통째로 산다:

```
prog[0]              = 명령 수 (ni)
prog[1]              = 클래스 수 (nk)
prog[2 .. 2+3·ni)    = 명령들 — 명령 하나 = 3워드 [op a b]
      …가운데는 빈 땅…
prog[len−4·(k+1) .. len−4·k)  = 클래스 k 의 비트맵 (256비트 = 4워드, 꼬리에서 역방향)
```

**코드는 앞에서 자라고, 문자클래스 비트맵은 뒤에서 거꾸로 자란다** — 둘이 만나면 컴파일이
`none` 을 낸다(버퍼가 작다고 말하는 값). 클래스 k 가 바이트 b 를 담는지는 비트맵의
`(b/64)` 워드, `(b%64)` 비트다 — `class_has` 가 이 계산이다.

### 명령 op 코드표

| 코드 | 이름 | a | b | 뜻 |
|---|---|---|---|---|
| 0 | CHAR | 바이트 값 | — | 그 바이트면 소비하고 전진 |
| 1 | ANY | — | — | 개행(10) 아닌 아무 바이트 소비 |
| 2 | CLASS | 클래스 번호 k | — | 비트맵 k 에 있는 바이트 소비 |
| 3 | SPLIT | 갈래1 pc | 갈래2 pc | 두 갈래 동시 진행(갈래1 우선 = 탐욕) |
| 4 | JMP | 목표 pc | — | 무조건 이동 |
| 5 | MATCH | — | — | 여기 도달 = 매치 |
| 6 | BOL | — | — | `^` — 위치 0 에서만 통과 |
| 7 | EOL | — | — | `$` — 위치 `len s` 에서만 통과 |
| 8 | SAVE | 슬롯 번호 | — | 캡처 자리 기록(엡실론 — 바이트를 안 먹는다) |
| 9 | CPPROP | 속성 코드 | — | **직전에 먹은** 코드포인트가 그 속성인가(엡실론) |

속성 코드: 0=L · 1=N · 2=P · 3=Z · 4=M · 5=Lu · 6=Ll · 7=Nd · 8=Lt · 9=Lm · 10=Lo ·
11=Nl · 12=No, **부정은 +16**
(`\P{L}` = 16). 부정 오프셋을 16 으로 넉넉히 잡아 둔 덕에, 표가 8 개에서 13 개로 늘 때 **인코딩이 한 비트도
안 바뀌었다** — 빠듯하게 잡았다면 표를 더할 때마다 바뀌었을 것이다.

### 컴파일러 상태 `st` (`slice u64`, 4칸)

`[0]`=명령 수 · `[1]`=클래스 수 · `[2]`=패턴 위치 · `[3]`=실패 플래그. 컴파일러가 자기
진행 상황을 적어 두는 스크래치다. `compile` 이 직접 초기화하므로 호출자는 **길이 ≥ 4**
만 보장하면 된다.

### 매처 스크래치

`clist`/`nlist`(현재/다음 스레드 목록)와 `marks`(중복 방지 도장) — 셋 다 `slice u64`,
각각 **길이 ≥ 명령 수**. 같은 pc 는 한 번만 목록에 들어가므로 목록은 명령 수를 넘지 않는다.
`search` 는 여기에 `cstart`/`nstart` 둘을 더 받는다 — 스레드 목록과 **나란한** 시작
위치 배열(역시 각각 길이 ≥ 명령 수)로, 스레드마다 자기가 어디서 시작했는지를 들고 다닌다.
다섯이 왜 다 필요한지는 아래 op 상세에서 하나씩 말한다.

## op 한눈에

내보내는(export) op 만 적는다. `rx_emit`·`rx_shift`·`rx_class_new`·`rx_class_set`·
`rx_class_preset`·`rx_atom_escape`·`rx_atom_class`·`rx_atom`·`rx_post`·`rx_repeat`·
`rx_copy`·`rx_cat`·`rx_alt` 는 컴파일러 내부, `rx_add`·`rx_add_s` 는 매처 내부다 —
내부 이름에만 `rx_` 접두사가 남아 있고, export 가 아니라서 밖에서는 보이지 않는다.
(export op 이름에 모듈 접두사를 안 붙이는 규칙은 RFC-0075 D7b — README 의 이름 규칙 참조.
호출은 `regex.compile`, 별칭을 주면 `rx.compile` 이다.)

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `compile` | proc | `pat slice u8, prog mut slice u64, st mut slice u64 → option u64` | 문법 오류·버퍼 부족 = `none` |
| `match_at` | proc | `prog slice u64, s slice u8, at u64, clist/nlist/marks mut slice u64 → option u64` | 매치 없음·빈 프로그램·스크래치 부족 = `none` |
| `find` | proc | `prog slice u64, s slice u8, clist/nlist/marks mut slice u64 → option u64` | 어디에도 매치 없음 = `none` |
| `search` | proc | `prog slice u64, s slice u8, clist/nlist/marks/cstart/nstart mut slice u64 → option u64` | 어디에도 매치 없음·빈 프로그램·스크래치 부족 = `none` |
| `test_at` | proc | `prog slice u64, s slice u8, clist/nlist/marks mut slice u64 → bool` | 실패 없음(거짓이 곧 답) |
| `class_has` | fn | `prog slice u64, k u64, b u64 → bool` | 실패 없음 |

## op 상세

- `compile pat prog st` — 패턴을 컴파일한다. `pat` 은 패턴 바이트, `prog` 는 프로그램이
  담길 호출자 버퍼(만들어진 프로그램의 집이다), `st` 는 컴파일러 상태 4칸(컴파일러의
  진행 상황 스크래치 — 이것까지 매개변수인 이유는 이 라이브러리가 아무것도 할당하지
  않아서다).
  - 조건: `len prog ≥ 8` · `len st ≥ 4`. 모자라면 즉시 `none`.
  - 성공 = `some <명령 수>`. 이 값이 곧 매처 스크래치(`clist`/`nlist`/`marks`)의 최소
    길이다.
  - `none` 이 되는 경우: 닫히지 않은 그룹 `(ab` · 원자 자리에 온 `) | * + ?` · 닫히지
    않은 `[…]` · 뒤집힌 범위 `[z-a]` · 패턴 끝의 홀로 선 `\` · 잘못된 `{m,n}` 셈
    (상한<하한 `a{4,2}` · 숫자 없음 `a{}` · 안 닫힘 `a{2` · m 이나 n 이 64 초과) ·
    **코드와 비트맵이 만남**(프로그램 버퍼 부족).
  - 빈 패턴은 유효하다: MATCH 하나로 컴파일되어 어디서나 빈 매치가 된다.
  - `prog` 는 매칭 때 **그대로(같은 길이로)** 넘겨야 한다 — 이유는 주의사항 참조.
- `match_at prog s at clist nlist marks` — `at` 에서 시작하는(anchored) 매치의
  **가장 긴 끝**을 찾는다(탐욕). 반환은 끝 위치(배타적) `some end`. 없으면 `none`.
  - `at` 이 매개변수인 이유: anchored 매칭은 "정확히 여기서 시작하는가"를 묻는 물음이라
    시작점이 물음의 일부다.
  - 스크래치 셋이 왜 필요한가 — `clist` 는 지금 위치의 스레드 목록, `nlist` 는 다음
    위치의 목록(한 바이트 전진할 때마다 둘을 맞바꾼다 — 두 개가 필요한 이유다),
    `marks` 는 "이 pc 는 이번 위치에 이미 담았다"는 도장이다. 도장이 없으면 같은 pc 가
    목록에 여러 번 들어가 목록 길이의 상한(=명령 수)이 깨진다. 도장을 찍는 일은
    **엡실론 폐포**(ε-closure: SPLIT·JMP·`^`·`$` 처럼 입력을 소비하지 않는 명령을 따라
    갈 수 있는 자리 전부를 미리 펼쳐 두는 일) 안에서 일어난다.
  - 조건: 명령 수 > 0 · `len clist`/`len nlist`/`len marks` ≥ 명령 수. 어기면 `none`.
  - 빈 매치도 매치다: 패턴이 빈 문자열에 맞으면 `some at` 이 나온다.
  - 비용: **선형** — O(`len s` × 명령 수). 입력·패턴이 무엇이든.
- `find prog s clist nlist marks` — 매치가 **시작되는 가장 왼쪽 위치**(왼쪽 최선).
  `at = 0, 1, …, len s` 로 `match_at` 을 재시작하는 루프다 — 상한 O(n²·m)이지만
  **지수 폭발은 없다**(각 시도가 선형이라서). 끝 위치가 필요하면 반환된 시작에서
  `match_at` 을 한 번 더 부른다. 스크래치 셋은 `match_at` 에 그대로 넘기는 것들이다.
- `search prog s clist nlist marks cstart nstart` — `find` 와 같은 물음(매치가
  시작되는 가장 왼쪽 위치)을 **단일 패스**로 답한다.
  - 시그니처: `output option u64 . input prog slice u64 . input s slice u8 . input clist mut slice u64 . .
    input nlist mut slice u64 . . input marks mut slice u64 . . input cstart mut slice u64 . .
    input nstart mut slice u64 . . effects none`
  - 스크래치가 **다섯**인 이유: `clist`/`nlist`/`marks` 는 `match_at` 과 같은 세 가지
    구실이고, `cstart`/`nstart` 는 스레드 목록과 **나란한**(같은 인덱스가 같은 스레드)
    시작 위치 배열이다 — 재시작 루프를 없앤 대신 "이 스레드가 어디서 출발했는가"를
    스레드마다 들고 다녀야 답(시작 위치)을 낼 수 있다. 현재/다음 두 벌이 필요한 것도
    `clist`/`nlist` 와 같은 이유다.
  - 아직 매치가 없는 동안에만 매 위치에서 pc=0 스레드를 새로 넣는다 — 입력을 **한 번만**
    훑는다: O(입력 × 명령 수). 왼쪽 우선은 자연히 지켜진다(더 왼쪽 start 가 언제나 이긴다).
  - 조건: 명령 수 > 0 · 다섯 스크래치가 각각 길이 ≥ 명령 수. 어기면 `none`.
  - 반환 = `some <시작 위치>`. 어디에도 없으면 `none`. `find` 와 **같은 답**이다 —
    골든이 둘을 맞대 본다(같은 답, 다른 비용).
- `test_at prog s clist nlist marks` — 어딘가에 매치가 있는가. `find` 가 `some` 인가와
  같다(내부에서 `find` 를 부른다). 위치가 필요 없고 예/아니오만 필요할 때 쓴다.
- `class_has prog k b` — 컴파일된 프로그램의 클래스 `k` 가 바이트 `b` 를 담는가.
  매처가 내부에서 쓰는 계산이지만, 비트맵을 직접 묻고 싶을 때 쓸 수 있다. `prog` 를
  받는 이유는 비트맵이 프로그램 버퍼의 꼬리에 살기 때문이다. `k` 가 클래스 수 미만이어야
  의미가 있다(범위 검사는 하지 않는다).

## 사용법과 예제

별칭 관례는 `use regex as rx .` 다. 스크래치는 전부 파라미터로 받는다 — 버퍼를 마련하는
정책은 호출자의 것이다.

**주의 — 이스케이프는 두 겹이다.** Lowent 문자열 리터럴의 이스케이프 집합은 **닫혀
있다**(`\\ \" \n \t \r \0 \xNN` 일곱뿐 — 그 밖은 `E-STR-ESCAPE` 오류). 그래서 정규식의 `\d` 를
소스에 쓰려면 **`"\\d"`** 로 써야 한다: 문자열 리터럴이 `\\` 를 백슬래시 한 개로 만들고,
그 `\d` 두 바이트를 정규식 컴파일러가 숫자 클래스로 읽는다. `"\d"` 라고 쓰면 정규식에
도달하기 전에 컴파일 오류다.

```lowent
module ex_regex .

use regex as rx .

rem 컴파일 한 번, 매칭 여러 번. 성공 = 42.
proc demo output u64 . input prog mut slice u64 . .
  input st mut slice u64 . . input cl mut slice u64 . . input nl mut slice u64 . .
  input mk mut slice u64 . . effects none .
do
  guard ge (len prog) 32 . else return 90 .

  rem ① 리터럴·`.`·탐욕 `*`: "ab.d*" 를 "abcdddx" 에 anchored.
  rem    `.` 이 'c' 를 먹고, 탐욕 `d*` 가 ddd 를 전부 먹는다 → 끝 = 6 (x 앞).
  let n option u64 . be rx.compile "ab.d*" prog st .
  guard is_some n . else return 1 .
  let m option u64 . be rx.match_at prog "abcdddx" 0 cl nl mk .
  guard is_some m . else return 2 .
  guard eq (some_value m) 6 . else return 3 .

  rem ② 클래스·이스케이프: 정규식으로는 [a-c]+z\d 다.
  rem    소스에는 "\\d" — 문자열 리터럴이 \\ 를 \ 하나로 만들어 정규식에 \d 가 닿는다.
  let n2 option u64 . be rx.compile "[a-c]+z\\d" prog st .
  guard is_some n2 . else return 4 .
  let m2 option u64 . be rx.match_at prog "abz7" 0 cl nl mk .
  guard is_some m2 . else return 5 .
  guard eq (some_value m2) 4 . else return 6 .

  rem ③ 닻·탐색: "ab$" 는 끝에서만 — find 가 가장 왼쪽 시작을 준다.
  let n3 option u64 . be rx.compile "ab$" prog st .
  guard is_some n3 . else return 7 .
  let f option u64 . be rx.find prog "xxab" cl nl mk .
  guard is_some f . else return 8 .
  guard eq (some_value f) 2 . else return 9 .
  rem "abx" 에는 없다 — none 도 값이다. 트랩이 아니라 guard 로 받는다.
  let g option u64 . be rx.find prog "abx" cl nl mk .
  guard eq (is_some g) false . else return 10 .
  return 42 .
end
```

단일 패스 탐색과 `{m,n}` 반복 — `impl/tests/vm_regex.low` 의 `search_pass`·`counted` op
이 실제로 이렇게 검증한다:

```lowent
rem search: find 와 같은 답 — 시작 위치를 들고 다닐 스크래치 둘(cs·ns)이 더 든다. 성공 = 42.
proc demo_search output u64 . input prog mut slice u64 . .
  input st mut slice u64 . . input cl mut slice u64 . . input nl mut slice u64 . .
  input mk mut slice u64 . . input cs mut slice u64 . .
  input ns mut slice u64 . . effects none .
do
  guard ge (len prog) 32 . else return 90 .

  rem ① 같은 답, 다른 비용: "b+c" 를 "xxbbbc" 에서 — 둘 다 가장 왼쪽 시작 2.
  let n option u64 . be rx.compile "b+c" prog st .
  guard is_some n . else return 1 .
  let a option u64 . be rx.find prog "xxbbbc" cl nl mk .
  guard is_some a . else return 2 .
  let b option u64 . be rx.search prog "xxbbbc" cl nl mk cs ns .
  guard is_some b . else return 3 .
  guard eq (some_value a) (some_value b) . else return 4 .
  guard eq (some_value b) 2 . else return 5 .

  rem ② 닻도 존중한다: "a$" 는 "aaa" 에서 마지막 a 에서만 — 시작 = 2.
  let n2 option u64 . be rx.compile "a$" prog st .
  guard is_some n2 . else return 6 .
  let e option u64 . be rx.search prog "aaa" cl nl mk cs ns .
  guard is_some e . else return 7 .
  guard eq (some_value e) 2 . else return 8 .

  rem ③ {m,n} 반복 — 컴파일 시 펼친다: "a{2,4}b" 는 "aaab" 에 anchored 끝 = 4.
  let n3 option u64 . be rx.compile "a{2,4}b" prog st .
  guard is_some n3 . else return 9 .
  let m3 option u64 . be rx.match_at prog "aaab" 0 cl nl mk .
  guard is_some m3 . else return 10 .
  guard eq (some_value m3) 4 . else return 11 .
  rem 하한 미달은 매치가 아니다: "ab" 는 a 가 하나뿐 → none.
  let f option u64 . be rx.match_at prog "ab" 0 cl nl mk .
  guard eq (is_some f) false . else return 12 .

  rem ④ 열린 상한 {m,}: "ab{2,}c" 는 "abbbbc" 에 끝 = 6.
  let n4 option u64 . be rx.compile "ab{2,}c" prog st .
  guard is_some n4 . else return 13 .
  let m4 option u64 . be rx.match_at prog "abbbbc" 0 cl nl mk .
  guard is_some m4 . else return 14 .
  guard eq (some_value m4) 6 . else return 15 .
  return 42 .
end
```

호출자는 예컨대 `prog` 32칸 · `st` 4칸 · `cl`/`nl`/`mk`(그리고 `search` 를 쓰면
`cs`/`ns` 까지) 각 32칸을 마련해 넘기면 된다(`compile` 이 돌려준 명령 수보다 크기만
하면 된다). `{m,n}` 은 펼쳐지므로 명령 수가 반복 횟수만큼 늘어난다 — 버퍼도 그만큼
잡아야 한다.

## 반례 — 이렇게 쓰면 안 된다

```lowent
rem ⓐ 닫히지 않은 그룹 — 문법 오류는 값이다. 증상: 컴파일이 none.
rx.compile "(ab" prog st          rem → none

rem ⓑ 앞이 없는 반복 — `*` 는 원자 뒤에만 온다. 증상: 컴파일이 none.
rx.compile "*a" prog st           rem → none

rem ⓒ 프로그램 버퍼 부족 — 코드와 비트맵이 만난다. 증상: none (트랩이 아니다).
var tiny mut slice u64 . be subslice prog 0 8 .
rx.compile "abcdefghij" tiny st   rem → none

rem ⓓ 스크래치 부족 — clist/nlist/marks 가 명령 수보다 짧다. 증상: 매칭이 none.
rx.match_at prog s 0 short short short   rem → none

rem ⓔ 소스에 "\d" 라고 쓴다 — 정규식이 아니라 문자열 리터럴에서 죽는다.
rem    증상: 실행 전에 E-STR-ESCAPE 컴파일 오류. "\\d" 로 써라.
rx.compile "\d" prog st           rem E-STR-ESCAPE

rem ⓕ 잘못된 {m,n} 셈 — 전부 값(none)으로 거절된다. 트랩 없음.
rx.compile "a{4,2}" prog st       rem → none (상한 < 하한)
rx.compile "a{}" prog st          rem → none (숫자 없음)
rx.compile "a{2" prog st          rem → none (안 닫힘)
rx.compile "a{65}" prog st        rem → none (상한 64 초과 — 펼치는 것이라 폭발을 막는다)
```

그리고 **ReDoS 형 패턴이 그래도 즉답하는** 예 — 역추적 엔진이라면 `(a*)*b` 에
`a` 64개를 주면 2⁶⁴ 갈래를 헤매지만, Pike VM 은 스레드 목록이라 선형으로 끝난다:

```lowent
let n option u64 . be rx.compile "(a*)*b" prog st .
guard is_some n . else return 1 .
rem a×64 — b 가 없으니 답은 none 인데, 그 none 이 즉시 나온다.
let f option u64 . be rx.match_at prog
  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" 0 cl nl mk .
guard eq (is_some f) false . else return 2 .
```

## 주의사항

- **`prog` 를 컴파일 때와 같은 길이로 매칭에 넘겨라.** 클래스 비트맵이 버퍼의 **꼬리**에
  살고, 그 주소는 `len prog` 기준(`len − 4·(k+1)`)으로 계산된다. `subslice` 로 잘라
  넘기면 명령은 살아도 비트맵 주소가 어긋나 `[…]`·`\d\w\s` 가 조용히 엉뚱한 바이트를
  본다. 프로그램은 자르지 말고 통째로 들고 다녀라.
- **한 프로그램 버퍼 = 한 패턴.** 다시 `compile` 하면 앞 프로그램은 덮인다. 두 패턴을
  번갈아 쓰려면 버퍼를 두 개 마련하라.
- 스크래치 최소 크기: `st ≥ 4`, `clist`/`nlist`/`marks`(그리고 `search` 는
  `cstart`/`nstart` 까지) 각각 ≥ **명령 수**(`compile` 의 반환값). `marks` 는
  `match_at`/`search` 가 매번 스스로 초기화한다 — 호출 사이에 보존할 것이 없다.
- **`find` 냐 `search` 냐** — 둘은 **같은 답**을 낸다. 고르는 기준은 비용뿐이다:
  `search` 는 스크래치가 둘(`cstart`/`nstart`) 더 들지만 상한이 O(n×m) 단일 패스로
  내려간다. 긴 입력·잦은 탐색이면 `search`, 작은 입력에서 스크래치를 아끼려면 `find` 다.
- 비용: `match_at` 은 O(입력 × 명령 수) **선형**, `find`/`test_at` 은 재시작 루프라
  상한 O(n²·m), `search` 는 단일 패스 O(입력 × 명령 수) 다.
- 모든 op 가 `effects none` 이고 상태는 전부 호출자 버퍼다 — **재진입은 공짜**고,
  오라클(VM≡native 차등 검증) 안에서 돈다.
- 빈 매치에 주의: `a*` 같은 패턴은 어디서나 `some at`(길이 0)으로 성공한다. `find` 는
  그런 패턴에 항상 `some 0` 을 낸다. "한 글자라도 먹었는가"가 필요하면 끝 위치를 시작과
  비교하라.
- 매칭 단위는 **바이트**다. UTF-8 입력에서 `.`/`[^…]` 는 코드포인트가 아니라 바이트
  하나를 먹는다 — 유니코드 클래스는 안 지었다(경계 절 참조).
