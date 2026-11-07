#import "../../typst-ko/lib.typ": *

This part covers *how far* the claims this language makes about itself are true. "This is proven" came up several times in earlier chapters. This part
honestly gathers what each of those claims means, what model it is about, and where it ends.

*Why, and the tools* (#chrefs("proofs-why", "proofs-math")) --- tests show existence and proofs show absence. Why proof, exhaustive checking and
cross-checking are overlapped, the scale of what is proven, and the six pieces of mathematics later chapters use (partial orders, lattices, fixed points,
induction, abstract interpretation), from scratch.

*Numbers and memory* (#chrange("proofs-numbers", "proofs-loops")) --- widening and division, the interval, relational and row-major proofs that remove
bounds checks, the central theorem of ownership and borrowing, and loops that are safe however many times they run.

*Effects and concurrency* (#chrange("proofs-effects", "proofs-locks")) --- the theorem that effect declarations cover reality, the theorems that discipline
removes races and parallel equals sequential, the weak memory model, and locks, where heavy tools are really needed.

*Surface and identity* (#chrefs("proofs-syntax", "proofs-hash")) --- the proof that whichever closer closes a form gives the same tree, and hashes that
recognise the same program by meaning.

*What is not proven* (#chref("proofs-limits")) --- the trusted base, the gap between model and implementation, and what remains by topic. That this part's
last chapter is the last chapter of the main text is no editorial accident. Quoting this part without reading that chapter makes this manual say things it did
not say.

Theorems are put into plain sentences so you need not read Coq, and examples show which rejection by the compiler each theorem leads to. The mathematics
needed is high-school level and is explained from scratch in #chref("proofs-math"). Formal explanations written so the text continues if skipped are placed
in maths boxes.
