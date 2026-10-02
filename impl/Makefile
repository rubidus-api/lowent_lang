# lowent front-end + lowent-mini interpreter — C23, built on vendored proven_c_lib.
# Shared front-end (lexer + point-closure CST + resolver) per BOOTSTRAP.md / DECISION-0005.
#
#   make            debug build (-g -O0)         -> build/lowentc
#   make asan       debug + ASan/UBSan
#   make run ARGS=… build then run
#   make test       build + run tests
#   make clean

CC      ?= gcc
CSTD    := -std=c23
WARN    := -Wall -Wextra -Wshadow
OPT     ?= -g -O0
PROVEN  := vendor/proven
INC     := -Isrc -I$(PROVEN)/include -I$(PROVEN)/platform
# ★ 지연 확장(=)이어야 한다 — `dist: OPT := -O2` 같은 **목표별 변수**가 CFLAGS 에 반영되려면.
#   `:=` 로는 파스 시점의 OPT(-O0)가 박제돼, dist 가 -O2 를 말하고 -O0 를 짓고 있었다.
# ★★★ **POSIX feature 매크로가 돌아왔다** (RFC-0090 N1, 2026-08-07). 아래 주석이 v26.07.13m
#   에 *"이제 불필요"* 라고 적었는데, **시계 리프가 그것을 되살린다**: `clock_gettime`·
#   `nanosleep`·`CLOCK_MONOTONIC` 은 `_POSIX_C_SOURCE >= 199309L` 아래에만 보인다.
#   ☞ *"이제 불필요"* 는 **그때의 참**이었고, 기능이 늘면 다시 필요해진다 — 사라진 이유를
#     적어 둔 덕에 되살릴 때 무엇을 되살리는지 알 수 있었다.
CFLAGS  = $(CSTD) $(WARN) $(OPT) $(INC) -D_POSIX_C_SOURCE=200809L -MMD -MP
BUILD   := build

# Minimal proven core we link against (arena/strings/containers + mem PAL).
PROVEN_SRC := \
  $(PROVEN)/src/proven/arena.c \
  $(PROVEN)/src/proven/memory.c \
  $(PROVEN)/src/proven/buffer.c \
  $(PROVEN)/src/proven/u8str.c \
  $(PROVEN)/src/proven/array.c \
  $(PROVEN)/src/proven/panic.c \
  $(PROVEN)/src/proven/heap.c \
  $(PROVEN)/src/proven/pool.c \
  $(PROVEN)/src/proven/scan.c \
  $(PROVEN)/src/proven/float_parse.c \
  $(PROVEN)/src/proven/float_decimal.c \
  $(PROVEN)/src/proven/utf.c \
  $(PROVEN)/src/proven/u16str.c \
  $(PROVEN)/src/proven/alloc_check.c \
  $(PROVEN)/platform/proven_sys_mem.c \
  $(PROVEN)/platform/proven_sys_time.c \
  $(PROVEN)/platform/proven_sys_random.c
# ★ 시계 리프 (RFC-0090 N1, 2026-08-07): `proven_sys_time.c` 는 **구현이 이미 있었고
#   빌드에만 안 들어가 있었다**. 헤더는 선언만 주므로 링크에서 터진다 — 그것이 이 한 줄이다.
# ★ v26.07.13m 마이그레이션: lowent 은 proven **map 을 안 쓴다**(자기 심볼표를 쓴다 — src 는
#   arena/array/heap 만 include 한다). 그래서 map.c 를 컴파일 대상에서 뺐다(전엔 죽은 코드로
#   들어와 있었다). 새 map 이 끌어오던 hash/random/thread(SipHash HashDoS 방어)도 함께 빠져
#   컴파일 집합이 **실제 쓰는 것만** 남는다 — POSIX feature 매크로도 이제 불필요.
# ★ v0.6.0 재벤더 뒤(2026-10-02): utf.c — 소스 검증·`u"…"` 리터럴의 UTF-8 해독을 proven 의 `proven_utf8_decode_next` 가 한다
#   (손으로 짠 해독기 둘을 걷어냈다). u16str.c 는 utf.c 가 부르는 덧붙이기 하나 때문에 따라온다. alloc_check.c —
#   `make alloccheck` 가 힙을 블록을 아는 할당기로 감싸 남의 블록 해제·두 번 해제를 그 자리에서 잡는다(보통 빌드는 감싸지 않는다).
# scan.c (+ float_parse.c/float_decimal.c) added: the `scan` builtin uses proven's
# cursor tokenizer to auto-type input fields (int/float/string).

