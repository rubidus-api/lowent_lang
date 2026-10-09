#import "../lib.typ": *

= `io` --- 슬라이스 위의 스트림 읽기 <mod-io>

#modhead(file: "lib/io.low", layer: [L2 --- 바깥 세계의 순수한 절반], caps: [없음 --- 바이트를 채워 오는 쪽만 권한이 든다])

메모리에 이미 들어온 바이트열을 *스트림처럼* 조금씩 꺼내 읽는다. 파일을 통째로 읽어 놓고 줄 단위로 훑을 때, 파서에 먹일 커서가 필요할 때 쓴다. 읽기 표면이
`read(buf) → n` 이 아니라 *peek · take · toss → 뷰*다 --- 호출자 메모리를 채우는 대신 원본의 한 구간을 가리키는 빌린 뷰를 건넨다(복사 0).

```lowent
var r io.mem_reader spawn actor io.mem_reader . .
let z u64 send r. attach src. . .
let line option slice u8 send r. take_line 4096 . .
```

*왜 권한이 필요 없나.* 이 모듈은 이미 메모리에 있는 바이트를 자르기만 하고 커널에 닿지 않는다. 바이트를 실제로 채워 오는 쪽(`read_in` 은 `cap io`, `files.read` 는
`cap file_system`)만 권한을 요구한다. 그래서 프레이밍 로직 전부(버퍼 관리 · `take_line`)를 커널을 건드리기 전에 VM 과 네이티브의 대조로 검증해 두고, 커널 읽기는 이 검증된
로직에 바이트를 공급하는 얇은 잎으로 남는다(#chref("io-files")).

#aside[`none` 은 실패가 아니다][
  `peek` · `take` · `take_until` · `take_line` 의 `none` 은 *부재*다 --- "청한 만큼의 바이트가 남아 있지 않다". `mem_reader` 는 전부 산술이라 실패할 자리가 없다. 그래서
  #modref("files")[`files`] 가 `result (option T) file_error` 세 자리로 옮겨 갈 때 이 모듈은 따라가지 않았다 --- 여기에 `result` 를 붙이면 절대 생기지 않는 `error`
  가지를 부르는 쪽마다 만들게 된다. 세 자리가 필요한 것은 fd 를 직접 읽는 리더이고, 그것은 아직 없다.
]

#dtable(
  columns: 3,
  id: "mod-io-ops",
  caption: [`io` 의 op],
  [*op*], [*모양 · effects*], [*실패*],
  [`scan_until`], [`fn (src slice u8, from u64, delim u8, limit u64) → option u64`], [limit 안에 없거나 끝이면 `none`],
  [`mem_reader`], [actor --- state `src slice u8` · `pos u64`], [---],
  [`attach s`], [`→ u64`(언제나 0), state --- 소스를 걸고 커서 0. 다시 부르면 처음부터], [없음],
  [`remaining`], [`→ u64`, none], [없음],
  [`peek n`], [`→ option slice u8`, none --- 소비하지 않고 본다], [남은 게 n 미만이면 `none`],
  [`take n`], [`→ option slice u8`, state], [모자라면 `none`, 커서 불변],
  [`take_rest`], [`→ slice u8`, state], [없음(끝이면 빈 뷰)],
  [`toss n`], [`→ u64`(실제 버린 수), state], [없음(끝까지만 버린다)],
  [`take_until delim limit`], [`→ option slice u8`, state --- 구분자는 소비하되 뷰에 넣지 않는다], [못 찾으면 `none`, 커서 불변],
  [`take_line limit`], [`→ option slice u8`, state --- `take_until 10 limit`], [LF 를 limit 안에 못 찾으면 `none`],
)

*설계.* EOF 는 오류가 아니라 `none` 이다. *상한(limit)이 필수 인자다* --- 기본값이 없어 적대적 입력이 무한 읽기를 유발할 수 없고, 한 줄이 정당하게 길어야 얼마인지는
그 데이터를 아는 호출자만 안다. *부분 소비가 없다* --- 못 주면 `none` 이고 커서를 움직이지 않으므로 다른 길이로 다시 시도해도 안전하다. *개행 규약은 LF 만* --- CR 은
내용에 남는다. 라이브러리가 조용히 바이트를 지우지 않는다(지우면 되돌릴 수 없고, 안 지우면 호출자가 고를 수 있다). 위치 찾기는 순수 op `scan_until` 이 원시 연산이고
상태 있는 커서(actor)는 그 위의 얇은 껍질이다 --- 커서를 값으로 들고 싶으면(재진입 · 역추적) `scan_until` 과 호출자 pos 를 쓴다.

```lowent
proc count_lines input src slice u8 . output u64 . effects state . do
  var r io.mem_reader spawn actor io.mem_reader . .
  let z u64 send r. attach src. . .
  var lines u64 0 .
  var going bool true .
  while going. do
    let l option slice u8 send r. take_line 4096 . .
    if is_some l. . do set lines. add lines. 1 . . end .
    if eq is_some l. . false . do set going. false . end .
  end .
  if gt send r. remaining . 0 . do set lines. add lines. 1 . . end .
  return lines. .
end .
```

`count_lines` 는 파일도 stdin 도 모른다 --- `main` 이 권한을 쥔 한 줄(`read_in out. 0 buf. .`)로 바이트를 채워 `subslice` 로 넘기면 된다. 개행 없이 끝난 마지막 조각은
`take_line` 이 주지 못하므로 `remaining` 으로 확인한다.

#antipattern[`take_line` 의 `none` 을 EOF 로 단정한다][
  `none` 은 (a) 이미 끝이거나 (b) limit 안에 LF 가 없다는 뜻이다. 꼬리를 거두지 않으면 개행 없이 끝난 마지막 줄이 조용히 사라지고, 한 줄이 유난히 길면 그때부터 아무것도
  읽지 않는 것처럼 보인다. `remaining` 으로 가른다.
]

#antipattern[CRLF 입력의 뷰를 그대로 비교한다][
  `take_line` 은 CR 을 지우지 않으므로 뷰는 `"data\r"` 다. 눈으로는 같아 보이는데 `eq` 비교가 언제나 `false` 다(길이가 1 크다). 비교 전에
  `strings.remove_suffix line. "\r" .` 로 벗긴다.
]

#antipattern[`attach` 를 빼먹는다 · 루프 계속 여부를 `guard` 로 고른다][
  `spawn` 만 하고 `attach` 를 빼면 컴파일 오류가 아니라 읽기가 곧장 `none` 이다 --- 입력이 있는데 0 줄이면 `attach` 부터 확인한다. `guard` 는 반드시 나가야 하므로
  계속 여부를 고르면 `E-GUARD-FALLTHROUGH` 다 --- `if` 로 고른다.
]

*주의.* 뷰를 오래 들고 있지 않는다 --- `take_line` 이 준 슬라이스는 복사가 아니므로, 원본 버퍼를 다시 채우면 들고 있던 뷰는 다른 내용을 가리킨다. 오래 쓸 값이면 복사해
둔다. actor 핸들러를 부르는 op 도 `state` 를 선언해야 한다. 소스를 거는 핸들러가 `open` 이 아니라 `attach` 인 이유 --- `files` 의 `open` 과 겹쳐 효과 해석이 엉뚱한 쪽을
집었다. 함께 쓸 모듈들과 이름이 겹치지 않게 짓는다.
