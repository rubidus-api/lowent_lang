#import "../lib.typ": *

= 그릇과 정렬 — `sortlib`·`sortgen`·`hashmap`·`vecgen`·`spsc`

#chapter-toc()

#prereq(
  ([#chref("generics") 제네릭], [비교는 값이 아니라 타입이 들고 온다]),
  ([#chref("fixed-memory") 할당기와 고정 메모리], [할당기를 `using` 으로 건넨다]),
  ([#chref("lib-map") 표준 라이브러리의 지도], [버퍼는 호출자의 것]),
)

#deepqa[
  #chref("generics")에서 C 의 `qsort` 는 비교 함수를 값으로 받는데 Lowent 의 `sortgen` 은 무엇으로 받는다고 했는가?
][
  비교를 *타입*이 들고 온다. 정렬할 타입이 `ordered` 트레이트를 갖추고 `less` 를 붙이면, `sort_by` 는 그 타입 전용으로 단형화되어
  비교가 직접 호출로 박힌다. 이 장은 그 정렬과 함께 표준 라이브러리의 그릇들을 둘러본다.
]

#why[
  정렬·탐색·해시맵·자라는 배열은 거의 모든 프로그램에 들어간다. 다른 언어에서 이런 그릇은 대개 몰래 힙에서 할당하고, 가득
  차면 조용히 자란다. Lowent 의 그릇은 두 갈래다. 호출자가 뒷받침 슬라이스를 건네는 *고정 그릇*(`hashmap`·`sortlib`)과, 할당기를
  `using` 으로 받아 스스로 자라는 *제네릭 그릇*(`vecgen`·`mapgen`)이다. 어느 쪽이든 메모리가 어디서 오는지가 머리에 보인다.
]

#organizer[
  `sortlib.sort` 와 `searchlib.bsearch` 로 `u64` 슬라이스를 정렬·탐색하고, `sortgen.sort_by` 로 구조체를 타입이 정한 기준으로
  정렬하는 법을 익힌다. `hashmap` 이 호출자의 슬라이스 위에서 동작하는 방식(빈칸·묘비)을 알게 된다. `vecgen` 이 할당기를 받아
  자라는 모습과 그 효과가 할당기 타입에 따라 달라지는 것을 확인한다. 락 없이 흐름 사이로 값을 넘기는 `spsc` 와 그 밖의 그릇의
  자리도 보게 된다.
]

#chapter-questions()

== `u64` 를 정렬하고 찾는다

#demo("examples/ch34/sorted.low")

- `alloc_bytes al capacity 40` 으로 40 바이트를 받고 `view_array u64` 로 `u64` 다섯 칸의 슬라이스로 본다. 할당은 여기서만
  일어난다.
- `sortlib.sort xs` 는 제자리에서 오름차순으로 정렬한다. `effects none` 이다.
- `searchlib.bsearch xs 19` 는 정렬된 슬라이스에서 19 의 자리를 `option` 으로 준다. 없으면 `none` 이다. `lower_bound` 는 넣을
  자리를 준다.

가장 작은 값 3 과 19 의 자리 2 로 32 가 나온다. `bsearch` 는 입력이 정렬되어 있다고 *믿는다*. 정렬되지 않은 슬라이스를 주면
틀린 답을 낸다. 그 믿음은 모듈 문서에 적힌 전제다.

== 타입이 기준을 들고 온다

#demo("examples/ch34/rows.low")

`score` 는 `satisfies sortgen.ordered .` 로 순서를 안다고 선언하고, `score.less` 가 "점수가 높은 것이 앞" 이라는 기준을 준다.
`sortgen.sort_by score rs` 는 그 기준으로 정렬한다. 점수 95·80·70 의 번호가 차례로 2·3·1 이다.

내림차순이나 여러 키로 정렬하고 싶으면 `less` 를 그렇게 쓰면 된다. 모드 인자를 다는 대신 타입이 뜻을 들고 온다. `sort_by` 는
삽입정렬이라 *안정*하고(같은 점수의 차례가 바뀌지 않는다) 거의 정렬된 입력에 빠르다. 큰 배열에는 `sort_fast`(제네릭 quicksort)
가 있다. `u64` 같은 스칼라는 트레이트를 갖출 수 없으므로 칸 하나짜리 구조체로 감싼다. 배치는 그대로 8 바이트다.

#qa[
  `less a a` 가 참이면 어떻게 되는가?
][
  정렬이 멈추지 않거나 틀린 차례를 낼 수 있다. `less` 는 *엄격한 약한 순서*여야 한다 --- 특히 자기 자신보다 앞일 수 없다.
  `le` 로 적어야 할 자리를 `lt` 로, 또는 그 반대로 적는 실수가 여기서 난다. 트레이트는 op 이 *있다*는 것을 검사하지 그 op 이
  수학적 성질을 지키는지까지 검사하지는 않는다. 그 성질은 모듈 문서에 적힌 계약이다.
]

== 호출자의 슬라이스 위의 해시맵

#demo("examples/ch34/table.low")

`hashmap` 은 `u64 → u64` 오픈 어드레싱 해시맵이다. 뒷받침은 호출자가 드는 `mut slice u64` 이고, `[키0+1, 값0, 키1+1, 값1, …]`
으로 배치된다. `size` 는 칸 수(`len / 2`)다. 처음에는 전부 0(빈칸)이어야 하는데, `alloc_bytes` 가 0 으로 채운 바이트를 주므로
그대로 쓴다.

- `put slots 7 700` 은 넣고, 같은 키로 다시 `put` 하면 값을 바꾼다. 가득 차면 `false` 다.
- `lookup` 은 `option` 을 준다.
- `del` 은 칸을 빈칸으로 되돌리지 않고 *묘비*로 덮는다. 빈칸으로 되돌리면 그 뒤에 이어진 탐색 사슬이 끊겨 뒤의 키를 못 찾기
  때문이다.

이 설계의 대가도 문서에 적혀 있다. 빈칸(0)과 묘비를 표시하려고 가장 큰 키 둘을 쓸 수 없고, 자동으로 자라지 않는다. 바이트열
키는 `strmap` 이, 자라는 제네릭 해시맵은 `mapgen` 이 맡는다.

== 자라는 제네릭 벡터

#demo("examples/ch34/growing.low")

- `allocs.heap_bytes` 를 띄워 할당기로 쓴다. `cap heap` 을 쥔 `main` 만 띄울 수 있다(#chref("fixed-memory")).
- `vecgen.open u32 4` 는 원소 타입 `u32`, 처음 용량 4 인 벡터를 연다. 할당기는 바인딩의 `using hb` 로 건넨다.
- `vecgen.append … v x` 는 자리가 모자라면 할당기에게 더 청해 자란다. 실패하면 `false` 다. 벡터는 자기 할당기를 이미 들고 있으므로
  `append` 에는 `using` 을 적지 않는다. 적으면 `E-ALLOC-USING-UNUSED` 로 거절된다.
- `vecgen.at … v 50` 은 50 번 원소를 `option` 으로 준다. 50 × 2 = 100 이다.

`vecgen.append` 의 효과는 `state via a` 다. 여기서 `a` 가 `heap_bytes` 이므로 이 인스턴스의 효과는 `heap state` 이고, `main` 의
머리에도 `heap` 이 선다. 같은 코드를 빌린 바이트 위의 범프로 열면 `state` 만 선다. 그릇이 운영체제 없는 기계에서 도는지를 할당기
타입이 정한다.

#misconception[자라는 벡터는 결국 숨은 할당이다][
  할당은 일어나지만 숨지 않는다. `append` 의 효과 줄이 할당기의 효과를 그대로 싣고, 부르는 op 의 머리에 `heap` 이나 `alloc` 이
  선다. 할당기가 모자라면 `append` 가 `false` 를 준다 --- 메모리 부족이 값이다. 숨은 할당은 머리에 보이지 않고 실패하면 프로그램을
  멈추는 할당이다.
]

== 흐름 사이로 값을 넘기는 링 버퍼

`spsc` 는 락 없는 단일 생산자·단일 소비자 링 버퍼다. 한 흐름이 넣고 다른 한 흐름이 뺀다. 제어 칸과 뒷받침 칸을 호출자가 건네고,
넣고 빼는 op 은 `cap atomic` 을 받아 원자 연산으로 커서를 옮긴다(#chref("parallel-atomic")). 인터럽트 처리기와 보통 코드가 값을
주고받는 자리(#chref("hardware"))에 쓰인다.

락 없는 자료구조는 드문 차례에서만 틀리기로 악명이 높다. `spsc` 의 정확성은 약한 메모리 모델에서 이미 증명된 알고리즘의 증명을
*빌려* 확인했다. 다중 생산자·다중 소비자 큐나 seqlock 은 빌릴 증명이 없어 넣지 않았다(#chref("proofs-effects-concurrency")).

== 그 밖의 그릇

#dtable(
  columns: 2,
  id: "containers-more",
  caption: [그 밖의 그릇 모듈],
  [*모듈*], [*한 줄*],
  [`strmap`], [바이트열 → `u64` 해시맵. 키 바이트도 호출자의 슬라이스에 담는다],
  [`mapgen`], [제네릭 해시맵 `table k v`. 스스로 다시 뿌린다],
  [`vecs` · `growvec`], [자라는 바이트 벡터. `growvec` 은 `vecgen.vec u8` 의 짧은 이름이다],
  [`nodelist`], [고정 크기 침입형(intrusive) 목록],
  [`soa`], [구조체 배열을 칸별 배열로 두는 배치의 실험],
)

#recap[
  `sortlib`·`searchlib` 는 `u64` 슬라이스를 제자리에서 정렬·탐색하고, `sortgen` 은 `ordered` 를 갖춘 타입이 기준을 들고 온다.
  `hashmap`·`strmap` 은 호출자의 슬라이스 위에서 묘비로 삭제하는 고정 그릇이다. `vecgen`·`mapgen` 은 할당기를 `using` 으로 받아
  자라며, 효과가 할당기 타입을 따라간다. `spsc` 는 원자 연산으로 흐름 사이에 값을 넘기고 그 정확성은 빌린 증명으로 확인했다.
]
