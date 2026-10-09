#import "../lib.typ": *

= `unicode` --- 유니코드 속성 표 <mod-unicode>

#modhead(file: "lib/unicode.low", layer: [L0 --- 순수 계산], caps: [없음])

"이 글자가 문자인가, 숫자인가, 공백인가" 를 묻는 곳이다. ASCII 만 다룰 때는 `ge c. 97 .` 이고 `le c. 122 .` 면 소문자라고 손으로 답할 수 있었다. `'한'` 이나
`'あ'` 나 `'Ω'` 가 들어오는 순간 그 방식은 무너진다. 유니코드에서 "문자" 인 코드포인트는 *658 개의 흩어진 구간*에 걸쳐 있어 손으로 적지 못한다. 이 모듈은
그 구간표를 *Unicode 15.1.0 원본에서 기계적으로 뽑아* 싣고 이진 탐색으로 답한다.

```lowent
use unicode .

proc ident_start input cp u64 . output bool . effects none . do
  if unicode.is_letter cp. . do return true . end .
  return eq cp. 95 . .
end .
```

`is_letter 54620`(`'한'`)은 `true`, `is_letter 128512`(😀)는 `false` 다 --- 이모지는 기호이지 문자가 아니다. *입력은 코드포인트(`u64`)이지 바이트가 아니다.*
UTF-8 바이트열에서 코드포인트를 꺼내는 일은 #modref("utf8")[`utf8`] 이 하고, 둘을 이어 쓴다.

*표는 데이터다 --- 코드로 쓰면 거짓말이 섞인다.* 이 모듈이 없으면 사람마다 자기 프로그램에 글자 판정을 손으로 적고, 그 판정은 거의 반드시 틀린다. 문제는
틀리는 방식이다. 빠뜨린 구간은 오류가 아니라 `false` --- *조용히 틀린 답*으로 나온다. 파서라면 그 글자에서 토큰이 끊기고, 검색이라면 그 낱말이 안 잡히며,
어디에서도 경고가 뜨지 않는다. 표시 폭(#modref("term")[`term`] 의 `cp_width`)은 틀려도 1 이라는 쓸 만한 기본값이 있었지만, 여기에는 그런 기본값이 없다.

== 설계와 경계

- *표를 고정폭 16 진 문자열 하나로 싣는다.* 구간 하나가 `<시작 6 자리><끝 6 자리>` = 12 바이트다. `n` 번째 구간의 위치가 `n × 12` 로 바로 나오므로 해독 없이
  이진 탐색이 된다. 펼칠 배열도, 초기화 코드도, 할당도 없어 층 전체가 `effects none` 으로 남고 VM·네이티브 대조 안에서 검증된다.
- *범위는 U+0000 … U+2FFFF 다.* 그 위는 아직 거의 쓰이지 않는다. *담지 않은 것을 담은 척하지 않는다* --- U+30000 이상은 모두 "속성 없음" 으로 답한다.
- *세부 범주는 물을 곳이 생길 때 나눈다.* 정규식 `\p{Lu}` 가 물을 곳을 만들었을 때 Lu·Ll·Nd 를 나눴고 이어서 나머지도 나눴다. 이제 L 과 N 의 하위가 모두
  있다(L = Lu+Ll+Lt+Lm+Lo, N = Nd+Nl+No). P·Z·M 의 하위는 아직 나누지 않았다.
- *표는 다시 뽑을 수 있다.* 개발 저장소의 추출 스크립트가 표를 뽑고, *실린 표가 추출과 바이트까지 같은지* 매번 검사한다. 손으로 한 번 붙여 넣은 표는 그
  순간부터 검증할 수 없는 상수 덩어리다. 이 검사가 실제로 잡은 것도 있다 --- 옛 문서가 `is_space` 에 탭과 개행이 든다고 적었는데 표는 그렇지 않았다.
- *변환은 구간이 아니라 짝이다.* 범주는 "이 구간에 드는가" 를, 변환은 "*어디로* 가는가" 를 묻는다. 그래서 `tab_toupper`·`tab_tolower` 는 같은 고정폭이되
  `<from><to>` 짝을 싣는다. *1 대 1 만* 싣고 그 한계를 숨기지 않는다(`is_special_case`).
- *짓지 않은 것* --- 정규화(NFC·NFD), 조합, P·Z·M 의 하위 범주, 문자 이름, 전체 대소문자 폴딩(ß → ss 처럼 길이가 바뀌는 변환). 모두 또 다른 표가 필요하고
  표는 공짜가 아니다.

표는 `fn` 이 돌려주는 `slice u8`(문자열 리터럴 --- 복사도 할당도 없다)이고, 구조체도 상태도 없다. 표의 불변식은 넷이다 --- 구간은 소문자 16 진 12 바이트,
시작 오름차순 정렬, 겹침 없음(인접 구간은 병합), *끝 포함*(`start ≤ cp ≤ end`). 표를 직접 만들어 `has_cp` 에 넘긴다면 정렬과 겹침 없음을 지켜야 이진 탐색이
선다.

== op 한눈에

#dtable(
  columns: 2,
  id: "mod-unicode-ops",
  caption: [`unicode` 의 op],
  [*op*], [*하는 일*],
  [`tab_letter` · `tab_number`], [문자(L) 표 658 구간 · 숫자(N) 표 137 구간],
  [`tab_punct` · `tab_space` · `tab_mark`], [문장부호(P) · 공백(Z, 탭과 개행은 들지 않는다) · 결합 표시(M) 표],
  [`tab_zerowidth`], [*폭 0*(Mn·Me·Cf) 표 354 구간],
  [`tab_upper` · `tab_lower` · `tab_digit`], [대문자(Lu) 646 · 소문자(Ll) 658 · 십진 숫자(Nd) 64 구간],
  [`tab_title` · `tab_modifier` · `tab_other_letter`], [Lt 10 · Lm 71 · Lo 509 구간],
  [`tab_numletter` · `tab_numother`], [Nl 12 · No 72 구간],
  [`tab_toupper` · `tab_tolower`], [대소문자 *짝* 표 1423 · 1432 짝],
  [`tab_casespecial`], [1 대 1 로 바꾸지 못하는 코드포인트 103 개],
  [`range_count`], [표의 구간 개수(`len tab / 12`)],
  [`has_cp`], [표에 이 코드포인트가 드는가 --- 이진 탐색],
  [`is_letter` · `is_number` · `is_punct` · `is_space` · `is_mark`], [각 표의 술어],
  [`is_alnum`], [문자 또는 숫자],
  [`is_upper` · `is_lower` · `is_digit`], [Lu · Ll · *십진* 숫자(Nd)인가],
  [`is_zerowidth`], [폭 0(결합 문자·서식 문자)인가],
  [`is_title` · `is_modifier` · `is_other_letter` · `is_numletter` · `is_numother`], [Lt · Lm · Lo · Nl · No 인가],
  [`to_upper` · `to_lower`], [대문자로 · 소문자로(바뀌지 않으면 *그대로*)],
  [`is_special_case`], [1 대 1 로 바꾸지 못하는 코드포인트(ß 따위)인가],
)

