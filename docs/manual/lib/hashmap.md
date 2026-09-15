# hashmap — u64 → u64 해시 맵, 오픈 어드레싱 (`lib/hashmap.low`)

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** u64 열쇠로 u64 값을 넣고 찾는 **해시 맵**이다.

**언제 쓰나.** 번호로 무언가를 빨리 찾아야 할 때(아이디 → 개수, 핸들 → 상태) 쓴다.

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use hashmap .

let ok bool be hashmap.put b 7 42 .         rem 열쇠 7 에 값 42 를 넣는다
let v option u64 . be hashmap.lookup b 7 .  rem 찾으면 some 42, 없으면 none
```

**꼭 알아 둘 것 둘.** ① 맵은 스스로 메모리를 만들지 않는다 — 저장 공간(`b`)을 호출자가
마련해 매번 넘긴다. ② 새 맵은 **반드시 전부 0 으로 비워** 시작한다. 이 둘을 어기면
에러 없이 답이 틀린다.

**읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## 왜 있는가

이 모듈이 왜 언어 기능이 아니라 라이브러리인지를 적는다.

이 모듈은 **숫자 키로 값을 바로 찾을 때** 쓴다. 딕셔너리(사전: 열쇠 → 값 표)는 거의 모든
프로그램이 필요로 하는 자료구조다 — 빈도 세기·중복 제거·룩업 테이블(소스 상단 rem).
그런데 해시 맵은 **순수 계산이라 세상에 안 닿는다**(파일·시계·전역 상태를 안 만진다)
⇒ 리프(빌트인)가 아니라 **라이브러리**다. Lowent 로 쓴다. 빌트인 op 증가 **0**.

문자열 키가 필요하면 [`strmap`](strmap.md) 이 이 모듈과 같은 배치를 바이트열 키로
넓힌다.

## 설계 의도와 경계

이 모듈이 고른 방식(오픈 어드레싱)과, 그 대가로 못 하는 것들을 적는다. 대가를 알면
"왜 이 키는 안 들어가지" 같은 의문이 안 생긴다.

**하는 것** — u64 키 → u64 값. 넣기/갱신·찾기·삭제(묘비)·순회·성장(rehash).
전부 `effects none` 순수 계산이다. **오픈 어드레싱**(open addressing)이란 키마다 별도
연결 리스트를 달지 않고, 자리가 차 있으면 **같은 표의 다음 슬롯**으로 옮겨 가며 넣는
방식이다 — 버퍼 하나로 끝나 호출자-소유 규율과 맞는다. 옮겨 가며 자리를 찾는 그 동작을
**프로빙**(probing)이라 부른다.

**정직하게 안 하는 것:**

- **할당하지 않는다.** 백킹(backing: 맵의 실제 저장 공간이 되는 슬라이스)은 호출자가
  마련한다(RFC-0043 D1 — strbuf·pool 과 같은 규율).
- **자동으로 안 자란다.** 꽉 차면 `put` 이 false 를 낸다(트랩이 아니다). 자라려면
  호출자가 큰 백킹을 마련해 `rehash` 를 부른다.
- **키 둘을 못 담는다.** `MAX_U64` 와 `MAX_U64 − 1` 은 인코딩에 예약됐다(아래) —
  `put`/`lookup`/`del` 이 거절한다(false/none). 별도 상태 바이트 없이 삭제를 얻는 대가다.

## 자료구조

백킹 하나에 열쇠와 값이 번갈아 들어간다. 여기서 알아야 할 것은 "0 = 빈칸" 과
"MAX = 묘비" 두 표식이다.

백킹은 `mut slice u64` 하나다. 배치:

```
[key0+1, val0, key1+1, val1, …]     슬롯 수 N = len/2
```

- **빈칸 인코딩 = key+1**: key 슬롯에 `k+1` 을 저장한다. `0` 은 빈칸 표식 — 그래서
  **키 0 도 저장할 수 있다.**
- **묘비(tombstone) = MAX_U64**: 묘비란 "여기 항목이 있었는데 지워졌다" 는 표식이다.
  삭제한 슬롯을 빈칸으로 되돌리면 프로브 체인(충돌 때문에 뒤로 밀려 들어간 키들의 자취)이
  끊겨 그 뒤 키를 못 찾게 된다. 그래서 삭제는 key 슬롯을 `MAX_U64` 로 덮는다. `lookup`
  은 묘비를 건너뛰고, `put` 은 재사용하고, 순회는 안 센다.
- **불변식:** 처음에 백킹이 **전부 0**(빈 맵)이어야 한다(`alloc_bytes` 가 0 을 주므로
  그대로 쓴다). 해시는 곱셈 해시(황금비 상수 — 키를 골고루 흩는 계산) + 선형 프로빙
  (차 있으면 다음 슬롯으로 한 칸씩).

## op 한눈에

급할 때 이 표만 봐도 된다. 실패가 트랩이 아니라 `false`·`none` 으로 온다는 점을 눈여겨
본다.

모든 op 은 `effects none`. 부를 때는 모듈 접두사를 붙인다 — `hashmap.put` 처럼.

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `put` | proc | `(b mut slice u64, k u64, v u64) → bool` | 꽉 참·예약 키 → false |
| `lookup` | proc | `(b slice u64, k u64) → option u64` | 없음·예약 키 → none |
| `del` | proc | `(b mut slice u64, k u64) → bool` | 없던 키·예약 키 → false |
| `size` | fn | `(b slice u64) → u64` | — |
| `occupied_at` | fn | `(b slice u64, slot u64) → bool` | 범위 밖 → false |
| `key_at` | fn | `(b slice u64, slot u64) → u64` | occupied 아닐 때 뜻 없음 |
| `val_at` | fn | `(b slice u64, slot u64) → u64` | 범위 밖 → 0 |
| `rehash` | proc | `(nb mut slice u64, ob slice u64) → bool` | nb 가 작으면 도중 false |

## op 상세

**매개변수를 왜 받나 — 이 모듈의 규약.** `b` 하나가 맵 전체다 — 칸들이 (열쇠, 값) 쌍으로
나란히 누워 있는 `slice u64` 이고, 모듈은 그것 말고 아무 상태도 갖지 않는다. 그래서 같은 맵을
여러 op 에 계속 넘긴다. 칸 수는 `len b / 2` 로 **길이가 곧 용량**이라, 용량을 바꾸려면
더 큰 슬라이스를 마련해 `rehash` 로 옮기는 수밖에 없다 — 자동으로 커지지 않는 것이 이 계층의
약속이다(숨은 할당 0).


op 하나하나를 시그니처와 함께 본다. 매개변수마다 "왜 이것이 필요한가" 를 한 줄로 붙였다 —
모든 op 이 백킹 `b` 를 다시 받는 것이 눈에 띌 텐데, 맵이 자기 저장 공간을 안 들기
때문이다.

#### put
```lowent
export proc put input b mut slice u64 . . input k u64 . input v u64 . output bool . effects none .
```
넣거나 갱신한다.

- `b` — 맵의 저장 공간. 맵이 백킹을 소유하지 않으므로 상태가 전부 인자로 온다. 쓰기를
  하므로 `mut` 이다.
- `k` — 열쇠.
- `v` — 값.
- true=성공 · false=꽉 참 또는 `k ≥ MAX−1`(예약 상단 둘).
- 같은 키면 제자리 갱신이다(새 슬롯을 안 쓴다). 프로브 중 처음 만난 묘비를 기억해 뒀다가,
  키가 뒤에 없음이 확인되면(빈칸·한 바퀴) 거기 넣는다 — **묘비 재사용**으로 클러스터
  (연달아 찬 구간)가 안 자란다.

#### lookup
```lowent
export proc lookup input b slice u64 . input k u64 . output option u64 . effects none .
```
키 `k` 의 값 또는 `none` 을 낸다.

- `b` — 뒤질 맵. 백킹을 안 바꾸므로 비소유(`mut` 아님) slice 로 충분하다.
- `k` — 찾을 열쇠.
- 묘비는 건너뛰고 **빈칸에 닿아야 "없음"** 이다. 이름이 `get` 이 아닌 이유는
  [주의사항](#주의사항) 참조.

#### del
```lowent
export proc del input b mut slice u64 . . input k u64 . output bool . effects none .
```
키를 지운다.

- `b` — 지울 맵. 묘비를 써 넣으므로 `mut` 이다.
- `k` — 지울 열쇠.
- true=지웠다 · false=없던 키. 슬롯을 **묘비로 덮어** 프로브 체인을 보존한다(빈칸으로
  되돌리면 그 뒤 키를 못 찾는다). 값 슬롯은 0 으로 지운다.

#### size · occupied_at · key_at · val_at — 순회
```lowent
export fn size input b slice u64 . output u64 .
export fn occupied_at input b slice u64 . input slot u64 . output bool .
export fn key_at input b slice u64 . input slot u64 . output u64 .
export fn val_at input b slice u64 . input slot u64 . output u64 .
```
맵 전체를 훑을 때 쓰는 넷이다. 순회 = `0 .. size` 를 훑으며 `occupied_at` 인 슬롯만
처리한다.

- `b` — 훑을 맵. 읽기만 하므로 전부 비소유다.
- `slot` — **몇 번째 슬롯을 보는가**. 순회 루프의 인덱스를 그대로 넣는다. 맵이 자기
  커서를 안 들기 때문에 "지금 어디" 는 호출자가 든다.
- `size` 는 슬롯 개수(= `len b / 2`)다 — 들어 있는 항목 수가 아니다.
- `occupied_at` 은 빈칸도 묘비도 아닐 때 true 다. `key_at` 은 **디코드된 키**(저장값 − 1)
  를 낸다. `key_at`/`val_at` 은 occupied 일 때만 뜻이 있다.
- 순서는 슬롯(해시) 순서지 삽입 순서가 아니다.

#### rehash — 성장
```lowent
export proc rehash input nb mut slice u64 . . input ob slice u64 . output bool .
```
꽉 찼거나 묘비가 쌓인 맵을 더 큰 백킹으로 옮긴다.

- `nb` — **새 백킹**. 맵은 스스로 할당 못 하므로 호출자가 큰 것을 마련해 넘긴다. 처음에
  0(빈 맵)이어야 한다.
- `ob` — 옛 백킹. 읽기만 하므로 비소유다 — 실패해도 원본은 온전하다.
- `ob` 의 **살아있는 항목만** `nb` 에 넣는다. true=전부 옮겼다 · false=`nb` 가 작아 도중
  실패. 묘비를 거르므로 **압축도 한다**(묘비가 사라진다).

## 사용법과 예제

기본 흐름은 "백킹을 0 으로 비우고 → `put` 으로 넣고 → `lookup` 으로 찾고 → 필요하면
슬롯을 훑는다" 이다. 아래 예제는 `impl/tests/vm_hashmap.low` 의 통과 코드를 근거로 한다.

`lib/hashmap.low` 의 모듈 이름은 `hashmap` 이다.

```lowent
use hashmap from "../../lib/hashmap.low" .   rem 경로 직접 지정
use hashmap .                                rem from-생략 — 예약(std) 해소
use hashmap as hm .                          rem 별칭 관례 — hm.put …
```

넣고·갱신하고·꺼내고·순회한다(픽스처 `vm_hashmap.low` 의 `hm_check`·`hm_sum_vals`
기반). 백킹은 호출자가 마련하고 **먼저 0 으로 비운다**:

```lowent
module demo .

