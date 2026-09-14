# strmap — 문자열 키 해시 맵, 바이트열 → u64 (`lib/strmap.low`)

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 바이트열(문자열) 열쇠로 u64 값을 넣고 찾는 **해시 맵**이다.

**언제 쓰나.** 단어 세기, 이름 → 번호 표처럼 열쇠가 글자열일 때 쓴다.

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use strmap .

let ok bool be strmap.put slots keys "apple" 1 .
let v option u64 . be strmap.lookup slots keys "apple" .
```

**읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## 왜 있는가

이 모듈은 **문자열을 키로 값을 찾을 때** 쓴다 — 진짜 딕셔너리다: **단어 세기·심볼
테이블·설정 맵**(소스 상단 rem). u64 키 해시맵([`hashmap`](hashmap.md))을 가변 길이
**바이트열 키**로 넓힌 것이다. 순수 계산이라 세상에 안 닿는다 ⇒ 리프(빌트인)가 아니라
**라이브러리** — Lowent 로 쓴다. 빌트인 op 증가 **0**.

## 설계 의도와 경계

**하는 것** — 바이트열(문자열) 키 → u64 값. 넣기/갱신·찾기·삭제(tombstone)·순회·
성장(rehash). 키 바이트는 넣을 때 아레나로 **복사**되므로 원본 문자열의 수명에
매이지 않는다. 전부 `effects none`.

**정직하게 안 하는 것:**

- **할당하지 않는다.** 슬롯 표와 키 아레나, 두 버퍼를 호출자가 든다(RFC-0043 D1 —
  strbuf·hashmap 과 같은 규율).
- **빈 문자열 키는 금지.** `keylen 0` 이 빈칸 표식과 겹치므로 `put` 이 거절한다(false).
- **아레나를 회수하지 않는다.** bump 아레나다(커서를 앞으로만 미는 가장 단순한 할당) —
  같은 키를 갱신하면 아레나를 더 안 쓰지만, 지운 키의 바이트는 남는다(문서화된 한계).
  회수는 `rehash` 가 새 아레나로 옮기며 한다.
- **자동으로 안 자란다.** 슬롯·아레나가 모자라면 false — 트랩이 아니다. 구조가 못 자라면
  그렇게 말한다.

## 자료구조

버퍼 둘을 호출자가 마련한다. 처음에 `slots` 는 **전부 0**(빈 맵)이어야 한다.

- **`slots` (`mut slice u64`)** — 슬롯 표. **`slots[0]` 은 arena 커서**(키 바이트가 얼마나
  쓰였나). 진짜 슬롯은 u64 인덱스 1 부터, 슬롯 하나 = **3 u64** `[keyoff, keylen, value]`.
  슬롯 수 `N = (len(slots) − 1) / 3` — 쓸모 있으려면 `len(slots) ≥ 4`(슬롯 1 개 이상).
- **`keys` (`mut slice u8`)** — 키 바이트 **아레나**(arena: 여러 키의 바이트를 한 버퍼에
  이어 담는 저장소). 넣을 때 키를 여기 복사한다(커서 전진).

`keylen` 의 세 가지 뜻: `0` = 빈칸 · `MAX_U64`(18446744073709551615) =
**묘비**(tombstone: "여기 있었는데 지워졌다" 표식 — 아래 `del` 참조) · 그 밖 = 살아있는
키의 길이. 키는 실존 길이라 MAX 일 수 없으므로 예약 없이 묘비값이 된다.
해시는 FNV-1a(64비트) + 선형 프로빙(자리가 차 있으면 다음 슬롯으로 한 칸씩 이동).

## op 한눈에

모든 op 은 `effects none`. 부를 때는 모듈 접두사를 붙인다 — `strmap.put` 처럼.

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `put` | proc | `(slots mut slice u64, keys mut slice u8, k slice u8, v u64) → bool` | 꽉 참·arena 부족·빈 키 → false |
| `lookup` | proc | `(slots slice u64, keys slice u8, k slice u8) → option u64` | 없음·빈 키 → none |
| `del` | proc | `(slots mut slice u64, keys slice u8, k slice u8) → bool` | 없던 키·빈 키 → false |
| `size` | fn | `(slots slice u64) → u64` | `len(slots) < 4` → 0 |
| `keylen_at` | fn | `(slots slice u64, slot u64) → u64` | 범위 밖 → 0 |
| `occupied_at` | fn | `(slots slice u64, slot u64) → bool` | 범위 밖 → false |
| `keyoff_at` | fn | `(slots slice u64, slot u64) → u64` | 범위 밖 → 0 |
| `val_at` | fn | `(slots slice u64, slot u64) → u64` | 범위 밖 → 0 |
| `rehash` | proc | `(ns mut slice u64, na mut slice u8, os slice u64, oa slice u8) → bool` | ns/na 가 작으면 도중 false |

## op 상세

**매개변수를 왜 받나 — 이 모듈의 규약.** 열쇠가 글자열이라 자리가 둘 필요하다:
`slots` 는 (해시·오프셋·길이·값) 같은 고정 크기 칸들이고, `keys` 는 **열쇠 바이트가 쌓이는**
아레나다. 그래서 op 마다 둘을 함께 넘긴다. 열쇠를 칸 안에 넣지 않는 이유는 열쇠 길이가
제각각이기 때문이고, 아레나에 쌓기만 하고 지우지 않는 이유는 지운 열쇠의 자리를 되찾으려면
압축이 필요한데 그 비용을 몰래 치르지 않기 위해서다.


#### put
```lowent
export proc put input slots mut slice u64 . . input keys mut slice u8 . . input k slice u8 . input v u64 .
  output bool . effects none .
