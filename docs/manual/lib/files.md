# files — 파일·디렉터리 스트림, close 망각은 컴파일 오류

소스: `lib/file.low` · 모듈명 `files`

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 파일과 디렉터리를 여닫고 읽고 쓰는 모듈이다.

**언제 쓰나.** 파일 내용을 읽을 때, 디렉터리를 훑을 때 쓴다. 모두 `cap file_system`(파일 권한)이 있어야 한다.

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use files .

rem 파일 전체를 buf 로. 없거나 buf 보다 크면 none 이다(자르지 않는다).
let n option u64 . be files.slurp fs "data.txt" buf .
```

**읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## 답은 세 자리다 (2026-08-15)

```
read  → result (option u64) file_error
        ok (some n)  n 바이트를 읽었다
        ok none      **끝**이다 (실패가 아니다)
        error e      실패다 — 그리고 **어느 연산이** 실패했는지 말한다
```

**기계적으로 감싸지 않았다.** op 마다 정직한 모양이 다르다:

| op | 답 | 왜 |
|---|---|---|
| `open`·`open_dir` | `result X file_error` | 여는 일에는 *끝* 이 없다 — 열리거나 실패다 |
| `read`·`read_dir` | `result (option u64) file_error` | 가운데 자리가 **끝**이다 |
| `write`·`seek_to`·`slurp` | `result u64 file_error` | *끝* 이라는 개념이 없다 |
| `type_of`·`is_dir`·`is_file` | **안 바뀜**(`option`/`bool`) | `file_type` 잎이 *없음* 과 *stat 실패* 를 **한 비트**로 준다 — 없는 구별을 타입으로 약속하지 않는다 |

`file_error` 의 변형도 **구별할 수 있는 것만**이다: 잎이 errno 를 안 주므로 정직한 해상도는
*어느 연산이 실패했나* 까지이고, 거기에 라이브러리가 **스스로 세는** `buffer_too_small` 이 더해진다.

## 실패와 EOF 는 다른 답이다 (2026-08-15)

`read` 는 **`ok none` 을 파일 끝**으로, **`error` 를 실패**로 답한다(2026-08-15 이전에는
`some 0` 과 `none` 이었다 — 같은 구별을 두 자리 안에 우겨 넣던 모양이다). 둘을 섞으면
안 된다 — 2026-08-15 까지 `slurp` 이 정확히 그것을 섞고 있었다: 읽기가 실패하면 루프를 멈출
뿐이어서, 호출자는 **완독과 중도 실패를 구별할 수 없었고** `wc` 가 읽기 실패를 "0 lines" 라는
성공으로 냈다. 지금은 실패면 `slurp` 이 `none` 을 답한다(핸들은 그래도 닫는다 — 자원 반납은
답과 별개다).

★ **짧은 읽기는 실패가 아니다.** `read` 가 부탁한 것보다 적게 주는 것은 정상이고, 부르는 쪽이
**계속 읽어야** 한다. `slurp` 이 그렇게 한다.

## 실패를 직접 불러 보기

```
LOW_HOST_FAULT="open:err"        모든 open 이 실패한다
LOW_HOST_FAULT="read:err@2"      두 번째 읽기가 실패한다
LOW_HOST_FAULT="read:short@1=4"  첫 읽기를 4 바이트로 자른다
LOW_HOST_FAULT="close:err"       닫기가 실패한다
```

VM 과 방출 네이티브가 **같은 주입기**를 쓰므로 두 등의 답이 같아야 한다 — 골든이 그것을 요구한다.
기본은 꺼짐이다.


## 왜 있는가

쉬운 요약: 파일을 다루는 일은 커널에 부탁하는 일이고, 열었으면 반드시 닫아야 한다. 이
모듈은 그 부탁을 대신 해 주면서 **닫기를 잊으면 컴파일이 안 되게** 만든다.

이 모듈은 **디스크의 파일을 열고 읽고 쓰고, 디렉터리에 무엇이 있는지 물을 때** 쓴다.

파일 스트림이다(RFC-0069 §6). 리프 규칙(SPEC-002 부록 P)대로 **리프는 얇고 owned 는
여기 산다**: 빌트인(`file_open`·`file_read`·`file_write`·`file_seek`·`file_close`·
`dir_open`·`dir_read`·`dir_close`·`file_type`)은 커널에 닿을 뿐 정수 핸들만 안다.
핸들(handle)은 "열린 파일을 가리키는 표" 다 — 커널이 준 정수(fd)를 그대로 들면 닫는
것을 잊어도 아무도 모르므로, 이 모듈이 **잊을 수 없는 값**(`owned handle` — 정해진
op 으로 끝내야만 하는 소유 값)으로 감싼다. 그 감싸기는 Lowent 로 쓸 수 있으므로
라이브러리다. `slurp`(파일 통째 읽기)은 빌트인 `read_file` 의 부채 상환이다 — 편의는
리프 자격이 아니었고, 스트림 리프가 서자 Lowent 로 다시 쓸 수 있게 됐다.

**cap 이 왜 필요한가.** 모든 op 이 첫 인자로 **`cap file_system`** 을 요구한다.
cap(capability·능력)은 *무엇을 할 권한을 나타내는 값*이다 — 안 들고 있으면 그 일을 못
한다. 다른 언어에서는 어느 함수 안에서든 파일을 열 수 있다. Lowent 에는 그런 **전역
권한이 없다**: "파일시스템에 닿을 권한" 은 프로그램이 시작될 때 `main` 의 입력으로 딱 한
번 건네지고, 그 값을 **넘겨받은 op 만** 파일을 만질 수 있다. 이것을 주변 권한(ambient
authority — "어디서든 그냥 되는" 암묵 권한)이 없다고 말한다.

초보자에게 실질적으로 무슨 뜻인가. ① 내 proc 안에서 파일을 읽고 싶으면 그 proc 의
시그니처에도 `input fs cap file_system .` 을 적고, 부르는 쪽에서 자기 것을 넘겨줘야 한다
— 권한은 위에서 아래로만 흐른다. ② 반대로 시그니처에 그런 인자가 없는 op 은, 아무리 긴
코드라도 파일을 못 건드린다는 것이 **읽지 않고도** 보장된다. ③ cap 은 정적 토큰이라
struct 에 담을 수 없으므로, 라이브러리가 핸들 속에 권한을 숨겨둘 수도 없다.

## 설계 의도와 경계

쉬운 요약: 연 것은 반드시 닫는다(안 닫으면 컴파일 오류), 끝(EOF)은 실패가 아니고,
못 여는 것 같은 진짜 실패는 `none`/`error` 로 값이 되어 돌아온다.

- **close 를 잊으면 컴파일이 안 된다.** `close`/`close_dir` 가 `owned` 를 받고
  `result` 를 내므로, 핸들을 조용히 버리면 **E-OWN-INCOMPLETE**(RFC-0058)다. 그리고
  닫기는 진짜로 실패한다(NFS·디스크 가득) — `result` 가 장식이 아니다.
- **0 = EOF / 끝** (실패가 아니라 사실). 못 여는 것 등 실패는 `none` — 트랩이 아니다.
- 파일 핸들과 디렉터리 핸들은 **다른 타입**이다. 둘 다 정수 인덱스지만 섞으면 뜻이
  없으므로 타입으로 가른다.
- `read_dir` 는 readdir 그대로라 `.`·`..` 도 낸다 — 거르는 것은 프로그램의 몫(raw 를
  숨기지 않는다).
- 파일시스템 **변경**(dir_make·path_remove·path_rename)은 상태 없는 경로→bool 리프라
  여기 감싸지 않는다(감싸면 순수 전달 = 동의어 금지 §2.5 위반). 프로그램이 직접 부른다.
- 안 지은 것: `wait`(중단 가능 IO). 소켓은 [`net`](net.md) 이 맡는다.

## 자료구조

```lowent
export enum file_error .
  open_failed .
  read_failed .
  write_failed .
  seek_failed .
  close_failed .
  buffer_too_small .
