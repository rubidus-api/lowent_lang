// 장의 순서와 이름을 정하는 *단 하나의 자리*.
//
// 장 번호는 여기에 적지 않는다 --- 이 목록에서의 위치가 곧 번호다.
// 원고는 `#chref("id")` 로 가리킨다.
// 파일 이름 규칙: 이 목록의 N 번째 장은 `chapters/chNN.typ` 이다.

#let parts = (
  (ko: "제1부 — 시작",
   en: "Part I — Getting started",
   intro: "part01",
   chapters: ("intro", "first-program", "surface",)),
  (ko: "제2부 — 값과 흐름",
   en: "Part II — Values and flow",
   intro: none,
   chapters: ("numbers", "ops", "locals", "control", "expr",)),
  (ko: "제3부 — 데이터",
   en: "Part III — Data",
   intro: none,
   chapters: ("slices", "structs-enums", "option-result", "references", "named-types",)),
  (ko: "제4부 — 계약과 효과",
   en: "Part IV — Contracts and effects",
   intro: "part04",
   chapters: ("contracts", "effects", "capabilities", "errors-design",)),
  (ko: "제5부 — 메모리",
   en: "Part V — Memory",
   intro: none,
   chapters: ("regions", "ownership", "fixed-memory",)),
  (ko: "제6부 — 추상",
   en: "Part VI — Abstraction",
   intro: none,
   chapters: ("modules", "generics", "traits", "pipe",)),
  (ko: "제7부 — 동시성",
   en: "Part VII — Concurrency",
   intro: none,
   chapters: ("actors", "tasks-channels", "parallel-atomic",)),
  (ko: "제8부 — 바깥 세계",
   en: "Part VIII — The outside world",
   intro: none,
   chapters: ("io-files", "ffi", "hardware", "build-test",)),
  (ko: "제9부 — 표준 라이브러리",
   en: "Part IX — The standard library",
   intro: "part09",
   chapters: ("lib-map", "lib-text", "lib-containers", "lib-alloc", "lib-io-net", "lib-terminal",)),
  (ko: "제10부 — 근거: 무엇이 증명되었나",
   en: "Part X — Grounds: what has been proven",
   intro: "part10",
   chapters: ("proofs-why", "proofs-numbers", "proofs-ownership", "proofs-effects-concurrency", "proofs-syntax-hash", "proofs-limits",)),
)

// 읽기 순서대로 펼친 장 id --- 위치가 곧 장 번호다.
#let chapter-ids = parts.map(p => p.chapters).flatten()

// id → 장 번호.
#let chapter-no = {
  let d = (:)
  for (i, id) in chapter-ids.enumerate() { d.insert(id, i + 1) }
  d
}
