# lowentc — the Lowent compiler (`impl/`)

C23 implementation of Lowent, built on the vendored
[`proven_c_lib`](vendor/proven/VENDORED.md) (original untouched; copied, never moved).

> ★★★ **This is a COMPILER.** It was an interpreter once — the old tree-walking evaluator
> (`low_eval.c`, 1422 lines) and the **second, undocumented language** it ran were
> **deleted** (DECISION-0012). **The language is one.** The artefact was called `lowmini`
> for a long time after that; **a name that outlives its meaning is a lie**, so it is now
> **`lowentc`**.

## What it does (measured 2026-07-24)

```
lex → point-closure CST → arity normalisation (the TREE) → monomorphisation
    → checks (contracts · effects · ownership · visibility · tiers · capabilities · FFI)
    → its own stack IR
    → ① a VM that runs it        ② a C back end that emits native code
```

★★★ **Two back ends are an ORACLE.** `scripts/diff-sweep.py` runs **706 ops** on both,
with randomised boundary arguments, and compares byte for byte. A disagreement is a
**compiler bug**, not a test failure. (It has caught: a `try` that leaked the call-depth
ledger, a fast path that dropped a contract check, a bitset lowered as an integer add.)

| | measured |
|---|---|
| golden checks | **STATUS.md 가 센다**(`golden_checks`) — `sh tests/golden.sh </dev/null` |
| unit tests | **ALL PASS (0 failures)** · **450** passing checks (`make test | grep -c '[PASS]'`) |
| keywords | **43** (closed vocabulary — `scripts/check-vocab.py`) |
| builtin ops | **170** (closed — `scripts/check-builtins.py` compares the tool against SPEC appendix P) |
| typed lowering | ☞ **not re-counted** — the old figure (306/353) cannot be reproduced: `--why-slow` gives 467/648 over `tests/*.low` and 583/859 with `prog/`, so the denominator never matched. A number nobody can re-derive is not a measurement; `--why-slow` still says, per op, why an op is not lowered |
| differential sweep | **1,434 ops · 11,400 argument vectors**, 0 divergences (`python3 ../scripts/diff-sweep.py` — 재측정 2026-08-13; 픽스처 단위 병렬로 6분 20초 → 1분 45초) |
| fixtures | **211** (`find tests -name '*.low' | wc -l`; repo-wide `.low` is **239**) |
| source | **32,673** lines C23 (`find impl/src -name '*.[ch]' | xargs wc -l`) — 문서(SPEC+RFC)는 **34,153** 줄(`cat SPEC-*.md docs/rfc/*.md | wc -l`)로 **여전히 코드보다 많다** |

> 이 표는 **2026-08-02 재측정**이다. 여러 줄이 오래 낡아 있었다(픽스처 194→211 · 소스
> 27,895→32,673 · 차등 스윕 706 ops→1,014). `scripts/check-docs-fresh.py` 는 골든·키워드 수만
> 실측과 대조하므로 **나머지는 아무도 안 지킨다** — 그래서 각 줄에 **다시 세는 명령**을 적어 둔다.

**Typed lowering — RFC-0109 단계 1** (2026-09-03, WO-0178): 빠른 경로 안의 슬라이스는 **원소 단위**다 — 파라미터 슬롯이 처음부터 원소 폭이고 같은 폭의 `view.array` 는 항등(방출 0). 바이트→원소 검사는 태그 어댑터 경계에서만 난다. 대조 스위치 `--no-elemsl`. 수는 `docs/bench/emit-path.md` §②′(churn 103 → 88.5 ns, 명령 −15 %).

**Typed lowering** (DECISION-0018): an op whose types are known is emitted as *natural C*
(`long long`, plain arrays, a `(tag, value)` pair for option/result) instead of a tagged
stack machine. `--no-fast` is the contrast switch — without one, "it got faster" is an
unmeasured claim.

**Re-measured 2026-08-02** (`tests/vm_structfast.low`, op `many` — a `make` + two field
reads per iteration; `--emit-c` → `cc -O2`; process start-up measured at `N=1` and
subtracted; this build server):

| | ns / iteration | how |
|---|---|---|
| typed lowering | **4.98** | N=4,000 × 2,000 runs |
| `--no-fast` (tagged) | **218.27** | N=4,000 × 2,000 runs |
| **ratio** | **43.8×** | same N — the fair comparison |
| typed lowering, at scale | **0.45** | N=20M × 5; linear in N (10M→5.9ms, 80M→36.4ms), so the loop is real, not folded to a closed form |

