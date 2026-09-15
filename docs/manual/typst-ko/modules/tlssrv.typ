#import "../lib.typ": *

= `tlssrv` --- TLS 1.3 서버 핸드셰이크 <mod-tlssrv>

#modhead(file: "lib/tlssrv.low", layer: [L0 --- 순수 계산(호출자의 뒷받침)], caps: [없음])

#modref("tls13")[`tls13`] 이 규격의 *계산*을 갖고 있다. 이 모듈은 그것을 *순서대로 부르는 층*이다 --- ClientHello 를 읽고, 서버 메시지를 짓고, 상태를 옮기고,
응용 데이터 레코드를 봉하고 연다. 모든 실패는 `0` 이고, op 은 *자리*를 낸다 --- 자르는 것은 부르는 쪽이 `subslice` 로 한다.

#aside[이것만으로는 TLS 서버가 아니다][
  *전송이 없다* --- 소켓도 재조립도 없고 바이트열은 부르는 쪽이 모아 온다(레코드 경계도 부르는 쪽이 정한다). 키 갱신(KeyUpdate) · 레코드 패딩 ·
  HelloRetryRequest · PSK · 0-RTT · 클라이언트 인증서 요구 · 세션 재개가 없다. 인증서 체인은 `build_cert` 가 하나만 담는다. 확장은 *찾아만* 준다(SNI · ALPN 해석은
  부르는 쪽). 감사받지 않았다.
]

*ClientHello 는 가장 적대적인 입력이다.* 아직 아무것도 인증되지 않은 바이트열이고 길이 필드는 전부 상대가 썼다. 그래서 --- 어떤 자리도 버퍼를 넘으면 즉시 실패한다.
확장 걷기는 재귀하지 않고 걸음 수를 버퍼 크기로 묶는다(길이 0 짜리 확장이 이어지면 묶지 않은 루프는 끝나지 않는다). 길이가 맞지 않으면 고쳐 읽지 않는다 --- `ch_ok`
는 선언된 길이가 버퍼와 *정확히* 같기를, `ch_ext_len` 은 확장이 끝까지 맞기를 요구한다.

#dtable(
  columns: 2,
  id: "mod-tlssrv-ops",
  caption: [`tlssrv` 의 op],
  [*op*], [*하는 일*],
  [`st_start` · `st_recvd_ch` · `st_negotiated` · `st_wait_flight2` · `st_wait_finished` · `st_connected`], [상태 번호],
  [`hs_client_hello` · `hs_server_hello` · `hs_encrypted_extensions` · `hs_certificate` · `hs_certificate_verify` · `hs_finished`], [메시지 종류 번호],
  [`next_ok` · `step`], [지금 이 메시지를 받아도 되는가 · 상태를 옮긴다(못 옮기면 같은 상태를 낸다)],
  [`ch_ok`], [종류와 길이가 버퍼와 맞는가],
  [`ch_random_off` · `ch_sid_off` · `ch_sid_len`], [랜덤 · session_id],
  [`ch_suites_off` · `ch_suites_len` · `ch_has_suite`], [제안된 스위트],
  [`ch_ext_off` · `ch_ext_len` · `ch_ext_find` · `ch_ext_find_len`], [확장 블록 · 종류로 찾기],
  [`ch_x25519_off` · `be16`], [key_share 안의 x25519 공개키 자리 · 빅엔디언 2 바이트],
  [`build_sh` · `build_ee` · `build_cert`], [ServerHello · EncryptedExtensions · Certificate(DER 하나) 짓기],
  [`cv_content` · `build_cv` · `build_fin`], [CertificateVerify 가 서명하는 130 바이트 · CertificateVerify · Finished 짓기],
  [`server_finished`], [ECDHE 부터 키 스케줄을 올려 서버 Finished 까지 한 줄로],
  [`app_secrets` · `check_client_finished`], [응용 트래픽 비밀(c · s) · 상대 Finished 확인(1 = 맞음)],
  [`traffic_keys` · `seal_app` · `open_app`], [비밀 → 키 ‖ IV(한 방향씩) · 응용 데이터 레코드 봉하기 · 열기],
)

메시지 짓기는 틀이 같다(`<종류 1> <길이 3> <본문>`) --- 그래서 틀을 한 번만 적었다. 네 곳에 같은 산술을 되풀이하면 한 곳만 고치는 날이 온다.

*`cv_content` --- 공백 64 개는 장식이 아니다.* 서명 대상은 `0x20 × 64 ‖ "TLS 1.3, server CertificateVerify" ‖ 0x00 ‖ 전사 해시` 다(§4.4.3). 그 앞머리가 없으면 이
서명이 다른 문맥(인증서 서명, 클라이언트 쪽 서명)의 서명으로 재활용될 수 있다. 규격에서 "왜 이런 게 있지" 싶은 상수는 대개 이미 일어난 공격의 흔적이다.

*순서는 규격의 절반이다.* `next_ok` 는 받아들이는 자리를 열거하고, 열거하는 것이 곧 나머지를 거절하는 것이다. 보지 않으면 중간자가 Finished 를 앞당기거나
ClientHello 를 두 번 보낼 수 있다. *`session_id` 는 그대로 되울린다* --- TLS 1.3 은 그 필드를 쓰지 않지만, 되울리지 않으면 1.2 로 보이게 하려는 호환 장치 때문에
실제 망의 중간 상자에서 끊긴다(§4.1.3).

*전사는 이 층에서 세 번 잘린다.* `s hs traffic` 은 `CH‖SH`, 서버 Finished 는 `CH‖…‖CertificateVerify`, 응용 트래픽 비밀과 *클라이언트 Finished* 는 `CH‖…‖서버 Finished` 위에 선다. 클라이언트 Finished 는 자기 자신을 포함하지 않고, 그 verify_data 는 *클라이언트* 핸드셰이크 비밀에서 나온다 --- 같은 전사, 다른 비밀. 서버 것을
쓰면 언제나 거절하게 되고 증상은 "클라이언트가 이상하다" 로 보인다.

*응용 데이터는 방향마다 다른 비밀 · 다른 시퀀스다.* 하나를 공유하면 논스가 겹치고, AEAD 에서 논스가 겹치는 것은 평문과 인증키를 함께 잃는 일이다. 그래서 키뭉치를
한 방향씩 만든다. 시퀀스는 *레코드마다* 오르고 키 세대마다 0 부터다. `traffic_keys` 의 출력 버퍼는 키가 16 이어도 32 바이트여야 한다 --- `expand_label` 은 HMAC 한
블록을 쓰므로 언제나 32 를 쓴다. 경보도 같은 레코드다 --- 속 타입이 21 이고, 그것을 보지 않으면 경보를 데이터로 읽는다.

*확인하는 것* --- RFC 8448 §3 의 실제 핸드셰이크. 정본의 ClientHello 필드를 읽고, 메시지 다섯을 지어 정본과 바이트로 맞댄다(ServerHello 90 · EncryptedExtensions 40 ·
Certificate 445 · CertificateVerify 136 · Finished 36). 그리고 끝까지 엮는다 --- 정본의 키로 ECDHE 를 우리가 내고 키 스케줄을 올려 서버 Finished 를 정본과 맞춘다.
이 한 수가 전사 자르는 자리 · 키 스케줄 · finished_key · verify_data 를 한꺼번에 잰다. 정본의 CertificateVerify 는 RSA-PSS 이고 우리 서명기는 ECDSA 라, 여기서 재는
것은 틀이고 서명 자체는 #modref("ecdsa")[`ecdsa`] 가 따로 잰다.
