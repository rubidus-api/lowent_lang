#import "../lib.typ": *

= 입출력과 파일

#chapter-toc()

#prereq(
  ([#chref("capabilities") 권한], [`cap io`·`cap file_system` 과 `effects io`]),
  ([#chref("ownership") 소유], [완결이 필요한 값은 적어서 끝낸다]),
  ([#chref("errors-design") 실패를 설계하기], [경계에서는 `result` 로 말한다]),
)

#deepqa[
  #chref("ownership")에서 어떤 타입이 "완결이 필요하다" 는 것은 무엇으로 정해졌는가?
][
  그 타입을 `owned` 로 받아 `result` 를 돌려주는 op 이 있으면 정해졌다. 그런 값을 끝내지 않고 범위를 벗어나게 두면
  `E-OWN-INCOMPLETE` 로 거절되었다. 이 장에서 그 규칙이 가장 쓸모 있는 자리 --- 열었으면 반드시 닫아야 하는 파일 --- 을 본다.
]

#why[
  제8부는 프로그램이 바깥 세계와 닿는 자리들이다. 첫째가 입출력이다. 입출력은 실패가 흔한 자리다 --- 파일이 없고, 디스크가
  가득 차고, 읽다가 끊긴다. 그리고 가장 흔한 결함은 실패를 *끝*과 섞는 것, 닫기를 잊는 것이다. Lowent 는 권한으로 누가
  입출력을 할 수 있는지 정하고, 소유로 닫기를 잊지 못하게 하며, 답의 모양으로 끝과 실패를 가른다. 앞의 네 부에서 세운
  도구들이 한자리에 모인다.
]

#organizer[
  표준출력에 쓰는 `write_out` 과 표준 라이브러리 `files` 로 파일을 여닫고 읽고 쓰는 법을 익힌다. 파일 핸들이 완결이 필요한
  소유 값이라서 닫기를 잊으면 번역이 거절된다는 것을 확인한다. 읽기의 답이 "읽었다·끝이다·실패다" 세 자리로 갈리는 까닭과,
  실패를 일부러 일으켜 시험하는 방법도 알게 된다.
]

#chapter-questions()

== 표준출력

표준출력에 쓰는 통로는 `write_out <cap io> <fd> <바이트>` 하나다. 파일 기술자 1 은 표준출력, 2 는 표준오류다. 쓴 바이트
수를 돌려준다. 이미 여러 장에서 썼으므로 새로울 것은 없지만, 한 가지를 짚는다. 이 op 이 권한을 *첫 피연산자*로 받는다는
것은, 표준출력에 한 바이트라도 내는 모든 op 의 머리에 `cap io` 가 보인다는 뜻이다. 디버그용으로 몰래 찍는 출력이 없다.

== 파일을 통째로 읽는다

#demo("examples/ch28/lines.low")

- `main` 은 권한 셋을 받는다. 출력(`cap io`), 파일(`cap file_system`), 버퍼를 받을 할당(`cap allocator`)이다. 머리만
  보고도 이 프로그램이 네트워크에 닿지 않는다는 것을 안다.
- `files.slurp fs "notes.txt" buf` 는 파일 전체를 `buf` 에 읽고 읽은 바이트 수를 `result u64 files.file_error` 로 준다.
  파일이 없거나 버퍼보다 크면 오류다. 자르지 않는다.
- 오류면 메시지를 쓰고 종료 코드 2 를 돌려준다. 경계에서 실패를 다루는 모양이다(#chref("errors-design")).
- 읽은 부분을 `subslice` 로 잘라 `pipe` 로 줄바꿈을 세고(#chref("pipe")), 내용을 그대로 표준출력에 쓴다.

예제는 자기 폴더의 `notes.txt` 를 읽는다. 세 줄이므로 종료 코드는 3 이다.

#qa[
  버퍼보다 큰 파일은 어떻게 읽는가?
][
  `files.open` 으로 열고 `files.read` 를 되풀이한다. `read` 는 버퍼만큼만 읽으므로 파일 크기와 상관없이 조각으로 읽을 수 있다.
  짧게 읽히는 것은 실패가 아니다. 부탁한 것보다 적게 주는 것은 정상이고, 부르는 쪽이 계속 읽어야 한다. `slurp` 이 안에서
  그렇게 한다.
]

== 열었으면 닫는다

#demo("examples/ch28/copy.low")

- `files.open fs <경로> 1` 은 쓰기 모드로 연다. 모드는 0 읽기, 1 쓰기(자르고 만들기), 2 덧붙이기다.
- 성공하면 `ok_value o` 를 `owned files.handle` 에 담는다.
- `files.write` 로 쓰고 `files.close fs h` 로 닫는다. `close` 는 `owned handle` 을 받고 `result` 를 돌려준다. 닫기는 진짜로
  실패할 수 있다 --- 네트워크 파일 시스템이나 가득 찬 디스크에서 마지막 버퍼를 비우지 못하면 닫기에서 드러난다.

`close` 가 그 모양이므로 `handle` 은 완결이 필요한 타입이다. 닫지 않고 두면 거절된다.

#demo("examples/ch28/forgot.low")

닫기를 잊은 프로그램은 번역되지 않는다. 운영체제가 프로세스 종료 때 파일 기술자를 닫아 준다는 기대에 기대지 않는다. 정말로
닫지 않고 버리려면 `drop h .` 로 버린다고 적는다.

#misconception[파일 핸들을 정수로 들고 다니면 더 가볍다][
  커널이 준 정수를 그대로 들면 실행 비용은 같다. `files.handle` 도 안에 정수 하나뿐인 구조체다. 차이는 번역이 무엇을 알 수
  있느냐다. 정수는 복사해도, 잊어도, 두 번 닫아도 아무도 모른다. 소유 값은 옮겨지고, 잊으면 거절되고, 두 번 닫으면 거절된다.
  같은 바이트에 규칙을 입힌 것이다.
]

== 끝과 실패는 다른 답이다

`files.read` 의 답은 세 자리다.

#dtable(
  columns: 2,
  id: "io-read",
  caption: [`read` 의 답],
  [*답*], [*뜻*],
  [`ok (some n)`], [`n` 바이트를 읽었다],
  [`ok none`], [파일의 *끝*이다. 실패가 아니다],
  [`error e`], [실패다. `e` 는 어느 연산이 실패했는지 말한다],
)

한때 이 모듈의 `slurp` 은 읽기가 실패하면 반복을 멈추기만 했다. 그러면 실패한 읽기가 "파일을 다 읽었다" 로 보고되었고,
이 모듈로 지은 줄 세기 프로그램이 읽기 실패를 "0 줄" 이라는 성공으로 냈다. 값 하나가 두 뜻(끝과 실패)을 나르면 안 된다는
것이 그때 얻은 교훈이고, 그래서 답이 세 자리가 되었다.

반대로 모든 op 에 세 자리를 기계적으로 씌우지도 않았다. 여는 일에는 "끝" 이 없으므로 `open` 은 `result handle file_error`
두 자리다. 메모리 위의 슬라이스를 읽는 `io` 모듈의 리더는 실패할 자리가 없으므로 `option` 만 쓴다. op 마다 정직한 모양이
다르다.

== 실패를 일부러 일으킨다

파일 실패는 평소에 잘 일어나지 않아서, 실패를 다루는 코드는 시험되지 않은 채 남기 쉽다. 표준 라이브러리의 호스트 연산에는
결함 주입기가 있다. 환경 변수로 켠다.

```text
LOW_HOST_FAULT="open:err"        모든 open 이 실패한다
LOW_HOST_FAULT="read:err@2"      두 번째 읽기가 실패한다
LOW_HOST_FAULT="read:short@1=4"  첫 읽기를 4 바이트로 자른다
LOW_HOST_FAULT="close:err"       닫기가 실패한다
```

VM 과 네이티브가 같은 주입기를 쓰므로 두 백엔드의 답이 같아야 한다. `LOW_HOST_FAULT="open:err" lowentc --run main lines.low`
로 돌리면 `lines.low` 는 오류 갈래로 가서 메시지를 쓰고 2 를 돌려준다. 기본은 꺼져 있다.

#realcase[줄 세기 프로그램이 찾아낸 결함][
  위의 "0 줄" 결함은 주입기가 실물로 보였다. `LOW_HOST_FAULT="read:err@1"` 로 첫 읽기를 실패시키자 줄 세기가 성공으로 끝났다.
  실패 경로가 한 번도 돌지 않았다면 이 결함은 실제 디스크 오류가 날 때까지 숨어 있었을 것이다. 권한과 소유가 "닫기를 잊음"
  을 번역에서 막는다면, 주입기는 "실패를 잘못 다룸" 을 실행에서 드러낸다.
]