```
넣거나 갱신한다. `slots`·`keys` 를 매번 받는 이유: 맵이 버퍼를 소유하지 않으므로
상태가 전부 인자로 온다. true=성공 · false=꽉 참·arena 부족·빈 키. 같은 키(길이+바이트
일치)면 값만 제자리 갱신 — 새 슬롯도 아레나도 안 쓴다. 새 키는 아레나에 복사되고
`[keyoff, keylen, value]` 가 슬롯에 적힌다. 프로브 중 처음 만난 **묘비를 재사용**한다
(키가 뒤에 없음이 확인된 뒤) — 삭제로 생긴 구멍을 도로 메운다.

#### lookup
```lowent
export proc lookup input slots slice u64 . input keys slice u8 . input k slice u8 . output option u64 . effects none .
```
키 `k` 의 값 또는 `none`. 저장된 키와의 비교는 길이부터, 그다음 바이트별. 버퍼를 안
바꾸므로 둘 다 비소유로 받는다. 이름이 `get` 이 아닌 이유는 [주의사항](#주의사항) 참조.

#### del
```lowent
export proc del input slots mut slice u64 . . input keys slice u8 . input k slice u8 . output bool . effects none .
```
키를 지운다. true=지웠다 · false=없던 키·빈 키. 슬롯의 keylen 을 **묘비(MAX)** 로 덮어
프로브 체인을 보존하고(빈칸으로 되돌리면 그 뒤로 밀려 들어간 키를 못 찾는다) value 는
0 으로 지운다. 아레나의 옛 키 바이트는 남는다.

#### size · keylen_at · occupied_at · keyoff_at · val_at — 순회
```lowent
export fn size input slots slice u64 . output u64 .
export fn keylen_at input slots slice u64 . input slot u64 . output u64 .
export fn occupied_at input slots slice u64 . input slot u64 . output bool .
export fn keyoff_at input slots slice u64 . input slot u64 . output u64 .
export fn val_at input slots slice u64 . input slot u64 . output u64 .
```
순회 = `0 .. size` 를 훑으며 occupied 슬롯을 처리한다. `slot` 인자는 "몇 번째 슬롯을
보는가" — 순회 루프의 인덱스를 그대로 넣는다. `occupied_at` 은 빈칸(0)도 묘비(MAX)도
아닐 때 true — **삭제를 쓰는 코드의 occupancy 판별은 이것**이다(삭제가 없으면
`keylen > 0` 으로도 충분하다 — 묘비가 안 생기므로). 키 바이트는
`keys[keyoff .. keyoff+keylen]`, 즉 `subslice keys off (add off klen)` 으로 읽는다.
접근자는 범위 밖 slot 에 0/false 를 낸다(트랩 아님). 순서는 슬롯(해시) 순서다.

#### rehash — 성장
```lowent
export proc rehash input ns mut slice u64 . . input na mut slice u8 . . input os slice u64 . input oa slice u8 . output bool .
```
맵은 스스로 할당 못 하므로, 호출자가 더 큰 두 버퍼(`ns` 슬롯·`na` 아레나)를 마련해
옛 버퍼(`os`·`oa`)와 함께 넘긴다 — 네 인자가 그래서 필요하다. old 의 **살아있는
항목만** new 에 넣는다(키 바이트는 na 로 새로 복사된다). `ns` 는 처음에 0(빈 맵)이어야
한다. true=전부 옮겼다 · false=ns/na 가 작아 도중 실패. 묘비와 아레나의 죽은 바이트를
거르므로 **압축도 한다.** old 는 안 바꾸므로 비소유.

## 사용법과 예제

`lib/strmap.low` 의 모듈 이름은 `strmap` 이다.

```lowent
use strmap from "../../lib/strmap.low" .   rem 경로 직접 지정
use strmap .                               rem from-생략 — 예약(std) 해소
use strmap as sm .                         rem 별칭 관례 — sm.put …
```

넣고·갱신하고·지우고·순회한다(픽스처 `impl/tests/vm_strmap.low` 기반):

```lowent
module demo .

use strmap from "../../lib/strmap.low" .