== op 상세

- *`tab_*`* --- 매개변수가 없고, 프로그램 수명 내내 유효한 읽기 전용 뷰를 돌려준다. 직접 쓸 일은 여러 표를 한 반복에서 돌리거나 `range_count` 로 크기를 볼
  때 정도다.
- *`range_count tab`* --- `len tab / 12`. 표가 아닌 슬라이스를 넘기면 뜻 없는 수가 나온다(검사하지 않는다).
- *`has_cp tab cp`* --- 핵심. 비용은 O(log n) 비교이고 비교마다 6 자리 16 진을 두 번 읽는다. 할당도 버퍼도 없다. *표가 망가졌으면*(16 진이 아닌 바이트) `false`
  를 돌려준다 --- 지어내지 않는다는 뜻이자 망가진 표를 조용히 넘어간다는 뜻이다.
- *`is_letter` 따위* --- `has_cp <해당 표> cp` 의 얇은 껍질이다. 어떤 `u64` 를 넣어도 답한다(범위 밖이면 `false`). `is_alnum` 은 식별자 검사에 가장 자주 쓰는
  조합이라 따로 뒀다.
- *`is_digit` 는 `is_number` 보다 좁다.* 로마 숫자 `Ⅶ`(U+2166)나 분수 `½` 는 N 이지만 Nd 가 아니다. 자릿수를 계산하려면(`d = cp − '0'`) 반드시 Nd 여야 한다.
- *한글은 `is_upper` 도 `is_lower` 도 `false` 다*(Lo --- 대소문자가 없는 문자). 그래도 `is_letter` 는 `true` 다. "대문자가 아니다" 가 "문자가 아니다" 는 아니다.
- *`is_zerowidth`* --- Mn(결합 표시)·Me(둘러싸는 표시)·Cf(서식 문자)에 `true`. 이 코드포인트들은 자기 칸을 차지하지 않고 앞 글자에 얹힌다. #modref("term")[`term`]
  의 폭 계산과 글자 묶음 세기가 *같은 표*에서 나오므로 "폭은 맞는데 커서 이동은 틀리는" 어긋남이 생기지 않는다. `is_mark` 는 M 범주 전체(자기 칸을 차지하는
  Mc 포함)라서, 폭을 세려면 `is_zerowidth` 를 쓴다.
