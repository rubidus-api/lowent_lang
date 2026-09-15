# Detailed contents

The contents opened out to the section level --- for going straight to a place.

Searches headings and index terms --- not the full text

### Part I — Getting started

[1 What Lowent sets out to do](ch01.md)

- [1.1 Can you tell from the head alone?](ch01.md#s1-1)
- [1.2 Five ideas](ch01.md#s1-2)
- [1.3 One meaning, one spelling](ch01.md#s1-3)
- [1.4 Where the language stands](ch01.md#s1-4)
- [1.5 How to read this book](ch01.md#s1-5)

[2 A first program — build, run, get rejected](ch02.md)

- [2.1 Building the tool](ch02.md#s2-1)
- [2.2 hello, entropy](ch02.md#s2-2)
- [2.3 Running it two ways](ch02.md#s2-3)
- [2.4 How a contract stops execution](ch02.md#s2-4)
- [2.5 Getting rejected](ch02.md#s2-5)
- [2.6 --fmt fixes the shape](ch02.md#s2-6)
- [2.7 Arguments and capabilities](ch02.md#s2-7)

[3 The surface — full stops, blocks and clause order](ch03.md)

- [3.1 The name first, then the arguments](ch03.md#s3-1)
- [3.2 A free-standing full stop closes](ch03.md#s3-2)
- [3.3 A block is always do … end](ch03.md#s3-3)
- [3.4 Comments and literals](ch03.md#s3-4)
- [3.5 Names](ch03.md#s3-5)
- [3.6 Words are a budget](ch03.md#s3-6)
- [3.7 The boundaries of the expr island](ch03.md#s3-7)
- [3.8 Clause order in an op head](ch03.md#s3-8)

### Part II — Values and flow

[4 Numbers — fixed-width integers and floating point](ch04.md)

- [4.1 Integer types name their width](ch04.md#s4-1)
- [4.2 Widening is automatic, narrowing is written](ch04.md#s4-2)
- [4.3 Overflow stops](ch04.md#s4-3)
- [4.4 Division and remainder](ch04.md#s4-4)
- [4.5 Booleans are not numbers](ch04.md#s4-5)
- [4.6 Bitwise operations](ch04.md#s4-6)
- [4.7 Floating point](ch04.md#s4-7)

[5 Ops — fn and proc](ch05.md)

- [5.1 Two kinds](ch05.md#s5-1)
- [5.2 Purity is decided by observation](ch05.md#s5-2)
- [5.3 Don’t write effects on a fn](ch05.md#s5-3)
- [5.4 Effects spread to the caller](ch05.md#s5-4)
- [5.5 Parameters and return](ch05.md#s5-5)
- [5.6 Only neg is unary](ch05.md#s5-6)
- [5.7 Modifiers](ch05.md#s5-7)

[6 Locals — let and var](ch06.md)

- [6.1 The word says whether it changes](ch06.md#s6-1)
- [6.2 Write the type, or leave it to the value](ch06.md#s6-2)
- [6.3 No name without a value](ch06.md#s6-3)
- [6.4 How long a local lives](ch06.md#s6-4)
- [6.5 if yields no value](ch06.md#s6-5)

[7 Flow — branches, loops and leaving early](ch07.md)

- [7.1 Conditions and loops](ch07.md#s7-1)
- [7.2 guard — turning a condition into a fact](ch07.md#s7-2)
- [7.3 Every path returns a value](ch07.md#s7-3)
- [7.4 match — every case, none missing](ch07.md#s7-4)
- [7.5 panic is an effect](ch07.md#s7-5)

[8 Expressions — prefix notation and the expr island](ch08.md)

- [8.1 Nothing to memorise in prefix notation](ch08.md#s8-1)
- [8.2 The island’s precedence table](ch08.md#s8-2)
- [8.3 What the island cannot do](ch08.md#s8-3)
- [8.4 Short-circuiting guards conditions](ch08.md#s8-4)
- [8.5 Expressions computed at translation time](ch08.md#s8-5)

### Part III — Data

[9 Sequences — arrays and slices](ch09.md)

- [9.1 Start and length together](ch09.md#s9-1)
- [9.2 Writing needs mut slice](ch09.md#s9-2)
- [9.3 Narrowing the window](ch09.md#s9-3)
- [9.4 Contracts remove bounds checks](ch09.md#s9-4)

[10 Aggregates — struct and enum](ch10.md)

- [10.1 struct — a collection of named fields](ch10.md#s10-1)
- [10.2 enum — one of several](ch10.md#s10-2)
- [10.3 Variants are closed with full stops](ch10.md#s10-3)
- [10.4 Nothing may contain itself](ch10.md#s10-4)

[11 Types that hold answers — option and result](ch11.md)

- [11.1 option — a value, or none](ch11.md#s11-1)
- [11.2 result and the errors clause](ch11.md#s11-2)
- [11.3 try — passing failure upwards](ch11.md#s11-3)
- [11.4 Crossing between the two channels](ch11.md#s11-4)
- [11.5 Three ways to report failure](ch11.md#s11-5)
- [11.6 Combining and nesting patterns](ch11.md#s11-6)

[12 Borrowing — ref and mut_ref](ch12.md)

- [12.1 Two kinds of borrow](ch12.md#s12-1)
- [12.2 Write permission only narrows](ch12.md#s12-2)
- [12.3 Many readers or one writer](ch12.md#s12-3)
- [12.4 A borrow cannot outlive what it borrows](ch12.md#s12-4)
- [12.5 There is no mut ref slice](ch12.md#s12-5)

[13 Named types — type, newtype, range, cast](ch13.md)

- [13.1 type is an alias, newtype a new type](ch13.md#s13-1)
- [13.2 range — a contract that became the shape of a parameter](ch13.md#s13-2)
- [13.3 cast — where a value may change](ch13.md#s13-3)
- [13.4 bits — from 1 to 64 bits](ch13.md#s13-4)
- [13.5 Pinning down byte layout](ch13.md#s13-5)

### Part IV — Contracts and effects

[14 Contracts — write them, have them checked, lose the checks](ch14.md)

- [14.1 A contract is a checked promise](ch14.md#s14-1)
- [14.2 Whose fault is it?](ch14.md#s14-2)
- [14.3 Contracts remove checks](ch14.md#s14-3)
- [14.4 Conditions over every element](ch14.md#s14-4)
- [14.5 Naming a contract](ch14.md#s14-5)
- [14.6 Decide now what can be decided now](ch14.md#s14-6)
- [14.7 Contract grades](ch14.md#s14-7)
- [14.8 Build modes decide the remaining checks](ch14.md#s14-8)

[15 Effects — the marks an op leaves on the world](ch15.md)

- [15.1 Effects are closed words](ch15.md#s15-1)
- [15.2 Effects spread to the caller](ch15.md#s15-2)
- [15.3 Declared but not done is reported](ch15.md#s15-3)
- [15.4 The effects line is a set](ch15.md#s15-4)
- [15.5 via — a type argument decides the effects](ch15.md#s15-5)
- [15.6 What purity allows](ch15.md#s15-6)

[16 Capabilities — power that is handed over](ch16.md)

- [16.1 Kinds of capability](ch16.md#s16-1)
- [16.2 Capabilities travel down the chain](ch16.md#s16-2)
- [16.3 Effects and capabilities are a pair](ch16.md#s16-3)
- [16.4 Kind, not presence](ch16.md#s16-4)
- [16.5 What the entry point receives](ch16.md#s16-5)

[17 Designing failure](ch17.md)

- [17.1 The boundary and the inside](ch17.md#s17-1)
- [17.2 How to split an error enum](ch17.md#s17-2)
- [17.3 Moving up the layers](ch17.md#s17-3)
- [17.4 Where information may be thrown away](ch17.md#s17-4)
- [17.5 Where panic is acceptable](ch17.md#s17-5)

### Part V — Memory

[18 Regions — where values live, and memory reclaimed all at once](ch18.md)

- [18.1 Three places values live](ch18.md#s18-1)
- [18.2 Opening a region and obtaining space](ch18.md#s18-2)
- [18.3 Receiving a region](ch18.md#s18-3)
- [18.4 Nothing is carried out of a region](ch18.md#s18-4)
- [18.5 One cursor per root](ch18.md#s18-5)
- [18.6 The growing root](ch18.md#s18-6)

[19 Ownership — one party responsible for disposal](ch19.md)

- [19.1 owned and drop](ch19.md#s19-1)
- [19.2 Where branches meet](ch19.md#s19-2)
- [19.3 Release and completion](ch19.md#s19-3)
- [19.4 Not counting on the operating system to clean up](ch19.md#s19-4)
- [19.5 At what strength is memory safety guaranteed?](ch19.md#s19-5)

[20 Allocators and fixed memory](ch20.md)

- [20.1 The three parts of an allocator](ch20.md#s20-1)
- [20.2 Cutting borrowed bytes](ch20.md#s20-2)
- [20.3 Swapping allocators](ch20.md#s20-3)
- [20.4 Default allocators that carve straight from a root](ch20.md#s20-4)
- [20.5 Who sets the size of the fixed window?](ch20.md#s20-5)
- [20.6 Same bits, different reading](ch20.md#s20-6)

### Part VI — Abstraction

[21 Modules — hidden by default](ch21.md)

- [21.1 One file, one module](ch21.md#s21-1)
- [21.2 Importing](ch21.md#s21-2)
- [21.3 There is no search path](ch21.md#s21-3)
- [21.4 When names collide](ch21.md#s21-4)
- [21.5 Top-level order does not matter](ch21.md#s21-5)

[22 Generics — parameters fixed at translation time](ch22.md)

- [22.1 Taking values and types at translation time](ch22.md#s22-1)
- [22.2 An instance per combination](ch22.md#s22-2)
- [22.3 Putting conditions on types](ch22.md#s22-3)
- [22.4 The type carries it, not a value](ch22.md#s22-4)

[23 Traits — one promise kept by many types](ch23.md)

- [23.1 What they are for](ch23.md#s23-1)
- [23.2 Ops attached to types, and method](ch23.md#s23-2)
- [23.3 Traits with several ops](ch23.md#s23-3)
- [23.4 Signatures do not say fn or proc](ch23.md#s23-4)
- [23.5 When a trait is not satisfied](ch23.md#s23-5)
- [23.6 via self — more allocation effects only](ch23.md#s23-6)

[24 pipe — one line for each thing you mean to do](ch24.md)

- [24.1 The same work, two shapes](ch24.md#s24-1)
- [24.2 Stages and terminals](ch24.md#s24-2)
- [24.3 Walking once is the definition](ch24.md#s24-3)
- [24.4 Reading only as much as needed](ch24.md#s24-4)

### Part VII — Concurrency

[25 Actors — living with state, by messages](ch25.md)

- [25.1 Declare, make, talk](ch25.md#s25-1)
- [25.2 Carrying values in messages](ch25.md#s25-2)
- [25.3 Mailboxes — putting in and emptying](ch25.md#s25-3)
- [25.4 Let it crash, then restart](ch25.md#s25-4)
- [25.5 Where actors may be used](ch25.md#s25-5)

[26 Tasks and channels — exchange between bound flows](ch26.md)

- [26.1 Flows are bound to blocks](ch26.md#s26-1)
- [26.2 Channels — containers with an order](ch26.md#s26-2)
- [26.3 Deadlocks you can see from what is written](ch26.md#s26-3)
- [26.4 What does not exist yet](ch26.md#s26-4)

[27 Parallel loops and atomic operations](ch27.md)

- [27.1 Declaring that a loop may be split](ch27.md#s27-1)
- [27.2 The three conditions the processor checks](ch27.md#s27-2)
- [27.3 The combining operation must be associative](ch27.md#s27-3)
- [27.4 Atomic operations](ch27.md#s27-4)
- [27.5 Memory orderings](ch27.md#s27-5)

### Part VIII — The outside world

[28 Input, output and files](ch28.md)

- [28.1 Standard output](ch28.md#s28-1)
- [28.2 Reading a whole file](ch28.md#s28-2)
- [28.3 What is opened is closed](ch28.md#s28-3)
- [28.4 End and failure are different answers](ch28.md#s28-4)
- [28.5 Triggering failure on purpose](ch28.md#s28-5)

[29 Meeting C](ch29.md)

- [29.1 Calling C — mark, right, effects line](ch29.md#s29-1)
- [29.2 Types that cross the boundary](ch29.md#s29-2)
- [29.3 C calls Lowent](ch29.md#s29-3)
- [29.4 Callbacks and ownership](ch29.md#s29-4)

[30 Hardware — registers, interrupts, machine instructions](ch30.md)

- [30.1 The register map](ch30.md#s30-1)
- [30.2 Access is enforced at translation](ch30.md#s30-2)
- [30.3 Interrupt handlers](ch30.md#s30-3)
- [30.4 What a machine supports — build tier](ch30.md#s30-4)
- [30.5 Machine instructions — asm](ch30.md#s30-5)

[31 Building and testing — packages, configuration, tests, cross-checks](ch31.md)

- [31.1 The manifest and the project](ch31.md#s31-1)
- [31.2 Dependencies are pinned by hash](ch31.md#s31-2)
- [31.3 A different program per build — build option and config](ch31.md#s31-3)
- [31.4 Tests](ch31.md#s31-4)
- [31.5 How the compiler is verified](ch31.md#s31-5)
- [31.6 Asking where it is slow](ch31.md#s31-6)

### Part IX — The standard library

[32 A map of the standard library](ch32.md)

- [32.1 Three layers — language, leaf, library](ch32.md#s32-1)
- [32.2 What gets in](ch32.md#s32-2)
- [32.3 The map by layer](ch32.md#s32-3)
- [32.4 File names and module names](ch32.md#s32-4)
- [32.5 Conventions every module follows](ch32.md#s32-5)

[33 Text and encodings — strings, fmt, utf8, codec, hash](ch33.md)

- [33.1 Cut, find, assemble](ch33.md#s33-1)
- [33.2 UTF-8 — no replacement characters](ch33.md#s33-2)
- [33.3 Hex and base64](ch33.md#s33-3)
- [33.4 Hashes — three questions, three answers](ch33.md#s33-4)
- [33.5 Regular expressions — no backtracking](ch33.md#s33-5)

[34 Containers and sorting — sortlib, sortgen, hashmap, vecgen, spsc](ch34.md)

- [34.1 Sorting and finding u64](ch34.md#s34-1)
- [34.2 The type brings the criterion](ch34.md#s34-2)
- [34.3 A hash map over the caller’s slice](ch34.md#s34-3)
- [34.4 A growing generic vector](ch34.md#s34-4)
- [34.5 A ring buffer passing values between flows](ch34.md#s34-5)
- [34.6 Other containers](ch34.md#s34-6)

[35 Storage and handles — pool, shard, budget, wire](ch35.md)

- [35.1 Generational handles](ch35.md#s35-1)
- [35.2 Brands — no mixing pools](ch35.md#s35-2)
- [35.3 Non-overlapping pieces — shard](ch35.md#s35-3)
- [35.4 Bit budgets — budget](ch35.md#s35-4)
- [35.5 Bit fields — wire](ch35.md#s35-5)
- [35.6 Other storage modules](ch35.md#s35-6)

[36 Input/output, networking, time, randomness, cryptography](ch36.md)

- [36.1 Buffered output — forget to flush and translation refuses](ch36.md#s36-1)
- [36.2 Networking — handles are resources](ch36.md#s36-2)
- [36.3 Randomness — reproducible sequences and operating-system entropy](ch36.md#s36-3)
- [36.4 Time — monotonic clocks and wall clocks](ch36.md#s36-4)
- [36.5 The HTTP request parser — its heart is rejection](ch36.md#s36-5)
- [36.6 Cryptography — the order of the stack and the missing top](ch36.md#s36-6)

[37 The terminal — term and tty](ch37.md)

- [37.1 term does not write to the screen](ch37.md#s37-1)
- [37.2 Redrawing only changed cells](ch37.md#s37-2)
- [37.3 The number of cells is not the number of bytes](ch37.md#s37-3)
- [37.4 Reading keys is computation](ch37.md#s37-4)
- [37.5 Raw mode is a capability](ch37.md#s37-5)
- [37.6 What is not built yet](ch37.md#s37-6)

### Part X — Grounds: what has been proven

[38 Why prove](ch38.md)

- [38.1 Tests show existence, proofs show absence](ch38.md#s38-1)
- [38.2 Overlapping three layers](ch38.md#s38-2)
- [38.3 What is proven](ch38.md#s38-3)
- [38.4 Three ways of proving](ch38.md#s38-4)
- [38.5 Promises this part keeps](ch38.md#s38-5)

[39 The mathematical toolkit](ch39.md)

- [39.1 Six tools](ch39.md#s39-1)
- [39.2 Sets and relations](ch39.md#s39-2)
- [39.3 Partial orders — incomparable pairs](ch39.md#s39-3)
- [39.4 Lattices and join — what to pick when combining](ch39.md#s39-4)
- [39.5 Monotone functions and fixed points — why analysis stops](ch39.md#s39-5)
- [39.6 Induction and invariants — dominoes](ch39.md#s39-6)
- [39.7 Abstract interpretation — ranges instead of values](ch39.md#s39-7)
- [39.8 Table of symbols](ch39.md#s39-8)
- [39.9 How the tools interlock](ch39.md#s39-9)

[40 Proofs about numbers — widening, narrowing, division](ch40.md)

- [40.1 Widening is a partial order](ch40.md#s40-1)
- [40.2 Division and remainder](ch40.md#s40-2)
- [40.3 Theorems that range provides](ch40.md#s40-3)
- [40.4 Putting stopping into the model honestly](ch40.md#s40-4)
- [40.5 What is not proven](ch40.md#s40-5)

[41 Proofs about bounds — intervals, relations, row-major addresses](ch41.md)

- [41.1 Computing with intervals](ch41.md#s41-1)
- [41.2 Relations — what intervals cannot do](ch41.md#s41-2)
- [41.3 Four stages](ch41.md#s41-3)
- [41.4 The row-major proof](ch41.md#s41-4)
- [41.5 Trust proofs, but back trust with checks](ch41.md#s41-5)
- [41.6 What is not proven](ch41.md#s41-6)

[42 Proofs about ownership and borrowing](ch42.md)

- [42.1 Two machines and three events](ch42.md#s42-1)
- [42.2 The central theorem](ch42.md#s42-2)
- [42.3 Shapes rejected and shapes accepted](ch42.md#s42-3)
- [42.4 Finding an implementation defect while widening the model](ch42.md#s42-4)
- [42.5 Between model and implementation](ch42.md#s42-5)
- [42.6 What is not proven](ch42.md#s42-6)

[43 Loops and fixed points — proving “however many times”](ch43.md)

- [43.1 It breaks on the second round](ch43.md#s43-1)
- [43.2 At a fixed point, any number of times](ch43.md#s43-2)
- [43.3 Only one dangerous shape](ch43.md#s43-3)
- [43.4 Fresh tokens — one move that removes the naming problem](ch43.md#s43-4)
- [43.5 Nested loops — from function to relation](ch43.md#s43-5)
- [43.6 What is not proven](ch43.md#s43-6)

[44 Proofs about effects — writing “what it can do” into the type](ch44.md)

- [44.1 Effects are sets of atoms](ch44.md#s44-1)
- [44.2 Four rules the processor enforces](ch44.md#s44-2)
- [44.3 Capabilities — the partner of effects](ch44.md#s44-3)
- [44.4 Effect soundness — not circular](ch44.md#s44-4)
- [44.5 Optimisations relying on effects are legitimate](ch44.md#s44-5)
- [44.6 What is not proven](ch44.md#s44-6)

[45 Proofs about races and parallelism — discipline instead of a memory model](ch45.md)

- [45.1 The Bernstein condition — an answer from 1966](ch45.md#s45-1)
- [45.2 Independence means no races, and the condition is necessary](ch45.md#s45-2)
- [45.3 Between actors there is always a message](ch45.md#s45-3)
- [45.4 Containment — where heavy tools are needed](ch45.md#s45-4)
- [45.5 No races is not the same as deterministic](ch45.md#s45-5)
- [45.6 What is not proven](ch45.md#s45-6)

[46 Proofs about weak memory — with the default, you may think sequentially](ch46.md)

- [46.1 Sequential consistency and its counterexample](ch46.md#s46-1)
- [46.2 An execution is a graph](ch46.md#s46-2)
- [46.3 Impossible with the default, possible with relaxed](ch46.md#s46-3)
- [46.4 Exactly that axiom forbids it](ch46.md#s46-4)
- [46.5 Message passing — the weak ones exist for this shape](ch46.md#s46-5)
- [46.6 Over all executions](ch46.md#s46-6)
- [46.7 Weaker orderings add behaviours](ch46.md#s46-7)
- [46.8 Seven tests from the literature](ch46.md#s46-8)
- [46.9 What is not proven](ch46.md#s46-9)

[47 Proofs about locks — where heavy tools are really needed](ch47.md)

- [47.1 The vocabulary of separation logic](ch47.md#s47-1)
- [47.2 There cannot be two tokens](ch47.md#s47-2)
- [47.3 Tools only as needed](ch47.md#s47-3)
- [47.4 rwlock — two proofs of the same sentence](ch47.md#s47-4)
- [47.5 Deadlock freedom and no starvation](ch47.md#s47-5)
- [47.6 A borrowed proof in weak memory — spsc](ch47.md#s47-6)
- [47.7 What is not proven](ch47.md#s47-7)

[48 Proofs about syntax — whichever closer closes it, the same tree](ch48.md)

- [48.1 Algebra and denotation](ch48.md#s48-1)
- [48.2 One statement is a one-statement block](ch48.md#s48-2)
- [48.3 Closers do the same job](ch48.md#s48-3)
- [48.4 What is not proven](ch48.md#s48-4)

[49 Proofs about hashes — calling things by content, not name](ch49.md)

- [49.1 Hash functions and Merkle DAGs](ch49.md#s49-1)
- [49.2 Called by content, not by name](ch49.md#s49-2)
- [49.3 What changes a hash and what does not](ch49.md#s49-3)
- [49.4 What can and cannot be proven](ch49.md#s49-4)
- [49.5 What hashes stop](ch49.md#s49-5)
- [49.6 What is not proven](ch49.md#s49-6)

[50 What is not proven](ch50.md)

- [50.1 The trusted base — what proofs rely on](ch50.md#s50-1)
- [50.2 The largest gap — between model and implementation](ch50.md#s50-2)
- [50.3 What remains — by topic](ch50.md#s50-3)
- [50.4 Not sold as “fully statically safe”](ch50.md#s50-4)
- [50.5 So what may you trust](ch50.md#s50-5)

### Front and back matter

[Appendix A — Words and builtins](sec54.md)

- [ The forty-three words](sec54.md#sx1)
- [ Removed words](sec54.md#sx2)
- [ Frequently used builtins](sec54.md#sx3)
- [ Effects and capabilities](sec54.md#sx4)

[Appendix B — Diagnostics index](sec55.md)

- [ At translation time](sec55.md#sx1)
- [ When execution stops](sec55.md#sx2)

[Appendix C — Common mistakes and how to fix them](sec56.md)

- [ The surface](sec56.md#sx1)
- [ Values and flow](sec56.md#sx2)
- [ Effects, capabilities and memory](sec56.md#sx3)
- [ Abstraction and concurrency](sec56.md#sx4)

[Appendix D — Grammar summary](sec57.md)

- [ Clause order of an op head](sec57.md#sx1)
- [ Declarations](sec57.md#sx2)
- [ Statements](sec57.md#sx3)
- [ Patterns](sec57.md#sx4)
- [ Expressions](sec57.md#sx5)
- [ Heads and closers](sec57.md#sx6)

[Appendix E — Standard library modules](sec58.md)

[strings — string view operations](sec59.md)

- [ Design and boundaries](sec59.md#sx1)
- [ Ops at a glance](sec59.md#sx2)
- [ Ops in detail](sec59.md#sx3)
- [ Using it](sec59.md#sx4)
- [ Counter-examples](sec59.md#sx5)
- [ Cautions](sec59.md#sx6)

[strbuf — owned string buffer and null-terminated cstr](sec60.md)

- [ Design and boundaries](sec60.md#sx1)
- [ Ops at a glance](sec60.md#sx2)
- [ Ops in detail](sec60.md#sx3)
- [ Using it](sec60.md#sx4)
- [ Counter-examples](sec60.md#sx5)
- [ Cautions](sec60.md#sx6)

[fmt — formatting assembled into the caller’s buffer](sec61.md)

- [ Design and boundaries](sec61.md#sx1)
- [ Ops at a glance](sec61.md#sx2)
- [ Ops in detail](sec61.md#sx3)
- [ Using it](sec61.md#sx4)
- [ Counter-examples](sec61.md#sx5)
- [ Cautions](sec61.md#sx6)

[utf8 — UTF-8 code point iteration and validation](sec62.md)

- [ Design and boundaries](sec62.md#sx1)
- [ Ops at a glance](sec62.md#sx2)
- [ Ops in detail](sec62.md#sx3)
- [ Using it](sec62.md#sx4)
- [ Counter-examples](sec62.md#sx5)
- [ Cautions](sec62.md#sx6)

[utf16 — UTF-16 surrogate arithmetic](sec63.md)

- [ Design and boundaries](sec63.md#sx1)
- [ Ops at a glance](sec63.md#sx2)
- [ Ops in detail](sec63.md#sx3)
- [ Using it](sec63.md#sx4)
- [ Counter-examples](sec63.md#sx5)
- [ Cautions](sec63.md#sx6)

[unicode — Unicode property tables](sec64.md)

- [ Design and boundaries](sec64.md#sx1)
- [ Ops at a glance](sec64.md#sx2)
- [ Ops in detail](sec64.md#sx3)
- [ Using it](sec64.md#sx4)
- [ Counter-examples](sec64.md#sx5)
- [ Cautions](sec64.md#sx6)

[codec — hex and base64](sec65.md)

- [ Design and boundaries](sec65.md#sx1)
- [ Ops at a glance](sec65.md#sx2)
- [ Ops in detail](sec65.md#sx3)
- [ Using it](sec65.md#sx4)
- [ Counter-examples](sec65.md#sx5)
- [ Cautions](sec65.md#sx6)

[regex — Pike VM regular expressions without backtracking](sec66.md)

- [ Design and boundaries](sec66.md#sx1)
- [ Data structures](sec66.md#sx2)
- [ Ops at a glance](sec66.md#sx3)
- [ Ops in detail](sec66.md#sx4)
- [ Using it](sec66.md#sx5)
- [ Counter-examples](sec66.md#sx6)
- [ Cautions](sec66.md#sx7)

[term — terminal renderer (the pure half)](sec67.md)

[sortlib — in-place quicksort](sec68.md)

[sortgen — generic sorting (the type brings the comparison)](sec69.md)

[searchlib — binary search on sorted slices](sec70.md)

[hashmap — u64 → u64 hash map](sec71.md)

[strmap — string-keyed hash map](sec72.md)

[vecs — growable byte vectors with caller-held buffers](sec73.md)

[spsc — lock-free SPSC ring buffer](sec74.md)

[hash — hashing (hash map slots · corruption detection · SHA-256)](sec75.md)

[math — floating-point maths](sec76.md)

[random — random numbers (reproducible sequences · OS entropy)](sec77.md)

[hmac — HMAC-SHA256 and HKDF](sec78.md)

[chacha — ChaCha20 stream cipher](sec79.md)

[poly — Poly1305 one-time authenticator](sec80.md)

[aead — ChaCha20-Poly1305 seal and open](sec81.md)

[x25519 — key agreement on a curve](sec82.md)

[aes — the AES-128 block cipher](sec83.md)

[gcm — AES-128-GCM authenticated encryption](sec84.md)

[bigint — big-number modular arithmetic](sec85.md)

[rsa — RSASSA-PSS verification](sec86.md)

[p256 — the NIST P-256 curve and ECDSA verification](sec87.md)

[ecdsa — ECDSA P-256 signing](sec88.md)

[ed25519 — Ed25519 signature verification](sec89.md)

[der — minimal DER parser](sec90.md)

[pem — unwrapping PEM envelopes](sec91.md)

[tls13 — the computational parts of TLS 1.3](sec92.md)

[tlssrv — the TLS 1.3 server handshake](sec93.md)

[http — HTTP/1.1 request parser](sec94.md)

[soa — an SoA layout trial: one array per field](sec95.md)

[allocs — allocator traits, bumps and default allocators](sec96.md)

[pool — generational-handle block pool](sec97.md)

[shard — access units that split a store](sec98.md)

[budget — handle bit budgets and generation wraparound](sec99.md)

[wire — dividing one word into fields](sec100.md)

[flags — named on/off settings in one word](sec101.md)

[segarena — fixed-size segment arena](sec102.md)

[pagecache — page ids and pinning cursors](sec103.md)

[growvec — self-growing byte vector](sec104.md)

[vecgen — generic self-growing vector vec t a](sec105.md)

[mapgen — generic hash map table k v](sec106.md)

[nodelist — fixed intrusive lists](sec107.md)

[segview — cursors, total length and coalescing for segment views](sec108.md)

[lifemode · lifeatom — when a value ends](sec109.md)

[io — stream reading over slices](sec110.md)

[outbuf — buffered output where forgetting to flush is a compile error](sec111.md)

[files — file and directory streams where forgetting to close is a compile error](sec112.md)

[tty — terminal input](sec113.md)

[net — sockets (TCP loopback · in-process pairs)](sec114.md)

[clock — time and deadlines](sec115.md)

[Index](sec116.md)

---

[← Prev](README.md) · [Contents](README.md)