== 흔한 실수

#antipattern[읽기 모드로 열고 쓴다 --- 쓴 바이트 수를 보지 않는다][
  #demo("examples/ch28/mistake_readmode.low")

  `files.open fs "notes.txt" 0` 은 읽기 모드다. 거기에 쓰면 실패해야 하는데, 이 판에서는 `files.write` 가 오류 대신 `ok 0`("0 바이트
  썼다")을 돌려준다(개발 저장소에 결함으로 적어 두었다). `is_ok w` 만 물었다면 성공으로 지나갔을 것이다. 이 예제는 쓴 수가 11 인지 확인해서
  종료 코드 3 으로 드러낸다. 모드를 확인하고(0 읽기 · 1 쓰기 · 2 덧붙이기), 쓰기의 답은 성공 여부와 함께 *쓴 수*까지 본다. 짧게 쓰인 것은
  남은 바이트를 다시 써야 한다는 뜻이다.
]

#antipattern[닫은 핸들로 또 쓴다][
  #demo("examples/ch28/mistake_afterclose.low")

  `files.close` 는 `owned handle` 을 받으므로 부르는 순간 소유가 넘어간다. 그 뒤의 `h` 는 이미 없는 핸들이라 `E-OWN-MOVED` 다. C 에서
  `fclose` 뒤의 `fwrite` 는 정의되지 않은 동작이고, 같은 번호가 새로 연 다른 파일에 재사용되었다면 엉뚱한 파일에 쓴다. 소유가 그 자리를
  번역에서 막는다.
]