- *`to_upper` · `to_lower`* --- 바뀌지 않으면 그대로 돌려준다(`none` 이 아니다). 세상 코드포인트의 대부분은 대소문자가 없고, 그때마다 `option` 을 풀게 하면
  호출부가 온통 `some_value` 가 된다. *1 대 1 만 한다* --- 독일어 `ß`(U+00DF)의 대문자는 두 글자 `"SS"` 라 짝으로 못 적으므로 `to_upper 223` 은 223 을 그대로
  준다. 그런 코드포인트는 `is_special_case` 가 따로 말한다. 왕복도 늘 성립하지 않는다(터키어 `ı`, 그리스어 종결 시그마). 대소문자 무시 비교는 폴딩이고 아직
  없다.

== 쓰는 법

UTF-8 문자열에서 낱말(문자·숫자가 이어진 덩어리)을 센다.

```lowent
module wordcount .

use unicode .
use utf8 .

export proc count_words input s slice u8 . output option u64 . effects none . do
  var off u64 0 .
  var words u64 0 .
  var inword bool false .
  while lt off. len s. . . do
    let cp option u64 utf8.decode s. off. . .
    guard is_some cp. . else return none . .
    let n u64 utf8.seq_len idx s. off. . . .
    guard gt n. 0 . else return none . .
    if unicode.is_alnum some_value cp. . . do
      if eq inword. false . do set words. add words. 1 . . end .
      set inword. true .
    end .
    if eq unicode.is_alnum some_value cp. . . false . do
      set inword. false .
    end .
    set off. add off. n. . .
  end .
  return some words. . .
end .
```

`"한글 word 123"` 에서 3 이 나온다 --- ASCII 만 아는 판정으로는 `'한글'` 을 세지 못한다. 정규식의 `\p{L}`·`\P{L}` 이 바로 이 표를 쓴다
(#modref("regex")[`regex`]). 정규식으로 충분하면 정규식을 쓰고, 코드포인트 하나만 물으면 되는 자리에서는 이 모듈을 직접 부르는 편이 훨씬 싸다.

== 반례

#antipattern[바이트를 그대로 넘긴다][
  ```lowent
  rem ✘ index 는 바이트를 준다. '한' 의 첫 바이트는 0xED 다
  if unicode.is_letter widen u64 idx s. 0 . . . do
    rem …
  end .
  ```
  0xED(237)는 코드포인트 U+00ED(í)다 --- 우연히 문자로 판정되지만 *묻고 있던 글자가 아니다*. UTF-8 을 다루면 반드시 `utf8.decode` 를 거친다.
]

#antipattern[폭을 세면서 `is_mark` 를 쓴다][
  Mc(자기 칸을 차지하는 결합 표시)까지 0 으로 세게 된다. 폭에는 `is_zerowidth` 다 --- 애초에 폭은 `term` 의 `cp_width` 가 다 처리한다.
]

#antipattern[`to_upper` 가 늘 바꿔 준다고 여긴다][
  `ß` 는 그대로 온다. 정확해야 하면 먼저 `is_special_case` 로 묻고, 두 글자 이상으로 펼치는 일은 호출자가 정한다.
]

#antipattern[`is_number` 로 자릿수를 계산한다][
  ```lowent
  rem ✘ Ⅶ(U+2166)도 통과하고 cp − 48 은 뜻 없는 수다
  if unicode.is_number cp. . do set v. add mul v. 10 . sub cp. 48 . . . end .
  ```
  십진 숫자만 받으려면 `is_digit` 이다.
]

#antipattern[`is_upper` 가 `false` 라서 소문자로 여긴다][
  한글·한자·아랍 문자는 둘 다 `false` 다. 그 판정은 세상 대부분의 글자를 소문자로 분류한다. 소문자인지는 `is_lower` 로 직접 묻는다.
]

== 주의

- *범위 밖은 조용히 `false` 다.* U+30000 이상을 다루는 프로그램이라면 이 모듈이 답하지 못한다는 것을 알고 쓴다.
- *`has_cp` 의 `false` 는 "들지 않는다" 와 "표가 깨졌다" 를 구별하지 않는다.* 내장 표는 늘 온전하다. 표를 직접 만든다면 넘기기 전에 `mod len tab. . 12 .` 가 0
  인지 확인한다.
- *표는 Unicode 15.1.0 기준이다.* 유니코드가 갱신되면 표를 손으로 고치지 않고 다시 뽑는다.
- *바이너리 크기.* 표 전체가 문자열로 들어간다(수십 KB). 작은 기계에서 문제가 되면 필요한 표만 쓰는 모듈을 따로 두는 것이 맞다.
- *`is_space` 는 Z 범주 그대로다 --- 탭·개행·CR 은 들지 않는다.* 유니코드에서 그것들은 제어 문자(Cc)다. 정규식의 `\s` 처럼 굴기를 바란다면 호출부에서 더한다.
  이 표가 `\p{Z}` 에 그대로 쓰이기 때문에 섞지 않았다.
