#import "../lib.typ": *

= `hashmap` --- `u64` → `u64` 해시 맵 <mod-hashmap>

#modhead(file: "lib/hashmap.low", layer: [L0 --- 순수 계산(호출자의 뒷받침)], caps: [없음])

`u64` 열쇠로 `u64` 값을 넣고 찾는 해시 맵이다. 번호로 무언가를 빨리 찾아야 할 때(아이디 → 개수, 핸들 → 상태) 쓴다. 문자열 열쇠는 #modref("strmap")[`strmap`], 원소
타입과 얼로케이터를 고르는 컨테이너는 #modref("mapgen")[`mapgen`] 이다(#chref("lib-containers")).

```lowent
let stored be bool hashmap.put b 7 42 .
let v be option u64 hashmap.lookup b 7 .
```

#aside[꼭 알아 둘 것 둘][
  ① 맵은 스스로 메모리를 만들지 않는다 --- 저장 공간 `b` 를 호출자가 마련해 매번 넘긴다. ② 새 맵은 *반드시 전부 0 으로* 시작한다. 이 둘을 어기면 에러 없이 답이
  틀린다 --- 쓰레기 값이 든 슬라이스는 "이미 뭔가 들어 있는 맵" 으로 읽혀, 넣은 적 없는 키가 찾히거나 방금 넣은 키가 `none` 으로 나온다.
]

*오픈 어드레싱.* 키마다 연결 리스트를 달지 않고, 자리가 차 있으면 같은 표의 다음 슬롯으로 옮겨 가며 넣는다(선형 프로빙) --- 버퍼 하나로 끝나 호출자 소유 규율과
맞는다. 백킹은 `mut slice u64` 하나이고 `[key0+1, val0, key1+1, val1, …]` 로 눕는다(슬롯 수 N = `len / 2`). key 슬롯에 `k+1` 을 저장하므로 `0` 이 빈칸 표식이 되고
*키 0 도 저장할 수 있다*. 삭제는 key 슬롯을 `MAX_U64` 인 *묘비*로 덮는다 --- 빈칸으로 되돌리면 충돌로 뒤에 밀려 들어간 키들의 자취가 끊겨 그 뒤 키를 못 찾게 된다.
`lookup` 은 묘비를 건너뛰고, `put` 은 재사용하고, 순회는 세지 않는다. 해시는 곱셈 해시(황금비 상수)다.

#dtable(
  columns: 3,
  id: "mod-hashmap-ops",
  caption: [`hashmap` 의 op --- 모두 `effects none`],
  [*op*], [*모양*], [*실패*],
  [`put`], [`proc (b mut slice u64, k u64, v u64) → bool`], [꽉 참 · 예약 키 → `false`],
  [`lookup`], [`proc (b slice u64, k u64) → option u64`], [없음 · 예약 키 → `none`],
  [`del`], [`proc (b mut slice u64, k u64) → bool`], [없던 키 · 예약 키 → `false`],
  [`size`], [`fn (b slice u64) → u64`], [슬롯 수(항목 수가 아니다)],
  [`occupied_at`], [`fn (b slice u64, slot u64) → bool`], [범위 밖 → `false`],
  [`key_at` · `val_at`], [`fn (b slice u64, slot u64) → u64`], [occupied 일 때만 뜻이 있다],
  [`rehash`], [`proc (nb mut slice u64, ob slice u64) → bool`], [`nb` 가 작으면 도중 `false`],
)

- `put` 은 같은 키면 제자리 갱신이다. 프로브 중 처음 만난 묘비를 기억해 뒀다가 키가 뒤에 없음이 확인되면 거기 넣는다 --- 묘비 재사용으로 클러스터가 자라지 않는다.
- *자동으로 자라지 않는다.* 꽉 차면 `put` 이 `false` 를 낸다(멈추지 않는다). 자라려면 호출자가 큰 백킹을 0 으로 마련해 `rehash nb ob` 를 부른다. 살아 있는 항목만
  옮기므로 묘비도 사라진다(압축). 옛 백킹은 읽기만 하므로 실패해도 온전하다.
- *키 둘을 담지 못한다.* `MAX_U64` 와 `MAX_U64 − 1` 은 인코딩에 예약됐다 --- 별도 상태 바이트 없이 삭제를 얻는 대가다.
- 순회는 `0 .. size` 를 훑으며 `occupied_at` 인 슬롯만 처리한다. 순서는 해시 순서다 --- 정렬된 열거가 필요하면 키를 뽑아 #modref("sortlib")[`sortlib`] 로 정렬한다.

```lowent
proc hm_sum input b mut slice u64 . output u64 . effects none . do
  var i be u64 0 .
  while lt i (len b) . do
    set (idx b i) 0 .
    set i (add i 1) .
  end
  guard ge (div (len b) 2) 4 . else return 0 .
  guard eq (hashmap.put b 10 100) true . else return 0 .
  guard eq (hashmap.put b 20 222) true . else return 0 .
  guard eq (hashmap.del b 10) true . else return 0 .
  var slot be u64 0 .
  var total be u64 0 .
  while lt slot (hashmap.size b) . do
    if hashmap.occupied_at b slot . do
      set total (add total (hashmap.val_at b slot)) .
    end
    set slot (add slot 1) .
  end
  return total .
end
```

#antipattern[`occupied_at` 없이 슬롯을 읽는다][
  빈칸 · 묘비의 `key_at` · `val_at` 은 뜻 없는 값이라(빈칸의 `key_at` 은 `0 − 1` 까지 간다) 에러 없이 합계가 틀린다. 순회는 언제나 `occupied_at` 으로 거른다.
]

#antipattern[`put` 의 `false` 를 보지 않는다][
  꽉 찬 맵은 멈추는 대신 `false` 를 내므로 그 줄은 조용히 지나가고, 나중에 `lookup` 이 `none` 을 내어 검사 없는 `some_value` 에서 `E-VM-NONE` 으로 멈춘다. 멈추는
  자리가 원인에서 멀다 --- `guard eq (hashmap.put …) true .` 가 습관이어야 한다.
]

*주의.* op 이름이 `get` 이 아니라 `lookup`, `capacity` 가 아니라 `size` 인 이유 --- `get` 은 페이로드 enum 해체 낱말이고 `capacity` 는 `alloc_bytes … capacity n` 의
문법 낱말이라 op 이 가질 수 없다. 넣고 지우기를 되풀이하면 묘비가 쌓여 프로브가 길어진다 --- 같은 크기라도 `rehash` 로 옮겨 압축한다. 백킹 길이가 홀수면 마지막
u64 하나는 쓰이지 않는다.
