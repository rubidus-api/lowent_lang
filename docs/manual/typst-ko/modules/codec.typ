#import "../lib.typ": *

= `codec` --- 16 진과 base64 <mod-codec>

#modhead(file: "lib/codec.low", layer: [L0 --- 순수 계산(호출자의 버퍼)], caps: [없음])

바이트를 16 진이나 base64 *글자열로 바꾸고 되돌린다*. 세상에는 텍스트만 통과시키는 자리가 많다 --- 설정 파일 한 줄, 로그, URL, 메일 본문, 화면 출력. 그런
자리에 바이트를 그대로 넣으면 0 바이트나 제어 바이트가 끼어 통로가 망가진다. 16 진(바이트 하나를 `0-9 a-f` 두 글자로)과 base64(임의 바이트를
`A-Z a-z 0-9 + /` 64 가지 글자로)는 *임의 바이트를 안전한 글자만으로 나르는 포장지*다. 16 진은 사람이 읽기 좋지만 크기가 정확히 2 배가 되고, base64 는 읽기
어렵지만 약 4/3 배로 끝난다. 해시값을 눈으로 볼 때는 16 진, 덩어리를 실어 나를 때는 base64 가 보통이다.

```lowent
use codec as c .

let n option u64 . be c.hex_enc "abc" dst .
guard is_some n . else return 1 .
```

`dst` 는 이 모듈이 만드는 것이 아니라 *호출자가 미리 잡아 두는 출력 자리*다(여기서는 6 바이트 이상). `n` 은 *실제로 쓴 바이트 수*이고 결과는 `dst[0..n)` 이다.

포장은 비밀을 지키지 않는다 --- 누구나 되돌린다. 비밀이 필요하면 봉인하는 #modref("aead")[`aead`] 를 쓴다(#chref("lib-text")). 이 모듈은 바이트에서 바이트로 가는
순수 계산이라 `index`·`set`·`shl`·`shr`·`bit_and`·`bit_or`·`guard`·`while` 만 쓰고, 내장 연산은 하나도 늘지 않았다.

== 설계와 경계

- *잘못된 입력은 값으로 답한다 --- `none` 하나다.* 홀수 길이 16 진, 16 진이 아닌 글자, base64 가 아닌 글자, 어긋난 패딩, *모자란 출력 버퍼* 모두 `none` 이다.
  멈추지도 자르지도 않는다 --- 잘라 주면 뒤의 모든 층이 "다 썼다" 는 거짓 전제 위에 선다. 성공은 `some <쓴 바이트 수>` 이고, 호출자가 `subslice` 로 정확한
  결과를 얻는다.
- *16 진 인코딩은 소문자를 낸다.* 해독은 대소문자를 둘 다 받는다 --- 관대하게 받고 보수적으로 보낸다.
- *base64 는 표준 알파벳*(`A–Z a–z 0–9 + /`)과 `=` 패딩이다. base64 는 입력을 3 바이트씩 묶어 4 글자로 적고, 마지막 묶음이 모자라면 `=` 로 메워 길이를 늘 4
  의 배수로 맞춘다.
- *짓지 않은 것* --- URL 에 안전한 변형(`-_`), 개행 접기. 필요해지면 *따로 된 op* 으로 짓는다 --- 한 op 에 모드 인자를 다는 것이 곧 혼란이다.
- *출력 버퍼는 호출자의 것이다.* 버퍼를 마련하는 비용과 정책은 호출자만 안다. 그래서 `effects none` 이고 숨은 할당이 없다.

#dtable(
  columns: 3,
  id: "mod-codec-sizes",
  caption: [필요한 출력 자리],
  [*op*], [*필요한 `dst` 크기*], [*성공 반환*],
  [`hex_enc`], [`2 × len src`], [`some (2 × len src)`],
  [`hex_dec`], [`len src / 2`], [`some (len src / 2)`],
  [`b64_enc`], [`4 × ⌈len src / 3⌉`], [`some (4 × ⌈len src / 3⌉)`],
  [`b64_dec`], [`3 × (len src / 4) − 패딩 수`], [`some <쓴 바이트 수>`],
)

`dst` 가 공식보다 작으면 결과는 늘 `none` 이고, 크면 상관없다 --- 앞에서부터 쓰고 쓴 수를 돌려준다. 빈 입력은 성공이다(`hex_enc ""`·`b64_enc ""`·`b64_dec ""`
모두 `some 0`).

== op 한눈에

