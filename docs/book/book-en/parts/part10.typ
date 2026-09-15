#import "../../book/lib.typ": *

This part covers *how far* the claims this language makes about itself are true. "This is proven" came up several times in earlier chapters. This part
honestly gathers what each of those claims means, what model it is about, and where it ends.

*Why* (#chref("proofs-why")) --- tests show existence and proofs show absence. Why proof, exhaustive checking and cross-checking are overlapped, and the
scale of what is proven.

*What is proven* (#chrange("proofs-numbers", "proofs-syntax-hash")) --- numbers and bounds, ownership and borrowing, effects and concurrency, syntax and
hashes. Theorems are put into plain sentences so you need not read Coq, and examples show which rejection by the compiler each theorem leads to.

*What is not proven* (#chref("proofs-limits")) --- the trusted base, the gap between model and implementation, and what remains by topic. That this part's
last chapter is the book's last chapter is no editorial accident. Quoting this part without reading that chapter makes the book say things it did not say.

The mathematics needed is high-school level --- partial orders, induction and interval arithmetic --- and is explained where it appears. Formal
explanations written so the text continues if skipped are placed in maths boxes.