☞ **The older figure in this file — 824ms → 12ms (69×) — is withdrawn: it cannot be
reproduced.** At 218 ns/iteration, 824ms would be ~3.8M iterations, and **the tagged path
cannot reach that**: it dies at `N≈8000` with `panic: record pool` (the finite 64-record
pool — the fixture's own comment says a struct built in a loop kills it). A contrast
switch that cannot run the workload is not a contrast at that size, so the ratio above is
quoted at `N=4000`, the largest size the control survives. **A number nobody can re-derive
is not a measurement** — the same rule already applied to the typed-lowering percentage below.

☞ And that ceiling is the more important half of the result: typed lowering is not only
faster here, it is **the only path that runs this program at all** past a few thousand
iterations.

**No LLVM, no external IR.** The C back end emits C; `cc -O2` does instruction selection.
The optimisations that are *ours* are the ones a C compiler cannot derive: contract-proven
check elision, monomorphisation, comptime folding, and the typed lowering above.

## CLI

```sh
./build/lowentc --check FILE.low        # checks only
./build/lowentc --run OP FILE.low ARGS  # run on the VM
./build/lowentc --emit-c FILE.low       # emit C (native build)
./build/lowentc --emit-h FILE.low       # emit lowent.h  (C calls US — RFC-0063)
./build/lowentc --no-main --emit-c …    # emit as a LIBRARY (the C program owns main)
./build/lowentc --why-slow FILE.low     # which ops are still on the tagged path, and WHY
./build/lowentc --no-fast --emit-c …    # contrast switch: turn typed lowering off
./build/lowentc --config FILE           # build options (RFC-0036)
./build/lowentc --lock FILE             # pin dependencies by CONTENT HASH (RFC-0032)
./build/lowentc --fmt | --ir | --cst | -t | --doc | --test | --ops
```

## Build

```sh
make            # debug build            → build/lowentc
make asan       # + ASan/UBSan
make test       # build + run smoke tests
make run ARGS="tests/sample.low"
make clean
```

Requires a C23 compiler (`gcc -std=c23`, or clang). No external deps — proven is
vendored under `vendor/proven` (original untouched; copied, never moved).

```sh
./build/lowentc [-t] <file.low>            # -t also dumps the token stream
./build/lowentc --doc <file.low>           # lowdoc: render Markdown API docs to stdout
./build/lowentc --doc-out DIR <file.low>   # …write DIR/<module>.md + DIR/llms.txt
```

### `--doc` (lowdoc prototype)

Renders Markdown API docs for every `calcop`/`procop`, straight off the shared
front-end CST — no separate doc parser (the Doxygen contrast). Emitted in the RFC-0033
D12 canonical order: title → Summary → Description → Signature → Contract → Effects →
Examples (absent clauses omit their section). See `tests/doc.low`.

- **Fields** from the MVP clause syntax: `input`/`output`/`effects`/`requires`/`ensures`/
  `errors`/`tests`. `requires`/`ensures` render their **grade badge** `[static|debug|assume]`
  (RFC-0008 §6.3, cost-visible); a `calc op` is tagged `· pure`.
- **Prose** from either a `lowdoc` clause (`lowdoc "…"` / `lowdoc text TERM … TERM`) or,
  failing that, the **adjacent `rem` comments** rescanned from source (comments are lexed
  away, so lowdoc reads them back). First line → Summary, rest → Description.
- **`--doc-out DIR`** writes `DIR/<module>.md` per module plus `DIR/llms.txt` — the curated
  root index (Answer.AI `llms.txt` convention, RFC-0014 §6.6): links + one-line summaries.
- **S4 verdict injected**: lowdoc runs the four S4 passes (effect / type / contract / region)
  and adds a **Checks** section per op — `✓ … checks pass` when verified, or the actual
  violations (e.g. `⚠ \`E-EFFECT-CALC\` — … (line N)`) attributed to that op by line. The
  doc reflects the *analyzer*, not just the declared clauses.
- **Declared vs inferred effects**: the Effects section shows both the declared clause and
  the effect set *inferred* from the body (io builtins + transitive `procop` effects), and
  flags any inferred effect missing from the declaration.
- **`--doc-out DIR` writes the full set**: `<module>.md` (human doc) · `<module>.lowctx`
  (terse card — `summary` inline, `details h:XXXXXXXX` with the body **externalised** to
  `details/h-XXXXXXXX.md` by content hash) · `llms.txt` (index) · `llms-full.txt` (bundle).

## Layout

```
src/
  low_token.{h,c}   token kinds, keyword table
  low_diag.h        diagnostics (errors-as-values)
  low_lex.{h,c}     L0 lexer  (point-closure tokenizer)
  low_cst.{h,c}     L1 parser (point-closure CST) + tree dump
  main.c            CLI driver
tests/
  run_tests.c       smoke tests (make test)
  sample.low        v0.5 surface sample
vendor/proven/      vendored proven_c_lib (MIT)
```

## proven conventions we follow (per the goal directive)

- **Memory = proven arena** (region/bump, bulk-free): CST nodes come from a
  `proven_arena_t` — mini's §8.1 memory model, literally. Dynamic-growth containers
  (token/diag/form arrays) use the heap allocator that backs the arena.
- **Strings = `proven_u8str_view_t`**: tokens are zero-copy length+ptr slices into the
  source buffer (never NUL-terminated substrings).
- **Errors are values**: result structs `{ err, value }` and diagnostic arrays; no
  exceptions, no errno. `[[nodiscard]]` on fallible calls.
- **Naming**: `low_` prefix (shared front-end), `snake_case`, `_t` typedefs — matching
  proven's house style (`proven_`).

## Status — S0–S5 + V1(verifier+fuzz) + structs/views/strings + portable SIMD (RFC-0051 D1 core queue complete)

### 네 개의 오라클 — 모두 **기대 출력 없이** 판정한다 (실측치)

| 오라클 | 규모 | 무엇이 실패인가 |
|---|---|---|
| 유닛 테스트 | **449** (`make test | grep -c '[PASS]'`; 400-seed soundness fuzz · 200-seed layout fuzz · 3 exhaustive model checks · 1-edit mutation metric 포함) ☞ 옛 표기 **525** 는 **어떤 세는 법으로도 재현되지 않았다**(현재 `check(` 호출은 444) — 그래서 **세는 법을 함께 적는다** | 손으로 적은 기대값 |
| 골든 게이트 | **STATUS.md 의 `golden_checks`** | 픽스처가 `--check` 초록이어야 하고, **의도적 음성은 반드시 위반해야 한다** |
| **계약 = 오라클** | **649 op** 유도 (건너뛴 op **0**) · 케이스 수는 ☞ **다시 안 셌다**(세는 법을 정하는 것이 후속) | 계약 **안**이면 트랩하면 안 되고, **밖**이면 트랩해야 한다 — **기대 출력이 필요 없다** |
| **차등 퍼저** (VM ↔ 네이티브) | **706 op · 6108 인자 벡터** (도달 못 하는 op **0**) | 두 실행이 **다른 답**을 내면 컴파일러 버그. 그리고 **VM 의 자기 고발**(E-VM-ANALYSIS)은 **언제나** 실패 — 네이티브는 그 검사를 **진짜로 지운다** |
| **쪼개기 오라클** (R2 · DET-1) | `parallel` op 마다 **K=2·3·4·n × 정순/역순**, 그리고 **진짜 pthread** | 쪼갠 결과가 순차와 **비트 동일**해야 한다. 다르면 `parallel` 선언이 **거짓말**이거나(쓰기가 겹친다) 리덕션이 **결합적이지 않다**(DET-3) |

Coq: **9 파일 Qed**(공리 0 — RC11 약한 메모리 모델 포함). 픽스처 **70개**.
(Counts are measured, not incremented — `make test | grep -c '\[PASS\]'`.) **SPEC-MVP §8 gate: BOTH examples now RUN on
the VM** — A (parse_header): ok path renders the record + all three contract
errors fire (`tests/vm_parse_header.low`); B (graph_reachable, spec body
verbatim): stack + pop-into + visited-bitset + for over adjacency slices,
cycles terminate (`tests/vm_graph.low`); both stay `--check` green; and **both
compile to native via the C backend** (`--emit-c` → cc) with output
byte-identical to the VM (golden-diffed) — §8's "컴파일·실행" path is closed.
`make golden`: behavior + fmt round-trip + MVP parse + 4 static checks +
stack-IR VM runs + **C-backend ≡ VM diffs**. ASan/UBSan clean, 0 warnings.

### S5a/S5b — stack IR + content addressing + value VM (`--ir`, `--run`)

`low_ir` lowers MVP `calcop`/`procop` bodies to the SPEC-013 §14.2 **stack IR**
(operand stack + de Bruijn locals) and `--run OP <file> [ints…]` executes them on a
tagged-value stack interpreter — `fact`/`fib` (while/`expr` islands/recursion,
`tests/vm.low`) and **§8 example A** (guard/`error <variant>`/`ok`/`try`/`make` +
byte slices `len`/`index`/`subslice`, `tests/vm_parse_header.low`) run end-to-end.
CLI ints map to args; a single slice-typed param takes all ints as its bytes.
Bounds violations, div-zero, type confusions → `E-VM-*` runtime diags.

Content addressing per SPEC-011 §12.3 on a self-contained, reference-verified
**BLAKE3-256** (`low_blake3`; official empty vector + cross-chunk vector in tests):
**iface-hash** (kind + param/return types + effects) vs **def-hash** — now a true
**SCC fixed-point Merkle hash**: call sites carry the callee def-hash
(topological over the SCC DAG); within an SCC, members are canonically ordered by
a preliminary name-free hash and call sites carry the member index. Structurally
identical defs hash equal (dedup; names are metadata); a callee **body edit
propagates into caller def-hashes** while iface-hash stays the relink-only
boundary (both golden/unit-locked, incl. mutual recursion determinism).

S5c adds containers: **stack** (`stack_new R capacity n` — the region argument is
erased at lowering, VM pools stand in for arenas; `push`; `while pop … into N do`
pop-as-condition), **bitset** (`bitset_new` harness constructor, polymorphic `add`
= set insertion per RFC-0010 §6.7.1, `contains`, `count`), and **`for x in
<slice> do`** (hidden de Bruijn iterator slots). Reader-split statements are
reassembled at block level (guard cond…else, `let N <type> . be value`).

S5d adds the **C backend** (`low_cbe`, `--emit-c`): one self-contained C
translation unit — a value runtime mirroring the VM (tagged values, pools,
panic-on-violation for bounds/div0), one function per lowered def (straight-line
stack code, branches → gotos, calls → direct C calls), and a `main` whose CLI
and rendering are byte-identical to `--run`, so native and VM outputs are
diffable (golden does exactly that for fact/fib and §8 A/B).

### S-impl-4 V1 (first slice) — dynamic verifier: runtime references (RFC-0017)

The VM now models **references**: `ref x`/`mut_ref x` on locals/params, `deref`,
and G7 write-through (`set <mut_ref-typed place> v`). Every call frame gets a
fresh **generation**; a reference carries (frame, generation) and every access
revalidates them — Miri-style. Catches: **E-VM-DANGLING** (use-after-return —
the dynamic counterpart of static E-ESCAPE; the twin fixture
`tests/vm_dangling.low` is rejected statically AND trapped dynamically when
forced to run: static ≡ dynamic, the V1 purpose) and **E-VM-READONLY** (write
through a shared ref). The same shared-ref write is now also caught statically:
the type checker tracks reference kinds (`E-TYPE-REF`; write-through values are
checked against the referent type). Cross-frame `mut_ref` write-through runs
green (`tests/vm_refs.low`). References are not in the C backend yet (emitted
code skips such defs with a comment).

**Dynamic EXCL (borrow-stack-lite, Stacked-Borrows 축소판)**: each borrowed slot
keeps a stack of borrow tags; conflicting accesses (owner read/write, a write
through an older borrow) invalidate newer borrows, and using an invalidated
borrow is `E-VM-EXCL` (`tests/vm_excl.low`). Sequential reborrow patterns stay
green. **First V1 finding, same day**: the twin fixture exposed that static
EXCL missed reader-split bindings (`var a mut_ref u64 . be mut_ref x .`)
entirely — fixed in `low_region` (unit+golden locked).

**Static EXCL v2 — last-use liveness (NLL-style).** Driven by the verifier
findings, static EXCL moved from lexical scope to intervals: a borrow lives
from creation to its last textual use; overlapping borrows (either mut) and
owner accesses inside a mut borrow's interval are E-EXCL; `while` bodies are
handled span-wise (a borrow crossing into a loop conflicts with owner accesses
inside it regardless of textual order — iteration wrap-around). Precision
gains: UNUSED overlapping mut borrows are now green (NLL-style); the
`owner_read` case that lexical statics missed is now rejected — every vm_excl
fixture is a full static≡dynamic twin.

**Fuzzing (V1 completion).** `make test` runs a deterministic 400-seed fuzzer:
random MVP-core programs (ints, borrows, deref/write-through, if, bounded
while, nesting) → 4 static passes → statically-green programs execute on the
verifying VM. Property: **0 soundness diagnostics on green programs** — any hit
is a static false negative. Current run: 357/400 green, 0 violations, ASan
clean. (Also fixed en route: VM/C-backend integer arithmetic now wraps via
unsigned ops — the C UB the fuzzer's mul chains would have hit; the MVP's
checked-overflow trap semantics remain future work.)

### First-class structs (records readable, all access surfaces)

`make` records are now readable end-to-end: canonical prefix **`field s name`**
(G1), infix **`s to name` / `name in s`** (RFC-0047), and glued **`s.name`**
(rev.f §2.8) all lower to one `field` IR word — proven by hashing: `field p a`
≡ `p.a` and `p to b` ≡ `b in p` yield **identical def-hashes** (surface = view,
hash-invariant), while different field names differ. Nested records and mixed
chains work (`o.q.b + (a in q in o) + (o to tag)`, `tests/vm_struct.low`), the
C backend emits `lw_field` (golden-diffed vs the VM), and field names are
structural in the def-hash. En route fix: nested `make` literals corrupted the
make-site table (index reserved before lowering children now).

### view / try_view (RFC-0025 §6.6-6.7 first slice)

Struct declarations now carry a **layout**: field sizes (u8…u64/usize), optional
**`be`** field endianness, `layout packed .` (cumulative offsets) vs default
native (size-aligned offsets + tail padding, RFC-0050 §6.6.1). **`view T b`**
reinterprets a byte slice zero-copy (length-checked; short → panic `E-VM-VIEW`,
the requires-violation analog); **`try_view T b`** returns `some(view)`/`none`
(consumed via `is_some`/`some_value` prelude ops). Field access on a view reads
bytes at the layout offset with endian decode — all three access surfaces work
on views too. The wire-header pattern runs end-to-end on VM and C backend
(golden-diffed): `tests/vm_view.low` — magic `0xDEADBEEF` be-decode, u16 be →
258, none-path guard, short-slice panic. Native (non-packed) layouts are unit-tested (size-aligned offsets, tail padding) and a **200-seed layout fuzzer** with an independently reimplemented layout oracle checks the round-trip + wire length over random packed/native·be/le structs. **The §6.7 round-trip is closed**: `encode T v` serializes a native record (or re-normalizes a view) through the layout — `view(encode(make …))` preserves every field, wire bytes render as `[222 173 190 239 1 2 7]`, VM ≡ C backend (`lw_encode`). Layouts are structural in def-hashes
(packed flag + field names/sizes/endian).

**view_array / strings / view-EXCL** (`tests/vm_varray.low`, `tests/vm_stale.low`):
`view_array T b` reinterprets bytes as a typed scalar array (native/le decode,
length-multiple checked) — `len` = element count and `index` decodes, so
**`for x in view_array u16 b do` works unchanged**. String literals are byte
slices (zero-copy into the source): `len "hello"` = 5, `index`/`for-in` work,
literal bytes are structural in def-hashes, and the C backend emits a string
table — both golden-diffed vs the VM. A **view is a shared borrow of its source
slice** in static EXCL v2: rebinding the slice inside a live view's interval is
rejected (`E-EXCL`, twin fixture), read-then-rebind stays green. Not yet:
alignment checks beyond length (VM slices are byte-addressed), encode of
non-integer fields, dynamic view invalidation (unreachable in statically-green
programs — slices are otherwise immutable in the core).

### Portable SIMD (RFC-0040 first slice — the last RFC-0051 D1 core item)

Lanes are **comptime, taken from the binding's `vec t n` type** (`var va vec u32
4 . be load xs 0 .`) — the lowering threads that context into `splat`/`load`.
Arithmetic and comparisons **lift** onto vectors with no new vocabulary
(`mul va vb` = lanewise; `gt v lim` → `mask`); only vector-specific ops are new:
`select m a b`, `reduce_add/mul/min/max`, `any`/`all` on masks, and mask
logic lifts (`and`/`or`/`not`). Semantics on the value VM (integer lanes,
width-masked wrap): dot product = `reduce_add mul va vb` (70), clamp =
compare→mask→`select` (30), bounds-checked loads panic past the array
(`tests/vm_vec.low`, golden). Not in the C backend yet (defs with vector words
were initially skipped — now emitted: refs lower to real C pointers (release semantics; the frame-generation verifier stays VM-side by design) and vectors mirror the VM lane pool, both golden-diffed); target intrinsics stay opt-in gated per RFC-0051 D2.

### Float core + runtime `cast` (G2) · canonical tree hashing · fuzz coverage

Floats are values now: IEEE-754 double literals/arith (`1.0/0.0` = `inf`, no
trap), compares, `neg`; int×float mixing is rejected (no implicit coercion);
`cast` runs at runtime (`cast f64 n`, `cast u32 7.9` truncates+masks) — VM ≡ C
backend (`%g` render parity). Floats reach **layouts and lanes** too: `f32`/`f64`
struct fields encode/view round-trip with field endianness (`temp f32 be`),
`view_array f32` decodes typed float arrays, and `vec f32 n` carries float lanes
through splat/load/arith/compare/select/reduce — an f32 dot product
(`reduce_add mul va vb`) runs on both the VM and the native backend, byte-identical.
The 200-seed layout fuzzer now mixes float fields into its random structs
(exactly-representable values, `cast u64` recovered) — 0 violations against the
independent oracle. Def-hashes are now computed over the **canonical
tree encoding** (SPEC-011 §12.2): expressions reconstructed from the word
stream by stack simulation, branch targets normalized to label ids (flat-stream
fallback kept as a never-expected safety) — all hash properties (dedup, surface
invariance, SCC Merkle, determinism) re-verified. The soundness fuzzer also
generates strings, record literals+field reads, inline encode→view round-trips
and u8-vector loads (380/400 green, 0 violations). V2 proof track:
`docs/proofs/lambda-lowent-core-agreement.md` — the implemented fragment's
static interval-EXCL ≡ dynamic borrow-stack agreement theorem (straight-line
proven, loop-span sketched) with the two defects V2 actually found on record.

### RFC-0052 S1 — the ⊑ lattice, ported from Coq (adopted 2026-07-12, DECISION-0007)

`low_typecheck.c` now judges *operands*, not just boundaries. The safe-widening
lattice `ty_sub` is a line-by-line port of `sub` in `docs/proofs/coq/NumericLattice.v`,
whose `sub_preserves` theorem (`Qed`) is the entire safety argument for allowing
implicit widening: **τ ⊑ τ' means every value of τ is representable in τ'.**
Binary numeric ops require ⊑-**comparable** operands and yield the `join` — which
`join_is_an_operand` proves is always one of the two, never an invented wider type.
`expr` islands no longer erase width/sign (that erasure was the root cause of the
old 54% silent-acceptance rate). `usize`/`isize` are distinct nominal types (D11);
`bool` cannot be a cast target (D12); `int + float` is `E-TYPE-MIX` (D8).

**Measured effect** (the 1-edit mutation metric, `make test`):

```
                  before → after
type width         15%  →  87%      (the residue is widening a declaration — legitimately safe)
type sign          24%  → 100%
type kind         100%  → 100%
TYPE combined      46%  →  95%      silent acceptance 54% → 5%
controls (op/lit)   0%  →   0%      unchanged — the rule is not over-rejecting
```

Zero false positives: every fixture and golden check still passes untouched.

### RFC-0052 S2 — sign-faithful runtime

Integer values now carry their **declared** width and signedness through lowering:
a type shadow stack in `ir_emit` (driven by `ir_word_arity`, the same table the
canonical hash uses) computes the operand type of every arithmetic/compare word and
encodes it into the instruction (`IR_TY_KNOWN`/`IR_TY_SIGNED` + width). The VM and the
C backend both read it, so `div`, `mod` and the ordered compares follow the type the
programmer wrote. `MIN / -1` now traps (`E-VM-OVERFLOW`) — the second of the two
division failure points the Coq proof pins down.

The bug this fixes was real and silent:

```
  u64 big = 2^63 ;  lt big 1        was 1 (!)  → now 0     — 2^63 is not less than 1
  u64 big = 2^63 ;  div big 2       was −2^62  → now 2^62  — the quotient was negative
  i32 a   = −7   ;  div a 2         −3 (unchanged) — signed types still behave signed
```

Untracked positions (a value whose type the lowering cannot see) fall back to the old
signed path, so this is a strict improvement, never a regression.

### RFC-0052 S3 — width-faithful arithmetic, and policy chosen by name

Arithmetic now happens at the **declared width**, and the disposition is **trap by
default** — build mode has nothing to do with it (P3). The policy is picked by the
op's *name*, so the source says what happens on overflow:

```
u8 a = 255                             u32 x = 300
  add a 1        → E-VM-OVERFLOW         narrow u8 x       → E-VM-CAST   (was 44, silently)
  wrap_add a 1   → 0                     narrow_wrap u8 x  → 44
  sat_add a 1    → 255                   narrow_sat  u8 x  → 255
                                         widen u32 (u8)    → total, cannot fail
```

`widen` is a total function — `widen_never_fails` in Coq says so. Overflow is detected
with `__builtin_*_overflow` (u64 included, via unsigned carry), so the check is exact
rather than a range approximation. Positions whose type the lowering cannot see keep
the old 64-bit wrap, so this is a strict improvement with no regressions — every
fixture and golden check still passes. Trapping programs are golden-checked as
"both sides refuse" (the VM prints a diagnostic, the native binary panics — not
byte-comparable, and pretending otherwise would be a fake parity check).

Still to come: `chk_*` (overflow as a value), `nonzero τ` + `div_nz`, `narrow_try`,
and RFC-0053's interval analysis, which is what *removes* these checks when the
contract proves the range.

### RFC-0052 S4 — float width is precision, and the builtins stop lying

`f32` arithmetic is now performed **at f32 precision** (D9). C's `FLT_EVAL_METHOD`
lets a compiler compute `float` expressions in `double` behind your back; we refuse
that explicitly. The test is sharp: `2^24 + 1` is not representable in f32, so

```
f32 a = 16777216.0 ;  eq (add a 1.0) a   →  true    (rounded at f32 — correct)
f64 a = 16777216.0 ;  eq (add a 1.0) a   →  false   (the bit survives)
```

If the f32 sum leaked into a double, the first would be false. It isn't — in the VM
and in the C backend, byte-identically.

`sqrt` and `fmod` are **float-only** now (D14): `sqrt x` on an integer was quietly
promoting to float, which contradicted G2's "no implicit coercion" in the one place
nobody looked. Write `sqrt (cast f64 x)`. Float literals are comptime-untyped (D3),
so `f32 + 1.0` stays f32 rather than being dragged up to f64.

**With S4, all six measured defects from RFC-0052 §2 are closed.**

### RFC-0053 — interval analysis: the contract buys the performance back

The traps S3 introduced are only affordable if something *removes* them. That
something is a forward interval analysis over the lowered IR, and its most important
input is the **`requires` clause** — the point where a contract stops being a runtime
cost and becomes a static fact:

```
calcop bare   input a u8 . output u8 .                     do return add a 1 . end
calcop proven input a u8 . output u8 . requires le a 200 . do return add a 1 . end

-- overflow checks (interval analysis) --
   1 / 3 removed  (33%)
   bare: 1 check(s) remain — add a `requires` to prove the range
```

`a ∈ [0,200]` plus `1` gives `[1,201] ⊆ u8`, so `proven`'s check is provably dead and
the C backend emits raw arithmetic there. `bare` keeps its check — the analysis is
conservative, never optimistic. **Residual checks are printed, not hidden** (RFC-0053
§6.4): a silent performance model would itself be entropy.

**The fuzzer validates the analysis.** The VM keeps the check even at a "proven" site;
if it ever fires there, that is not an overflow — it is a *false removal*, and the VM
says so (`E-VM-ANALYSIS`). The 400-seed soundness fuzzer counts that as a violation, so
an unsound interval rule is caught the moment it is written. (Overflow and div0 traps
are *not* soundness violations — the static checker never promised they wouldn't happen.)

### RFC-0053 E4 — one analysis, three pillars

The same interval pass now discharges all three of SPARK's AoRTE obligations:

```
requires ge b 1 .   →  div a b       the div0 check is gone
requires le x 200 . →  narrow u8 x   the narrowing trap is gone
requires le a 200 . →  add a 1       the overflow check is gone   (already)

-- runtime checks (interval analysis: overflow · division · narrowing) --
   2 / 4 removed  (50%)
   dbare: 1 check(s) remain — add a `requires` to prove the range
```

The Coq theorems pin down exactly what has to be discharged for division:
`div_unsigned_total` says the *only* unsigned failure is a zero divisor, and
`div_signed_failure_is_only_min_neg1` adds `MIN / -1` for signed — so the analysis
checks precisely those two, and nothing else. A proof told the analysis what its job is.

`narrow_try : τ -> option τ'` rounds out the narrowing family: trap / wrap / saturate /
value. Every lossy conversion now names its policy.

### RFC-0053 E5 — error visible (P2′): the accurate one is the default

Numerical error has the same shape as a hidden cost: it isn't in the source, it
accumulates, and it surfaces late. So it gets the same discipline — **the op's name
carries its error bound**, and the *accurate* variant is the default:

```
sum      : |result − Σxᵢ| ≤ 2ε·|Σ|xᵢ||   (Kahan-Babuška-Neumaier — independent of n)   ← default
sum_fast : |result − Σxᵢ| ≤ n·ε·Σ|xᵢ|    (naive — error grows with the term count)     ← opt-in
```

The difference is not academic. Summing `[1e16, 1.0, −1e16]`:

```
sum_fast → 0     the 1 is rounded away (1e16 + 1 = 1e16 in f64)
sum      → 1     the compensation term keeps it
```

C tradition makes the fast one the default and lets you discover the error in
production. We invert that: **speed wears its cost in its name.** `lowdoc` now emits a
**Numeric error** section for any op that uses these — an unstated bound would make P2′
a slogan rather than a rule.

### RFC-0053 §8-1 — loop precision: the contract reaches inside the loop

The first analysis treated anything a loop touches as ⊤, so nothing inside a loop was
ever provable. It is now a proper basic-block CFG fixpoint: **widening** on back edges
for termination, and — the part that actually matters — **branch-condition narrowing**.
Without the third piece the widening flattens everything to ⊤ and `while lt i 10` fails
to bound `i` at all, which is why each abstract stack entry carries its predicate
provenance (comparison · left slot · **right-hand interval**).

Treating the right-hand side as an *interval* rather than a constant is what lets a
contract cross into the loop:

```
calcop guarded input n u8 . output u8 . requires le n 100 .
do  var i u8 be 0 .  while lt i n . do set i expr i + 1 . end  return i .  end

   n ≤ 100  and  i < n   ⇒   i ≤ 99   inside the body   ⇒   `add i 1` cannot overflow
```

Measured: the loop counter and the contracted bound are both proven; an unbounded
`add i 100` is not (2/3 removed). Conservative, never optimistic — and the 400-seed
fuzzer agrees (a false removal would surface as `E-VM-ANALYSIS`).

### RFC-0054 — conditional compilation, with the `#ifdef` disease removed

C's `#ifdef` is not bad because it branches. It is bad because **the arm you did not
select is never compiled**, so it rots — the error surfaces years later on someone
else's platform. Lowent had *no* mechanism at all here (`comptime` is specified but
cannot see the target; the `asm target` gate was declared and left without semantics).

