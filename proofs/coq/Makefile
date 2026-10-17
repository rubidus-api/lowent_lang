# Coq proofs. `coqc` must be on PATH — see README.md
# (workstation: pacman/apt install coq;  container: . ~/coqroot/env.sh)
COQC ?= coqc

# 순수 Coq — 외부 라이브러리가 필요 없다.
VS  := NumericLattice.v LowentBlock.v LowentEXCL.v LowentLoop.v LowentPlace.v LowentLaunder.v \
       LowentRC11.v LowentRC11SC.v LowentRC11Sweep.v LowentRC11Mono.v LowentOrderExt.v \
       LowentCert.v LowentPar.v LowentDRF.v LowentNest.v LowentEffect.v LowentType.v \
       LowentJoin.v LowentTypeStore.v LowentOpt.v LowentDeadlock.v LowentFair.v \
       LowentHash.v LowentEnum.v LowentMono.v

# Iris 가 필요하다 (sh scripts/setup-iris-userland.sh  또는  opam install coq-iris).
IVS := LowentLock.v LowentRWLock.v

# ★★ **iRC11(gpfsl)** 이 필요하다 — 그리고 그것은 **Rocq 9.x + Iris dev** 를 요구한다.
#   `sh scripts/setup-rocq9-userland.sh` 로 Rocq 9.2 를 짓고, 그 위에 stdpp/iris dev, gpfsl 을 얹은 뒤
#   `. ~/rocqroot/env9.sh` 를 source 해서 짓는다. **Coq 8.20 으로는 안 된다.**
#   ⇒ 그래서 별도 변수다: `make` 의 기본 목표에 넣으면 8.20 환경에서 전체가 실패한다.
#     도구가 없는 환경에서 **다른 것까지 못 짓게 만들지 않는다.**
WVS := LowentIRC11.v LowentSPSC.v

weak: $(WVS:.v=.vo)

# ★★ **추출된 판정자**(RFC-0086 §6-2) — 증명에서 **파생된 실행 파일**.
#   zarith 의 **인터페이스**가 필요하다(`.cmi`). 이 저장소의 Rocq 9 부트스트랩이 그것을 짓는다
#   (`scripts/setup-rocq9-userland.sh`). 없으면 `check-cert-extract.sh` 가 **건너뛴 것을 말한다.**
SITE ?= $(HOME)/rocqroot/site
Z    ?= $(SITE)/ocaml/zarith

cert_check: LowentCertExtract.v LowentCert.vo cert_main.ml
	$(COQC) $(COQFLAGS) LowentCertExtract.v
	ocamlopt -I $(Z) $(Z)/zarith.cmxa lowent_cert.mli lowent_cert.ml cert_main.ml -o cert_check

all: $(VS:.v=.vo) $(IVS:.v=.vo)

# Iris 가 없는 환경에서: 순수 Coq 부분만 검증한다.
pure: $(VS:.v=.vo)

# ★ `LowentRC11SC.v` 는 **`LowentRC11.v` 를 Require 한다** — 이 저장소에서 파일 사이 의존이
#   생긴 첫 자리다(모델을 베끼지 않고 빌려 쓴다 — 두 벌 금지). 그래서 두 가지가 필요하다:
#     · `-Q . ""`  — 현재 디렉터리를 로드 경로에 넣는다(없으면 Require 가 못 찾는다)
#     · 아래 순서 의존 — 병렬 make 에서도 RC11 이 먼저 컴파일되게 한다
COQFLAGS ?= -Q . ""

LowentRC11SC.vo: LowentRC11.vo
# ★ 전수 열거는 **93 초** 걸린다(실측). 다른 파일은 전부 몇 초다 — 그 차이를 적어 둔다:
#   전수 검사는 증명보다 **비싸다**. 그것이 봉투를 작게 유지하는 이유이고, 봉투를 키우려면
#   계산을 Coq 밖으로 내야 한다(그러면 또 하나의 "믿을 대상" 이 된다 — 그 교환을 눈감지 않는다).
LowentRC11Sweep.vo: LowentRC11.vo LowentRC11SC.vo
LowentOrderExt.vo: LowentRC11.vo LowentRC11SC.vo
# ★ 단조성 **일반 정리** — 전수(Sweep)의 반례 계산을 그대로 쓰므로 셋 다 앞선다.
LowentRC11Mono.vo: LowentRC11.vo LowentRC11SC.vo LowentRC11Sweep.vo
# ★ 중첩 루프는 `LowentLoop.v` 의 agreement_run 을 **빌려 쓴다**(두 벌 금지).
LowentNest.vo: LowentLoop.vo

%.vo: %.v
	$(COQC) $(COQFLAGS) $<

clean:
	rm -f *.vo *.vok *.vos *.glob .*.aux

.PHONY: all pure clean
