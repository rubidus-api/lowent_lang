# term — 터미널 렌더러 (순수 절반)

소스: `lib/term.low` · 검증: `impl/tests/vm_term.low` · 의존: `lib/fmt.low` · `lib/utf8.low`

## 처음 쓰는 사람에게

이 절은 이 모듈이 무엇이고 언제 쓰는지를 한 화면에 담는다. 낯선 낱말은 여기서 한 번씩 풀어
두었으니, 뒤 절에서 다시 만나면 여기로 돌아오면 된다.

**무엇을 하는 모듈인가.** 터미널 화면에 보낼 **ANSI 제어 바이트를 조립**하고, 화면이 얼마나 바뀌었는지 계산하는 모듈이다.

**중요한 오해 하나 먼저.** 이 모듈은 **화면에 아무것도 쓰지 않는다.** 바이트를 만들어
호출자 버퍼에 넣어 줄 뿐이다. 실제로 화면에 나가는 일은 `outbuf`(버퍼링 출력 모듈)가
한다 — 조립과 출력이 나뉘어 있다. 이걸 모르면 "코드는 도는데 화면에 아무것도 안 뜬다" 는
막다른 길에 빠진다.

**낱말 넷.** 이 문서 전체가 아래 넷 위에 서 있다.

- **ANSI 이스케이프**(escape sequence) — 터미널에게 "여기로 커서를 옮겨라 / 색을 바꿔라 /
  화면을 지워라" 를 말하는 **특수 바이트열**이다. ESC(바이트 27)로 시작하고, **화면에 글자로
  보이지 않는다** — 터미널이 그것을 글자가 아니라 명령으로 알아듣는다. 예: `ESC[2J` =
  "화면을 지워라".
- **셀**(칸) — 터미널 화면의 글자 한 자리다. 화면은 셀이 격자로 늘어선 것이고, 위치는
  (행, 열)로 가리킨다.
- **화면 diff** — 직전 화면과 새 화면을 비교해 **바뀐 칸만 다시 그리는 것**이다. 매번 전체를
  다시 그리면 깜빡이고 전송량도 크다. 바뀐 구간만 골라 "거기로 가서 이것만 써라" 로 줄이는
  것이 diff 다.
- **호출자 버퍼와 `pos`** — 조립한 바이트가 담기는 자리는 이 모듈이 아니라 **호출자가 미리
  잡아 둔 버퍼**다. `pos` 는 그 버퍼에서 **다음에 쓸 자리**의 번호다. 각 op 은 새 `pos` 를
  돌려주고, 그것을 다음 op 에 넘기면 시퀀스가 차곡차곡 이어 붙는다.

**효과(effect)에 대해.** 시그니처 끝의 `effects …` 는 그 op 이 **무슨 비용을 내는지**의
선언이다. 이 모듈의 op 은 **전부 `effects none`** 이다 — 세상에 닿지 않고, 숨은 할당도 없는
**순수 조립**이라는 뜻이다. 터미널에 실제로 쓰는 일은 `effects io` 를 가진 쪽(`outbuf`)의
몫이다.

**언제 쓰나.** TUI(터미널 UI)를 만들 때, 화면 전체를 다시 그리지 않고 바뀐 곳만 고쳐 그리고 싶을 때 쓴다.

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use term as t .

