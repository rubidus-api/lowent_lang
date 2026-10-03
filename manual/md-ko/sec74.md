# <a id="mod-hash"></a>`hash` — 해시(해시맵 자리 · 손상 검출 · SHA-256)

소스

`lib/hash.low`

층

L0 — 순수 계산

권한

없음

바이트열에서 수를 낸다. 성격이 다른 셋이 한 모듈에 있고, 섞으면 위험한 쪽으로 틀린다.

> **`of` 와 `crc` 는 암호용이 아니다**
>
> > `of`(FNV-1a 64)는 **해시맵 자리** — 빠르고 잘 흩어지지만 암호용이 아니다. `crc`(반사 CRC-32)는 **우연한 손상** 검출 — 원하는 CRC 를 맞추는 것은 쉬우므로 위조를 막지 못한다. `digest` · `digest_ok`(SHA-256)는 암호 해시로, 무결성 증명은 이쪽이다. 키가 있는 인증은 `digest` 도 아니고 [`hmac`](sec77.md#mod-hmac) 이다 — 해시 하나로는 **누가 보냈는가** 에 답할 수 없다.

```lowent
use hash .

fn slot input key slice u8 . input nslots u64 . output u64 . do
  return hash.bucket_of key nslots .
end
```

**왜 알고리즘을 고정했나.** 해시는 그럴듯한 수가 나오면 맞아 보인다. 고정하지 않으면 검사할 것이 남지 않는다. 고정했기에 표준이 정한 검사값이 시험의 기준이 되고 (CRC-32 의 `"123456789"` → `0xCBF43926`), VM 과 네이티브가 갈리면 그 자리에서 드러난다.

| **op** | **모양** | **쓸모** |
|---|---|---|
| `of` | `(data slice u8) → u64` | FNV-1a 64 — 해시맵 자리 |
| `bucket_of` | `(data, nslots u64) → u64` | 칸 고르기(나머지 연산) |
| `bucket_mask` | `(data, mask u64) → u64` | 칸 수가 2 의 거듭제곱일 때 |
| `crc` | `(data slice u8) → u64` | 반사 CRC-32 — 손상 검출 |
| `crc_ok` | `(data, want u64) → bool` | 검사값 대조 |
| `maybe_same` | `(a, b slice u8) → bool` | 해시가 같은가 — 같다는 증명이 아니다 |
| `digest` | `(data, out mut slice u8) → u64` | SHA-256 — 쓴 바이트 수(32) |
| `digest_ok` | `(data, out mut slice u8, want slice u8) → bool` | 받은 다이제스트와 대조 |

*표 50.1 — `hash` 의 op*

`digest_ok` 가 있는 이유 — 해시의 올바른 쓰임은 왕복이 아니라 **대조**다. 다시 계산해 받은 것과 맞대는 것이 유일하게 옳은 쓰임이라 그 모양이 op 으로 있다(`want` 가 32 바이트가 아니면 그대로 `false` — 길이부터 계약(op 이 스스로 적는 약속)이다). `maybe_same` 에 `maybe` 가 붙은 이유 — 해시가 같아도 값이 같다는 뜻은 아니다. `same` 이라 불렀으면 이름이 거짓말이 됐을 것이다.

**짓지 않은 것** — 키드 해시(그 자리는 `hmac`), 시드 가능한 해시(HashDoS 방어), 스트리밍(조각으로 이어 먹이기), SHA-256 말고 다른 암호 해시.

---

[← 이전](sec73.md) · [목차로](README.md) · [다음 →](sec75.md)
