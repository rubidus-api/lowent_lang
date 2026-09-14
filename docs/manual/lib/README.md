# 표준 라이브러리 매뉴얼

`lib/` 의 표준 모듈을 하나씩 다룬다. 각 문서는 같은 순서로 아홉 절이다:
**왜 있는가 · 설계 의도와 경계 · 자료구조 · op 한눈에 · op 상세 · 사용법과 예제 · 반례 · 주의사항**.

## 모듈 이름은 파일 이름이 아니다

`use <이름> .` 이 찾는 것은 **파일 안의 `module` 선언**이다. 몇 곳은 파일 이름과 다르다:

| 소스 파일 | 모듈 이름 | 소스 파일 | 모듈 이름 |
|---|---|---|---|
| `lib/alloc.low` | `allocs` | `lib/str.low` | `strings` |
| `lib/vec.low` | `vecs` | `lib/out.low` | `outbuf` |
| `lib/sort.low` | `sortlib` | `lib/file.low` | `files` |
| `lib/search.low` | `searchlib` | 나머지 | 파일 이름 그대로 |

## 계층별 목록

**L0 — 순수 계산** (effects none · 호출자 버퍼 · 오라클 안에서 검증된다)

| 모듈 | 한 줄 |
|---|---|
| [strings](strings.md) | 문자열 뷰 연산 |
| [strbuf](strbuf.md) | 소유 문자열 버퍼와 널종단 `cstr` |
| [fmt](fmt.md) | 호출자 버퍼에 조립하는 포매팅 |
| [utf8](utf8.md) | UTF-8 코드포인트 순회와 검증 |
| [utf16](utf16.md) | UTF-16 서로게이트 산술 |
| [codec](codec.md) | hex · base64 |
| [regex](regex.md) | Pike VM 정규식 (ReDoS-free) |
| [term](term.md) | 터미널 렌더러의 순수 절반 (ANSI 조립 · 화면 diff · 폭 · grapheme cluster) |
| [unicode](unicode.md) | 유니코드 속성 표 (문자·숫자·공백·폭 0 — 기계적으로 뽑았다) |
| [sortlib](sortlib.md) | 제자리 quicksort (u64) |
| [sortgen](sortgen.md) | 제네릭 정렬 — 비교는 타입이 들고 온다 |
| [searchlib](searchlib.md) | 정렬된 슬라이스의 이진 탐색 |
| [hashmap](hashmap.md) | u64 → u64 오픈 어드레싱 |
| [spsc](spsc.md) | ★ **lock-free SPSC 링 버퍼** — 락 없이 스레드 사이로 값을 넘긴다(RFC-0018 §8-3) |
| [strmap](strmap.md) | 바이트열 → u64 |
| [vecs](vecs.md) | 성장하는 바이트 벡터 |
| [hash](hash.md) | 해시 — 해시맵 자리(FNV-1a)·손상 검출(CRC-32)·**SHA-256**(`digest`) |
| [math](math.md) | 부동소수 수학 — **`==` 대신 `close`**(오라클이 비트가 아닌 이유) |
| [random](random.md) | 난수 — **재현되는 열과 OS 엔트로피를 이름으로 가른다** |
| [hmac](hmac.md) | HMAC-SHA256 · HKDF — *누가 보냈는가* 를 답한다 |
| [chacha](chacha.md) | ChaCha20 스트림 — **혼자서는 안전하지 않다**(→ aead) |
| [poly](poly.md) | Poly1305 — **키는 메시지마다 새것** |
| [aead](aead.md) | ChaCha20-Poly1305 — 봉인과 개봉. **크립토는 여기서 시작하라** |
| [x25519](x25519.md) | 키 합의 — 작은 위수 점 거르기는 **부르는 쪽** |
| [aes](aes.md) | AES-128 블록 암호 — **혼자 쓰면 대개 틀린다**(→ gcm). S-box 를 표가 아니라 정의로 낸다 |
| [gcm](gcm.md) | AES-128-GCM — TLS 1.3 의 **MUST** 스위트. **논스를 되풀이하지 마라** |
| [bigint](bigint.md) | 큰수 모듈러 산술 — **나눗셈을 안 지으려고** 몽고메리를 쓴다 |
| [rsa](rsa.md) | RSASSA-PSS **검증** — PKCS#1 v1.5 는 규격이 금지한다 |
| [p256](p256.md) | NIST P-256 과 ECDSA **검증** — 공개 웹이 실제로 쓰는 곡선 |
| [ecdsa](ecdsa.md) | ECDSA P-256 **서명** — nonce 를 **난수 없이** 유도한다(RFC 6979) |
| [ed25519](ed25519.md) | Ed25519 **검증** — 덧셈이 완비라 예외 갈래가 없다 |
| [der](der.md) | DER 최소 파서 — **공개키만** 꺼낸다(+ PKCS#8 개인키). PKI 가 아니다 |
| [pem](pem.md) | PEM 봉투 벗기기 — 인증서는 **밖에서 받는다**(ACME 안 짓는다) |
| [tls13](tls13.md) | TLS 1.3 의 **부품들** — 키 스케줄·레코드·전사·Finished |
| [tlssrv](tlssrv.md) | 서버 TLS — 핸드셰이크 양방향 + **응용 데이터 레코드**(전송만 아직) |
| [http](http.md) | HTTP/1.1 **요청 파서** — 알맹이는 **거절**이다(밀반입이 사는 자리) |
| [soa](soa.md) | SoA 배치 시범 (RFC-0070 실험) |

☞ **TLS 로 가는 길은 이 순서로 쌓인다** — 그리고 아직 **꼭대기가 없다**:

```
hash(SHA-256) · hmac(HKDF)          ← 유도
chacha·poly → aead  /  aes → gcm    ← 봉인 (스위트 둘)
x25519                              ← 키 합의
bigint → rsa  ·  p256  ·  ed25519   ← 서명 검증
p256 → ecdsa                        ← 서명 **생성**(서버가 하는 일)
pem → der                           ← 파일에서 키·인증서 꺼내기
tls13                               ← 규격이 요구하는 계산들
tlssrv                              ← 핸드셰이크 전부(전송만 아직)
```

**L1 — 저장(할당 권한이 필요하다)**

| 모듈 | 한 줄 |
|---|---|
| [allocs](allocs.md) | 얼로케이터 trait 과 범프 얼로케이터 · 기본 얼로케이터 둘(고정 창 `fixed_bytes` · 힙 `heap_bytes`) |
| [pool](pool.md) | 세대 핸들 블록 풀 |
| [shard](shard.md) | 저장소를 쪼개는 접근 단위(서로소 토큰) |
| [budget](budget.md) | 핸들 bit 예산과 wrap 정책 |
| [wire](wire.md) | 한 낱말을 칸으로 나눠 쓰기 — 마스크가 자리와 폭을 둘 다 든다 |
| [flags](flags.md) | 이름 붙은 켬/끔 설정을 한 낱말에 — 자리는 비트 번호다 |
| [segarena](segarena.md) | 고정 크기 세그먼트 아레나 |
| [pagecache](pagecache.md) | 페이지 id와 고정 커서 |
| [growvec](growvec.md) | 자가성장 바이트 벡터 — `vecgen.vec u8` 의 별칭(짧은 이름) |
| [vecgen](vecgen.md) | **제네릭** 자가성장 벡터 `vec t` — 한 틀에서 `vec u32`·`vec u8`·`vec pt` 가 나온다 |
| [mapgen](mapgen.md) | **제네릭** 해시맵 `table k v` — 타입 파라미터 둘, 스스로 다시 뿌린다 |
| [nodelist](nodelist.md) | 고정 intrusive 목록 |
| [segview](segview.md) | 조각 뷰의 커서·총길이·펴기 |
| [lifemode](lifemode.md) | `lifemode` · `lifeatom` — 값이 언제 끝나는가 |

**L2 — 호스트(능력이 필요하다 — `cap io` · `cap file_system` · `cap tty` · `cap net` · `cap clock`)**

| 모듈 | 한 줄 |
|---|---|
| [io](io.md) | 슬라이스 위의 스트림 읽기 |
| [outbuf](outbuf.md) | 버퍼링 출력 (flush 망각은 컴파일 오류) |
| [files](files.md) | 파일·디렉터리 스트림 (close 망각은 컴파일 오류) |
| [tty](tty.md) | 터미널 입력 — raw 모드·키 읽기(`cap tty`) + 순수 키 파싱 |
| [net](net.md) | 소켓 — TCP loopback·프로세스 안 한 쌍(`cap net`). **핸들은 자원이다** |
| [clock](clock.md) | 시각과 마감(`cap clock`) — 단조 시계와 벽시계는 **약속이 다르다** |

## 전 모듈에 공통인 규약

**이름을 끌어오는 법.** 크로스모듈 이름은 **한정**이다 — `use` 없이는 보이지 않고, `use` 해도
`모듈.op` 로만 부른다(글롭은 없다). 자주 쓰면 별칭을 준다:

```lowent
use codec as c .          rem 표준 모듈은 from 없이 풀린다
let n option u64 . be c.hex_enc src dst .
```

**버퍼는 호출자의 것.** L0 모듈은 아무것도 할당하지 않는다. 출력 버퍼·스크래치는 전부
매개변수로 받는다 — 그래서 재진입이 공짜고, 오라클이 그대로 돌릴 수 있다.

**실패는 값이다.** 대부분 `option`(`none` = 못 했다)이거나 `result`(어떤 오류인지 말한다).
트랩도, 조용한 잘림도, 치환 문자도 없다. 그리고 되도록 **전량 아니면 무**다 — 자리가
모자라면 한 바이트도 안 쓴다(절반 쓴 버퍼는 조용히 틀린 출력이라서).

**소유는 갚아야 한다.** `outbuf` 의 미출력 바이트, `files` 의 핸들처럼 완료가 필요한 것은
소유 값이라 잊으면 **컴파일이 거절한다**(`E-OWN-INCOMPLETE`).