#dtable(
  columns: 3,
  id: "mod-codec-ops",
  caption: [`codec` 의 op],
  [*op*], [*시그니처*], [*안 될 때*],
  [`hex_digit`], [`fn (v u64) → u8`], [실패 없음(0 … 15 가 전제)],
  [`hex_val`], [`fn (c u8) → option u64`], [`0-9 a-f A-F` 밖이면 `none`],
  [`hex_enc`], [`proc (src slice u8, dst mut slice u8) → option u64`], [`dst` 부족이면 `none`],
  [`hex_dec`], [`proc (src slice u8, dst mut slice u8) → option u64`], [홀수 길이 · 16 진이 아닌 글자 · `dst` 부족이면 `none`],
  [`b64_digit`], [`fn (v u64) → u8`], [실패 없음(0 … 63 이 전제)],
  [`b64_val`], [`fn (c u8) → option u64`], [알파벳 밖이면 `none`(`=` 도 `none`)],
  [`b64_enc`], [`proc (src slice u8, dst mut slice u8) → option u64`], [`dst` 부족이면 `none`],
  [`b64_dec`], [`proc (src slice u8, dst mut slice u8) → option u64`], [길이가 4 의 배수가 아님 · 알파벳 밖 · 어긋난 패딩 · `dst` 부족이면 `none`],
)

보통은 `hex_enc`·`hex_dec`·`b64_enc`·`b64_dec` 넷만 쓰고, 나머지 넷은 글자 하나짜리 부품이다.

== op 상세

모든 변환이 `src`(읽을 바이트)와 `dst`(쓸 자리)를 따로 받는다. 제자리 변환을 하지 않는 것은 인코딩이 결과를 키워서 원본을 덮으면 아직 읽지 않은 바이트를
밟기 때문이다. *`src` 의 길이가 곧 입력의 전부*다 --- 끝 표식을 찾지 않고 `len src` 만 믿으므로, 큰 버퍼에 든 결과를 다시 넘길 때는 `subslice` 로 정확히
잘라 준다. 결과의 길이는 `dst` 의 길이가 아니라 반환된 `some n` 의 `n` 이다.

- *`hex_digit v`* --- 니블(0 … 15)을 소문자 16 진 글자로. `v < 10` 이면 `'0'+v`, 아니면 `'a'+(v−10)`. 범위를 검사하지 않는 대신 호출자가 보장한다.
- *`hex_val c`* --- 16 진 글자 하나의 값. 대소문자를 둘 다 받고, 밖이면 `none`.
- *`hex_enc src dst`* --- 먼저 `2×len src ≤ len dst` 를 검사하고, 모자라면 아무것도 쓰지 않고 `none`.
- *`hex_dec src dst`* --- 길이가 짝수이고, 모든 글자가 16 진이고, `len src / 2 ≤ len dst` 여야 한다. 16 진이 아닌 글자는 *훑다가* 발견되므로 그 앞까지의
  바이트는 이미 `dst` 에 쓰였을 수 있다. `none` 이면 `dst` 의 내용은 정해지지 않은 것으로 다룬다.
- *`b64_digit v`* --- 표준 알파벳의 `v`(0 … 63)번째 글자. 0 … 25 는 `A-Z`, 26 … 51 은 `a-z`, 52 … 61 은 `0-9`, 62 는 `+`, 63 은 `/`. base64 는 바이트가 아니라
  6 비트 조각을 글자에 대응시킨다.
- *`b64_val c`* --- base64 글자 하나의 값. *`=` 도 `none`* 이다 --- 패딩은 글자가 아니라 해독기 본문이 다루는 구조다.
- *`b64_enc src dst`* --- `=` 패딩을 포함해 쓴다. 꼬리가 1 바이트면 `XX==`, 2 바이트면 `XXX=`.
- *`b64_dec src dst`* --- 길이가 4 의 배수이고, 모든 글자가 알파벳이거나 규칙에 맞는 패딩이고, `dst` 가 충분해야 한다. `=` 는 *마지막 묶음*에만 올 수 있고,
  셋째 자리가 `=` 면 넷째도 `=` 여야 한다. 성공하면 패딩만큼 줄어든 정확한 길이를 준다. `none` 이면 `dst` 의 내용은 정해지지 않았다.

== 쓰는 법

버퍼 크기를 먼저 확인하고, 변환하고, 돌려받은 개수로 잘라 쓴다.