`machine.*` are comptime constants, an ordinary `if` on them folds at compile time, and
the dead arm is dropped from codegen — **but it is still parsed and type-checked**:

```
$ lowentc --target x86_64 --check rot.low
  8:0 E-TYPE-SIGN: sign mismatch …          ← the error is in the arm x86_64 never runs
$ lowentc --target x86_64 --run rot rot.low 10 5
  rot(10, 5) = 11                            ← and the program still runs fine
```

Same source, three targets:

```
--target x86_64    ptr=64  endian=little  fpu path
--target cortex_m  ptr=32  endian=little  soft path (no FPU, no heap)
--target mips_be   ptr=32  endian=big     fpu path
```

The fold is reported (`--ir`), because a branch you cannot see the cost of is entropy.
Profile knobs (`no_heap`, `no_float`) become **readable** here — until now the build
could only *reject* violating code, and the source had no way to ask.

### RFC-0055 — `range` types: the contract crosses the op boundary

RFC-0053 turned `requires` into a static fact, but the fact stayed **trapped inside the
op**. A caller who knows `x ≤ 50` could not tell the callee, so the callee's entry check
survived. The fix is Ada's: **put the range in the type**, and the signature carries it.

```
calcop scale input a range 0 100 . output u8 . do return mul a 2 . end
calcop good  input x range 0 50  . output u8 . do return scale x . end
calcop bad   input y u8          . output u8 . do return scale y . end

   scale:  [0,100] * 2 = [0,200] ⊆ u8            → overflow check gone
   good:   x ∈ [0,50] ⊆ [0,100]                  → **the call's range check is gone**
   bad:    y ∈ [0,255] ⊄ [0,100]                 → check stays (and traps on 200)
```

