# unicode — 유니코드 속성 표

`lib/unicode.low` · 모듈 이름 `unicode` · 계층 L0(순수 계산, `effects none`)

## 처음 쓰는 사람에게

"이 글자가 문자인가, 숫자인가, 공백인가" 를 묻는 곳이다.

ASCII 만 다룰 때는 이런 물음에 손으로 답할 수 있었다. `ge c 97` 이고 `le c 122` 면 소문자다 —
비교 두 번이면 끝난다. 그런데 `'한'` 이나 `'あ'` 나 `'Ω'` 가 들어오는 순간 그 방식은 무너진다.
유니코드에서 "문자" 인 코드포인트는 **658 개의 흩어진 구간**에 걸쳐 있다. 손으로 못 적는다.

이 모듈은 그 구간표를 **Unicode 15.1.0 원본에서 기계적으로 뽑아** 그대로 싣고, 이진 탐색으로
답한다. 쓰는 쪽에서 보면 그냥 이렇다:

```lowent
use unicode .

proc ident_start output bool . input cp u64 . effects none . do
  if unicode.is_letter cp . do return true . end
  return eq cp 95 .
end
```

`is_letter 54620`(`'한'`)은 `true`, `is_letter 128512`(😀)는 `false` 다 — 이모지는 기호이지
문자가 아니다. 그 구별을 직접 적으려 들지 않아도 된다는 것이 이 모듈의 전부다.

**입력은 코드포인트(u64)다. 바이트가 아니다.** UTF-8 바이트열에서 코드포인트를 꺼내는 일은
[utf8](utf8.md) 이 한다. 둘을 이어서 쓴다.

## 왜 있는가

**표는 데이터다 — 코드로 쓰면 거짓말이 섞인다.**

이 모듈이 없으면 사람마다 자기 프로그램 안에 "글자 판정" 을 손으로 적게 된다. 그리고 그 판정은
**거의 반드시 틀린다**. 문제는 틀린 방식이다: 빠뜨린 구간은 에러가 아니라 `false` 로 나온다.
"이건 글자가 아니다" 라는 **조용히 틀린 답**이다. 파서라면 그 글자에서 토큰이 끊기고, 검색이라면
그 낱말이 안 잡히고, 아무 데서도 경고가 안 뜬다.

표시 폭([term](term.md) 의 `cp_width`)은 못 맞혀도 1 이라는 쓸 만한 기본값이 있었다. 여기는
그런 기본값이 없다. 틀린 답이 곧 오답이다. ⇒ 손으로 고르는 선택지는 처음부터 없었다.

## 설계 의도와 경계

**① 표를 고정폭 16진 문자열 하나로 싣는다.** 구간 하나 = `<시작 6자리><끝 6자리>` = 12바이트다.
고정폭이라 `n` 번째 구간의 위치가 `n * 12` 로 바로 나온다 ⇒ **디코드 없이 이진 탐색**이 된다.
표를 펼칠 배열도, 초기화 코드도, 할당도 없다. 그래서 이 층 전체가 `effects none` 으로 남고
오라클(VM ≡ 네이티브) 안에서 그대로 검증된다.

**② 범위는 U+0000–U+2FFFF 다**(BMP + SMP + SIP 시작). 그 위는 아직 거의 안 쓰인다.
그리고 **담지 않은 것을 담은 척하지 않는다** — U+30000 이상은 전부 "속성 없음" 으로 답한다.
`is_letter 196608` 은 `false` 이고, 그것이 정직한 답이다.

**③ 세부 범주는 필요해질 때 나눈다.** Lu/Ll/Lt/Lm/Lo 를 처음에는 전부 `tab_letter` 하나로 뒀다.
그러다 regex `\p{Lu}` 가 물을 곳을 만들자 **그때** Lu·Ll·Nd 를 나눴고, 이어서 나머지(Lt·Lm·Lo·
Nl·No)도 나눴다(2026-07-26). 이제 **L 과 N 의 하위가 전부 있다** — L = Lu+Ll+Lt+Lm+Lo,
N = Nd+Nl+No. P·Z·M 의 하위는 아직 안 나눴다: 물을 곳이 생기면 그때 나눈다.