```lowent
module ex_codec .

use codec as c .

proc hex_roundtrip input enc mut slice u8 . . input dec mut slice u8 . . output u64 . effects none . do
  guard ge (len enc) 6 . else return 90 .
  guard ge (len dec) 3 . else return 91 .
  let en option u64 . be c.hex_enc "abc" enc .
  guard is_some en . else return 1 .
  guard eq (some_value en) 6 . else return 2 .
  rem 쓴 수가 곧 경계다 --- enc 통째로 넘기면 뒤의 쓰레기까지 입력이 된다
  let dn option u64 . be c.hex_dec (subslice enc 0 6) dec .
  guard is_some dn . else return 3 .
  guard eq (some_value dn) 3 . else return 4 .
  guard eq (index dec 0) 97 . else return 5 .
  rem 대문자도 받는다: "4A" → 74
  let up option u64 . be c.hex_dec "4A" dec .
  guard is_some up . else return 6 .
  guard eq (index dec 0) 74 . else return 7 .
  return 42 .
end

proc b64_roundtrip input enc mut slice u8 . . input dec mut slice u8 . . output u64 . effects none . do
  rem 2 바이트는 한 묶음(4 글자)이 되고 끝에 = 하나가 붙는다: "aGk="
  guard ge (len enc) 4 . else return 90 .
  guard ge (len dec) 2 . else return 91 .
  let e option u64 . be c.b64_enc "hi" enc .
  guard is_some e . else return 1 .
  guard eq (some_value e) 4 . else return 2 .
  let d option u64 . be c.b64_dec (subslice enc 0 4) dec .
  guard is_some d . else return 3 .
  guard eq (some_value d) 2 . else return 4 .
  return 42 .
end
```

== 반례

모두 멈춤이 아니라 `none` 이다. 증상은 늘 "결과가 `none` 이고 `guard` 가 `else` 로 빠지는 것" 이다.

#dtable(
  columns: 3,
  id: "mod-codec-bad",
  caption: [자주 틀리는 입력],
  [*입력*], [*결과*], [*흔한 원인과 `dst`*],
  [`c.hex_dec "abc" dst`], [`none`], [홀수 길이 --- 길이 검사에서 먼저 걸려 `dst` 는 그대로],
  [`c.hex_dec "zz" dst`], [`none`], [16 진이 아닌 글자 --- 훑다가 발견되므로 `dst` 앞부분이 덮였을 수 있다],
  [`c.hex_enc "abcdefgh" dst3`], [`none`], [3 바이트 자리에 16 이 필요 --- 한 바이트도 쓰지 않는다],
  [`c.b64_dec "abc" dst`], [`none`], [길이가 4 의 배수가 아님 --- `enc` 를 `subslice` 로 자르지 않고 통째로 넘긴 경우가 흔하다],
  [`c.b64_dec "a?cd" dst`], [`none`], [알파벳 밖 --- URL 에 안전한 `-`·`_` 나 개행이 섞인 경우가 대부분],
  [`c.b64_dec "aG==YWJj" dst`], [`none`], [가운데 패딩 --- 두 base64 문자열을 그냥 이어 붙이면 이 꼴이 된다],
)

== 주의

- *`none` 만 계속 나오면 먼저 버퍼 크기를 의심한다.* "코드는 맞는데 늘 `none`" 의 원인은 거의 늘 `dst` 를 공식보다 작게 잡은 것이다. 넉넉히 잡는 것은 손해가
  없다.
- *`none` 이면 `dst` 를 결과로 쓰지 않는다.* 인코더는 크기를 먼저 검사해 실패하면 `dst` 가 그대로지만, 해독기는 훑으면서 쓰므로 앞부분이 덮였을 수 있다. 성공한
  `some n` 의 `dst[0..n)` 만 결과다.
- *돌려받은 수를 버리고 `dst` 통째를 결과로 쓰지 않는다.* 뒤에 남은 옛 바이트까지 섞인다.
- *인코딩과 해독의 버퍼를 겹치지 않는다.* `src` 와 `dst` 가 겹치면 아직 읽지 않은 입력을 출력이 덮는다. 겹침은 검사하지 않고 결과가 조용히 틀린다.
- *패딩을 손으로 다루지 않는다.* `b64_val` 에 `=` 를 물으면 `none` 이다. `b64_dec` 에 맡긴다.
- *URL 에 안전한 base64 와 여러 줄 base64 는 받지 않는다* --- 지원하지 않는 것이지 결함이 아니다. 넘기기 전에 호출자가 개행을 걷어 낸다.
- *선형 비용, 공짜 재진입.* 모두 입력 길이에 선형이고 상태가 없어서 여러 곳에서 동시에 불러도 간섭하지 않는다.
