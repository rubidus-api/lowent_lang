# λLowent 건전성 종이 증명 스케치 (V2)

- 상태: **스케치(draft)** — RFC-0017 §8 V2. 기계 증명(V3) 전 *설계 결함 조기 발견*이 목적.
- 소관: RFC-0017(건전성 모델), RFC-0004(region·ESC·provenance), RFC-0005(EXCL·EXCL-REGION·provenance 집계).
- 범위: **정적** 안전 핵심만(L-EXCL·L-ESC·L-MEET·L-SPLIT·**L-UNIT 통합·L-REGION-PAR 병렬 sub-region**). 그래프 핸들 dangling(L-GEN)은 *런타임* 이라 정적 증명 밖(§7). unsafe 계약은 V4(semantic typing).

> **동반 문서**: [lambda-lowent-core-agreement.md](lambda-lowent-core-agreement.md) —
> 구현된 단편(interval-EXCL v2 ≡ borrow-stack VM)의 합치 정리(직선 완증 + 루프 span)와
> V2가 실제로 찾은 결함 2건의 기록(2026-07-10).

> 표기는 ASCII 수학. 이 문서는 *증명*이 아니라 *증명 스케치* — 불변식·정리·핵심 케이스를 적고, **발견된 설계 결함·갭(§8)** 을 남긴다. 그게 V2 의 산출물이다.

---

## 1. λLowent 핵 — 구문

작은 핵(P1): region·참조(ref/mut_ref)·provenance·EXCL·집계(pair=struct 대용)만. 핸들/그래프/동시성은 확장(§7).

```
region 이름   ρ            (provenance 라벨; outlives 부분순서 ⊒ — "ρ ⊒ ρ' = ρ 가 ρ' 만큼 이상 산다")
값            v ::= () | n | loc                     (loc = 저장소 위치)
식            e ::= v | x
                  | region ρ in e                    (ρ 도입, 스코프 한정)
                  | alloc[ρ] e                        (ρ 에 할당 → loc)
                  | ref[μ] p | deref r | r := e       (μ ∈ {shr, exc};  p=place, r=참조값)
                  | let x = e1 in e2
                  | pair e1 e2 | proj[i] e             (집계=struct 축소판)
place          p ::= x | proj[i] p                    (별칭 단위, §6.2 D2 서로소 필드)
타입          τ ::= unit | int | ref[μ,ρ] τ | (τ1 × τ2)
접근          μ ::= shr (=ref, 공유 읽기) | exc (=mut_ref, 배타 쓰기)
```

provenance 는 *값에* 붙는다: 런타임 loc 은 자신이 사는 region ρ 를 안다(아래 store).

## 2. 런타임 상태 (operational)

```
Store   S : loc ⇀ (v, ρ)          저장소. 각 loc 은 값 + 거주 region.
Regions Λ : ρ 들의 스택 + ⊒ 순서.  live(ρ) = Λ 에 있음.
Ledger  B : loc ⇀ { free | shr(k) (k≥1) | exc }     borrow 원장(EXCL 의 동적 그림자).
Config  C = (Λ, S, B, e)
```

핵심 축약 규칙(발췌):
```
ALLOC   (Λ,S,B, alloc[ρ] v) → (Λ, S[loc↦(v,ρ)], B[loc↦free], loc)    ρ∈live, loc fresh
REF-S   (Λ,S,B, ref[shr] p) → (…, B[ℓ_p ↦ shr(k+1)], loc_p)           B(ℓ_p)≠exc
REF-X   (Λ,S,B, ref[exc] p) → (…, B[ℓ_p ↦ exc],      loc_p)           B(ℓ_p)=free
WRITE   (Λ,S,B, loc := v)   → (Λ, S[loc↦(v,ρ)], B, ())                B(loc)=exc 인 ref 경유
DEREF   (Λ,S,B, deref loc)  → (…, S(loc).v)                           B(loc)∈{shr,exc}
ENDB    (borrow scope 종료)  B(ℓ): shr(k)→shr(k-1)|free, exc→free      (reborrow 복귀)
EXIT    (Λ·ρ, S, B, region ρ in v) → (Λ, S∖dom_ρ, B∖dom_ρ, v)         ρ pop·dom_ρ 회수
```

## 3. 정적 의미 (judgment) — 핵심 규칙

