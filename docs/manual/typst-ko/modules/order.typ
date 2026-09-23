// 부록 E 의 모듈 차례 --- 층(L0 · L1 · L2) 안에서 쓰임새가 가까운 것끼리 둔다.
#let module-layers = (
  (ko: "L0 --- 순수 계산", en: "L0 --- pure computation", modules: (
    "strings", "strbuf", "fmt", "utf8", "utf16", "unicode", "codec", "regex", "term",
    "sortlib", "sortgen", "searchlib", "hashmap", "strmap", "vecs", "spsc",
    "hash", "math", "random",
    "hmac", "chacha", "poly", "aead", "x25519", "aes", "gcm", "crypto_hw", "bigint", "rsa", "p256", "ecdsa", "ed25519",
    "der", "pem", "tls13", "tlssrv", "http", "soa",
  )),
  (ko: "L1 --- 저장", en: "L1 --- storage", modules: (
    "allocs", "pool", "shard", "budget", "wire", "flags", "segarena", "pagecache",
    "growvec", "vecgen", "mapgen", "nodelist", "segview", "lifemode",
  )),
  (ko: "L2 --- 호스트", en: "L2 --- host", modules: (
    "io", "outbuf", "files", "tty", "net", "clock",
  )),
)
#let module-names = module-layers.map(l => l.modules).flatten()