#antipattern[읽기의 답에서 실패만 묻는다][
  #demo("examples/ch28/mistake_eof.low")

  `files.read` 의 답은 세 자리다. `is_ok r` 는 "실패가 아니다" 만 말한다. 파일 끝의 `ok none` 도 실패가 아니므로 통과하고, 그 안에서
  `some_value` 를 꺼내다 멈춘다. 세 자리를 모두 가른다.

  #demo("examples/ch28/eof_fixed.low")

  16 바이트 버퍼로 34 바이트 파일을 세 번에 나누어 읽고, 넷째 읽기의 `ok none` 에서 멈춘다. 실패하면 닫고 3 을, 닫기가 실패하면 4 를
  돌려준다. `rounds` 의 상한은 끝을 영영 알리지 않는 원천에서도 반복이 끝나게 한다.
]

#misconception[버퍼보다 큰 파일을 `slurp` 하면 앞부분만 읽힌다][
  #demo("examples/ch28/slurp_small.low")

  8 바이트 버퍼에 34 바이트 파일을 `slurp` 하면 앞 8 바이트가 오는 것이 아니라 오류가 온다. 잘린 내용을 온전한 파일로 믿는 결함 --- 설정
  파일의 뒷부분이 조용히 사라지는 일 --- 을 막으려는 것이다. 파일이 버퍼보다 클 수 있으면 `files.open` 과 `files.read` 로 조각씩 읽는다.
]

== 이 장의 문법 한눈에

#dtable(
  columns: 3,
  id: "io-files-glance",
  caption: [입출력과 파일의 문법 --- 모양 · 뜻 · 왜 이렇게 생겼나],
  [*모양*], [*뜻*], [*왜 이렇게*],
  [`write_out out 1 "…"`], [표준출력(1) · 표준오류(2)에 쓴다 --- 쓴 수를 준다], [권한이 첫 피연산자 --- 몰래 찍는 출력이 없다],
  [`use files .` + `input fs cap file_system .`], [파일 모듈과 그 권한], [머리만 보고 파일에 닿는지 안다],
  [`files.open fs "notes.txt" 0`], [연다 --- 0 읽기 · 1 쓰기 · 2 덧붙이기 · `result handle file_error`], [여는 일에는 "끝" 이 없다 --- 두 자리],
  [`var h owned files.handle be ok_value o .`], [핸들을 소유로 담는다], [잊으면 `E-OWN-INCOMPLETE` · 닫은 뒤 쓰면 `E-OWN-MOVED`],
  [`files.read fs h buf`], [`ok (some n)` 읽었다 · `ok none` 끝 · `error e` 실패], [값 하나가 두 뜻을 나르지 않는다],
  [`files.write fs h bytes`], [쓴 수를 `result` 로 준다], [짧게 쓰일 수 있다 --- 수를 확인한다],
  [`files.close fs h`], [`owned` 로 받아 `result` --- 완결], [닫기도 실패할 수 있다],
  [`files.slurp fs path buf`], [통째로 읽는다 --- 버퍼보다 크면 오류], [자르지 않는다],
  [`LOW_HOST_FAULT="read:err@2"`], [실패를 일부러 일으킨다(환경 변수)], [실패 경로를 시험한다],
)

#recap[
  표준출력은 `write_out <cap io> <fd> <바이트>` 이고 권한이 첫 피연산자다. `files` 모듈은 `cap file_system` 을 받아 파일을 여닫고
  읽고 쓴다. 파일 핸들은 `close` 가 `owned` 로 받아 `result` 를 돌려주므로 완결이 필요한 값이고, 닫기를 잊으면 번역이 거절한다.
  `read` 의 답은 읽었다·끝이다·실패다 세 자리이며, 실패 경로는 `LOW_HOST_FAULT` 주입기로 일부러 돌려 본다.
]
