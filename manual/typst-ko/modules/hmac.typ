#import "../lib.typ": *

= `hmac` --- HMAC-SHA256 과 HKDF <mod-hmac>

#modhead(file: "lib/hmac.low", layer: [L0 --- 순수 계산(호출자의 뒷받침)], caps: [없음])

두 가지를 한다. *HMAC* 은 키를 아는 사람만 만들 수 있는 증표(`mac`)로 "누가 보냈는가" 를 답한다(RFC 4231). *HKDF* 는 하나의 비밀에서 여러 키를 갈라내는 표준
방법(`extract` + `expand1`)이다(RFC 5869).

#aside[무엇을 약속하고 무엇을 하지 않나][
  상수 시간을 약속하지 않고 감사받지 않았다. *MAC 비교는 부르는 쪽의 몫이다* --- 이른 반환 없이(바이트를 XOR 로 누적해) 비교한다. 먼저 다른 바이트에서 돌아오는
  비교는 태그를 한 바이트씩 알아내게 해 준다. `expand1` 은 *한 블록(32 바이트 이하)만* 낸다.
]

*왜 해시로는 안 되나.* #modref("hash")[`hash`] 의 `sha256`(SHA-256)는 "내용이 그대로인가" 만 답한다. 누구나 다시 계산할 수 있으므로 누가 보냈는지는 답하지 못한다.
키가 들어가야 그 물음이 열린다. 뒷받침(`scratch`)은 호출자가 든다. 크기 요구가 문서가 아니라 *가드*로 박혀 있어서, 모자라면 0 을 답하고 아무것도 쓰지 않는다.

#dtable(
  columns: 3,
  id: "mod-hmac-ops",
  caption: [`hmac` 의 op],
  [*op*], [*하는 일*], [*요구*],
  [`mac`], [`HMAC-SHA256(key, msg)` → `out` 32 바이트], [`out ≥ 32` · `scratch ≥ 64 + len(msg)`],
  [`key_block`], [K′(64 바이트로 맞춘 키)를 만든다], [`pad ≥ 64`. 64 보다 긴 키는 해시로 줄인다],
  [`extract`], [HKDF-Extract: `PRK = HMAC(salt, ikm)`], [`mac` 과 같다],
  [`expand1`], [HKDF-Expand 의 첫 블록: `T(1) = HMAC(prk, info ‖ 0x01)`], [`scratch ≥ 130 + len(info)` · `len(info) ≤ 60`],
)

*왜 `expand1` 은 한 블록뿐인가.* TLS 1.3 의 `HKDF-Expand-Label` 은 32 바이트 이하만 쓴다(키·IV·finished 모두). 그래서 T(2) 를 잇는 반복을 짓지 않았다. *왜 60 에서
자르나.* `msg` 가 `scratch` 안(128 부터)에 살고 `mac` 이 `scratch[0 .. 64+len(msg)]` 를 덮어쓰므로, `info` 가 길면 둘이 겹쳐 해시가 자기 입력을 밟는다. 겹치는 순간을
가드가 거절한다 --- 조용히 틀리느니 거절한다.

*확인하는 것* --- RFC 4231 · RFC 5869 표준 벡터와 VM·네이티브 일치. *짓지 않은 것* --- 상수 시간 보장, MAC 비교 op, 여러 블록 HKDF-Expand(L > 32), SHA-256 밖의 해시,
이어 먹이는 MAC. 함께 보기 --- `hash`(SHA-256 자체) · #modref("aead")[`aead`] (갈라낸 키를 쓰는 자리) · #modref("x25519")[`x25519`] (그 비밀을 만드는 자리).