end .

rem 잊을 수 없는 파일 핸들. 스칼라만 담는다.
export struct handle
  fd u64 .
end

rem 디렉터리 핸들 — 파일 핸들과 다른 타입이다.
export struct dir_handle
  fd u64 .
end
```

열림 모드(정수 — enum 값을 리프에 넘기는 길이 아직 없다):
`0` = 읽기 · `1` = 쓰기(자르고 만들기) · `2` = 덧붙이기. 그 밖은 리프가 거절한다
(E-VM-FMODE).

## op 한눈에

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `open` | proc | `(fs cap file_system, path slice u8, mode u64) → option handle` | 못 열면 `none` |
| `read` | proc | `(fs, h handle, buf mut slice u8) → option u64` | 안 맞으면 `none` · `some 0` = EOF |
| `write` | proc | `(fs, h handle, bytes slice u8) → option u64` | 실패 시 `none` |
| `seek_to` | proc | `(fs, h handle, off u64, whence u64) → option u64` | 못 하면 `none` |
| `close` | proc | `(fs, h owned handle) → result void file_error` | `error close_failed` |
| `open_dir` | proc | `(fs, path slice u8) → option dir_handle` | 못 열면 `none` |
| `read_dir` | proc | `(fs, h dir_handle, buf mut slice u8) → option u64` | 안 맞으면 `none` · `some 0` = 끝 |
| `close_dir` | proc | `(fs, h owned dir_handle) → result void file_error` | `error close_failed` |
| `type_of` | proc | `(fs, path slice u8) → option u64` | 없으면 `none` |
| `is_dir` | proc | `(fs, path slice u8) → bool` | 없으면 `false` |
| `is_file` | proc | `(fs, path slice u8) → bool` | 없으면 `false` |
| `slurp` | proc | `(fs, path slice u8, buf mut slice u8) → option u64` | 못 열거나 안 들어가면 `none` |

전부 `effects io` 다. `close`/`close_dir` 는 `errors close_failed` 를 선언한다.

## op 상세

모든 op 의 첫 매개변수 `fs` 는 `cap file_system` 이다 — 커널에 닿는 순간마다 호출자가
권한을 대야 하기 때문이고, 이 덕에 권한 없는 코드는 파일을 만질 수 없다.

### open / close

`open fs path mode` — 연다. 못 열면 `none`. `path` 는 열 대상, `mode` 는 어떻게 열지
(위 정수 표)다. 얻은 `handle` 은 `owned` 로 들어야 하며
(`var h owned files.handle be some_value ho .`) 닫지 않고 버리면 컴파일 오류다.
`close fs h` 가 owned 를 소비하고 `result void file_error` 를 낸다 — 이 선언 하나가
`handle` 을 "완결이 필요한 타입" 으로 만든다.

### read / write

`read fs h buf` — 한 조각, 버퍼만큼만 읽는다. `buf` 가 인자인 이유: 읽은 바이트를 담을
자리를 호출자가 대므로 숨은 할당이 없다. **`some 0` 이 EOF** 다. 그래서 버퍼보다 큰
파일도 조각으로 읽는다(`prog/copy.low` 가 64 바이트 버퍼로 그보다 큰 파일을 복사한다).
`write fs h bytes` — 쓴 바이트 수를 낸다. 둘 다 핸들은 비소유 파라미터다 — 여러 번
읽고 마지막에 닫는 모양이 그대로 표현된다(SPEC-004 §4.8: 파라미터의 타입이 이동을
정한다).

### seek_to

`seek_to fs h off whence` — 스트림 커서(다음 read/write 가 일어날 위치)를 옮긴다.
`off` 는 오프셋, `whence` 는 그 기준이다: `0`=set(절대) · `1`=cur(현재 기준) ·
`2`=end(끝 기준). 새 절대 위치를 낸다(못 하면 `none`). `seek_to fs h 0 2` 로 **파일
크기**를 알 수 있다 — `prog/seek.low` 가 그렇게 잰다. seek 는 뷰가 아니라 커서만
바꾸므로 핸들은 비소유로 충분하다.

### open_dir / read_dir / close_dir

디렉터리 열거. `read_dir fs h buf` 는 다음 엔트리 **이름**을 호출자 버퍼로 넣고 길이를
낸다 — **`some 0` 이 끝**이다(이름은 절대 0 길이가 아니라 모호하지 않다). `.`·`..` 도
나오므로 호출자가 거른다. `close_dir` 는 `close` 와 같은 owned 패턴이다.
이름 끝의 `2` 는 빌트인 리프 `dir_open`/`dir_read`/`dir_close` 가 그 이름을 이미 차지하고
있어서다 — 같은 이름으로 감싸면 부딪친다(주의사항 참조).

### type_of / is_dir / is_file

`type_of fs path` — stat 질의(핸들 없음): `some 0`=파일 · `some 1`=디렉터리 ·
`some 2`=그 밖 · 없으면 `none`. `is_dir`/`is_file` 은 그 편의형 — 경로가 없으면
`false`(없는 것은 디렉터리가 아니다). 순회가 낸 이름이 하위 디렉터리인지 알아야
재귀(find·du·tree)가 된다.

### slurp

`slurp fs path buf` — 파일 전체를 호출자 버퍼로 읽고 총 바이트 수를 낸다. 열기 실패,
close 실패, 또는 버퍼에 **안 들어가면 `none`** — 자르지 않는다. 내부에서 열고 조각으로
읽고 닫는 전 과정을 Lowent 로 쓴 것이다.

## 사용법과 예제

`impl/tests/prog/wc.low`(slurp)와 `prog/ls.low`(디렉터리 열거)의 골격. cap 은 entry
`main` 의 입력으로 받는다 — 안 쓰는 권한은 안 받는 것이 규율이다.

```lowent
module lsbytes .

