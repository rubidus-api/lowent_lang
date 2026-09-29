#import "../lib.typ": *

= `vecs` --- 호출자가 버퍼를 드는 성장 바이트 벡터 <mod-vecs>

#modhead(file: "lib/vec.low", layer: [L1 --- 얼로케이터를 `using` 으로], caps: [없음 --- 얼로케이터를 건네받는다])

자리가 모자라면 *더 큰 자리를 받아 옮기는* 바이트 벡터다. 얼마나 커질지 미리 모르는 바이트열을 모을 때 쓴다. 없는 것은 재할당이지 성장이 아니다 --- 더 큰 자리를
받아 옛 내용을 옮기면 되고, 그것은 Lowent 로 쓸 수 있다. 그래서 빌트인이 아니라 라이브러리이고, 성장 정책(두 배)도 라이브러리의 몫이다.

#dtable(
  columns: 3,
  id: "mod-vecs-vs-vecgen",
  caption: [`vecs` 와 `vecgen` --- 다른 것은 소유다],
  [], [*`vecs`*], [*#modref("vecgen")[`vecgen`]*],
  [저장소], [*호출자가 든다* --- op 에 버퍼를 넘긴다], [컨테이너가 소유한다],
  [성장], [새 자리를 *값으로 돌려준다*(호출자가 다시 묶는다)], [안에서 갈아 끼운다],
  [얼로케이터], [부를 때마다 `using` 으로 건넨다], [필드로 들고 있다],
)

region 안에서 버퍼 수명을 *직접* 쥐고 싶을 때가 `vecs` 다. 벡터는 `vec_u8`(길이 `len` 하나)과 `mut slice u8`(저장소) *두 값의 쌍*으로 산다. 용량은 따로 들지 않는다
--- `len buf` 가 곧 용량이다. 불변식 `v.len ≤ len buf` 를 push 가 유지한다.

#dtable(
  columns: 3,
  id: "mod-vecs-ops",
  caption: [`vecs` 의 op],
  [*op*], [*모양*], [*실패*],
  [`vec_u8`], [구조체 --- `len u64`], [---],
  [`next_cap`], [`fn (cur u64) → u64` --- 0 이면 8, 아니면 두 배], [없음],
  [`push_byte`], [`proc (comptime a, using al a, v mut vec_u8, buf mut slice u8, x u8) → option (mut slice u8)`, `effects state via a`, `requires allocs.byte_allocator a`], [OOM 이면 `none` --- 이때 `v` · `buf` 는 그대로],
)

`push_byte` 는 자리가 있으면 그대로 쓰고 `some buf` 를, 꽉 찼으면 `send al reserve (next_cap (len buf))` 로 두 배를 받아 옛 내용을 복사한 뒤 `some <새 버퍼>` 를
돌려준다. 얼로케이터는 위치 인자가 아니라 `using` 절로 건넨다 --- 타입 `a` 는 그 출처에서 채워지고 단형화되므로 전달 비용은 0 이다(#chref("lib-alloc")). 몰래
할당하지 않으므로 얼로케이터를 가지지 않은 코드는 이 op 을 부를 수 없다.

```lowent
module vecgrow .
use vecs .
use allocs .

proc main input al cap allocator . input out cap io . output u8 . effects io alloc state . do
  let memopt be option mut slice u8 alloc_bytes al capacity 256 .
  guard is_some memopt . else return 1 .
  let mem be mut slice u8 some_value memopt .
  var bump be allocs.bump_bytes spawn actor allocs.bump_bytes . .
  var c be u64 send bump init mem .
  let b0 be option mut slice u8 send bump reserve 2 .
  guard is_some b0 . else return 2 .
  var buf be mut slice u8 some_value b0 .
  var v be lit vecs.vec_u8 do len 0 . end .
  var i be u64 0 .
  while lt i 10 . do
    let r using bump be option mut slice u8 vecs.push_byte v buf (narrow u8 (add 65 i)) .
    guard is_some r . else return 3 .
    set buf (some_value r) .
    set i (add i 1) .
  end
  let m be u64 write_out out 1 (subslice buf 0 (field v len)) .
  return narrow u8 (field v len) .
end
```

2 바이트로 시작해 10 개를 밀며 2 → 4 → 8 → 16 으로 세 번 자라고, `ABCDEFGHIJ` 를 출력한다.

#antipattern[돌려받은 버퍼로 다시 묶지 않는다][
  `set buf (some_value r) .` 를 빼먹으면 컴파일도 실행도 통과하는데 결과가 소리 없이 틀린다. 성장이 일어난 뒤 옛 `buf` 를 계속 주면 `v.len` 은 새 버퍼 기준으로
  올라가 있는데 쓰기는 옛(작은) 버퍼에 간다. *반환값이 곧 다음 버퍼다* --- 이 모듈에서 가장 위험한 실수다.
]

#antipattern[OOM 을 보지 않는다 · 순수 fn 에서 부른다][
  `some_value r` 를 검사 없이 꺼내면 얼로케이터가 말랐을 때 `E-VM-NONE` 으로 멈춘다. `effects none` 인 `fn` 에서 부르면 `E-EFFECT-CALC` 다 --- `push_byte` 는
  `effects state` 인 proc 이다. trait 을 충족하지 않는 값(예: `u64`)을 출처로 건네면 `E-BOUND-UNSAT` 이다.
]

*주의.* 성장은 복사다 --- push 한 번이 최악 O(len)이지만 두 배 정책이라 상환 O(1)이다. 크기를 미리 알면 처음부터 그만큼 받는 쪽이 싸다. *옛 버퍼는 회수되지 않는다*
--- 범프 위에서 n 까지 키우면 뒷받침 소비는 대략 2n 이고, 옛 버퍼는 `region` 이 끝날 때 통째로 걷힌다(#chref("regions")). 머리와 버퍼는 쌍이다 --- 서로 다른 벡터의
것을 섞어 넘기는 것을 막을 장치는 없다. 원소 타입 일반화 · pop · insert · remove · 줄어들기는 없다.