흐름 민감(NLL 류) 판단:  `Γ; Δ ⊢ e : τ ⊣ Δ'`  (Δ = borrow/region 문맥; 사용 후 Δ' 로 갱신).

```
T-ALLOC   Γ;Δ ⊢ e:τ⊣Δ      ρ∈Δ.live          ⟹  Γ;Δ ⊢ alloc[ρ] e : ref[exc,ρ] τ ⊣ Δ
T-REF-X   p:τ ∈ Γ   ¬borrowed(Δ,ℓ_p)          ⟹  Γ;Δ ⊢ ref[exc] p : ref[exc,ρ_p] τ ⊣ Δ+exc(ℓ_p)
T-REF-S   p:τ ∈ Γ   ¬exc-borrowed(Δ,ℓ_p)      ⟹  Γ;Δ ⊢ ref[shr] p : ref[shr,ρ_p] τ ⊣ Δ+shr(ℓ_p)
T-WRITE   Γ;Δ ⊢ r:ref[exc,ρ]τ   Γ;Δ ⊢ e:τ     ⟹  Γ;Δ ⊢ r:=e : unit ⊣ Δ
T-REGION  Γ; Δ·ρ ⊢ e:τ⊣Δ'·ρ    ρ ∉ fv_ρ(τ)   ⟹  Γ;Δ ⊢ region ρ in e : τ ⊣ Δ'     (ESC: τ 에 ρ 없음)
T-PAIR    Γ;Δ⊢e1:τ1   Γ;Δ⊢e2:τ2               ⟹  Γ;Δ ⊢ pair e1 e2 : (τ1×τ2) ⊣ …
          (집계 provenance: prov(τ1×τ2) = prov(τ1) ⊓ prov(τ2)  — meet, §6.5a A1)
```

`fv_ρ(τ)` = τ 에 나타나는 자유 region. T-REGION 의 `ρ∉fv_ρ(τ)` 가 **ESC 3지점(return)** 의 핵심형 — 결과 타입이 ρ 를 참조하면 거부.

## 4. 안전 불변식 INV(Λ,S,B,Δ)

well-typed config 가 *유지*하는 불변식. 건전성은 "INV 가 모든 step 에서 보존됨"으로 환원.

```
I1 (provenance 유효)  모든 도달가능 loc 값에 대해: S(loc)=(v,ρ) ⟹ ρ∈live(Λ).   (dangling 없음)
I2 (원장 일치)        B(ℓ)=exc ⟺ Δ 에 ℓ 로의 live exc 참조 1개;  B(ℓ)=shr(k) ⟺ live shr 참조 k개.
I3 (EXCL)             ∀ℓ: ¬(B(ℓ)=exc ∧ 동시에 shr).  즉 readers XOR writer.   (§6.3)
                      일반화 I3-UNIT: ∀unit u. u 와 overlap 하는 live borrow 는 all-shr xor single-exc
                      (place/region/field/split 을 관통; L-UNIT). ℓ 은 u=place 특수화.
I4 (region 중첩·ESC)  live region 은 ⊒ 스택; 도달가능 값의 provenance ρ 는, 그 값이 흐를 수 있는
                      모든 목적지 d 에 대해 ρ ⊒ d.   (escape 없음)
I5 (집계 meet)        집계 값의 provenance = 필드 provenance 들의 ⊓.   (§6.5a A1)
```

## 5. 정리

**정리(건전성, 목표).** `∅;∅ ⊢ e:τ⊣Δ'` 이고 INV 가 초기 config 에 성립하면, e 의 실행은
*막히지 않거나(progress) 값에 도달*하고, 도중 **(¬use-after-free) ∧ (¬dangling) ∧ (¬place-race)** 를 만족한다.

표준 syntactic 안전(Wright–Felleisen) 형태로 분해:

- **Progress.** `⊢e:τ` 이고 INV 면 e 는 값이거나 ∃ step. *핵심*: `deref loc`/`loc:=v` 의 loc 은
  타입상 `ref[μ,ρ]` 이고 I1 로 ρ∈live ⟹ S(loc) 존재 ⟹ step 가능. **stuck(=UAF/dangling) 불가.**
- **Preservation.** step 이 타입과 INV 를 보존. (각 규칙별 케이스; §6 에서 INV 절별로 스케치.)

⟹ **따름정리.** 진행 중 어떤 config 도 "해제된 loc 접근"(UAF)·"죽은 region 참조"(dangling)·"동일 place 동시 write+access"(race) 상태에 들어가지 않는다. ∎(목표)

## 6. 보조정리 스케치 (INV 보존)

### L-EXCL — I3(readers-XOR-writer) 보존
B 를 바꾸는 step 은 REF-S/REF-X/ENDB·WRITE 뿐.
- **REF-X**: T-REF-X 전제 `¬borrowed(ℓ_p)` ⟹ 직전 B(ℓ_p)=free(I2). 후: exc, shr 없음 ⟹ XOR ✓.
- **REF-S**: 전제 `¬exc-borrowed` ⟹ 직전 B(ℓ_p)≠exc. 후: shr(k+1), exc 없음 ⟹ XOR ✓.
- **WRITE**: B 불변(exc 유지). write 는 B(loc)=exc 경유(T-WRITE) ⟹ 그 순간 shr 없음(I3) ⟹ 쓰기와 동시 reader 없음.
- **ENDB**: 감소/free — XOR 깨지 않음.
⟹ **데이터레이스 부재(place)**: write step 은 exc 를 요구하고 exc 동안 다른 접근 없음(I3). place 서로소(§6.2 D2)면 서로 다른 ℓ 이라 무간섭. ∎(스케치)

### L-ESC — I1·I4(dangling/escape) 보존
위험 step 은 **EXIT**(ρ pop·dom_ρ 회수)뿐.
- T-REGION 의 `ρ∉fv_ρ(τ)` ⟹ `region ρ in e` 의 결과값 v 의 타입 τ 에 ρ 없음 ⟹ I5/I4 로 prov(v) ⊒ (바깥 region), ρ 는 그 아래(스택) ⟹ **v 는 dom_ρ 를 가리키지 않는다.**
- 따라서 EXIT 후에도 도달가능 값 중 provenance=ρ 인 것 없음 ⟹ I1 유지(회수된 loc 으로의 live 참조 0). ✓
- 잔여 채널 점검(중요): v 외에 dom_ρ 가 새는 경로 = field-store(집계에 ρ 참조 저장)·out-param. λLowent 는 *가변 전역·캡처 클로저가 없어* 이 둘 뿐 ⟹ ESC 3지점이 *완전*(§8-G2). ∎(스케치)

### L-MEET — I5(집계 provenance) 건전성
주장: 집계 provenance = 필드들의 **meet ⊓** 이 *정확한* 건전 경계.
- ESC 는 "값 provenance ρ 가 목적지 d 로 흐를 수 있다 ⟺ ρ ⊒ d". 집계가 ρ1,ρ2 로의 참조를 담으면
  d 에서 안전 ⟺ (ρ1⊒d) ∧ (ρ2⊒d).
- ⊓ 가 glb 이므로:  ρ1⊓ρ2 ⊒ d  ⟺  d ⊑ ρ1⊓ρ2  ⟺  d⊑ρ1 ∧ d⊑ρ2  ⟺  ρ1⊒d ∧ ρ2⊒d.
- 즉 `prov(집계)=ρ1⊓ρ2` 로 두면 집계 ESC 가 *두 필드 모두* 의 조건과 **동치** — 과·과소 없이 정확. ∎
- (mut_ref 필드 → 집계 affine: I2/I3 가 그 exc 를 집계 liveness 동안 유지 ⟹ 복제 시 exc 둘 → I3 위반이라 copy 금지. §6.5a A5 와 일치.)

### L-SPLIT — level-1 병렬 레이스 부재 (개요)
disjoint split 은 mut slice 를 place-서로소(§6.2 D3) sub-slice k 개로 나눔. 각 task 가 한 sub 의 exc.
서로소 ⟹ 두 task 의 place 집합 ∩ = ∅ ⟹ L-EXCL 의 "서로 다른 ℓ 무간섭" 을 *병렬 합성* 에 적용 ⟹ race 없음.
**단 병렬 의미론(인터리빙) 정의 필요**(§8-G1). ∎(개요)

### L-REGION — EXCL-REGION (개요)
그래프를 region R 의 *한 place* 로 취급: R 변경은 R 의 exc(write-token) 보유 시만, I3 를 *region 입도* 로.
⟹ 그래프 직렬변경 데이터레이스 부재. **단 핸들 dangling 은 미포함**(아래 L-GEN). 입도 건전성은 아래 L-UNIT.

### L-UNIT — 입도 통합 (RFC-0017 §6.6 흡수)
L-EXCL(place)·L-REGION(region)·L-FIELD(field)·L-SPLIT(disjoint)를 *한 정리*로 통합한다.
- **access-unit 격자** U:  region ⊒ root ⊒ place ⊒ field(RFC-0017 §6.6). 원장을 unit 색인으로 일반화:
  `B : unit ⇀ {free | shr(k) | exc}`.  `overlap(u,u')` = ⊑-비교가능(포함) ∧ ¬disjoint(§9.3 MA).
- **UNIT-EXCL(I3 일반화)**:  ∀u. u 와 *overlap* 하는 live borrow 집합은 (all shr) xor (single exc).
  unit u 에 exc 취득 = u 와 overlap 하는 모든 live borrow(조상 R·자손 field)와 충돌 → "경로 borrow 가
  그 prefix·extension 을 잠근다"의 격자판(place-tree borrow 의 일반화).
- **보존**:  L-EXCL 의 케이스 분석에서 `may-alias(p,q)` 를 `overlap(u,u')` 로 치환하면 그대로 성립 —
  REF-X/REF-S/WRITE/ENDB 가 UNIT-EXCL 을 보존(borrow-stack G2 가 시간축 LIFO 제공). disjoint unit
  (다른 subtree·다른 color)은 결코 충돌 안 함 → 병렬 가능(L-SPLIT/L-REGION-PAR).
- ⟹ L-EXCL(u=place)·L-REGION(u=R)·L-FIELD(u=field)·L-SPLIT(disjoint u)은 **L-UNIT 의 입도별 특수화**. ∎(스케치)

### L-REGION-PAR — 병렬 sub-region 토큰 (SR3 보존; G1·G4 해소)
`split region R into R1…Rn`(RFC-0005 §6.3b): 각 task Ti 가 Ri 의 exc sub-token. **frame + confluence 논증.**
- **store 분해**:  S = S_1 ⊎ … ⊎ S_n ⊎ S_F.  S_i = interior(Ri) 사유 슬롯,  S_F = frozen(구조·read-only·경계 에지).
  (이 분해의 *존재*는 AUTOCUT 이 준다: color 총함수(AC-SAFE) ⟹ 슬롯이 정확히 하나의 Ri → ⊎ 이 well-defined.
   정적 shape(AC-GRID/TREE/FOREST) 또는 런타임 coloring(AC-COLOR)이 witness. 즉 disjoint 전제 = AUTOCUT 의 산물.)
- **SR3 공통 불변**:  writes(Ti) ⊆ S_i,  reads(Ti) ⊆ S_i ∪ S_F.  (mode P: M 파티션 사유·F 전역 read;
  mode S: 내부 전체 사유·경계 F 만 read.)
- **무간섭(Bernstein/frame)**:  i≠j 에서  W_i∩W_j=∅(사유 서로소)·W_i∩R_j=∅(R_j⊆S_j∪S_F, W_i⊆S_i 는 S_j 와 서로소·
  S_F 는 frozen 이라 W 없음). ⟹ write-write·read-write·write-read 해저드 모두 없음.
- **교환(commute)**:  서로 다른 task 의 두 step 은 footprint 서로소 ⟹ s_i;s_j ≡ s_j;s_i (분리논리 frame 규칙,
  disjoint-heap 교환). **이 국소 교환성이 G1 이 요구한 병렬 operational 의미를 준다** — 인터리빙이 confluent.
- **confluence(Newman)**:  모든 교차 step 쌍이 교환(국소 confluence) + 종료 ⟹ 전역 confluence ⟹ *모든 인터리빙이
  같은 최종 store* ⟹ 임의 순차 순서와 동일 ⟹ **level-1 결정론**(RFC-0009). = L-SPLIT 의 region/graph 판.
- **UNIT-EXCL 보존**:  Ri 별 exc 는 unit Ri 위·파티션이라 Ri 서로소(disjoint) ⟹ overlap exc 쌍 없음 → 전역
  UNIT-EXCL 유지. S_F 는 shr(read)만. rejoin(SR4)에서 sub-token → R 단일 exc 병합(LIFO). ∎(스케치)
- ⟹ **G4 해소**(region-token 입도 = disjoint unit)·**G1 부분 해소**(disjoint 케이스의 병렬 의미 = confluent 인터리빙).
  잔여: 비-disjoint 공유(level 2/3)의 인터리빙은 여전히 범위 밖.

## 7. 정적 밖 — L-GEN (런타임, dynamic)
그래프 핸들 use-after-free(슬롯 재사용)는 정수 핸들이 borrow 원장을 *우회*하므로 정적 INV 로 안 잡힘.
런타임 적합성으로: 풀이 슬롯 재사용 시 gen 증가, 접근은 `handle.gen == slot.gen` 검사 ⟹ stale 거부.
⟹ **이 속성은 등급 dynamic(§4.6)**, 본 정적 증명의 정리에 *포함 안 됨* — 정직히 분리(과약속 금지).

## 8. 발견된 설계 결함·갭 (V2 의 핵심 산출물)

```
G1  [✅ 해소(스케치)] 병렬 의미론이 level 별로 갈렸다:
    · level-1(disjoint): L-REGION-PAR — footprint 서로소 step 교환(frame) + confluent(Newman) ⟹ 순서 무관 결정론.
    · level-2(actor/channel): 메시지 happens-before(RFC-0009 §6.5a MHB1~5) — actor 내부 순차 + send/recv
      synchronizes-with. move/공유read(AC4)라 *공유 가변 별칭 없음* ⟹ MHB 밖 접근이 같은 셀 안 건드림 = race 없음.
    · 전역: DRF 정리(§6.5a DRF1~4) — 안전 코드의 모든 동시 공유 가변은 L1 disjoint ∪ L2 actor-격리로 매개 ⟹
      데이터 race 없음 ⟹ DRF-SC(RFC-0018 §6.2)로 SC. RC11 약한 모델은 level-3 atomic(system·cap·audit)으로만 노출.
    ⟹ "비-disjoint 공유 인터리빙"은 (a) level-2 MHB 부분순서(비결정이나 race 아님)·(b) level-3 RC11(정적 밖·책임
    영역)로 정확히 분리. 잔여: DRF-SC·MHB 의 기계 증명(iGPS/Cosmo)은 V3(§9).
G2  [✅ 해소] ★reborrow = borrow-stack: 단순 원장 shr(k)/exc 는 *중첩 reborrow*(mut_ref→ref 하강 후
    복귀, §6.6 R1)에 불충분 — 어느 reborrow 가 어느 부모를 동결했는지 *스택* 으로 추적해야(Rust 의
    Stacked/Tree Borrows 지점). **→ RFC-0005 §6.6.1 로 형식화**: place 별 borrow-stack(프레임=exc|shr
    그룹, push/use(LIFO)/pop/freeze), EXCL 스택판 불변식(top 활성 프레임 단일). L-EXCL 의 reborrow
    케이스가 이로써 닫힘. Lowent 단순화(place 입도·정적·LIFO·raw 제외)로 Rust 보다 가벼움.
G3  [✅ 해소] ESC 3지점 완전성(L-ESC): "가변 전역·캡처 클로저 없음" 가정을 **언어 불변식으로 명문화** —
    **ESC-INV-1**(전역 가변 없음, RFC-0011 §6.1: 모듈 최상위 불변 선언만·var 는 op 지역 전용) +
    **ESC-INV-2**(캡처 클로저 없음, RFC-0010 §5: word-level op 참조만). 정식 정의 = RFC-0004 §6.6.1.
    ⟹ escape 경로 = {return, field-store, out-param} 뿐 ⟹ ESC 3지점 완전. comptime static region(RFC-0015)
    은 별 provenance(프로그램 수명)라 무관, task/actor 경계는 move/공유라 borrow escape 아님.
G4  [✅ 해소] EXCL-REGION 입도(L-REGION): 부분 region(sub-region) 토큰 = access-unit 격자의 *R-disjoint
    unit*(L-UNIT). 병렬 건전성 = L-REGION-PAR(frame+confluence). 기제는 RFC-0005 §6.3b(SR1~6·CUT1~5),
    통합은 RFC-0017 §6.6. 잔여: Iris 에서 frame 규칙·Newman confluence 기계화(V3).
G7  [✅ 해소·V2 발견] ★sub-region 교차 read 레이스: 초안 SR3b("교차 핸들로 대상 노드 payload 읽기 OK")는
    owner 의 동시 write 와 read-write 레이스. **교정: 교차 read 를 frozen 집합 F(구조·read-only)로 제한**,
    mutable M 은 파티션 사유(RFC-0005 §6.3b SR3 mode P/S). 상대 M 필요 시 split 진입 전 스냅샷. L-REGION-PAR
    의 "reads(Ti) ⊆ S_i ∪ S_F" 불변이 이를 강제 — V2 스케치가 잡은 설계 결함.
G5  핸들/정수의 정적 불투명성: L-GEN 이 dynamic 인 건 정직하나, "핸들이 정적 안전을 *우회*한다"는
    사실을 타입에 표식(예: handle 접근은 effects/등급에 dynamic 표시)할지 — RFC-0017 §6.1 카탈로그 연동.
G6  drop 순서·affine 소비와 I2 의 상호작용(move 후 borrow 원장 정리) — 미세 케이스.
```

## 9. V3(Iris/Coq)로의 사상

```
Store S·Region Λ → Iris heap + region(lifetime) 토큰.   loc↦(v,ρ) = points-to + region 소유.
Ledger B(EXCL)   → 분수 소유(fractional): shr = 분수 perm, exc = full perm.  (RustBelt lifetime logic 재사용)
ESC(I4)·EXIT     → region invariant + later;  region pop = invariant 닫기.
borrow-stack(G2) → Stacked/Tree Borrows 의 Iris 모델 차용.
L-UNIT           → access-unit 격자 = Iris 의 계층 소유(nested points-to)·overlap = 소유 겹침. UNIT-EXCL = 분수 perm.
L-REGION-PAR     → S = ⊎ S_i ⊎ S_F 는 분리논리 *separating conjunction*. Iris **par 규칙**+frame(native) 로
                   disjoint sub-token task 합성. confluence = Newman(국소 confluence+종료, 별도 Coq). RustBelt 병렬 소유 분할 재사용.
DRF-SC(level 2/3)→ level-2 MHB(RFC-0009 §6.5a)=메시지 passing invariant·level-3=iGPS/Cosmo(RC11 release/acquire 토큰).
                   "안전 코드 race-free ⟹ DRF-SC" = safe fragment 의 no-race 로 RC11 이 SC 로 접힘.
정리             → RustBelt 의 "semantic typing ⟹ adequacy" 골격. λLowent 가 더 거칠어 *부담↓* 예상.
단계 계획         → RFC-0017 §6.7 V3a(순차핵)→V3b(L-UNIT)→V3c(borrow-stack)→V3d(par L-REGION-PAR)→V3e(DRF-SC).
```
**계보 신뢰**: Cyclone(region 타입안전)·Tofte-Talpin·RustBelt 가 각 조각을 이미 증명 — 조합 + 거친 EXCL.

---

**V2 결론:** 핵심 보조정리(L-EXCL·L-ESC·L-MEET)는 표준 progress+preservation 으로 *스케치 수준에서 성립* 하며, **L-MEET 은 meet 가 정확한 경계임을 깔끔히 증명**한다. 가장 중요한 발견 **G2 — reborrow 에 borrow-stack 필요 — 는 즉시 해소**됐다: **RFC-0005 §6.6.1** 에 place 별 borrow-stack 규율(프레임=exc|shr 그룹, push/use(LIFO)/pop/freeze, EXCL 스택판 불변식)로 형식화 — Lowent 의 place 입도·정적·LIFO·raw 제외 덕에 Rust Stacked/Tree Borrows 보다 *가벼운* 인스턴스다. 이로써 L-EXCL 의 reborrow 케이스가 닫히고 V3 의 borrow-stack 의무가 충족된다.

이번 라운드 산출: **L-UNIT**(place/region/field/split 을 access-unit 격자의 한 정리로 통합) + **L-REGION-PAR**
(병렬 sub-region 토큰을 frame+confluence 로 증명 — **G4 해소·G1 부분 해소**). 그리고 V2 스케치가 **설계 결함
G7 을 발견**: sub-region 교차 read 가 owner 의 동시 write 와 레이스 → 교차 read 를 frozen 집합으로 제한하도록
RFC-0005 §6.3b SR3 교정(이것이 종이 증명의 목적 그대로다). 남은 갭은 G1 잔여(비-disjoint 공유·메모리 모델)·
G5(핸들 정적 불투명 표식)·G6(drop/affine×원장)으로, V3 기계 증명 단계에서 다룬다.