**④ 표는 다시 뽑을 수 있어야 한다.** `scripts/gen-unicode-tables.py` 가 표를 뽑고, **실린 표가
추출과 바이트까지 같은지 검사**한다(골든 게이트가 매번 돌린다). "표는 데이터다" 라는 주장은
표를 재추출할 수 있을 때만 참이다 — 손으로 한 번 붙여 넣은 표는 그 순간부터 검증할 수 없는
상수 덩어리다. 새 표를 넣을 때는 `--emit <이름>` 으로 뽑아 붙인다.

**⑤ 변환은 구간이 아니라 짝이다.** 범주는 "이 구간에 드는가" 를 묻지만 변환은 "이것이
**어디로** 가는가" 다. 그래서 `tab_toupper`/`tab_tolower` 는 같은 고정폭이되 `<from><to>` 짝을
싣고 앞자리로 이진 탐색한다. **1:1 만** 싣고 — 그 한계를 삼키지 않는다(아래 `is_special_case`).

**안 지은 것(정직히)**: 정규화(NFC/NFD) · 조합 · P/Z/M 의 하위 범주 · 문자 이름 ·
**전체 케이스 폴딩**(ß→ss 같은 길이가 바뀌는 변환). 전부 **또 다른 표**가 필요하고,
표는 공짜가 아니다.

## 자료구조

없다. 구조체도 상태도 없다 — 표는 `fn` 이 돌려주는 `slice u8` 이고, 술어는 그 표를 읽는 순수
계산이다. 이 모듈에는 초기화할 것도, 닫을 것도, 들고 다닐 것도 없다.

표의 모양(불변식):

| | |
|---|---|
| 구간 하나 | 12바이트 = 시작 6자리 + 끝 6자리, 소문자 16진 |
| 정렬 | 시작 코드포인트 오름차순 |
| 겹침 | 없다(인접 구간은 병합돼 있다) |
| 끝 포함 | `start <= cp <= end` — **끝도 포함**이다 |

이진 탐색이 성립하는 근거가 저 "정렬 + 겹침 없음" 이다. 표를 직접 만들어 `has_cp` 에 넘길
생각이라면 저 두 가지를 지켜야 한다.

## op 한눈에

| op | 하는 일 |
|---|---|
| `tab_letter` | 문자(L) 표 — 658 구간 |
| `tab_number` | 숫자(N) 표 — 137 구간 |
| `tab_punct` | 문장부호(P) 표 |
| `tab_space` | 공백(Z) 표 — 탭·개행은 안 든다 |
| `tab_mark` | 결합 표시(M) 표 |
| `tab_zerowidth` | **폭 0**(Mn·Me·Cf) 표 — 354 구간 |
| `tab_upper` / `tab_lower` | 대문자(Lu) 646 · 소문자(Ll) 658 구간 |
| `tab_digit` | 십진 숫자(Nd) 표 — 64 구간 |
| `tab_title` / `tab_modifier` / `tab_other_letter` | Lt 10 · Lm 71 · Lo 509 구간 |
| `tab_numletter` / `tab_numother` | Nl 12 · No 72 구간 |
| `tab_toupper` / `tab_tolower` | 대소문자 **짝** 표 — 1423 · 1432 짝 |
| `tab_casespecial` | 1:1 로 못 바꾸는 코드포인트 — 103 개 |
| `range_count` | 표의 구간 개수 |
| `has_cp` | 표에 이 코드포인트가 드는가 — 이진 탐색 |
| `is_letter` / `is_number` / `is_punct` / `is_space` / `is_mark` | 각 표의 술어 |
| `is_alnum` | 문자 또는 숫자 |
| `is_upper` / `is_lower` | 대문자(Lu) · 소문자(Ll)인가 |
| `is_digit` | **십진** 숫자(Nd)인가 — `is_number` 보다 좁다 |
| `is_zerowidth` | 폭 0(결합 문자·서식 문자)인가 |
| `is_title` / `is_modifier` / `is_other_letter` | Lt · Lm · Lo 인가 |
| `is_numletter` / `is_numother` | Nl · No 인가 |
| `to_upper` / `to_lower` | 대문자로 / 소문자로 (없으면 **그대로**) |
| `is_special_case` | 1:1 로 못 바꾸는 코드포인트인가(ß 등) |

