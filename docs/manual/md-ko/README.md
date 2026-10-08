# Lowent 매뉴얼

계약과 효과로 짜는 시스템 프로그래밍

초안(draft)

v0.2.0 · 최종 수정 2026-09-15

rubidus

- [rubidus@gmail.com](mailto:rubidus@gmail.com)
- [github.com/rubidus-api/lowent_lang](https://github.com/rubidus-api/lowent_lang)

이 책은 로우엔트 언어의 입문서이자 사용 설명서입니다.

대상 독자는 프로그래밍을 조금 해 본 사람부터,  
C·Rust 같은 시스템 언어를 쓰는 사람까지입니다.

이 판은 **초안(draft)**이다. 쪽 번호가 붙은 찾아보기와 정확한 조판은 [PDF](../pdf-ko/lowent-manual-ko.pdf)를 보라.

책 내용에 궁금한 것이 있으면 [**질문 게시판**](https://github.com/rubidus-api/lowent_lang/discussions/categories/q-a)에 남겨 주세요. 시간이 되는 대로 아는 범위 안에서 답하겠습니다. 한국어와 영어 어느 쪽이든 괜찮습니다. 오탈자나 틀린 내용은 [이슈](https://github.com/rubidus-api/lowent_lang/issues) 쪽이 낫습니다.

## 목차

- [머리말](sec01.md)
- [이 책을 읽는 법](sec02.md)
- [상세 차례 →](toc.md)

### 제1부 — 시작

- [1 Lowent 는 무엇을 하려는 언어인가](ch01.md)
- [2 첫 프로그램 — 짓고, 돌리고, 거절당하기](ch02.md)
- [3 겉모습 — 마침표, 블록, 절의 차례](ch03.md)

### 제2부 — 값과 흐름

- [4 수 — 폭이 정해진 정수와 부동소수](ch04.md)
- [5 op — fn 과 proc](ch05.md)
- [6 지역 — let 과 var](ch06.md)
- [7 흐름 — 갈래, 되풀이, 빠져나가기](ch07.md)
- [8 식 — 전위 표기와 expr 섬](ch08.md)

### 제3부 — 데이터

- [9 줄 — 배열과 슬라이스](ch09.md)
- [10 묶음 — struct 와 enum](ch10.md)
- [11 답을 담는 타입 — option 과 result](ch11.md)
- [12 빌리기 — ref 와 mut_ref](ch12.md)
- [13 이름 붙인 타입 — type·newtype·range·cast](ch13.md)

### 제4부 — 계약과 효과

- [14 계약 — 적고, 검사받고, 검사를 지운다](ch14.md)
- [15 효과 — op 이 세상에 남기는 자국](ch15.md)
- [16 권한 — 건네받는 힘](ch16.md)
- [17 실패를 설계하기](ch17.md)

### 제5부 — 메모리

- [18 영역 — 값이 사는 곳과 한꺼번에 걷히는 메모리](ch18.md)
- [19 소유 — 없앨 책임은 하나에게](ch19.md)
- [20 할당기와 고정 메모리](ch20.md)

### 제6부 — 추상

- [21 모듈 — 감춘 것이 기본이다](ch21.md)
- [22 제네릭 — 번역 시점에 정해지는 매개변수](ch22.md)
- [23 트레이트 — 여러 타입이 지키는 한 가지 약속](ch23.md)
- [24 pipe — 하려는 일만 한 줄씩](ch24.md)

### 제7부 — 동시성

- [25 액터 — 상태를 가진 채 메시지로 사는 것](ch25.md)
- [26 태스크와 채널 — 묶인 흐름끼리 주고받기](ch26.md)
- [27 병렬 되풀이와 원자 연산](ch27.md)

### 제8부 — 바깥 세계

- [28 입출력과 파일](ch28.md)
- [29 C 와 만나는 자리](ch29.md)
- [30 하드웨어 — 레지스터, 인터럽트, 기계 명령](ch30.md)
- [31 짓고 시험하기 — 패키지, 구성, 시험, 대조](ch31.md)

### 제9부 — 표준 라이브러리

- [32 표준 라이브러리의 지도](ch32.md)
- [33 글자와 부호화 — strings·fmt·utf8·codec·hash](ch33.md)
- [34 그릇과 정렬 — sortlib·sortgen·hashmap·vecgen·spsc](ch34.md)
- [35 저장소와 핸들 — pool·shard·budget·wire](ch35.md)
- [36 입출력·네트워크·시간·난수·암호](ch36.md)
- [37 터미널 — term 과 tty](ch37.md)

### 제10부 — 근거: 무엇이 증명되었나

- [38 왜 증명하는가](ch38.md)
- [39 수학 도구상자](ch39.md)
- [40 수의 증명 — 넓히기, 좁히기, 나눗셈](ch40.md)
- [41 경계의 증명 — 구간, 관계, 행우선 주소](ch41.md)
- [42 소유와 차용의 증명](ch42.md)
- [43 되풀이와 고정점 — “몇 번을 돌든” 을 증명하기](ch43.md)
- [44 효과의 증명 — “무엇을 할 수 있나” 를 타입에 적기](ch44.md)
- [45 경합과 병렬의 증명 — 규율이 메모리 모델을 대신한다](ch45.md)
- [46 약한 메모리의 증명 — 기본값이면 순서대로 생각해도 된다](ch46.md)
- [47 락의 증명 — 무거운 도구가 실제로 필요한 자리](ch47.md)
- [48 문법의 증명 — 어느 닫개로 닫아도 같은 나무](ch48.md)
- [49 해시의 증명 — 이름이 아니라 내용으로 부르기](ch49.md)
- [50 증명하지 않은 것](ch50.md)

### 부록과 찾아보기

- [부록 A — 낱말과 기본 연산](sec53.md)
- [부록 B — 진단 찾아보기](sec54.md)
- [부록 C — 흔한 실수와 고치는 법](sec55.md)
- [부록 D — 문법 요약](sec56.md)
- [부록 E — 표준 라이브러리 모듈](sec57.md)
- [strings — 문자열 뷰 연산](sec58.md)
- [strbuf — 소유 문자열 버퍼와 널 종단 cstr](sec59.md)
- [fmt — 호출자의 버퍼에 조립하는 포매팅](sec60.md)
- [utf8 — UTF-8 코드포인트 순회와 검증](sec61.md)
- [utf16 — UTF-16 서로게이트 산술](sec62.md)
- [unicode — 유니코드 속성 표](sec63.md)
- [codec — 16 진과 base64](sec64.md)
- [regex — 역추적하지 않는 Pike VM 정규식](sec65.md)
- [term — 터미널 렌더러(순수 절반)](sec66.md)
- [sortlib — 제자리 quicksort](sec67.md)
- [sortgen — 제네릭 정렬(비교는 타입이 들고 온다)](sec68.md)
- [searchlib — 정렬된 슬라이스의 이진 탐색](sec69.md)
- [hashmap — u64 → u64 해시 맵](sec70.md)
- [strmap — 문자열 키 해시 맵](sec71.md)
- [vecs — 호출자가 버퍼를 드는 성장 바이트 벡터](sec72.md)
- [spsc — 락 없는 SPSC 링 버퍼](sec73.md)
- [hash — 해시(해시맵 자리 · 손상 검출 · SHA-2)](sec74.md)
- [hash_legacy — 옛 해시(MD5 · SHA-1)](sec75.md)
- [math — 부동소수 수학](sec76.md)
- [random — 난수(재현되는 열 · OS 엔트로피)](sec77.md)
- [hmac — HMAC-SHA256 과 HKDF](sec78.md)
- [chacha — ChaCha20 스트림 암호](sec79.md)
- [poly — Poly1305 한 번 쓰는 인증자](sec80.md)
- [aead — ChaCha20-Poly1305 봉인과 개봉](sec81.md)
- [x25519 — 곡선 위의 키 합의](sec82.md)
- [aes — AES-128 블록 암호](sec83.md)
- [gcm — AES-128-GCM 인증 암호](sec84.md)
- [crypto_hw — 기계가 도와주는 암호 셈](sec85.md)
- [bigint — 큰수 모듈러 산술](sec86.md)
- [rsa — RSASSA-PSS 검증](sec87.md)
- [p256 — NIST P-256 곡선과 ECDSA 검증](sec88.md)
- [p384 — NIST P-384 곡선과 ECDSA 검증](sec89.md)
- [ecdsa — ECDSA P-256 서명 생성](sec90.md)
- [ed25519 — Ed25519 서명 검증](sec91.md)
- [der — DER 최소 파서](sec92.md)
- [pem — PEM 봉투 벗기기](sec93.md)
- [x509 — X.509 인증서 읽기](sec94.md)
- [verify — 인증서 서명과 체인의 한 마디 확인](sec95.md)
- [tls13 — TLS 1.3 의 계산 부품](sec96.md)
- [tlssrv — TLS 1.3 서버 핸드셰이크](sec97.md)
- [tlscli — TLS 1.3 클라이언트 핸드셰이크](sec98.md)
- [http — HTTP/1.1 요청 파서](sec99.md)
- [soa — SoA 배치 시범: 필드마다 배열 하나](sec100.md)
- [allocs — 얼로케이터 trait 과 범프 · 기본 얼로케이터](sec101.md)
- [pool — 세대 핸들 블록 풀](sec102.md)
- [shard — 저장소를 쪼개는 접근 단위](sec103.md)
- [budget — 핸들 비트 예산과 세대 한 바퀴](sec104.md)
- [wire — 한 낱말을 칸으로 나눠 쓰기](sec105.md)
- [flags — 이름 붙은 켬 · 끔을 한 낱말에](sec106.md)
- [segarena — 고정 크기 세그먼트 아레나](sec107.md)
- [pagecache — 페이지 id 와 고정 커서](sec108.md)
- [growvec — 자가성장 바이트 벡터](sec109.md)
- [vecgen — 제네릭 자가성장 벡터 vec t a](sec110.md)
- [mapgen — 제네릭 해시맵 table k v](sec111.md)
- [nodelist — 고정 intrusive 목록](sec112.md)
- [segview — 조각 뷰의 커서 · 총길이 · 펴기](sec113.md)
- [lifemode · lifeatom — 값이 언제 끝나는가](sec114.md)
- [io — 슬라이스 위의 스트림 읽기](sec115.md)
- [outbuf — 버퍼링 출력, flush 망각은 컴파일 오류](sec116.md)
- [files — 파일 · 디렉터리 스트림, close 망각은 컴파일 오류](sec117.md)
- [tty — 터미널 입력](sec118.md)
- [net — 소켓(TCP loopback · 프로세스 안 한 쌍)](sec119.md)
- [clock — 시각과 마감](sec120.md)
- [trust — 신뢰 저장소에서 믿는 뿌리 찾기](sec121.md)
- [찾아보기](sec122.md)

## 저작권과 연락처

지은이

rubidus

연락

[rubidus@gmail.com](mailto:rubidus@gmail.com)

저장소

[github.com/rubidus-api/lowent_lang](https://github.com/rubidus-api/lowent_lang)

판

v0.2.0 — 초안(draft)

최종 수정

2026-09-15

**본문** — 크리에이티브 커먼즈 저작자표시-비영리-동일조건변경허락 4.0 국제 라이선스(CC BY-NC-SA 4.0). 출처를 밝히면 자유롭게 공유하고 고칠 수 있으나, 영리 목적 이용은 허용되지 않으며, 고친 결과물에는 같은 라이선스를 적용해야 합니다.  
[creativecommons.org/licenses/by-nc-sa/4.0/](https://creativecommons.org/licenses/by-nc-sa/4.0/)

**예제 코드** — MIT 라이선스. 자유롭게 가져다 쓰실 수 있습니다. 예제가 쓰는 로우엔트 컴파일러와 표준 라이브러리는 저장소의 MIT 라이선스를 따릅니다.

이 책의 모든 코드 시연은 실제로 lowentc 로 검사·실행해 얻은 출력을 그대로 인쇄한 것입니다. 조판은 Typst로 했습니다.

이 책은 계속 고쳐집니다. 지금 읽고 계신 것은 위 번호의 판이고, 그 뒤로도 오류 수정과 내용 보강이 이어집니다. 오류 신고와 수정 제안은 저장소에서 받습니다.