rem 커서를 2행 4열(0부터 센다)로 보내는 바이트를 buf 에 쓴다 → ESC[3;5H
rem 0 은 pos — "buf 의 0번 자리부터 써라" 는 뜻이다.
let p option u64 . be t.goto buf 0 2 4 .
guard is_some p . else return 1 .          rem buf 가 좁으면 한 바이트도 안 쓰고 none 이다
rem 여기서 p 는 새 pos = 6. 아직 화면은 그대로다 — buf 에 바이트만 놓였을 뿐이다.
rem 실제로 보이게 하려면 subslice buf 0 6 을 outbuf 로 내보내야 한다.
```

**읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## 왜 있는가

이 절은 **왜 이 모듈이 화면을 건드리지 않는가**를 설명한다. 요지: 이스케이프 바이트를 짓는
일과 diff 를 계산하는 일은 IO 가 아니라 순수 계산이라, 권한도 빌트인도 필요 없다.

이 모듈은 터미널 화면을 그릴 바이트를 만들 때 쓴다. 터미널은 **ANSI 이스케이프
시퀀스** — ESC(바이트 27)로 시작하는 약속된 바이트열, 예컨대 `ESC[2J` = "화면을 지워라" —
로 조종되는 장치다. 커서 이동·색·지우기가 전부 이런 바이트열이고, 이 모듈은 그것을
호출자 버퍼에 짓는다.

RFC-0075 는 터미널을 **"순수 인코딩 + 얇은 cap 껍질"** 로 반 가른다(§5 · 3차 슬라이스 3).
이 라이브러리는 그 **순수 절반**이다: 이스케이프 시퀀스를 **호출자 버퍼에 짓는 것**과
**화면 diff 계산**(이전 화면과 다음 화면을 비교해 바뀐 부분만 찾아내는 일)은 IO 가 아니라
바이트 계산이다 — `effects none` 이고, 오라클(화면 없이 바이트만 비교하는 검증 방식) 안에서
검증된다. 그래서 capability(능력 — "세상에 닿아도 된다" 는 권한 표식) 도
빌트인도 필요 없다.

실제로 화면에 **쓰는** 쪽(cap tty · raw 모드 · 키 입력)은 **별도 슬라이스**다 — 새
capability 클래스가 필요해서, D7 이 이 슬라이스에서는 막는다.

## 설계 의도와 경계

이 절은 **이 모듈이 지키는 약속**과 **일부러 안 지은 것**을 밝힌다. 가장 중요한 약속은
"자리가 모자라면 한 바이트도 안 쓴다" 이고, 예외는 두 diff op 뿐이다.

- **cap tty 없음.** 이 라이브러리는 터미널을 건드리지 않는다. 바이트만 만든다. 만들어진
  버퍼를 내보내는 것은 호출자의 일이고, 그때 비로소 `io` 능력이 필요하다.
- 시퀀스 op 은 `fmt` 의 규약 그대로다: `(buf, pos, …) → option u64` (새 pos),
  **전량-아니면-무**(요청한 바이트를 전부 쓸 수 있을 때만 쓰고, 아니면 하나도 안 쓴다) —
  자리가 모자라면 한 바이트도 안 쓴다. 절반 쓰인 이스케이프는 화면을
  깨는, 조용히 틀린 출력이다(터미널이 명령의 뒷부분을 글자로 읽어 화면에 `[3;5H` 같은
  찌꺼기가 뜬다).
- 좌표는 **0-기준**으로 받고 ANSI 의 1-기준으로 변환해 낸다 — 코드는 0-기준으로 계산하고,
  1-기준은 배선(wire — 실제로 터미널에 나가는 바이트열)의 사정이다.
- **1바이트 셀 op 과 UTF-8 셀 op 이 나란히 있다.** `diff` 의 셀은 바이트 1개(ASCII/
  단일 바이트 charset)다. **셀이 넓어지면 diff 의 뜻이 바뀌므로** `diff` 를 고치지
  않고 UTF-8 셀 op(`row_cells` · `col_off` · `diff_row_utf8`, 슬라이스 5)을 **별도
  op 으로** 옆에 두었다 — 옛 계약을 쓰던 코드가 조용히 달라지면 안 된다. UTF-8 쪽의
  열은 **바이트 오프셋이 아니라 코드포인트 번째**다(코드포인트 = 문자 하나에 붙은
  유니코드 번호; '한' 은 UTF-8 로 3바이트지만 코드포인트로는 하나다) — 터미널 커서가
  그렇게 센다. 여전히 안 세는 것(정직히): 동아시아 넓은 글자(2칸)·결합 문자·grapheme
  cluster — 폭 표(Unicode EastAsianWidth)가 필요하고, 그 표는 별도 슬라이스다.
  속성(색) 셀도 별도 슬라이스다.
- 예외 하나: `diff`(그리고 `diff_row_utf8`)는 복합 op 라 전량-아니면-무를
  **약속하지 않는다**. `none` 이면 `out` 내용은 미정이다(정확한 크기를 먼저 알려면 두 번
  훑어야 하고, 그 비용을 낼지는 호출자가 고른다).

## 자료구조

이 절은 **화면을 어떤 모양의 데이터로 들고 다니는가**를 정한다. 새 타입은 없다 — 화면은
그냥 `slice u8` 하나이고, 폭 `w` 가 그것을 격자로 읽는 법을 알려 준다.

**화면 모델(1바이트 셀)**: 화면 = `slice u8` 하나. 폭 `w` 의 행들이 이어진 것이고,
`len % w == 0` 이어야 한다. 인덱스 `i` 의 셀은 행 `i / w`, 열 `i % w` 에 있다. 셀 하나 =
바이트 하나. (예: `w`=5 이고 10바이트면 5칸짜리 두 행이다. 인덱스 6 은 행 1·열 1 이다.)

**행 모델(UTF-8 셀, 슬라이스 5)**: 행 = 개행 없는 UTF-8 바이트열, 셀 = 코드포인트 하나.
화면 행 번호는 호출자가 들고 다니고, 열은 코드포인트 번째로 센다. 유효하지 않은 UTF-8 은
전부 값(`none`)으로 거절된다.

`diff` 는 이전 화면 `prev` 와 다음 화면 `nxt`(같은 길이)를 비교해, **같은 행 안에서
연속으로 바뀐 구간**마다 `goto + 바뀐 바이트들` 을 출력 버퍼에 쓴다. 구간은 행 경계를
넘지 않는다 — 행 끝과 다음 행 처음이 함께 바뀌면 구간 둘(goto 둘)이다. 바뀐 것이 없으면
0 바이트 — **0 바이트가 곧 "화면 건드리지 마라"** 는 답이다.

시퀀스는 전부 **CSI**(Control Sequence Introducer — `ESC [`, 바이트 `27 91` 두 개.
모든 제어 시퀀스의 머리)로 시작한다. 아래 표는 **버퍼를 얼마나 잡아야 하는지**를 재는 데
쓴다 — 필요한 크기보다 `pos` 뒤 여유가 작으면 그 op 은 `none` 이다.

| op | 시퀀스 | 크기(바이트) |
|---|---|---|
| `clear` | `ESC[2J` | 4 |
| `goto r c` | `ESC[<r+1>;<c+1>H` | 4 + 두 수의 10진 자릿수 |
| `sgr n` | `ESC[<n>m` | 3 + `dec_width n` |
| `color256 n bg` | `ESC[38;5;<n>m` / `ESC[48;5;<n>m` | 8 + `dec_width n` |
| `cursor show` | `ESC[?25h` / `ESC[?25l` | 6 |

## op 한눈에

이 절은 내보내는 op 전부를 한 표로 훑는다. 앞의 다섯은 **이스케이프 하나를 짓는 단순 op**,
`diff`/`diff_row_utf8` 은 **화면 비교**, `row_cells`/`col_off` 는 **UTF-8 열 계산 보조**,
`cp_width`/`row_width`/`fit_width` 는 **표시 폭**(칸 수 — 코드포인트 수가 아니다),
`cluster_len`/`row_clusters` 는 **사람이 세는 글자**(grapheme cluster)다.
실패는 전부 `none` 이라는 값이고 트랩이 아니다.

셋을 헷갈리면 커서가 엉뚱한 데 선다 — 한 문장으로 갈라 둔다:
**바이트 수 ≠ 코드포인트 수(`row_cells`) ≠ 칸 수(`row_width`) ≠ 글자 수(`row_clusters`).**
`"한글"` 은 6바이트 · 2코드포인트 · **4칸** · 2글자이고, `"e"+U+0301` 은 3바이트 ·
2코드포인트 · **1칸** · **1글자**다.

내보내는 op 만 적는다(`t_csi`·`t_flush_run` 은 내부다 — 내부 이름에만 `t_` 접두사가
남아 있고, export 가 아니라서 밖에서는 보이지 않는다. export op 이름에 모듈 접두사를
안 붙이는 규칙은 RFC-0075 D7b — README 의 이름 규칙 참조. 호출은 `term.goto`, 별칭을
주면 `t.goto` 다).

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `clear` | proc | `buf mut slice u8, pos u64 → option u64` | 자리 부족 = `none`, 안 씀 |
| `goto` | proc | `buf mut slice u8, pos u64, row u64, col u64 → option u64` | 자리 부족 = `none`, 안 씀 |
| `sgr` | proc | `buf mut slice u8, pos u64, n u64 → option u64` | 자리 부족 = `none`, 안 씀 |
| `color256` | proc | `buf mut slice u8, pos u64, n u64, bg bool → option u64` | 자리 부족 = `none`, 안 씀 |
| `cursor` | proc | `buf mut slice u8, pos u64, show bool → option u64` | 자리 부족 = `none`, 안 씀 |
| `diff` | proc | `prev slice u8, nxt slice u8, w u64, out mut slice u8, pos u64 → option u64` | 길이 불일치·`w`=0·`len%w≠0`·out 부족 = `none`, **out 미정** |
| `row_cells` | proc | `row_bytes slice u8 → option u64` | 유효하지 않은 UTF-8 = `none` |
| `col_off` | proc | `row_bytes slice u8, c u64 → option u64` | 유효하지 않은 UTF-8·행이 c 셀에 못 미침 = `none` |
| `diff_row_utf8` | proc | `prev slice u8, nxt slice u8, row u64, out mut slice u8, pos u64 → option u64` | 유효하지 않은 UTF-8·out 부족 = `none`, **out 미정** |
| `cp_width` | proc | `cp u64 → u64` | 실패 없다 — 모르면 1 |
| `row_width` | proc | `row_bytes slice u8 → option u64` | 유효하지 않은 UTF-8 = `none` |
| `fit_width` | proc | `row_bytes slice u8, cols u64 → option u64` | 유효하지 않은 UTF-8 = `none` |
| `cluster_len` | proc | `s slice u8, at u64 → option u64` | 범위 밖·유효하지 않은 UTF-8 = `none` |
| `row_clusters` | proc | `row_bytes slice u8 → option u64` | 유효하지 않은 UTF-8 = `none` |

## op 상세

**매개변수를 왜 받나 — 이 모듈의 규약.** 시퀀스 op 은 전부 `(buf, pos, …)` 를 받고 **새 pos**
를 돌려준다. 이렇게 하면 여러 시퀀스를 한 버퍼에 이어 쓸 수 있고(앞 op 의 반환을 다음 op 의
`pos` 로), 모듈이 버퍼를 갖지 않아도 된다. 화면을 실제로 만지지 않고 **바이트만** 만드는 것이
이 모듈의 절반이라 `effects none` 이고, 그래서 테스트가 화면 없이 바이트를 그대로 대조할 수
있다. diff 의 `prev`/`nxt` 는 각각 지금 화면과 그리려는 화면이다 — 둘을 다 받아야 무엇이
바뀌었는지 알 수 있다.


이 절은 op 하나씩을 뜯어보되, **각 매개변수를 왜 받는지**를 먼저 밝힌다. 공통 매개변수는
`buf`/`out`(쓸 자리)과 `pos`(쓸 위치) 둘이다.

**공통 매개변수의 몫**

- **`buf` / `out`(쓸 자리)** — 조립된 바이트가 담기는 **호출자의 버퍼**다. 이 모듈이 스스로
  메모리를 잡지 않기 때문에 호출자가 댄다. 그 덕에 `effects none` 이 되고, 스택·아레나 중
  무엇을 쓸지도 호출자가 고른다. `mut` 표시는 "이 op 이 여기에 쓴다" 는 뜻이다.
- **`pos`(다음에 쓸 자리)** — 버퍼의 **몇 번째 바이트부터 쓸지**다. 이것이 매개변수인 이유는
  버퍼가 호출자의 것이기 때문이다: 한 버퍼에 여러 시퀀스를 이어 지으려면 "어디까지 썼는가"를
  누군가 들고 다녀야 하는데, 이 모듈은 상태를 갖지 않으므로 호출자가 든다.

버퍼에 쓰는 op 의 성공 반환은 전부 `some <새 pos>` 다 — 그대로 다음 op 의 `pos` 로
넘겨 시퀀스를 이어 붙인다. 마지막 `pos` 가 곧 **완성된 시퀀스의 길이**이므로,
`subslice buf 0 <마지막 pos>` 가 내보낼 바이트 전부다.
`row_cells`/`col_off` 만 pos 가 아니라 개수/오프셋을 낸다.

- `clear buf pos` — 화면 지우기 `ESC[2J` 4바이트.
- `goto buf pos row col` — 커서를 (row, col)로. **0-기준** 입력이 ANSI 1-기준
  `ESC[<row+1>;<col+1>H` 로 나간다. 최대 폭을 먼저 재고, 모자라면 안 쓰고 `none`.
  - `row`/`col` 이 필요한 이유: 옮길 목적지다. 화면 좌표는 이 모듈이 알 수 없고 그리는 쪽만
    안다 — 그래서 매번 받는다. 둘 다 **0-기준**이다(왼쪽 위가 `0 0`).
- `sgr buf pos n` — SGR(글자 속성) 하나짜리 `ESC[<n>m`. `n`=0 리셋, 1 굵게, 4 밑줄,
  7 반전. `n` 이 매개변수라 속성 코드를 호출자가 고른다.
  - `n` 이 필요한 이유: 속성마다 op 을 따로 두는 대신 코드 하나로 받는다 — 새 속성이
    생겨도 op 이 늘지 않는다.
- `color256 buf pos n bg` — 256색. `bg` 가 `false` 면 전경 `ESC[38;5;<n>m`, `true` 면
  배경 `ESC[48;5;<n>m` — 전경/배경이 시퀀스의 머리 숫자만 다르기에 bool 하나로 가른다.
  - `n` 이 필요한 이유: 256색 표에서 몇 번 색인가(0~255).
  - `bg` 가 필요한 이유: 글자색인가 배경색인가. 두 시퀀스가 숫자 하나만 달라 op 을 나눌
    값어치가 없다.
- `cursor buf pos show` — 커서 보이기 `ESC[?25h`(`show`=`true`) / 숨기기 `ESC[?25l`.
  - `show` 가 필요한 이유: 같은 이유로 보이기/숨기기를 bool 하나로 가른다. 다시 그리는
    동안 커서를 숨기면 커서가 화면을 돌아다니는 것이 안 보인다.
- `diff prev nxt w out pos` — 화면 diff. **이 라이브러리의 존재 이유**다.
  - `prev`(직전 화면)가 매개변수인 이유: 무엇이 **이미 화면에 있는지**를 알아야 안 바뀐
    칸을 건너뛸 수 있다. 이 모듈은 상태가 없으므로 직전 화면을 기억하지 못한다 — 호출자가
    들고 다닌다.
  - `nxt`(그리려는 새 화면)가 매개변수인 이유: 목표 상태다. 출력에 실려 나가는 바이트는
    전부 `nxt` 에서 온다. **`prev` 와 `nxt` 의 순서를 뒤바꾸면 화면이 옛 내용으로 되돌아간다.**
  - `w`(폭)가 매개변수인 이유: 화면은 1차원 `slice u8` 이라, 폭이 있어야 행렬로 읽고
    구간이 행 경계를 넘지 않게 자를 수 있다.
  - `out`/`pos` 가 매개변수인 이유: 위 «공통 매개변수의 몫» 과 같다 — 결과 바이트를 쓸
    호출자 버퍼와, 그 버퍼에서 쓰기 시작할 자리다.
  - 조건: `len prev == len nxt` · `w > 0` · `len prev % w == 0`. 어기면 `none`.
  - 훑으면서 바뀐 셀을 만나면, 같은 행 안에서 연속으로 다른 구간 `[i, j)` 를 잡아
    `goto(행, 열)` 을 쓰고 `nxt` 의 그 구간 바이트들을 그대로 쓴다.
  - 반환 = `some <새 pos>`. 무변경이면 `some pos`(0 바이트).
  - 도중에 `out` 이 모자라면 `none` — 이때까지 쓴 바이트가 남아 있으므로 **`out` 내용은
    미정**이다. `none` 이면 그 버퍼를 화면에 흘려보내면 안 된다.
- `row_cells row_bytes` — 행(개행 없는 UTF-8 바이트열)의 **코드포인트 개수**.
  - 시그니처: `output option u64 . input row_bytes slice u8 . effects none`
  - `row_bytes` 가 필요한 이유: 셀 수를 셀 대상이다. 바이트 수와 셀 수가 다르기 때문에
    (한글 한 글자 = 3바이트 = 1셀) 이 계산이 따로 필요하다.
  - 유효하지 않은 UTF-8(첫 바이트가 시퀀스 머리가 아니거나 행이 시퀀스 중간에서 끝남)이면
    `none` — 값으로 답한다.
- `col_off row_bytes c` — 행에서 **코드포인트 c 번째의 바이트 오프셋**. 셀 번호(터미널
  좌표)와 바이트 자리(버퍼 수술 자리)를 잇는 다리다.
  - 시그니처: `output option u64 . input row_bytes slice u8 . input c u64 . effects none`
  - `c` 가 필요한 이유: 알고 싶은 열(셀 번째)이다. 터미널은 열로 세고 버퍼는 바이트로
    세므로, 그 사이를 옮기려면 열 번호를 받아야 한다.
  - 행이 c 셀에 못 미치거나 유효하지 않은 UTF-8 이면 `none` — 화면 밖을 가리키는 커서는
    만들지 않는다. `c`=0 은 항상 `some 0` 이다.
- `diff_row_utf8 prev nxt row out pos` — **UTF-8 한 행**의 diff. `prev`/`nxt` 는 같은
  행(개행 없음)의 바이트열이다.
  - `prev`/`nxt` 가 필요한 이유: `diff` 와 같다 — 직전 행과 새 행이다. 다만 단위가 화면
    전체가 아니라 **행 하나**이고, 셀이 바이트가 아니라 코드포인트다.
  - `row` 가 매개변수인 이유: 이 op 은 행 하나만 보므로 그 행이 화면의 몇째 줄인지
    모른다 — `goto` 에 넣을 행 번호는 호출자가 안다.
  - `out`/`pos` 가 매개변수인 이유: 결과 바이트를 쓸 호출자 버퍼와 시작 자리다. 행마다 이
    op 을 이어 부를 때 **앞 행이 돌려준 pos 를 그대로 다음 행에 넘긴다.**
  - 시그니처: `output option u64 . input prev slice u8 . input nxt slice u8 . input row u64 .
    input out mut slice u8 . . input pos u64 . effects none`
  - 바뀐 **코드포인트 구간**만 `goto(row, 코드포인트 열)` + 그 구간의 `nxt` 바이트로
    낸다. 한쪽이 먼저 끝나면 남은 쪽은 전부 바뀐 것으로 친다. 같으면 0 바이트(`some pos`).
  - 유효하지 않은 UTF-8 · `out` 부족 = `none` — `diff` 와 같은 이유로 **`out` 내용은
    미정**이다.

### 표시 폭 — 칸 수를 센다

- `cp_width cp` — 코드포인트 하나가 몇 칸인가. **실패가 없다**: 모르는 코드포인트는 1 이다
  (폭은 못 맞혀도 1 이 쓸 만한 기본값이라 그렇게 뒀다 — 속성 판정과 다른 점이다).
  - **0 칸**: 결합 표시·서식 문자(`unicode.is_zerowidth`). 앞 글자에 얹히므로 자기 칸이 없다.
  - **2 칸**: 한글·CJK·전각 기호 등 East Asian Wide/Fullwidth.
  - **1 칸**: 나머지.
- `row_width row_bytes` — 한 행의 칸 수 합. 유효하지 않은 UTF-8 이면 `none`.
- `fit_width row_bytes cols` — `cols` 칸 안에 들어가는 **바이트 수**를 돌려준다.
  - `cols` 가 매개변수인 이유: 터미널 폭은 실행 시각에 정해진다(`tty_size`).
  - **글자 한가운데를 자르지 않는다.** `"한글"` 을 3칸에 맞추면 `"한"`(2칸)까지 = 3바이트다.
    반 글자는 화면에서 쓰레기 바이트가 되므로 아예 안 넣는다.
  - 한 칸도 못 넣으면 `some 0` 이다(`none` 이 아니다 — "0바이트가 답" 인 정상 경우다).

### grapheme cluster — 사람이 세는 한 글자

**코드포인트 하나가 곧 한 글자는 아니다.** `"e"` 뒤에 결합 악센트(U+0301)가 오면 화면에는
`"é"` 하나가 보인다 — 코드포인트는 둘, 칸은 하나, 사람이 세는 글자도 하나다. 커서를 옮기거나
백스페이스로 지울 때 이 단위를 안 쓰면 **악센트만 지워지거나 커서가 글자 가운데 선다.**

- `cluster_len s at` — `at` 에서 시작하는 덩어리의 **바이트 길이**.
  - 규칙: 선두 코드포인트 하나 + 뒤따르는 **폭 0**(결합·서식) 전부.
  - `at` 이 범위 밖이거나 유효하지 않은 UTF-8 이면 `none`. 결과는 항상 1 이상이다.
  - `at` 은 **덩어리 경계**여야 뜻이 있다. 코드포인트 가운데를 가리키면 `none` 이다.
- `row_clusters row_bytes` — 행의 덩어리 개수. `row_cells`(코드포인트 수)와 다르다.

**안 세는 것(정직히)**: 지역 표시 기호 쌍(국기 🇰🇷) · 이모지 ZWJ 시퀀스(👨‍👩‍👧) ·
한글 자모 조합(ᄒ+ᅡ+ᆫ). 전부 UAX #29 의 나머지 규칙이 필요하고, 그건 또 다른 표다.
**부분 규칙을 완전한 척하는 것보다 무엇을 안 하는지 적어 두는 편이 낫다.**

## 사용법과 예제

이 절은 세 가지를 보인다: ① 시퀀스를 이어 붙여 한 프레임을 조립하는 법, ② 조립한 바이트를
`outbuf` 로 **실제로 내보내는** 법, ③ diff 와 UTF-8 셀 op 의 실제 값. 조립(순수)과
출력(io)이 나뉘어 있다는 점을 계속 눈여겨보면 된다.

별칭 관례는 `use term as t .` 다. 만든 바이트를 실제로 내보내려면 `io` 능력을 가진 쪽에서
`write_out`(또는 그것을 감싼 `outbuf`)으로 쓴다 — 그 부분만 `effects io` 다.

```lowent
module ex_term .

use term as t .

rem 프레임 하나를 짓는다: 지우고 → (2,4)로 가서 → 굵게. 성공 = 42.
rem 이 proc 은 effects none 이다 — 화면에는 아직 아무 일도 일어나지 않는다.
proc build_frame output u64 . input buf mut slice u8 . . effects none . do
  rem 버퍼가 너무 작으면 아래 op 들이 전부 none 이 된다 — 미리 걸러 원인을 분명히 한다.
  guard ge (len buf) 32 . else return 90 .
  rem 각 op 이 낸 새 pos 를 다음 op 의 pos 로 넘긴다 — 시퀀스가 이어 붙는다.
  let p0 option u64 . be t.clear buf 0 .          rem buf[0..4) = ESC[2J
  guard is_some p0 . else return 1 .              rem none = 자리 부족(버퍼는 그대로)
  let p1 option u64 . be t.goto buf (some_value p0) 2 4 .   rem 이어서 buf[4..10)
  guard is_some p1 . else return 2 .
  rem (2,4) 는 0-기준 — 배선에는 1-기준 ESC[3;5H = 27 91 51 59 53 72 로 나간다.
  let p2 option u64 . be t.sgr buf (some_value p1) 1 .      rem 1 = 굵게
  guard is_some p2 . else return 3 .
  rem buf[0 .. some_value p2) 가 완성된 시퀀스다 — 마지막 pos 가 곧 길이다.
  rem 여기까지는 조립일 뿐이다. 다음 예제가 이 subslice 를 실제로 내보낸다.
  return 42 .
end
```

**조립 → 실제 출력.** 위에서 지은 바이트가 화면에 나가려면 `io` 능력을 가진 쪽이 내보내야
한다. `outbuf`(버퍼링 출력, `lib/out.low`)를 쓰는 꼴은 이렇다 — **조립은 `effects none`,
내보내기만 `effects io`** 로 갈린다.

```lowent
use term   as t .
use outbuf .

rem 조립한 바이트를 실제로 터미널에 내보낸다. 성공 = 42.
proc draw
  output u64 .
  input out  cap io .         rem ① 세상에 닿을 권한 — 이것이 있어야 화면에 쓴다(권한이 먼저 온다)
  input buf  mut slice u8 .   rem ② term 이 이스케이프를 조립할 자리(호출자 버퍼)
  input obuf mut slice u8 .   rem ③ outbuf 가 나가기 전 바이트를 쌓아 둘 자리
  effects io .
do
  guard ge (len buf) 32 . else return 90 .

  rem ── 조립 단계(순수) ── term 은 buf 에 바이트만 놓는다. 화면은 아직 그대로다.
  let p0 option u64 . be t.clear buf 0 .
  guard is_some p0 . else return 1 .
  let p1 option u64 . be t.goto buf (some_value p0) 2 4 .
  guard is_some p1 . else return 2 .

  rem ── 출력 단계(io) ── 조립한 만큼만 잘라서 넘긴다. buf 통째가 아니다.
  var w owned outbuf.pending be outbuf.open 1 .        rem 1 = stdout
  let r result (owned outbuf.pending) outbuf.io_error .
    be outbuf.write out w obuf (subslice buf 0 (some_value p1)) .
  guard is_ok r . else return 3 .
  set w (ok_value r) .                                  rem 소유를 값으로 이어받는다

  rem ── flush ── 여기서 비로소 바이트가 터미널로 나간다.
  rem    이 줄을 빠뜨리면 화면에 아무것도 안 보인다 — 그래서 컴파일이 거절한다
  rem    (owned pending 을 완결 안 하면 E-OWN-INCOMPLETE).
  let f result void outbuf.io_error . be outbuf.finish out w obuf .
  guard is_ok f . else return 4 .
  return 42 .
end
```

**diff.** 여기서부터는 다시 순수 계산이다 — 결과 바이트를 눈으로 세어 볼 수 있다.

```lowent
rem diff: 폭 5·두 행에서 둘째 행의 연속 두 셀만 바뀜 → goto(1,1) + 두 바이트 = 8 바이트.
proc diff_frame output u64 . input out mut slice u8 . . effects none . do
  guard ge (len out) 32 . else return 90 .
  rem 화면은 1차원이다. 폭 5 이므로 "aaaaa" / "aaaaa" 두 행으로 읽힌다.
  let prev slice u8 be "aaaaaaaaaa" .    rem 직전 화면 — 이미 터미널에 나가 있는 내용
  let nxt  slice u8 be "aaaaaaxyaa" .    rem 새 화면 — 그리고 싶은 내용
  rem 인덱스 6·7 이 다르다 = 행 1(둘째 행), 열 1·2 — 한 구간이라 goto 는 하나다.
  rem 인수 순서에 주의: prev 가 먼저다. 뒤바꾸면 옛 내용을 다시 그리게 된다.
  let p option u64 . be t.diff prev nxt 5 out 0 .
  guard is_some p . else return 1 .
  guard eq (some_value p) 8 . else return 2 .   rem ESC[2;2H(6) + 'x' 'y'(2)
  rem out[0..8) 이 내보낼 바이트다 — 이것을 위 draw 처럼 outbuf 로 흘려보낸다.
  rem 무변경 diff 는 0 바이트 — 보낼 것이 없다는 답이다(오류가 아니다).
  let q option u64 . be t.diff nxt nxt 5 out 0 .
  guard is_some q . else return 3 .
  guard eq (some_value q) 0 . else return 4 .   rem 0 이면 아무것도 내보내지 않는다
  return 42 .
end
```

UTF-8 셀 — `impl/tests/vm_term.low` 의 `utf8_cells`·`utf8_diff` op 이 실제로 이렇게
검증한다:

```lowent
rem UTF-8 셀: 열은 바이트가 아니라 코드포인트 번째다. 성공 = 42.
proc utf8_frame output u64 . input out mut slice u8 . . effects none . do
  guard ge (len out) 32 . else return 90 .

  rem ① "한글" 은 6바이트지만 2셀이다 — 코드포인트('한'·'글')가 둘이라서.
  rem    터미널 커서는 바이트가 아니라 이 단위로 움직인다.
  let c option u64 . be t.row_cells "한글" .
  guard is_some c . else return 1 .
  guard eq (some_value c) 2 . else return 2 .

  rem ② "a한b": 셀 2번째('b')의 바이트 오프셋 = 1('a') + 3('한') = 4.
  rem    열 번호(터미널의 셈)를 버퍼 자리(바이트의 셈)로 옮기는 다리다.
  let o option u64 . be t.col_off "a한b" 2 .
  guard is_some o . else return 3 .
  guard eq (some_value o) 4 . else return 4 .
  rem 셀 수보다 먼 열은 값으로 거절된다 — 화면 밖을 가리키는 커서는 만들지 않는다.
  let far option u64 . be t.col_off "a한b" 9 .
  guard eq (is_some far) false . else return 5 .

  rem ③ 행 diff: 둘째 셀만 바뀜 → goto(0,1) = ESC[1;2H 6바이트 + '라' 3바이트 = 9바이트.
  rem    셋째 인수 0 은 "이 행은 화면의 0번째 줄" 이라는 호출자의 앎이다.
  let d option u64 . be t.diff_row_utf8 "가나다" "가라다" 0 out 0 .
  guard is_some d . else return 6 .
  guard eq (some_value d) 9 . else return 7 .
  rem 같으면 0 바이트 — 1바이트 셀 diff 와 같은 "건드리지 마라" 는 답이다.
  let z option u64 . be t.diff_row_utf8 "가나다" "가나다" 0 out 0 .
  guard is_some z . else return 8 .
  guard eq (some_value z) 0 . else return 9 .
  return 42 .
end
```

렌더 루프의 꼴: `nxt` 화면을 계산 → `diff prev nxt w out 0` → `some p` 면
`subslice out 0 p` 를 내보내고 `prev` 와 `nxt` 를 맞바꾼다. `p` 가 0 이면 아무것도
보내지 않는다. **맞바꾸는 것을 잊으면** 다음 프레임의 `prev` 가 옛 화면이라, 같은 구간을
매번 다시 그리게 된다. UTF-8 화면이면 행마다 `diff_row_utf8 prev_row next_row r out p` 를
이어 부르는 꼴이 된다 — 행 번호 `r` 는 호출자가 센다.

## 반례 — 이렇게 쓰면 안 된다

이 절은 자주 틀리는 다섯 가지와 그때 **무엇이 보이는가**(증상)를 짝지어 놓았다. 이
라이브러리에는 트랩(실행을 그 자리에서 멈추는 것)이 없다 — 증상은 대부분 `none` 이고,
`none` 을 무시하고 밀고 나갈 때만 **화면이 깨진다.**

```lowent
rem ⓐ 모자란 버퍼에 goto — 반쪽 이스케이프는 안 나간다.
rem    증상: none, 버퍼는 그대로. 화면은 멀쩡하다(아무것도 안 나갔으므로).
var tiny mut slice u8 . be subslice buf 0 5 .
t.goto tiny 0 2 4            rem 필요 6바이트 → none, 한 바이트도 안 씀

rem ⓑ prev/nxt 길이가 다르다 — diff 가 정의되지 않는다.
rem    증상: none, out 은 안 건드린다. 화면 크기가 바뀐 뒤 옛 prev 를 그대로 쓰면 이 꼴이 된다.
t.diff "ab" "abc" 2 out 0    rem → none

rem ⓒ 폭 0 · 폭이 길이를 안 나눈다.
rem    증상: none. w 를 실제 화면 폭과 다르게 넘긴 것이 흔한 원인이다.
t.diff "ab" "cd" 0 out 0     rem → none
t.diff "abc" "abd" 2 out 0   rem len 3 % 2 ≠ 0 → none

rem ⓓ diff 가 none 인데 out 을 내보낸다.
rem    증상: 화면이 깨진다. none 은 값이라 프로그램은 멈추지 않고, 반쯤 쓰인 이스케이프가
rem    터미널로 나가 찌꺼기 글자(예: [3;5H)와 엉뚱한 커서 위치로 나타난다.
let p option u64 . be t.diff prev nxt w out 0 .
rem p 가 none 이면 out 은 버려라. 더 큰 버퍼로 다시 하거나 clear + 전체 다시 그리기.

rem ⓔ prev 와 nxt 를 뒤바꿔 넘긴다.
rem    증상: none 도 오류도 없다 — 조용히 틀린다. 화면이 새 내용 대신 옛 내용으로 되돌아간다.
rem    바이트 수까지 같아서 테스트도 통과할 수 있으니, 인수 순서를 눈으로 확인해야 한다.
t.diff nxt prev w out 0      rem 틀렸다. 올바른 순서는 t.diff prev nxt w out 0
```

```lowent
rem ⓕ 코드포인트 수로 칸을 센다 — 커서가 왼쪽으로 밀린다.
rem    증상: "한글" 을 2칸으로 여겨 오른쪽 정렬이 두 칸 어긋난다.
let c option u64 . be t.row_cells "한글" .    rem 2 — 이건 **코드포인트** 수다
rem ✔ 칸을 물으려면
let w option u64 . be t.row_width "한글" .    rem 4

rem ⓖ 폭으로 자르면서 바이트 수를 직접 센다 — 반 글자가 나간다.
rem    증상: 화면에 깨진 글자(♦ 나 ?)가 뜬다. 되돌릴 수 없다 — 이미 나간 바이트다.
var cut slice u8 be subslice row 0 3 .        rem ✘ 3바이트가 글자 경계라는 보장이 없다
rem ✔ fit_width 가 경계를 지킨다
let n option u64 . be t.fit_width row 3 .
var cut2 slice u8 be subslice row 0 (some_value n) .

rem ⓗ 백스페이스를 코드포인트 단위로 지운다 — 악센트만 사라진다.
rem    증상: "é" 를 지웠는데 "e" 가 남는다. 한 번 더 눌러야 지워진다.
rem ✔ cluster_len 만큼 지운다(뒤에서 앞으로 찾으려면 경계를 앞에서부터 세어 둔다).
```

## 주의사항

이 절은 초보자가 실제로 자주 밟는 함정들이다. 요약하면 넷이다 — **조립만 하고 안 내보내면
아무것도 안 보인다 · 버퍼를 작게 잡으면 `none` 만 받는다 · `prev`/`nxt` 순서를 뒤바꾸지
마라 · 두 diff 의 `none` 은 버퍼를 못 쓰게 만든다.**

- **아무것도 안 보이면 flush 를 안 한 것이다.** 이 모듈의 op 은 전부 `effects none` 이라
  화면에 쓰지 않는다 — 버퍼에 바이트만 놓는다. 조립한 `subslice buf 0 <마지막 pos>` 를
  `outbuf` 로 넘기고 `finish`(또는 `flush`)까지 불러야 비로소 보인다. `outbuf` 는 완결을
  잊으면 컴파일이 거절하지만(E-OWN-INCOMPLETE), 애초에 내보내는 코드를 안 쓰면 컴파일은
  통과하고 화면만 조용하다.
- **`none` 만 계속 나오면 버퍼 크기를 의심하라.** 「자료구조」 절의 크기 표가 기준이다.
  `goto` 하나가 6바이트 이상, `color256` 은 9바이트 이상이다 — 버퍼를 5~10바이트로 잡고
  여러 시퀀스를 이으려 하면 두 번째부터 `none` 이 난다. `pos` **뒤에 남은 자리**로 재야
  한다는 점도 잊기 쉽다: `len buf` 가 넉넉해도 `pos` 가 이미 끝 가까이 와 있으면 `none` 이다.
- **`prev`/`nxt` 를 뒤바꾸면 아무 신호도 없이 틀린다.** 두 인수가 같은 타입·같은 길이라
  컴파일러도 실행도 잡아 주지 못한다. 증상은 "화면이 옛 내용으로 되돌아간다" 뿐이다.
  `diff <직전> <새것>` 순서를 외워 두는 편이 낫다.
- **`diff`·`diff_row_utf8` 만 전량-아니면-무가 아니다.** 단순 시퀀스 op 5개는
  실패해도 버퍼가 깨끗하지만, 두 diff 의 `none` 은 `out` 을 미정으로 만든다. `none` 을
  받으면 그 버퍼를 절대 내보내지 마라 — 더 큰 버퍼로 다시 하거나, `clear` 후 전체를
  다시 그리는 쪽으로 물러나라.
- `out` 크기의 어림: 최악(모든 셀이 바뀌고 행마다 goto 하나)에 행 수 × (goto 최대 폭) +
  화면 크기다. 확실히 하려면 화면 크기 + 행 수 × 12 정도를 잡으면 좌표가 4자리일 때까지
  안전하다. 부족은 어차피 값(`none`)으로 알게 된다.
- 무변경 = 0 바이트가 **정상 경로**다. "쓸 것이 없다"를 오류로 다루지 마라. `some 0` 을
  실패로 오해해 `clear` + 전체 다시 그리기로 물러나면, 화면은 맞지만 매 프레임 깜빡인다.
- `diff` 의 셀은 바이트 1개다. UTF-8 문자열을 1바이트 셀 화면 버퍼에 그대로 넣으면
  멀티바이트 문자가 셀 여러 개에 걸쳐 diff 가 문자 가운데를 자를 수 있다 — 증상은 `none` 이
  아니라 **화면에 깨진 글자(�)가 뜨는 것**이다. UTF-8 화면은
  행 단위로 `diff_row_utf8` 을 써라. 두 diff 는 계약이 다른 별개 op 이다 — 섞지 마라.
- **폭 표는 없다.** UTF-8 셀 op 은 코드포인트 하나 = 1칸으로 센다. 한글·CJK 넓은 글자는
  실제 터미널에서 **2칸**을 먹으므로, 넓은 글자가 섞인 행에서는 계산한 열과 실제 커서
  위치가 어긋난다(증상: 글자가 한 칸씩 밀려 찍힌다). 폭을 맞추려면 Unicode EastAsianWidth
  표가 필요하고, 그 표는 별도
  슬라이스다. 결합 문자·grapheme cluster 도 마찬가지로 세지 않는다.
- 좌표를 이미 1-기준으로 들고 있다면 그대로 넘기지 마라 — `goto` 가 또 +1 한다. 증상은
  `none` 이 아니라 **모든 것이 한 칸씩 오른쪽·아래로 밀려 그려지는 것**이다. 이
  라이브러리의 세계는 일관되게 0-기준이다.
- 모든 op 가 `effects none` 이고 스크래치가 전부 호출자 버퍼다 — 재진입은 공짜고, 화면
  없는 테스트(오라클)에서 바이트 단위로 검증할 수 있다. `vm_term.low` 가 그렇게 한다.