use files from "../lib/file.low" .

rem entry 패턴: main 의 input 은 전부 cap — 시작할 때 건네받는 권한이다.
proc main
  output u8 .
  input fs cap file_system .   rem 파일시스템에 닿을 권한
  input al cap allocator .     rem 버퍼를 얻을 권한
  input a  cap args .          rem 명령행 인자를 읽을 권한
  effects alloc io .
do
  rem 경로는 인자로 받는다 — 하드코딩된 경로는 숨은 의존이다.
  let dpath option slice u8 . be arg a 0 .
  guard is_some dpath . else return 64 .

  let g option mut slice u8 . . be alloc_bytes al capacity 256 .
  guard is_some g . else return 70 .
  let buf mut slice u8 . be some_value g .

  rem 디렉터리를 연다. 얻은 핸들은 owned — 닫아야만 하는 값이다.
  let di result files.dir_handle files.file_error . be files.open_dir fs (some_value dpath) .
  guard is_ok di . else return 71 .
  var dh owned files.dir_handle be ok_value di .

  var total u64 be 0 .
  var going bool be true .
  while going . do
    rem ★ 답이 **세 자리**다: 실패는 `error` · **끝은 `ok none`** · 읽었으면 `ok (some n)`.
    let n result (option u64) files.file_error . be files.read_dir fs dh buf .
    if eq (is_ok n) false . do set going false . end
    if is_ok n . do
      let nvo option u64 . be ok_value n .
      if eq (is_some nvo) false . do set going false . end
      var nl u64 be 0 .
      if is_some nvo . do set nl (some_value nvo) . end
      if eq nl 0 . do set going false . end
      if gt nl 0 . do set total (add total nl) . end
    end
  end

  rem 이 줄을 지우면 컴파일이 안 된다(E-OWN-INCOMPLETE).
  let c result void files.file_error . be files.close_dir fs dh .
  guard is_ok c . else return 72 .
  return (narrow u8 total) .
