#import "../../typst-ko/lib.typ": *

This part tours the standard library. Detailed descriptions of each module are in Appendix E, one page each, and this part does not repeat
them. Instead it sets out, with examples, *which module sits on which layer, which conventions they all follow, and where to look for a given job*.

*The map* (#chref("lib-map")) --- the three layers of language, leaf and library, the criteria for entering the library, the layers of pure
computation, storage and host, and the four conventions every module follows.

*A tour by use* (#chrange("lib-text", "lib-terminal")) --- text and encodings, containers and sorting, storage and handles, input/output, networking,
time, randomness and cryptography, and the terminal. Each chapter actually runs a few representative modules and points to the rest in one line each.

While reading this part you will see one thing again and again. The library has no privileges, so the rules learned in the previous eight parts ---
contracts, effects, capabilities, ownership --- apply as they are. Not a single new word was added. And the top of every module document always says
*what it does not do*.
