# A note on this translation

The Korean edition is the original. This English edition is translated from it chapter by chapter, and **chapter numbers are kept identical to the original**, so a cross-reference to “chapter 12” means the same chapter in both editions. The examples are shared: both editions print the same programs and the same verified output.

Contents

### Part I — Getting started

- 1. What Lowent sets out to do
- 2. A first program — build, run, get rejected
- 3. The surface — full stops, blocks and clause order

### Part II — Values and flow

- 4. Numbers — fixed-width integers and floating point
- 5. Ops — `fn` and `proc`
- 6. Locals — `let` and `var`
- 7. Flow — branches, loops and leaving early
- 8. Expressions — prefix notation and the `expr` island

### Part III — Data

- 9. Sequences — arrays and slices
- 10. Aggregates — `struct` and `enum`
- 11. Types that hold answers — `option` and `result`
- 12. Borrowing — `ref` and `mut_ref`
- 13. Named types — `type`, `newtype`, `range`, `cast`

### Part IV — Contracts and effects

- 14. Contracts — write them, have them checked, lose the checks
- 15. Effects — the marks an op leaves on the world
- 16. Capabilities — power that is handed over
- 17. Designing failure

### Part V — Memory

- 18. Regions — where values live, and memory reclaimed all at once
- 19. Ownership — one party responsible for disposal
- 20. Allocators and fixed memory

### Part VI — Abstraction

- 21. Modules — hidden by default
- 22. Generics — parameters fixed at translation time
- 23. Traits — one promise kept by many types
- 24. `pipe` — one line for each thing you mean to do

### Part VII — Concurrency

- 25. Actors — living with state, by messages
- 26. Tasks and channels — exchange between bound flows
- 27. Parallel loops and atomic operations

### Part VIII — The outside world

- 28. Input, output and files
- 29. Meeting C
- 30. Hardware — registers, interrupts, machine instructions
- 31. Building and testing — packages, configuration, tests, cross-checks

### Part IX — The standard library

- 32. A map of the standard library
- 33. Text and encodings — `strings`, `fmt`, `utf8`, `codec`, `hash`
- 34. Containers and sorting — `sortlib`, `sortgen`, `hashmap`, `vecgen`, `spsc`
- 35. Storage and handles — `pool`, `shard`, `budget`, `wire`
- 36. Input/output, networking, time, randomness, cryptography
- 37. The terminal — `term` and `tty`

### Part X — Grounds: what has been proven

- 38. Why prove
- 39. The mathematical toolkit
- 40. Proofs about numbers — widening, narrowing, division
- 41. Proofs about bounds — intervals, relations, row-major addresses
- 42. Proofs about ownership and borrowing
- 43. Loops and fixed points — proving “however many times”
- 44. Proofs about effects — writing “what it can do” into the type
- 45. Proofs about races and parallelism — discipline instead of a memory model
- 46. Proofs about weak memory — with the default, you may think sequentially
- 47. Proofs about locks — where heavy tools are really needed
- 48. Proofs about syntax — whichever closer closes it, the same tree
- 49. Proofs about hashes — calling things by content, not name
- 50. What is not proven

---

[← Prev](README.md) · [Contents](README.md) · [Next →](sec02.md)
