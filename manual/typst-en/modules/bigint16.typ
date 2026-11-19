#import "../../typst-ko/lib.typ": *

= `bigint16` --- big-number modular arithmetic (16-bit limbs) <mod-bigint16>

#modhead(file: "lib/bigint16.low", layer: [L0 --- pure computation (the caller's backing)], caps: [none])

The op names, inputs and meanings are the same as #modref("bigint32")[`bigint32`]'s; numbers are held in *16-bit limbs*. How to use it and the op table are on that
page --- change `use bigint32 .` to `use bigint16 .`.

A limb product is 16×16 = 32 bits, so this module is for *machines with a narrow multiplier* (8- and 16-bit CPUs). One slot's sum is (2#super[16]−1) +
(2#super[16]−1)#super[2] + (2#super[16]−1) = 2#super[32] − 1. On the machines measured (x86-64 · x86 `-m32`) it was the slowest of the three --- RSA-2048
exponentiation took 3 to 4 times as long as with `bigint32`. It has not yet been measured on a narrow machine.

A number takes `limbs nb.` = ⌈`nb` / 2⌉ slots --- the most of the three widths. That is where the rule «⌈`nb` / 2⌉ + 2 slots fit every width» comes from.
