# Preface

This book is a guide to Lowent, an experimental systems programming language. It rebuilds the manual that lived in the repository into a single book. Scattered manual chapters, explanations from the specification, standard library documents and proof commentary are laid out in one line of reading, and every example was actually run and checked.

The problem Lowent tries to solve fits in one sentence: **know what an op can and cannot do from its head alone.** Whether it is pure, what it receives and returns, what it does to the outside, and who permitted that are written in the head, and the compiler checks them. That goal explains nearly all of this language’s unfamiliar surface — clauses closed with periods, capabilities handed over as arguments, heads written in only one order. So this book sets up the goal before the grammar, and tries to explain how each rule follows from it.

## <a id="sx1"></a>What this book promises and does not promise

Every example in this book is a real file in `docs/manual/examples/`. The top of each file says what must happen — whether it must give the same answer on the VM and natively, or which diagnostic must reject it — and the output left by the verification script after checking it is printed on the page as is. No run results were transcribed by hand.

Thanks to that discipline, several discrepancies between the compiler and the specification came to light while this book was written. Those that could be avoided by changing examples were avoided, and those readers might trip over are written in the text as defects. So this book describes **the language the repository’s compiler actually accepts now**. Where it differs from the ideal the specification describes, it says so.

There are also things it does not promise. This language has not reached version 1, and its grammar and standard library still change. This book has its own edition number and follows the compiler as it changes. And this book has no exercises. It explains, asks questions and answers them. The ability to actually produce code grows only by writing it yourself. Fortunately this language’s compiler rejects wrong code with coded diagnostics, so writing alone is less lonely than in other languages.

## <a id="sx2"></a>Who this book is for

It assumes readers who have programmed in at least one language. It does not explain what variables, loops and functions are; it explains **how they differ** in Lowent. If you know C, the parts on memory and the outside world are easy, and if you know Rust, the borrowing rules will be familiar. It is written to be readable without either. The proof commentary in Part X can be followed with high-school mathematics.

## <a id="sx3"></a>Editions and licence

The text of this book is released under CC BY-NC-SA 4.0 and the example code under MIT. The book comes out in Korean and English editions together, with the same chapter order and examples. The code and output of the examples are one set shared by both editions.

---

[← Prev](sec01.md) · [Contents](README.md) · [Next →](sec03.md)