## op 상세

### `tab_*` — 표

```lowent
fn tab_letter output slice u8 .
```

매개변수 없다. 프로그램 수명 내내 유효한 **읽기 전용 뷰**를 돌려준다(문자열 리터럴이다 —
복사도 할당도 없다).

직접 쓸 일은 드물다. 필요한 경우는 둘이다: ① 여러 표를 한 루프에서 돌릴 때
② `range_count` 로 크기를 볼 때.

### `range_count` — 구간 개수

```lowent
fn range_count output u64 . input tab slice u8 .
```

`len tab / 12` 다. 표가 아닌 슬라이스를 넘기면 **뜻 없는 수**가 나온다(검사하지 않는다 —
이 op 은 나눗셈 하나다).

### `has_cp` — 표 조회 (핵심)

```lowent
proc has_cp output bool . input tab slice u8 . input cp u64 .
```

- `tab` — 위 모양을 지키는 표.
- `cp` — 코드포인트. **바이트가 아니다.**
- 반환 — 구간에 들면 `true`.

비용: `O(log n)` 비교, 각 비교는 6자리 16진 파싱 두 번. 할당 없음, 버퍼 없음.

**표가 망가졌으면**(16진수가 아닌 바이트가 있으면) `false` 를 돌려준다 — 트랩이 아니라 값이다.
지어내지 않는다는 뜻이고, 동시에 **망가진 표를 조용히 넘어간다**는 뜻이기도 하다. 표를 손으로
만들어 넣는다면 이 점을 기억한다.

### `is_letter` · `is_number` · `is_punct` · `is_space` · `is_mark` · `is_alnum`

```lowent
proc is_letter output bool . input cp u64 .
```

`has_cp <해당 표> cp` 의 얇은 껍질이다. 매개변수는 코드포인트 하나뿐이고 제한도 없다 —
어떤 u64 를 넣어도 답한다(범위 밖이면 `false`).

`is_alnum` 은 `is_letter` 또는 `is_number` 다. 식별자 검사에 가장 자주 쓰는 조합이라 따로 뒀다.

### `is_upper` · `is_lower` · `is_digit` — 좁은 물음

```lowent
proc is_upper output bool . input cp u64 .
proc is_lower output bool . input cp u64 .
proc is_digit output bool . input cp u64 .
```

**`is_digit` 는 `is_number` 보다 좁다.** 로마 숫자 `Ⅶ`(U+2166)나 분수 `½` 는 N 이지만 Nd 가
아니다. 자릿수를 계산하려면(`d = cp - '0'` 같은 일) 반드시 Nd 여야 한다 — N 이면 값이 10진
자릿수가 아닌 것이 섞여 들어온다.

한글은 `is_upper` 도 `is_lower` 도 `false` 다(Lo — 대소문자 구별이 없는 문자다). 그래도
`is_letter` 는 `true` 다. **"대문자가 아니다" 가 "문자가 아니다" 는 아니다.**

### `is_zerowidth` — 폭 0인가

```lowent
proc is_zerowidth output bool . input cp u64 .
```

Mn(결합 표시) · Me(둘러싸는 표시) · Cf(서식 문자)에 `true` 다. 이 코드포인트들은 **자기 칸을
차지하지 않는다** — 앞 글자에 얹힌다.

이 표는 두 곳에서 쓰인다: [term](term.md) 의 `cp_width`(폭을 0 으로 센다)와
`row_clusters`/`cluster_len`(앞 글자에 붙여 한 덩어리로 센다). **둘은 같은 표에서 나온다** —
그래서 "폭 계산은 맞는데 커서 이동은 틀리는" 어긋남이 생기지 않는다.

