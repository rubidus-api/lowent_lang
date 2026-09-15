#!/usr/bin/env python3
"""부록 B(진단 찾아보기)를 예제에서 짓는다.

examples/chNN/*.low 의 `rem expect: <코드>` 를 모아, 코드마다 그것을 보이는 장을 잇는다.
뜻은 아래 표가 준다 — 표에 없는 코드가 예제에 나타나면 실패한다(뜻 없는 줄을 싣지 않는다).
결과: typst-ko/appendix/a2-diagnostics.typ · typst-en/appendix/a2-diagnostics.typ
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

MEANING = {
 "E-ALLOC-NESTED": ("같은 뿌리의 안쪽 영역이 열린 채 바깥 출처로 깎았다", "allocated from an outer source while an inner region of the same root is open"),
 "E-ALLOC-NOCAP": ("`alloc` 을 적었는데 할당 권한을 받지 않았다", "declares `alloc` but receives no allocation capability"),
 "E-ASM-UNBOUND": ("어셈블리 템플릿이 선언하지 않은 피연산자를 부른다", "the assembly template names an undeclared operand"),
 "E-ATOMIC-NOCAP": ("`atomic` 을 적었는데 `cap atomic` 을 받지 않았다", "declares `atomic` but receives no `cap atomic`"),
 "E-ATOMIC-ORDER": ("그 원자 연산에 뜻이 없는 기억 차례다", "a memory ordering that has no meaning for that atomic operation"),
 "E-BOUND-UNSAT": ("타입 인자가 요구한 트레이트를 갖추지 못했다", "the type argument does not satisfy the required trait"),
 "E-CAP-FORGE": ("권한 칸을 가진 액터를 그 권한 없이 띄운다", "spawns an actor holding a capability without holding that capability"),
 "E-CAP-KIND": ("다른 종류의 권한을 건넸다", "a capability of the wrong kind was handed over"),
 "E-CAP-MISSING": ("권한이 필요한 내장 연산에 권한을 첫 피연산자로 적지 않았다", "a capability-requiring builtin was not given its capability as the first operand"),
 "E-CLAUSE-ORDER": ("op 머리의 절이 정해진 차례를 어겼다", "the clauses of an op header are out of the fixed order"),
 "E-COMPTIME-ARG": ("`comptime` 자리에 실행 값을 주었다", "a run-time value in a `comptime` position"),
 "E-CONC-ALONE": ("묶음이 흐름 하나만 만들고 그 흐름이 짝을 기다린다", "a task group spawns one task that waits for a peer"),
 "E-CONC-DEADLOCK": ("묶인 흐름이 모두 받기만 하고 아무도 보내지 않는다", "every task in the group receives and none sends"),
 "E-CONTRACT-DEAD": ("`requires` 가 이미 배제한 오류를 선언했다", "declares an error that `requires` already excludes"),
 "E-CONTRACT-IMPOSSIBLE": ("상수끼리의 호출이 상대의 `requires` 를 어긴다", "a call with constant arguments breaks the callee's `requires`"),
 "E-EFFECT": ("선언하지 않은 효과를 낸다", "performs an effect its `effects` clause does not declare"),
 "E-EFFECT-CALC": ("순수한 `fn` 이 효과를 낸다", "a pure `fn` performs an effect"),
 "E-EFFECT-DUP": ("효과 줄에 같은 원자를 두 번 적었다", "the same atom twice in an `effects` clause"),
 "E-EFFECT-NO-CAP": ("`io` 따위를 적었는데 허락하는 권한이 없다", "declares `io` (or similar) without a capability that authorises it"),
 "E-EFFECT-NONE-MIX": ("`none` 을 다른 효과와 함께 적었다", "`none` written together with a real effect"),
 "E-EFFECT-PURITY": ("`fn` 이 호출자나 액터가 볼 수 있는 상태에 쓴다", "a `fn` writes state visible to its caller or actor"),
 "E-EFFECT-REDUNDANT": ("`fn` 에 `effects none` 을 적었다", "`effects none` written on a `fn`"),
 "E-EFFECT-UNDEF": ("목록에 없는 효과 낱말이다", "an effect word outside the closed vocabulary"),
 "E-ENTRY-PARAMS": ("시작점이 권한 아닌 입력을 받는다", "the entry point takes a non-capability input"),
 "E-ENUM-DOT": ("열거의 갈래를 마침표로 닫지 않았다", "an enum variant not closed with `.`"),
 "E-ENUM-INFINITE": ("갈래가 자기 타입을 값으로 품는다", "a variant embeds its own type by value"),
 "E-ERR-UNDECLARED": ("`errors` 절에 없는 오류를 돌려준다", "returns an error not in its `errors` clause"),
 "E-ESCAPE": ("지역을 가리키는 참조가 op 밖으로 나간다", "a reference to a local escapes the op"),
 "E-EXCL": ("같은 값에 대한 빌림이 겹친다", "overlapping borrows or owner access to the same value"),
 "E-EXPR-APP": ("`expr` 섬 안의 부름을 괄호로 묶지 않았다", "a call inside an `expr` island is not parenthesised"),
 "E-EXPR-CHAIN": ("`expr` 섬에서 비교를 이어 썼다", "comparisons chained in an `expr` island"),
 "E-EXPR-UNARY": ("`expr` 섬에 단항 연산자를 썼다", "a unary operator in an `expr` island"),
 "E-FFI-NOCAP": ("C 를 부르는데 `cap c` 를 받지 않았다", "calls C without receiving `cap c`"),
 "E-FFI-NOUNSAFE": ("C 를 부르는데 `unsafe` 표시가 없다", "calls C without the `unsafe` mark"),
 "E-FFI-TYPE": ("C ABI 가 표현할 수 없는 타입이 경계를 건넌다", "a type the C ABI cannot express crosses the boundary"),
 "E-GROUP-UNCLOSED": ("괄호가 닫히지 않았다", "an unclosed parenthesis"),
 "E-GUARD-FALLTHROUGH": ("`guard` 의 `else` 가 떠나지 않는다", "the `else` of a `guard` does not leave"),
 "E-HEAP-NOHOST": ("운영체제 없는 대상에서 자라는 뿌리를 청한다", "asks for the growing root on a freestanding target"),
 "E-IF-VALUE": ("`if` 를 값으로 썼다", "`if` used as a value"),
 "E-IMMUTABLE": ("`let` 으로 지은 이름에 `set` 했다", "`set` on a name bound with `let`"),
 "E-ISR-CALLED": ("인터럽트 처리기를 코드에서 불렀다", "an interrupt handler called from code"),
 "E-LET-NOVALUE": ("`be` 뒤에 값이 없다", "nothing after `be`"),
 "E-MATCH-INEXHAUSTIVE": ("`match` 가 모든 경우를 덮지 않는다", "a `match` does not cover every case"),
 "E-MATCH-REDUNDANT": ("`match` 의 갈래가 영영 돌지 않는다(`_` 뒤의 갈래·겹친 범위)", "a `match` arm can never run (an arm after `_`, overlapping ranges)"),
 "E-METHOD-UNDEF": ("수신자의 타입에 그 이름의 붙은 op 이 없다", "no op of that name is attached to the receiver's type"),
 "E-MMIO-BYVALUE": ("레지스터 묶음을 값으로 받는다", "an mmio register block taken by value"),
 "E-MMIO-PERM": ("읽기 전용 레지스터에 쓴다", "writes a read-only register"),
 "E-MREF-SLICE": ("`mut ref slice` 를 썼다", "`mut ref slice` used"),
 "E-NAME-DUP": ("한 모듈에 같은 이름을 두 번 선언했다", "the same name declared twice in one module"),
 "E-NAME-SHADOW": ("살아 있는 이름을 다시 지었다(가림)", "re-binds a live name (shadowing)"),
 "E-OPT-UNUSED": ("선언한 빌드 손잡이를 아무 코드도 읽지 않는다", "a declared build option that no code reads"),
 "E-OWN-INCOMPLETE": ("완결이 필요한 값을 끝내지 않고 버린다", "a value that needs completion is dropped implicitly"),
 "E-OWN-JOIN": ("갈래마다 소유 상태가 다르다", "ownership state differs between branches"),
 "E-OWN-MOVED": ("옮긴 값을 다시 쓴다", "uses a value after it was moved"),
 "E-PAR-ASSOC": ("모으는 연산이 결합적이지 않다", "the reduction operator is not associative"),
 "E-PAR-CARRY": ("나누는 되풀이가 걸음을 넘어 사는 자리에 쓴다", "a split loop writes a local that lives across iterations"),
 "E-PAR-READ": ("나누는 되풀이가 남의 원소를 읽는다", "a split loop reads another iteration's element"),
 "E-PAR-WRITE": ("나누는 되풀이가 남의 원소에 쓴다", "a split loop writes another iteration's element"),
 "E-PIPE-NO-TERMINAL": ("종결자 뒤에 스테이지가 온다", "a stage after the terminal"),
 "E-PIPE-STAGE": ("`pipe` 스테이지 목록에 없는 낱말이다", "a word that is not a `pipe` stage"),
 "E-PROFILE-LEVEL": ("빌드 프로파일이 주지 않는 동시성을 쓴다", "uses concurrency the build profile does not provide"),
 "E-REGION-ESCAPE": ("영역에서 얻은 바이트를 밖으로 들고 나간다", "carries region bytes out of the region"),
 "E-REGION-KIND": ("영역의 종류가 닫힌 여덟에 없다", "a region kind outside the closed eight"),
 "E-RETURN-PARTIAL": ("어떤 길에서 값을 돌려주지 않는다", "some path does not return a value"),
 "E-SPAWN-SCOPE": ("`task_group` 밖에서 흐름을 만든다", "spawns a task outside a `task_group`"),
 "E-TIER-EFFECT": ("선언한 기계 등급이 감당하지 못하는 효과다", "an effect the declared machine tier cannot carry"),
 "E-TRAIT-EFFECT": ("갖춘 op 이 서명보다 많은 효과를 가진다", "the implementing op has more effects than the signature"),
 "E-TRAIT-MISSING": ("트레이트의 op 하나가 없다", "an op required by the trait is missing"),
 "E-TRAIT-SIG": ("서명에 `fn`·`proc` 을 적었거나 매개변수 수가 다르다", "`fn`/`proc` in a signature, or a parameter-count mismatch"),
 "E-TYPE-ARRAY": ("`array` 의 길이를 타입 뒤에 적었다", "`array` length written after the type"),
 "E-TYPE-BITCAST": ("`bit_cast` 의 목표가 모든 비트열이 값인 타입이 아니다", "`bit_cast` target is not a plain scalar"),
 "E-TYPE-COND": ("조건 자리에 참거짓이 아닌 값이 왔다", "a non-boolean in a condition"),
 "E-TYPE-DECL": ("타입 선언에 `be` 를 끼웠다", "`be` in a type declaration"),
 "E-TYPE-INSTANCE": ("같은 제네릭의 서로 다른 인스턴스를 섞었다", "mixes different instances of the same generic"),
 "E-TYPE-MUT": ("`mut` 이 아닌 슬라이스의 원소에 쓴다", "writes an element of a non-`mut` slice"),
 "E-TYPE-NOMINAL": ("표현이 같아도 이름이 다른 타입을 섞었다", "mixes nominally distinct types"),
 "E-TYPE-REF": ("읽기 참조로 쓴다", "writes through a shared `ref`"),
 "E-TYPE-SIGN": ("값을 지키는 넓히기가 없는 부호 섞기다", "mixes signs with no value-preserving widening"),
 "E-TYPE-WIDTH": ("값이 선언된 타입에 들어가지 않는다", "the value does not fit the declared type"),
 "E-VISIBILITY": ("다른 모듈의 감춘 이름에 닿는다", "reaches a non-exported name of another module"),
 "E-VOCAB-REMOVED": ("없앤 낱말이나 철자다", "a removed word or spelling"),
 "W-EFFECT-OVER": ("선언만 하고 내지 않는 효과다(경고)", "an effect declared but never performed (warning)"),
 "W-USE-EXTERNAL": ("번역 단위에 없는 모듈을 들여온다(경고)", "imports a module not in the compilation unit (warning)"),
 "E-ALLOC-AMBIGUOUS": ("맞는 할당기가 둘 이상인데 `using` 으로 고르지 않았다", "more than one fitting allocator and none chosen with `using`"),
 "E-ALLOC-USING-UNUSED": ("할당기를 쓰지 않는 호출에 `using` 을 적었다", "`using` on a call that does not draw from an allocator"),
 "W-NOT-YET": ("이름은 받지만 아직 뜻이 없는 낱말을 썼다", "a word accepted by name that has no meaning yet"),
 "E-LOCK-NOTYET": ("흐름끼리 나누는 자물쇠 타입은 아직 짓지 않았다", "shared lock types are not built yet"),
 "E-ACCESS-MODE": ("`access` 로 적은 읽기·쓰기 약속을 몸이 어긴다", "the body breaks the read or write promise made with `access`"),
 "E-ALLOC-NOSOURCE": ("할당기를 쓰는 호출인데 이 op 안에 맞는 할당기가 하나도 없다", "a call draws from an allocator but no fitting one is visible in this op"),
 "E-ALLOC-OUTLIVES": ("영역의 바이트를 영역 밖에서 태어난 액터에게 건넨다", "region bytes handed to an actor born outside the region"),
 "E-ALLOC-TASK": ("뿌리에서 깎는 op 을 태스크로 띄운다", "an op that carves from a root is spawned as a task"),
 "E-ALLOC-SHARED": ("원자적이지 않은 할당기를 태스크에 건넨다", "an allocator whose cursor is not atomic is handed to a task"),
 "E-ASM-TARGET-UNKNOWN": ("`asm` 절의 기계 이름이 대상 목록에 없다", "the machine name in an `asm` clause is not a known target"),
 "E-BLOCK-UNCLOSED": ("`do` 로 연 블록을 `end` 로 닫지 않았다", "a block opened with `do` is never closed with `end`"),
 "E-BRAND-REUSED": ("같은 브랜드로 저장소를 두 번 열었다", "a brand opens a second storage"),
 "E-CHAR": ("이 언어에 없는 글자(기호)다", "a character (symbol) this language does not have"),
 "E-CHAR-WIDTH": ("글자 리터럴이 글자 하나가 아니다", "a character literal is not a single character"),
 "E-CONFIG-TYPE": ("구성이 손잡이의 선택지에 없는 값을 준다", "the configuration gives a value the option does not offer"),
 "E-CONFIG-UNDEF": ("선언하지 않은 빌드 손잡이를 구성이나 `config` 가 부른다", "a configuration or `config` names an undeclared build option"),
 "E-DEP-MISSING": ("`use … from` 의 자리에서 파일을 읽을 수 없다", "the file at a `use … from` place cannot be read"),
 "E-ENS-UNDEF": ("`ensures` 가 없는 이름을 가리킨다 --- 돌려주는 값은 `ret`", "`ensures` names something undefined --- the returned value is `ret`"),
 "E-FFI-NOEFFECT": ("C 를 부르는 op 에 효과 줄이 없다", "an op calling C declares no effect"),
 "E-FIELD-GLUED": ("값 뒤에 점을 붙여 칸을 읽으려 했다 --- `field` 로 적는다", "tried to read a field by gluing a dot to a value --- write `field`"),
 "E-FN-CAP": ("되부름으로 넘길 op 이 권한을 요구한다", "an op to be passed as a callback requires a capability"),
 "E-FN-NOTEXPORT": ("`unsafe_fn` 이 `export extern` 이 아닌 op 을 가리킨다", "`unsafe_fn` names an op that is not `export extern`"),
 "E-FOLD-OP": ("`pipe` 스테이지가 없는 op 을 부른다", "a `pipe` stage names an op that does not exist"),
 "E-IR-ARITY": ("op 을 인자 수가 맞지 않게 불렀다", "an op is called with the wrong number of arguments"),
 "E-IR-UNDEF": ("그 자리에서 알 수 없는 이름이다", "a name unknown at that place"),
 "E-ISR-EFFECT": ("인터럽트 처리기가 `effects device` 를 적지 않았다", "an interrupt handler does not declare `effects device`"),
 "E-ISR-PARAMS": ("인터럽트 처리기가 매개변수를 받는다", "an interrupt handler takes parameters"),
 "E-NAME-BUILTIN": ("내장 op 의 이름을 선언이나 지역 이름으로 썼다", "a builtin op's name is used for a declaration or local"),
 "E-PAR-FLOAT": ("부동소수 누적을 나누어 모으려 했다", "a floating-point accumulation declared splittable"),
 "E-PAR-NOLOOP": ("`parallel` 절이 나눌 되풀이를 찾지 못했다", "the `parallel` clause finds no loop to split"),
 "E-TOPLEVEL": ("최상위에 올 수 없는 것이 최상위에 있다", "something that cannot appear at top level is at top level"),
 "E-TYPE-FIELD": ("구조체를 만들 때 칸이 빠졌거나 없는 칸을 적었다", "a field is missing or unknown when building a struct"),
 "E-TYPE-KIND": ("그 연산에 쓸 수 없는 갈래의 타입이다(예: `bool` 을 `cast`)", "a type of the wrong kind for that operation (e.g. `cast` of `bool`)"),
 "E-TYPE-LOGICAL": ("논리 연산에 참거짓이 아닌 값을 주었다", "a non-boolean value given to a logical operation"),
 "E-TYPE-RETURN": ("돌려주는 값이 op 의 출력 타입과 다르다", "the returned value does not match the op's output type"),
 "E-UNSAFE-UNDECLARED": ("`unsafe` 효과를 적었는데 op 에 `unsafe` 표시가 없다", "declares the `unsafe` effect but the op is not marked `unsafe`"),
 "E-WIDEN-SIGN": ("부호 있는 수를 부호 없는 타입으로 `widen` 했다", "`widen` from a signed to an unsigned type"),
}

RUNTIME = [
 ("E-VM-OVERFLOW", "numbers", "정수가 선언된 폭에서 넘쳤다", "integer overflow at the declared width"),
 ("E-VM-DIV0", "numbers", "0 으로 나누었다", "division by zero"),
 ("E-VM-CAST", "numbers", "좁히기·`cast` 의 값이 목표 타입에 들어가지 않는다", "a narrowing or `cast` value does not fit"),
 ("E-VM-SHIFT", "numbers", "옮기는 칸 수가 폭 이상이다", "shift amount not smaller than the width"),
 ("E-VM-BOUNDS", "slices", "색인이 범위 밖이다", "index out of bounds"),
 ("E-VM-NONE", "option-result", "없는 값을 꺼냈다", "took the value out of `none`"),
 ("E-VM-CONTRACT", "contracts", "계약(`requires`·`ensures`·`errors`·범위)이 깨졌다", "a contract was broken"),
 ("E-VM-PANIC", "control", "`panic` 을 불렀다", "the program called `panic`"),
 ("E-VM-ANALYSIS", "proofs-numbers", "지운 검사의 자리가 실제로 범위 밖이다(컴파일러 결함)", "an eliminated check was actually needed (compiler bug)"),
 ("E-TEST-FAIL", "build-test", "시험의 `expect` 가 거짓이다", "an `expect` in a test is false"),
]

def main():
    import chapters  # noqa: F401  (registry reader used by the other checks)
    reg = (ROOT / "typst-ko" / "registry.typ").read_text(encoding="utf-8")
    ids = re.findall(r'"([a-z0-9-]+)"', reg.split("#let chapter-ids")[0].split("#let parts")[1])
    ids = [i for i in ids if not i.startswith("part")]
    by_code = {}
    for f in sorted((ROOT / "examples").glob("ch*/*.low")):
        m = re.search(r"^rem expect: *([A-Z0-9-]+)", f.read_text(encoding="utf-8"), re.M)
        if not m:
            continue
        n = int(f.parent.name[2:])
        by_code.setdefault(m.group(1), [])
        cid = ids[n - 1]
        if cid not in by_code[m.group(1)]:
            by_code[m.group(1)].append(cid)
    missing = [c for c in by_code if c not in MEANING]
    if missing:
        print("gen-diag-appendix: 뜻이 없는 코드:", " ".join(missing), file=sys.stderr)
        return 1
    for lang, dirn, title, lead, head, rt in (
        ("ko", "typst-ko", "부록 B --- 진단 찾아보기",
         "이 책의 예제가 실제로 일으킨 진단만 모았다. 코드는 판이 바뀌어도 같은 뜻을 지킨다. 진단의 긴 영문 설명과 고치는 법은 컴파일러가 그 자리에서 말한다.",
         ("*코드*", "*뜻*", "*보이는 곳*"), "== 실행 중에 멈출 때"),
        ("en", "typst-en", "Appendix B --- Diagnostics index",
         "Only the diagnostics that this book's examples actually trigger are listed. A code keeps its meaning across releases; the compiler explains the details and the repair at the site.",
         ("*Code*", "*Meaning*", "*Shown in*"), "== When execution stops")):
        imp = '#import "../lib.typ": *' if lang == "ko" else '#import "../../typst-ko/lib.typ": *'
        out = [imp, "", f"= {title}", "", lead, "",
               "== " + ("번역할 때" if lang == "ko" else "At translation time"), "",
               "#dtable(", "  columns: 3,", '  id: "a2-static",',
               "  caption: [" + ("예제가 보이는 진단" if lang == "ko" else "Diagnostics shown by the examples") + "],",
               f"  [{head[0]}], [{head[1]}], [{head[2]}],"]
        for code in sorted(by_code):
            mean = MEANING[code][0 if lang == "ko" else 1]
            refs = ", ".join(f'#chref("{c}")' for c in by_code[code])
            out.append(f"  [`{code}`], [{mean}], [{refs}],")
        out += [")", "", rt, "", "#dtable(", "  columns: 3,", '  id: "a2-runtime",',
                "  caption: [" + ("실행 중의 진단" if lang == "ko" else "Run-time diagnostics") + "],",
                f"  [{head[0]}], [{head[1]}], [{head[2]}],"]
        for code, cid, ko, en in RUNTIME:
            out.append(f'  [`{code}`], [{ko if lang == "ko" else en}], [#chref("{cid}")],')
        out += [")", ""]
        (ROOT / dirn / "appendix" / "a2-diagnostics.typ").write_text("\n".join(out), encoding="utf-8")
    print(f"gen-diag-appendix: 진단 {len(by_code)} · 실행 중 {len(RUNTIME)}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
