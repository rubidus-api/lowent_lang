#import "../lib.typ": *

= `files` --- 파일 · 디렉터리 스트림, close 망각은 컴파일 오류 <mod-files>

#modhead(file: "lib/file.low", layer: [L2 --- 바깥 세계], caps: [`cap file_system`])

파일과 디렉터리를 여닫고 읽고 쓴다. 모든 op 이 첫 인자로 `cap file_system` 을 받는다 --- 파일시스템에 닿을 권한은 `main` 의 입력으로 한 번 건네지고, 넘겨받은 op 만
파일을 만질 수 있다(#chref("capabilities"), #chref("io-files")). 빌트인 잎(`file_open` · `file_read` · `dir_open` · `file_type` 등)은 커널에 닿을 뿐 정수 핸들만
안다. 정수 fd 를 그대로 들면 닫기를 잊어도 아무도 모르므로, 이 모듈이 *잊을 수 없는 값*(`owned handle`)으로 감싼다.

```lowent
let n result u64 files.file_error . be files.slurp fs "data.txt" buf .
```

#dtable(
  columns: 2,
  id: "mod-files-answers",
  caption: [답의 모양은 op 마다 정직하게 다르다],
  [*op*], [*답과 그 이유*],
  [`open` · `open_dir`], [`result X file_error` --- 여는 일에는 "끝" 이 없다. 열리거나 실패다],
  [`read` · `read_dir`], [`result (option u64) file_error` --- `ok (some n)` 읽었다 · *`ok none` 끝이다(실패가 아니다)* · `error e` 실패다],
  [`write` · `seek_to` · `slurp`], [`result u64 file_error` --- "끝" 이라는 개념이 없다],
  [`type_of` · `link_type_of` · `is_dir` · `is_file` · `is_symlink`], [`option u64` · `bool` --- 잎이 "없음" 과 "stat 실패" 를 한 비트로 준다. 없는 구별을 타입으로 약속하지 않는다],
)

`file_error` 의 변형도 *구별할 수 있는 것만*이다 --- `open_failed` · `read_failed` · `write_failed` · `seek_failed` · `close_failed`, 그리고 라이브러리가 스스로 세는
`buffer_too_small`. 잎이 errno 를 주지 않으므로 정직한 해상도는 "어느 연산이 실패했나" 까지다.

#dtable(
  columns: 2,
  id: "mod-files-ops",
  caption: [`files` 의 op --- 모두 `effects io`, 첫 인자 `fs cap file_system`],
  [*op*], [*하는 일*],
  [`handle` · `dir_handle`], [잊을 수 없는 핸들(스칼라 `fd` 하나) --- 파일과 디렉터리는 다른 타입],
  [`open fs path mode`], [연다. mode `0` 읽기 · `1` 쓰기(자르고 만들기) · `2` 덧붙이기],
  [`read fs h buf` · `write fs h bytes`], [한 조각을 읽는다(짧은 읽기는 정상) · 쓴 바이트 수],
  [`seek_to fs h off whence`], [커서를 옮긴다. whence `0` 절대 · `1` 현재 기준 · `2` 끝 기준. `seek_to fs h 0 2` 가 파일 크기다],
  [`close fs h`], [`owned handle` 을 소비하고 `result void file_error`],
  [`open_dir` · `read_dir fs h buf` · `close_dir`], [디렉터리 열거. `read_dir` 는 다음 이름을 버퍼에 넣는다(`.` · `..` 도 나온다)],
  [`type_of` · `link_type_of`], [`some 0` 파일 · `some 1` 디렉터리 · `some 2` 그 밖 · 없으면 `none`(`link_type_of` 는 링크를 따라가지 않는다)],
  [`is_dir` · `is_file` · `is_symlink`], [편의형 --- 경로가 없으면 `false`],
  [`slurp fs path buf`], [파일 전체를 버퍼로. 안 들어가면 `error buffer_too_small` --- 자르지 않는다],
)