The width is **derived from the range** (`range 0 200` → u8) — you state the contract,
the compiler picks the cheapest representation. Or you pin it: **`range u32 0 100`** says
"a u32, refined to [0,100]", and the compiler *enforces the declaration itself* —
`range u8 0 300` is a compile error, because u8 cannot hold 300. **The declaration is a
claim, and a claim that cannot be true is a lie, not a check.**

The two axes are enforced differently, and the split is the whole idea:

| | enforced how | why |
|---|---|---|
| **representation** (`u32`) | **statically — a type error** | passing an `f64` can never be right |
| **range** (`0..100`) | **proven, or checked at runtime** | passing a `u8` *is* right when it's ≤ 100 |

That's why `bad input y u8 . do return scale y . end` compiles (it keeps a check) while
`input z f64 . do return scale z . end` does not (no execution could make it correct). The subtyping is the ⊑ lattice again,
extended to ranges, and `rsub_preserves` / `radd_no_check` / `idx_no_check` are `Qed` in
`NumericLattice.v` — so the elimination rests on the same machine-checked argument as
implicit widening does.

This is what "the contract buys the performance" finally means at scale: **per call site,
independently**. A proven caller pays nothing.

### The declaration axis, measured

The 1-edit mutation metric used to move only **types** (`u8`→`u32`, `mut_ref`→`ref`). What this
session grew is the **declaration** axis — contracts, structs, names. So it is measured too:

```
      type width  (u8->u32)           480 mutants   87% detected
      type sign   (u32->i32)          480 mutants  100% detected
      type kind   (int->f64)          480 mutants  100% detected
      ref kind    (mut_ref->ref)      480 mutants  100% detected
      operator    (+ -> -)  [control]  330 mutants    0% detected
      literal     (n -> n+1)[control]  480 mutants    0% detected
      TYPE classes combined: 95% detected  (silent acceptance = 5%)

      declaration (static)              8 mutants  100% detected
      declaration (runtime contract)    2 mutants  (caught at the error/exit site)
```

Ten one-edit lies in a declaration: a variant renamed, a `tests` target that does not exist, a
`range` past its type, a `make` field dropped or invented, an unbound type name, a typo'd effect, a
`try` on a value with nothing to unwrap. **Every one that can be caught statically is caught.** The
two that cannot — a flipped `errors … when`, a broken `ensures` — are *runtime contracts by design*,
and the metric **says so** instead of calling them a gap. Before this session, most of these passed
silently.

Fixing the metric itself found two things: it was only running `low_typecheck` + `low_region`, so it
was **under-measuring what it claimed to measure**; and the `try` mutation exposed that builtins had
no return types (`index d 0` was `unknown`, so a `try` on it slipped through). Both fixed.

### A declaration that is never checked becomes a lie

`errors small when lt (len data) 4` says *"this op returns `small` only when the input is short."*
Until now that `when` condition was **never evaluated** — only the error *name* was checked. You
could write the exact opposite of the body and the compiler said nothing:

```
errors bad when gt a 200 .    <- the declaration
do  guard lt a 5 . else ...   <- the body does the opposite
    return error bad .
                              -> == check: ok ==   nobody looked
```

Now the condition is evaluated **at each error site**. Return the error on a path where your own
declared condition is false, and you are told so, by name: *the `errors` clause lied*.

**And the honest declaration is free.** The guard's condition and the `when` condition are the same
expression, so the analysis recognises it and discharges the check. Getting there needed two things:

1. `guard C else ...` lowers to `C; not; brz`, and **`not` was throwing the predicate away** — so
   *guard-derived branch narrowing never worked at all*. Now `not` inverts the predicate instead.
2. A small **path-sensitive predicate memory**: each expression gets a fingerprint, a branch records
   whether that expression is true or false on each side, and an error-site check consults it.
   Comparison operators are canonicalised (`ne` = not `eq`, `ge` = not `lt`, `le` = not `gt`) so the
   guard and the `when` clause recognise each other — and a match is confirmed by **comparing the
   actual instruction sequences**, not by trusting the hash, because the native backend really does
   delete the check.

**Polarity is the axis of soundness here.** `errors small when ge (len data) 4` on a body that
returns `small` when `len < 4` is the *same expression with the opposite truth value* — a lie the
fingerprint alone cannot see. Drop the polarity check and the VM immediately accuses itself
(`E-VM-ANALYSIS`). That is a test, not a claim.

