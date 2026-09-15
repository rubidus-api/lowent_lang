#import "../../book/lib.typ": *

= How to read this book

This book was written to be *read front to back*. Later chapters stand on the vocabulary and rules earlier chapters set up. The top of each chapter lists
"what to know first" with chapter numbers, so if you skip ahead and get stuck, go back to that place.

But every reader's situation differs. The paths below are *shortcuts*; pick the one closest to where you are.

★ Whichever path you choose, do not drop *Parts I and IV*. Part I sets up the goal and surface of this language, and Part IV the contracts, effects and
capabilities that hold up that goal. Every other part stands on these two.

== Path ① --- learning this language from the start

The book's order as is. Read Parts I to VIII in order, only the chapters you need from Part IX, and from Part X at least the last chapter
(#chref("proofs-limits")).

== Path ② --- familiar with Rust or C and wanting only the differences

#chrange("intro", "surface") → #chrefs("numbers", "option-result") → all of Part IV (#chrange("contracts", "errors-design")) →
Part V (#chrange("regions", "fixed-memory")) → #chrefs("generics", "traits") → #chref("parallel-atomic") → #chref("ffi").
For the rest of Parts II and III, skim only each chapter's "Recap", and read the chapter when a rule is new to you.

== Path ③ --- writing for machines without an operating system

Part I → #chref("numbers") → Part IV → #chrefs("regions", "fixed-memory") → #chref("parallel-atomic") → #chrefs("ffi", "hardware") →
#chref("lib-alloc"). Where the heap is rejected (#chref("regions")) and machine tiers (#chref("hardware")) are the heart of this path.

== Path ④ --- wanting to know how far this language's claims are true

#chref("intro") → #chref("contracts") → #chref("effects") → #chref("references") → all of Part X. Each chapter of Part X states at its top which earlier
chapters it connects to.

== Devices in the text

Chapters open the same way --- this chapter's contents, what to know first, a question and answer that brings it out, why this chapter is here, what you
gain at the end, and the questions this chapter answers. The text contains questions and answers, common misconceptions, real cases, maths boxes and
demonstrations (examples with actual output), and the recap at the end of each chapter gathers the points once more. Demonstration output is printed as the
verification script left it. The long English diagnostics seen in the output are sentences the compiler actually produced.
