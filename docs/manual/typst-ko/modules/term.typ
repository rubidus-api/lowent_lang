#import "../lib.typ": *

= `term` --- 터미널 렌더러(순수 절반) <mod-term>

#modhead(file: "lib/term.low", layer: [L0 --- 순수 계산(호출자의 버퍼)], caps: [없음 --- 내보내기는 #modref("outbuf")[`outbuf`] 의 몫])

터미널 화면에 보낼 *ANSI 제어 바이트를 조립*하고, 화면이 얼마나 바뀌었는지와 글자가 몇 칸을 차지하는지 계산한다. TUI 에서 전체를 다시 그리지 않고 바뀐 곳만 고쳐 그릴 때
쓴다(#chref("lib-terminal")).

#aside[이 모듈은 화면에 아무것도 쓰지 않는다][
  바이트를 만들어 호출자 버퍼에 넣을 뿐이다. 모든 op 이 `effects none` 이다. 실제로 화면에 나가는 일은 `cap io` 를 가진 쪽(#modref("outbuf")[`outbuf`])이 한다 ---
  조립과 출력이 나뉘어 있다. 이것을 모르면 "코드는 도는데 화면에 아무것도 안 뜬다" 는 막다른 길에 빠진다. 그 대가로 이스케이프 조립과 diff 전부가 화면 없이 바이트
  대조로 검증된다. 키 입력과 raw 모드는 #modref("tty")[`tty`] 다.
]

```lowent
use term as t .

let p option u64 t.goto buf 0 2 4 .
guard is_some p else return 1 .
```

*규약.* 시퀀스 op 은 #modref("fmt")[`fmt`] 의 규약 그대로다 --- `(buf, pos, …) → option u64`(새 pos). 앞 op 의 반환을 다음 op 의 `pos` 로 넘기면 시퀀스가 이어 붙고,
마지막 pos 가 곧 완성된 시퀀스의 길이다. *전량 아니면 무* --- 자리가 모자라면 한 바이트도 쓰지 않는다. 반쯤 쓰인 이스케이프는 터미널이 뒷부분을 글자로 읽어 화면에
`[3;5H` 같은 찌꺼기를 띄우는, 조용히 틀린 출력이다. 좌표는 *0 기준*으로 받아 ANSI 의 1 기준으로 바꿔 낸다 --- 1 기준은 선의 사정이다.

#dtable(
  columns: 3,
  id: "mod-term-seq",
  caption: [시퀀스 op 과 필요한 바이트],
  [*op*], [*시퀀스*], [*크기*],
  [`clear buf pos`], [`ESC[2J`], [4],
  [`goto buf pos row col`], [`ESC[<row+1>;<col+1>H`], [4 + 두 수의 10 진 자릿수],
  [`sgr buf pos n`], [`ESC[<n>m` --- 0 리셋 · 1 굵게 · 4 밑줄 · 7 반전], [3 + n 의 자릿수],
  [`color256 buf pos n bg`], [`ESC[38;5;<n>m`(전경) · `ESC[48;5;<n>m`(배경)], [8 + n 의 자릿수],
  [`cursor buf pos show`], [`ESC[?25h` · `ESC[?25l`], [6],
)

*화면 diff.* `diff prev nxt w out pos` 는 직전 화면 `prev` 와 새 화면 `nxt`(같은 길이, 폭 `w` 의 행들이 이어진 1 바이트 셀 화면)를 비교해, *같은 행 안에서 연속으로
바뀐 구간*마다 `goto + 바뀐 바이트들` 을 쓴다. 구간은 행 경계를 넘지 않는다. 바뀐 것이 없으면 0 바이트 --- *0 바이트가 곧 "화면을 건드리지 마라"* 는 정상 답이다. 조건은
`len prev == len nxt` · `w > 0` · `len prev % w == 0` 이다. UTF-8 화면은 행 단위로 `diff_row_utf8 prev nxt row out pos` 를 쓴다 --- 바뀐 *코드포인트 구간*만
`goto(row, 코드포인트 열)` + 그 구간의 `nxt` 바이트로 낸다. 셀이 넓어지면 diff 의 뜻이 바뀌므로 `diff` 를 고치지 않고 별도 op 으로 옆에 두었다 --- 옛 계약을 쓰던 코드가
조용히 달라지면 안 된다.

#dtable(
  columns: 3,
  id: "mod-term-ops",
  caption: [diff 와 셈 op --- 모두 `effects none`],
  [*op*], [*답*], [*실패*],
  [`diff prev nxt w out pos`], [`some <새 pos>`], [조건 위반 · `out` 부족 = `none`, *`out` 내용은 미정*],
  [`diff_row_utf8 prev nxt row out pos`], [`some <새 pos>`], [유효하지 않은 UTF-8 · `out` 부족 = `none`, *`out` 미정*],
  [`row_cells row`], [코드포인트 수], [유효하지 않은 UTF-8 = `none`],
  [`col_off row c`], [코드포인트 c 번째의 바이트 오프셋], [유효하지 않은 UTF-8 · 행이 c 셀에 못 미침 = `none`],
  [`cp_width cp`], [0 · 1 · 2 칸], [실패 없음 --- 모르면 1],
  [`row_width row`], [행의 칸 수 합], [유효하지 않은 UTF-8 = `none`],
  [`fit_width row cols`], [`cols` 칸 안에 들어가는 *바이트 수*(글자 가운데를 자르지 않는다)], [유효하지 않은 UTF-8 = `none` · 한 칸도 못 넣으면 `some 0`],
  [`cluster_len s at`], [`at` 에서 시작하는 글자 덩어리의 바이트 길이(1 이상)], [범위 밖 · 경계가 아님 · 유효하지 않은 UTF-8 = `none`],
  [`row_clusters row`], [행의 글자 덩어리 수], [유효하지 않은 UTF-8 = `none`],
)

