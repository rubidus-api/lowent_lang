#import "../lib.typ": *

= `strmap` --- 문자열 키 해시 맵 <mod-strmap>

#modhead(file: "lib/strmap.low", layer: [L0 --- 순수 계산(호출자의 뒷받침)], caps: [없음])

바이트열(문자열) 열쇠로 `u64` 값을 넣고 찾는 해시 맵이다 --- 단어 세기 · 심볼 테이블 · 설정 맵. #modref("hashmap")[`hashmap`] 을 가변 길이 바이트열 키로 넓힌 것이다.
키 바이트는 넣을 때 아레나로 *복사*되므로 원본 문자열의 수명에 매이지 않는다.

```lowent
let stored be bool strmap.put slots keys "apple" 1 .
let v be option u64 strmap.lookup slots keys "apple" .
```

*버퍼 둘을 호출자가 마련한다.* 처음에 `slots` 는 전부 0 이어야 한다.

- `slots`(`mut slice u64`) --- 슬롯 표. `slots[0]` 은 *아레나 커서*(키 바이트가 얼마나 쓰였나)이고, 진짜 슬롯은 인덱스 1 부터 슬롯 하나 = u64 셋 `[keyoff, keylen, value]` 다. 슬롯 수 N = `(len slots − 1) / 3`.
- `keys`(`mut slice u8`) --- 키 바이트 아레나. 여러 키의 바이트를 한 버퍼에 이어 담는다(커서를 앞으로만 미는 bump).

`keylen` 은 세 뜻이다 --- `0` = 빈칸, `MAX_U64` = 묘비, 그 밖 = 살아 있는 키의 길이. 실제 키 길이는 MAX 일 수 없으므로 예약 없이 묘비 값이 된다. 해시는 FNV-1a 64 +
선형 프로빙이다.

#dtable(
  columns: 3,
  id: "mod-strmap-ops",
  caption: [`strmap` 의 op --- 모두 `effects none`],
  [*op*], [*모양*], [*실패*],
  [`put`], [`proc (slots mut slice u64, keys mut slice u8, k slice u8, v u64) → bool`], [꽉 참 · 아레나 부족 · 빈 키 → `false`],
  [`lookup`], [`proc (slots slice u64, keys slice u8, k slice u8) → option u64`], [없음 · 빈 키 → `none`],
  [`del`], [`proc (slots mut slice u64, keys slice u8, k slice u8) → bool`], [없던 키 · 빈 키 → `false`],
  [`size`], [`fn (slots slice u64) → u64`], [`len slots < 4` → 0],
  [`occupied_at`], [`fn (slots, slot u64) → bool`], [범위 밖 → `false`],
  [`keylen_at` · `keyoff_at` · `val_at`], [`fn (slots, slot u64) → u64`], [범위 밖 → 0],
  [`rehash`], [`proc (ns mut slice u64, na mut slice u8, os slice u64, oa slice u8) → bool`], [`ns` · `na` 가 작으면 도중 `false`],
)

- `put` 은 같은 키(길이 + 바이트 일치)면 값만 갱신한다 --- 새 슬롯도 아레나도 쓰지 않는다. 새 키는 아레나에 복사된다. 프로브 중 처음 만난 묘비를 재사용한다.
- `del` 은 keylen 을 묘비로 덮고 value 를 0 으로 지운다. *아레나의 옛 키 바이트는 남는다* --- 회수는 `rehash` 가 새 아레나로 옮기며 한다(압축).
- 키 바이트는 `subslice keys off (add off klen)` 으로 읽는다. 그 뷰는 아레나의 뷰라 아레나가 살아 있는 동안만 유효하다.

#antipattern[빈 문자열 키][
  `strmap.put slots keys "" 7` 은 언제나 `false` 다. `keylen 0` 이 빈칸 표식이라 빈 키는 인코딩상 존재할 수 없다. `lookup ""` 도 언제나 `none` 이다.
]

#antipattern[삭제 뒤 `keylen > 0` 으로 순회한다][
  묘비의 keylen 은 MAX 라 `gt keylen 0` 이 참이다 --- 지운 항목이 집계에 섞여 든다. `del` 을 한 번이라도 쓴 맵은 `occupied_at` 으로 거른다.
]

#antipattern[`slots[0]` 을 직접 만진다][
  아레나 커서를 손대면 다음 `put` 이 키 바이트를 엉뚱한 곳에 복사한다. 멈추지 않고 조용히 틀린다. 초기화는 "전부 0", 이후 수정은 op 으로만 한다.
]

*주의.* 아레나는 bump 다 --- 서로 다른 키를 넣었다 지웠다 되풀이하면 슬롯이 남아도 아레나 부족으로 `put` 이 `false` 가 될 수 있다. `put` 의 `false` 하나로 슬롯
부족과 아레나 부족이 구별되지는 않는다. 버퍼 크기는 슬롯 N 개 = `3N + 1` u64, 아레나는 살아 있는 키 길이 합 + 죽은 바이트로 짐작한다. 실제 쓰임은 파일을 읽어
#modref("strings")[`strings`] 로 단어를 쪼개고 빈도를 세는 프로그램이다(#chref("lib-containers")).