`is_mark` 와 다르다: `is_mark` 는 M 범주 전체(Mc = **자기 칸을 차지하는** 결합 표시 포함),
`is_zerowidth` 는 Mn+Me+Cf 다. 폭을 세려면 `is_zerowidth` 를 쓴다.

### `to_upper` · `to_lower` · `is_special_case` — 대소문자 변환

```lowent
proc to_upper output u64 . input cp u64 .
proc to_lower output u64 . input cp u64 .
proc is_special_case output bool . input cp u64 .
```

- **바뀌지 않으면 그대로 돌려준다** — `none` 이 아니다. 세상 코드포인트의 대부분은 대소문자가
  없고(한글·한자·숫자·기호), 그때마다 `option` 을 풀게 하면 호출부가 온통 `some_value` 가 된다.
- **1:1 만 한다.** 독일어 `ß`(U+00DF)의 대문자는 `"SS"` — **두 글자**라 짝으로 못 적는다.
  `to_upper 223` 은 **223 을 그대로** 돌려준다. 그게 틀린 답일 수 있으므로 `is_special_case` 가
  그런 코드포인트(103 개)를 따로 말한다. **모르는 것을 아는 척하지 않되, 숨기지도 않는다.**
- 왕복이 항상 성립하지는 않는다: `to_lower (to_upper cp)` 가 원래 값이 아닌 코드포인트가 있다
  (터키어 `ı`·그리스어 종결 시그마 등). 대소문자 무시 비교가 필요하면 그것은 **케이스 폴딩**이고,
  아직 없다.

## 사용법과 예제

### ① UTF-8 문자열에서 낱말 세기

```lowent
module wordcount .

use unicode .
use utf8 .

rem 문자·숫자가 이어지는 덩어리의 개수. 유효하지 않은 UTF-8 이면 none.
export proc count_words output option u64 . input s slice u8 . do
  var off u64 be 0 .
  var words u64 be 0 .
  var inword bool be false .
  while lt off (len s) . do
    let cp option u64 . be utf8.decode s off .
    guard is_some cp . else return none .
    let n u64 be utf8.seq_len (index s off) .
    guard gt n 0 . else return none .
    if unicode.is_alnum (some_value cp) . do
      if eq inword false . do set words (add words 1) . end
      set inword true .
    end
    if eq (unicode.is_alnum (some_value cp)) false . do
      set inword false .
    end
    set off (add off n) .
  end
  return some words .
end
```

`"한글 word 123"` 에 대해 3 이 나온다 — ASCII 만 아는 판정으로는 `'한글'` 을 못 센다.

### ② 식별자 검증

```lowent
proc valid_ident output bool . input s slice u8 . effects none . do
  guard gt (len s) 0 . else return false .
  var off u64 be 0 .
  var first bool be true .
  while lt off (len s) . do
    let cp option u64 . be utf8.decode s off .
    guard is_some cp . else return false .
    let c u64 be some_value cp .
    if first . do
      rem 첫 글자는 숫자면 안 된다.
      guard unicode.is_letter c . else do
        guard eq c 95 . else return false .
      end
    end
    if eq first false . do
      guard unicode.is_alnum c . else do
        guard eq c 95 . else return false .
      end
    end
    set first false .
    let n u64 be utf8.seq_len (index s off) .
    guard gt n 0 . else return false .
    set off (add off n) .
  end
  return true .
end
```

### ③ regex 와의 관계

[regex](regex.md) 의 `\p{L}` / `\P{L}` 이 바로 이 표를 쓴다. 정규식으로 충분하면 정규식을 쓰고,
코드포인트 하나만 물으면 되는 자리에서는 이 모듈을 직접 부르는 것이 훨씬 싸다(정규식 컴파일이
없다).

## 반례 — 이렇게 쓰면 안 된다

### ✘ 바이트를 그대로 넘긴다