LOW_SRC := $(filter-out src/main.c,$(wildcard src/*.c))
LOW_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(LOW_SRC))
PROVEN_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(PROVEN_SRC))

BIN  := $(BUILD)/lowentc
TESTBIN := $(BUILD)/lowtest

.PHONY: check all clean asan alloccheck run test
all: $(BIN)

$(BIN): $(PROVEN_OBJ) $(LOW_OBJ) $(BUILD)/src/main.o
	$(CC) $(CFLAGS) $^ -o $@ -lm
# ★ 링크 뒤에 **소스 지문**을 남긴다(WO-0161). 이것이 없으면 *"이 바이너리가 어느 소스에서
#   나왔나"* 를 물을 방법이 없고, 그러면 잰 수가 누구 것인지 모른다. 실패해도 빌드는 죽이지
#   않는다(`-`) — 지문이 없으면 `check-binfresh` 가 **없다고 말한다**.
	-@python3 ../scripts/check-binfresh.py --stamp

$(TESTBIN): $(PROVEN_OBJ) $(LOW_OBJ) $(BUILD)/tests/run_tests.o
	$(CC) $(CFLAGS) $^ -o $@ -lm

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ★★★★★ **asan 물건을 남기고 가지 않는다** (2026-09-11, WO-0197 을 짓다 물렸다).
#   `make asan` 뒤에 `make` 를 하면 .o 는 **새것으로 보이므로** 다시 안 짓고 링크만 한다:
#   운이 나쁘면 `__asan_init` 미정의로 안 서고, 운이 더 나쁘면 **서긴 서는데 섞인**
#   바이너리가 나온다. 그것은 돌면서 **출력을 4096 바이트 경계에서 잘랐고**, 나는 그것을
#   한참 「컴파일러가 UTF-8 을 깬다」 로 쫓았다.
#   ⇒ 끝나면 .o 를 지운다(바이너리는 남는다 — 그것으로 재려고 지은 것이니까).
#   ☞ *지어 둔 것을 다시 쓰려면 무엇으로 지었는지도 함께 기억해야 한다. 못 기억하면 버린다.*
asan: OPT := -g -O0 -fsanitize=address,undefined
asan: clean all
	@find $(BUILD) -name '*.o' -delete
	@echo "asan: 다음 \`make\` 가 섞이지 않도록 .o 를 지웠다(바이너리는 남았다)"

# make alloccheck — 힙을 proven 의 블록을 아는 할당기(`alloc_check.h`)로 감싼 처리기를 build-ac/ 에 따로 짓고, 시험 픽스처마다
#   `--check` 를 돌린다. 남의 블록 해제 · 두 번 해제 · 크기가 틀린 realloc 은 그 자리에서 멈춘다(보통 build/ 는 안 건드린다).
alloccheck:
	$(MAKE) BUILD=build-ac OPT='-g -O0 -DPROVEN_ALLOC_CHECK' build-ac/lowentc
	@n=0; for f in tests/*.low; do n=$$((n+1)); build-ac/lowentc --check "$$f" >/dev/null 2>&1; rc=$$?; \
	  if [ $$rc -ge 128 ]; then echo "alloccheck: $$f 에서 멈췄다(rc $$rc) — gdb --args build-ac/lowentc --check $$f"; exit 1; fi; done; \
	  echo "alloccheck: ok — 픽스처 $$n 개, 할당기 짝이 틀린 해제 0"

# release dist: **정적 링크** 실행파일 + 표준 라이브러리를 한 폴더에 (glibc 버전 비의존)
#   레이아웃 = 도구가 스스로 읽는 <install> 관례 그대로: bin/lowentc 가 ../std 를 본다.
#   ⇒ 폴더째 옮기면 어디서든 그대로 동작한다 (관리자 불요·설치 스크립트 불요).
DISTDIR := dist/lowent-linux-x64
dist: OPT := -O2 -DNDEBUG
dist: clean $(BIN)
	@rm -rf $(DISTDIR) && mkdir -p $(DISTDIR)/bin $(DISTDIR)/std $(DISTDIR)/keys/official
	$(CC) $(CFLAGS) -static $(PROVEN_OBJ) $(LOW_OBJ) $(BUILD)/src/main.o -o $(DISTDIR)/bin/lowentc -lm
	@strip $(DISTDIR)/bin/lowentc
	@cp ../lib/*.low $(DISTDIR)/std/
	@printf 'lowent — 정적 배포본 (linux x86_64)\n\nbin/lowentc      컴파일러+빌드도구 (정적 링크 — glibc 버전 무관)\nstd/             표준 모듈 (`use <이름> .` 이 여기서 풀린다; 이름 = 파일 안 module 선언)\nkeys/official/   신뢰 공개키 (.pub 를 넣으면 그 발행자의 서명을 믿는다)\n\n폴더째 아무 데나 두고 bin/lowentc 를 실행하면 된다 — 도구는 실행파일 기준 상대로 std 를 찾는다.\n사용자 데이터는 ~/.lowent/{lib,keys,cache,config} 에 산다. 자세한 것은 bin/lowentc 실행(도움말).\n' > $(DISTDIR)/README.md
	@echo "$(DISTDIR):  bin/lowentc ($$(du -h $(DISTDIR)/bin/lowentc | cut -f1), static) + std/$$(ls ../lib/*.low | wc -l) modules"

run: all
	./$(BIN) $(ARGS)

test: $(TESTBIN)
	./$(TESTBIN)

# 공개 저장소만으로 도는 짧은 회귀 시험: 단위 시험 + 표준 라이브러리 검사 + VM≡네이티브 + 거절 진단
check: all $(TESTBIN)
	@./$(TESTBIN) > $(BUILD)/unit.log 2>&1 && echo "unit: $$(tail -n 1 $(BUILD)/unit.log)" || { grep -v "\[PASS\]" $(BUILD)/unit.log; exit 1; }
	sh tests/smoke.sh

golden: all
	@if [ -f tests/golden.sh ]; then sh tests/golden.sh; else echo "golden: 골든 스위트는 개발 저장소에 있다 — 공개본은 \`make test\` 로 단위 시험을 돈다"; fi

clean:
	rm -rf $(BUILD)

# auto-generated header dependencies (-MMD) so header edits trigger recompiles
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
