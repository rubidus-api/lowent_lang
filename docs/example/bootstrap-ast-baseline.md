# 예제 — 부트스트랩 AST baseline (M0 gap ① 실증)

> RFC-0080 §4.2 의 **대조 baseline**. (원래는 RFC-0079 §9-1 의 M0 합격 픽스처였다 —
> 자기호스팅은 2026-07-31 철회됐고, 이 예제는 **페이로드 enum 의 예제로 남는다**.) 페이로드 enum(RFC-0080)이 언어에 서기
> **전에**, `lowentc.low` 가 필요로 하는 트리 짓기 기계 — **아레나 + 인덱스 자식 + 재귀 하강
> 순회** — 가 *지금 도는지*를 실측으로 확인한다. **19번째 예제.**

## 무엇을 증명하나

원래 물음은 M0 관문(철회된 RFC-0079)의 gap ① — "컴파일러 AST 를 어떻게 표현하나" 였다. 소유자는 페이로드
enum(RFC-0080)을 채택했지만, 그것이 구현되기 전에도 **트리의 밑기계**(자기참조 없는 노드
저장·인덱스로 잇기·인덱스를 타고 재귀)는 현행 Lowent 로 표현된다. 이 예제가 그 baseline 이다.

AST 를 **SoA 아레나**(구조체 배열이 아니라 병렬 슬라이스)로 둔다:

- `tag[i]` — 노드 종류: `0`=lit(리터럴), `1`=add(덧셈)
- `a[i]` — lit 이면 값, add 이면 **왼쪽 자식 인덱스**
- `b[i]` — add 이면 **오른쪽 자식 인덱스**

자식을 **인덱스**로 가리키므로 노드가 자기 자신을 값으로 담지 않는다(무한 크기 회피) —
RFC-0080 §4.2 가 페이로드 enum 의 재귀에 "간접 강제" 라 부른 것의, 인덱스 판(版)이다.

## 소스

```text
module ast_baseline .

fn eval
  input tag slice u8 .
  input a slice u8 .
  input b slice u8 .
  input i u8 .
  output u8 .
do
  guard eq (index tag i) 1 . else do return index a i . end
  return add (eval tag a b (index a i)) (eval tag a b (index b i)) .
end
```

`eval` 은 재귀 하강 평가의 축소판이다: **"이 노드가 add(tag==1)인가? 아니면 잎(lit)이니 값을
낸다. 맞으면 자식 둘을 재귀해 더한다."** 파서가 `is n add → 자식 둘 재귀` 로 도는 것과 같은
모양이며, 페이로드 enum 이 서면 `index tag i`·`index a i` 자리가 `is n add`·`get n add l` 로
바뀔 뿐 **뼈대는 동일**하다(RFC-0080 §5 가 그 목표 픽스처).

## 실행 — `2 + 3` 트리

노드 셋: `[0]=lit 2` · `[1]=lit 3` · `[2]=add(0,1)`. 루트는 인덱스 2.

```text
tag = [0,0,1]   a = [2,3,0]   b = [0,0,1]   i = 2

$ lowentc --run eval bootstrap-ast-baseline.low [0,0,1] [2,3,0] [0,0,1] 2
eval([0,0,1], [2,3,0], [0,0,1], 2) = 5

$ lowentc --emit-c … && cc … && ./bin eval [0,0,1] [2,3,0] [0,0,1] 2
eval([0,0,1], [2,3,0], [0,0,1], 2) = 5
```

**VM ≡ native, 둘 다 5** (2026-07-24 실측). 아레나·인덱스·재귀 순회가 두 백엔드에서 함께
돈다 — M0 의 트리 밑기계는 페이로드 enum 을 기다리지 않아도 이미 선다.

## 무엇이 아직 없나 (그래서 RFC-0080)

이 baseline 은 밑기계가 돎을 보이지만 **타입 안전하지 않다**: `tag`/`a`/`b` 가 그냥 u8 슬라이스라
"tag==1 인데 a 를 값으로 읽는" 실수를 컴파일러가 못 막는다. RFC-0080 의 페이로드 enum 은
그 실수를 **타입으로** 막는다(`get n add l` 은 활성 변형 확인 후에만 유효, `E-ENUM-UNCHECKED`).
그것이 소유자가 "넓게" 를 고른 이유다 — 언어가 AST 를 **안전하게** 자기 자신으로 쓴다.

## 관련

- ~~RFC-0079 §6-① · §9-1~~ (철회·아카이브 — RFC-0088)
- RFC-0080 (페이로드 enum — 이 baseline 의 타입 안전판, §5 가 목표 픽스처)
- `docs/example/generational-handle.md` (인덱스 참조의 동적 안전 = 세대핸들)