```lowent
rem ✘ 틀렸다: index 는 **바이트**를 준다. '한' 의 첫 바이트는 0xED 다.
if unicode.is_letter (widen u64 (index s 0)) . do … end
```

0xED(237)는 코드포인트 U+00ED(í)다 — 우연히 문자로 판정되지만 **묻고 있던 글자가 아니다**.
UTF-8 을 다루면 반드시 `utf8.decode` 를 거친다.

```lowent
rem ✔ 옳다
let cp option u64 . be utf8.decode s 0 .
guard is_some cp . else return none .
if unicode.is_letter (some_value cp) . do … end
```

### ✘ `false` 를 "표가 망가졌다" 로 읽는다

`has_cp` 의 `false` 는 "안 든다" 와 "표가 깨졌다" 를 구별하지 않는다. 표를 직접 만들어
넘기는 경우가 아니라면 문제가 안 되지만(내장 표는 항상 온전하다), 직접 만든다면 넘기기 전에
`range_count` 와 길이(`mod (len tab) 12` 가 0 인지)를 확인한다.

### ✘ 폭을 세면서 `is_mark` 를 쓴다

```lowent
rem ✘ Mc(자기 칸을 차지하는 결합 표시)까지 0 으로 세게 된다
if unicode.is_mark cp . do set w 0 . end
```

폭에는 `is_zerowidth` 다. 애초에 폭은 [term](term.md) 의 `cp_width` 가 이미 다 처리한다 —
직접 셀 이유가 거의 없다.

### ✘ `to_upper` 가 항상 바꿔 준다고 여긴다

```lowent
rem ✘ ß 는 그대로 온다 — "SS" 는 짝으로 못 적기 때문이다
let u u64 be unicode.to_upper cp .
rem ✔ 정확해야 하면 먼저 묻는다
if unicode.is_special_case cp . do rem 두 글자 이상으로 펴야 한다 — 호출자가 정한다
end
```

### ✘ `is_number` 로 자릿수를 계산한다

```lowent
rem ✘ Ⅶ(U+2166)도 통과한다 — 그리고 cp-48 은 뜻 없는 수다
if unicode.is_number cp . do set v (add (mul v 10) (sub cp 48)) . end
rem ✔ 십진 숫자만
if unicode.is_digit cp . do … end
```

### ✘ `is_upper` 가 `false` 라고 소문자로 여긴다

한글·한자·아랍 문자는 **둘 다 false** 다. `if eq (is_upper c) false . do 소문자 . end` 는
세상 대부분의 글자를 소문자로 분류한다. 물으려면 `is_lower` 를 직접 묻는다.

## 주의사항

- **범위 밖은 조용히 `false`다.** U+30000 이상을 다루는 프로그램이라면(현재는 거의 없다)
  이 모듈이 답을 못 한다는 것을 알고 써야 한다.
- **표는 Unicode 15.1.0 기준이다.** 유니코드가 갱신되면 표도 다시 뽑는다 — 손으로 고치는 것이
  아니라 `python3 scripts/gen-unicode-tables.py --emit <이름>` 을 다시 돌린다. 인자 없이 돌리면
  **실린 표가 추출과 바이트까지 같은지 검사**한다(골든 게이트가 매번 돌린다). 이 검사가 실제로
  잡은 것이 있다: 매뉴얼이 `is_space` 에 탭·개행이 든다고 적었는데 표는 그렇지 않았다.
- **바이너리 크기**: 표 전체가 문자열로 들어간다(수십 KB). 임베디드에서 문제가 되면 필요한 표만
  쓰는 모듈을 따로 두는 것이 맞다 — 지금은 한 모듈이 전부를 들고 있다.
- **`is_space` 는 Z 범주 그대로다 — 탭·개행·CR 은 안 든다.** 유니코드에서 그것들은 제어문자(Cc)다.
  regex 의 `\s` 처럼 굴기를 바란다면 호출부에서 더한다. 이 표가 `\p{Z}` 에 그대로 쓰이기 때문에
  섞지 않았다 — 속성 Z 를 물은 사람에게 틀린 답을 줄 수는 없다.
