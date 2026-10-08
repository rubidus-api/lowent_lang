# <a id="mod-hash_legacy"></a>`hash_legacy` — 옛 해시(MD5 · SHA-1)

소스

`lib/hash_legacy.low`

층

L0 — 순수 계산

권한

없음

MD5 와 SHA-1 을 낸다. 둘 다 **보안에는 쓸 수 없는 해시**다. 보안이 아닌 자리 — 체크섬, 내용으로 이름 짓기, 옛 파일 읽기 — 에서는 지금도 널리 쓰인다.

> **보안에는 쓰지 않는다**
>
> > 두 해시는 같은 다이제스트를 내는 서로 다른 입력을 만들 수 있다 — MD5 는 2004 년에, SHA-1 은 2017 년에 실제 충돌이 나왔다. 서명 · 인증서 · 비밀번호 · 변조 검출에 쓰면 안 된다. 그 자리는 [`hash`](sec74.md#mod-hash) 의 `sha256`(SHA-256) · `sha384` · `sha512` 다. 이 모듈이 맞는 자리는 누군가 일부러 속이려 들지 않는 곳이다: 내려받은 파일이 깨지지 않았는지 보는 체크섬, 내용으로 이름을 짓는 저장소 형식, 캐시의 열쇠, 그리고 이미 MD5 나 SHA-1 로 적힌 옛 파일을 읽는 일.

```lowent
use hash_legacy .

proc sum input data slice u8 . input work mut slice u64 . input out mut slice u8 . output u64 . effects none . do
  return hash_legacy.md5 data work out .
end
```

**왜 모듈을 갈랐나.** 이름이 경고다. `hash.md5` 라고 적히면 `hash.sha256` 와 같은 무게로 읽힌다. `hash_legacy.md5` 는 읽는 사람이 한 번 멈추게 한다. 없어질 모듈이라는 뜻은 아니다 — 보안용 해시와 섞이지 않게 따로 둔 것이다.

**왜 빌트인이 아닌가.** 전부 이 언어로 썼다. SHA-2 가 컴파일러의 잎인 것은 비용 때문인데, 체크섬과 옛 파일 읽기는 그만큼 뜨거운 길이 아니다. 느린 대신 컴파일러에 기대는 것이 없다.

| **op** | **모양** | **쓸모** |
|---|---|---|
| `md5` | `(data slice u8, work mut slice u64, out mut slice u8) → u64` | MD5(RFC 1321) — 쓴 바이트 수(16) |
| `sha1` | `(data slice u8, work mut slice u64, out mut slice u8) → u64` | SHA-1(FIPS 180) — 쓴 바이트 수(20) |

*표 50.1 — `hash_legacy` 의 op*

`work` 는 호출자가 주는 작업 공간이다 — `md5` 는 16 낱말, `sha1` 은 80 낱말이 있어야 한다. 라이브러리는 몰래 할당하지 않는다. `out` 이나 `work` 가 모자라면 0 을 답하고 **아무것도 쓰지 않는다** — 조용히 덜 쓴 다이제스트는 맞아 보이기 때문이다.

시험의 기준은 규격이 정한 답이다. MD5 는 RFC 1321 부록 A.5 의 시험 묶음, SHA-1 은 FIPS 180 의 예에 대고, 덧붙임이 블록을 하나 더 쓰는 경계(길이 55 · 56 · 63 · 64)를 따로 댄다. VM 과 네이티브가 같은 답을 낸다.

**짓지 않은 것** — 스트리밍(조각으로 이어 먹이기), HMAC-MD5 · HMAC-SHA1, MD4 같은 더 옛 해시.

---

[← 이전](sec74.md) · [목차로](README.md) · [다음 →](sec76.md)
