#import "../../typst-ko/lib.typ": *

= `args` --- command-line arguments <mod-args>

#modhead(file: "lib/args.low", layer: [L2 --- host (wrapper over a builtin op)], caps: [`cap args`])

Reads the command-line arguments the program received. Arguments are not a value visible everywhere; they are a *capability* handed to `main`, which is
why the first input is `cap args` (#chref("capabilities")). The module is one wrapper that passes the builtin op `arg` through.

```lowent
use args .

proc first_len input a cap args . output usize . effects none . do
  let who option slice u8 args.get a. 0 . .
  guard is_some who. . else return 0 . .
  return len some_value who. . . .
end .
```

#dtable(
  columns: 3,
  id: "mod-args-ops",
  caption: [The ops of `args`],
  [*op*], [*shape*], [*notes*],
  [`arg`], [`(cap args, i u64) → option slice u8`], [The `i`-th argument, from 0. `none` when there is no argument at that position],
)

The number of arguments comes from the core op `count a. .`.

*Not built*: option parsing (`--name value`), reading an argument as a number.