end
```

☞ **`option` 이 아니라 `result` 인 이유**: *"못 읽었다"* 와 *"더 없다"* 는 다른 답이고,
`option` 하나로는 그 둘이 같은 `none` 이 된다. 그래서 실패는 `error`, 끝은 **`ok none`** 이다
(2026-08-15 에 그렇게 갈랐다 — 이 문서가 그 전 모양으로 남아 있었고, 게이트
`check-manual-code` 가 그것을 잡았다).

파일 통째 읽기는 한 줄이다: `let n result u64 files.file_error . be files.slurp fs path buf .` —
`wc.low` 가 이것으로 줄 수를 센다. 랜덤 접근(오프셋으로 건너뛰고 파일 크기 재기)은
`prog/seek.low` 가, 경로 조립(디렉터리 + "/" + 이름)이 필요한 재귀 순회는
`prog/walk.low` 가 `strbuf` 와 엮어 보인다.

## 반례 — 이렇게 쓰면 안 된다

**① 핸들을 닫지 않고 반환.** `open` 으로 얻은 `owned handle` 을 `close` 없이 버리면
증상은 컴파일 오류 **E-OWN-INCOMPLETE** 다. 오류 조기 반환 경로에서도 마찬가지다 —
열었으면 그 경로도 닫아야 한다.

**② cap 없이 파일 만지기.** `effects io` 를 선언하고도 `cap file_system`(또는 다른
인가 cap)을 입력으로 안 받으면 **E-EFFECT-NO-CAP** — io 는 건네받는 권리다. 반대로
cap 을 받고 `effects io` 선언을 빼면 **E-EFFECT** 다.

**③ `some 0` 을 실패로 처리.** `read`/`read_dir` 의 `some 0` 은 EOF/끝이라는 **사실**
이다. `guard gt (some_value n) 0 . else return …` 로 끊으면 정상 종료가 오류 코드로
바뀐다. `none`(진짜 실패)과 구별해서 다룬다.

**④ `read_dir` 결과를 거르지 않고 사용.** `.`·`..` 이 섞여 나온다. 증상: 재귀
순회에서 안 거르면 같은 디렉터리를 다시 들어가 무한 루프다(`ls.low`·`walk.low` 의
is_dot 검사 참조).

**⑤ `slurp` 버퍼를 파일보다 작게.** `none` 이 온다 — 잘라서 주지 않는다. 크기를
모르면 `seek_to fs h 0 2` 로 먼저 재거나 `read` 조각 루프로 바꾼다.

## 주의사항

- **이름 충돌이 남긴 흔적 둘.** `seek_to` 가 `seek` 이 아닌 이유: `seek` 은 이미 모듈
  이름으로 쓰여 겹친다. `open_dir`/`read_dir`/`close_dir` 의 `2` 는 빌트인 리프
  `dir_open`/`dir_read`/`dir_close` 가 그 이름을 차지하고 있어서다. 이름 충돌은
  초보자도 자기 코드에서 만난다 — op·모듈·빌트인과 겹치지 않게 짓는다.
- 경로는 `slice u8` — 바이트열이다. 인코딩을 라이브러리가 해석하지 않으므로 OS 가 받는
  바이트를 그대로 넘긴다(관례상 UTF-8).
- `handle`/`dir_handle` 의 `fd` 는 스칼라 필드라 `field h fd` 로 읽을 수 있지만, 리프를
  직접 부르는 데 쓰면 owned 규율이 지켜주는 것이 사라진다 — 항상 `files` 의 op 을 쓴다.
- `close` 실패(`close_failed`)는 실제 상황이다(NFS·디스크 가득). `guard is_ok` 로
  받아 종료 코드로든 뭐로든 답을 낸다.
- entry 가 받을 수 있는 cap 다섯(file_system·allocator·args·io·env) 중 필요한 것만
  받는다 — 안 쓰는 권한은 안 받는 것이 규율이다.