*넷을 헷갈리면 커서가 엉뚱한 데 선다.* 바이트 수 ≠ 코드포인트 수(`row_cells`) ≠ 칸 수(`row_width`) ≠ 글자 수(`row_clusters`). `"한글"` 은 6 바이트 · 2 코드포인트 · *4 칸* ·
2 글자이고, `"e"` + U+0301 은 3 바이트 · 2 코드포인트 · *1 칸* · *1 글자*다. `cp_width` 는 결합 표시 · 서식 문자(#modref("unicode")[`unicode`] 의 `is_zerowidth`)에 0, 한글 ·
CJK · 전각 기호에 2, 나머지에 1 이다. 폭은 못 맞혀도 1 이 쓸 만한 기본값이라 실패가 없다 --- 속성 판정과 다른 점이다. 글자 덩어리는 선두 코드포인트 하나 + 뒤따르는 폭
0 코드포인트 전부다 --- 백스페이스와 커서 이동을 이 단위로 해야 악센트만 지워지거나 커서가 글자 가운데 서지 않는다.

```lowent
proc diff_frame input out mut slice u8 . output u64 . effects none . do
  guard ge (len out) 32 else return 90 .
  let prev slice u8 "aaaaaaaaaa" .
  let nxt slice u8 "aaaaaaxyaa" .
  let p option u64 t.diff prev nxt 5 out 0 .
  guard is_some p else return 1 .
  guard eq (some_value p) 8 else return 2 .
  let d option u64 t.diff_row_utf8 "가나다" "가라다" 0 out 0 .
  guard is_some d else return 3 .
  guard eq (some_value d) 9 else return 4 .
  return 42 .
end
```

첫 diff 는 둘째 행 열 1 · 2 만 바뀌어 `ESC[2;2H`(6) + `xy`(2) = 8 바이트, UTF-8 행 diff 는 `ESC[1;2H`(6) + `라`(3) = 9 바이트다. 렌더 루프는 `nxt` 를 계산 → `diff` → `some p` 면 `subslice out 0 p` 를 `outbuf` 로 내보내고 `prev` 와 `nxt` 를 맞바꾼다. 맞바꾸기를 잊으면 같은 구간을 매번 다시 그린다.

#antipattern[`diff` 가 `none` 인데 `out` 을 내보낸다][
  두 diff 만 전량 아니면 무가 아니다 --- 도중에 `out` 이 모자라면 쓴 바이트가 남는다. `none` 을 받고 그 버퍼를 흘려보내면 반쯤 쓰인 이스케이프가 나가 화면이 깨진다.
  더 큰 버퍼로 다시 하거나 `clear` 뒤 전체를 다시 그린다.
]

#antipattern[`prev` 와 `nxt` 를 뒤바꾼다][
  같은 타입 · 같은 길이라 컴파일도 실행도 잡지 못하고, 바이트 수까지 같아 시험도 통과할 수 있다. 증상은 화면이 옛 내용으로 되돌아가는 것뿐이다. `diff <직전> <새것>` 순서를
  외운다.
]

#antipattern[바이트 수로 자르거나 코드포인트 수로 칸을 센다][
  `subslice row 0 3` 은 3 바이트가 글자 경계라는 보장이 없어 반 글자가 나간다 --- `fit_width` 가 경계를 지킨다. `row_cells "한글"` 은 2 인데 칸은 4 라, 코드포인트 수로
  정렬하면 두 칸 어긋난다 --- 칸은 `row_width` 로 묻는다.
]

*주의.* `none` 만 계속 나오면 버퍼 크기를 의심한다 --- `pos` *뒤에 남은 자리*로 재야 한다. `out` 크기는 최악에 행 수 × goto 최대 폭 + 화면 크기로 어림한다. 무변경의
`some pos` 를 실패로 오해해 전체 다시 그리기로 물러나면 화면은 맞지만 매 프레임 깜빡인다. 좌표를 이미 1 기준으로 들고 있다면 그대로 넘기지 않는다 --- 모든 것이 한 칸씩
밀린다. 1 바이트 셀 `diff` 에 UTF-8 화면을 넣으면 멀티바이트 문자 가운데를 자를 수 있다 --- 두 diff 는 계약이 다른 별개 op 이다. *짓지 않은 것* --- 국기(지역 표시 기호
쌍) · 이모지 ZWJ 시퀀스 · 한글 자모 조합 같은 나머지 글자 덩어리 규칙, 속성(색) 셀 diff. 부분 규칙을 완전한 척하기보다 무엇을 하지 않는지 적어 둔다.
