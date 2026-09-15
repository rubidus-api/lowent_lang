#import "../../book/lib.typ": *

This part is where Lowent differs most from other languages. It unfolds the three things that actually hold up the goal set in #chref("intro") ---
*knowing what an op can and cannot do from its head alone*.

*Contracts* (#chref("contracts")) --- write what an op receives and what it returns. Diagnostics decide whose fault it is, and enforced contracts become
facts that remove run-time checks.

*Effects* (#chref("effects")) --- write what an op does to the outside. Effects spread along calls, and purity becomes grounds for optimisation.

*Capabilities* (#chref("capabilities")) --- write who permitted that work. Power is not scattered globally but handed over as arguments, and looking at the
entry point alone tells you what outside the program can reach.

The last chapter (#chref("errors-design")) uses all three together, covering the design that places failure with `result` at the boundary and with
contracts on the inside.
