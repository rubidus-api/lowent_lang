#import "../lib.typ": *

= `lifemode` · `lifeatom` --- 값이 언제 끝나는가 <mod-lifemode>

#modhead(file: "lib/lifemode.low · lib/lifeatom.low", layer: [L1 --- 호출자의 저장], caps: [`lifeatom` 에 `cap atomic`])

값의 끝을 정하는 방법은 넷이고, 넷 다 값이 다르다. 새 낱말도 새 내장 연산도 늘지 않았다 --- 언어가 이미 알던 것을 쓰기 좋게 묶을 뿐이다.

#dtable(
  columns: 3,
  id: "mod-lifemode-modes",
  caption: [값의 끝을 정하는 네 방법],
  [*모드*], [*무엇으로 쓰나*], [*값*],
  [① 주인이 하나], [`owned` --- 낱말 하나면 끝이다(#chref("ownership"))], [셈이 없다],
  [② 스레드에 국한된 참조 셈], [`lifemode.near_*` --- 그냥 더하기], [원자 명령 0],
  [③ 원자적으로 공유하는 참조 셈], [`lifeatom.rc_*` + `cap atomic`], [원자 명령이 있다],
  [④ 바깥의 완료], [완료권을 `owned` 표로], [셈이 없다, 대신 컴파일러가 센다],
)

```lowent
guard lifemode.near_open c 0 . else return 1 .
let a be option u64 lifemode.near_share c 0 .
let b be option u64 lifemode.near_drop c 0 .
rem 0 을 답하면 마지막이었다 --- 그때 부른 쪽이 저장을 거둔다
```

원자 쪽은 모듈이 다르다 --- `lifeatom.rc_open` · `rc_share k c` · `rc_release k c`(`k` = `cap atomic`). `rc_release` 가 0 을 답하면 마지막이었다. ④ 는 라이브러리가
아니라 모양이다 --- `fn finish input t owned mymod.ticket . output result u64 mymod.late .` 처럼 완료가 완료권을 *먹게* 하면, 두 번 완료하면 `E-OWN-MOVED`, 완료를 잊으면
(완료가 실패할 수 있을 때) `E-OWN-INCOMPLETE` 다. *정확히 한 번* 이 라이브러리가 아니라 언어에서 나온다.

#dtable(
  columns: 2,
  id: "mod-lifemode-ops",
  caption: [`lifemode` · `lifeatom` 의 op],
  [*op*], [*하는 일*],
  [`lifemode.ceiling`], [두 모드가 함께 보는 셈의 상한],
  [`lifemode.near_open` · `near_share` · `near_drop`], [국한 셈: 1 로 열기 · 하나 늘리기 · 하나 줄이기(0 = 마지막)],
  [`lifeatom.rc_open` · `rc_share` · `rc_release`], [원자 셈: 같은 셋, `cap atomic` 을 받는다],
)

*왜 모듈이 둘인가 --- `use` 는 부르는 것이 아니라 내보내는 것만큼 치른다.* 처음에는 넷을 한 모듈에 넣었다. 그랬더니 국한 셈만 쓰는 프로그램의 바이너리에 `lock`
접두 명령이 2 개 남았다. `export` 인 op 은 C ABI 래퍼로 전역 심볼을 얻으므로 링커가 버리지 못한다. 원자를 `lifeatom` 으로 가르자 0 개가 됐다. 원자를 들이는 값이
`use lifeatom` 한 줄에 보인다.

*넘침 --- 포화하고, 그 객체는 영원히 산다.* 그냥 원자 덧셈으로 세면 상한에서 조용히 0 으로 감긴다(네이티브 실행으로 확인했다). 감긴 수는 이르게 0 에 닿아 살아 있는
객체를 해제한다 --- 해제 후 사용으로 가는 길이다. 그래서 이 모듈은 *포화한다*(Linux `refcount_t` 가 고른 답과 같다). 상한에 닿으면 더 늘리지도 줄이지도 않는다. 그
객체는 죽지 않지만 메모리 안전은 깨지지 않는다. 값은 명령 수 약 19 % 증가이고, 원자 쪽 `lock` 수는 시도당 하나로 같다(CAS 고리라 경합할 때만 돈다).

*짓지 않은 것* --- 진짜 비동기 완료(뒷받침 없는 표면은 만들지 않는다), 완료 콜백(호출 시점과 효과를 감춘다), 약한 참조, 순환 수집.