proc tally input slots mut slice u64 . . input keys mut slice u8 . . output u64 . do
  var i u64 be 0 .
  while lt i (len slots) . do                  rem 빈 맵으로 시작 — 슬롯 표를 전부 0 으로
    set (index slots i) 0 .
    set i (add i 1) .
  end
  guard eq (strmap.put slots keys "apple" 1) true . else return 0 .      rem 새 키
  guard eq (strmap.put slots keys "banana" 2) true . else return 0 .     rem 새 키
  guard eq (strmap.put slots keys "banana" 22) true . else return 0 .    rem 같은 키 = 갱신
  guard eq (strmap.del slots keys "apple") true . else return 0 .        rem 삭제 → 묘비
  let b option u64 . be strmap.lookup slots keys "banana" .
  guard is_some b . else return 0 .            rem 있어야 한다
  guard eq (some_value b) 22 . else return 0 . rem 갱신된 값
  rem 순회 — 살아있는 항목만 센다(묘비·빈칸 제외)
  var slot u64 be 0 .
  var cnt u64 be 0 .
  while lt slot (strmap.size slots) . do       rem 슬롯 0 .. N 을 전부 훑는다
    if strmap.occupied_at slots slot . do      rem 살아있는 슬롯만
      set cnt (add cnt 1) .
    end
    set slot (add slot 1) .
  end
  return cnt .                                 rem 1 (banana 만 남았다)
end
```

실전 예는 `impl/tests/prog/wordfreq.low` — 파일을 읽고 [`strings`](strings.md) 의
`split_next` 로 단어를 쪼개 `lookup`/`put` 으로 빈도를 세고, 순회로 최다 빈도를
찾는다. 버퍼는 `alloc_bytes`(전부 0 으로 온다)로 마련하고 `view_array u64` 로 바이트
버퍼를 u64 슬롯으로 무복사 재해석한다.

## 반례 — 이렇게 쓰면 안 된다

**✗ 빈 문자열 키:**

```lowent
guard eq (strmap.put slots keys "" 7) true . else return 0 .   rem ✗ 항상 false 로 떨어진다
```
증상: `put` 이 언제나 **false** 를 낸다(트랩·오류 아님). `keylen 0` 은 빈칸 표식이라
빈 키는 인코딩상 존재할 수 없다. `lookup ""` 도 언제나 `none` 이다.

**✗ 삭제 후 `keylen > 0` 으로 순회:** 묘비의 keylen 은 MAX 라 `gt keylen 0` 이 **참**이다
— 증상: 지운 항목이 개수·집계에 섞여 든다(조용히 틀린 답). `del` 을 한 번이라도 쓴 맵의
순회는 `occupied_at` 으로 거른다.

**✗ 슬롯 표를 안 비우고 시작 / `slots[0]` 을 직접 만지기:** 쓰레기가 든 slots 는 이미
찬 맵으로 읽히고, arena 커서(`slots[0]`)를 손대면 다음 put 이 키 바이트를 엉뚱한 곳에
복사한다. 증상: 넣은 적 없는 키가 찾히거나, 넣은 키가 `none` 으로 나온다 — 트랩이
아니라 조용히 틀린 답이다. 초기화는 "전부 0", 이후 슬롯 표 수정은 op 을 통해서만.

## 주의사항

- **op 은 모듈 접두사로 부른다** — `strmap.put`·`strmap.lookup`. 옛 `put` 같은
  `sm_` 접두사 이름은 없다.
- **`get` 이 아니라 `lookup`, `capacity` 가 아니라 `size` 인 이유** — `get` 은 페이로드
  enum 해체 낱말(RFC-0080)이고 `capacity` 는 `alloc_bytes … capacity n` 의 문법
  낱말이라, 언어 낱말과 겹치는 이름은 op 이 가질 수 없다. 자기 코드의 이름에서도 같은
  충돌을 만난다(아래 마지막 항목).
- **아레나는 bump 다.** 같은 키 갱신은 공짜지만, 서로 다른 키를 넣었다 지웠다 반복하면
  아레나가 죽은 바이트로 찬다 — 슬롯이 남아도 arena 부족으로 put 이 false 가 될 수 있다.
  그때는 같은 크기라도 `rehash` 로 새 아레나에 **압축**한다.
- **묘비도 쌓인다**(hashmap 과 같다) — 프로브가 길어지면 rehash 로 청소한다.
- **버퍼 크기 짐작:** 슬롯 N 개 = `3N + 1` u64, 아레나는 살아있는 키 길이 합 + 죽은 바이트.
  `put` 의 false 하나로 "슬롯 부족"과 "아레나 부족"이 구별되지는 않는다.
- **키는 복사된다.** 넣은 뒤 원본 문자열 버퍼가 바뀌어도 맵은 무관하다. 반대로 순회에서
  꺼낸 키 뷰(`subslice keys …`)는 **아레나의 뷰**다 — 아레나가 살아 있는 동안만 유효하다.
- 변수 이름이 빌트인 op(`sum`·`ok`·`max` 등)과 겹치면 진단이 어긋날 수 있다 —
  픽스처는 `total`·`okc` 같은 이름을 쓴다(`impl/tests/vm_strmap.low` 상단 rem).

---

[← 목차](../README.md)