use hashmap from "../../lib/hashmap.low" .

proc hm_demo input b mut slice u64 . . output u64 . do
  var i u64 be 0 .
  while lt i (len b) . do                       rem 빈 맵으로 시작 — 백킹을 전부 0 으로
    set (index b i) 0 .                         rem 0 = 빈칸 표식이다
    set i (add i 1) .
  end
  guard ge (div (len b) 2) 4 . else return 0 .  rem 슬롯 4 개는 있어야 시험이 된다
  guard eq (hashmap.put b 10 100) true . else return 0 .      rem 새 키 — true 를 꼭 본다
  guard eq (hashmap.put b 20 200) true . else return 0 .      rem 새 키
  guard eq (hashmap.put b 20 222) true . else return 0 .      rem 같은 키 = 제자리 갱신
  let g option u64 . be hashmap.lookup b 20 .   rem 찾기 — 결과는 option 이다
  guard is_some g . else return 0 .             rem none 이면 여기서 떠난다
  guard eq (some_value g) 222 . else return 0 . rem 갱신된 값이 나온다
  guard eq (hashmap.del b 10) true . else return 0 .          rem 삭제 → 슬롯이 묘비가 된다
  rem 순회 — 살아있는 항목의 값 합(묘비·빈칸 제외)
  var slot u64 be 0 .                           rem 슬롯 번호는 내가 든다
  var total u64 be 0 .                          rem 누산 변수(빌트인 sum 과 안 겹치게)
  while lt slot (hashmap.size b) . do           rem 슬롯 0 .. N 을 전부 훑는다
    if hashmap.occupied_at b slot . do          rem ★ 살아있는 슬롯만 — 묘비를 거른다
      set total (add total (hashmap.val_at b slot)) .   rem 그 슬롯의 값을 더한다
    end
    set slot (add slot 1) .
  end
  return total .                                rem 222 (10 은 지워졌다)
