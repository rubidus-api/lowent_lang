#import "../lib.typ": *

= 부록 E --- 표준 라이브러리 모듈

제9부(#chrange("lib-map", "lib-terminal"))는 표준 라이브러리를 쓰임새별로 둘러보았다. 이 부록은 모듈마다 한 쪽씩, 무엇을 하고 어떤 op 을 내며 무엇을 하지
않는지를 모은 *참조*다. 처음부터 읽는 곳이 아니라 필요할 때 펴 보는 곳이다.

모듈 쪽은 같은 모양이다. 머리에 소스 파일 · 층 · 받는 권한을 적고, 무엇을 하는 모듈인지, op 한눈에, 쓰는 법, 설계와 경계, 짓지 않은 것 차례로
이어진다. 모듈마다 적는 성숙도(`experimental` · `incubating` · `standard`)는 소스 머리와 개발 저장소의 장부가 권위이고 판마다 움직이므로 여기
옮기지 않는다. 모든 모듈이 따르는 규약 --- 호출자의 버퍼, 값으로 돌아오는 실패, 전량 아니면 무, 갚아야 하는 소유 --- 은 #chref("lib-map")에 있다.

모듈 이름은 파일 이름과 다를 수 있다. `use` 가 찾는 것은 파일 안의 `module` 선언이다(`lib/str.low` 는 `strings`, `lib/alloc.low` 는 `allocs`).
크로스 모듈 이름은 늘 한정해서 부르고(`strings.find`), 자주 쓰면 `use codec as c .` 로 별칭을 준다.

#dtable(
  columns: 2,
  id: "a5-l0",
  caption: [L0 --- 순수 계산(`effects none` · 호출자의 버퍼)],
  [*모듈*], [*한 줄*],
  [#modref("strings")[`strings`]], [문자열 뷰 연산 --- 자르고 찾고 비교한다],
  [#modref("strbuf")[`strbuf`]], [소유 문자열 버퍼와 널 종단 `cstr`],
  [#modref("fmt")[`fmt`]], [호출자의 버퍼에 조립하는 포매팅],
  [#modref("utf8")[`utf8`]], [UTF-8 코드포인트 순회와 검증],
  [#modref("utf16")[`utf16`]], [UTF-16 서로게이트(surrogate) 산술],
  [#modref("unicode")[`unicode`]], [유니코드 속성 표 --- 기계적으로 뽑았다],
  [#modref("codec")[`codec`]], [16진 · base64],
  [#modref("regex")[`regex`]], [역추적하지 않는 Pike VM 정규식],
  [#modref("term")[`term`]], [터미널 렌더러의 순수한 절반 --- ANSI 조립 · 화면 diff · 폭],
  [#modref("sortlib")[`sortlib`]], [`u64` 제자리 quicksort],
  [#modref("sortgen")[`sortgen`]], [제네릭 정렬 --- 비교는 타입이 들고 온다],
  [#modref("searchlib")[`searchlib`]], [정렬된 슬라이스의 이진 탐색],
  [#modref("hashmap")[`hashmap`]], [`u64 → u64` 오픈 어드레싱],
  [#modref("strmap")[`strmap`]], [바이트열 → `u64` 해시맵],
  [#modref("vecs")[`vecs`]], [자라는 바이트 벡터],
  [#modref("spsc")[`spsc`]], [락 없는 단일 생산자·단일 소비자 링 버퍼],
  [#modref("hash")[`hash`]], [해시맵 자리(FNV-1a) · 손상 검출(CRC-32) · SHA-256],
  [#modref("math")[`math`]], [부동소수 수학 --- `==` 대신 `close`],
  [#modref("random")[`random`]], [재현되는 열과 운영체제 엔트로피를 이름으로 가른다],
  [#modref("hmac")[`hmac`]], [HMAC-SHA256 · HKDF --- 누가 보냈는가],
  [#modref("chacha")[`chacha`]], [ChaCha20 스트림 --- 혼자서는 안전하지 않다],
  [#modref("poly")[`poly`]], [Poly1305 --- 키는 메시지마다 새것],
  [#modref("aead")[`aead`]], [ChaCha20-Poly1305 봉인과 개봉 --- 암호는 여기서 시작한다],
  [#modref("x25519")[`x25519`]], [키 합의],
  [#modref("aes")[`aes`]], [AES-128 블록 암호 --- 혼자 쓰면 대개 틀린다],
  [#modref("gcm")[`gcm`]], [AES-128-GCM --- 논스를 되풀이하지 않는다],
  [#modref("bigint")[`bigint`]], [큰수 모듈러 산술],
  [#modref("rsa")[`rsa`]], [RSASSA-PSS 검증],
  [#modref("p256")[`p256`]], [NIST P-256 곡선과 ECDSA 검증],
  [#modref("ecdsa")[`ecdsa`]], [ECDSA P-256 서명 --- 난수 없이 논스를 유도한다],
  [#modref("ed25519")[`ed25519`]], [Ed25519 서명 검증],
  [#modref("der")[`der`]], [DER 최소 파서 --- 공개키만 꺼낸다],
  [#modref("pem")[`pem`]], [PEM 봉투 벗기기],
  [#modref("tls13")[`tls13`]], [TLS 1.3 의 부품 --- 키 스케줄 · 레코드 · 전사 · Finished],
  [#modref("tlssrv")[`tlssrv`]], [서버 TLS 핸드셰이크와 응용 데이터 레코드],
  [#modref("http")[`http`]], [HTTP/1.1 요청 파서 --- 알맹이는 거절이다],
  [#modref("soa")[`soa`]], [구조체 배열을 칸별 배열로 두는 배치의 시범],
)

#dtable(
  columns: 2,
  id: "a5-l1",
  caption: [L1 --- 저장(할당 권한이나 빌린 바이트가 필요하다)],
  [*모듈*], [*한 줄*],
  [#modref("allocs")[`allocs`]], [할당기 트레이트 · 범프 할당기 · 기본 할당기 둘],
  [#modref("pool")[`pool`]], [세대 핸들 블록 풀],
  [#modref("shard")[`shard`]], [저장소를 겹치지 않는 조각으로 나누는 토큰],
  [#modref("budget")[`budget`]], [핸들의 비트 예산과 세대 수의 은퇴],
  [#modref("wire")[`wire`]], [한 워드를 칸으로 나눠 쓰기 --- 마스크가 자리와 폭을 든다],
  [#modref("flags")[`flags`]], [이름 붙은 켬/끔 설정을 한 워드에],
  [#modref("segarena")[`segarena`]], [고정 크기 세그먼트 아레나],
  [#modref("pagecache")[`pagecache`]], [페이지 번호와 고정 토큰],
  [#modref("growvec")[`growvec`]], [자라는 바이트 벡터 --- `vecgen.vec u8` 의 짧은 이름],
  [#modref("vecgen")[`vecgen`]], [제네릭 자라는 벡터 `vec t`],
  [#modref("mapgen")[`mapgen`]], [제네릭 해시맵 `table k v`],
  [#modref("nodelist")[`nodelist`]], [고정 크기 침입형 목록],
  [#modref("segview")[`segview`]], [흩어진 조각 뷰의 커서 · 총길이 · 펴기],
  [#modref("lifemode")[`lifemode`]], [`lifemode` · `lifeatom` --- 값이 언제 끝나는가],
)

#dtable(
  columns: 2,
  id: "a5-l2",
  caption: [L2 --- 호스트(권한이 필요하다)],
  [*모듈*], [*한 줄*],
  [#modref("io")[`io`]], [슬라이스 위의 스트림 읽기],
  [#modref("outbuf")[`outbuf`]], [버퍼링 출력 --- 비우기를 잊으면 번역이 거절한다],
  [#modref("files")[`files`]], [파일과 디렉터리 --- 닫기를 잊으면 번역이 거절한다],
  [#modref("tty")[`tty`]], [터미널 입력 --- raw 모드 · 키 읽기 · 순수 키 파싱],
  [#modref("net")[`net`]], [소켓 --- TCP 루프백 · 프로세스 안 연결 쌍],
  [#modref("clock")[`clock`]], [시각과 마감 --- 단조 시계와 벽시계],
)

암호 모듈은 TLS 1.3 을 향해 층으로 쌓여 있고, 그 차례와 아직 없는 꼭대기는 #chref("lib-io-net")에 있다.
