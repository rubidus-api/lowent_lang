#import "../../typst-ko/lib.typ": *

= `clock` --- time and deadlines <mod-clock>

#modhead(file: "lib/clock.low", layer: [L2 --- the outside world], caps: [`cap clock`])

Answers "what time is it", "how long has passed" and "until when".

*Reading the clock is not `io`.* In the specification `io` means exchanging data with the outside (files, connections, standard I/O), and reading the time exchanges no data.
So clock ops declare no effect (`effects none`), and the fact that they break determinism is carried by a *capability* (`cap clock`). The one exception is `sleep_ms`, which
has the `wait` effect. Declaring a larger effect would saddle callers with a cost that does not exist --- pure ops could not use the clock. `cap random` is the pair split off
for the same reason (#chref("capabilities")).

```lowent
proc work input k cap clock . output u64 . effects none . do
  let due u64 clock.deadline_in k. 50 . .
  var n u64 0 .
  while eq clock.past k. due. . false . do set n. add n. 1 . . end .
  return n. .
end .
```

#dtable(
  columns: 3,
  id: "mod-clock-axes",
  caption: [Two clocks --- similar names, different promises],
  [], [*Monotonic*], [*Wall clock (local)*],
  [ops], [`now_ns` · `now_ms` · `since_ns` · `since_ms` · `sleep_ms` · `deadline_in` · `past` · `left_ms`], [`local_packed` + `year_of` … `second_of`],
  [Promises], [*Never goes backwards* --- so it measures durations], [Human-readable date and time],
  [Does not promise], [Absolute time (the reference point is meaningless)], [Monotonicity --- time zones, daylight saving and corrections can move it backwards],
)

*Do not measure durations with the wall clock.* That is why the table above exists.

#dtable(
  columns: 3,
  id: "mod-clock-ops",
  caption: [Ops of `clock`],
  [*op*], [*Shape*], [*Notes*],
  [`now_ns` · `now_ms`], [`(cap clock) → u64`], [monotonic],
  [`since_ns` · `since_ms`], [`(cap clock, start u64) → u64`], [elapsed],
  [`sleep_ms`], [`(cap clock, ms u64) → u64`], [time actually slept · `effects wait`],
  [`deadline_in`], [`(cap clock, ms u64) → u64`], [deadline point],
  [`past` · `left_ms`], [`(cap clock, deadline u64) → bool` · `u64`], [deadline passed · time left],
  [`local_packed`], [`(cap clock) → u64`], [One value holding six wall-clock fields (year · month · day · hour · minute · second)],
  [`year_of` … `second_of`], [`(p u64) → u64`], [*pure* --- the unpacking pair],
)

Because the unpacking pair is pure, code that *receives* a time need not receive the clock capability.

*What is checked --- what can be claimed.* Tests claim only monotonicity and ordering. Absolute time is not claimed because there is nothing to compare it with --- tests do
not invent distinctions that do not exist. *Not built* --- time zones, calendar arithmetic (leap years, month ends), formatting, timers and alarms, the monotonic clock's
reference point.
