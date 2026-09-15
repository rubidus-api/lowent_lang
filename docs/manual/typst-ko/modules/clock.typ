#import "../lib.typ": *

= `clock` --- 시각과 마감 <mod-clock>

#modhead(file: "lib/clock.low", layer: [L2 --- 바깥 세계], caps: [`cap clock`])

"지금 몇 시인가", "얼마나 지났나", "언제까지인가" 에 답한다.

*시계를 읽는 것은 `io` 가 아니다.* 명세의 `io` 는 바깥과 자료를 주고받는 일(파일 · 연결 · 표준 입출력)인데, 시각을 읽는 것은 자료를 주고받지 않는다. 그래서 시계 op
은 효과를 적지 않고(`effects none`), 결정성을 깨는 일이라는 사실은 *권한*(`cap clock`)이 나른다. 예외는 `sleep_ms` 하나로, `wait` 효과다. 효과를 크게 적으면 부르는
쪽이 없는 비용을 떠안는다 --- 순수한 op 이 시계를 쓰지 못하게 된다. `cap random` 이 같은 이유로 갈린 짝이다(#chref("capabilities")).

```lowent
proc work input k cap clock . output u64 . effects none . do
  let due u64 be clock.deadline_in k 50 .
  var n u64 be 0 .
  while eq (clock.past k due) false . do set n (add n 1) . end
  return n .
end
```

#dtable(
  columns: 3,
  id: "mod-clock-axes",
  caption: [두 시계 --- 이름만 비슷하고 약속이 다르다],
  [], [*단조(monotonic)*], [*벽시계(local)*],
  [op], [`now_ns` · `now_ms` · `since_ns` · `since_ms` · `sleep_ms` · `deadline_in` · `past` · `left_ms`], [`local_packed` + `year_of` … `second_of`],
  [약속하는 것], [*뒤로 가지 않는다* --- 그래서 지속을 잰다], [사람이 읽는 날짜 · 시각],
  [약속하지 않는 것], [절대 시각(기준점은 뜻이 없다)], [단조성 --- 시간대 · 서머타임 · 보정으로 뒤로 갈 수 있다],
)

*지속을 잴 때 벽시계를 쓰지 않는다.* 위 표가 있는 이유가 그것이다.

#dtable(
  columns: 3,
  id: "mod-clock-ops",
  caption: [`clock` 의 op],
  [*op*], [*모양*], [*비고*],
  [`now_ns` · `now_ms`], [`(cap clock) → u64`], [단조],
  [`since_ns` · `since_ms`], [`(cap clock, start u64) → u64`], [지난 만큼],
  [`sleep_ms`], [`(cap clock, ms u64) → u64`], [실제로 잔 시간 · `effects wait`],
  [`deadline_in`], [`(cap clock, ms u64) → u64`], [마감 시점],
  [`past` · `left_ms`], [`(cap clock, deadline u64) → bool` · `u64`], [마감 판정 · 남은 시간],
  [`local_packed`], [`(cap clock) → u64`], [벽시계 여섯 값(연 · 월 · 일 · 시 · 분 · 초)을 담은 한 값],
  [`year_of` … `second_of`], [`(p u64) → u64`], [*순수* --- 푸는 짝],
)

푸는 짝이 순수하므로, 시각을 *받아서* 다루는 코드는 시계 권한을 받지 않아도 된다.

*확인하는 것 --- 무엇을 주장할 수 있나.* 시험이 주장하는 것은 단조성과 순서뿐이다. 절대 시각은 대볼 것이 없어서 주장하지 않는다 --- 없는 구별을 시험으로 지어내지
않는다. *짓지 않은 것* --- 시간대, 달력 산술(윤년 · 월말), 형식화, 타이머 · 알람, 단조 시계의 기준점.
