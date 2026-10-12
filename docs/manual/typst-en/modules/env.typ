#import "../../typst-ko/lib.typ": *

= `env` --- environment variables <mod-env>

#modhead(file: "lib/env.low", layer: [L2 --- host (wrapper over a builtin op)], caps: [`cap env`])

Environment variables are a channel that changes behaviour quietly from outside the program. Reading one therefore takes `cap env`, and that fact shows
in the head of the op (#chref("capabilities")). The module is one wrapper that passes the builtin op `env_get` through.

```lowent
use env .

proc has_home input e cap env . output bool . effects none . do
  let v option slice u8 env.env_get e. "HOME" . .
  return is_some v. . .
end .
```

#dtable(
  columns: 3,
  id: "mod-env-ops",
  caption: [The ops of `env`],
  [*op*], [*shape*], [*notes*],
  [`env_get`], [`(cap env, name slice u8) → option slice u8`], [The value of the variable with that name. `none` when it is not set],
)

*Not built*: setting or removing a variable, listing all of them.