end
```

맵이 꽉 찼을 때 키우는 흐름(`vm_hashmap.low` 의 `hm_rehash_check` 기반):

```lowent
proc grow input ob mut slice u64 . . input nb mut slice u64 . . output u64 . effects none . do
  rem ob·nb 를 0 으로 비우는 루프는 위와 같다(생략)
  guard eq (hashmap.put ob 10 100) true . else return 0 .
  guard eq (hashmap.put ob 20 200) true . else return 0 .
  guard eq (hashmap.put ob 30 300) false . else return 0 .   rem 2 슬롯이 꽉 찼다 → false
  guard eq (hashmap.rehash nb ob) true . else return 0 .     rem ★ 큰 자리는 내가 마련한다
  guard eq (hashmap.put nb 30 300) true . else return 0 .    rem 이제 들어간다
  let a option u64 . be hashmap.lookup nb 10 .               rem 옛 키도 따라왔다
  guard is_some a . else return 0 .
  return some_value a .                                      rem 100
end
```

VM 으로 돌릴 때 슬라이스 인자는 `[a,b,c]` 로 준다([1장](../01-build-and-run.md)):

```sh
$ build/lowentc --run hm_demo demo.low [0,0,0,0,0,0,0,0]
```

## 반례 — 이렇게 쓰면 안 된다

실제로 자주 밟는 잘못된 코드들이다. 각 반례에 **증상**을 적었다. 이 모듈은 트랩을 거의
안 내므로, 증상이 "조용히 틀린 답" 인 경우가 많다는 점을 특히 눈여겨본다.

**✗ 백킹을 안 비우고 시작:**

```lowent
proc f input b mut slice u64 . . output u64 . effects none . do
  guard eq (hashmap.put b 10 100) true . else return 0 .   rem ✗ b 를 0 으로 안 비웠다
