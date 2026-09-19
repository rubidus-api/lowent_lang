# `apps/` — 이 언어로 지은, **사람이 돌리는 프로그램**

여기 있는 것은 픽스처가 아니라 **제품**이다. 저마다 폴더 하나를 갖고, 그 안에
자기를 소개하는 글(`README.md`) · 쓰는 법(`doc/manual.md`) · 정체(`pkg.low`) ·
소스(`src/`)가 함께 산다.

| 앱 | 무엇을 하나 | 바깥 오라클 | 성숙도 |
|---|---|---|---|
| [`lowdiff`](lowdiff/) | 두 폴더를 **내용으로** 비교한다 | `diff -rq` | experimental |
| [`lowget`](lowget/) | `https://…` 에 붙어 본문을 받는다 — 인증서를 확인하면서 | `curl` · `openssl s_server` | experimental |

## 이 서랍의 규율 셋

**① 앱은 구현체를 부르지 않는다.** 소스는 `impl/` 안의 무엇도 가리키지 않는다. 기대는 것은
**언어와 표준 라이브러리**뿐이고, 그래서 `use net .` 처럼 **이름으로** 부른다 — 어느 파일인지는
도구가 표준 모듈로 해소한다(RFC-0011 §6.3). 다른 구현체·다른 설치 자리에서도 같은 소스가 선다.

**② 앱마다 셋이 다 있다** — `pkg.low`(정체·판·진입점) · `README.md`(무엇을·왜) ·
`doc/manual.md`(쓰는 법·종료 코드·한계). 하나라도 없으면 그것은 앱이 아니라 스크립트다.

**③ 답을 바깥 도구와 맞댄다.** 이 서랍에 들어오는 조건은 *"돈다"* 가 아니라
*"남이 만든 것과 같은 답을 낸다"* 이다. 위 표의 **바깥 오라클** 칸이 그 자리다.

## 짓는 법

```sh
impl/build/lowentc --emit-c apps/lowget/src/lowget.low > /tmp/lowget.c
cc -O2 -o /tmp/lowget /tmp/lowget.c -lm
/tmp/lowget https://example.com/
```

VM 으로도 같은 답을 낸다(대조용):

```sh
impl/build/lowentc --run main apps/lowdiff/src/lowdiff.low before/ after/
```

★ 네이티브로 낸 실행 파일은 `main` 에 인자를 **자기 argv 로** 받는다(`cap args`) —
`--run` 의 디스패처가 아니라 프로그램의 것이다(RFC-0030 D2′).
