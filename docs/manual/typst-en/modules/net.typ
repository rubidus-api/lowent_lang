#import "../../typst-ko/lib.typ": *

= `net` --- sockets (TCP loopback · in-process pairs) <mod-net>

#modhead(file: "lib/net.low", layer: [L2 --- the outside world], caps: [`cap net`])

Opens, exchanges over and closes sockets. There are two ways to open --- *TCP* (listening and connecting on 127.0.0.1) and a *pair* (two ends inside the same process, no
address). Every op takes `cap net`. The ability to reach the world is a right handed over, not something fetched from somewhere (#chref("capabilities")).

*Handles are resources.* `conn`, `listener` and `pair` must be closed, and *closing can fail* (`result void net_error`). So forgetting to close does not compile
(`E-OWN-INCOMPLETE`, #chref("ownership")).

```lowent
proc main input k cap net . output u8 . effects io . do
  let po option net.pair . be net.pair_of k .
  guard is_some po . else return 1 .
  var p owned net.pair be some_value po .
  let s option u64 . be net.send_all k (net.pair_a p) "hi" .
  guard is_some s . else return 2 .
  let c result void net.net_error . be net.shut_pair k p .
  guard is_ok c . else return 3 .
  return 0 .
end
```

#dtable(
  columns: 3,
  id: "mod-net-ops",
  caption: [Ops of `net`],
  [*op*], [*Shape*], [*Failure*],
  [`serve`], [`(cap net, port u64) → option listener`], [`none` --- could not listen],
  [`dial`], [`(cap net, port u64) → option conn`], [`none` --- could not connect],
  [`take`], [`(cap net, l listener) → option conn`], [`none` --- could not accept],
  [`pair_of`], [`(cap net) → option pair`], [`none`],
  [`send_all`], [`(cap net, fd u64, buf slice u8) → option u64`], [answers *how much was sent*],
  [`recv_once`], [`(cap net, fd u64, dst mut slice u8) → option u64`], [`some 0` = peer closed · `none` = failure],
  [`shut` · `shut_listener` · `shut_pair`], [`(cap net, owned X) → result void net_error`], [`error close_failed`. A pair closes both ends *separately*],
  [`conn_fd` · `pair_a` · `pair_b`], [`→ u64`], [*pure* --- no capability needed],
)

The listening port is read with `field l port` (the assigned number if an ephemeral port was requested). Reading an fd is pure --- a resource must not be forgotten, but
looking at its number needs no right.

*Two contracts to know.* `send_all` sends to the end but does not lie --- one send is not guaranteed to send everything, so it repeats, and if blocked midway it answers *how
far it got*. On `some n`, check that `n` equals `len buf`. `0` from `recv_once` is not failure but the *fact* that the peer closed.

*Why the close-three-at-once op was removed.* Handles used to be integers, and `shut3` closed three fds at once. Listening, connecting and accepted sockets are different
resources, and bundling three into one call leaves the compiler unable to say *what was forgotten*. Convenience that hides safety is not worth having. Now a pair holds each
end as `owned conn`, so closing one end and forgetting the other does not compile.

*Testing --- summoning failure.* `LOW_HOST_FAULT="connect:err"` (connection refused), `"accept:err"`, `"send:err@1"` (first send fails), `"recv:short=2"` (receive cut to 2
bytes). VM and native use the same injector, so both back ends must give the same answers (#chref("io-files")).

*Not built* --- remote targets (loopback only), timeouts, non-blocking I/O and polling, UDP, address parsing.