### Contracts generate their own tests — **and their own oracle**

RFC-0008 left Q6 open: *"`requires ge length 2` → `0 1 2 3` — what algorithm makes those?"*

The answer is **boundary values**. Bugs live at the edges, not the middle — and the inequalities
the interval analysis used to *remove* a check (`v.hi ≤ r.lo`) live at exactly those edges. So
hitting the contract's boundary is hitting **the analysis's claim**.

The better half: **you don't have to write expected outputs.** The contract *is* the oracle.

```
a value INSIDE  the contract  → must NOT trap on the contract   (else the op rejects a legal input)
a value OUTSIDE the contract  → MUST     trap on the contract   (else a check was wrongly removed)
```

`--ir` reports it:

```
-- contract-derived boundary tests (RFC-0008 §6.4 · Q6) --
   2 op(s) with a contract tighter than their types → 12 case(s) (8 admitted · 4 rejected)
   0 oracle failure(s)  — every admitted input is accepted, every rejected input traps
```

**Two axes.** An integer parameter has a *value* range to shake (`range 0 100`, `requires le a 100`).
A slice parameter has no such range — but the contract still names constants about it:
`errors small when lt (len data) 4` points at **length 4**. So the generator walks lengths 3, 4, 5.
And there the oracle is the `when` clause itself: an honest op never violates its own declaration at
any length. **The generator catches a lying `errors` clause entirely on its own — no expected output
is written anywhere.**

**It found a real soundness bug on its first run.** A `range` parameter was enforced only at
*internal call sites* — never at the op's entry. But the interval analysis had already **trusted**
that range and deleted the overflow check. So a value arriving from *outside* the program (the VM
runner, C `main`, FFI) hit no barrier at all: the native binary silently returned **−2 from a `u8`
op**. RFC-0053 §6.6 again, from the other side: *a contract used as a fact must be enforced* —
and **the program boundary is the last door.** Internal calls still pay nothing.

### `ensures` — the contract comes back across the op boundary

`requires` carries a fact *into* the callee. `ensures` carries one *back out*. Until now it was
**pure decoration**: recognised as a clause word and then ignored — never enforced, never proven,
never used. So it made three changes at once:

```
calcop double_it input a range 0 100 . output u16 . ensures le ret 200 . do return mul a 2 . end
calcop use_it    input x range 0 100 . output u8  . do return narrow u8 (double_it x) . end
                                                            ↑ **no check** — and that is the point
```

`double_it` returns a `u16`. Narrowing a `u16` to a `u8` normally needs a check. But the caller
knows the result is in `[0,200]` — **because the callee promised, and the callee is made to keep
the promise.** Drop the `ensures` line and the check comes back. It is load-bearing.

1. **Enforced at exit.** A broken postcondition traps — and the diagnostic says *this op broke its
   own promise*, not "the caller broke the contract". Whose fault it is, is part of the message.
2. **Proven away when the body proves it.** `[0,100] * 2 = [0,200] ≤ 200` → the exit check is gone.
3. **Handed to the caller as a fact** — intersected with the declared return type, because
   `le ret 200` gives only an upper bound; the type gives the lower one.

**The entry `requires` check is never eliminated**, even when "provable". The analysis *planted*
that fact; discharging the check with it would be circular, and the contract would quietly become
a lie again — that is exactly the bug that once made a `u8` op return 256.

### Module linking — several files, one unit

`lowentc` only ever read **one file**. So `use` was matched against nothing (and said so, honestly),
and a `tests` clause could not name an op that lived in another file. Now several `.low` files link
into **one compilation unit** — lex and parse each, merge the form arrays, resolve names across.

```
$ lowentc --check tests/mod/math.low tests/mod/app.low     # one unit
$ lowentc --run main tests/mod/math.low tests/mod/app.low 5
main(5) = 10                                               # the call crossed the file boundary
```

**`use` has three answers, and the third one is the honest one:**

| | |
|---|---|
| the module is in the unit | **resolved.** The tool says nothing, because it *checked* |
| the module is not in the unit | **`W-USE-EXTERNAL`** — there is no module search path, so nothing here *can* confirm it. Calling it wrong would be a lie (it may be external); saying nothing would also be a lie |
| the name does not exist anywhere | `E-IR-UNDEF` at the call site — the **program** is wrong |

And the promise a `tests` clause makes in one file is now **kept in another**: alone, the target does
not resolve (`E-CONTRACT-UNDEF`); together, it does. That is what a module system buys.

Names are a **flat namespace** — no shadowing, no qualified paths. That is said plainly rather than
implied, and it is *enforced*: declaring the same name twice is `E-NAME-DUP`, because in a flat
namespace a second declaration does not **hide** the first — it silently makes one of the two
unreachable. Module linking exposed this immediately: module `b`'s `g` was calling module `a`'s `f`,
not its own. A wrong answer, quietly. When qualified paths (`a.f`) arrive, this rule can be relaxed;
until then it cannot.

### `parallel` — the compiler checks what the proof requires

`LowentPar.v` proves that if tasks do not touch each other's read/write sets, the parallel result is
**bit-identical** to the sequential one (`det1_par_eq_seq`, Qed), and that if they overlap, order
changes the answer (`overlap_is_nondeterministic`, Qed). So there is exactly one job for the
compiler: **check that the premise holds.**

```
calcop dbl input s mut slice u8 . . output u64 . parallel s split . do
  var i u64 be 0 .
  while lt i (len s) . do
    set (index s i) expr (index s i) * 2 . .    <- writes only its OWN element
    set i expr i + 1 . .
  end
  ...

  W-PAR-OK: this loop satisfies the Bernstein conditions and may be split
            (DET-1 proves the parallel result is bit-identical to the sequential one)
```

Break the premise and it says which one broke:

| | |
|---|---|
| reads another index (`index s 0`) | `E-PAR-READ` — rd ∩ wr ≠ ∅, a cross-iteration dependence |
| writes an accumulator | `E-PAR-CARRY` — loop-carried; a reduction must be declared (and its tree fixed: DET-3) |
| writes another element | `E-PAR-WRITE` — wr ∩ wr ≠ ∅ |

And a reduction must say what it is — because **whether the tree may be reshaped depends on the
operator**:

```
reduce acc add .        integer add is associative  -> the tree shape cannot change the answer
                        (assoc_shape_free, Qed)     -> splittable

var acc f64 be 0.0 .    float add is NOT associative -> splitting changes the tree, and the answer
reduce acc add .        (nonassoc_shape_matters, Qed)   would depend on the SCHEDULE
                        -> E-PAR-FLOAT. Use the sequential `sum` (Neumaier).
                           Determinism is part of the meaning, not a detail.
```

`E-PAR-CARRY` used to say *"declare a reduction instead"* — and **there was no such syntax**. The
tool was telling you to do something that did not exist. Now it exists, and DET-3 rides on it.

**Execution is still sequential — and that is a *valid implementation*, precisely because DET-1 says
so.** The proof buys the freedom to ship the check now and the scheduler later, without the meaning
changing underneath. Without the theorem, "we'll parallelise it later" would be a promise; with it,
it is a refactor.

### Slice element writes — R2 had a proof but no vessel

`set` only ever took a local name. So a kernel had **nowhere to put its result**: level-1 parallel
(R2) has machine-checked semantics (`LowentPar.v`) and no way to express the thing being
parallelised. The place form was missing.

```
calcop dbl input s mut slice u8 . . output u64 . do
  var i u64 be 0 .
  while lt i (len s) . do
    set (index s i) expr (index s i) * 2 . .     <- the place is a parenthesised form
    set i expr i + 1 . .
  end
  ...
```

Three things had to be true, and are:

1. **The bounds check on the WRITE is discharged by the same relational fact as the read.**
   The loop guard `lt i (len s)` establishes `i < len(s)`; `idx_no_check` (Qed) covers both
   directions. Deliberately removing the polarity of that fact makes the VM accuse itself
   (`E-VM-ANALYSIS`) — verified, not asserted. A false removal here is an out-of-bounds *write*,
   which is worse than a read.
2. **Mutability is enforced statically** (`E-TYPE-MUT`). The runtime slice value has no mut flag —
   its pointer is `const` — so the static side is the only place that can carry it, and we say so
   instead of pretending there is a runtime guard.
3. **The C backend agrees with the VM**, including on the trap.

### Index bounds — the interval domain alone can never do it

`index s i` is safe iff `i < len(s)`. But `len(s)` is a **runtime value**, so in an interval
domain it is ⊤ — and no amount of interval precision fixes that. This is exactly where
RFC-0053 stalled. The answer isn't a wider domain; it's **one relational fact**:

```
lenlt[i] = s   ⟺   "local i is less than len(local s)"
```

One slot next to the interval, because **that is the shape real code has**:

```
if lt i (len g) . do index g i . end        ← the guard creates the fact
while lt i (len g) . do … index g i … end   ← re-established by the loop condition
index g (mod i (len g))                     ← mod_is_a_safe_index (Qed) is the warrant
```

The fact **dies** when the slice is reassigned, and **dies** at a merge where the two paths
disagree. Measured on the fixture: all 3 provable bounds checks eliminated; the unguarded
index and the one that indexes a *different* slice keep theirs.

The VM keeps the check and **accuses itself** (`E-VM-ANALYSIS`) if a discharged check would
have failed — so the fuzzer is a soundness verifier for the analysis. The C backend actually
removes it (`lw_index_nc`). Exhaustive grid: len 0..8 × i 0..20, **756 runs, 0 self-accusations**.
And when the analysis is deliberately broken, that grid fails immediately — the check has teeth.

### Next (implementation plan, aligned with RFC-0050/0051)

RFC-0050 (safe 기본 90 / blessed 특례 9 / 비지원 1) and RFC-0051 (align/
intrinsic/asm/view placement) are policy RFCs — they gate *where* future
surfaces land, not new inventions. Impact here:

1. **Struct/view work follows RFC-0050 §6.6**: default struct (padding allowed,
   layout contract visible) → `layout c` → `layout packed` + `view`/`try_view`
   at boundaries only; packed access never spreads into hot paths.
2. **Blessed primitives stay primitives** (`linked intrusive`, `queue spsc`,
   `mmio block`, `view packed`): compiler/stdlib boundary with contracts
   (RFC-0050 §6.4-6.5 invariants I1–I5), not core grammar — no new keywords.
3. **Core additions queue** (RFC-0051 D1, in order): `align n` ✓ → `layout c` /
   `layout packed` ✓ → `view`/`try_view`/`view_array` ✓ →
   `vec`/`mask` ✓ — **the D1 core queue is complete**; target intrinsics stay
   opt-in gated (D2). `align n .` is a *contract*, not a hint: it raises the
   struct's alignment (and trailing pad) and is checked at every view boundary —
   `view` panics `E-VM-ALIGN` on a mis-aligned base, `try_view` returns `none`.
   That is exactly RFC-0051 §5.1's use sites (SIMD load/store, MMIO block,
   zero-copy view, packed boundary) enforced rather than assumed.
4. **Numeric builtins** ✓: `sqrt`/`abs`/`floor`/`ceil` (unary) and
   `fmod`/`min`/`max` (binary), tag-dispatched — `abs`/`min`/`max` stay integral
   on ints, `sqrt` always lifts to float, mixed int×float is rejected. Same
   no-implicit-coercion rule as the arithmetic core.
5. Remaining: full V3 mechanization (Coq/Lean). The agreement theorem's defect
   list (D1–D5) is closed and the D1 core queue is implemented, so the impl side
   of RFC-0050/0051 has no open items.

### S4 — MVP semantic analysis (`--check`)

Four static passes over the shared front-end's CST:

- **Effect discipline** (`low_check`): a `calcop` declares purity, so if its body
  performs an effect — **directly** (io builtin like `print`) or **transitively**
  (calling a `procop`) — it is rejected (`E-EFFECT-CALC`). Actual effects ⊆ declared.
  "calc op + io = structural rejection" (SPEC-003) — the C++-effect-visibility
  contrast, in code.
- **Type checking** (`low_typecheck`): kind-level (bool / integer / float / slice /
  named) **plus integer width/signedness**. Collects op signatures from
  `input`/`output` clauses, infers expression types (literals, `expr` islands,
  calls), and flags clear mismatches at `var N type be e`, `return e`, `set N e`
  (markerless, RFC-0049), and call arguments (`E-TYPE-*`). Width rules: literal
  values are range-checked against the declared width (`var i u8 be 300` →
  `E-TYPE-WIDTH`), implicit narrowing (u32→u8, f64→f32) is rejected, widening is
  allowed, signed/unsigned mixing → `E-TYPE-SIGN`; `cast <scalar> <e>` (G2) is the
  explicit escape hatch and yields the target type. Conservative — unknown/named
  types and mixed-width `expr` islands never flag.
- **Contracts** (`low_contract`): `errors`-closure — an op may only produce errors
  listed in its `errors` clause (`return error X` / `err X` with X undeclared →
  `E-ERR-UNDECLARED`); a closed declared error set (checked exceptions without the
  boilerplate). `requires`-resolve — a `requires` condition may only reference in-scope
  names (params, declared ops, known predicates), so a typo'd contract is caught
  (`E-REQ-UNDEF`).
- **Region / escape / EXCL** (`low_region`): a reference to a *local* (`var`) must not
  escape the op — `return`/`give` of `ref`/`mut_ref`/`addr` of a local → `E-ESCAPE`
  (the "return &local" dangling-reference bug, rejected statically). References to
  params are fine. **EXCL exclusivity** (readers-XOR-writer, SPEC-004 §4.4): a borrow
  of a local may not overlap a live `mut_ref` of the same local → `E-EXCL` — covers
  bound borrows (`var a … be mut_ref x`) and transient call-argument borrows
  (`g mut_ref x mut_ref x`). Liveness is lexical (a bound borrow lives to the end of
  its block; no CFG shortening) — conservative by construction; dataflow-precise
  liveness is a future refinement.

### S3 — shared front-end grows to the MVP (BOOTSTRAP §3)

The MVP surface (`calcop`/`procop` with `.`-terminated contract clauses, `make T do…
end` aggregates, `struct`/`enum` — normative headless field list `struct N 필드* end`
(no `do`, SPEC-002 schema; the `do` form is also accepted), `while`, `for` with a
`.`-closed iterable form, `var … be …`; `set` is markerless per RFC-0049) parses on
the **same** point-closure parser — **no rewrite**, just keyword rows + block-head
dispatch. `tests/mvp.low` parses clean. This proves the
bootstrap claim: mini and MVP share the front-end; MVP adds schemas + (later, S4)
semantic passes, not a new parser. The mini evaluator ignores MVP-only forms.

### ★ S2 · S1 — **the mini language. IT IS GONE.** (history)

> ★★★ Everything in the two sections below describes the **old tree-walking evaluator**
> and the **second, undocumented language** it ran (`to`/`in` access, `fail`, `give`,
> `print`, closures, lists). **All of it was deleted** (DECISION-0012): `low_eval.c`
> (1422 lines) and the vocabulary that only it understood.
>
> **THE LANGUAGE IS ONE.** These sections are kept as a *record of what was removed* —
> they are **not** a description of the tool. Read them in the past tense.
>
> (Why keep them at all? Deleting the record of a deletion is how a project forgets what
> it learned. But a stale section written in the present tense is a **lie**, and this
> banner is the fix.)


### S2 — practical mini

- **Formatter** (`--fmt`): re-emits canonical source; **round-trip idempotent** and
  behavior-preserving (golden-tested). Re-inserts dropped `for`-marker / `else`.