```

증상: 트랩도 오류도 없다 — **조용히 틀린 답**이다. 쓰레기 값이 든 슬라이스는 "이미 뭔가
들어 있는 맵" 으로 읽히므로, 넣은 적 없는 키가 `lookup` 에 잡히거나 프로브가 엉켜
방금 넣은 키가 `none` 으로 나온다. 알아채는 법: 같은 코드가 실행할 때마다 다르게
동작한다(빌린 버퍼의 내용에 따라 달라지기 때문이다). 새 맵은 반드시 전부 0 에서 시작한다.

**✗ 예약 키를 넣기:**

```lowent
let maxk u64 be bit_not 0 .                                      rem MAX_U64 를 만든다
guard eq (hashmap.put b maxk 7) false . else return 0 .          rem MAX → false
guard eq (hashmap.put b (sub maxk 1) 7) false . else return 0 .  rem MAX−1 도 false
```

증상: `put` 이 **false** 를 낸다(트랩 아님). `k+1` 인코딩이 0(빈칸)·MAX(묘비)와 겹치면
안 되므로 최상단 키 둘은 못 담는다. 알아채는 법: 반환 bool 을 안 보면 값이 조용히
사라지고, 나중에 `lookup` 이 `none` 을 낸다.

**✗ `occupied_at` 없이 슬롯을 읽기:**

```lowent
while lt slot (hashmap.size b) . do
  set total (add total (hashmap.val_at b slot)) .   rem ✗ 빈칸·묘비도 함께 더한다
  set slot (add slot 1) .
