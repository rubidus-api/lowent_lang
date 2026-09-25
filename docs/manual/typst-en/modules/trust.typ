#import "../../typst-ko/lib.typ": *

= `trust` --- finding a trusted root in the trust store <mod-trust>

#modhead(file: "lib/trust.low", layer: [L2 --- host], caps: [`cap file_system`])

The last question of chain checking is "was this intermediate signed by a certificate we *decided in advance to trust*". Those certificates
live in the *trust store* --- on Linux, a bundle file of PEM certificates joined together. This module finds the one certificate in that
bundle whose *subject name is this one*.

#aside[Here "trust" means "trust this file"][
  Who writes that file is not this module's question; it belongs to whoever handed over the right to open it (`cap file_system`). To keep
  that visible the path is an *argument* (not built in).
]

== It never holds the whole bundle

A system bundle is over 200 KB (measured: 224,449 bytes · 150 certificates). This language's heap arena is 64 KiB. So it *streams* --- it
slides one window along, unwraps PEM blocks one at a time, compares only the name, and drops them. Only the one it finds is kept.

```text
 file:      [ cert 1 ][ cert 2 ][ cert 3 ][ cert 4 ] …
 window 1:  [ cert 1 ][ cert 2 ][ front of cert 3 ]
                                 └─ a block that is not complete is not unwrapped in this window
 window 2:            [ front of cert 3 ][ rest ][ cert 4 ] …
                      └─ the leftover tail is pulled to the front, and the rest is filled by the next read
```

Without pulling the tail forward, a certificate that straddles a window edge silently becomes "not there" --- and that one may be exactly the
root being looked for.

== ops

#dtable(
  columns: 2,
  id: "mod-trust-ops",
  caption: [ops of `trust`],
  [*op*], [*what it does*],
  [`find_anchor`], [finds the certificate whose subject name is `want` (at offset `wo`) in the bundle file `path`, puts its DER in `out` and returns the length. `effects io` --- it reads a file],
)

`find_anchor` has four answers.

#dtable(
  columns: 2,
  id: "mod-trust-results",
  caption: ["not found" and "could not measure" are different],
  [*answer*], [*meaning*],
  [`ok n` (n > 0)], [found --- the first `n` bytes of `out` are that certificate],
  [`ok 0`], [read to the end, and it is not there --- the only thing said *after measuring*],
  [`error short_workspace`], [the window (`win`, over 16 KiB) or the unwrap space was too small --- it *could not read to the end* (a block larger than the window, or the step limit)],
  [`error unreadable`], [the bundle file could not be opened, or a read failed --- check the path first],
)

The last two rows once did not exist. A window too narrow to read everything gave 0, and a file that would not even open gave
`ok 0` (fixed 2026-09-25). So the tool said "there is no trusted root" --- which was not *absent* but *unmeasured*.

```text
 a read failed ──────────────────────▶ error unreadable
 block larger than window / step cap ▶ error short_workspace
 read to the end ─┬─ there ──────────▶ ok n
                  └─ not there ──────▶ ok 0      <- the only "not there"
```

Linking the last step with the found root is #modref("verify")[`verify`]'s `link_ok`. Modules it uses: #modref("files")[`files`] ·
#modref("pem")[`pem`] · #modref("x509")[`x509`] · `verify`.