*close 를 잊으면 컴파일되지 않는다.* `close` · `close_dir` 가 `owned` 를 받고 `result` 를 내므로 핸들을 조용히 버리면 `E-OWN-INCOMPLETE` 다. 그리고 닫기는 진짜로 실패한다
(NFS, 디스크 가득) --- `result` 가 장식이 아니다. `read` · `write` 의 핸들은 비소유 파라미터라, 여러 번 읽고 마지막에 닫는 모양이 그대로 표현된다.

*실패와 EOF 는 다른 답이다.* 한때 `slurp` 은 읽기가 실패하면 루프를 멈출 뿐이어서 호출자가 완독과 중도 실패를 구별할 수 없었고, 줄 수를 세는 프로그램이 읽기 실패를
"0 lines" 라는 성공으로 냈다. 결함 주입기가 그것을 실물로 보였다. 지금은 실패면 `error` 를 답한다(핸들은 그래도 닫는다 --- 자원 반납은 답과 별개다).

```lowent
proc main input fs cap file_system . input al cap allocator . input a cap args . output u8 . effects alloc io . do
  let dpath option slice u8 . be arg a 0 .
  guard is_some dpath . else return 64 .
  let g option mut slice u8 . . be alloc_bytes al capacity 256 .
  guard is_some g . else return 70 .
  let buf mut slice u8 . be some_value g .
  let di result files.dir_handle files.file_error . be files.open_dir fs (some_value dpath) .
  guard is_ok di . else return 71 .
  var dh owned files.dir_handle be ok_value di .
  var total u64 be 0 .
  var going bool be true .
  while going . do
    let n result (option u64) files.file_error . be files.read_dir fs dh buf .
    if eq (is_ok n) false . do set going false . end
    if is_ok n . do
      let nvo option u64 . be ok_value n .
      if eq (is_some nvo) false . do set going false . end
      if is_some nvo . do set total (add total (some_value nvo)) . end
    end
  end
  let c result void files.file_error . be files.close_dir fs dh .
  guard is_ok c . else return 72 .
  return (narrow u8 total) .
end
```

*실패를 직접 불러 본다.* `LOW_HOST_FAULT="open:err"`(모든 open 실패), `"read:err@2"`(두 번째 읽기 실패), `"read:short@1=4"`(첫 읽기를 4 바이트로 자름), `"close:err"`.
VM 과 네이티브가 같은 주입기를 쓰므로 두 백엔드의 답이 같아야 한다.

#antipattern[오류 경로에서 핸들을 닫지 않는다][
  열었으면 조기 반환 경로도 닫아야 한다. 빠뜨리면 `E-OWN-INCOMPLETE` 로 컴파일러가 잡는다.
]

#antipattern[`read_dir` 결과를 거르지 않는다][
  `.` · `..` 이 섞여 나온다. 재귀 순회에서 거르지 않으면 같은 디렉터리를 다시 들어가 끝나지 않는다.
]

#antipattern[`ok none` 을 실패로 처리한다 · 짧은 읽기를 끝으로 여긴다][
  `ok none` 은 끝이라는 *사실*이다. 반대로 부탁한 것보다 적게 읽은 `ok (some n)` 은 끝이 아니다 --- 계속 읽어야 한다(`slurp` 이 그렇게 한다).
]

*주의.* 경로는 `slice u8` 바이트열이고 인코딩을 해석하지 않는다(관례상 UTF-8). `field h fd` 로 fd 를 읽어 잎을 직접 부르면 owned 규율이 지켜 주는 것이 사라진다. 버퍼
크기를 모르면 `seek_to fs h 0 2` 로 먼저 재거나 `read` 조각 루프로 바꾼다. 파일시스템 변경(디렉터리 만들기 · 지우기 · 이름 바꾸기)은 상태 없는 경로 → bool 잎이라
감싸지 않는다 --- 순수 전달은 동의어일 뿐이다. 소켓은 #modref("net")[`net`] 이 맡는다. 기다리다 중간에 멈출 수 있는 입출력(`wait`)은 아직 없다 --- 그것은 반응기(reactor)가 먼저 서야 지을 수 있다. `seek` 이 아니라 `seek_to` 인 이유 --- `seek` 은 다른 이름과 겹쳤다.
