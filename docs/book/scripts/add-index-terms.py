#!/usr/bin/env python3
"""찾아보기 표제어를 원고에 심는다(한 번만 돌리는 도구 — 이미 심은 표제어는 건너뛴다).

표제어마다 (장 id, 원고에서 찾을 글) 을 준다. 그 장의 본문(`#chapter-questions()` 뒤)에서 그 글이
처음 나오는 줄의 앞에 `#idx("표제어")` 줄을 넣는다. 못 찾으면 알린다.
"""
import pathlib, re, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent

TERMS = {
 "ko": [
  ("주변 권한", "intro", "주변 권한(ambient authority)"), ("권한", "capabilities", "권한은 `input"),
  ("효과", "effects", "*효과*는"), ("순수함", "ops", "순수함은 관측"), ("fn", "ops", "`fn` --- *순수한*"),
  ("proc", "ops", "`proc` --- 효과를"), ("계약", "contracts", "*계약*은"), ("requires", "contracts", "`requires`], [들어올 때"),
  ("ensures", "contracts", "`ensures` 에서"), ("errors 절", "option-result", "`errors` 절에 적는다"),
  ("계약의 등급", "contracts", "계약 절 하나에 등급을"), ("빌드 모드", "contracts", "빌드 모드가 정하는"),
  ("닫개", "surface", "*닫개*라"), ("전위 표기", "surface", "*전위 표기*다"), ("expr 섬", "expr", "섬 안에서만 우선순위"),
  ("절 차례", "surface", "op 의 머리는 이름 뒤에"), ("가림", "surface", "*가림(shadowing)이 없다.*"),
  ("넓히기", "numbers", "*넓히기*라"), ("좁히기", "numbers", "*좁히기*이고"), ("넘침", "numbers", "산술은 *선언된 폭에서*"),
  ("단락 평가", "expr", "`and` 와 `or` 는 앞쪽만으로"), ("comptime", "generics", "`comptime` 을 붙인 매개변수"),
  ("지역", "locals", "*지역*이다"), ("guard", "control", "`guard <조건> . else"), ("match", "control", "`match` 는 값을 경우별로"),
  ("panic", "control", "`panic` 은 프로그램을 즉시"), ("슬라이스", "slices", "`slice t` 는 타입"), ("array", "slices", "`array 4 u8` 은"),
  ("struct", "structs-enums", "`struct` 는 이름 붙은"), ("enum", "structs-enums", "`enum` 은 여러 갈래"),
  ("option", "option-result", "`option t` 는"), ("result", "option-result", "`result t e` 는"), ("try", "option-result", "`try` 는 `result` 를 받아"),
  ("참조", "references", "*참조*는"), ("배타 규칙", "references", "같은 값에 대한 빌림이 겹칠"),
  ("newtype", "named-types", "`newtype <이름> <타입> .` 은"), ("range", "named-types", "`range <아래> <위>` 를 적으면"),
  ("cast", "named-types", "`cast <타입> <값>` 으로"), ("영역", "regions", "영역은 `region <이름> <종류>"),
  ("고정 창", "regions", "고정 창은 운영체제가"), ("힙", "regions", "`region <이름> heap` 은"),
  ("소유", "ownership", "`owned t` 는 소유를"), ("완결", "ownership", "어떤 타입이 완결을 요구하는지"),
  ("할당기", "fixed-memory", "할당기를 이루는 셋"), ("using", "fixed-memory", "`using al a .` 은"),
  ("bit_cast", "fixed-memory", "`bit_cast`\n다"), ("모듈", "modules", "소스 파일 하나가 모듈 하나다"),
  ("export", "modules", "*감춘 것이 기본*"), ("단형화", "generics", "*단형화*"),
  ("트레이트", "traits", "`trait shape do … end` 는"), ("method", "traits", "`method <값> <이름>"),
  ("pipe", "pipe", "`filter is_digit .` ---"), ("액터", "actors", "`actor counter do … end` 가"),
  ("우편함", "actors", "`spawn send` 로 우편함에"), ("task_group", "tasks-channels", "`task_group do … end` 는"),
  ("채널", "tasks-channels", "채널은 흐름 사이에"), ("parallel", "parallel-atomic", "`parallel s split .` 이"),
  ("원자 연산", "parallel-atomic", "원자 연산은 쪼개지지"), ("기억 차례", "parallel-atomic", "`order <이름>` 을 붙여"),
  ("extern", "ffi", "`extern` op 은 몸이 C 에"), ("mmio", "hardware", "`mmio` 뒤의 수가"),
  ("인터럽트 처리기", "hardware", "`vector <번호> .` 절을"), ("build tier", "hardware", "`build tier <이름> .` 은"),
  ("pkg.low", "build-test", "프로젝트의 뿌리에 `pkg.low`"), ("build option", "build-test", "`build option <이름> <갈래> …` 이"),
  ("두 백엔드 대조", "build-test", "같은 것을\n두 가지 방법으로"), ("잎", "lib-map", "어떤 조각이 잎인지"),
  ("성숙도", "lib-map", "모듈은 저마다 *성숙도*"), ("세대 핸들", "lib-alloc", "블록 번호와 그때의 세대 수"),
  ("브랜드", "lib-alloc", "*브랜드*를 든다"), ("구간 분석", "proofs-numbers", "구간 `[lo, hi]` 로 계산해"),
  ("증명서 검산", "proofs-numbers", "*증명서 검산.*"), ("신뢰 기반", "proofs-limits", "== 신뢰 기반"),
  ("내용 주소화", "proofs-syntax-hash", "*내용 주소화*는"), ("전수 검사", "proofs-ownership", "*유계 전수 모델 검사*가"),
 ],
}

def chapter_path(lang, cid):
    reg = (ROOT / "book" / "registry.typ").read_text(encoding="utf-8")
    body = reg.split("#let parts")[1].split("#let chapter-ids")[0]
    ids = [i for i in re.findall(r'chapters: \(([^)]*)\)', body)]
    flat = [x.strip().strip('"') for group in ids for x in group.split(",") if x.strip()]
    n = flat.index(cid) + 1
    d = "book" if lang == "ko" else "book-en"
    return ROOT / d / "chapters" / f"ch{n:02d}.typ"

def main(lang):
    bad = 0
    for term, cid, needle in TERMS[lang]:
        p = chapter_path(lang, cid)
        s = p.read_text(encoding="utf-8")
        marker = f'#idx("{term}")'
        if marker in s:
            continue
        start = s.find("#chapter-questions()")
        first = needle.split("\n")[0]
        i = s.find(first, start)
        if i < 0:
            print(f"  못 찾음: {term} — {cid} — {first}"); bad += 1; continue
        ls = s.rfind("\n", 0, i) + 1
        # 표·문답·상자 안이면 그 장치의 시작 줄 앞에 둔다
        s = s[:ls] + marker + "\n" + s[ls:]
        p.write_text(s, encoding="utf-8")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "ko"))