end
```

증상: 에러 없이 **합계가 틀린다** — 빈칸/묘비 슬롯의 `key_at`·`val_at` 은 뜻 없는
값이기 때문이다(빈칸의 `key_at` 은 `0 − 1` 언더플로까지 간다). 알아채는 법: 넣은 값의
합보다 큰 수가 나오거나 터무니없이 큰 수가 섞인다. 순회는 언제나
`if hashmap.occupied_at b slot . do … end` 로 거른다.

**✗ `put` 의 false 를 안 보기:**

```lowent
let dummy bool be hashmap.put b 30 300 .   rem ✗ 반환값을 안 본다
let g option u64 . be hashmap.lookup b 30 .
return some_value g .                       rem 못 넣었으면 여기서 터진다
```

증상: 꽉 찬 맵은 트랩 대신 false 를 내므로 그 줄은 조용히 지나가고, 나중에 `lookup` 이
`none` 을 내며 검사 없는 `some_value` 에서 **E-VM-NONE 트랩(패닉)** 으로 멈춘다.
멈추는 자리가 원인에서 멀어 원인 찾기가 어려워진다 — `guard eq (hashmap.put …) true .`
가 습관이어야 한다.

## 주의사항

초보자가 자주 걸리는 함정을 모았다. 대부분 "실패가 값으로 온다" 와 "저장 공간은 호출자
것" 에서 나온다.

- **op 은 모듈 접두사로 부른다** — `hashmap.put`·`hashmap.lookup`. 옛 `put` 같은
  `hm_` 접두사 이름은 없다.
- **`get` 이 아니라 `lookup`, `capacity` 가 아니라 `size` 인 이유** — `get` 은 페이로드
  enum 해체 낱말(RFC-0080)이고 `capacity` 는 `alloc_bytes … capacity n` 의 문법
  낱말이라, 언어 낱말과 겹치는 이름은 op 이 가질 수 없다. 자기 코드의 변수 이름에서도
  같은 충돌을 만난다.
- **`size` 는 슬롯 개수다.** 들어 있는 항목 수가 아니다 — 항목 수가 필요하면 순회하며
  `occupied_at` 을 센다.
- **`put` 의 false 를 무시하지 않는다.** 꽉 찬 맵은 트랩하지 않고 false 로 말한다 —
  못 넣은 값은 어디에도 없다.
- **묘비는 쌓인다.** 넣고 지우기를 반복하면 묘비가 프로브 길이를 늘린다(빈칸이 안
  나와서 탐색이 길어진다). 묘비가 많아지면 같은 크기라도 `rehash` 로 옮겨 **압축**한다.
- **rehash 는 새 백킹이 0 이어야 한다** — 재사용하는 백킹이면 먼저 비운다. 옛 백킹은
  불변이므로 rehash 실패(false) 후에도 원본은 온전하다.
- 순회 순서는 해시 순서다 — 삽입 순서·키 크기 순서가 아니다. 정렬된 열거가 필요하면
  키를 뽑아 [`sortlib`](sortlib.md)로 정렬한다.
- 백킹 길이가 홀수면 마지막 u64 하나는 그냥 안 쓰인다(N = len/2 내림).

---

[← 목차](../README.md)