- **Richer prelude**: higher-order `map`/`filter`/`fold` (closures as callbacks),
  `sort`/`reverse`/`append`/`rest`, `to_string`/`to_number`/`upper`/`lower`.
- **`fmt`** — structural string formatting on the **`proven/fmt.h` grammar** (rather than
  string interpolation): `fmt template arg…` with `{}` positional, `{N}` 0-based indexed,
  `{:[fill]align[width][x]}` spec (align `<`/`>`/`^`, `x` = hex), and `{{`/`}}` literals.
  e.g. `fmt "[{:>6}] {:*^8} {:x}" n name 255` → `"[    42] **bob*** ff"`.
  **`print_fmt`** = `print ∘ fmt` (a printf: format, then print the string + newline; io,
  so script-only like `print`).
- **Mutation** (§12.B): `cell`/`get_cell`/`set_cell` (`LOW_V_CELL`).
- **Error reification** (§13): `catch thunk` → tagged record `{kind, value|msg}`
  (mini-only; 본체 uses typed result/option + `try`-propagation, RFC-0006).

### S1 — tree-walking evaluator (`low_value`, `low_eval`)

Dynamic values (none/unit/bool/int/float/string/list/record/op/builtin) in a value
arena (bulk-freed). Lexical scopes = proven maps; **user ops are closures** capturing
their defining scope, plus **`fn` anonymous lambdas** — light `fn x to expr x * 2`
(`to` = "maps to", implicit return) or block `fn x do … end`, both → an op value.
**Strict arity** (a call whose arg count ≠ the op's params → `E-ARITY`, like 본체).
Runtime diagnostics name the offender (`E-UNDEF: undefined name 'foo'`, `E-ARITY: op
expects 2 arguments, got 1`). `let`/`set`, `op`, `if`/
`else`, `for … in`, `guard`/`return`/`give`/`break`/`continue`/`fail`, **expr islands
with precedence** (`* /` > `+ -` > compare > `and/or`), **`to`/`in` access** →
field/index. Prelude Γ: `add sub mul div mod eq ne lt le gt ge and or not print concat
len list range type_of first field call`. `call fn arg…` = apply — the way to invoke a
0-arg op/builtin (a bare `pwd`/`list` is a *reference*): `call list` = `()`. **Posture**:
`--pure` withholds io (`print`) from Γ. A reusable eval API (`low_eval_new`/`low_bind`/
`low_eval_forms`) lets hosts embed the interpreter and add native builtins. The shell that
used it (`lowshell/`) was moved out of this repository on 2026-08-29.

### S0 — front-end (still the shared base)

Implemented and tested (ASan/UBSan clean):

- **L0 lexer**: IDENT + glued-dot qualified names (`net.http`), NUMBER — signed
  dec/hex/bin ints, dec floats (`1.5e3`) and **hex floats** (`0x1.8p3`), `_` separators;
  `-`/`+` glued to a digit is a signed literal in argument position but the arithmetic
  operator inside an `expr` island (so `range 10 0 -1` and `expr a - b` both read right) —
  STRING with escape decoding (`\n \t \r \\ \" \' \0 \a \b \f \v`, `\xHH` hex byte,
  `\u{…}` Unicode → UTF-8; no-escape strings stay zero-copy), `text` heredoc (verbatim,
  no escapes), DOT closer
  vs glued dot disambiguation, COMMA, `( )`, `+ - * /`, `rem`/`note` comments, `..`
  lexical error, line/col tracking.
- **L1 point-closure CST**: uniform `head args closer` forms; `do…end` blocks; `( )`
  groups; `to`/`in` access chains (forward left-assoc / reverse right-assoc);
  comma-grouped arguments; four schema-aware block heads (`op`/`if`/`for`/`loop`).

### Concrete grammar decisions this implementation settles

The rev.d point-closure surface left a residual ambiguity (§3.2); building the parser
forces concrete choices. This impl commits to (see `low_cst.h` header comment):

1. **Tail-merge, one closer per statement.** `let x add a b .` — one dot. No `. .`.
2. **Explicit nesting only.** Flat operand runs stay flat at L1 (`let x add a b .` =
   `let(x, add, a, b)`); the evaluator brackets with Γ. Nested *calls* use parens
   (`let x (add a b) .`) or `do…end`. No bare-dot arity bracketing at parse time.
3. **Comma groups arguments** of the current head; each comma-segment is one arg
   (lone operand, or a sub-form for a multi-operand run).
4. **Headed block** `record do…end` binds the block to the preceding atom, except
   under the four block-statement heads where `do` binds to the statement.

These resolve the long-standing "needs a real parser to validate" caveats in the spec.

## Performance / scaling

Loops are **O(1) memory** and scale unbounded: `range` is a lazy iterator (never
materialized) and `for` reuses one scope (rebinding the loop var) instead of
allocating a fresh scope+map per iteration. A tight scalar loop is faster than the
shells and no longer overflows the value arena:

| loop-sum | N=10k | N=1M | N=10M |
|----------|-------|------|-------|
| lowentc  | 12 ms | 0.72 s | 7.4 s (scales linearly) |
| dash     | 18 ms | — | — |
| bash     | 28 ms | 2.6 s | — |

**Nursery + escape-safe promotion** (the general design): each loop iteration allocates
its body scope + temporaries in a **scratch arena** that is bump-**reset** at the end of
the iteration (near-free — just move the offset). Values that **escape** the iteration
(assigned to an outer scope via `set`, mutated into an outer `cell`, or `return`/`give`n)
are **promoted** — deep-copied into the persistent arena first, sharing already-persistent
substructure so accumulation stays linear. A forwarding memo breaks cycles (e.g. a
closure whose captured scope binds the closure itself). So loop bodies that build and
discard strings/lists each iteration are now **O(1)** too:

```
for i in range 0 1000000 do let msg (concat "row " (to_string i)) . print msg . end
```
— 1M transient strings, reclaimed per iteration, flat memory. (ASan-clean, incl.
closures escaping loops.) **Persistent-generation GC** (major collection). The persist arena is compacted by a
**copying collector**: when it fills past 75%, live objects (reachable from the scope
tree + a shadow root stack of the C-stack scope/value pointers pushed by active loops)
are copied to a fresh backing block (sharing + cycle-safe via the same forwarding memo),
and the old block is freed — superseded values (e.g. old versions of an accumulator)
vanish. So even repeated *reassignment* of a growing heap value stays memory-bounded:

```
let s (list 0) . for i in (range 1 10000) do set s (append s i) . end   # was O(N²) memory
```
— completes in bounded memory (~one arena), ASan-clean including the moving GC. Together:
**minor GC** (nursery reset per iteration) + **major GC** (copying compaction of persist)
give a two-generation collector, so loops scale unbounded in memory regardless of pattern.

## Next — measured, not guessed

The tool now says what it cannot do. Ask it:

```sh
for f in tests/*.low; do ./build/lowentc --why-slow $f; done   # what is still on the tagged path, and WHY
python3 ../scripts/diff-sweep.py                               # what the two back ends were never asked
```

Standing debt (measured 2026-07-15):

- **Typed lowering is at 67%.** The rest is named by `--why-slow` — mostly struct/view
  returns and values that cross a call in a shape the fast path cannot follow.
- **The VM boxes what the native build does not.** Records, bitsets, stacks, option/result
  boxes and encode buffers each live in a **finite pool**; the VM says so by name
  (`E-VM-*POOL`) instead of dying. The lowered native path boxes **nothing**.
- **`lowshell/` is gone from this repository** (moved to the private archive, 2026-08-29). It linked the deleted evaluator and did not build.
- **Raw `kids[]` accesses** in the consumers still bypass the tree in places
  (`low_flat_kids` is the one window; the number of uses IS the remaining debt).
- FFI: struct-by-value, callbacks, and `@cImport`-style binding generation (RFC-0063 §5).
