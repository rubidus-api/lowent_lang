#define _POSIX_C_SOURCE 200809L   // open_memstream(3) — 프레임을 본문 보고 정하려고
// low_cbe.c — S5 C backend (see low_cbe.h).
#include "low_mem.h"   /* ★ W-20261112-0207 — proven 할당기 */
#include "low_fmt.h"   /* ★ W-20261112-0207 S2 — proven 포매터(low_bfmt) */
#include "low_cbe.h"
#include "low_cbe_prelude.h"
#define LW_EXPECT_MSG "E-TEST-FAIL: an `expect` is FALSE -- the test failed (this is not a contract violation)"   // ★ 2026-09-27 — 네이티브는 이것을 «requires violated at entry» 로 찍었다
#define LW_NODEKIDS_MSG "drop ... use: this node still holds a child — move its option (owned ...) fields out first"   /* VM 의 E-VM-NODE-KIDS */
#define LW_ENUMCAST_MSG "cast to an enum: the number is not the number of any variant of that enum"   /* VM 의 E-VM-CAST 와 같은 말 */
#define LW_SINKFULL_MSG "collect into / map / filter: the receiving slice is full and the source still has elements (panic). A full sink never drops the rest in silence (X-0062): give a longer buffer, or say how many with take N"   // ★ X-0065 — VM 의 E-VM-BOUNDS 와 같은 말
#include "low_sha256.h"
#include "low_sha512.h"   // ★ VM 과 **같은 매크로**를 방출한다 — 갈릴 자리가 없다
#include "low_blake3.h"
#include "low_version.h"   /* ★ 방출 헤더가 **자기가 어느 epoch 의 것인지** 적는다 */

#include <stdlib.h>
#include <string.h>
// ★★★ 결함 주입기 — **생성된 블롭**(원본 impl/src/low_hostfault.inc, RFC-0075 S5).
//   VM 과 **같은 바이트**를 찍어야 주입 아래서도 VM ≡ native 가 성립한다.
#include "low_hostfault_blob.h"

// The emitted runtime mirrors the VM's value model and rendering exactly.
// ★★★ **프리스탠딩 타깃은 호스트 헤더를 안 낸다** (2026-08-01 — 빌드 서버가 잡았다).
//
//   `--target cortex_m` 은 검사 단계에서는 floor 였다(alloc·float 을 막는다). 그런데 **방출은
//   호스트 그대로**였다: `pthread.h`·`dirent.h`·`sys/socket.h`·`netinet/in.h` 를 무조건 냈다.
//   그래서 arm-none-eabi-gcc 에서 **컴파일 자체가 안 됐다**(`<dirent.h> not supported`).
//   ⇒ *"floor 프로파일에서 초록"* 이 **검사기까지만** 참이었다. 실기 툴체인에 한 번 걸어 보는
//     기준(RFC-0088 §4.1 ②-d)이 없었으면 이 간극은 v1 뒤에도 남았을 것이다.
//
//   프리스탠딩에 남기는 것: 자립 헤더(stdint/stdalign/string/math) + newlib 가 주는 setjmp.
//   빼는 것: OS 가 있어야 뜻이 있는 전부(스레드·파일계·소켓·stdio/stdlib).

static void put_view(low_out_t *o, proven_u8str_view_t v) { low_write(o, v.ptr, v.size); }

// ★★★ **C 심볼 이름은 이름만으로는 유일하지 않다.**
//
//   자격 있는 op 이름(`rect.area`, RFC-0062)은 C 식별자가 될 수 없어서 점을 `_` 로 바꿔야
//   하는데 — 그러면 `rect.area` 와 `rect_area` 가 **둘 다 `rect_area`** 가 된다. **충돌한다.**
//   (사용자 지적. 그리고 옳다: **인코딩만으로는 당연히 충돌한다.**)
//
//   ⇒ 이름 뒤에 **그 이름의 해시**를 붙인다: `lw_op_rect_area__<h(“rect.area”)>`.
//     정제는 정보를 **잃지만** 해시는 안 잃는다 ⇒ **단사(injective)** 가 회복된다.
//
//   ★★ 처음엔 `iface_hash` 를 붙였다 — **틀렸다.** RFC-0012 는 이름을 **해싱하지 않는다**
//     (`name — surface name (metadata only — not hashed)`). 그래서 시그니처가 같은
//     `rect.area` 와 `rect_area` 가 **같은 해시**를 받아 C 가 **redefinition** 으로 거부했다.
//     ⇒ **내용 해시는 뜻의 것이고, 심볼 해시는 이름의 것이다. 둘은 다르다.**
// ★★★ **C 로 내보내는 이름은 C 가 부를 이름이다 — 그리고 그 이름은 저자의 것이다.**
//
//   `export` 는 C 가 우리를 부르는 자리이고, 그 이름은 헤더에 나가 **C 저자가 직접 타이핑**
//   한다. 즉 **다른 언어에 하는 약속**이다. 단계 U ③ 이 다른 모듈의 같은 이름을 허용하자
//   그 약속이 C 에서 부딪혔는데(C 이름공간은 평평하다), 한때 도구가 `<모듈>_<이름>` 으로
//   **지어냈다.** 그것이 틀렸다 — **도구는 공개 이름을 발명하지 않는다.**
//   ⇒ 저자가 `link "…" .` 로 정하고(RFC-0063, 이미 있던 절), 안 정했는데 겹치면 **거절**한다
//     (E-ABI-NAME-DUP, low_cbe 의 계획 단계). 여기서는 **정해진 것을 쓸 뿐**이다.
//   ☞ 내부 심볼(`put_sym`)은 반대다: C 저자가 볼 일이 없으니 도구가 지어도 된다.
// ★★★★ **RFC-0134 (2026-09-29, 소유자 결정) — 이제 이름 짓는 법이 정본의 규칙이다.** 위 판단의 흠은 도구가
//   **아무도 약속한 적 없는** 이름을 지어낸 것이었다. 규칙이 정본에 서면 그 이름이 곧 약속이다:
//     C 이름 = `lw` + 경로의 마디마다 `_<길이><마디>` — `lw_7listlit_6inside` · `lw_6shapes_4rect_4area`.
//   전엔 맨 이름을 그대로 내서 `inline`·`register`(C 예약어) · `printf`(방출 C 가 불러오는 헤더) · `lw_panic`
//   (런타임)이 네이티브 빌드를 깨뜨렸다(X-0078). 길이를 적으므로 밑줄이 어디 있어도 모호하지 않고, `lw_` 다음이
//   **숫자**라 런타임(`lw_` 다음이 글자)과 저절로 갈린다. 저자가 다른 이름을 원하면 `link "…" .`(그대로 쓴다).
static proven_size_t cbe_export_cname(const low_ir_def_t *d, char *buf, proven_size_t cap) {
    if (d->link_name.size) {
        proven_size_t n = d->link_name.size < cap - 1 ? d->link_name.size : cap - 1;
        memcpy(buf, d->link_name.ptr, n); buf[n] = 0; return n;
    }
    proven_size_t o = 0;
    #define CN_PUT(...) do { int w_ = low_bfmt(buf + o, o < cap ? cap - o : 0, __VA_ARGS__); if (w_ > 0) o += (proven_size_t)w_; } while (0)
    CN_PUT("lw");
    if (d->owner_mod.size) CN_PUT("_{}{}", PROVEN_ARG((size_t)d->owner_mod.size), low_sv((const char *)d->owner_mod.ptr, (int)d->owner_mod.size));
    proven_size_t s = 0;
    for (proven_size_t i = 0; i <= d->name.size; i++)
        if (i == d->name.size || d->name.ptr[i] == (proven_u8)'.') {   // `rect.area` — 그릇이 한 마디 더
            CN_PUT("_{}{}", PROVEN_ARG((size_t)(i - s)), low_sv((const char *)d->name.ptr + s, (int)(i - s)));
            s = i + 1;
        }
    #undef CN_PUT
    if (o >= cap) o = cap - 1;
    buf[o] = 0;
    return o;
}
static void put_export_name(low_out_t *o, const low_ir_def_t *d) {
    char nb[512];
    proven_size_t n = cbe_export_cname(d, nb, sizeof nb);
    low_write(o, nb, n);
}

// ★★★★★ **`#line` 을 낸다** (2026-09-10, REQ-0015 · WO-0196).
//   방출 C 에 자리표가 없어서 `gdb` 는 사람이 쓴 `.low` 가 아니라 **생성된 C** 를 보여 줬다.
//   이제 명령이 자기 줄을 들고 오므로(`low_ir_ins_t.line`), 자리가 바뀔 때마다 한 줄 찍는다.
//   ★ **실행 비용 0**: `#line` 은 디버그 정보만 바꾼다 — 명령 하나도 안 붙는다.
//   ★ 자리표가 없는 글루 코드는 **직전 자리에 붙는다**(다음 문장이 곧 고쳐 준다). 그것이
//     아무것도 안 말하는 것보다 낫다 — 완벽한 대응은 문장 단위 IR 이 아니라 표현식 단위
//     IR 을 요구하고, 그것은 이 항목의 일이 아니다.
//   ☞ *줄을 못 주면 이름이라도 준다. 그러나 줄을 줄 수 있으면 줄을 준다.*
static void cbe_line(low_out_t *o, const low_ir_t *ir, const low_ir_ins_t *in,
                     proven_u32 *last_ln, proven_u16 *last_fid) {
    if (!in->line || !in->fileid || (proven_size_t)in->fileid >= ir->nfiles) return;
    // ★★★ **바뀔 때만 찍으면 안 된다** (2026-09-10, 실측으로 배웠다).
    //   `#line N` 은 *다음* C 줄을 N 으로 놓을 뿐이고 그 뒤는 N+1, N+2 … 로 **흘러간다**.
    //   한 `.low` 문장이 C 여러 줄로 펴지므로, 바뀔 때만 찍으면 뒤쪽 줄이 **없는 줄 번호**를
    //   가리킨다(9 줄짜리 파일에서 `12` 를 봤다). ⇒ **명령마다** 못 박는다.
    //   지시문은 컴파일 시각에만 사는 것이라 실행 비용은 여전히 0 이다.
    (void)last_ln; (void)last_fid;
    const char *f = ir->files[in->fileid];
    if (!f || !*f) return;
    low_fpf(o, "#line {} \"", PROVEN_ARG((unsigned)((unsigned)in->line)));
    for (const char *s = f; *s; s++) {
        if (*s == '"' || *s == '\\') low_putc(o, '\\');
        low_putc(o, *s);
    }
    low_puts(o, "\"\n");
}
// ★ 몸이 끝나면 자리표를 **되돌린다**. `#line` 은 전처리기 상태라 함수 경계를 안 지킨다 —
//   되돌리지 않으면 그 뒤의 **글루 코드**(FFI 감싸개·디스패처·`main`)가 앞 함수의 `.low`
//   파일에 **없는 줄 번호**로 붙는다(실측: 9 줄짜리 파일의 `149` 줄). 생성물은 생성물이라고
//   말하는 편이 남의 파일을 가리키는 것보다 정직하다.
static void cbe_line_end(low_out_t *o) { low_puts(o, "#line 1 \"<lowent-generated>\"\n"); }
static void put_sym(low_out_t *o, const low_ir_def_t *d) {
    if (d->canon_of) d = (const low_ir_def_t *)d->canon_of;   // ★ 내용이 같은 def 은 **한 심볼**이다 (RFC-0012 dedup)
    for (proven_size_t i = 0; i < d->name.size; i++)
        // ★ `.`(이름공간) 과 `#`(단형화 인스턴스, RFC-0021) 둘 다 C 식별자에 못 온다.
        //   해시가 뒤에 붙으므로 정제로 잃은 단사성은 **회복된다**.
        low_putc(o, (d->name.ptr[i] == (proven_u8)'.' || d->name.ptr[i] == (proven_u8)'#' || d->name.ptr[i] == (proven_u8)':')
              ? '_' : (int)d->name.ptr[i]);   // ★ `:` — 로컬 op 의 `<바깥>::<로컬>`(RFC-0121)
    // ★★★ **액터 핸들러의 이름은 이름만으로 유일하지 않다.**
    //   IR 은 이미 그걸 안다 — `send` 는 핸들러를 **수신자의 타입 안에서** 찾는다(그게 아니면
    //   두 액터가 같은 이름의 핸들러를 조용히 공유한다). 그런데 **뒤끝은 그걸 몰랐다**:
    //   두 액터의 `init` 이 **한 C 심볼**이 되어 네이티브 빌드가 `redefinition` 으로 죽었다.
    //   앞이 고쳐진 병이 뒤에 남아 있었다 — **같은 뜻이 두 곳에서 읽히면 두 곳이 갈린다**(교훈 7).
    //   ⇒ 심볼 해시는 **이름 + 소속 액터**의 것이다.
    proven_u8 h[32];
    proven_u8 key[288];
    proven_size_t kn = 0;
    for (proven_size_t i = 0; i < d->name.size && kn < 256; i++) key[kn++] = (proven_u8)d->name.ptr[i];
    if (d->is_actor && d->nparams > 0) {
        key[kn++] = (proven_u8)'@';
        key[kn++] = (proven_u8)('0' + (d->param_sidx[0] % 10));
        key[kn++] = (proven_u8)('0' + ((d->param_sidx[0] / 10) % 10));
    }
    // ★★★ **그리고 이제 이름은 모듈 안에서만 유일하다** (RFC-0060 재개 · 단계 U ③, 2026-08-06).
    //   다른 모듈끼리 같은 이름을 허용했으므로, `qual_a.pick` 과 `qual_b.pick` 은 **다른 op** 이다.
    //   그런데 심볼 키가 이름(+액터)뿐이라 **한 C 심볼**이 되어 네이티브가 redefinition 으로 죽었다.
    //   ⇒ 위 액터 때와 **같은 병의 세 번째 얼굴**이다(앞: 자격 이름 정제 · 액터 핸들러):
    //     **앞이 가르는 것을 뒤끝도 갈라야 한다.** 골든의 차등 훑기가 그 자리를 잡았다.
    if (d->owner_mod.size) {
        key[kn++] = (proven_u8)'/';
        for (proven_size_t i = 0; i < d->owner_mod.size && kn < 280; i++)
            key[kn++] = (proven_u8)d->owner_mod.ptr[i];
    }
    low_blake3_256(key, kn, h);
    low_puts(o, "__");
    for (int i = 0; i < 8; i++) low_fpf(o, "{:02x}", PROVEN_ARG((unsigned)(h[i])));
}

// every IR word is emittable now; refs use release semantics (real C pointers,
// no frame-generation verifier — the VM owns dynamic verification)
// ★ C 에서 `-9223372036854775808ll` 은 **부호 없는 상수의 부정**이다(경고).
//   생성된 코드도 **경고 0** 이어야 한다 — 이 프로젝트가 자기 코드에 요구하는 것과 같다.
//   (세 번째 진짜 프로그램을 컴파일하다 드러났다: `--emit-c` 가 낸 C 가 경고를 여섯 개 냈다.)
static void put_i64(low_out_t *out, proven_i64 v) {
    if (v == INT64_MIN) low_puts(out, "(-9223372036854775807ll - 1)");
    else low_fpf(out, "{}ll", PROVEN_ARG((long long)((long long)v)));
}
// ★★★★★ **이 뒤끝이 아직 못 내는 옵코드** (2026-08-07).
//
//   여기 없으면 태그 경로의 `switch` 가 그 명령을 **조용히 지나친다** — `default:` 가 없다.
//   그러면 스택이 어긋난 채로 다음 명령이 돌고, 프로그램은 **틀린 답을 내거나 패닉한다.**
//   실측(`tests/vm_tty.low`): VM 은 셋 다 0 인데 네이티브는 `tty_raw` 가 **1**(터미널이
//   아닌데 raw 진입 성공이라고 말한다)이고 `tty_size`·`tty_read` 는
//   *"panic: is_some needs an option"* 으로 죽었다 — 옵션을 낼 자리에 아무것도 안 밀었으니까.
//
//   ☞ 그리고 **컴파일러가 처음부터 말하고 있었다**: 이 파일을 빌드하면 gcc 가
//     `enumeration value 'IRW_TTYSIZE' not handled in switch [-Wswitch]` 를 낸다.
//     경고가 옳았고 아무도 안 읽었다. **읽히지 않는 경고는 없는 경고다.**
//
//   ⇒ 담고 있으면 그 op 을 **방출하지 않고 W-CBE-SKIP 으로 말한다**. 없는 것을 있는 척하지
//     않는 것이 이 저장소의 규율이고(PRINCIPLES.md §0 교훈 2), 그래야 차등 검증도 **못 본다는
//     사실을 본다**. 구현하는 것은 별개의 일이고, 그때 이 목록에서 지운다.
//   ★ 목록은 **실측이 있는 것만** 담는다. `IRW_RMARK`·`IRW_RRESET` 도 이 파일에 case 가
//     없지만(같은 -Wswitch 경고가 난다) **네이티브 region 은 실제로 옳은 답을 낸다**
//     (`vm_region.low`: reuse = 99000 · many 통과). 그래서 넣지 않았다 — 넣었더니 통과하던
//     비교가 *"네이티브에 없음"* 으로 바뀌었다. **경고가 곧 결함은 아니고, 결함인지는 돌려
//     봐야 안다.** 그 둘은 남은 관찰로 적어 둔다(vm_region.low 주석).
static bool cbe_op_unsupported(low_irw_t w) {
    (void)w;
    return false;   // ★ 지금은 없다 — tty 셋은 2026-08-07 에 구현했다(위 lw_tty_* 참조)
}
static bool cbe_emittable(const low_ir_def_t *d) {
    // ★★★ **크기는 방출 여부의 이유가 아니다** (X-0028 · WO-0205). 전에는 명령 4096 이 넘으면
    //   그 op 은 네이티브 빌드에서 **통째로 빠졌다** — VM 에서만 돌고, 두 백엔드의 대조(오라클)가
    //   그 자리를 아예 못 봤다. 그 수 역시 배열 크기였을 뿐이다(이제 명령 수만큼 잡는다).
    if (!d->lowered) return false;
    for (proven_size_t j = 0; j < d->ncode; j++)
        if (cbe_op_unsupported(d->code[j].w)) return false;
    return true;
}
// ★★★ **본문은 한 번만 찍는다** (RFC-0012 dedup) — 이름만 정본으로 바꾸면 같은 이름의 본문이 둘이 되어
//   C 가 redefinition 으로 죽는다. ★ 그러나 **디스패치 표에는 모든 op 이름이 남아야** 한다: 이름이 빠지면
//   네이티브가 그 op 을 못 부르고 **차등 검사가 그곳을 못 본다**(커버리지 게이트가 바로 그것을 잡았다).
//   표는 cbe_emittable 로, 본문 방출만 이 술어로 거른다 — 중복 이름은 정본 함수를 가리킨다.
static bool cbe_emit_body(const low_ir_def_t *d) {
    return cbe_emittable(d) && !d->canon_of;
}


// ★ 레지스터 **클래스**(`reg`)와 **특정 레지스터**(`rax`…)를 GCC 제약으로 옮긴다.
//   모르는 레지스터는 **거절한다** — 조용히 `r` 로 떨어뜨리면 syscall 이 **틀린 레지스터**로 간다.
static const char *cbe_asm_constraint(proven_u8str_view_t r) {
    struct { const char *n, *c; } M[] = {
        { "reg", "r" }, { "rax", "a" }, { "rbx", "b" }, { "rcx", "c" },
        { "rdx", "d" }, { "rsi", "S" }, { "rdi", "D" },
    };
    for (proven_size_t i = 0; i < sizeof M / sizeof M[0]; i++)
        if (proven_u8str_view_eq(r, proven_u8str_view_from_cstr(M[i].n))) return M[i].c;
    return NULL;
}

// ★★★ **빌드 타깃과 대조한다** (RFC-0041, 2026-08-02 수리). 여기는 `"x86_64"` 를 **하드코딩**
//   하고 있었다 — 그래서 `--target cortex_m` 에서 옳은 asm 은 거절되고 x86 asm 은 통과했다.
//   앞단(low_ir)이 이미 같은 판정을 하므로 여기는 **백스톱**이다(두 곳이 같은 권위를 본다).
static bool cbe_asm_target_ok(proven_u8str_view_t t) {
    return proven_u8str_view_eq(t, proven_u8str_view_from_cstr(low_ir_target()->name));
}

// 템플릿의 `{name}` → `%N`. GCC 는 **출력을 먼저** 센다.
static void cbe_asm_tmpl(low_out_t *out, const low_ir_asm_t *A) {
    low_putc(out, '"');
    for (proven_size_t i = 0; i < A->tmpl.size; i++) {
        proven_byte_t ch = A->tmpl.ptr[i];
        if (ch == (proven_byte_t)'{' && i + 1 < A->tmpl.size &&
            A->tmpl.ptr[i + 1] == (proven_byte_t)'{') { low_putc(out, '{'); i++; continue; }
        if (ch == (proven_byte_t)'{') {
            proven_size_t j = i + 1;
            while (j < A->tmpl.size && A->tmpl.ptr[j] != (proven_byte_t)'}') j++;
            proven_u8str_view_t nm = { A->tmpl.ptr + i + 1, j - i - 1 };
            proven_size_t n = 0;
            for (proven_size_t k = 0; k < A->nops; k++) if (A->ops[k].is_out) {
                if (proven_u8str_view_eq(nm, A->ops[k].name)) goto found;
                n++;
            }
            for (proven_size_t k = 0; k < A->nops; k++) if (!A->ops[k].is_out) {
                if (proven_u8str_view_eq(nm, A->ops[k].name)) goto found;
                n++;
            }
        found:
            low_fpf(out, "%{}", PROVEN_ARG((size_t)((size_t)n)));
            i = j;
            continue;
        }
        // ★ Lowent 템플릿의 자리표시자는 **`{name}` 하나뿐**이다. 그러니 `%` 는 **글자 그대로**여야
        //   한다(`%cl` 같은 레지스터 이름). GCC 확장 asm 에서 그것은 `%%` 다 — **표기의 층은
        //   우리 것이고, 뒤끝의 철자는 뒤끝의 것이다.** 저자가 두 방언을 다 알 필요는 없다.
        if (ch == (proven_byte_t)'%')  { low_puts(out, "%%"); continue; }
        if (ch == (proven_byte_t)'\n') { low_puts(out, "\\n\\t"); continue; }
        if (ch == (proven_byte_t)'"')  { low_puts(out, "\\\""); continue; }
        if (ch == (proven_byte_t)'\\') { low_puts(out, "\\\\"); continue; }
        low_putc(out, (int)ch);
    }
    low_putc(out, '"');
}

static void cbe_asm_stmt(low_out_t *out, const low_ir_asm_t *A) {
    low_puts(out, "    {\n");
    for (proven_size_t k = 0; k < A->nops; k++) {
        if (A->ops[k].is_out) low_fpf(out, "        unsigned long long a{} = 0;\n", PROVEN_ARG((size_t)((size_t)k)));
        else low_fpf(out, "        unsigned long long a{} = (unsigned long long)loc[{}].i;\n",
                     PROVEN_ARG((size_t)((size_t)k)), PROVEN_ARG((int)((int)A->ops[k].slot)));
    }
    low_puts(out, "        __asm__ __volatile__ (\n            ");
    cbe_asm_tmpl(out, A);
    low_puts(out, "\n            : ");
    { proven_size_t n = 0;
      for (proven_size_t k = 0; k < A->nops; k++) if (A->ops[k].is_out) {
          const char *cs = cbe_asm_constraint(A->ops[k].reg);
          // ★★★ **earlyclobber(`&`)** — 컴파일러는 템플릿을 **안 읽으므로**(low_ir.c: "asm 안을 못
          //   읽는다") 출력이 입력을 **다 읽기 전에** 쓰이는지 알 수 없다. `&` 없이 `"=r"` 이면 GCC 가
          //   출력 레지스터를 아직 살아있는 입력 레지스터에 겹쳐 할당할 수 있고, `mov {b},{r}` 같은
          //   write-before-read 템플릿이 그 입력을 뭉갠다(실측: `mov {b},{r}; add {a},{r}` 가 a+b 대신
          //   b+b). 출력을 항상 earlyclobber 로 내는 것이 보수적이고 **언제나 옳다**.
          low_fpf(out, "{}\"=&{}\"(a{})", proven_arg_cstr(n++ ? ", " : ""), proven_arg_cstr(cs ? cs : "r"), PROVEN_ARG((size_t)((size_t)k)));
      } }
    low_puts(out, "\n            : ");
    { proven_size_t n = 0;
      for (proven_size_t k = 0; k < A->nops; k++) if (!A->ops[k].is_out) {
          const char *cs = cbe_asm_constraint(A->ops[k].reg);
          low_fpf(out, "{}\"{}\"(a{})", proven_arg_cstr(n++ ? ", " : ""), proven_arg_cstr(cs ? cs : "r"), PROVEN_ARG((size_t)((size_t)k)));
      } }
    // ★ clobber 는 **프로그래머가 컴파일러에게 주는 약속**이다. 컴파일러는 그것을 **믿는다**
    //   (asm 안을 못 읽으니까). 틀리면 UB — asm 은 **검증 불가한 탈출구**다(RFC-0041 D7, 정직).
    low_puts(out, "\n            : ");
    { proven_size_t n = 0;
      for (proven_size_t k = 0; k < A->nclob; k++) {
          if (proven_u8str_view_eq(A->clob[k], proven_u8str_view_from_cstr("flags")))
              low_fpf(out, "{}\"cc\"", proven_arg_cstr(n++ ? ", " : ""));
          else low_fpf(out, "{}\"{}\"", proven_arg_cstr(n++ ? ", " : ""),
                       low_sv((const char *)A->clob[k].ptr, (int)A->clob[k].size));
      } }
    low_puts(out, ");\n");
    { bool has_out = false;
      for (proven_size_t k = 0; k < A->nops; k++) if (A->ops[k].is_out) {
          low_fpf(out, "        st[sp++] = lw_int((long long)a{});\n", PROVEN_ARG((size_t)((size_t)k)));
          has_out = true; break;
      }
      if (!has_out) low_puts(out, "        st[sp++] = lw_int(0);\n"); }
    low_puts(out, "    }\n");
}


// ★★ **이 op 은 트랩할 수 있는가.**
//   `pure` 는 *"반환값 말고는 아무 일도 안 한다"* 는 주장이다. 그런데 계약 위반은 **중단**이고,
//   중단은 **관찰 가능한 일**이다. 결과를 안 쓰는 호출을 컴파일러가 지우면 — **일어났어야 할
//   패닉이 안 일어난다.** 그러므로 **트랩할 수 있는 op 에는 절대 못 붙인다.**
//   (증명된 검사는 애초에 방출되지 않으므로 그 자리는 트랩이 아니다 — IR_POL_PROVEN.)
static bool cbe_word_safe(const low_ir_ins_t *in) {
    switch (in->w) {
        case IRW_CONST: case IRW_FCONST: case IRW_LOAD: case IRW_STORE:
        case IRW_RET:   case IRW_BR:     case IRW_BRZ:
        case IRW_EQ: case IRW_NE: case IRW_LT: case IRW_LE: case IRW_GT: case IRW_GE:
        case IRW_AND: case IRW_OR: case IRW_NOT: case IRW_SELECT:
        case IRW_LEN: case IRW_COUNT:
        case IRW_ISSOME: case IRW_ISOK: case IRW_ISERR:
        case IRW_WRAP_SOME: case IRW_WRAP_NONE:
            return true;
        // 산술은 **검사가 증명으로 지워졌을 때만** 안전하다. 그것이 계약이 사 주는 것이다.
        case IRW_ADD: case IRW_SUB: case IRW_MUL: case IRW_NEG:
        case IRW_INDEX:
            return (in->a & IR_POL_PROVEN) != 0;
        default:
            return false;   // ★ 모르면 **트랩할 수 있다**고 본다. 과대근사는 안전하다.
    }
}

// 호출을 타고 전파한다 — 트랩하는 op 을 부르는 op 도 트랩한다(고정점).
static void cbe_mark_pure(const low_ir_t *ir, bool *pure) {
    for (proven_size_t i = 0; i < ir->ndefs; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        pure[i] = d->lowered && d->is_calc && !d->is_test && !d->is_actor && !d->is_extern;
        if (!pure[i]) continue;
        for (proven_size_t k = 0; k < d->ncode; k++)
            if (d->code[k].w != IRW_CALL && !cbe_word_safe(&d->code[k])) { pure[i] = false; break; }
    }
    for (bool changed = true; changed; ) {          // 고정점
        changed = false;
        for (proven_size_t i = 0; i < ir->ndefs; i++) {
            if (!pure[i]) continue;
            const low_ir_def_t *d = &ir->defs[i];
            for (proven_size_t k = 0; k < d->ncode; k++) {
                if (d->code[k].w != IRW_CALL) continue;
                // ★ 호출 낱말의 피연산자에는 **플래그가 섞여 있다**(IR_POL_PROVEN 등).
                //   그대로 인덱스로 쓰면 **엉뚱한 def** 를 가리킨다. 마스크가 답이다.
                proven_size_t c = IR_CALL_IDX(d->code[k].a);
                if (c >= ir->ndefs || !pure[c]) { pure[i] = false; changed = true; break; }
            }
        }
    }
}


// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **우리는 타입을 다 아는데, 백엔드에서 그것을 버리고 있었다.**
//
//   `fn clamp255 input a u64 . output u64 .` 이 이렇게 방출됐다:
//     · 값 하나가 **48바이트 태그 구조체**(`lowv`: tag·i·p·n·box·q)
//     · 함수마다 **`lowv st[256]`** = **12KB 스택 프레임** + `memset`
//     · **런타임 태그 검사**(`lw_want_int`) — 타입체커가 **이미 정적으로 증명한 것**을 다시
//
//   자연스러운 C 는 `uint64_t clamp255(uint64_t)` 다. 그리고 이 차이는 **LLVM 으로 가도
//   안 고쳐진다** — IR 이 동적이면 백엔드가 무엇이든 마찬가지다. 병은 **표현**에 있다.
//
//   ⇒ **스칼라 전용 op** — 파라미터·중간값·결과가 전부 정수인 op — 은 `long long` 으로
//     내린다. 태그도, 박스도, 12KB 프레임도 없다. `lowv` 판본은 **얇은 어댑터**로 남는다
//     (디스패치 표·다른 호출자가 그대로 산다).
//
//   ★ 안전은 어디서 오나: **타입체커가 강제한 것**만 믿는다. 그리고 **차등 스윕**이
//     VM 과 네이티브의 답을 250 op × 2400 벡터로 대조한다. 틀리면 **즉시 고발된다.**
// ★ **대조 스위치** — `--no-fast` 는 타입 하강을 끈다(`--flat` 이 나무를 끄는 것과 같은 규율).
static bool g_no_fast;
void low_cbe_set_no_fast(bool v) { g_no_fast = v; }
// ★★★ **RFC-0109 단계 1 — 빠른 경로 안의 슬라이스는 원소 단위다** (WO-0178, 2026-09-03).
//   전에는 파라미터 슬라이스를 **바이트**로 받고 본문 첫머리의 `view.array`(IR 이 진입에서 씌운다)가
//   호출마다 `n % w` 를 검사하고 `/= w` 로 나눴으며, 호출하는 쪽은 `*= w` 로 되돌렸다 — 슬라이스 셋이면
//   호출마다 아홉 연산이다(WO-0177 실측: 3.7× 가운데 줄 단위로 평평하게 깔린 부분).
//   ⇒ 파라미터 슬롯이 **처음부터 원소 폭**(lez = w)이고, 같은 폭으로 보는 `view.array` 는 **항등**이다
//     (방출 0). 바이트→원소 검사는 **바이트가 원소가 되는 자리**에서만 난다: 태그 어댑터(LWV_SLICE 가
//     들어올 때) · 빠른 호출 자리(슬롯이 아직 바이트일 때) · 사용자가 쓴 `view_array`. 검사는 옮겨졌지
//     없어지지 않았다(RFC-0109 I2). IR 은 그대로다(I1 — 해시 불변).
//   `--no-elemsl` 은 대조 스위치다(`--no-fast` 와 같은 규율): 끄면 옛 규약 그대로 낸다.
static bool g_no_elemsl;
void low_cbe_set_no_elemsl(bool v) { g_no_elemsl = v; }
// ★★ `slice i64`(폭 표시 164 = 100 + 비트)도 8 바이트다 (2026-10-03). 8 바이트를 읽어 `long long` 으로 보면 값이 그대로이므로
//   부호 확장이 필요 없다. 전에는 `slice i64` 를 받는 op 이 통째로 느린 길로 떨어졌다: lowstat 의 그룹 합(`slice i64`)이 그래서
//   파이썬보다 5배 느렸다.
// ★★ i8·i16·i32(108·116·132)도 받는다 (2026-10-04). 읽을 때 부호를 늘려야 하므로 슬롯의 `fl` 이 **2**(부호 있는 정수 원소)를
//   싣는다 — `view.array` 의 부호 비트(IR_SGN_BIT)나 슬라이스 칸의 `slmeta` 가 그것을 단다. `fl` 이 1 이면 부동 원소다.
//   부동 표식이 다니는 길(지역 저장·부분 슬라이스·호출 인자)을 그대로 타므로 따로 나를 것이 없다.
static int cbe_ebits_w(proven_u8 eb) {
    return (eb == 8 || eb == 108) ? 1 : (eb == 16 || eb == 116) ? 2 : (eb == 32 || eb == 132) ? 4 : (eb == 64 || eb == 164) ? 8 : 0;
}
// ★ 2026-11-01 (W-20261101-1150) — 슬라이스 **매개변수**의 원소 폭(정수 · 부동 둘 다). 부동 원소는 ebits 가 255 라 폭을 param_esz 에서 읽는다.
//   종류 실행 · 방출 · 경계 어댑터 · 부르는 자리가 모두 이 하나를 본다(두 반쪽이 다른 폭을 보면 곧 갈린다).
static int cbe_pslice_w(const low_ir_def_t *d, proven_size_t q) {
    if ((d->param_sflt >> q) & 1u) return (d->param_esz[q] == 4 || d->param_esz[q] == 8) ? (int)d->param_esz[q] : 0;
    return cbe_ebits_w(d->param_ebits[q]);
}
// ★ 슬롯의 `fl` 값 — 0 = 무부호 정수 원소 · 1 = 부동 원소 · 2 = 부호 있는 정수 원소(8 바이트 미만, 읽을 때 부호를 늘린다).
#define CBE_FL_FLT 1
#define CBE_FL_SGN 2
// ★ 슬라이스 칸에서 읽은 슬라이스의 `fl` (2026-10-04). 전에는 늘 0 이었다 — 그래서 구조체 칸의 `slice i32` 를 빠른 길에서 읽으면
//   -1 이 4294967295 로, `slice f64` 는 1.5 가 비트 그대로의 정수로 나왔다(VM 은 옳았다 — 오라클이 갈리는 자리였다).
static unsigned char cbe_sf_fl(const low_ir_sfield_t *f) {
    if (f->slmeta & IR_FLT_BIT) return CBE_FL_FLT;
    if ((f->slmeta & IR_SGN_BIT) && f->elem < 8) return CBE_FL_SGN;
    return 0;
}
// ★ `--no-main` — **라이브러리로 낸다**(RFC-0063 D3). C 프로그램이 `main` 을 갖는다.
static bool g_no_main;
void low_cbe_set_no_main(bool v) { g_no_main = v; }

// ── **슬롯의 종류를 IR 위에서 추론한다** ────────────────────────────────────────
//   IR 은 스택 머신이고 낱말마다 arity 가 정해져 있다 ⇒ **정적으로 시뮬레이션**하면
//   각 슬롯이 정수인지 바이트 슬라이스인지 알 수 있다. 모르면 **빠른 경로를 안 쓴다.**
//   (★ 조용히 틀린 표현으로 내리느니 **안 내리는 게 낫다** — 나무를 세울 때와 같은 규율.)
static bool cbe_ret_flt(const low_ir_def_t *d);   // ★ extern 의 출력이 f64/f32 인가 (앞선 사용)
#define K_INT 0
#define K_SL  1
#define K_BAD 2
#define K_OPT 3          /* option·result — (태그, 값) 쌍 */
#define K_FLT 4          /* f64 — double. 태그도 박스도 없다. */
// ★★★★ **바이트 슬라이스를 감싼 option** (RFC-0112 §8-11 · WO-0222). `alloc_bytes` 의 답과 얼로케이터 `reserve` 의
//   반환이 이 모양이다. 태그 경로에서는 `some` 이 **박스 풀 칸**을 먹고, 그 칸 때문에 루프 뒷분기마다 풀 GC 가 돌았다
//   (실측: 할당 한 번 ≈37–247 ns 중 GC 가 절반). 값은 `lw_r` 의 `.s` 칸 하나에 담긴다 — `try_view` 가 이미 쓰는 칸이다.
//   ★ WO-0158 이 닫은 관문(슬라이스 칸을 든 **레코드**를 감싼 option)과는 다른 길이다: 여기엔 평행 칸이 없다.
//   ★ 이 종류는 option **파라미터·인자로 건너가지 않는다**(K_OPT 와 다른 종류라 인자 대조가 거절한다) — 받는 쪽이
//     안쪽 종류를 모르기 때문이다. 반환과 지역만 나른다.
#define K_OPTSL 10
// ★★ **줄의 배열** (2026-10-31 · W-20261031-1421 · 소유자 «다른 것들 다 진행») — `slice (array M T)`: 바이트 위의 줄 보기.
//   ss[] 에 (포인터, **줄 수**)로 살고, 슬롯의 ve = 줄 안 원소 폭 · vn = 줄 하나의 바이트(≤ 255) · fl = 원소 표식이다. `index` 는 그 줄을 보는
//   보통 슬라이스(K_SL)를 낸다. ★ 2026-11-02 (W-20261102-0024 O4) — 세 겹 이상: 줄이 다시 줄의 나열이면(IR 의 줄 꼴 표 `rowsh_*`) 그 표의 칸을
//   fl 의 위 비트에 싣는다(`CBE_FL_RSUB` · 칸 ≤ 63). `index` 는 그 칸의 아래 줄의 나열(K_ROWS)을 낸다. **종류를 따로 둔 까닭**: 슬라이스(K_SL)에 표식만
//   달면 그 표식을 모르는 자리(색인 · 쓰기 · 해시 · 부름 인자)가 줄을 바이트로 읽는다. 따로 두면 모르는 자리는 스스로 거절한다.
#define K_ROWS 11
#define CBE_FL_MARK 3                               // fl 의 아래 두 비트 = 원소 표식(CBE_FL_FLT · CBE_FL_SGN)
#define CBE_FL_RSUB(f) ((unsigned)(f) >> 2)          // K_ROWS 의 fl 위 비트 = 아래 줄 꼴의 칸(0 = 줄 안이 수)
static unsigned cbe_arr_rb(const low_ir_sfield_t *f);
static unsigned cbe_arr_len(const low_ir_sfield_t *f);
/* ★ 2026-10-31 (W-20261031-2053) — **option · result 안의 option · result**(`result (option usize) e` — 파일 읽기의 모양).
   (바깥 태그, 안 태그 `t2`, 수) 셋이다. 안이 수를 싼 것일 때만(ve 0 · fl 0). 둘이 같은 칸 `lw_r` 에 산다. */
#define K_OPT2 12
/* ★★ 2026-11-01 (W-20261101-1150) — **액터를 가리키는 값**(컨테이너가 든 할당기 칸을 읽은 것). 슬라이스 스택 `ss[]` · 지역 `sloc[]` 에
   lw_sl 하나로 산다(lw_aref_in — 태그 쪽 레코드의 자리). ve = 그 액터 상태 구조체의 sidx + 1. */
#define K_AREF 13
// ★★ W-20261105-2304 (소유자 «128비트도 하기») — **128 비트 정수도 빠른 경로의 값이다.** 칸은 `lw_u128` 하나(`lw_hs` 스택 · `lw_hloc` 지역).
//   부름을 건널 때는 정수 칸 둘(아래 64 · 위 64 비트 — `ai` / `ci`)로, 돌려줄 때는 `lw_r` 의 바이트 칸(`r_.r`, tag 11)으로 간다.
//   셈의 규칙은 프렐류드의 한 몸(`lw_wc_*`)이다 — 태그 경로의 `lw_w_*` 도 그것을 부른다.
#define K_W128 14
// ★★★ **비트셋은 64비트 마스크다 — 박스가 아니다.**
//   태그 경로는 그것을 **8칸짜리 풀**에 넣는다 ⇒ **루프에서 아홉 번째를 만들면 죽는다**
//   (레코드가 64칸에서 죽던 것과 **같은 병**). 마스크는 그냥 `long long` 이다.
//   ★ 그리고 `add b k` 는 **b 를 바꾼다**(비트를 세우고 0 을 돌려준다) — 참조 의미다.
//     그래서 **출신 지역**을 알아야 한다. 모르면 안 내린다(레코드와 같은 규율).
#define K_BSET 5
// ★★★ **스택 컨테이너도 값이다** — 배열 하나와 길이 하나다.
//   태그 경로는 그것을 **8칸짜리 풀**에 넣는다(용량 128). 레코드·비트셋과 **같은 병**이다.
//   `push`/`pop` 은 **박스를 바꾼다** ⇒ **집(홈 지역)** 을 알아야 한다. 모르면 안 내린다.
#define K_STK 6
// ★★★ **참조는 프레임의 지역을 가리킨다** — 그리고 그 지역이 **누구인지 컴파일 시점에 안다.**
//   그러면 포인터가 **필요 없다**: `deref r` 은 그냥 `loc[대상]` 이다.
//   (프레임 밖으로 나가면 이야기가 다르다 — 호출로 넘기면 **안 내린다.**)
#define K_REF 7
// ★★★ **벡터는 레인의 배열이다** — 그리고 그것이 **여섯 번째 유한 풀**이었다(lw_vecs[32]).
//   마스크는 **비트마스크**다. 레인 수·원소 폭은 **컴파일 시점에 안다**(meta 가 싣는다).
//   ★ 부동 레인은 아직 안 내린다 — 모르면 안 내린다.
#define K_VEC 8
// ★★★ **구조체 슬라이스** (`slice pt`) — 바이트 위의 **구조체 배열 뷰**. 시스템 코드의 기본 모양.
//   sidx 를 **종류에 담는다**(128 + sidx) — 스트라이드는 레이아웃(`total`)에서 계산한다.
//   ss[] 에 (포인터, **원소 수**) 로 산다. `index` 는 `total` 스트라이드로 **구조체 뷰**를 낸다.
#define K_SVIEW 128
#define K_MASK 9
#define K_STK_CAP 128
// ★★★ **구조체는 값이다 — 박스가 아니다.**
//   태그 경로에서 `make` 는 **64개짜리 고정 박스 풀**에서 할당하고(루프에서 구조체를 만들면
//   **고갈된다**), `field` 는 **런타임 `strcmp` 루프**로 필드를 찾는다. 필드 접근이 **문자열
//   비교**다. 우리는 그 이름을 **컴파일 시점에 안다.**
//   ⇒ 레코드 값 = **필드가 곧 슬롯**인 작은 배열. 이름은 방출 때 **슬롯 번호로 사라진다.**
#define K_REC 16          /* K_REC  + <구조체 인덱스> — `make` 로 만든 **값** */
#define K_VIEW 64         /* K_VIEW + <구조체 인덱스> — 바이트 위의 **무복사 뷰** */
/* ★★★★ **상한은 이제 "칸에 들어가는가" 다** (RFC-0039 §9-5, 2026-08-03).
 *   그전에는 레코드가 `long long [8]` 슬롯이라 **필드 8개**가 상한이었다 — 그런데 IR 은 16 을
 *   허용했고(`IR_MAKE_MAXF`), 이 매크로의 옛 주석은 **둘이 같다고 적어 두었다**(안 같았다).
 *   바이트로 합친 지금은 슬롯이 없다: 상한은 **바이트 폭**(칸 64)과 IR 의 필드 수뿐이다.
 *   ☞ 뷰에는 처음부터 이 상한이 없었다(오프셋으로 읽으니까) — 레코드가 그 자유를 얻었다. */
#define K_REC_MAX  IR_MAKE_MAXF
#define K_REC_BYTES 64

// ★ 슬롯의 **출신**도 따라간다: 이 레코드가 어느 **지역**에서 실려 왔는가.
//   태그 경로의 레코드는 **박스(참조)** 다 — `set (field q x) v .` 가 지역 `q` 에 **보인다**.
//   값 복사본에 쓰면 그 쓰기가 **사라진다**(그리고 차등 스윕이 그것을 즉시 고발했다).
//   ⇒ 출신 지역을 알면 **거기에 직접 쓴다**. 모르면 **안 내린다.**
// ★★★ **원소 폭은 슬롯의 성질이지 def 의 성질이 아니었다.**
//   `view_array u32 b` 는 **바이트 슬라이스를 u32 배열로** 본다 — 한 op 안에서 폭이 **둘**이다.
//   def 하나에 폭 하나를 강요했더니 그런 op 이 **통째로** 느린 경로에 남았다(SIMD 가 다 그렇다).
//   ⇒ `ve[]` 가 **슬롯의 원소 폭**을 든다(벡터의 레인 폭도 같은 자리다).
typedef struct { unsigned char k[64]; signed char o[64];
                 unsigned char vn[64], ve[64];   // 벡터: 레인 수 · 원소 폭 / 슬라이스: 원소 폭
                 unsigned char fl[64];           // ★ 부동 원소/레인 표식(슬라이스·벡터가 f32/f64 인가)
                 int n; bool set; } kstack_t;


// ★★★ **빠른 경로의 «크기» 한도는 판단이 아니라 이 배열들의 크기였다** (X-0028 · WO-0205, 2026-09-12).
//   전에는 `nlocals <= 32 && ncode < 4096` 이 빠른 프레임의 입장 조건에 **섞여** 있었다. 그런데
//   그 수들은 «이 op 은 자연 C 로 못 낸다» 는 말이 아니라 **여기 잡아 둔 배열이 그만큼**이라는
//   말이었다(`lk/slot/rmap/…[32]` · `kstack_t at[4096]`). 소유자가 물었다: *"단지 파라미터 갯수가
//   많다는 이유로 태그 경로로 가야 되는 이유가 있나."* — 없다.
//   ⇒ 배열을 **필요한 만큼 잡는다.** 그러면 빠른/태그 판정에 남는 기준은 하나다:
//     *모든 값의 종류를 방출 시점에 못 박을 수 있는가.*
//   ☞ 새 상수를 만들지 않는다 — 상수를 만들면 그것이 다음 번의 «낡은 한 곳» 이 된다(교훈 7).
//   ★ 한 번 잡으면 컴파일이 끝날 때까지 **다시 쓴다**(줄이지 않는다). 컴파일러의 수명이
//     한 번의 실행이므로 이것이 arena 와 같은 값을 한다.
typedef struct { unsigned char *lk, *slot, *rmap, *lez, *lln, *lfl; int *smap; proven_size_t cap; } cbe_lbuf_t;
static cbe_lbuf_t g_lb;
static bool cbe_lbuf_ensure(proven_size_t n) {
    if (n < 1) n = 1;
    if (g_lb.cap >= n) return true;
    proven_size_t nc = g_lb.cap ? g_lb.cap * 2 : 64;
    while (nc < n) nc *= 2;
    // ★ W-20261112-0207 — proven 힙(옛 크기를 넘긴다 — alloccheck 빌드가 맞는지 본다). 하나가 실패해도 다른 하나는 제 크기 그대로 산다.
    unsigned char *p6 = (unsigned char *)low_realloc(g_lb.lk, g_lb.cap * 6, nc * 6);
    if (!p6) return false;
    g_lb.lk = p6; g_lb.slot = p6 + g_lb.cap; g_lb.rmap = p6 + g_lb.cap * 2; g_lb.lez = p6 + g_lb.cap * 3; g_lb.lln = p6 + g_lb.cap * 4; g_lb.lfl = p6 + g_lb.cap * 5;
    int *ps = (int *)low_realloc(g_lb.smap, g_lb.cap * sizeof *ps, nc * sizeof *ps);
    if (!ps) return false;            // ★ 못 잡으면 **거짓말하지 않는다** — 부르는 쪽이 태그 경로로 간다
    g_lb.lk = p6; g_lb.slot = p6 + nc; g_lb.rmap = p6 + nc * 2;
    g_lb.lez = p6 + nc * 3; g_lb.lln = p6 + nc * 4; g_lb.lfl = p6 + nc * 5;
    g_lb.smap = ps; g_lb.cap = nc;
    return true;
}
// 명령마다 하나 — 종류 시뮬레이터의 자리표(`at`·`at2` 둘이 각각 쓴다)
static kstack_t *g_at[2]; static proven_size_t g_atcap[2];
static kstack_t *cbe_at_ensure(int which, proven_size_t n) {
    if (n < 1) n = 1;
    if (g_atcap[which] >= n) return g_at[which];
    proven_size_t nc = g_atcap[which] ? g_atcap[which] * 2 : 4096;
    while (nc < n) nc *= 2;
    kstack_t *p = (kstack_t *)low_realloc(g_at[which], g_atcap[which] * sizeof *p, nc * sizeof *p);   // ★ W-20261112-0207 — proven 힙
    if (!p) return 0;
    g_at[which] = p; g_atcap[which] = nc;
    return p;
}

static bool cbe_kind_ok(const low_ir_t *ir, const low_ir_def_t *d, const bool *sc);
static bool cbe_w128_place(const low_ir_t *ir, const low_ir_def_t *d);   // ★ W-20261105-2304 (아래)
// ★ RFC-0151 — 128 비트 정수의 낱말인가(low_vm.c vm_is_w128 의 거울 — 그 판정이 바뀌면 여기도).
static bool cbe_is_w128(const low_ir_ins_t *in) {
    if (in->w == IRW_MK128) return true;
    if (in->w == IRW_CAST) return (in->a & 0xff) == 16 || (in->a & IR_SRC_W128);
    switch (in->w) {
        case IRW_ADD: case IRW_SUB: case IRW_MUL: case IRW_DIV: case IRW_MOD: case IRW_NEG:
        case IRW_LT: case IRW_LE: case IRW_GT: case IRW_GE:
        case IRW_BAND: case IRW_BOR: case IRW_BXOR: case IRW_BNOT: case IRW_SHL: case IRW_SHR: case IRW_WSHL: case IRW_WSHR:
        case IRW_ROTL: case IRW_ROTR: case IRW_POPCNT: case IRW_CLZ: case IRW_CTZ: case IRW_BSWAP:
            return (in->a & IR_TY_KNOWN) && (in->a & 0xff) == 128 && !(in->a & IR_TY_FLT);
        default: return false;
    }
}
static bool cbe_out_w128(const low_ir_def_t *d) {
    return d->out_tyname.size == 4 && (memcmp(d->out_tyname.ptr, "u128", 4) == 0 || memcmp(d->out_tyname.ptr, "i128", 4) == 0);
}
// ★ 각 def 이 **무엇을 돌려주는가**(정수인가 감싼 값인가) — 호출자가 스택을 고르려면
//   그것을 **정적으로** 알아야 한다. 시뮬레이터가 채운다.
static unsigned char g_retk[512];
static proven_size_t g_cur_di = 0;   // ★ 지금 내리는 def — 반환 모양을 고르는 데 쓴다
// ★★★ **호출 규약: 안 쓰는 것은 넘기지 않는다** (2026-07-27 벤치).
//   빠른 경로는 늘 **일곱 개**의 배열 포인터를 넘겼다(ai·as·af·ar·ab·ao·av). 정수 하나만 받는
//   op 도 호출 자리마다 일곱 배열을 **짓고** 넘겼다 — 주소를 넘기니 그 배열들은 레지스터에
//   못 남고 메모리에 서야 한다. 재귀에서 그 값이 그대로 드러났다.
//   ⇒ **본문이 실제로 만지는 것만** 넘긴다. 여기서도 종류를 새로 추론하지 않는다 —
//     본문을 먼저 만들고 **방출된 글자**를 본다(교훈 7: 추론을 두 벌로 지으면 갈린다).
#define LWA_AI 1u
#define LWA_AS 2u
#define LWA_AF 4u
#define LWA_AR 8u
#define LWA_AB 16u
#define LWA_AO 32u
#define LWA_AV 64u
// ★★★★★ **여덟 번째 칸 — 레코드의 슬라이스 필드** (RFC-0106 단계 1).
//   스칼라 필드는 64 바이트 버퍼에, **슬라이스 필드는 이 평행 칸**에 산다.
//   `rsl[파라미터 * K_REC_MAX + 슬롯]` — 평평하게 둔다(C 시그니처가 단순해진다).
#define LWA_RSL 128u
#define LWA_ALL 255u
static unsigned g_argmask[512];
// ★★★ **울릴 수 없는 검사는 비용만이다** (2026-07-27 벤치).
//   호출 깊이 원장(`lw_depth`)은 진짜 보장이다 — 무한 재귀를 스택 붕괴 대신 **패닉**으로
//   바꾸고, 스레드마다 따로 센다(DRF). 골든이 그것을 지킨다(deep 5000 → 패닉). **안 없앤다.**
//   그러나 **재귀가 불가능한 op** 에서는 그 검사가 결코 울리지 않는다: 호출 그래프에 사이클이
//   없으면 사슬 길이는 def 개수로 정적으로 묶인다. 그런 자리의 증감은 순전히 비용이다
//   (게다가 메모리 의존 사슬이라 호출을 직렬화한다 — 실측 fib 의 격차 대부분이 이것이었다).
//   ⇒ **사이클에 속한 def 에만** 원장을 둔다. 판정을 못 하면(간접 호출 등) **두는 쪽**이다.
static bool g_needdepth[512];
// ★ `cbe_inline_hint` 가 op 의 크기를 물어보려면 IR 이 필요하다 — 계획 단계에서 받아 둔다.
static const low_ir_t *g_ir_for_inline;
// ★★★★★ **벡터 스택은 쓰는 만큼만** (2026-08-16, 메모리 축 측정이 찾았다).
//   `lw_v vs[16]` 은 **1024 바이트**다(`lw_v` = 64 B 공용체 × 16 칸). 4 레인 내적 하나가
//   프레임 **1,744 B** 를 쓰고 있었고 — 그 커널은 바로 전날 ×3.20 → ×1.12 로 빨라진 것이다.
//   **속도와 발자국이 반대로 움직였는데 축이 비어 있어서 안 보였다.**
//   ⇒ 이 함수가 **밀어 넣는 자리의 수**를 세어 그만큼만 잡는다.
//   ★ 건전성: 세는 것은 **밀어 넣는 자리의 수**이고, 스택 기계에서 동시에 살아 있는 벡터는
//     그보다 많을 수 없다(루프 안의 한 자리는 매 바퀴 밀고 빼므로 한 칸만 쓴다). 그래서
//     `g_vspush <= 16` 이면 이 크기는 **정확한 상계**이고 검사가 할 일이 없다.
//   ★★ 열여섯을 넘겨 **잘라 낸 함수에만** 넘침 검사를 낸다(`@@VSG@@` 자리표). 거기서는
//     상계가 아니므로 조용히 남의 바이트를 밟는 대신 **말하고 멈춰야** 한다 — 이 저장소가
//     가장 무서워하는 것이 *조용히 틀린 답*이고, 크기 최적화는 그것을 만들기 딱 좋은 자리다.
//     ☞ 검사를 **모든** 함수에 두었더니 simd 가 ×1.12 → ×1.17 로 느려졌다(루프 안의 분기
//       둘). 안 넘칠 수 있음을 아는 자리에까지 검사를 두는 것은 보수가 아니라 **낭비**다.
static int g_vspush;
// ★★★★★ **칸 크기도 쓰는 만큼만** (2026-08-18 — 명령 수 축이 가리킨 자리).
//   `vs[]` 의 **칸 수**는 2026-08-16 에 줄였는데 **칸 크기**는 64 B 고정이었다:
//   `lw_v` 가 최대 폭(u8×64)에 맞춘 공용체라, `vec u32 4`(16 B)만 쓰는 프로그램도
//   칸마다 **네 배**를 잡는다. 그리고 그 크기가 속도까지 먹는다 — 64 B 원소 배열은
//   GCC 가 **스칼라 치환(SRA)을 포기**해서 값이 레지스터에 못 산다(§9-4 의 되돌린
//   시도가 가르쳐 준 것: 슬롯의 주소를 취하면 진다).
//   ⇒ 유닛이 **실제로 쓰는** 최대 폭으로 칸을 만든다. simd 처럼 16 B 면 칸이 16 B 다.
//   ★ 건전성: 벡터의 출처는 `splat`·`load`·벡터 파라미터뿐이고(나머지는 거기서 파생),
//     셋 다 폭이 **컴파일 시 상수**다. 그러니 이 최댓값은 상계가 아니라 **정확한 폭**이다.
//   ★★ 2의 거듭제곱으로 올린다(공용체 배열 길이가 정수여야 한다) · 하한 8 · 상한 64.
static int g_vbytes = 64;
// ★★★★★ **레코드 풀도 쓰는 만큼만** (2026-08-18 — `lw_v` 와 **같은 수법**).
//   `lw_recs[4096]` 은 **1,605,632 B** 다(`lowrec` 392 B × 4096). 그리고 그것이
//   hosted 빌드 bss 1,736,704 B 의 **92%** 다 — 레코드를 하나도 안 만드는 프로그램도
//   그대로 짊어진다(메모리 문서 §2 가 적어 둔 그 수).
//   ★ 레코드를 만드는 것은 `lw_make` 뿐이고 그것은 **태그 경로**다. 유닛의 모든 op 이
//     하강했으면(`--why-slow` 가 0 을 찍는 그 상태) 도달할 수 없다.
//   ★★ 그래도 **가드에 기댄다**: `lw_nrec >= lw_reclim` 이 이미 있으므로, 내 판정이 틀려
//     누군가 레코드를 만들면 **조용히 남의 바이트를 밟는 대신 말하고 멈춘다**.
//     크기 최적화는 *조용히 틀린 답*을 만들기 딱 좋은 자리라 이 순서가 중요하다.
//   ★★★ 동시성 유닛은 **제외한다**: 워커마다 풀을 **서로소 구간으로 쪼개 쓰므로**
//     (`T->rb/rl`) 작은 풀은 그 분할을 못 견딘다. 판정을 못 하면 **두는 쪽**이다.
static bool cbe_uses_conc(const low_ir_t *ir);   /* ★ 아래에 있다 — 선언만 앞으로 */
// ★★★ **argv 슬라이스 버퍼는 안 줄인다 — 세 봤고, 상계가 안 선다** (2026-08-18, 되돌림).
//   `sbufs[16][8192]` 은 131,072 B 라 레코드 풀 다음으로 큰 자리다. 그래서 칸 수를
//   *유닛의 슬라이스 파라미터 최대 개수*로 잡아 봤다(matmul 3 · simd 2). **차등 스윕이
//   즉시 잡았다**: `native cannot read argument: [128]`.
//   ☞ 이유가 구조적이다: **디스패처는 어느 op 을 부를지 모르는 채로 argv 를 읽는다.**
//     인자 변환이 op 선택보다 먼저이므로 "이 op 은 슬라이스를 둘만 받는다" 는 사실을
//     그 자리에서 쓸 수가 없다. 내 상계는 *op 별* 사실인데 버퍼는 *유닛 전체*의 것이었다.
//   ★★ 그리고 **실패가 조용하다**: 칸이 모자라면 `[128]` 이 아래 정수 분기로 떨어져
//     `strtoll("[128]")` = **0** 이 된다. 틀린 값을 조용히 넘기는 자리다 —
//     이 저장소가 가장 무서워하는 종류이고, 131 KB 는 그 값을 치를 만하지 않다.
//   ⇒ 줄이려면 먼저 **디스패처가 op 을 먼저 고르고 인자를 읽게** 바꿔야 한다. 별도 일감.
static int g_recpool = 4096;
// ★ **박스 풀도 같은 판정**이다(2026-08-18). `lw_boxes[4096]` = 196,608 B 이고, 박스를
//   만드는 것은 `lw_wrap_some/ok` 등 **태그 경로**뿐이며 가드도 같은 모양으로 이미 있다
//   (`lw_nbox >= lw_boxlim` → panic). 그래서 판정을 나누지 않고 **한 곳에서** 정한다 —
//   나누면 두 벌이 되고, 두 벌은 갈린다(교훈 7).
static int g_boxpool = (int)LOW_BOXPOOL;
static void cbe_mark_pools(const low_ir_t *ir, const bool *sc) {
    g_recpool = 4096; g_boxpool = (int)LOW_BOXPOOL;
    if (cbe_uses_conc(ir)) return;   // 워커가 풀을 **서로소 구간**으로 쪼갠다 — 작은 풀은 못 견딘다
    for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++)
        if (ir->defs[i].lowered && !sc[i]) return;      // 태그로 떨어진 op 이 하나라도 있으면 그대로
    g_recpool = 64; g_boxpool = 64;
}
static void cbe_mark_vbytes(const low_ir_t *ir) {
    int mx = 0;
    for (proven_size_t i = 0; i < ir->ndefs; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        if (d->lowered && d->code)
            for (proven_size_t j = 0; j < d->ncode; j++) {
                low_irw_t w = d->code[j].w;
                if (w != IRW_SPLAT && w != IRW_VLOAD && w != IRW_VSTORE) continue;
                int ln = (int)((d->code[j].a >> 8) & 0xff), ez = (int)(d->code[j].a & 0xff);
                if (ln > 0 && ln <= 64 && ez > 0 && ez <= 8 && ln * ez > mx) mx = ln * ez;
            }
        for (proven_size_t q = 0; q < d->nparams && q < LOW_MAX_PARAMS; q++)
            if ((d->param_vec >> q) & 1u) {
                int ln = (int)d->param_vlanes[q], ez = (int)d->param_ebits[q];
                if (ln > 0 && ez > 0 && ln * ez > mx) mx = ln * ez;
            }
    }
    int p = 8; while (p < mx) p <<= 1;
    g_vbytes = p > 64 ? 64 : p;
}
static void cbe_mark_depth(const low_ir_t *ir) {
    static bool adj[512][512];
    proven_size_t n = ir->ndefs < 512 ? ir->ndefs : 512;
    for (proven_size_t i = 0; i < n; i++) {
        g_needdepth[i] = false;
        for (proven_size_t j = 0; j < n; j++) adj[i][j] = false;
    }
    for (proven_size_t i = 0; i < n; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        for (proven_size_t k = 0; k < d->ncode; k++) {
            const low_irw_t w_ = d->code[k].w;
            // ★ 2026-11-10 (W-20261110-2349) — `drain` · `schedule` 은 처리 코드를 이 op 의 깊이 + 1 에서 돌린다(VM 과 같이).
            //   그 깊이를 전역에 적으려면 이 op 이 깊이를 알아야 한다 ⇒ 센다. `spawn send` 는 그 처리 코드로 가는 간선이다.
            if (w_ == IRW_DRAIN || w_ == IRW_SCHED) { g_needdepth[i] = true; continue; }
            if (w_ != IRW_CALL && w_ != IRW_ASEND && w_ != IRW_TASEND) continue;
            proven_size_t c = IR_CALL_IDX(d->code[k].a);
            if (c >= n) { g_needdepth[i] = true; continue; }   // 모르는 대상 = 있다고 친다
            adj[i][c] = true;
        }
    }
    // 도달성 폐포(n ≤ 512 — 단순하게, 그러나 **분명하게**).
    for (proven_size_t m = 0; m < n; m++)
        for (proven_size_t i = 0; i < n; i++)
            if (adj[i][m])
                for (proven_size_t j = 0; j < n; j++)
                    if (adj[m][j]) adj[i][j] = true;
    for (proven_size_t i = 0; i < n; i++) if (adj[i][i]) g_needdepth[i] = true;   // 자기에게 돌아온다 = 재귀
    // ★ 재귀 op 을 (간접적으로라도) 부르는 쪽도 원장을 세야 사슬 길이가 맞는다 —
    //   VM 은 **사슬 전체**를 세기 때문이다. 여기서 아끼면 패닉 자리가 달라진다(오라클 위반).
    // ★★★ 2026-11-10 (W-20261110-2349 · 소유자 «가»): **세는 op 이 부르는 op 도 센다** — VM 은 모든 부름을 센다.
    //   전에는 재귀의 바닥에서 부르는 잎 op(재귀에 닿지 않는 op)을 안 세서, 한도 바로 앞에서 VM 은 멈추고 네이티브는
    //   답했다(`r 127` → VM E-VM-DEPTH · 네이티브 8128). 잎이 잎을 부르면 두 칸이 모자랐다 — 부르는 자리에서 «+1» 을
    //   보는 것으로는 못 맞춘다(docs/known-defects/depth-leaf-call-off-by-one.md). 그래서 아래로도 닫는다: 세는 op 이
    //   (간접으로라도) 부르는 op 은 센다. 그러면 그 op 을 부르는 쪽도 세야 하므로(인자를 넘길 것이 있어야 한다) 위아래를
    //   고정점까지 돌린다. 재귀가 없는 덩어리는 여전히 안 센다 — 아래에서 사슬 길이로 따로 본다.
    bool ch = true;
    while (ch) {
        ch = false;
        for (proven_size_t i = 0; i < n; i++)
            for (proven_size_t j = 0; j < n; j++) {
                if (!adj[i][j] || g_needdepth[i] == g_needdepth[j]) continue;
                g_needdepth[i] = g_needdepth[j] = true; ch = true;
            }
        // ★ 내용이 같아 한 심볼이 되는 def 들(canon_of)은 시그니처가 하나다 — 한쪽이 세면 다 센다(로컬 op 의 같은 몸 두 벌이
        //   «conflicting types» 로 C 컴파일을 깼다 · 골든 localop).
        for (proven_size_t i = 0; i < n; i++) {
            const low_ir_def_t *cd = (const low_ir_def_t *)ir->defs[i].canon_of;
            if (!cd) continue;
            proven_size_t ci = (proven_size_t)(cd - ir->defs);
            if (ci >= n || g_needdepth[i] == g_needdepth[ci]) continue;
            g_needdepth[i] = g_needdepth[ci] = true; ch = true;
        }
    }
    // ★★ 재귀가 없는 덩어리: 사슬은 op 수로 묶이지만 그 길이가 한도(128)를 넘을 수 있으면 VM 은 멈춘다 — 그런 덩어리도 센다.
    //   높이 h(i) = 1 + max h(부르는 op) (재귀가 없으니 잘 정의된다). 세지 않는 op 만 본다.
    static int ht[512];
    for (proven_size_t i = 0; i < n; i++) ht[i] = g_needdepth[i] ? -1 : 1;
    for (proven_size_t it = 0; it < n; it++) {
        bool hc = false;
        for (proven_size_t i = 0; i < n; i++) {
            if (g_needdepth[i]) continue;
            for (proven_size_t j = 0; j < n; j++)
                if (adj[i][j] && ht[j] + 1 > ht[i]) { ht[i] = ht[j] + 1; hc = true; }
        }
        if (!hc) break;
    }
    bool deep = false;
    for (proven_size_t i = 0; i < n; i++) if (!g_needdepth[i] && ht[i] > 128) deep = true;
    if (deep) for (proven_size_t i = 0; i < n; i++) g_needdepth[i] = true;   // 드문 꼴 — 통째로 센다(두는 쪽)
}
// ★★★ **정수만 돌려주는 op 은 정수로 돌려준다** (2026-07-27 벤치).
//
//   반환은 늘 `lw_r` 였다 — `long long v; double f; lw_sl s; long long r[8]; int tag;` = **104 바이트**.
//   그것을 **값으로** 돌려주니 호출마다 그 크기의 복사(그리고 숨은 반환 슬롯 포인터)가 섰다.
//   재귀가 잦은 코드에서 이게 지배적이었다: fib 가 C 의 **10.7배**였는데 반환만 스칼라로 바꾸니
//   **3.6배**가 됐다(같은 답, 같은 검사).
//   ⇒ 반환 종류는 이미 `g_retk` 가 안다. **새 추론을 짓지 않고** 그것을 읽는다 — 정수면 정수로.
static bool cbe_ret_scalar(proven_size_t di) { return di < 512 && g_retk[di] == K_INT; }
static unsigned char g_retopt[512];   // ★ K_OPT 반환이 **레코드**를 감쌌으면 그 sidx+1 (0=스칼라)
static unsigned char g_ret_optrec;    // cbe_kind_run 이 RET 에서 채운다 → cbe_kind_ok 가 g_retopt 로
// ★ 2026-11-03 (W-20261103-1518) — K_OPT 반환이 **마디의 뷰**(할당기 바이트 위 그림자 — cbe_nodeview)를 쌌으면 그 그림자 번호+1(0 = 아니다).
//   g_retopt(레코드)와 따로 둔다: 받는 쪽이 그 값을 레코드로 벗기면 안 된다(2026-11-01 의 vm_vecgen `at`). 값은 lw_r 의 .s(주소, 바이트 수)다.
static unsigned char g_retnode[512], g_ret_optnode;
// ★★ 2026-10-31 (W-20261031-1610) — **돌려주는 슬라이스의 원소 폭 · 표식**(부동 · 부호). 전에는 폭 1 의 바이트 슬라이스만 돌려줄 수 있었다 — 받는 쪽이
//   폭을 몰라 `index` 가 바이트로 읽었기 때문이다(VM ≠ native). 이제 op 마다 적어 두고 부르는 쪽 · 경계 어댑터가 그것을 읽는다.
static unsigned char g_retve[512], g_retfl[512];
static unsigned char g_ret_ve, g_ret_fl; static bool g_ret_veset;
static unsigned char g_retrb[512], g_ret_rb;   // ★ 2026-11-01 — 줄의 나열을 돌려주면 그 줄 바이트(K_ROWS · 원소 폭은 g_retve)
// ★★★ **도구는 자기가 못 하는 일을 말해야 한다.** 어떤 op 이 왜 느린 경로에 남았는지
//   물어볼 수 없으면, 그 목록은 **아무도 줄이지 못한다**(그리고 아무도 재지 않는다).
static char g_why[512][96];
static int g_kj, g_kw;   // 종류 추론이 **어디서** 멈췄나
static char g_sub[64];   // …그리고 **왜**
static bool g_why_on;
void low_cbe_why(bool on) { g_why_on = on; }
const char *low_cbe_why_of(proven_size_t i) { return (i < 512) ? g_why[i] : ""; }
// ★★★ **구조체 파라미터의 필드에 쓰는 def** — 액터 핸들러가 정확히 그 모양이다
//   (`proc inc` 가 인스턴스의 `value` 를 올린다). 태그 경로에서 레코드는 **박스**라
//   그 쓰기가 **호출자에게 보인다.** 값으로 넘기면 그 쓰기가 **사라진다.**
//   ⇒ 구조체 파라미터는 **참조로** 넘긴다(`ar`), 그리고 반환에서 **되쓴다**.
//   ★ 보수적으로 잡는다: `field.store` 가 있고 구조체 파라미터가 있으면 그렇다고 본다.
//
// ★★★ **직접 쓰기만으로는 부족했다 — `send` 는 전이(transitive)로 쓴다.**
//   액터 인스턴스를 인자로 받아 `send x <op>` 하는 proc(예: `bump(x rec)` 가 `send x put`)
//   은 자기 안에 IRW_FSTORE 가 **없다** — 값 `send` 는 핸들러로의 **IRW_CALL 로 하강**하고,
//   필드 쓰기는 그 핸들러(`put`) 안에 있다. 그래서 `cbe_pmut` 이 직접 FSTORE 만 보면 `bump`
//   을 **비변형**으로 오판했고, 되쓰기 사슬이 끊겨 **인자로 넘긴 액터의 변형이 네이티브에서
//   사라졌다**(VM 은 액터를 박스로 공유하므로 7, 네이티브는 값 복사라 0 — 차등 스윕이 잡는
//   최악 부류). ⇒ **전이 폐포**로 잡는다: 구조체 파라미터가 있고, 스스로 변형 op 을 부르면
//   그 op 도 변형이다. 되쓰기(cbe_wb·호출지·래퍼)와 자격 판정(홈 없는 즉석 make 거절)이
//   모두 같은 술어를 쓰므로 사슬이 일관되게 이어진다.
//   과대근사는 안전하다: 평범한(비액터) 레코드는 **제자리 변형 경로가 없어** 되쓰기가
//   불변 값의 복사 = 무해이고, 홈 없는 인자엔 자격 판정이 느린 경로로 물러난다(여전히 정답).
static bool cbe_pmut_direct(const low_ir_def_t *d) {
    if (!d->param_struct) return false;
    // ★★★ 2026-11-02 (W-20261102-0024) — `mut` 구조체 매개변수는 칸 쓰기(`set field`) 없이도 바뀐다: 배열 칸의 원소 쓰기
    //   (`set idx field r. body . 0 . v`)는 INDEX · ISTORE 라 아래 FSTORE 만 보던 판정이 «안 바뀜» 이라 했고, 경계 어댑터가 되쓰기를
    //   건너뛰었다 — 태그 경로 op 이 넘긴 레코드에 쓴 것이 사라졌다(VM 77 · 네이티브 5 · v1.25.1). `mut` 이면 바뀐 것으로 본다.
    //   `owned` 는 세지 않는다 — 소유를 넘긴 쪽은 그 값을 다시 못 본다(되쓸 곳이 없다 · `net.shut` 이 그 꼴).
    if (d->param_struct & d->param_mutx) return true;
    for (proven_size_t k = 0; k < d->ncode; k++)
        if (d->code[k].w == IRW_FSTORE) return true;
    return false;
}
static const low_ir_t *g_pmut_ir;
static bool g_pmut_ready;
static bool g_pmut[512];
static bool cbe_pmut(const low_ir_def_t *d) {
    if (!d->param_struct) return false;
    if (g_pmut_ready && g_pmut_ir) {
        proven_size_t idx = (proven_size_t)(d - g_pmut_ir->defs);
        if (idx < 512 && idx < g_pmut_ir->ndefs) return g_pmut[idx];
    }
    return cbe_pmut_direct(d);
}
// 호출을 타고 전파한다 — 변형 op 을 부르는(구조체 파라미터 있는) op 도 변형이다(고정점).
//   ★ **cbe_mark_scalar 보다 먼저** 돌려야 한다: 종류 시뮬레이터의 자격 판정(호출지의
//     "홈 없는 구조체 변형" 거절)이 이 술어를 읽으므로, 자격과 방출이 **같은 값**을 봐야
//     `rloc[-1]` 같은 방출을 막는다.
static void cbe_mark_pmut(const low_ir_t *ir) {
    g_pmut_ir = ir;
    for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++)
        g_pmut[i] = cbe_pmut_direct(&ir->defs[i]);
    for (bool changed = true; changed; ) {
        changed = false;
        for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++) {
            const low_ir_def_t *d = &ir->defs[i];
            if (g_pmut[i] || !d->param_struct) continue;
            for (proven_size_t k = 0; k < d->ncode; k++) {
                // ★ 값 `send` = IRW_CALL(핸들러로 하강). 핸들러가 변형이면 이 op 도 변형이다.
                if (d->code[k].w != IRW_CALL) continue;
                proven_size_t c = IR_CALL_IDX(d->code[k].a);
                if (c < ir->ndefs && c < 512 && g_pmut[c]) { g_pmut[i] = true; changed = true; break; }
            }
        }
    }
    g_pmut_ready = true;
}

// ★ **이름은 컴파일 시점에 슬롯 번호가 된다.** 런타임 strcmp 가 사라지는 자리.
//   슬롯의 뜻은 **구조체 선언**이 정한다 — make 가 아니라(같은 구조체를 두 곳에서 만들어도
//   슬롯이 같아야 한다).
static int cbe_rec_slot(const low_ir_t *ir, int sidx, proven_i64 fi) {
    if (sidx < 0 || (proven_size_t)sidx >= ir->nstructs) return -1;
    if (fi < 0 || (proven_size_t)fi >= ir->nfields) return -1;
    const low_ir_struct_t *S = &ir->structs[sidx];
    for (proven_size_t q = 0; q < S->nf && q < K_REC_MAX; q++)
        if (proven_u8str_view_eq(S->f[q].name, ir->fields[fi])) return (int)q;
    return -1;
}

// ★★★ 2026-11-01 (W-20261101-1150 · 소유자 «모두 진행») — **액터를 가리키는 칸**: 컨테이너가 든 할당기(`vec` 의 `al a .`).
//   VM 과 태그 경로에서 이 칸은 액터 **하나**를 가리킨다(make 는 값 구조체를 베끼지만 액터 상태는 안 베낀다 — low_vm.c 의
//   vm_rec_clone). 그래서 빠른 레코드도 이 칸에 값을 풀어 두지 않는다: 평행 칸(`rssl` · `rlocsl` · `rsl`) 하나에 태그 쪽 레코드의
//   자리를 담고(lw_aref_in), 말을 보낼 때 그 레코드를 풀어 빠른 몸을 부르고 되쓴다. 칸의 모양은 둘이다 — 액터 상태가 바이트로
//   못 보이면 크기 0 · sidx -1(이름으로 찾는다), 보이면 sidx 가 그 상태를 가리킨다. 답: 그 액터 상태의 sidx, 아니면 -1.
static int cbe_aref_sidx(const low_ir_t *ir, const low_ir_sfield_t *f) {
    if (f->boxed || f->arrn || f->elem || f->flt || f->slmeta || f->bxself) return -1;
    if (f->sidx >= 0)
        return ((proven_size_t)f->sidx < ir->nstructs && ir->structs[f->sidx].is_actor_state) ? (int)f->sidx : -1;
    if (f->size) return -1;
    const proven_u8str_view_t t = f->tyname.size ? f->tyname : f->cname;
    if (!t.size) return -1;
    const proven_u8str_view_t tb = low_view_after_last(t, '.');
    for (proven_size_t i = 0; i < ir->nstructs; i++) {
        if (!ir->structs[i].is_actor_state) continue;
        if (proven_u8str_view_eq(ir->structs[i].name, t) ||
            proven_u8str_view_eq(low_view_after_last(ir->structs[i].name, '.'), tb)) return (int)i;
    }
    return -1;
}
// ★★ 2026-11-03 (W-20261103-1143) — **마디를 가리키는 레코드 칸** `root option (owned node) .`(`tree.head`). 그 마디가 이 유닛에서
//   늘 할당기 바이트 위의 뷰라면(cbe_nodeview) 칸의 값은 주소 하나다 — 빠른 레코드는 그 칸의 바이트 자리(태그 값 칸이라 8 바이트)에
//   주소를 둔다(0 = none, 마디 칸과 같은 모양). 읽으면 option(마디의 뷰), 쓰면 그 주소 · 경계에서 태그 값 option ↔ 주소. 답: 그림자 번호, 아니면 -1.
static int cbe_nodeview(const low_ir_t *ir, int sidx);
static int cbe_f_nopt(const low_ir_t *ir, const low_ir_sfield_t *f) {
    if (!f->boxed || f->bxself || f->bxs1 || f->elem || f->arrn || f->slmeta || f->flt || f->size != 8) return -1;
    if (!f->tyname.size || !proven_u8str_view_eq(f->tyname, (proven_u8str_view_t){ (const proven_u8 *)"option", 6 })) return -1;
    const proven_u8str_view_t t = f->cname;
    if (!t.size) return -1;
    const proven_u8str_view_t tb = low_view_after_last(t, '.');
    int hit = -1, nshort = 0;   // 온 이름이 먼저 · 짧은 이름은 하나뿐일 때만(같은 짧은 이름이 모듈마다 설 수 있다)
    for (proven_size_t i = 0; i < ir->nstructs && hit < 0; i++) {
        const low_ir_struct_t *T = &ir->structs[i];
        if (!T->is_actor_state && !T->is_mmio && !T->is_shadow && proven_u8str_view_eq(T->name, t)) hit = (int)i;
    }
    for (proven_size_t i = 0; i < ir->nstructs && hit < 0; i++) {
        const low_ir_struct_t *T = &ir->structs[i];
        if (!T->is_actor_state && !T->is_mmio && !T->is_shadow && proven_u8str_view_eq(low_view_after_last(T->name, '.'), tb)) nshort++;
    }
    if (hit < 0 && nshort == 1)
        for (proven_size_t i = 0; i < ir->nstructs && hit < 0; i++) {
            const low_ir_struct_t *T = &ir->structs[i];
            if (!T->is_actor_state && !T->is_mmio && !T->is_shadow && proven_u8str_view_eq(low_view_after_last(T->name, '.'), tb)) hit = (int)i;
        }
    if (hit < 0) return -1;
    const int sh = cbe_nodeview(ir, hit);
    return (sh >= 0 && sh < 254) ? sh : -1;
}
// ★★ 2026-11-01 (W-20261101-1150) — **소유한 값 구조체 칸**(`a owned h .` — `net.pair` 의 두 끝). VM 은 이 칸에 그 구조체의 박스를 두고
//   `make` 때 베낀다(값이다) — 그 뒤로는 옮길 뿐이다(소유 검사가 옮긴 칸을 다시 못 읽게 한다). 빠른 레코드도 액터 칸처럼 평행 칸에 태그 쪽
//   레코드의 자리를 담는다: 읽으면 그 레코드를 빠른 레코드로 **베껴** 내고(옮겨 내는 것이라 되쓸 것이 없다), `make` 는 빠른 레코드로 태그
//   쪽 레코드를 새로 짓는다(VM 의 베끼기와 같다). 안의 구조체는 수 칸만 가진 것만(그 모양을 지을 `make` 가 이 유닛에 있어야 한다).
static int cbe_make_for_struct(const low_ir_t *ir, int sidx);
static bool cbe_rec_ok(const low_ir_t *ir, int sidx);
static int cbe_obox_sidx1(const low_ir_t *ir, const low_ir_sfield_t *f, bool deep);
// ★★ 2026-11-02 (W-20261102-0024 O4) — **소유 칸 안의 소유 칸**(`pagecache.pin2` 의 `under owned (pin b)` — pin 이 다시 `held owned (cache b)` 를
//   가진다). 안쪽 구조체가 수 칸과 «수 칸만 가진 구조체의 소유 칸» 으로만 되어 있으면 연다(한 겹 더 — 되부름은 없다). 안쪽 레코드의 소유 칸은
//   평행 칸(태그 쪽 레코드의 자리)으로 함께 나른다: 읽으면 그 자리를 풀어 두고(`cbe_emit_rec_unpack` · 평행 칸 있음), `make` 는 그 자리를 넘긴다.
static bool cbe_obox_inner2_ok(const low_ir_t *ir, int sidx) {
    if (sidx < 0 || (proven_size_t)sidx >= ir->nstructs) return false;
    const low_ir_struct_t *S = &ir->structs[sidx];
    if (S->nf == 0 || S->nf > K_REC_MAX || S->total > K_REC_BYTES || S->is_shadow || S->is_mmio || S->is_actor_state) return false;
    if (low_ir_struct_has_enumlayout(ir, (proven_size_t)sidx)) return false;
    bool any = false;
    for (proven_size_t q = 0; q < S->nf; q++) {
        const low_ir_sfield_t *f = &S->f[q];
        if (cbe_obox_sidx1(ir, f, false) >= 0) { any = true; continue; }
        if (f->boxed || f->arrn || f->slmeta || f->sidx >= 0 || f->flt || f->bxself || !f->size || f->size > 8) return false;
    }
    return any;
}
static int cbe_obox_sidx(const low_ir_t *ir, const low_ir_sfield_t *f) { return cbe_obox_sidx1(ir, f, true); }
static int cbe_obox_sidx1(const low_ir_t *ir, const low_ir_sfield_t *f, bool deep) {
    if (!f->boxed || !f->owned || f->elem || f->arrn || f->slmeta || f->bxself || f->flt) return -1;
    const proven_u8str_view_t t = f->cname.size ? f->cname : f->tyname;
    if (!t.size) return -1;
    const proven_u8str_view_t tb = low_view_after_last(t, '.');
    for (proven_size_t i = 0; i < ir->nstructs; i++) {
        const low_ir_struct_t *T = &ir->structs[i];
        if (T->is_actor_state || T->is_mmio || T->is_shadow) continue;
        if (!proven_u8str_view_eq(T->name, t) && !proven_u8str_view_eq(low_view_after_last(T->name, '.'), tb)) continue;
        if (!cbe_rec_ok(ir, (int)i) && !(deep && cbe_obox_inner2_ok(ir, (int)i))) return -1;
        return (int)i;   // ★ 그 구조체를 **짓는** 자리(`make`)는 태그 쪽 레코드의 칸 이름표가 있어야 한다 — 그 검사는 make 가 한다
    }
    return -1;
}
// ★ 2026-11-01 — 두 구조체가 **같은 모양**인가(완전적용 별칭 `gvec` = `vecgen.vec u8 allocs.bump_bytes` 는 칸까지 같은 다른 구조체다).
//   칸의 이름 · 순서 · 크기 · 자리 · 종류가 다 같으면 빠른 레코드(바이트 · 평행 칸)가 같으므로 넘겨도 된다.
static bool cbe_struct_same(const low_ir_t *ir, int a, int b) {
    if (a == b) return true;
    if (a < 0 || b < 0 || (proven_size_t)a >= ir->nstructs || (proven_size_t)b >= ir->nstructs) return false;
    const low_ir_struct_t *A = &ir->structs[a], *B = &ir->structs[b];
    if (A->nf != B->nf || A->total != B->total || A->is_actor_state || B->is_actor_state || A->is_mmio || B->is_mmio) return false;
    for (proven_size_t z = 0; z < A->nf; z++) {
        const low_ir_sfield_t *f = &A->f[z], *g = &B->f[z];
        if (!proven_u8str_view_eq(f->name, g->name) || f->size != g->size || f->off != g->off || f->boxed != g->boxed ||
            f->elem != g->elem || f->flt != g->flt || f->sidx != g->sidx || f->arrn != g->arrn || f->be != g->be) return false;
        if (low_sfield_sgn(f) != low_sfield_sgn(g)) return false;
        if (cbe_aref_sidx(ir, f) != cbe_aref_sidx(ir, g) || cbe_obox_sidx(ir, f) != cbe_obox_sidx(ir, g)) return false;
        if (cbe_f_nopt(ir, f) != cbe_f_nopt(ir, g)) return false;
    }
    return true;
}
// 이 칸이 평행 칸에 사는가 — 슬라이스 칸이거나 액터를 가리키는 칸.
static bool cbe_f_slot(const low_ir_t *ir, const low_ir_sfield_t *f) {
    return (f->boxed && f->elem) || cbe_aref_sidx(ir, f) >= 0 || cbe_obox_sidx(ir, f) >= 0;
}
// 평행 칸에 **태그 쪽 레코드의 자리**를 두는 칸인가(액터 칸 · 소유한 값 구조체 칸) — 경계에서 그 자리를 옮긴다.
static bool cbe_f_place(const low_ir_t *ir, const low_ir_sfield_t *f) {
    return cbe_aref_sidx(ir, f) >= 0 || cbe_obox_sidx(ir, f) >= 0;
}
// ★ 액터 상태 레코드의 **제 자리**(태그 쪽 레코드)는 평행 칸의 마지막 칸에 산다 — 경계가 들어올 때 채운다. 이 자리가 있어야
//   `make` 가 그 액터를 컨테이너의 칸에 담을 수 있다(`open` 이 받은 할당기를 `vec` 에 넣는다). 갓 띄운 액터(빠른 경로의 `spawn`)는
//   제 자리가 없다 — 그 값은 종류 표식 CBE_FL_NOID 를 달고, 그것을 칸에 담거나 담을 수 있는 op 에 넘기면 거절한다.
#define CBE_AID 15
#define CBE_FL_NOID 0x40
static bool g_aid_need[64];      // 이 유닛의 어떤 `make` 가 이 액터 상태를 칸에 담는가(sidx < 64)
static unsigned long long g_acap[512], g_aderef[512], g_aunp[512];   // 액터 상태 sidx 의 비트 — 담을 수 있다 · 칸을 거쳐 말한다 · 풀어 든다
static bool g_abox[512];         // 이 op(이나 그것이 부르는 op)이 태그 쪽 액터 레코드를 짓는다(빠른 경로의 `spawn` — 아래 cbe_abox)
static bool cbe_make_zero(const low_ir_t *ir, const low_ir_def_t *d, proven_size_t j, int sx, proven_size_t nf);
// ★★★ 2026-11-10 (W-20261110-1548 · 2 단계 2a, 소유자 «(b)») — **빠른 경로의 `spawn send`**. 이 op 이 띄운 액터(갓 띄운 지역 · CBE_FL_NOID)
//   앞으로 메시지를 넣으면 그 액터는 레코드 풀 밖의 자기 자리(프렐류드 lw_fa_mem)에 살고, 우편함 칸은 **그 바이트와 처리 코드의 빠른 판**을
//   든다(어댑터 lw_q_*). 태그 값 · 레코드 · 되쓰기가 없다. 고르는 차례는 그대로다(번호는 LW_RECPOOL 뒤 — 같은 줄 · 같은 busy).
//   g_faloc — 이 op 에서 그렇게 사는 지역(비트) · g_qthunk — 어댑터를 내야 하는 처리 코드 · g_mdel — 메시지를 배달할 수 있다(`schedule` ·
//   `drain` 이 몸에 있거나 부르는 op 에 있다 — 고정점).
static unsigned long long g_faloc[512];
static bool g_qthunk[512];
static bool g_mdel[512];
static bool g_conc_t0;   /* --conc-t0 (아래 low_cbe_set_conc_t0 — 여기서 먼저 읽는다) */
static bool cbe_may_suspend(const low_ir_t *ir, proven_size_t d, int depth);
// ★★ 2026-11-11 (W-20261111-2031 D2) — 처리 코드 `h` 의 액터를 다른 흐름이 **쥔 채 멈춰** 있을 수 있는가: 같은 액터 타입(첫 매개변수의 구조체)의
//   처리 코드 가운데 하나라도 멈출 수 있으면 그렇다. 모르면 그렇다고 본다.
static bool cbe_actor_holdable(const low_ir_t *ir, proven_size_t h) {
    if (h >= ir->ndefs || !ir->defs[h].is_actor || !ir->defs[h].nparams || !(ir->defs[h].param_struct & 1u)) return true;
    proven_size_t sx = (proven_size_t)ir->defs[h].param_sidx[0];
    for (proven_size_t e = 0; e < ir->ndefs; e++)
        if (ir->defs[e].is_actor && ir->defs[e].nparams && (ir->defs[e].param_struct & 1u) && (proven_size_t)ir->defs[e].param_sidx[0] == sx &&
            cbe_may_suspend(ir, e, 0)) return true;
    return false;
}
static long long cbe_restart_max(const low_ir_t *ir, const low_ir_def_t *h);
static bool cbe_struct_has_sl(const low_ir_t *ir, int sx);

// ★ 이 구조체를 만드는 **make 자리**. 태그 런타임의 레코드는 **make 인덱스로 필드 이름을
//   찾는다**(lw_mk_fields[mk]) — 그러니 경계에서 레코드를 만들려면 그 자리를 알아야 한다.
//   없으면 **구조체를 돌려주는 op 을 안 내린다**(경계에서 되돌릴 방법이 없으니까).
static int cbe_make_for_struct(const low_ir_t *ir, int sidx);

// make 자리 → 구조체 인덱스
static int cbe_make_sidx(const low_ir_t *ir, proven_i64 mk) {
    if (mk < 0 || (proven_size_t)mk >= ir->nmakes) return -1;
    for (proven_size_t i = 0; i < ir->nstructs; i++)
        if (proven_u8str_view_eq(ir->structs[i].name, ir->makes[mk].type_name)) return (int)i;
    // ★★ 2026-11-04 (W-20261104-0013 O4) — 다른 모듈의 구조체를 별칭으로 지은 자리(`lit g.point`)는 make 의 이름이 `g.point` 이고 배치는
    //   `point` 다. 끝 이름이 단위 안에서 **하나뿐**이면 그 배치다(둘이면 모른다 — 고르지 않는다). 전엔 짝을 못 찾아 op 이 태그 경로에 남았다.
    const proven_u8str_view_t tl_ = low_view_after_last(ir->makes[mk].type_name, '.');
    if (tl_.size == ir->makes[mk].type_name.size) return -1;
    int hit_ = -1;
    for (proven_size_t i = 0; i < ir->nstructs; i++)
        if (proven_u8str_view_eq(ir->structs[i].name, tl_)) { if (hit_ >= 0) return -1; hit_ = (int)i; }
    return hit_;
}

static int cbe_make_for_struct(const low_ir_t *ir, int sidx) {
    if (sidx < 0 || (proven_size_t)sidx >= ir->nstructs) return -1;
    for (proven_size_t m = 0; m < ir->nmakes; m++)
        if (proven_u8str_view_eq(ir->makes[m].type_name, ir->structs[sidx].name) &&
            ir->makes[m].nfields == ir->structs[sidx].nf) return (int)m;
    return -1;
}
// ★★★ 2026-11-04 (W-20261104-0013 O4) — **짐을 싣는 열거의 갈래 값**(`shape.circle 2`)을 빠른 레코드로 나른다. 배치는 `$t` + 갈래들의 칸을
//   이어 붙인 것이고(ir 의 fat-struct), 갈래 값은 그 갈래의 칸만 채운 레코드다(태그 경로 · VM). 빠른 레코드는 배치 전체를 들고 다른 갈래의
//   칸은 0 이다 — 읽는 쪽(`match`)은 `$t` 를 먼저 보고 제 갈래의 칸만 읽는다. 경계에서 태그 값으로 되지을 때는 `$t` 로 갈래를 골라 그 갈래의
//   `make` 자리로 짓는다. 단위 안에서 한 번도 짓지 않는 갈래는 make 자리가 없다 — 그 갈래의 값은 이 단위에 생길 수 없으므로 되짓기는
//   그 자리에서 멈춘다(조용히 다른 갈래로 짓지 않는다). 적어도 한 갈래는 make 자리가 있어야 연다.
static int cbe_penum_mk(const low_ir_t *ir, int sidx, unsigned v) {
    const low_ir_struct_t *S = &ir->structs[sidx];
    proven_size_t want = 0;
    for (proven_size_t q = 0; q < S->nf; q++) if ((S->f[q].vmask >> v) & 1u) want++;
    for (proven_size_t m = 0; m < ir->nmakes; m++) {
        if (!proven_u8str_view_eq(ir->makes[m].type_name, S->name) || ir->makes[m].nfields != want) continue;
        bool ok_ = true;
        for (proven_size_t z = 0; z < ir->makes[m].nfields && ok_; z++) {
            int sl = -1;
            for (proven_size_t q = 0; q < S->nf; q++) if (proven_u8str_view_eq(S->f[q].name, ir->makes[m].fields[z])) { sl = (int)q; break; }
            ok_ = sl >= 0 && ((S->f[sl].vmask >> v) & 1u);
        }
        if (ok_) return (int)m;
    }
    return -1;
}
static bool cbe_penum_ok(const low_ir_t *ir, int sidx) {
    if (sidx < 0 || (proven_size_t)sidx >= ir->nstructs) return false;
    const low_ir_struct_t *S = &ir->structs[sidx];
    if (!S->is_penum || !S->nvar || S->nvar > 32 || !S->nf || S->f[0].size != 4) return false;
    for (unsigned v = 0; v < S->nvar; v++) if (cbe_penum_mk(ir, sidx, v) >= 0) return true;
    return false;
}
// ★★ 2026-11-01 (W-20261101-1150) — 액터를 가리키는 칸의 **유닛 전체 판정**. 비트는 액터 상태 구조체의 sidx(64 미만)다.
//   g_aid_need — 어떤 `make` 가 그 액터를 칸에 담는다(그러면 그 액터 레코드는 제 자리를 평행 칸 CBE_AID 에 나른다).
//   g_acap   — 이 op(이나 그것이 부르는 op)이 그 액터를 칸에 담을 수 있다.
//   g_aderef — 이 op(이나 그것이 부르는 op)이 칸을 거쳐 그 액터에게 말한다(태그 쪽 레코드를 풀고, 부르고, 되쓴다).
//   g_aunp   — 이 op 이 그 액터를 **풀어 든다**(구조체 매개변수 · 빠른 경로의 `spawn`).
//   ★ 풀어 든 사본과 칸을 거친 원본이 한 op 안에서 함께 살면 둘이 갈린다(사본에 쓴 것을 원본이 모르고, 원본에 쓴 것을 사본이
//     모르며, 사본은 돌아갈 때 원본을 덮는다). 그래서 g_aunp & g_aderef 가 겹치는 op 은 태그 경로에 둔다 — 거기서는 둘이 같은 박스다.
static void cbe_mark_aref(const low_ir_t *ir) {
    for (proven_size_t i = 0; i < 64; i++) g_aid_need[i] = false;
    for (proven_size_t i = 0; i < 512; i++) { g_acap[i] = g_aderef[i] = g_aunp[i] = 0; g_abox[i] = false; }
    for (proven_size_t m = 0; m < ir->nmakes; m++) {
        const int sx = cbe_make_sidx(ir, (proven_i64)m);
        if (sx < 0) continue;
        for (proven_size_t z = 0; z < ir->structs[sx].nf; z++) {
            const int as_ = cbe_aref_sidx(ir, &ir->structs[sx].f[z]);
            if (as_ >= 0 && as_ < 64) g_aid_need[as_] = true;
        }
    }
    for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        for (proven_size_t q = 0; q < d->nparams && q < LOW_MAX_PARAMS; q++)
            if (((d->param_struct >> q) & 1u) && d->param_sidx[q] < 64 && (proven_size_t)d->param_sidx[q] < ir->nstructs &&
                ir->structs[d->param_sidx[q]].is_actor_state) g_aunp[i] |= 1ull << d->param_sidx[q];
        for (proven_size_t k = 0; k < d->ncode; k++) {
            const low_ir_ins_t *in = &d->code[k];
            if (in->w == IRW_MAKE) {
                const int sx = cbe_make_sidx(ir, in->a);
                if (sx < 0) continue;
                if (sx < 64 && ir->structs[sx].is_actor_state) {
                    // ★ 컨테이너가 담는 액터를 이 op 이 띄우면 처음부터 태그 쪽 레코드로 짓는다(cbe_abox) — 풀어 든 사본이 없다.
                    if (g_aid_need[sx] && ir->structs[sx].nf <= CBE_AID && cbe_make_zero(ir, d, k, sx, ir->makes[in->a].nfields)) g_abox[i] = true;
                    else g_aunp[i] |= 1ull << sx;
                }
                for (proven_size_t z = 0; z < ir->structs[sx].nf; z++) {
                    const int as_ = cbe_aref_sidx(ir, &ir->structs[sx].f[z]);
                    if (as_ >= 0 && as_ < 64) g_acap[i] |= 1ull << as_;
                    if (cbe_obox_sidx(ir, &ir->structs[sx].f[z]) >= 0) g_abox[i] = true;   // ★ 소유 칸의 레코드를 짓는다
                }
            } else if (in->w == IRW_FIELD && in->a >= 0 && (proven_size_t)in->a < ir->nfields) {
                // 어느 구조체의 칸인지는 종류가 안다 — 여기서는 그 이름의 칸이 액터를 가리키는 구조체를 모두 센다(보수적).
                for (proven_size_t t = 0; t < ir->nstructs; t++)
                    for (proven_size_t z = 0; z < ir->structs[t].nf; z++)
                        if (proven_u8str_view_eq(ir->structs[t].f[z].name, ir->fields[in->a])) {
                            const int as_ = cbe_aref_sidx(ir, &ir->structs[t].f[z]);
                            if (as_ >= 0 && as_ < 64) g_aderef[i] |= 1ull << as_;
                        }
            }
        }
    }
    for (bool changed = true; changed; ) {
        changed = false;
        for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++) {
            const low_ir_def_t *d = &ir->defs[i];
            for (proven_size_t k = 0; k < d->ncode; k++) {
                if (d->code[k].w != IRW_CALL) continue;
                const proven_size_t c = IR_CALL_IDX(d->code[k].a);
                if (c >= ir->ndefs || c >= 512) continue;
                const unsigned long long cap = g_acap[i] | g_acap[c], der = g_aderef[i] | g_aderef[c];
                if (cap != g_acap[i] || der != g_aderef[i]) { g_acap[i] = cap; g_aderef[i] = der; changed = true; }
                if (g_abox[c] && !g_abox[i]) { g_abox[i] = true; changed = true; }
            }
        }
    }
}
static int cbe_leaves(const low_ir_t *ir, int sidx);
static int cbe_flat_off(const low_ir_t *ir, int sidx, int z);
static bool cbe_rec_nest_ok(const low_ir_t *ir, int sidx);
static int cbe_slot_by_name(const low_ir_t *ir, int sidx, proven_u8str_view_t nm);
// ★★★ **평탄 슬롯(r_.r[base..]) → 태그 경로의 (중첩) 레코드 박스**를 재귀로 짓는다.
//   자식 박스를 **먼저** 할당하고(내부부터), 부모가 그 박스를 참조한다. `*nid` 로 고유 이름을 준다.
//   반환: 이 노드의 박스를 담은 C 변수 이름 접미(nid).
static void cbe_emit_recon_scalar(low_out_t *out, const low_ir_sfield_t *F, int me, size_t z, unsigned foff) {
    if (F->flt)
        low_fpf(out, "        rr_{}->f[{}] = lw_flt(lw_b2f(lw_ld_f(r_.r + {}, {}, {}), {}));\n",
                PROVEN_ARG((int)(me)), PROVEN_ARG((size_t)(z)), PROVEN_ARG((unsigned)(foff)), PROVEN_ARG((int)((int)F->size)), PROVEN_ARG((int)(F->be ? 1 : 0)), PROVEN_ARG((int)((int)F->size)));
    else if (low_sfield_sgn(F) && F->size < 8)
        low_fpf(out, "        rr_{}->f[{}] = lw_int((long long)(lw_ld_f(r_.r + {}, {}, {}) << {}) >> {});\n",
                PROVEN_ARG((int)(me)), PROVEN_ARG((size_t)(z)), PROVEN_ARG((unsigned)(foff)), PROVEN_ARG((int)((int)F->size)), PROVEN_ARG((int)(F->be ? 1 : 0)), PROVEN_ARG((int)(64 - 8 * (int)F->size)), PROVEN_ARG((int)(64 - 8 * (int)F->size)));
    else
        low_fpf(out, "        rr_{}->f[{}] = lw_int((long long)lw_ld_f(r_.r + {}, {}, {}));\n",
                PROVEN_ARG((int)(me)), PROVEN_ARG((size_t)(z)), PROVEN_ARG((unsigned)(foff)), PROVEN_ARG((int)((int)F->size)), PROVEN_ARG((int)(F->be ? 1 : 0)));
}
static int cbe_emit_rec_recon(low_out_t *out, const low_ir_t *ir, int sx, int base, int *nid) {
    const low_ir_struct_t *S = &ir->structs[sx];
    if (S->is_penum) {   // ★ 2026-11-04 — 짐을 싣는 열거: `$t` 로 갈래를 골라 그 갈래의 make 자리로 짓는다(cbe_penum_ok 가 모든 갈래의 자리를 보장했다)
        int me = (*nid)++;
        low_fpf(out, "        if (lw_nrec >= lw_reclim) lw_panic(\"record pool\");\n");
        low_fpf(out, "        int box_{} = lw_nrec++; lowrec *rr_{} = &lw_recs[box_{}];\n", PROVEN_ARG((int)(me)), PROVEN_ARG((int)(me)), PROVEN_ARG((int)(me)));
        low_fpf(out, "        switch ((int)lw_ld_f(r_.r + {}, 4, 0)) {{\n", PROVEN_ARG((unsigned)((unsigned)(base + (int)S->f[0].off))));
        for (unsigned v = 0; v < S->nvar; v++) {
            const int mv = cbe_penum_mk(ir, sx, v);
            const unsigned tv_ = (unsigned)S->vbase + v;   // `$t` 의 값 = 단위 전체의 갈래 번호
            if (mv < 0) { low_fpf(out, "        case {}: lw_panic(\"an enum variant this unit never builds\");\n", PROVEN_ARG((unsigned)(tv_))); continue; }
            low_fpf(out, "        case {}: rr_{}->mk = {}; rr_{}->nf = {};\n", PROVEN_ARG((unsigned)(tv_)), PROVEN_ARG((int)(me)), PROVEN_ARG((int)(mv)), PROVEN_ARG((int)(me)), PROVEN_ARG((int)((int)ir->makes[mv].nfields)));
            for (proven_size_t z = 0; z < ir->makes[mv].nfields && z < IR_MAKE_MAXF; z++) {
                int sl = cbe_slot_by_name(ir, sx, ir->makes[mv].fields[z]);
                cbe_emit_recon_scalar(out, &S->f[sl], me, z, (unsigned)(base + (int)S->f[sl].off));
            }
            low_puts(out, "        break;\n");
        }
        low_puts(out, "        default: lw_panic(\"an enum value whose tag names no variant\"); }\n");
        return me;
    }
    int mk = cbe_make_for_struct(ir, sx);
    int child[IR_MAKE_MAXF]; int childnid[IR_MAKE_MAXF];
    for (proven_size_t z = 0; z < S->nf && z < IR_MAKE_MAXF; z++) {
        child[z] = -1;
        if (S->f[z].sidx >= 0 && !cbe_f_place(ir, &S->f[z]))   // ★ 액터 칸 · 소유 칸은 새 박스를 짓지 않는다(2026-11-01)
            childnid[z] = cbe_emit_rec_recon(out, ir, (int)S->f[z].sidx,
                                             base + (int)S->f[z].off, nid), child[z] = 1;
    }
    int me = (*nid)++;
    low_fpf(out, "        if (lw_nrec >= lw_reclim) lw_panic(\"record pool\");\n");
    low_fpf(out, "        int box_{} = lw_nrec++; lowrec *rr_{} = &lw_recs[box_{}];\n", PROVEN_ARG((int)(me)), PROVEN_ARG((int)(me)), PROVEN_ARG((int)(me)));
    low_fpf(out, "        rr_{}->mk = {}; rr_{}->nf = {};\n", PROVEN_ARG((int)(me)), PROVEN_ARG((int)(mk)), PROVEN_ARG((int)(me)), PROVEN_ARG((int)((int)S->nf)));
    // 박스의 필드는 **make 순서** — 이름으로 슬롯을 찾는다.
    for (proven_size_t z = 0; z < ir->makes[mk].nfields && z < IR_MAKE_MAXF; z++) {
        int sl = cbe_slot_by_name(ir, sx, ir->makes[mk].fields[z]);
        // ★★★★ **바이트 오프셋이다** (2026-08-03 — 표현 통합). 그전에는 평탄 슬롯 번호였다.
        //   ☞ 차등 스윕이 바로 이 자리를 고발했다: `vm_view.low` 의 `mk_hdr` 가 VM 에서는
        //     {0xDEADBEEF, 258, 7} 인데 네이티브가 {222, 173, 190} 을 냈다 — 0xDE·0xAD·0xBE,
        //     즉 **바이트 하나씩 연속으로** 읽은 것이다(슬롯 번호를 오프셋으로 썼으니까).
        //     저장소를 바꾸면 그 저장소를 **읽는 모든 자리**를 함께 바꿔야 한다.
        unsigned foff = (unsigned)(base + (int)S->f[sl].off);
        if (cbe_f_nopt(ir, &S->f[sl]) >= 0)   // ★ 2026-11-03 — 마디를 가리키는 칸: 주소 → 태그 값 option
            low_fpf(out, "        rr_{}->f[{}] = lw_nopt_out(r_.r + {}, {});\n", PROVEN_ARG((int)(me)), PROVEN_ARG((size_t)((size_t)z)), PROVEN_ARG((unsigned)(foff)), PROVEN_ARG((int)(cbe_f_nopt(ir, &S->f[sl]))));
        else if (cbe_f_place(ir, &S->f[sl]))   // ★★ 2026-11-01 — 액터 칸 · 소유 칸: 같은 태그 쪽 레코드로 돌아간다
            low_fpf(out, "        rr_{}->f[{}] = lw_aref_out(r_.rs ? r_.rs[{}] : (lw_sl){{0,0}});\n", PROVEN_ARG((int)(me)), PROVEN_ARG((size_t)((size_t)z)), PROVEN_ARG((int)(sl)));
        else if (S->f[sl].arrn)   // ★ 2026-11-01 (W-20261101-1828) — 배열 칸: 빠른 바이트를 보는 값을 두고, 아래 `lw_rec_arrs` 가 레코드의 제 바이트로 베낀다
            low_fpf(out, "        {{ lowv av_ = {{0}}; av_.p = r_.r + {}; av_.n = {}; if ({} == 1) av_.tag = LWV_SLICE; else {{ av_.tag = LWV_VARRAY; av_.box = {}; }}"
                         " rr_{}->f[{}] = av_; }}\n",
                    PROVEN_ARG((unsigned)(foff)), PROVEN_ARG((unsigned)(cbe_arr_len(&S->f[sl]))), PROVEN_ARG((unsigned)(cbe_arr_rb(&S->f[sl]) ? 0u : (unsigned)S->f[sl].arresz)),
                    PROVEN_ARG((unsigned)(cbe_arr_rb(&S->f[sl]) ? cbe_arr_rb(&S->f[sl]) : (unsigned)S->f[sl].arresz)), PROVEN_ARG((int)(me)), PROVEN_ARG((size_t)((size_t)z)));   // ★ 줄 칸: 줄 수 × 줄 바이트(lw_rec_arrs 는 바이트 수만 본다)
        else if (S->f[sl].sidx >= 0)
            low_fpf(out, "        {{ lowv cv_ = {{0}}; cv_.tag = LWV_REC; cv_.box = box_{}; rr_{}->f[{}] = cv_; }}\n",
                    PROVEN_ARG((int)(childnid[sl])), PROVEN_ARG((int)(me)), PROVEN_ARG((size_t)((size_t)z)));
        else if (S->f[sl].boxed && S->f[sl].elem)
            // ★★★★★ **경계 복원 — 슬라이스 필드** (RFC-0106 단계 4, 2026-08-30 소유자 결정).
            //   반환된 레코드를 태그 박스로 되지을 때, 슬라이스 필드는 바이트에 없다.
            //   빠른 프레임이 그 값을 `r_.rs[슬롯]` 으로 함께 돌려주고 여기서 태그 값으로
            //   되돌린다. 원소 폭이 1 이 아니면 태그 쪽은 **원소 수**를 기대하므로 나눈다.
            //   ☞ *쓰기는 읽기의 거울이 아니다 — 나가는 값은 **되돌려 주어야** 산다.*
            low_fpf(out, "        {{ lowv sv_ = {{0}}; lw_sl q_ = r_.rs ? r_.rs[{}] : (lw_sl){{0,0}}; sv_.p = q_.p;"
                         " if ({} > 1) {{ sv_.tag = LWV_VARRAY; sv_.n = q_.n / {}; sv_.box = {}; }}"
                         " else {{ sv_.tag = LWV_SLICE; sv_.n = q_.n; }}"
                         " if (q_.p == LW_UNINIT) sv_ = lw_int(0);"   /* ★ 안 세운 칸은 0 으로 돌아간다(태그 경로 · VM 의 모양) */
                         " rr_{}->f[{}] = sv_; }}\n",
                    PROVEN_ARG((int)(sl)), PROVEN_ARG((unsigned)((unsigned)S->f[sl].elem)), PROVEN_ARG((unsigned)((unsigned)S->f[sl].elem)), PROVEN_ARG((unsigned)((unsigned)S->f[sl].elem)),
                    PROVEN_ARG((int)(me)), PROVEN_ARG((size_t)((size_t)z)));
        else cbe_emit_recon_scalar(out, &S->f[sl], me, (size_t)z, foff);   // ★ 2026-10-31 — 부호 있는 칸은 부호를 늘린다(그 안에서)
    }
    {   bool arr_ = false;
        for (proven_size_t z = 0; z < S->nf; z++) if (S->f[z].arrn) arr_ = true;
        if (arr_) low_fpf(out, "        lw_rec_arrs(rr_{}, {});\n", PROVEN_ARG((int)(me)), PROVEN_ARG((int)(mk)));   // ★ 배열 칸의 바이트를 레코드의 것으로(태그 경로의 `lw_make` 와 같다)
    }
    (void)child;
    return me;
}

// 이 구조체를 **레코드 값**으로 내릴 수 있나 — 필드가 전부 스칼라여야 한다.
//   ★ `allow_flt`: 레코드 **값**(make·field·encode·반환)은 부동 필드를 **비트로** 슬롯에
//     담을 수 있다. 하지만 구조체 **파라미터**는 안 된다 — 어댑터가 필드를 정수로 읽는다.
static bool cbe_rec_ok_f(const low_ir_t *ir, int sidx, bool allow_flt) {
    if (sidx >= 0 && (proven_size_t)sidx < ir->nstructs && low_ir_struct_has_enumlayout(ir, (proven_size_t)sidx)) return false;   // ★ RFC-0150 §8-31
    if (sidx < 0 || (proven_size_t)sidx >= ir->nstructs) return false;
    const low_ir_struct_t *S = &ir->structs[sidx];
    if (S->nf == 0 || S->nf > K_REC_MAX || S->total > K_REC_BYTES) return false;
    // ★★★ **배열 칸을 가진 구조체는 빠른 경로가 나르지 않는다** (2026-10-11 · RFC-0144 의 matrix 칸이 찾았다). 아래 `cbe_rec_slice_ok`
    //   는 그렇게 막는데 이 관문은 막지 않았다 — 배열 칸 구조체는 `viewable` 이라 그대로 통과했고, 빠른 경로의 `make` 는 그 칸을
    //   **슬라이스 칸**으로 실어 나열 리터럴의 읽기 전용 상수를 가리키게 했다. 칸에 쓰면 상수에 쓰고(`-O2` 는 그 쓰기를 못 본
    //   것으로 읽는다), VM 은 100 · 네이티브는 1 이었다. 값 복사(칸의 바이트를 레코드가 가진다)는 태그 경로가 한다.
    for (proven_size_t q = 0; q < S->nf; q++) if (S->f[q].arrn) return false;
    // ★★★ **필드가 전부 *크기 있는 스칼라* 여야 한다 — 그리고 그것을 이제 진짜로 검사한다**
    //   (2026-07-19, lib/alloc.low 이 찾았다). 위 주석은 그렇게 **단언하면서** 중첩·부동만 봤다.
    //   `state mem mut slice u8 . .` 를 가진 actor 는 그냥 통과했고, 경계 어댑터가
    //   **모든 필드를 정수로 읽었다**:
    //       ar[0][0] = lw_want_int(lw_field(a[0], 0), "ints");   ← mem 은 슬라이스다
    //   ⇒ 네이티브가 `panic: ints` 로 죽었다. VM 은 태그 값이라 멀쩡했으므로 **오라클이 갈렸고**,
    //     그 갈림이 이 결함을 볼 수 있게 한 유일한 이유다.
    //   ★★ **크기로는 못 잡는다**: 액터 상태를 읽는 코드가 크기를 모르는 타입에 `sz = 8` 을
    //     **때려 넣는다**(low_ir.c) — 그래서 `slice u8` 이 `u64` 와 구별되지 않는다.
    //     구별이 남아 있는 곳은 그때 함께 세우는 `viewable`(= 전 필드가 크기 있는 스칼라)뿐이다.
    //   ★ 인자 **있는** 핸들러는 다른 이유로 태그 경로에 남아 안 밟혔다 ⇒ `vm_alloc.low`
    //     (스칼라 state)로는 절대 보이지 않았다. **모양이 없으면 검사도 없다.**
    if (!S->viewable) return false;
    for (proven_size_t q = 0; q < S->nf; q++) {
        if (S->f[q].sidx >= 0) return false;                  // 중첩 구조체 필드 ⇒ 안 내린다
        if (S->f[q].flt) { if (!allow_flt || S->f[q].size > 8) return false; }  // 부동은 값에서만
    }
    return true;
}
static bool cbe_rec_ok(const low_ir_t *ir, int sidx) { return cbe_rec_ok_f(ir, sidx, false); }

// ★★ 2026-11-01 (W-20261101-1828 · 소유자 «다음 진행할 것들 마저») — **배열 칸을 가진 구조체를 한 op 안의 값으로** 나른다.
//   VM 의 레코드는 배열 칸의 바이트를 제 버퍼에 가지고(`vm_rec_arrays`), `make` 가 그 바이트를 베끼고, `field` 는 그 바이트를 보는
//   슬라이스를 주고, `set field` 는 제자리에 베낀다. 빠른 레코드의 배치(`ir_struct_layout`)는 이미 C 와 같은 인라인 배치라 그 바이트가
//   레코드 버퍼 안에 있다 — 2026-10-11 의 결함은 `make` 가 칸에 **나열 상수를 가리키는 슬라이스**를 담은 것이었다(지금은 베낀다).
//   여는 것: 원소가 수인 배열 칸(줄 · 구조체 원소는 아님)과 크기 있는 스칼라 칸. 경계(매개변수 · 반환 · 인자)는 아직 닫혀 있다 —
//   바깥 관문들(`cbe_rec_ok_f` …)은 그대로 배열 칸을 거절한다.
// ★ 2026-11-02 (W-20261102-0024 O4) — 배열 안의 배열 칸(줄 · 세 겹 이상)도 연다: 읽으면 줄의 나열(K_ROWS · 줄 바이트 ≤ 255 · 줄 꼴 칸 ≤ 63)이고,
//   바이트는 납작한 배열 칸과 같다(arrn = 맨 안 원소 수 · arresz = 그 폭). 베끼기 · 되쓰기는 바이트 수만 본다.
static unsigned cbe_arr_rb(const low_ir_sfield_t *f) {
    return (f->arrmeta & IR_ROW_BIT) ? (unsigned)(((proven_u64)f->arrmeta >> 32) & 0xffffffu) : 0u;
}
static unsigned cbe_arr_rsub(const low_ir_sfield_t *f) {
    return (f->arrmeta & IR_ROW_BIT) ? (unsigned)(((proven_u64)f->arrmeta & IR_ROWSUB_MASK) >> IR_ROWSUB_SHIFT) : 0u;
}
static bool cbe_f_arr(const low_ir_sfield_t *f) {
    if (!f->arrn || f->arresz < 1 || f->arresz > 8 || (f->arrmeta & IR_STRUCT_BIT)) return false;
    if (f->arrmeta & IR_ROW_BIT) {
        const unsigned rb_ = cbe_arr_rb(f);
        return rb_ && rb_ <= 255 && rb_ % f->arresz == 0 && ((unsigned)f->arrn * f->arresz) % rb_ == 0 && cbe_arr_rsub(f) <= 63;
    }
    return true;
}
static unsigned char cbe_arr_fl(const low_ir_sfield_t *f) {
    unsigned char m_ = 0;
    if (f->arrmeta & IR_FLT_BIT) m_ = CBE_FL_FLT;
    else if ((f->arrmeta & IR_SGN_BIT) && f->arresz < 8) m_ = CBE_FL_SGN;
    return (unsigned char)(m_ | (cbe_arr_rsub(f) << 2));   // 줄 칸이면 아래 줄 꼴(K_ROWS 의 fl)
}
// 배열 칸을 읽은 값의 종류와 길이 — 줄이면 K_ROWS(길이 = 줄 수 · vn = 줄 바이트), 아니면 K_SL(길이 = 원소 수).
static unsigned char cbe_arr_k(const low_ir_sfield_t *f) { return cbe_arr_rb(f) ? K_ROWS : K_SL; }
static unsigned cbe_arr_len(const low_ir_sfield_t *f) {
    const unsigned rb_ = cbe_arr_rb(f);
    return rb_ ? (unsigned)f->arrn * f->arresz / rb_ : (unsigned)f->arrn;
}
static bool cbe_rec_arr_ok(const low_ir_t *ir, int sidx) {
    if (sidx < 0 || (proven_size_t)sidx >= ir->nstructs) return false;
    if (low_ir_struct_has_enumlayout(ir, (proven_size_t)sidx)) return false;
    const low_ir_struct_t *S = &ir->structs[sidx];
    if (S->nf == 0 || S->nf > K_REC_MAX || S->total > K_REC_BYTES || S->is_shadow || S->is_mmio || S->is_actor_state) return false;
    bool any = false;
    for (proven_size_t q = 0; q < S->nf; q++) {
        const low_ir_sfield_t *f = &S->f[q];
        if (f->arrn) { if (!cbe_f_arr(f)) return false; any = true; continue; }
        if (f->sidx >= 0 || f->boxed || !f->size || f->size > 8) return false;
        if (f->flt && f->size != 4 && f->size != 8) return false;
    }
    return any;
}

// ★★★★★ **RFC-0106 단계 1 — 슬라이스 필드를 든 struct 도 파라미터로 나른다.**
//   스칼라 필드는 지금처럼 64 바이트 버퍼에, **슬라이스 필드는 그 옆의 평행 칸**
//   (`rsl[파라미터][슬롯]`)에 산다. 이 관문은 *"그 모양인가"* 만 답한다:
//     · 필드 수·크기는 여전히 프레임 안 · 중첩 struct·부동은 여전히 거절
//     · boxed 필드는 **슬라이스이고 원소 폭을 아는 것**만 허용(`elem != 0`)
//   ★ 단계 1 은 **읽기만**이다. 쓰기(FSTORE)·`make`·호출 인자·반환은 아래 자리들이
//     여전히 엄격한 `cbe_rec_ok` 를 물으므로 **저절로 물러난다** — 한 번에 하나씩.
static bool cbe_rec_slice_ok(const low_ir_t *ir, int sidx) {
    if (sidx < 0 || (proven_size_t)sidx >= ir->nstructs) return false;
    const low_ir_struct_t *S = &ir->structs[sidx];
    for (proven_size_t q = 0; q < S->nf; q++) if (S->f[q].arrn) return false;   // ★ T2b-3b 배열 칸 — 값 복사는 태그 경로가 한다
    if (S->nf == 0 || S->nf > K_REC_MAX || S->total > K_REC_BYTES) return false;
    bool any_slice = false;
    for (proven_size_t q = 0; q < S->nf; q++) {
        if (cbe_f_place(ir, &S->f[q])) { any_slice = true; continue; }   // ★ 액터 칸 · 소유한 값 구조체 칸 — 평행 칸에 산다(2026-11-01)
        if (cbe_f_nopt(ir, &S->f[q]) >= 0) { any_slice = true; continue; }   // ★ 2026-11-03 — 마디를 가리키는 칸: 바이트 자리에 주소 하나
        if (S->f[q].sidx >= 0) return false;          // 중첩 구조체 필드
        if (S->f[q].flt) return false;                // 부동은 값에서만
        if (S->f[q].boxed) {
            if (!S->f[q].elem) return false;          // 슬라이스가 아니거나 폭을 모른다
            any_slice = true;
            continue;
        }
        // ★★★★★ **크기 0 = 바이트로 나를 수 없는 것** (2026-08-30, WO-0155 — 골든이 찾았다).
        //   `vecgen` 의 `vec` 은 `al a .` 를 든다 — comptime 타입으로 받은 **capability** 다.
        //   `boxed` 도 아니고(타입 낱말이 하나다) 크기도 **0** 인데, 내 관문은 그것을
        //   *"평범한 스칼라"* 로 보고 통과시켰다 ⇒ 경계가 `lw_want_int` 로 읽어
        //   네이티브가 **`panic: ints`** 로 죽었다(`jobq`·`entity`).
        //   ★ 그리고 크기 0 필드가 있으면 `ir_struct_layout` 이 **거기서 멈추므로**
        //     그 뒤 필드의 오프셋도 못 믿는다 — 두 이유가 같은 한 줄로 막힌다.
        //   ☞ *"스칼라가 아니다" 를 `boxed` 하나로만 물으면, 크기 0 이라는 다른 얼굴을 놓친다.*
        if (!S->f[q].size) return false;
    }
    if (!any_slice) return S->viewable;               // 슬라이스가 없으면 옛 판정 그대로
    return true;
}

// ★★ 2026-10-31 (W-20261031-1421 · 소유자 «다른 것들 다 진행») — **안에 구조체를 품은 · 부동 칸을 가진 구조체 파라미터**.
//   빠른 경로의 레코드는 그 구조체의 바이트 배치이므로 안의 구조체는 같은 바이트의 한 구간이고(FIELD 가 주소만 옮긴다), 부동 칸은 그 폭의
//   비트다. 막혀 있던 것은 경계 어댑터였다 — 태그 값을 칸마다 정수로 풀었다. 그래서 어댑터가 안으로 내려가 풀게 하고(cbe_emit_rec_in),
//   **몸이 레코드를 바꾸지 않는 op 에만** 연다: 되쓰기(mut 파라미터 · 액터 핸들러)는 여전히 칸마다 정수로 쓴다 — 그 자리는 열지 않았다.
static bool cbe_rec_nested_ok(const low_ir_t *ir, int sidx, int depth) {
    if (depth > 4 || sidx < 0 || (proven_size_t)sidx >= ir->nstructs) return false;
    if (low_ir_struct_has_enumlayout(ir, (proven_size_t)sidx)) return false;
    const low_ir_struct_t *S = &ir->structs[sidx];
    if (!S->viewable || S->is_mmio || S->nf == 0 || S->nf > K_REC_MAX || S->total > K_REC_BYTES) return false;
    for (proven_size_t q = 0; q < S->nf; q++) {
        const low_ir_sfield_t *f = &S->f[q];
        if (f->arrn || f->boxed || !f->size) return false;
        if (f->sidx >= 0) { if (!cbe_rec_nested_ok(ir, f->sidx, depth + 1)) return false; continue; }
        if (f->flt) { if (f->size != 4 && f->size != 8) return false; continue; }
        if (f->size > 8) return false;
    }
    return true;
}
static int cbe_field_idx(const low_ir_t *ir, proven_u8str_view_t nm);
static void cbe_emit_rec_in(low_out_t *out, const low_ir_t *ir, int sidx, size_t br, unsigned off0, const char *val, int depth) {
    const low_ir_struct_t *S = &ir->structs[sidx];
    for (proven_size_t z = 0; z < S->nf; z++) {
        int fi = cbe_field_idx(ir, S->f[z].name);
        if (fi < 0) continue;   // 안 읽히는 칸은 전역 칸 표에 없다 — 칸은 이미 0 이다(평평한 칸과 같다)
        char v2[512];
        low_bfmt(v2, sizeof v2, "lw_field({}, {})", proven_arg_cstr(val), PROVEN_ARG((int)(fi)));
        if (S->f[z].sidx >= 0) { if (depth < 4) cbe_emit_rec_in(out, ir, S->f[z].sidx, br, off0 + S->f[z].off, v2, depth + 1); continue; }
        if (S->f[z].flt)
            low_fpf(out, "    {{ lowv fv_ = {}; if (fv_.tag != LWV_FLT) lw_panic(\"a float field\");\n"
                         "      lw_st_f(ar[{}] + {}, {}, {}, lw_f2b(lw_fval(fv_), {})); }}\n",
                    proven_arg_cstr(v2), PROVEN_ARG((size_t)(br)), PROVEN_ARG((unsigned)(off0 + (unsigned)S->f[z].off)), PROVEN_ARG((int)((int)S->f[z].size)), PROVEN_ARG((int)(S->f[z].be ? 1 : 0)), PROVEN_ARG((int)((int)S->f[z].size)));
        else
            low_fpf(out, "    lw_st_f(ar[{}] + {}, {}, {}, (unsigned long long)lw_want_int({}, \"ints (field {})\"));\n",
                    PROVEN_ARG((size_t)(br)), PROVEN_ARG((unsigned)(off0 + (unsigned)S->f[z].off)), PROVEN_ARG((int)((int)S->f[z].size)), PROVEN_ARG((int)(S->f[z].be ? 1 : 0)), proven_arg_cstr(v2),
                    low_sv((const char *)S->f[z].name.ptr, (int)S->f[z].name.size));
    }
}

// ★★ 2026-11-01 (W-20261101-1150) — **태그 값 레코드를 빠른 레코드로 푼다 / 되쓴다.** 경계 어댑터(매개변수 `a[q]`)와, 칸이 가리키는
//   액터에게 말하는 호출 자리(태그 쪽 레코드 하나)가 같은 코드를 쓴다. `val` 은 lowv 식, `buf` 는 바이트 버퍼 식, `rx` 는 평행 칸
//   배열의 이름(`rsl` · `crsl`), `q` 는 그 배열 안의 매개변수 자리다. `slots` 가 거짓이면 평행 칸이 없는 쪽이다(피호출자가 안 읽는다).
static void cbe_emit_rec_unpack(low_out_t *out, const low_ir_t *ir, int sidx, const char *val, const char *buf, const char *rx, size_t q, bool slots) {
    const low_ir_struct_t *S = &ir->structs[sidx];
    for (proven_size_t z = 0; z < S->nf; z++) {
        int fi = cbe_field_idx(ir, S->f[z].name);
        if (cbe_f_place(ir, &S->f[z])) {   // ★ 액터 칸 · 소유한 값 구조체 칸 — 태그 쪽 레코드의 자리
            if (fi >= 0 && slots) low_fpf(out, "    {}[{} * LW_RECF + {}] = lw_aref_in(lw_field({}, {}));\n", proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((size_t)((size_t)z)), proven_arg_cstr(val), PROVEN_ARG((int)(fi)));
            continue;
        }
        if (cbe_f_nopt(ir, &S->f[z]) >= 0) {   // ★ 2026-11-03 (W-20261103-1143) — 마디를 가리키는 칸: 태그 값 option → 주소(none = 0)
            if (fi >= 0)
                low_fpf(out, "    {{ lw_r o_ = lw_opt2r(lw_field({}, {})); const unsigned char *p_ = o_.tag == 1 ? o_.s.p : 0;"
                             " if (LW_UNLIKELY(o_.tag != 1 && o_.tag != 2)) lw_panic(\"a node field takes an option\"); memset({} + {}, 0, 8); memcpy({} + {}, &p_, sizeof p_); }}\n",
                        proven_arg_cstr(val), PROVEN_ARG((int)(fi)), proven_arg_cstr(buf), PROVEN_ARG((unsigned)((unsigned)S->f[z].off)), proven_arg_cstr(buf), PROVEN_ARG((unsigned)((unsigned)S->f[z].off)));
            continue;
        }
        if (S->f[z].arrn) {   // ★ 2026-11-01 (W-20261101-1828) — 배열 칸: 태그 쪽 레코드의 바이트를 빠른 레코드 안에 베낀다
            const unsigned need_ = (unsigned)S->f[z].arrn * (unsigned)S->f[z].arresz;
            if (fi >= 0)
                low_fpf(out, "    {{ lowv fv_ = lw_field({}, {}); size_t h_ = fv_.tag == LWV_SLICE ? fv_.n : fv_.tag == LWV_VARRAY ? fv_.n * (size_t)fv_.box : (size_t)-1;\n"
                             "      if (LW_UNLIKELY(h_ != {}u)) lw_panic(\"an array field takes exactly its length of elements\"); memmove({} + {}, fv_.p, {}); }}\n",
                        proven_arg_cstr(val), PROVEN_ARG((int)(fi)), PROVEN_ARG((unsigned)(need_)), proven_arg_cstr(buf), PROVEN_ARG((unsigned)((unsigned)S->f[z].off)), PROVEN_ARG((unsigned)(need_)));
            continue;
        }
        // ★★★ **슬라이스 필드는 바이트 칸이 아니라 평행 칸으로** (RFC-0106 단계 1).
        //   `lw_st_f` 로 정수처럼 넣으면 2026-07-19 의 `panic: ints` 가 돌아온다 —
        //   크기가 8 이라 스칼라와 구별되지 않기 때문이다. 여기서 갈라 넣는다.
        //   태그 쪽이 VARRAY 면 길이는 **원소 수**이므로 바이트로 되돌린다.
        if (S->f[z].boxed && S->f[z].elem) {
            if (fi >= 0 && slots)
                low_fpf(out,
                    "    {{ lowv fv_ = lw_field({}, {});\n"
                    "      if (fv_.tag == LWV_VARRAY) {{ {}[{} * LW_RECF + {}].p = fv_.p; {}[{} * LW_RECF + {}].n = fv_.n * {}; }}\n"
                    "      else if (fv_.tag == LWV_SLICE) {{ {}[{} * LW_RECF + {}].p = fv_.p; {}[{} * LW_RECF + {}].n = fv_.n; }}\n"
                    "      else {{ {}[{} * LW_RECF + {}].p = LW_UNINIT; {}[{} * LW_RECF + {}].n = 0; }} }}\n",   proven_arg_cstr(/* ★ 아직 안 세운 칸(갓 띄운 액터의 0) — 읽으면 멈춘다 */
                    val), PROVEN_ARG((int)(fi)),
                    proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((size_t)((size_t)z)), proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((size_t)((size_t)z)), PROVEN_ARG((unsigned)((unsigned)S->f[z].elem)),
                    proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((size_t)((size_t)z)), proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((size_t)((size_t)z)),
                    proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((size_t)((size_t)z)), proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((size_t)((size_t)z)));
            continue;
        }
        // ★ 안 읽히는 필드는 전역 필드표에 없다 — 그러면 경계에서도 안 읽는다(칸이 이미 0 이다).
        //   ★ 2026-11-04 — 짐을 싣는 열거의 칸은 그 갈래에만 있다: 없는 칸은 0 으로 둔다(`lw_field0`).
        if (fi >= 0)
            low_fpf(out, S->is_penum ? "    lw_st_f({} + {}, {}, {}, (unsigned long long)lw_want_int(lw_field0({}, {}), \"ints (field {})\"));\n"
                                     : "    lw_st_f({} + {}, {}, {}, (unsigned long long)lw_want_int(lw_field({}, {}), \"ints (field {})\"));\n",
                    proven_arg_cstr(buf), PROVEN_ARG((unsigned)S->f[z].off), PROVEN_ARG((int)S->f[z].size),
                    PROVEN_ARG((int)(S->f[z].be ? 1 : 0)), proven_arg_cstr(val), PROVEN_ARG((int)(fi)),
                    low_sv(S->f[z].name.ptr, (int)S->f[z].name.size));
    }
    // ★ 액터 상태 레코드의 **제 자리** — `make` 가 그 액터를 컨테이너의 칸에 담을 때 쓴다(CBE_AID).
    if (slots && S->is_actor_state && sidx < 64 && g_aid_need[sidx] && S->nf <= CBE_AID)
        low_fpf(out, "    {}[{} * LW_RECF + {}] = lw_aref_in({});\n", proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((int)(CBE_AID)), proven_arg_cstr(val));
}
static void cbe_emit_rec_writeback(low_out_t *out, const low_ir_t *ir, int sidx, const char *val, const char *buf, const char *rx, size_t q, bool slots) {
    const low_ir_struct_t *S = &ir->structs[sidx];
    for (proven_size_t z = 0; z < S->nf; z++) {
        int fi = cbe_field_idx(ir, S->f[z].name);
        if (fi < 0) continue;
        if (cbe_f_place(ir, &S->f[z])) {   // ★ 액터 칸 · 소유한 값 구조체 칸 — 가리키는 자리를 되쓴다
            if (slots) low_fpf(out, "    lw_fstore({}, {}, lw_aref_out({}[{} * LW_RECF + {}]));\n", proven_arg_cstr(val), PROVEN_ARG((int)(fi)), proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((size_t)((size_t)z)));
            continue;
        }
        // ★★★★★ **슬라이스 필드는 정수로 되쓰면 안 된다** (RFC-0106 단계 2).
        //   `lw_ld_f` 는 바이트 버퍼에서 **정수**를 읽는다 — 슬라이스 필드의 바이트 자리는 비어 있으므로 그 값은 **0** 이고,
        //   되쓰면 호출자의 슬라이스가 **지워진다**. ⇒ 평행 칸의 (포인터, 길이)를 **슬라이스 값으로** 되쓴다.
        //   ★ 원소 폭이 1 이 아니면 태그 쪽은 **원소 수**를 기대하므로 되돌린다.
        // ★★★★★ **원래 모양대로 되돌린다** (2026-08-30, WO-0155) — 원본을 복사한 뒤 **포인터와 길이만** 갈아 끼운다(WO-0156).
        // ★★★★★ **원래가 슬라이스가 아니었으면 태그를 새로 세운다** (2026-08-31, WO-0162) — 「썼는가」의 증인은 포인터다.
        if (cbe_f_nopt(ir, &S->f[z]) >= 0) {   // ★ 2026-11-03 — 마디를 가리키는 칸: 주소 → 태그 값 option(마디의 뷰 · 0 = none)
            low_fpf(out, "    lw_fstore({}, {}, lw_nopt_out({} + {}, {}));\n", proven_arg_cstr(val), PROVEN_ARG((int)(fi)), proven_arg_cstr(buf), PROVEN_ARG((unsigned)((unsigned)S->f[z].off)), PROVEN_ARG((int)(cbe_f_nopt(ir, &S->f[z]))));
            continue;
        }
        if (S->f[z].arrn) {   // ★ 2026-11-01 — 배열 칸: 빠른 레코드의 바이트를 태그 쪽 레코드의 제 바이트에 되쓴다(제자리 — `lw_fstore_arr` 와 같다)
            low_fpf(out, "    {{ lowv fv_ = lw_field({}, {}); memmove((unsigned char *)(void *)fv_.p, {} + {}, {}); }}\n",
                    proven_arg_cstr(val), PROVEN_ARG((int)(fi)), proven_arg_cstr(buf), PROVEN_ARG((unsigned)((unsigned)S->f[z].off)), PROVEN_ARG((unsigned)((unsigned)S->f[z].arrn * (unsigned)S->f[z].arresz)));
            continue;
        }
        if (S->f[z].boxed && S->f[z].elem) {
            if (!slots) continue;
            low_fpf(out,
                "    {{ lowv sv_ = lw_field({}, {});\n"
                "      if (sv_.tag != LWV_SLICE && sv_.tag != LWV_VARRAY) {{\n"
                "        if ({}[{} * LW_RECF + {}].p && {}[{} * LW_RECF + {}].p != LW_UNINIT)\n"
                "          sv_ = (lowv){{ .tag = LWV_SLICE, .p = {}[{} * LW_RECF + {}].p,\n"
                "                        .n = {}[{} * LW_RECF + {}].n }};\n"
                "      }} else {{\n"
                "        sv_.p = {}[{} * LW_RECF + {}].p;\n"
                "        if (sv_.tag == LWV_VARRAY) sv_.n = {}[{} * LW_RECF + {}].n / {};\n"
                "        else sv_.n = {}[{} * LW_RECF + {}].n;\n"
                "      }}\n"
                "      lw_fstore({}, {}, sv_); }}\n",
                proven_arg_cstr(val), PROVEN_ARG((int)(fi)),
                proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((size_t)((size_t)z)), proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((size_t)((size_t)z)),
                proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((size_t)((size_t)z)),
                proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((size_t)((size_t)z)),
                proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((size_t)((size_t)z)),
                proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((size_t)((size_t)z)), PROVEN_ARG((unsigned)((unsigned)S->f[z].elem)),
                proven_arg_cstr(rx), PROVEN_ARG((size_t)(q)), PROVEN_ARG((size_t)((size_t)z)),
                proven_arg_cstr(val), PROVEN_ARG((int)(fi)));
            continue;
        }
        if (low_sfield_sgn(&S->f[z]) && S->f[z].size < 8)   // ★ 2026-10-31 — 부호 있는 칸은 부호를 늘린다
            low_fpf(out, "    lw_fstore({}, {}, lw_int((long long)(lw_ld_f({} + {}, {}, {}) << {}) >> {}));\n",
                    proven_arg_cstr(val), PROVEN_ARG((int)(fi)), proven_arg_cstr(buf), PROVEN_ARG((unsigned)((unsigned)S->f[z].off)),
                    PROVEN_ARG((int)((int)S->f[z].size)), PROVEN_ARG((int)(S->f[z].be ? 1 : 0)), PROVEN_ARG((int)(64 - 8 * (int)S->f[z].size)), PROVEN_ARG((int)(64 - 8 * (int)S->f[z].size)));
        else
            low_fpf(out, "    lw_fstore({}, {}, lw_int((long long)lw_ld_f({} + {}, {}, {})));\n",
                    proven_arg_cstr(val), PROVEN_ARG((int)(fi)), proven_arg_cstr(buf), PROVEN_ARG((unsigned)((unsigned)S->f[z].off)),
                    PROVEN_ARG((int)((int)S->f[z].size)), PROVEN_ARG((int)(S->f[z].be ? 1 : 0)));
    }
}

// ★★★★★ **왜 안 되는지를 이름 붙여 말한다** (2026-08-30, WO-0151).
//   `--why-slow` 는 이 자리의 실패를 *"not lowerable: a test, or too many locals"* 라고
//   적고 있었다 — **코퍼스에서 가장 큰 덩어리(463 op)** 가 그 문장을 달고 있었고,
//   그 문장은 **틀렸다**: 이 op 들은 test 도 아니고 지역이 많지도 않다. 진짜 이유는
//   *"struct 파라미터의 필드가 빠른 프레임이 나를 수 있는 스칼라가 아니다"* 이고,
//   대개 **슬라이스 필드**다(`pool.block_pool` 의 `mem`·`gens`).
//   ☞ 진단이 엉뚱한 것을 탓하면 읽는 사람은 **엉뚱한 자리를 고치러 간다**. 이 저장소가
//     같은 병을 이미 세 번 적었다(E-IR-UNSUP · E-GUARD-FALLTHROUGH · R-FEWER-PARAMS).
static const char *cbe_rec_why(const low_ir_t *ir, int sidx) {
    if (sidx < 0 || (proven_size_t)sidx >= ir->nstructs) return "the struct is not in this unit";
    const low_ir_struct_t *S = &ir->structs[sidx];
    if (S->nf == 0) return "the struct has no fields";
    if (S->nf > K_REC_MAX) return "the struct has more fields than the fast frame carries";
    if (S->total > K_REC_BYTES) return "the struct is larger than the fast frame carries";
    if (!S->viewable) {
        // ★ 슬라이스 필드면 **폭까지 말한다** — RFC-0106 이 옮길 표현이 그 폭이다.
        for (proven_size_t q = 0; q < S->nf; q++)
            if (S->f[q].boxed && S->f[q].elem)
                return "a SLICE field (the fast frame carries scalars; RFC-0106 would carry the slice too)";
        return "a field is not a sized scalar (a SLICE field, most often) — the fast frame carries scalars";
    }
    for (proven_size_t q = 0; q < S->nf; q++) {
        if (S->f[q].sidx >= 0) return "a field is itself a struct (nested records are not carried here)";
        if (S->f[q].flt) return "a float field (floats are carried only by value)";
    }
    return "the struct parameter is not one the fast path carries";
}

// ★★★ **중첩 레코드는 평탄화한다** — 중첩 구조체 필드의 잎(leaf) 스칼라들을 **연속 슬롯**에
//   편다. `outerr{q pairr, tag}` → 슬롯 [q.a, q.b, tag]. 잎 수·필드의 평탄 오프셋을 센다.
static int cbe_leaves(const low_ir_t *ir, int sidx) {
    if (sidx < 0 || (proven_size_t)sidx >= ir->nstructs) return -1;
    const low_ir_struct_t *S = &ir->structs[sidx];
    int n = 0;
    for (proven_size_t q = 0; q < S->nf; q++) {
        if (S->f[q].sidx >= 0) { int sub = cbe_leaves(ir, (int)S->f[q].sidx); if (sub < 0) return -1; n += sub; }
        else n++;
    }
    return n;
}
// 필드 z 의 평탄 슬롯 오프셋(앞선 필드들의 잎 수 합).
static int cbe_flat_off(const low_ir_t *ir, int sidx, int z) {
    const low_ir_struct_t *S = &ir->structs[sidx];
    int off = 0;
    for (int q = 0; q < z; q++) {
        if (S->f[q].sidx >= 0) off += cbe_leaves(ir, (int)S->f[q].sidx);
        else off++;
    }
    return off;
}
// 레코드로 내릴 수 있나 — 중첩 구조체 필드를 **평탄화**로 허용한다(잎이 전부 스칼라 정수, 잎 ≤ 8).
static bool cbe_rec_nest_ok(const low_ir_t *ir, int sidx) {
    if (sidx < 0 || (proven_size_t)sidx >= ir->nstructs) return false;
    const low_ir_struct_t *S = &ir->structs[sidx];
    if (S->nf == 0) return false;
    if (S->total > K_REC_BYTES) return false;   // ★ 잎을 세지 않는다 — 칸에 들어가면 된다
    for (proven_size_t q = 0; q < S->nf; q++) {
        if (S->f[q].arrn) return false;        // ★ 배열 칸 — 값 복사는 태그 경로가 한다(위 `cbe_rec_ok_f` 의 까닭)
        if (S->f[q].sidx >= 0) { if (!cbe_rec_nest_ok(ir, (int)S->f[q].sidx)) return false; }
        else if (S->f[q].flt) return false;   // 부동 중첩 잎은 아직
    }
    return true;
}

// ★★★ **뷰**는 레코드와 다르다 — rloc[8] 슬롯에 담지 않고 **바이트 오프셋으로 직접 읽는다.**
//   그러니 레코드의 두 제약이 **뷰에는 필요 없다**: (1) 8필드 상한(rloc 크기), (2) 중첩 금지.
//   9번째 필드가 "조용히 사라지던" 자리·중첩 구조체 뷰가 여기서 막혀 있었다 — 오프셋만 맞으면
//   되는데도. **부동 필드만** 거절한다(뷰 필드 읽기는 아직 정수만 낸다). 중첩 필드는 그 자체가
//   뷰 가능해야 한다(재귀) — 읽으면 **중첩 뷰**가 된다.
static bool cbe_view_ok(const low_ir_t *ir, int sidx) {
    if (low_ir_struct_has_enumlayout(ir, (proven_size_t)sidx)) return false;   // ★ RFC-0150 §8-31 — 번호 ↔ 순번은 태그 경로가 한다
    if (sidx < 0 || (proven_size_t)sidx >= ir->nstructs) return false;
    const low_ir_struct_t *S = &ir->structs[sidx];
    if (S->nf == 0 || !S->viewable) return false;
    // ★ RFC-0135 D13 — 그림자(할당기 바이트에 지은 구조체 — 슬라이스 칸은 (주소, 길이) 두 낱말)는 태그 경로만이었다.
    //   ★★ 2026-11-01 (W-20261101-1828) — 원소가 수인 슬라이스 칸만 가진 그림자는 연다: 읽기는 이미 있었고(FIELD 의 slmeta 갈래 — 번역 때 지은
    //   표), 쓰기(FSTORE)는 VM 처럼 그 두 낱말을 적는다. 스스로를 품는 칸(마디의 주소)은 그대로 태그 경로다.
    if (S->is_shadow)
        for (proven_size_t q = 0; q < S->nf; q++) {
            const low_ir_sfield_t *f = &S->f[q];
            if (f->bxs1) continue;   // ★ 2026-11-02 (W-20261102-0024 O4) — 마디의 주소 칸(none = 0): FIELD · FSTORE 가 option(뷰)로 읽고 쓴다
            if (f->bxself) return false;
            if (f->slmeta) {
                const unsigned ez_ = (unsigned)(f->slmeta & 0xff);
                if ((f->slmeta & (IR_STRUCT_BIT | IR_ROW_BIT)) || !ez_ || ez_ > 8) return false;
                if ((f->slmeta & IR_FLT_BIT) && ez_ != 4 && ez_ != 8) return false;
            } else if (f->boxed || f->arrn) return false;
        }
    for (proven_size_t q = 0; q < S->nf; q++) {
        if (S->f[q].sidx >= 0) {                              // 중첩 구조체 필드 → 재귀로 뷰 가능해야
            if (!cbe_view_ok(ir, (int)S->f[q].sidx)) return false;
        } else if (S->is_shadow && S->f[q].slmeta) continue;  // ★ 그림자의 슬라이스 칸 — (주소, 길이) 16 바이트(위에서 검증)
        else if (S->is_shadow && S->f[q].bxs1) continue;    // ★ 그림자의 주소 칸(2026-11-02)
        else if (S->f[q].arrn) { if (!cbe_f_arr(&S->f[q])) return false; continue; }   // ★ 2026-11-01 — 배열 칸: 그 바이트를 보는 슬라이스
        else if (S->f[q].size > 8) return false;            // 스칼라 잎은 long long 에 담겨야
        // 부동 잎은 오프셋으로 읽어 lw_b2f 로 f64 로 본다(아래 FIELD 방출).
    }
    return true;
}

// ★★★ 2026-11-02 (W-20261102-0024 O4) — **마디는 늘 바이트 위에 산다.** 스스로를 품는 칸(`left option (owned node) .`)이 있는
//   구조체를 이 단위의 어느 op 도 레코드로 짓지 않으면(`make` 가 없다 — `use al. option (owned node) lit node …` 는 할당기 바이트 위의
//   그림자 뷰를 낸다), 그 구조체의 값은 실행 중 언제나 그 그림자의 뷰다(VM 의 VMV_VIEW). 그러면 매개변수 · `option (owned …)` 매개변수 ·
//   반환을 뷰(K_VIEW + 그림자)로 나른다 — 바이트를 가리키는 주소 하나다. 레코드로 짓는 자리가 하나라도 있으면 값이 레코드일 수도
//   있으므로 −1(태그 경로) — 매개변수의 종류는 하나여야 한다. 그림자 번호를 돌려준다.
static int cbe_view_slot(const low_ir_t *ir, int sidx, proven_i64 fi);
static int cbe_nodeview(const low_ir_t *ir, int sidx) {
    if (sidx < 0 || (proven_size_t)sidx >= ir->nstructs) return -1;
    const low_ir_struct_t *S = &ir->structs[sidx];
    if (S->is_shadow || !S->ashadow1) return -1;
    bool self_ = false;
    for (proven_size_t q = 0; q < S->nf; q++) if (S->f[q].bxself) self_ = true;
    if (!self_) return -1;
    for (proven_size_t i = 0; i < ir->ndefs; i++)
        for (proven_size_t j = 0; j < ir->defs[i].ncode; j++)
            if (ir->defs[i].code[j].w == IRW_MAKE && cbe_make_sidx(ir, ir->defs[i].code[j].a) == sidx) return -1;
    const int sh = (int)S->ashadow1 - 1;
    if (sh < 0 || K_VIEW + sh >= K_SVIEW || !cbe_view_ok(ir, sh)) return -1;
    return sh;
}
// 매개변수의 뷰 종류를 심는다 — 종류 실행과 방출이 같은 규칙을 본다(cbe_local_kinds 뒤에 부른다).
static void cbe_nodeview_seed(const low_ir_t *ir, const low_ir_def_t *d, unsigned char *lk, unsigned char *lez, unsigned char *lfl, proven_size_t n) {
    for (proven_size_t q = 0; q < d->nparams && q < n && q < LOW_MAX_PARAMS; q++) {
        if ((d->param_struct >> q) & 1u) {
            const int sh = cbe_nodeview(ir, (int)d->param_sidx[q]);
            if (sh >= 0) lk[q] = (unsigned char)(K_VIEW + sh);
        } else if (((d->param_opt >> q) & 1u) && d->param_optsx1[q]) {
            const int sh = cbe_nodeview(ir, (int)d->param_optsx1[q] - 1);
            if (sh >= 0 && sh < 254) { lk[q] = K_OPT; lez[q] = (unsigned char)(sh + 1); lfl[q] = 1; }
        }
    }
}
static int cbe_pnode(const low_ir_t *ir, const low_ir_def_t *d, proven_size_t q) {   // 이 매개변수가 마디의 뷰인가(그림자 번호 · 아니면 −1)
    if (q >= LOW_MAX_PARAMS || !((d->param_struct >> q) & 1u)) return -1;
    return cbe_nodeview(ir, (int)d->param_sidx[q]);
}
static int cbe_pnodeopt(const low_ir_t *ir, const low_ir_def_t *d, proven_size_t q) {   // `option (owned 마디)` 매개변수인가
    if (q >= LOW_MAX_PARAMS || !((d->param_opt >> q) & 1u) || !d->param_optsx1[q]) return -1;
    return cbe_nodeview(ir, (int)d->param_optsx1[q] - 1);
}

// 뷰 필드 조회 — cbe_rec_slot 과 같되 **8필드 상한이 없다**(뷰는 오프셋으로 읽으니까).
static int cbe_view_slot(const low_ir_t *ir, int sidx, proven_i64 fi) {
    if (sidx < 0 || (proven_size_t)sidx >= ir->nstructs) return -1;
    if (fi < 0 || (proven_size_t)fi >= ir->nfields) return -1;
    const low_ir_struct_t *S = &ir->structs[sidx];
    for (proven_size_t q = 0; q < S->nf; q++)
        if (proven_u8str_view_eq(S->f[q].name, ir->fields[fi])) return (int)q;
    return -1;
}

// ★★★ **이름으로 바로 슬롯을 찾는다.** 전역 필드표를 거치면, **한 번도 읽히지 않은 필드**는
//   그 표에 없어서 -1 이 나온다 — 그리고 그것이 그대로 배열 인덱스가 됐다(`rs[rsp][-1]`).
//   **읽히지 않는 필드는 흔하다**(만들기만 하고 encode 로 내보내는 레지스터 블록이 그렇다).
static int cbe_slot_by_name(const low_ir_t *ir, int sidx, proven_u8str_view_t nm) {
    if (sidx < 0 || (proven_size_t)sidx >= ir->nstructs) return -1;
    const low_ir_struct_t *S = &ir->structs[sidx];
    for (proven_size_t q = 0; q < S->nf && q < K_REC_MAX; q++)
        if (proven_u8str_view_eq(S->f[q].name, nm)) return (int)q;
    return -1;
}

// 필드 이름 → 전역 필드표의 인덱스(어댑터가 `lw_field` 로 경계에서 한 번 읽는다)
static int cbe_field_idx(const low_ir_t *ir, proven_u8str_view_t nm) {
    for (proven_size_t i = 0; i < ir->nfields; i++)
        if (proven_u8str_view_eq(ir->fields[i], nm)) return (int)i;
    return -1;
}

// 빠른 경로가 아는 낱말인가 (종류는 아래 시뮬레이터가 본다)
static bool cbe_fast_word(const low_ir_ins_t *in) {
    switch (in->w) {
        case IRW_MK128:                                     // ★ W-20261105-2304 — 128 비트 정수(cbe_w128_step)
        case IRW_CONST: case IRW_LOAD: case IRW_STORE:
        case IRW_RET:   case IRW_BR:   case IRW_BRZ:   case IRW_SWITCH:   // ★ MM9-B2 점프 테이블
        case IRW_EQ: case IRW_NE: case IRW_LT: case IRW_LE: case IRW_GT: case IRW_GE:
        case IRW_AND: case IRW_OR: case IRW_NOT: case IRW_SELECT:
        case IRW_NEG: case IRW_DIV: case IRW_MOD:
        case IRW_LEN: case IRW_INDEX: case IRW_ISTORE:      // ★ 슬라이스
        case IRW_VARRAY:                                    // ★ 타입 슬라이스 정규화
        case IRW_ASSERT:                                    // ★ **계약 검사도 그냥 정수 검사다**
        // ★★★ **내용 계약도 빠른 경로에 온다** (2026-09-09, RFC-0111 §8-11).
        //   `requires elem_le s N` 은 표면도 있고 구간 분석도 읽는데(`iv_apply_elem_requires`)
        //   **방출이 없어서** 붙이는 순간 op 이 통째로 태그 경로로 떨어졌다 — 즉 «값이 나오라고
        //   적는 계약» 이 **op 을 해석기로 보내는** 계약이었다. 편집 거리의 병목(표에서 읽은
        //   값의 넘침 검사)을 지울 수 있는 **유일한 표면**이 그 길이었다.
        //   ☞ *분석이 읽는 절은 방출도 낼 수 있어야 한다 — 아니면 그 절은 함정이다.*
        case IRW_ELEMCK:                                    // ★ 내용 계약 (RFC-0111 §8-11)
        case IRW_PANIC:
        case IRW_MAKE: case IRW_FIELD: case IRW_FSTORE:     // ★ 구조체 = 값
        case IRW_WRAP_SOME: case IRW_WRAP_NONE:             // ★ option·result = (태그,값) 쌍
        case IRW_WRAP_OK:   case IRW_WRAP_ERR:
        case IRW_ISSOME: case IRW_SOMEVAL:
        case IRW_ISOK:   case IRW_ISERR: case IRW_OKVAL: case IRW_ERRVAL:
        case IRW_VALOR:  case IRW_SPOP:  case IRW_HASVAL:
        case IRW_ELSE_NONE: case IRW_ELSE_ERR: case IRW_TRY:
        case IRW_BNEW: case IRW_CONTAINS: case IRW_COUNT:   // ★ 비트셋 = 64비트 마스크
        case IRW_SAMESL:                                    // ★ 같은 바이트인가 — 두 슬라이스 → 참거짓
        case IRW_AESCTR: case IRW_GHASH:                    // ★ 암호 잎 — 해시와 같은 모양(슬라이스와 수)
        case IRW_CHACHA20:                                  // ★ ChaCha20 (RFC-0122) — `aes_ctr` 와 같은 모양
        case IRW_POLY1305:                                  // ★ Poly1305 (RFC-0122) — 해시와 같은 모양
        case IRW_AESGCM:                                    // ★ GCM 한 덩이 (RFC-0124) — 슬라이스 여섯
        case IRW_CHAPOLY:                                   // ★ ChaCha20-Poly1305 한 덩이 (RFC-0128)
        case IRW_AESROUND: case IRW_AESLAST:                // ★ AES 한 라운드 (RFC-0119 §10)
        case IRW_SHA256: case IRW_SHA512: case IRW_SHA384: case IRW_CRC32:   // ★ 해시 — 태그와 **같은 함수**를 부른다
        case IRW_HASH64: case IRW_RNGNEXT: case IRW_RANDBYTES:  // ★ 해시·난수 — 같은 모양
        case IRW_VREVERSE: case IRW_VROTATE: case IRW_VSHUFFLE:  // ★ 레인 순열 — 그냥 레인 옮기기
        case IRW_VAVG:                                          // ★ 레인 평균 — (a+b+1)>>1
        case IRW_VLOADM: case IRW_VSTOREM:                      // ★ 마스크 적재·저장 — 켜진 레인만
        case IRW_TIMENOW: case IRW_TIMELOCAL: case IRW_TIMESLEEP:  // ★ 시계 — 잎 하나를 부를 뿐
        case IRW_ARGC: case IRW_ARGV:                       // ★ cap args (D2′)
        case IRW_ENVGET:                                    // ★ cap env  (D2′)
        case IRW_WRITE:                                     // ★ cap io   (A2)
        case IRW_ALLOCB:                                    // ★ cap allocator (A3)
        case IRW_RMARK: case IRW_RRESET:                    // ★ 영역 표식·되감기 — 커서 하나 (WO-0222)
        case IRW_RDIN:                                      // ★ cap io — stdin (A5)
        case IRW_FOPEN: case IRW_FREAD: case IRW_FWRITE: case IRW_FCLOSE: case IRW_FSEEK:   // ★ 스트림 리프 (§6)
        case IRW_DOPEN: case IRW_DREAD: case IRW_DCLOSE:   // ★ 디렉터리 순회 (§6)
        case IRW_FTYPE: case IRW_LTYPE:                    // ★ 파일 타입 질의 (§6) — 따라가지 않는 짝(lstat)도
        case IRW_DMAKE: case IRW_PREMOVE: case IRW_PRENAME:   // ★ 파일시스템 변경 (§6)
        case IRW_NPAIR: case IRW_NSEND: case IRW_NRECV: case IRW_NCLOSE:   // ★ 소켓 리프 (cap net)
        case IRW_NLISTEN: case IRW_NPORT: case IRW_NCONNECT: case IRW_NACCEPT:   // ★ 네트워크 면
        case IRW_NRESOLVE:                                                       // ★ 이름 해석 (X-0032)
        case IRW_TTYRAW: case IRW_TTYREAD: case IRW_TTYSIZE:                     // ★ 터미널 (cap tty · 2026-11-01)
        case IRW_PSPAWN: case IRW_PREAD: case IRW_PPOLL: case IRW_PWAIT: case IRW_PKILL:   // ★ 자식 프로세스 (cap process · RFC-0136)
        case IRW_PREFETCH:                                                       // ★ 힌트 — 답을 안 바꾼다
        case IRW_BREMOVE: case IRW_BUNION: case IRW_BINTER: case IRW_BDIFF:
        case IRW_BEMPTY: case IRW_BSUBSET: case IRW_BCOMPL:  // ★ 집합 연산 — 전부 워드 bitwise
        case IRW_SNEW: case IRW_SPUSH: case IRW_SPOP_INTO:  // ★ 스택 = 배열 + 길이
        case IRW_SUBSLICE:                                  // ★ 부분 슬라이스 = 포인터 + 길이
        case IRW_SWAP:                                      // ★ 원소 맞바꾸기 (원소 타입 무관)
        case IRW_SCOPY:                                     // ★ 내용 복사 — 두 슬라이스의 바이트(2026-10-31 · 결과 자리로 베끼는 `return r.`)
        case IRW_XCHK:                                      // ★ REQ-0036 — 두 조각의 바이트 자리 견주기
        case IRW_FCONST:                                    // ★ f64
        case IRW_BINM:                                      // ★ 수치 프렐류드 (fmod·min·max)
        case IRW_BAND: case IRW_BOR: case IRW_BXOR:         // ★ 비트 연산 (RFC-0064)
        case IRW_SHL: case IRW_SHR: case IRW_WSHL: case IRW_WSHR:
        case IRW_ROTL: case IRW_ROTR:
        case IRW_CLMULLO: case IRW_CLMULHI:                 // ★ 캐리 없는 곱셈 (RFC-0119)
        case IRW_MULHI:                                     // ★ 곱의 윗말 (RFC-0151)
        case IRW_BNOT: case IRW_POPCNT: case IRW_CLZ: case IRW_CTZ: case IRW_BSWAP:
        case IRW_ALOAD: case IRW_ASTORE: case IRW_AADD: case IRW_ASUB:   // ★ atomic (RFC-0018)
        case IRW_AAND: case IRW_AOR: case IRW_AXOR: case IRW_ASWAP: case IRW_ACAS:
        case IRW_AFENCE:
        case IRW_BITCAST:                                   // ★ 무비용 값 재해석 (SPEC-004 §190 T0)
        case IRW_REF: case IRW_MREF: case IRW_DEREF:        // ★ 참조 = 프레임 지역의 이름
        case IRW_ENCODE:                                    // ★ 레코드 → 바이트 (프레임 버퍼)
        // ★★★ **asm 문장도 빠른 경로에 남는다** (RFC-0042 D11).
        //   여기가 없으면 리셋 진입이 `wfi` 한 줄 때문에 **태그 경로로 떨어지고**, 태그 경로
        //   하나가 인터프리터를 통째로 이미지에 끌고 온다(실측: .text 5442 B · bss 1 MB —
        //   RFC-0039 §9-3 이 같은 값을 쟀다). 프리스탠딩에서 그것은 곧 **못 쓰는 프로그램**이다.
        //   ☞ 피연산자가 있는 asm 은 아직 태그 경로다 — cbe_mark_scalar 가 그렇게 **말한다**.
        case IRW_ASM:
            return true;
        // ★★★★★ **`store` 도 빠른 경로에 든다** (2026-08-17, SIMD-0001).
        //   여기 `VSTORE` 가 없어서 **벡터를 쓰는 op 은 통째로 태그 인터프리터로 떨어졌다** —
        //   실측: 네 줄짜리 커널에 `lw_s_*` 가 아예 안 생긴다. 즉 simd 벤치가 ×1.06 인 것은
        //   **읽기만 하는 커널이라서**였고, 쓰는 커널은 그 값을 하나도 못 받고 있었다.
        //   *짝이 없는 최적화는 절반만 참이다.*
        case IRW_SPLAT: case IRW_VLOAD: case IRW_VSTORE:    // ★ SIMD — 정수·부동 레인 (종류 시뮬레이터가 본다)
        case IRW_RADD: case IRW_RMUL: case IRW_RMIN: case IRW_RMAX:
            return true;
        case IRW_MANY: case IRW_MALL:                       // ★ 마스크 축약 — 정수만
            return (in->a & IR_FLT_BIT) == 0 && (in->a & 0x10000) == 0;
        case IRW_UNM:
            // sum/sum_fast(5,6) 은 **부동 배열**을 훑는다 — 종류 시뮬레이터가 부동 슬라이스인지 본다.
            return true;
        case IRW_BFILL:                                     // ★ 할당기가 준 바이트를 나열의 본으로 채운다(2026-10-31)
        case IRW_LBUF: case IRW_LBUFC:                      // ★ 틀 안 나열 자리 — 이 op 의 C 틀에 바이트 줄(2026-10-31 · W-20261031-1836)
        case IRW_STR:                                       // ★ 문자열 리터럴 = 바이트 슬라이스
        case IRW_RESBLK:                                    // ★ 링커가 준 칸(RFC-0039 §9-2)
        case IRW_MMIOBLK:                                   // ★ 기저 주소 위의 레지스터 블록(RFC-0042 §8-2)
        case IRW_VIEW:                                      // ★ 바이트 위의 무복사 구조체 뷰
        case IRW_TRYVIEW:                                   // ★ try_view → some(view) | none
        case IRW_EXTERN:                                    // ★ extern.call — C 함수를 직접 부른다
        case IRW_CSTR2STR:                                  // ★ cstr → str (strlen 스캔, RFC-0068 S4)
        case IRW_STR2CSTR:                                  // ★ str_buf → cstr (base 포인터, RFC-0068 S4)
        case IRW_DROP:
        case IRW_CALL:
            return true;
        // ★ 2a — 협력 floor(T0)는 칸이 작고 뜻이 따로 짜여 있어 태그 칸만 쓴다(빠른 판은 호스트 런타임에서만).
        case IRW_ASEND: case IRW_DRAIN: case IRW_SCHED:
            return !(low_ir_target()->no_heap || g_conc_t0);
        case IRW_CAST:
            // narrow_try(0x40000) 는 **option** 을 낸다 — 이제 (태그,값) 쌍으로 내린다.
            return true;
        case IRW_ADD: case IRW_SUB: case IRW_MUL:
            return true;   // chk_* 는 option 을 낸다 — ws[] 위의 (태그,값) 쌍으로 내린다
        default:
            return false;
    }
}

// ★ 이 def 의 슬라이스 파라미터들의 **원소 크기**(바이트). 0 = 슬라이스 없음 · -1 = 못 내린다.
//   ★★ **폭이 섞이면 안 내린다**: 한 지역에 두 폭의 슬라이스가 흘러들면 인덱스 계산이
//     달라지고, 그러면 **조용히 틀린 바이트**를 읽는다.
// ★ **레인 폭 → 공용체 멤버** (RFC-0040 §9). 폭이 1·2·4·8 이 아니면 벡터가 아니다 —
//   여기서 억지로 고르지 않고 `lw_v_ok` 가 애초에 빠른 경로를 거절한다.
static const char *lw_vmem(unsigned ez) {
    return ez == 1 ? "u8" : ez == 2 ? "u16" : ez == 4 ? "u32" : "u64";
}
// ★ 한 벡터가 **64바이트 칸에 들어가는가**. 레인 상한은 언어 전체와 같은 16(태그 풀 `l[16]`).
static bool lw_v_ok(unsigned ln, unsigned ez) {
    if (!ln || ln > 16) return false;
    if (ez != 1 && ez != 2 && ez != 4 && ez != 8) return false;
    return ln * ez <= 64;
}

static int cbe_def_esz(const low_ir_def_t *d) {
    int esz = 0;
    for (proven_size_t q = 0; q < d->nparams && q < LOW_MAX_PARAMS; q++) {
        if (!((d->param_slice >> q) & 1u)) continue;
        // ★ 구조체 슬라이스는 **바이트로 실려** 오고(view.array 가 원소 배열로 본다) ⇒ 폭 갈림에 안 넣는다.
        if (d->param_selem[q]) continue;
        int e;
        switch (d->param_ebits[q]) {
            case 8:  e = 1; break;
            case 16: e = 2; break;
            case 32: e = 4; break;
            case 108: e = 1; break;            // ★ 부호 있는 원소도 폭은 같다(부호는 슬롯의 fl 이 싣는다)
            case 116: e = 2; break;
            case 132: e = 4; break;
            case 64: case 164: e = 8; break;   // ★ i64 도 8 바이트 그대로(위 cbe_ebits_w)
            default: return -1;   // f32/구조체 원소… 모르면 **안 내린다**
        }
        if (esz && esz != e) return -1;
        esz = e;
    }
    return esz;
}
// ★ base 필터용: 슬라이스 파라미터의 폭이 **전부 알려졌나**(혼합은 OK — byte-carry 로 슬롯마다
//   따라간다). cbe_def_esz 는 "한 폭" 을 요구하지만, 그 균일성 요구는 byte-carry 이후 낡았다.
static bool cbe_slices_known(const low_ir_def_t *d) {
    for (proven_size_t q = 0; q < d->nparams && q < LOW_MAX_PARAMS; q++) {
        if (!((d->param_slice >> q) & 1u)) continue;
        if (d->param_selem[q]) continue;   // 구조체 슬라이스는 바이트로 실린다
        if (((d->param_sflt >> q) & 1u) && cbe_pslice_w(d, q)) continue;   // ★ 2026-11-01 — f32 · f64 원소
        if (d->param_rowb[q] && !g_no_elemsl) continue;                      // ★ 2026-11-01 — 줄의 나열(K_ROWS)
        switch (d->param_ebits[q]) {
            case 8: case 16: case 32: case 64: case 108: case 116: case 132: case 164: break;
            default: return false;   // f32/f64/모르는 원소는 거절
        }
    }
    return true;
}

// ★★ 2026-11-01 (W-20261101-1828) — **배열 칸 레코드를 매개변수로 받는 op** 은 그 칸의 슬라이스를 op 밖으로 못 내보내야 한다.
//   빠른 경로의 매개변수 레코드는 부른 쪽의 바이트를 가리킨다 — 부른 쪽이 넘긴 것이 스택 임시(`first mk a.`)면 그 바이트는 다음 푸시가
//   덮는다. VM 은 레코드가 풀에 살아서 괜찮다. 검사층은 이 길을 거절하지 않으므로(`return field p. body .` 는 적합하다) 빠른 경로가
//   스스로 막는다: 몸의 낱말이 아래 목록 안에 있어야 하고(슬라이스를 담을 자리 · 감쌀 자리 · 보낼 자리가 없다), 부르는 op 은 수 · 수
//   구조체 · 같은 규칙의 배열 칸 레코드만 받아야 한다. 반환이 슬라이스면 종류 실행의 RET 이 거절한다.
static bool cbe_def_arrpar(const low_ir_t *ir, const low_ir_def_t *d) {
    for (proven_size_t q = 0; q < d->nparams && q < LOW_MAX_PARAMS; q++)
        if (((d->param_struct >> q) & 1u) && cbe_rec_arr_ok(ir, (int)d->param_sidx[q])) return true;
    return false;
}
static bool cbe_arrpar_body_ok(const low_ir_t *ir, const low_ir_def_t *d, char *why, size_t wn) {
    for (proven_size_t k = 0; k < d->ncode; k++) {
        const low_ir_ins_t *in = &d->code[k];
        switch (in->w) {
            case IRW_CONST: case IRW_FCONST: case IRW_STR: case IRW_LOAD: case IRW_STORE: case IRW_DROP:
            case IRW_FIELD: case IRW_FSTORE: case IRW_INDEX: case IRW_ISTORE: case IRW_LEN: case IRW_SUBSLICE: case IRW_SCOPY: case IRW_XCHK:
            case IRW_LBUF: case IRW_VARRAY: case IRW_ELEMCK:
            case IRW_ADD: case IRW_SUB: case IRW_MUL: case IRW_DIV: case IRW_MOD: case IRW_NEG:
            case IRW_BAND: case IRW_BOR: case IRW_BXOR: case IRW_BNOT: case IRW_SHL: case IRW_SHR: case IRW_ROTL: case IRW_ROTR:
            case IRW_CLZ: case IRW_CTZ: case IRW_POPCNT: case IRW_BSWAP:
            case IRW_EQ: case IRW_NE: case IRW_LT: case IRW_LE: case IRW_GT: case IRW_GE:
            case IRW_AND: case IRW_OR: case IRW_NOT: case IRW_SELECT: case IRW_CAST: case IRW_BITCAST:
            case IRW_BR: case IRW_BRZ: case IRW_SWITCH: case IRW_RET: case IRW_ASSERT: case IRW_PANIC:
                break;
            case IRW_MAKE: {   // 슬라이스 칸 · 자리 칸이 없는 구조체만 — 칸 슬라이스를 담을 데가 없다
                const int sx = cbe_make_sidx(ir, in->a);
                if (sx < 0) return false;
                for (proven_size_t z = 0; z < ir->structs[sx].nf; z++) {
                    const low_ir_sfield_t *f = &ir->structs[sx].f[z];
                    if ((f->boxed && !f->arrn) || cbe_f_place(ir, f)) { low_bfmt(why, wn, "`make` of a struct with a slice field"); return false; }
                }
                break;
            }
            case IRW_CALL: {
                const proven_size_t c = IR_CALL_IDX(in->a);
                if (c >= ir->ndefs) return false;
                const low_ir_def_t *ce = &ir->defs[c];
                for (proven_size_t q = 0; q < ce->nparams && q < LOW_MAX_PARAMS; q++) {
                    if ((ce->param_slice >> q) & 1u) { low_bfmt(why, wn, "a call that takes a slice"); return false; }
                    if (((ce->param_struct >> q) & 1u) && !cbe_rec_ok(ir, (int)ce->param_sidx[q]) && !cbe_rec_arr_ok(ir, (int)ce->param_sidx[q])) {
                        low_bfmt(why, wn, "a call that takes a struct which could hold a slice"); return false; }
                }
                break;
            }
            default:
                low_bfmt(why, wn, "the word `{}`", proven_arg_cstr(low_irw_name((low_irw_t)in->w)));
                return false;
        }
    }
    return true;
}

// ★ 2026-11-01 — 이 op 이 `panic` 문장(lw_upanic — 슈퍼바이저가 잡는 그것)에 닿을 수 있나: 몸에 있거나 부르는 op 에 있거나(고정점).
//   유닛마다 `cbe_mark_upanic` 이 다시 센다(`cbe_mark_pmut` 과 같은 자리 — 한 처리 안에서 다음 유닛이 앞 유닛의 표를 읽지 않게).
static bool g_upanic[512];
static void cbe_mark_upanic(const low_ir_t *ir) {
    for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++) {
        g_upanic[i] = false;
        for (proven_size_t k = 0; k < ir->defs[i].ncode; k++) {
            const low_ir_ins_t *in = &ir->defs[i].code[k];
            if (in->w == IRW_PANIC && in->a != IR_PANIC_SINKFULL && in->a != IR_PANIC_ENUMCAST && in->a != IR_PANIC_NODEKIDS) { g_upanic[i] = true; break; }
        }
    }
    for (bool ch = true; ch; ) {
        ch = false;
        for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++) {
            if (g_upanic[i]) continue;
            for (proven_size_t k = 0; k < ir->defs[i].ncode; k++)
                if (ir->defs[i].code[k].w == IRW_CALL) {
                    const proven_size_t c = IR_CALL_IDX(ir->defs[i].code[k].a);
                    if (c < ir->ndefs && c < 512 && g_upanic[c]) { g_upanic[i] = true; ch = true; break; }
                }
        }
    }
}
static bool cbe_may_upanic(const low_ir_t *ir, proven_size_t di) { (void)ir; return di >= 512 || g_upanic[di]; }
static void cbe_mark_mdel(const low_ir_t *ir) {
    for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++) {
        g_mdel[i] = false; g_qthunk[i] = false;
        for (proven_size_t k = 0; k < ir->defs[i].ncode; k++)
            if (ir->defs[i].code[k].w == IRW_SCHED || ir->defs[i].code[k].w == IRW_DRAIN) { g_mdel[i] = true; break; }
    }
    for (bool ch = true; ch; ) {
        ch = false;
        for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++) {
            if (g_mdel[i]) continue;
            for (proven_size_t k = 0; k < ir->defs[i].ncode; k++)
                if (ir->defs[i].code[k].w == IRW_CALL) {
                    const proven_size_t c = IR_CALL_IDX(ir->defs[i].code[k].a);
                    if (c >= 512 || (c < ir->ndefs && g_mdel[c])) { g_mdel[i] = true; ch = true; break; }
                }
        }
    }
}
// ★ 2a — 빠른 `spawn send` 의 처리 코드가 될 수 있는가. 안 되면 까닭을 why 에 적는다.
static bool cbe_fa_hok(const low_ir_t *ir, proven_size_t c, const bool *sc, char *why, size_t wn) {
    if (c >= ir->ndefs || c >= 512) { low_bfmt(why, wn, "a `spawn send` handler outside the first 512 ops"); return false; }
    const low_ir_def_t *h = &ir->defs[c];
    if (!h->is_actor || h->nparams < 1 || !(h->param_struct & 1u) || (proven_size_t)h->param_sidx[0] >= ir->nstructs) {
        low_bfmt(why, wn, "`spawn send` to something that is not an actor handler"); return false; }
    if (sc && !sc[c]) { low_bfmt(why, wn, "`spawn send` to a handler that is on the tagged path itself"); return false; }
    if (cbe_struct_has_sl(ir, (int)h->param_sidx[0])) { low_bfmt(why, wn, "`spawn send` to an actor whose state has a slice field"); return false; }
    if (cbe_restart_max(ir, h) >= 0) { low_bfmt(why, wn, "`spawn send` to a `failure restart` actor (restart lives on the tagged path)"); return false; }
    if (cbe_may_suspend(ir, c, 0)) { low_bfmt(why, wn, "`spawn send` to a handler that may stop (yield · channel · await · I/O · drain)"); return false; }
    for (proven_size_t q = 1; q < h->nparams && q < LOW_MAX_PARAMS; q++) {
        const unsigned long long b = 1ull << q;
        if ((h->param_slice | h->param_struct | h->param_bset | h->param_flt | h->param_opt | h->param_vec | h->param_w128) & b) {
            low_bfmt(why, wn, "`spawn send` with an argument that is not an integer (argument {})", PROVEN_ARG((int)((int)q))); return false; }
        if (h->prng[q].has_rng) { low_bfmt(why, wn, "`spawn send` to a handler with a declared argument range"); return false; }
    }
    return true;
}

static void cbe_mark_scalar(const low_ir_t *ir, bool *sc) {
    // ★ 이유표는 **여기서** 비운다. 아래(종류 고정점)에서 비우면 **낱말 실패 이유가 지워진다**
    //   — 그리고 진단이 "느린 op 0개" 라고 **거짓말**을 한다. (그렇게 거짓말했다.)
    for (proven_size_t i = 0; i < 512; i++) g_why[i][0] = 0;
    if (g_no_fast) { for (proven_size_t i = 0; i < ir->ndefs; i++) sc[i] = false; return; }
    for (proven_size_t i = 0; i < ir->ndefs; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        // ★ `parallel` op 도 **보통의 op** 이다 — 청크마다 `lw_op_`(어댑터)를 부르므로,
        //   본체를 내리면 **워커가 그대로 빨라진다.** 병렬 래퍼는 그대로 위에 얹힌다.
        // ★ extern 도 **합성 본문**(requires 검사 + extern.call)을 가지므로 하강한다 —
        //   그러면 그것을 부르는 op 이 계약째로 빠른 경로에 남는다(계약이 문을 지킨다).
        // ★★★ **크기는 이제 조건이 아니다** (X-0028 · WO-0205). 남은 기준은 하나다:
        //   *하강했는가(모든 값의 종류를 방출 시점에 못 박았는가)* — 그리고 러너 것이 아닌가.
        //   파라미터 한도는 언어가 이미 `E-IR-ARITY` 로 거절하므로 여기 다시 적지 않는다.
        sc[i] = d->lowered && !d->is_test;
        if (!sc[i]) {
            // ★ **하강 대상이 아닌 것**은 그렇게 말한다 — "못 따라간다" 가 아니라(교훈 5).
            if (i < 512 && !g_why[i][0]) {
                if (d->is_test) low_bfmt(g_why[i], sizeof g_why[0],
                    "it is a `test` block — the runner owns it, not the native lowering");
                else if (d->is_extern) low_bfmt(g_why[i], sizeof g_why[0],
                    "its body is in C (`extern`) — there is nothing of ours to lower");
                else if (!d->lowered) low_bfmt(g_why[i], sizeof g_why[0],
                    "the IR could not lower this op at all (an unsupported construct upstream)");
                else low_bfmt(g_why[i], sizeof g_why[0],
                    "too many params/locals/instructions for the fast frame");
            }
            continue;
        }
        // ★★ 2026-11-01 (W-20261101-1828) — `failure restart` 액터의 핸들러가 `panic` 에 닿을 수 있으면 태그 경로에 둔다. 슈퍼바이저의 경계
        //   (`lw_restart_call` 의 setjmp)는 태그 경로의 `send` 만 세운다 — 빠른 `send` 는 핸들러를 바로 불러 `panic` 이 프로세스를 끝냈다
        //   (vm_restart 의 `recover` — VM 42). 전에는 `panic` 뒤의 `drop` 이 그런 op 을 모두 태그 경로에 두었다(우연히 맞았다).
        if (d->is_actor && d->nparams >= 1 && ((d->param_struct) & 1u) && (proven_size_t)d->param_sidx[0] < ir->nstructs &&
            (ir->structs[d->param_sidx[0]].failure_policy == 1 || ir->structs[d->param_sidx[0]].failure_policy == 3) &&
            cbe_may_upanic(ir, i)) {
            sc[i] = false;
            if (i < 512 && !g_why[i][0])
                low_bfmt(g_why[i], sizeof g_why[0], "a handler of a `failure restart` actor that can reach `panic` (the supervisor's restart lives on the tagged path)");
            continue;
        }
        // ★★ 2a — 메시지를 배달할 수 있는 op(`schedule` · `drain` · 그것을 부르는 op)이 액터를 **사본으로** 들면(처리 코드 · 액터 매개변수:
        //   태그 쪽에서 들어올 때 바이트로 풀린다) 배달된 메시지가 레코드를 바꾸고 사본이 돌아갈 때 그것을 덮는다. 그런 op 은 태그 경로에 둔다.
        if (i < 512 && g_mdel[i]) {
            bool acopy = d->is_actor;
            for (proven_size_t q = 0; q < d->nparams && q < LOW_MAX_PARAMS && !acopy; q++)
                if (((d->param_struct >> q) & 1u) && (proven_size_t)d->param_sidx[q] < ir->nstructs && ir->structs[d->param_sidx[q]].is_actor_state) acopy = true;
            if (acopy) {
                sc[i] = false;
                if (!g_why[i][0])
                    low_bfmt(g_why[i], sizeof g_why[0], "it holds an actor as a copy (a handler or an actor parameter) and may deliver messages (`schedule` · `drain`) — the copy would overwrite what they did");
                continue;
            }
        }
        // ★★★ 2026-11-11 (W-20261111-0940 O7) — **넘겨받은 액터에게 기다리는 `send`** 를 하는 op 은 동시성을 쓰는 단위에서 태그 경로에 둔다.
        //   빠른 경로는 넘겨받은 액터를 바이트 사본으로 들고 끝에서 되쓴다 — 그 액터를 다른 흐름이 쥐고 멈춰 있으면 기다려야 하는데(정본 §10.5 (1b))
        //   사본을 든 채로는 기다릴 자리가 없고, 되쓰기가 멈춘 메시지의 일을 덮는다. 태그 경로의 부름이 기다린다(LW_SEND_ENTER).
        //   자기 인스턴스(처리 코드의 첫 매개변수)와 이 op 이 띄운 액터는 그대로 빠른 경로다 — 그것은 이 사슬이 쥔 것이다.
        if (cbe_uses_conc(ir)) {
            bool aparam_ = false, asend_ = false;
            for (proven_size_t q = d->is_actor ? 1 : 0; q < d->nparams && q < LOW_MAX_PARAMS && !aparam_; q++)
                if (((d->param_struct >> q) & 1u) && (proven_size_t)d->param_sidx[q] < ir->nstructs && ir->structs[d->param_sidx[q]].is_actor_state) aparam_ = true;
            // ★★ 2026-11-11 (W-20261111-2031 D2) — 기다릴 일이 **생길 수 있는** 액터에게 보낼 때만이다. 액터를 다른 흐름이 쥔 채 멈춰 있으려면
            //   그 흐름이 그 액터의 처리 코드 안에서 멈춰야 한다(협조적 스케줄 — 멈추지 않는 처리 코드는 끝까지 돈다 · 우편함을 만지는 태스크는
            //   진짜 스레드로 안 간다, cbe_needs_peer). 그 액터 타입의 처리 코드가 하나도 멈출 수 없으면(cbe_may_suspend) 기다리는 `send` 는
            //   기다리지 않는다 — 빠른 경로의 사본이 맞다. 전엔 이 op 을 모두 태그 경로에 두어 300 만 번 보내기가 3 → 106 ms 였다.
            for (proven_size_t k = 0; k < d->ncode && aparam_ && !asend_; k++)
                if (d->code[k].w == IRW_CALL && !(d->code[k].a & IR_CALL_TRY) && IR_CALL_IDX(d->code[k].a) < ir->ndefs &&
                    ir->defs[IR_CALL_IDX(d->code[k].a)].is_actor && cbe_actor_holdable(ir, IR_CALL_IDX(d->code[k].a))) asend_ = true;
            if (aparam_ && asend_) {
                sc[i] = false;
                if (i < 512 && !g_why[i][0])
                    low_bfmt(g_why[i], sizeof g_why[0], "a waiting `send` to an actor it was given — in a unit with tasks it may have to wait for "
                             "another flow that holds that actor, and the fast path holds the actor as a copy");
                continue;
            }
        }
        // ★ 구조체 파라미터는 **필드가 전부 스칼라 정수**일 때만 내린다.
        int badq = -1;
        for (proven_size_t q = 0; q < d->nparams && q < LOW_MAX_PARAMS; q++)
            if ((d->param_struct >> q) & 1u && cbe_pnode(ir, d, q) < 0)
                if (!cbe_rec_slice_ok(ir, (int)d->param_sidx[q]) &&
                    !(!cbe_pmut(d) && cbe_rec_nested_ok(ir, (int)d->param_sidx[q], 0)) &&
                    !cbe_rec_arr_ok(ir, (int)d->param_sidx[q])) { sc[i] = false; badq = (int)q; break; }
        for (proven_size_t q = 0; q < d->nparams && q < LOW_MAX_PARAMS && sc[i]; q++)   // ★ 2026-11-04 — 짐을 싣는 열거 매개변수: 되쓰기(액터)는 태그 경로가
            if ((d->param_struct >> q) & 1u && d->param_sidx[q] < ir->nstructs && ir->structs[d->param_sidx[q]].is_penum &&
                cbe_pmut(d)) { sc[i] = false; badq = (int)q; }
        if (sc[i] && cbe_def_arrpar(ir, d)) {   // ★ 2026-11-01 — 배열 칸 레코드 매개변수: 칸 슬라이스가 op 밖으로 못 나가는 몸만
            char w_[160] = {0};
            if (!cbe_arrpar_body_ok(ir, d, w_, sizeof w_)) {
                sc[i] = false;
                if (i < 512 && !g_why[i][0])
                    low_bfmt(g_why[i], sizeof g_why[0], "a parameter is a struct with array fields and the body has {} (a field slice could outlive the caller's record)", proven_arg_cstr(w_[0] ? w_ : "a word outside the list"));
                continue;
            }
        }
        if (!sc[i]) {
            if (i < 512 && !g_why[i][0]) {
                if (badq >= 0)
                    low_bfmt(g_why[i], sizeof g_why[0], "parameter {} is a struct the fast path cannot carry: {}",
                             PROVEN_ARG((int)(badq)), proven_arg_cstr(cbe_rec_why(ir, (int)d->param_sidx[badq])));
                else
                    low_bfmt(g_why[i], sizeof g_why[0], "not lowerable: a test, or too many locals");
            }
            continue;
        }
        // ★★ 2026-11-01 (W-20261101-1150) — 같은 액터를 **풀어 든 사본**과 **칸을 거친 원본**으로 함께 쓰는 op 은 태그 경로에 둔다
        //   (cbe_mark_aref — 둘이 갈린다). 그 둘이 한 박스인 곳은 태그 경로뿐이다.
        if (i < 512 && (g_aunp[i] & g_aderef[i])) {
            sc[i] = false;
            low_bfmt(g_why[i], sizeof g_why[0], "it holds an actor unpacked (a parameter or a `spawn`) and also reaches an actor of "
                     "the same type through a container's field — the fast path would keep two copies that drift apart");
            continue;
        }
        // ★ 슬라이스의 **원소 폭**을 정적으로 알아야 한다 — 런타임 표현이 폭마다 다르다.
        if (!cbe_slices_known(d)) {
            sc[i] = false;
            if (i < 512) low_bfmt(g_why[i], sizeof g_why[0],
                                  "a slice parameter's element width is not one the fast path knows "
                                  "(f32/f64/struct elements)");
            continue;
        }
        // ★★ `view.array` 는 **바이트 길이 → 원소 수** 변환이다(`slice u32` 의 진입에 나온다).
        //   구조체·부동 원소는 빠른 경로가 아니다 — 그것들은 값이 정수가 아니다.
        // ★ 폭은 이제 **슬롯마다** 따라간다 — def 하나에 폭 하나를 강요하지 않는다.
        for (proven_size_t k = 0; k < d->ncode; k++) {
            if (d->code[k].w == IRW_CALL && (d->code[k].a & IR_CALL_TRY)) {   // ★ 2026-11-11 (O7) — `try send` 는 태그 경로에서만
                sc[i] = false;
                if (i < 512) low_bfmt(g_why[i], sizeof g_why[0], "`try send` is not lowered yet");
                break;
            }
            if (!cbe_fast_word(&d->code[k])) {
                sc[i] = false;
                if (i < 512) low_bfmt(g_why[i], sizeof g_why[0],
                                      "the word `{}` is not lowered yet", proven_arg_cstr(low_irw_name(d->code[k].w)));
                break;
            }
            // ★ RFC-0151 — 128 비트 정수는 태그 경로에만 살았다(빠른 길의 칸은 `long long` 하나였다).
            //   ★★ W-20261105-2304 (소유자 «128비트도 하기») — 이제 지역 · 셈 · 입력 · 결과 · 빠른 op 사이의 부름은 빠른 경로다(K_W128).
            //   남는 것은 칸에 든 128 비트 값(구조체 칸 · 배열 · 슬라이스 원소 · 스택 원소)과 값으로 «넘침» 을 알리는 `chk_*` · `narrow_try`
            //   — 종류 실행(cbe_w128_step)이 까닭을 말한다.
            if (cbe_w128_place(ir, d)) {
                sc[i] = false;
                if (i < 512) low_bfmt(g_why[i], sizeof g_why[0], "a 128-bit value in a struct field, array, slice or stack element stays on the tagged path");
                break;
            }
            // ★ 피연산자가 있는 asm 은 값을 **태그 프레임의 지역**에서 읽는다 — 빠른 프레임엔
            //   그 자리가 없다. 못 한다고 **말한다**(교훈 5: "못 따라간다" 가 아니라 이유를 댄다).
            if (d->code[k].w == IRW_ASM && (proven_size_t)d->code[k].a < ir->nasms &&
                ir->asms[d->code[k].a].nops > 0) {
                sc[i] = false;
                if (i < 512) low_bfmt(g_why[i], sizeof g_why[0],
                                      "an `asm` block with operands stays on the tagged path — its "
                                      "inputs and outputs live in the tagged frame");
                break;
            }
        }
    }
    for (bool ch = true; ch; ) {   // 호출을 타고 전파(고정점): 빠른 것만 빠른 것을 부른다
        ch = false;
        for (proven_size_t i = 0; i < ir->ndefs; i++) {
            if (!sc[i]) continue;
            const low_ir_def_t *d = &ir->defs[i];
            for (proven_size_t k = 0; k < d->ncode; k++) {
                if (d->code[k].w != IRW_CALL) continue;
                proven_size_t c = IR_CALL_IDX(d->code[k].a);
                if (c >= ir->ndefs || !sc[c]) { sc[i] = false; ch = true; break; }
            }
        }
    }
    // ★★ 마지막으로 **종류 시뮬레이션**. 반환 종류(g_retk)는 **피호출자를 먼저 봐야** 알므로
    //   **여러 패스**를 돈다: 패스마다 낱말 필터 결과에서 다시 시작하고, 알게 된 반환 종류가
    //   다음 패스의 호출자를 통과시킨다. (한 번만 돌면 호출 순서에 따라 **잘못 거절**한다.)
    static bool base[512];
    for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++) base[i] = sc[i];
    for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++) g_retk[i] = 0xff;
    // ★★★ **먼저 반환 종류(g_retk)를 안정될 때까지 예열한다.**
    //   g_retk 는 **위로**(피호출자 → 호출자) 낙관적으로 채워지고, 아래의 base 축소는
    //   **아래로**(실패 → 영구 거절) 좁힌다. 예열 없이 둘을 섞으면 — 피호출자 인덱스가
    //   호출자보다 **높을 때**(전방 참조) 호출자가 먼저 평가돼 "반환 종류 모름" 으로
    //   실패하고 **영구 거절**된다. 다음 패스에 g_retk 가 채워져도 되살아나지 못한다.
    //   (제네릭 단형화가 정확히 이 모양이었다 — twice#rect 가 뒤에 온 rect.area 를 부른다.)
    for (proven_size_t warm = 0; warm <= ir->ndefs; warm++) {
        bool grew = false;
        for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++)
            if (base[i] && g_retk[i] == 0xff && cbe_kind_ok(ir, &ir->defs[i], base) && g_retk[i] != 0xff)
                grew = true;
        if (!grew) break;
    }
    for (int pass = 0; pass < 6; pass++) {
        for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++) sc[i] = base[i];
        for (bool ch = true; ch; ) {   // 호출 전파
            ch = false;
            for (proven_size_t i = 0; i < ir->ndefs; i++) {
                if (!sc[i]) continue;
                const low_ir_def_t *d = &ir->defs[i];
                for (proven_size_t k = 0; k < d->ncode; k++) {
                    if (d->code[k].w != IRW_CALL) continue;
                    proven_size_t cc = IR_CALL_IDX(d->code[k].a);
                    if (cc >= ir->ndefs || !sc[cc]) {
                        sc[i] = false; ch = true;
                        if (i < 512 && cc < ir->ndefs)
                            low_bfmt(g_why[i], sizeof g_why[0], "it calls `{}`, which is not lowered",
                                     low_sv((const char *)ir->defs[cc].name.ptr, (int)ir->defs[cc].name.size));
                        break;
                    }
                }
            }
        }
        for (proven_size_t i = 0; i < ir->ndefs; i++)
            if (sc[i] && !cbe_kind_ok(ir, &ir->defs[i], sc)) {
                sc[i] = false;
                // ★ `g_kj < 0` 이면 **명령 자리에서 멈춘 것이 아니다**(입구에서 거절).
                //   그때 `\`ret\` at -1` 이라고 적으면 없는 자리를 가리키는 거짓말이 된다.
                if (i < 512) {
                    if (g_kj < 0) low_bfmt(g_why[i], sizeof g_why[0], "{}",
                                           proven_arg_cstr(g_sub[0] ? g_sub : "the fast path refused this op before reading its code"));
                    else low_bfmt(g_why[i], sizeof g_why[0], "`{}` at {}{}{}",
                                  proven_arg_cstr(low_irw_name((low_irw_t)g_kw)), PROVEN_ARG((int)(g_kj)),
                                  proven_arg_cstr(g_sub[0] ? " — " : ""), proven_arg_cstr(g_sub[0] ? g_sub : ""));
                }
            }
        for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++)
            if (!sc[i]) base[i] = false;   // 확정 거절만 남긴다
    }
}

// 지역의 종류 — 파라미터는 선언이 말하고, var 는 첫 STORE 가 말한다.
// ★ `n` = 채울 칸 수. 전에는 **32 로 못 박혀** 있었다 — 지역 표를 필요한 만큼 잡게 된 뒤에는
//   (X-0028) 그 뒤 칸이 **안 채워진 채** 읽혔을 자리다. 채울 곳의 크기는 부르는 쪽이 안다.
static void cbe_local_kinds(const low_ir_def_t *d, unsigned char *lk, proven_size_t n) {
    for (proven_size_t i = 0; i < n; i++) lk[i] = K_INT;
    for (proven_size_t q = 0; q < d->nparams && q < n; q++) {
        if ((d->param_slice >> q) & 1u)
            lk[q] = (!g_no_elemsl && d->param_selem[q]) ? (unsigned char)(K_SVIEW + d->param_selem[q] - 1)
                  : (!g_no_elemsl && d->param_rowb[q]) ? K_ROWS : K_SL;   // ★ RFC-0109 단계 1 · 줄의 나열(2026-11-01)
        else if ((d->param_struct >> q) & 1u) lk[q] = (unsigned char)(K_REC + d->param_sidx[q]);
        else if ((d->param_flt >> q) & 1u) lk[q] = K_FLT;
        else if ((d->param_w128 >> q) & 1u) lk[q] = K_W128;   // ★ W-20261105-2304
        else if ((d->param_bset >> q) & 1u) lk[q] = K_BSET;
        else if ((d->param_opt >> q) & 1u) lk[q] = d->param_optsl[q] ? K_OPTSL : K_OPT;   // ★ option·result 파라미터 · option(슬라이스)는 2026-11-01
        else if ((d->param_vec >> q) & 1u) lk[q] = K_VEC;   // ★ 벡터 파라미터
    }
}

// 코드를 **정적으로 돌려** 슬롯 종류를 정한다. 라벨에서 종류가 안 맞으면 **포기**한다.
// ★ 지역 번호가 빠른 프레임의 32 칸을 넘었다 — **까닭을 말하고** 거절한다 (X-0050, 2026-09-23).
//   전에는 그냥 `false` 였고, `--why-slow` 는 «`store.local` at 238» 이라고만 했다. 그 수는
//   IR 안의 자리이지 까닭이 아니어서, 읽는 사람이 «지역이 너무 많다» 를 스스로 알아내야 했다
//   (실측: `x25519.scalarmult` 이 그래서 태그 경로에 있는 줄 아무도 몰랐다).
// ★ 2026-10-31 (W-20261031-2053) — 32 → 64. 프레임의 칸 배열은 op 마다 필요한 만큼 잡는다(위 cbe_lbuf_ensure · 방출의 loc[n]);
//   남은 한도는 «이미 읽었나» 를 적는 64 비트 표 하나(`loaded_`)와 폭 좁히기 표(`g_fitn[64]`)다 — 둘 다 64 칸이다.
#define CBE_LOCMAX 64
static bool cbe_locals_over(unsigned idx) {
    low_bfmt(g_sub, sizeof g_sub, "local {} is past the fast frame's {} locals", PROVEN_ARG((unsigned)(idx)), PROVEN_ARG((int)(CBE_LOCMAX)));
    return false;
}
// ★ 2026-10-31 (W-20261031-2053) — `spawn actor T` 가 낮아진 꼴인가: 액터 상태를 칸마다 상수 0 으로 짓는다(low_ir.c — «선언적 초기화는 후속»).
//   수 칸의 0 은 맞는 값이다. 슬라이스 칸의 0 은 값이 아니다(정본 §10.3 (1c): 세우기 전에 읽는 것은 적합하지 않다) — VM 은 그것을 읽어 쓰면
//   «len needs a slice» 로 멈춘다. 처리기의 `E-ACTOR-UNINIT` 는 도우미 op 안으로 네 겹까지 따라가지만(2026-11-01) 그보다 깊거나 칸에 담겨
//   흐르는 길은 못 본다. 그래서 빈 슬라이스로 짓지 않고 **«아직 안 세움» 표지**(LW_UNINIT)를 둔다: 칸을 읽는 자리가 그 표지를 보면 같은 말로 멈추고, 태그 쪽으로
//   돌아갈 때는 0 으로 돌아간다(처음 빈 슬라이스로 지었더니 VM 은 멈추고 네이티브는 0 을 냈다 — 재 봤다). 안의 구조체 · 부동 칸은 받지 않는다.
//   앞의 nf 명령이 모두 `const 0` 이어야 한다(자리가 칸 하나씩 맞는다).
static bool cbe_make_zero(const low_ir_t *ir, const low_ir_def_t *d, proven_size_t j, int sx, proven_size_t nf) {
    if (sx < 0 || (proven_size_t)sx >= ir->nstructs || !ir->structs[sx].is_actor_state || j < nf) return false;
    for (proven_size_t q = 0; q < ir->structs[sx].nf; q++) {
        const low_ir_sfield_t *f = &ir->structs[sx].f[q];
        if (f->boxed && f->elem) continue;   // 슬라이스 칸 — «아직 안 세움» 표지(LW_UNINIT)를 둔다: 읽으면 VM 처럼 멈춘다
        if (f->sidx >= 0 || f->boxed || f->flt || f->arrn || !f->size || f->size > 8) return false;
    }
    for (proven_size_t q = 0; q < nf; q++) {
        const low_ir_ins_t *c = &d->code[j - nf + q];
        if (c->w != IRW_CONST || c->a != 0) return false;
    }
    return true;
}
// ★★ 2026-11-01 (W-20261101-1150) — 이 `spawn actor` 를 **태그 쪽 레코드로** 짓는가: 이 유닛의 컨테이너가 그 액터를 칸에 담는다면
//   빠른 경로에서도 처음부터 풀의 레코드 하나로 짓고 그 자리(K_AREF)만 든다. 그러면 그 액터에게 보내는 말은 모두 칸을 거친 말과 같은
//   길(풀고 · 부르고 · 되쓴다)로 가고, 풀어 든 사본이 없으니 컨테이너에 담아도 둘로 갈리지 않는다. 태그 경로의 `lw_make` 와 같은 레코드다.
static bool cbe_abox(const low_ir_t *ir, const low_ir_def_t *d, proven_size_t j, int sx, proven_size_t nf) {
    return sx >= 0 && sx < 64 && g_aid_need[sx] && ir->structs[sx].nf <= CBE_AID && cbe_make_for_struct(ir, sx) >= 0 &&
           cbe_make_zero(ir, d, j, sx, nf);
}
// ★ 2026-11-01 — 이 구조체에 슬라이스 칸이 있는가. 없으면 레코드의 평행 칸(`rlocsl` · `rssl`)을 옮길 까닭이 없다 —
//   늘 옮기면 레코드 지역마다 256 B 칸이 잡혀 파일 핸들 하나 쓰는 op 의 틀이 3,840 B 가 됐다(check-frames).
static bool cbe_struct_has_sl(const low_ir_t *ir, int sx) {
    if (sx < 0 || (proven_size_t)sx >= ir->nstructs) return false;
    for (proven_size_t z = 0; z < ir->structs[sx].nf; z++) if (cbe_f_slot(ir, &ir->structs[sx].f[z])) return true;
    if (ir->structs[sx].is_actor_state && sx < 64 && g_aid_need[sx]) return true;   // ★ 제 자리(CBE_AID)를 나른다
    return false;
}
// ★ 2026-11-01 (W-20261031-2053) — 이 option · result 가 **슬라이스 칸을 가진 레코드**를 쌌는가(ve = sidx+1 · fl 0).
//   그런 값의 슬라이스 칸은 `lw_r` 안에 없다 — 스택 자리마다 하나인 평행 칸 `wssl`, 지역마다 하나인 `wlocsl` 을 따라간다.
static bool cbe_optrec_sl(const low_ir_t *ir, unsigned char k, unsigned char ve, unsigned char fl) {
    if (k != K_OPT || !ve || fl || (proven_size_t)(ve - 1) >= ir->nstructs) return false;
    return cbe_struct_has_sl(ir, ve - 1);
}
// ★★ W-20261105-2304 — 128 비트 값이 **칸 안에** 사는 op 인가(구조체 칸 · 배열 · 슬라이스 원소 · 뷰 · 스택 원소).
//   빠른 경로의 칸 읽기는 그런 칸을 8 바이트 수로 읽는다 — 128 비트 셈에 닿는 op 이 그런 칸을 만지면 태그 경로에 둔다(그 경로가 16 바이트를 안다).
//   128 비트에 닿지 않는 op 은 묻지 않는다(전과 같다).
static bool cbe_w128_touch(const low_ir_def_t *d) {
    if (d->param_w128 || cbe_out_w128(d)) return true;
    for (proven_size_t k = 0; k < d->ncode; k++) if (cbe_is_w128(&d->code[k])) return true;
    return false;
}
static bool cbe_struct_w16(const low_ir_t *ir, int sx, int depth) {
    if (sx < 0 || (proven_size_t)sx >= ir->nstructs || depth > 8) return false;
    for (proven_size_t q = 0; q < ir->structs[sx].nf; q++) {
        const low_ir_sfield_t *f = &ir->structs[sx].f[q];
        if (f->size == 16 || (f->sidx >= 0 && cbe_struct_w16(ir, f->sidx, depth + 1))) return true;
    }
    return false;
}
static bool cbe_w128_place(const low_ir_t *ir, const low_ir_def_t *d) {
    if (!cbe_w128_touch(d)) return false;
    bool w16s = false;
    for (proven_size_t sx = 0; sx < ir->nstructs && !w16s; sx++) w16s = cbe_struct_w16(ir, (int)sx, 0);
    for (proven_size_t q = 0; q < d->nparams && q < LOW_MAX_PARAMS; q++) {
        if (((d->param_slice >> q) & 1u) && (d->param_esz[q] == 16 || d->param_rowe[q] == 16)) return true;
        if (((d->param_struct >> q) & 1u) && w16s) return true;
    }
    for (proven_size_t k = 0; k < d->ncode; k++) {
        const low_ir_ins_t *in = &d->code[k];
        switch (in->w) {
            case IRW_VARRAY: if (!(in->a & IR_STRUCT_BIT) && (in->a & 0xff) == 16) return true; break;
            case IRW_SNEW: case IRW_SPUSH: case IRW_SPOP: return true;
            case IRW_MAKE: case IRW_FIELD: case IRW_FSTORE: case IRW_VIEW: case IRW_TRYVIEW: case IRW_ENCODE:
                if (w16s) return true; break;
            default: break;
        }
    }
    return false;
}
// ★★ W-20261105-2304 — 128 비트 낱말 하나(종류 실행과 방출이 **함께** 부른다 — 두 벌로 짓지 않는다). out 이 NULL 이면 종류만 본다.
//   답: -1 이 낱말이 아니다(다음 규칙으로) · 0 빠른 경로에 못 내린다(g_sub 가 까닭) · 1 다 했다.
//   피연산자가 정수 칸(K_INT)이면 위 절반을 0 으로 본다 — VM 의 값 칸이 그렇다(정수 값의 `n` 은 0). 옮기는 양(shl …)이 그 자리다.
static const char *cbe_w128_pop(unsigned char k) { return k == K_W128 ? "lw_hs[--lw_hsp]" : "(lw_u128)(unsigned long long)st[--sp]"; }
static int cbe_w128_step(kstack_t *st, const low_ir_ins_t *in, low_out_t *out) {
    const bool eqw = (in->w == IRW_EQ || in->w == IRW_NE) && st->n >= 2 && (st->k[st->n-1] == K_W128 || st->k[st->n-2] == K_W128);
    if (!eqw && !cbe_is_w128(in)) return -1;
    const long long m = (long long)in->a;
    #define WK_OK(k_) ((k_) == K_W128 || (k_) == K_INT)
    #define W_PUSH(k_) do { st->o[st->n] = -1; st->ve[st->n] = 0; st->vn[st->n] = 0; st->fl[st->n] = 0; st->k[st->n++] = (k_); } while (0)
    static const struct { low_irw_t w; int op; } WB[] = {
        { IRW_ADD, 0 }, { IRW_SUB, 1 }, { IRW_MUL, 2 }, { IRW_DIV, 3 }, { IRW_MOD, 4 }, { IRW_LT, 5 }, { IRW_LE, 6 }, { IRW_GT, 7 },
        { IRW_GE, 8 }, { IRW_BAND, 9 }, { IRW_BOR, 10 }, { IRW_BXOR, 11 }, { IRW_SHL, 12 }, { IRW_SHR, 13 }, { IRW_WSHL, 14 },
        { IRW_WSHR, 15 }, { IRW_ROTL, 16 }, { IRW_ROTR, 17 } };
    static const struct { low_irw_t w; int op; } WU[] = {
        { IRW_NEG, 0 }, { IRW_BNOT, 1 }, { IRW_POPCNT, 2 }, { IRW_CLZ, 3 }, { IRW_CTZ, 4 }, { IRW_BSWAP, 5 } };
    if (st->n > 56) return 0;
    if (in->w == IRW_MK128) {
        if (st->n < 1 || st->k[st->n-1] != K_INT) return 0;
        st->n--;
        if (out) low_fpf(out, "    {{ long long lo_ = st[--sp]; lw_hs[lw_hsp++] = LW_WOF(lo_, {}ull); }}\n", PROVEN_ARG((unsigned long long)((unsigned long long)in->a)));
        W_PUSH(K_W128); return 1;
    }
    if (eqw) {
        if (!WK_OK(st->k[st->n-1]) || !WK_OK(st->k[st->n-2])) return 0;
        const unsigned char kb = st->k[st->n-1], ka = st->k[st->n-2];
        st->n -= 2;
        if (out) low_fpf(out, "    {{ lw_u128 b_ = {}; lw_u128 a_ = {}; st[sp++] = a_ {} b_; }}\n", proven_arg_cstr(cbe_w128_pop(kb)), proven_arg_cstr(cbe_w128_pop(ka)), proven_arg_cstr(in->w == IRW_EQ ? "==" : "!="));
        W_PUSH(K_INT); return 1;
    }
    if (in->w == IRW_CAST) {
        if (m & 0x40000) { low_bfmt(g_sub, sizeof g_sub, "`narrow_try` of a 128-bit value (its option holds one 64-bit slot)"); return 0; }
        if (st->n < 1) return 0;
        const unsigned char k0 = st->k[st->n-1];
        if (m & IR_SRC_W128) {
            if (k0 != K_W128) return 0;
            st->n--;
            if ((m & 0xff) == 0) { if (out) low_fpf(out, "    fs[fsp++] = lw_wc_flt({}ll, lw_hs[--lw_hsp]);\n", PROVEN_ARG((long long)(m))); W_PUSH(K_FLT); }
            else if ((m & 0xff) == 16) { if (out) low_fpf(out, "    lw_hs[lw_hsp-1] = lw_wf_of_w({}ll, lw_hs[lw_hsp-1]);\n", PROVEN_ARG((long long)(m))); W_PUSH(K_W128); }
            else { if (out) low_fpf(out, "    st[sp++] = lw_wf_to_int({}ll, lw_hs[--lw_hsp]);\n", PROVEN_ARG((long long)(m))); W_PUSH(K_INT); }
            return 1;
        }
        if ((m & 0xff) != 16) return 0;
        st->n--;
        if (k0 == K_INT) { if (out) low_fpf(out, "    lw_hs[lw_hsp++] = lw_wf_of_int({}ll, st[--sp]);\n", PROVEN_ARG((long long)(m))); }
        else if (k0 == K_FLT) { if (out) low_fpf(out, "    lw_hs[lw_hsp++] = lw_wf_of_flt({}ll, fs[--fsp]);\n", PROVEN_ARG((long long)(m))); }
        else if (k0 == K_W128) { if (out) low_fpf(out, "    lw_hs[lw_hsp-1] = lw_wf_of_int({}ll, LW_WLO(lw_hs[lw_hsp-1]));\n", PROVEN_ARG((long long)(m))); }   // 태그 경로와 같다(원천 표식이 없으면 아래 64 비트)
        else return 0;
        W_PUSH(K_W128); return 1;
    }
    for (size_t q = 0; q < sizeof WU / sizeof WU[0]; q++) {
        if (WU[q].w != in->w) continue;
        if (st->n < 1 || !WK_OK(st->k[st->n-1])) return 0;
        const unsigned char ka = st->k[--st->n];
        if (out) low_fpf(out, "    {{ lw_u128 a_ = {}; lw_hs[lw_hsp++] = lw_wc_un({}ll, {}, a_); }}\n", proven_arg_cstr(cbe_w128_pop(ka)), PROVEN_ARG((long long)(m)), PROVEN_ARG((int)(WU[q].op)));
        W_PUSH(K_W128); return 1;
    }
    for (size_t q = 0; q < sizeof WB / sizeof WB[0]; q++) {
        if (WB[q].w != in->w) continue;
        const int op = WB[q].op;
        if (op <= 2 && (m & 0x40000)) { low_bfmt(g_sub, sizeof g_sub, "`chk_*` on a 128-bit value (its option holds one 64-bit slot)"); return 0; }
        if (st->n < 2 || !WK_OK(st->k[st->n-1]) || !WK_OK(st->k[st->n-2])) return 0;
        const unsigned char kb = st->k[st->n-1], ka = st->k[st->n-2];
        st->n -= 2;
        if (op >= 5 && op <= 8) {
            if (out) low_fpf(out, "    {{ lw_u128 b_ = {}; lw_u128 a_ = {}; st[sp++] = lw_wc_cmp({}ll, {}, a_, b_); }}\n", proven_arg_cstr(cbe_w128_pop(kb)), proven_arg_cstr(cbe_w128_pop(ka)), PROVEN_ARG((long long)(m)), PROVEN_ARG((int)(op)));
            W_PUSH(K_INT);
        } else {
            if (out) low_fpf(out, "    {{ lw_u128 b_ = {}; lw_u128 a_ = {}; lw_hs[lw_hsp++] = lw_wf_bin({}ll, {}, a_, b_); }}\n", proven_arg_cstr(cbe_w128_pop(kb)), proven_arg_cstr(cbe_w128_pop(ka)), PROVEN_ARG((long long)(m)), PROVEN_ARG((int)(op)));
            W_PUSH(K_W128);
        }
        return 1;
    }
    #undef WK_OK
    #undef W_PUSH
    return 0;
}
// 바이트 슬라이스인가 — 원소 폭 1(또는 아직 안 적힌 0) · 부동 아님. 호스트 잎은 바이트만 읽고 쓴다.
#define CBE_BYTES(st_, i_) ((st_).k[(i_)] == K_SL && (st_).ve[(i_)] <= 1 && !(st_).fl[(i_)])
static bool cbe_kind_run(const low_ir_t *ir, const low_ir_def_t *d, const bool *sc,
                         kstack_t *at, unsigned char *lk, unsigned char *slot,
                         unsigned char *retk, unsigned char *rmap,
                         unsigned char *lez, unsigned char *lln, unsigned char *lfl) {
    kstack_t st = { {0},{0},{0},{0},{0}, 0, true };
    bool live = true;
    g_sub[0] = 0;
    const proven_size_t fa_di_ = (proven_size_t)(d - ir->defs);
    if (fa_di_ < 512) g_faloc[fa_di_] = 0;
    unsigned long long faloc_ = 0;   // ★ 2a — 이 op 에서 빠른 `spawn send` · `drain` 을 받는 지역(끝까지 가면 g_faloc 에 둔다)
    // ★ WO-0222 — 어느 지역을 **이미 읽었는가**. none 으로 시작한 option 지역을 슬라이스 option 으로 승격하는 것은
    //   읽기 전에만 한다: 방출기는 지역의 **마지막** 종류로 읽으므로, 앞선 읽기가 옛 종류로 판정됐으면 둘이 갈린다.
    //   파라미터는 경계에서 이미 채워졌으므로 읽힌 것으로 친다.
    unsigned long long loaded_ = 0;
    for (proven_size_t q = 0; q < d->nparams && q < CBE_LOCMAX; q++) loaded_ |= 1ull << q;
    for (proven_size_t j = 0; j < d->ncode; j++) {
        const low_ir_ins_t *in = &d->code[j];
        g_kj = (int)j; g_kw = (int)in->w;
        // ★★★ **도달 불가 코드는 아무것도 말하지 않는다.**
        //   본문 끝에는 언제나 **암묵적 `const 0; ret`** 이 붙는다(단위 반환). 그것이
        //   도달 불가인데도 "이 op 은 정수를 돌려준다" 고 읽혀서, `result` 를 돌려주는 op 이
        //   **"두 종류를 섞는다"** 고 거절당했다 — 즉 **오류를 다루는 op 은 전부 느린 경로**에
        //   남았다. **오진이다**(교훈 5). 라벨이 아니면, RET/BR 뒤는 **죽어 있다.**
        if (at[j].set) { st = at[j]; live = true; }
        else if (!live) continue;
        // ★ **깊이에 벽이 있어야 한다.** 없으면 종류 스택이 자기 이웃 배열을 밟는다
        //   — 그리고 컴파일러가 **조용히 죽는다**(그렇게 죽었다: json.low).
        if (st.n < 0 || st.n > 56) return false;
        { const int w128_ = cbe_w128_step(&st, in, NULL);   // ★ W-20261105-2304 — 128 비트 낱말(방출과 같은 함수)
          if (w128_ == 0) return false;
          if (w128_ == 1) continue; }
        unsigned char a1, a2, a3;
        switch (in->w) {
            case IRW_CONST: st.o[st.n] = -1; st.k[st.n++] = K_INT; break;
            case IRW_LOAD:  if (in->a >= CBE_LOCMAX) return cbe_locals_over(in->a);
                            loaded_ |= 1ull << in->a;
                            st.ve[st.n] = lez[in->a]; st.vn[st.n] = lln[in->a]; st.fl[st.n] = lfl[in->a];
                            if (lk[in->a] == K_REF) {
                                st.o[st.n] = (signed char)rmap[in->a];
                                st.k[st.n++] = K_REF;
                                break;
                            }
                            st.o[st.n] = (signed char)in->a; st.k[st.n++] = lk[in->a]; break;
            /* (뷰는 지역에 담기면 lk 가 K_VIEW 를 기억한다 — 저장소는 슬라이스와 같다) */
            case IRW_STORE:
                if (st.n < 1) return false;
                if (in->a >= CBE_LOCMAX) return cbe_locals_over(in->a);
                // ★ W-20261105-2304 — 128 비트로 선언한 지역에 담는 수(`var s u128 0 .` 의 0)는 128 비트 값이다(위 절반 0 — VM 과 같다)
                if (st.k[st.n-1] == K_INT && ((d->loc_w128 >> in->a) & 1u)) { st.k[st.n-1] = K_W128; st.o[st.n-1] = -1; }
                // ★★ 2026-11-01 (W-20261101-1828) — 배열 칸 레코드를 담는 지역은 **한 번만** 담긴다. VM 의 `set b. <새 레코드>` 는 새 버퍼를
                //   주고 앞서 낸 칸 슬라이스는 옛 바이트를 계속 본다 — 빠른 경로는 같은 `rbuf` 에 덮어쓰므로 두 번째 담기가 그 슬라이스를 바꾼다.
                //   (되풀이 안의 `let` 은 담는 자리가 하나다 — 앞 바퀴의 슬라이스는 검사층이 블록 밖으로 못 나가게 한다, E-LIT-ESCAPE.)
                if (st.k[st.n-1] >= K_REC && st.k[st.n-1] < K_VIEW && cbe_rec_arr_ok(ir, st.k[st.n-1] - K_REC)) {
                    int ns_ = 0;
                    for (proven_size_t k_ = 0; k_ < d->ncode; k_++) if (d->code[k_].w == IRW_STORE && d->code[k_].a == in->a) ns_++;
                    if (ns_ > 1 || (proven_size_t)in->a < d->nparams) {
                        low_bfmt(g_sub, sizeof g_sub, "local {} holds a record with array fields and is stored {} times (a field slice would see the new bytes)",
                                 PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((int)(ns_)));
                        return false; }
                }
                if (st.k[st.n-1] >= K_REC && st.k[st.n-1] < K_VIEW)   // ★ 갓 띄운 액터의 표식은 지역에 남는다(한 번 붙으면 안 떨어진다 — 보수적)
                    lfl[in->a] = (unsigned char)((lfl[in->a] & CBE_FL_NOID) | (st.fl[st.n-1] & CBE_FL_NOID));
                if (st.k[st.n-1] == K_SL || st.k[st.n-1] == K_ROWS || st.k[st.n-1] == K_VEC || st.k[st.n-1] == K_MASK ||
                    st.k[st.n-1] == K_OPTSL || st.k[st.n-1] == K_AREF || st.k[st.n-1] >= K_VIEW) {   // ★ K_OPTSL — 원소 폭(vn) · 표식(fl)을 지역이 기억한다(2026-11-01)
                    lez[in->a] = st.ve[st.n-1]; lln[in->a] = st.vn[st.n-1]; lfl[in->a] = st.fl[st.n-1];
                }
                // ★ option 지역은 내부 종류 표식(ve=sidx+1, fl=뷰?)을 lez/lfl 에 실어 왕복을 견딘다.
                if (st.k[st.n-1] == K_OPT) { lez[in->a] = st.ve[st.n-1]; lfl[in->a] = st.fl[st.n-1]; }
                if (st.k[st.n-1] == K_REF) {
                    // ★ 참조를 지역에 담으면 그 지역은 **그 대상의 다른 이름**이다.
                    //   두 대상을 담으면 이름이 흔들린다 ⇒ **안 내린다.**
                    if (rmap[in->a] != 0xff && rmap[in->a] != (unsigned char)st.o[st.n-1]) return false;
                    rmap[in->a] = (unsigned char)st.o[st.n-1];
                    slot[in->a] = K_REF; lk[in->a] = K_REF;
                    st.n--;
                    break;
                }
                            // ★ 레코드·스택을 **다른 지역에 다시 담으면** 태그 경로에서는
                            //   **별칭**이 된다(같은 박스). 값 복사는 그것과 다르다 ⇒ **안 내린다.**
                            // ★ K_VIEW·K_SVIEW 는 **참조가 아니다**(바이트 위의 무복사 뷰) —
                            //   지역에 담아도 별칭이 안 생긴다(포인터가 곧 그 자리다). 별칭 체크에서 뺀다.
                            // ★ 2026-11-04 — `match` 의 숨은 지역(d->mtmp)은 갈래 몸 앞에서만 읽힌다: 별칭이든 값 복사든 답이 같다.
                            if (st.k[st.n-1] >= K_REC && st.k[st.n-1] < K_VIEW &&
                                st.o[st.n-1] >= 0 && st.o[st.n-1] != (signed char)in->a &&
                                !(in->a < 64 && ((d->mtmp >> in->a) & 1u))) return false;
                            if ((st.k[st.n-1] == K_STK || st.k[st.n-1] == K_BSET) &&
                                st.o[st.n-1] >= 0 && st.o[st.n-1] != (signed char)in->a) return false;
                            a1 = st.k[--st.n];
                            if (slot[in->a] == 0xff) { slot[in->a] = a1; lk[in->a] = a1; }
                            // ★ WO-0222 — option(슬라이스) 지역에 none(안쪽 표식 없는 K_OPT)을 담는 것은 같은 종류다.
                            else if (slot[in->a] == K_OPTSL && a1 == K_OPT && st.ve[st.n] == 0 && st.fl[st.n] == 0) { }
                            // ★ 겹 option 지역에 안이 빈 바깥 값(none · error — 표식 없는 K_OPT)을 담는 것도 같은 종류다.
                            else if (slot[in->a] == K_OPT2 && a1 == K_OPT && st.ve[st.n] == 0 && st.fl[st.n] == 0) { }
                            else if (slot[in->a] == K_OPT && a1 == K_OPT2 && lez[in->a] == 0 && lfl[in->a] == 0 &&
                                     !(loaded_ & (1ull << in->a))) { slot[in->a] = K_OPT2; lk[in->a] = K_OPT2; }
                            else if (slot[in->a] == K_OPT && a1 == K_OPTSL && lez[in->a] == 0 && lfl[in->a] == 0 &&
                                     !(loaded_ & (1ull << in->a))) { slot[in->a] = K_OPTSL; lk[in->a] = K_OPTSL; }
                            else if (slot[in->a] != a1) {
                                // ★★★ **바이트 슬라이스 파라미터를 구조체 배열 뷰로 되담는다**
                                //   (`s be view_array pt s`): 같은 버퍼의 **다른 뷰**이지 두 종류가
                                //   아니다. K_SL ↔ (K_SVIEW·타입배열) 재담기는 허용하고, 슬롯을
                                //   **더 구체적인 뷰**로 승격한다.
                                bool sl_a = (slot[in->a] == K_SL), sl_1 = (a1 == K_SL);
                                bool sv_a = (slot[in->a] >= K_SVIEW), sv_1 = (a1 >= K_SVIEW);
                                if ((sl_a && sv_1)) { slot[in->a] = a1; lk[in->a] = a1; }
                                else if ((sv_a && sl_1)) { /* 이미 더 구체적 — 유지 */ }
                                else {
                                    low_bfmt(g_sub, sizeof g_sub, "local {} holds two kinds ({}, {})",
                                             PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((int)((int)slot[in->a])), PROVEN_ARG((int)((int)a1)));
                                    return false;
                                }
                            }
                            break;
            case IRW_LEN:   if (st.n < 1 || (st.k[st.n-1] != K_SL && st.k[st.n-1] != K_ROWS && st.k[st.n-1] < K_SVIEW)) return false;
                            st.k[st.n-1] = K_INT; break;
            // ★ `elem.check` — (슬라이스, 정수 한도) → 참거짓. **정수 원소만** 내린다:
            //   부동·구조체 원소는 값이 정수가 아니므로 태그 경로에 남는다(그렇게 말한다).
            case IRW_ELEMCK:
                            if (st.n < 2 || st.k[st.n-1] != K_INT || st.k[st.n-2] != K_SL) return false;
                            if (st.fl[st.n-2]) return false;
                            st.n--; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1;
                            st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            // ★★★★★ **해시도 빠른 경로에 온다** (2026-08-18 — 크립토 축을 열다 찾았다).
            //   `hash.sha256` 은 **태그 경로에만** 방출돼 있었다. 그래서 그것을 부르는 op 은
            //   통째로 태그로 떨어졌고, 네이티브 빌드에서도 **해석기로 돌았다** —
            //   `--why-slow` 가 *"the word `hash.sha256` is not lowered yet"* 라고 말하고 있었는데
            //   그 목록을 아무도 정기적으로 안 읽었다(2026-08-16 벡터 저장과 **같은 부류**:
            //   *느린 게 아니라 부를 수 없었다*. 답은 맞으니 차등 스윕도 못 본다).
            //   ★ 모양은 단순하다: `(바이트, 쓰기 가능한 바이트) → 쓴 길이`.
            case IRW_SHA256: case IRW_SHA512: case IRW_SHA384: case IRW_POLY1305:
                            if (st.n < 2 || st.k[st.n-1] != K_SL || st.k[st.n-2] != K_SL) return false;
                            st.n--; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            /* ★ 암호 잎: 슬라이스 넷(셋) → 수 하나. 모양이 해시와 같아 빠른 경로에 그대로 선다. */
            case IRW_AESCTR: case IRW_CHACHA20:
                            if (st.n < 4 || st.k[st.n-1] != K_SL || st.k[st.n-2] != K_SL ||
                                st.k[st.n-3] != K_SL || st.k[st.n-4] != K_SL) return false;
                            st.n -= 3; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_AESGCM:
                            if (st.n < 6 || st.k[st.n-1] != K_SL || st.k[st.n-2] != K_SL ||
                                st.k[st.n-3] != K_SL || st.k[st.n-4] != K_SL ||
                                st.k[st.n-5] != K_SL || st.k[st.n-6] != K_SL) return false;
                            st.n -= 5; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_CHAPOLY:
                            if (st.n < 5 || st.k[st.n-1] != K_SL || st.k[st.n-2] != K_SL ||
                                st.k[st.n-3] != K_SL || st.k[st.n-4] != K_SL ||
                                st.k[st.n-5] != K_SL) return false;
                            st.n -= 4; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_GHASH:
                            if (st.n < 3 || st.k[st.n-1] != K_SL || st.k[st.n-2] != K_SL ||
                                st.k[st.n-3] != K_SL) return false;
                            st.n -= 2; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_AESROUND: case IRW_AESLAST:
                            if (st.n < 2 || st.k[st.n-1] != K_SL || st.k[st.n-2] != K_SL) return false;
                            st.n--; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_SAMESL:
                            if (st.n < 2 || st.k[st.n-1] != K_SL || st.k[st.n-2] != K_SL) return false;
                            if (st.fl[st.n-1] || st.fl[st.n-2]) return false;
                            st.n--; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_CRC32: case IRW_HASH64: case IRW_RANDBYTES:
                            if (st.n < 1 || st.k[st.n-1] != K_SL) return false;
                            st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            // ★★★ **레인 순열도 빠른 경로다** (2026-08-18). 셋 다 벡터 하나를 받아 벡터 하나를
            //   낸다 — 레인 폭도 개수도 그대로다. 태그 경로의 `lw_vperm` 과 **뜻이 같아야** 하므로
            //   아래 방출은 그 함수의 정의를 **글자 그대로** 따라간다(패닉 문구까지).
            case IRW_VREVERSE: case IRW_VROTATE:
                            if (st.n < 1 || st.k[st.n-1] != K_VEC) return false;
                            st.o[st.n-1] = -1; break;
            // ★ 평균은 **정수 인트린식**이다(태그 경로가 부동이면 패닉한다). 여기서는 그 판정을
            //   컴파일 시에 하고, 부동이면 **안 받는다** — 조용히 다른 답을 내지 않기 위해서다.
            case IRW_VAVG:  if (st.n < 2 || st.k[st.n-1] != K_VEC || st.k[st.n-2] != K_VEC) return false;
                            if (st.vn[st.n-1] != st.vn[st.n-2] || st.ve[st.n-1] != st.ve[st.n-2]) return false;
                            if (st.fl[st.n-1] || st.fl[st.n-2]) return false;
                            st.n--; st.o[st.n-1] = -1; break;
            case IRW_VSHUFFLE: {
                            if (st.n < 1 || st.k[st.n-1] != K_VEC) return false;
                            // ★ 레인 수와 색인 개수가 다르면 태그 경로는 **런타임에 패닉**한다.
                            //   여기서는 그 판정을 **컴파일 시**에 할 수 있다 — 그러면 안 받는다
                            //   (빠른 경로가 조용히 다른 답을 내는 일이 없어야 한다).
                            if ((int)((in->a >> 32) & 0xf) != (int)st.vn[st.n-1]) return false;
                            st.o[st.n-1] = -1; break;
            }
            case IRW_RNGNEXT: case IRW_TIMESLEEP:
                            if (st.n < 1 || st.k[st.n-1] != K_INT) return false;
                            st.o[st.n-1] = -1; break;
            case IRW_TIMENOW: case IRW_TIMELOCAL:
                            if (st.n >= 60) return false;
                            st.k[st.n] = K_INT; st.o[st.n] = -1; st.n++; break;
            case IRW_VARRAY: {
                // ★★★ **바이트 슬라이스 → 타입 배열.** 원소 폭은 **이 슬롯의 것**이 된다.
                if (st.n < 1) return false;
                // ★ RFC-0132 §13.10 — 줄의 배열(0x80000)은 빠른 경로가 모른다: 원소 폭(ve)이 줄 바이트여야 하는데 슬롯은 한 바이트
                //   폭만 싣는다. 안쪽 `index` 가 잘못 늘어나지 않게 태그 경로로 내려보낸다.
                if (in->a & 0x80000) {   // 줄의 배열(K_ROWS) — 두 겹 · 줄 바이트 ≤ 255 만
                    const unsigned rb_ = (unsigned)(((proven_u64)in->a >> 32) & 0xffffffu), ez_ = (unsigned)(in->a & 0xff);
                    const unsigned sub_ = (unsigned)(((proven_u64)in->a & IR_ROWSUB_MASK) >> IR_ROWSUB_SHIFT);
                    if ((in->a & 0x40000) || !ez_ || ez_ > 8 || !rb_ || rb_ > 255 || rb_ % ez_) return false;
                    if (sub_ && (sub_ > 63 || sub_ >= ir->nrowsh || !ir->rowsh_rb[sub_] || rb_ % ir->rowsh_rb[sub_])) return false;   // ★ 세 겹 이상(2026-11-02)
                    if ((in->a & 0x10000) && ez_ != 4 && ez_ != 8) return false;
                    const unsigned char fl_ = (unsigned char)(((in->a & 0x10000) ? CBE_FL_FLT : ((in->a & 0x20000) && ez_ < 8) ? CBE_FL_SGN : 0) | (sub_ << 2));
                    if (st.k[st.n-1] == K_ROWS && st.vn[st.n-1] == rb_ && st.ve[st.n-1] == ez_ && st.fl[st.n-1] == fl_) break;   // 이미 그 모양
                    if (st.k[st.n-1] != K_SL || st.ve[st.n-1] != 1) return false;
                    st.k[st.n-1] = K_ROWS; st.ve[st.n-1] = (unsigned char)ez_; st.vn[st.n-1] = (unsigned char)rb_; st.fl[st.n-1] = fl_;
                    break;
                }
                // ★ RFC-0109 단계 1 — **이미 그 모양이면 항등이다**(파라미터가 원소 단위로 들어온 자리).
                if (!g_no_elemsl && (in->a & 0x40000) && st.k[st.n-1] == (unsigned char)(K_SVIEW + ((in->a >> 20) & 0xff))) break;
                if (!g_no_elemsl && !(in->a & 0x40000) && st.k[st.n-1] == K_SL && st.ve[st.n-1] > 1 &&
                    st.ve[st.n-1] == (unsigned char)(in->a & 0xff)) {
                    if (in->a & 0x10000) st.fl[st.n-1] = CBE_FL_FLT;
                    else if ((in->a & 0x20000) && (in->a & 0xff) < 8) st.fl[st.n-1] = CBE_FL_SGN;
                    break;
                }
                if (st.k[st.n-1] != K_SL) return false;
                if (in->a & 0x40000) {
                    // ★★★ **구조체 원소** — sidx 를 종류에 담고, 길이를 원소 수로 본다.
                    int sx = (int)((in->a >> 20) & 0xff);
                    // ★ 2026-10-31 (W-20261031-1421) — 부동 칸이 있는 구조체의 배열도 본다: 칸 읽기는 그 폭의 비트를 부동으로 읽고(뷰의 FIELD),
                    //   부동 칸에 쓰기는 위 FSTORE 가 거절한다.
                    // ★ 2026-11-02 (W-20261102-0024) — 배열 칸을 가진 구조체의 배열도 본다: 원소는 뷰이고, 그 배열 칸은 뷰의 배열 칸(무복사 · 제자리 쓰기)이다.
                    //   레코드 값을 원소에 쓰는 것(`set idx ps. i . r.`)은 아래 ISTORE 가 그대로 거절한다.
                    if (!cbe_rec_ok_f(ir, sx, true) && !cbe_rec_arr_ok(ir, sx)) return false;
                    if (st.ve[st.n-1] != 1) return false;
                    st.k[st.n-1] = (unsigned char)(K_SVIEW + sx); st.ve[st.n-1] = 0;
                    break;
                }
                unsigned ez = (unsigned)(in->a & 0xff);
                if (!ez || ez > 8) return false;
                if (st.ve[st.n-1] != 1) return false;   // 바이트 슬라이스 위에서만 (멱등성)
                // ★ **부호형 원소**(IR_SGN_BIT)는 슬롯에 fl=2 를 단다 — `index` 가 읽을 때 부호를 늘린다(2026-10-04).
                //   8 바이트(i64)는 늘릴 비트가 없으므로 표식 없이 그대로 읽는다(cbe_ebits_w 와 짝).
                // ★ 부동 원소 슬라이스 — 폭은 그대로, **부동 표식**을 단다(f32=4·f64=8).
                if (in->a & 0x10000) { if (ez != 4 && ez != 8) return false; st.fl[st.n-1] = CBE_FL_FLT; }
                else if ((in->a & 0x20000) && ez < 8) st.fl[st.n-1] = CBE_FL_SGN;
                st.ve[st.n-1] = (unsigned char)ez;
                break;
            }
            case IRW_ASSERT: if (st.n < 1 || st.k[st.n-1] != K_INT) return false;
                            st.n--; break;
            case IRW_FCONST: st.o[st.n] = -1; st.k[st.n++] = K_FLT; break;
            case IRW_REF: case IRW_MREF: {
                proven_size_t tgt = (proven_size_t)(in->a & 0xffff);
                if (tgt >= CBE_LOCMAX) return cbe_locals_over((unsigned)tgt);
                if (slot[tgt] != 0xff && slot[tgt] != K_INT) return false;   // 정수 지역만
                st.o[st.n] = (signed char)tgt;
                st.k[st.n++] = K_REF;
                break;
            }
            case IRW_DEREF:
                if (st.n < 1 || st.k[st.n-1] != K_REF || st.o[st.n-1] < 0) return false;
                st.k[st.n-1] = K_INT; break;
            case IRW_ENCODE: {
                // ★ 레코드를 **바이트로** 편다. 결과는 **프레임 안의 버퍼**를 가리키는 슬라이스다
                //   — 태그 경로는 그것을 **8칸짜리 버퍼 풀**에서 꺼냈다(다섯 번째 유한 풀).
                if (st.n < 1) return false;
                int sx = (int)(in->a & 0xffff);
                if ((!cbe_rec_ok_f(ir, sx, true) && !cbe_rec_arr_ok(ir, sx)) || !ir->structs[sx].viewable) return false;   // ★ 배열 칸은 레코드 바이트 안에 있다(2026-11-01)
                if (ir->structs[sx].total > 64) return false;
                if (st.k[st.n-1] != (unsigned char)(K_REC + sx)) return false;
                st.k[st.n-1] = K_SL; st.o[st.n-1] = -1; st.ve[st.n-1] = 1; st.vn[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            }
            case IRW_SPLAT: {
                // splat: 스칼라 → 벡터(레인 수·폭은 meta 가 안다). 부동이면 입력도 f64.
                // ★ 2026-10-14 (RFC-0148 S5 의 탐침이 찾았다) — 빠른 길의 벡터에는 **레인의 부호**가 없다. 부호 있는 레인의 `reduce_*` 가
                //   부호 없이 접혀 VM 과 다른 값을 냈다(`vec i32 4` 의 -3 넷의 합: VM -12 · 네이티브 17179869172). 부호를 아는 태그 길로 보낸다.
                // ★★ 2026-11-01 (W-20261101-1828) — 부호 있는 레인은 표식(fl = CBE_FL_SGN)을 단다: 축약 · 비교가 부호를 늘려 셈한다(VM 의 `sign`).
                unsigned ln = (unsigned)((in->a >> 8) & 0xff), ez = (unsigned)(in->a & 0xff);
                if (!lw_v_ok(ln, ez)) return false;
                bool fv = (in->a & 0x10000) != 0;
                if (st.n < 1) return false;
                if (fv) { if (st.k[st.n-1] != K_FLT || (ez != 4 && ez != 8)) return false; }
                else if (st.k[st.n-1] != K_INT) return false;
                st.k[st.n-1] = K_VEC; st.vn[st.n-1] = (unsigned char)ln;
                st.ve[st.n-1] = (unsigned char)ez; st.fl[st.n-1] = fv ? CBE_FL_FLT : (in->a & IR_SGN_BIT) ? CBE_FL_SGN : 0; st.o[st.n-1] = -1;
                break;
            }
            case IRW_VLOAD: {
                // load <slice> <i> → 벡터. 부동 슬라이스면 부동 벡터.
                if (st.n < 2 || st.k[st.n-1] != K_INT || st.k[st.n-2] != K_SL) return false;
                unsigned ln = (unsigned)((in->a >> 8) & 0xff), ez = (unsigned)(in->a & 0xff);
                if (!lw_v_ok(ln, ez)) return false;
                bool fv = (in->a & 0x10000) != 0;
                // ★ 2026-11-01 — 바이트 슬라이스에서도 읽는다: VM 은 그때 색인을 원소(폭 ez) 단위로 세고 한도를 n / ez 로 둔다(low_vm.c VLOAD).
                const bool vbyte_ = st.ve[st.n-2] <= 1 && st.fl[st.n-2] == 0 && ez > 1;
                if (st.ve[st.n-2] != (unsigned char)ez && !vbyte_) return false;   // 슬롯의 원소 폭과 같아야
                const unsigned char wfl_ = fv ? CBE_FL_FLT : (in->a & IR_SGN_BIT) ? CBE_FL_SGN : 0;   // ★ 2026-11-01 — 레인의 부동 · 부호
                if (!vbyte_ && st.fl[st.n-2] != wfl_) return false;               // 슬라이스의 표식과 같아야
                if (fv && ez != 4 && ez != 8) return false;
                unsigned char fl = wfl_;
                st.n--;
                st.k[st.n-1] = K_VEC; st.vn[st.n-1] = (unsigned char)ln;
                st.ve[st.n-1] = (unsigned char)ez; st.fl[st.n-1] = fl; st.o[st.n-1] = -1;
                break;
            }
            case IRW_VSTORE: {
                // store <slice> <i> <vec> → (). `VLOAD` 의 거울 — 레인/폭은 **벡터가 실어 온다**.
                if (st.n < 3 || st.k[st.n-1] != K_VEC ||
                    st.k[st.n-2] != K_INT || st.k[st.n-3] != K_SL) return false;
                unsigned ln = (unsigned)st.vn[st.n-1], ez = (unsigned)st.ve[st.n-1];
                if (!lw_v_ok(ln, ez)) return false;
                const bool vbyte_ = st.ve[st.n-3] <= 1 && st.fl[st.n-3] == 0 && ez > 1;   // ★ 2026-11-01 — 바이트 슬라이스(VM VSTORE 와 같은 단위)
                if (st.ve[st.n-3] != (unsigned char)ez && !vbyte_) return false;   // 슬롯의 원소 폭과 같아야
                if (!vbyte_ && (st.fl[st.n-1] != 0) != (st.fl[st.n-3] != 0)) return false;  // 부동성도 같아야
                // ★ **0 을 남긴다** — IR 은 `vector.store` 뒤에 `drop` 을 낸다(태그 경로의
                //   `lw_vstore` 가 `lw_int(0)` 을 돌려주기 때문이다). 시뮬레이터가 이걸
                //   모르면 스택 모형이 어긋나 그 op 이 **조용히 태그 경로로 떨어진다** —
                //   실제로 처음에 `st.n -= 3` 이라 적었다가 `--why-slow` 가 *"drop at 28"* 로
                //   가르쳐 줬다. `IRW_SWAP` 이 바로 위에서 같은 규약을 쓴다.
                st.n -= 2; st.k[st.n-1] = K_INT; st.ve[st.n-1] = 0;
                st.fl[st.n-1] = 0; st.o[st.n-1] = -1; break;
            }
            // ★★★ **마스크 적재·저장** (2026-08-18) — 마지막 비-동시성 벡터 낱말 둘.
            //   빠른 경로의 마스크는 `st[]` 위의 **비트마스크**다(비교가 이미 그렇게 낸다).
            //   ☞ 스택 모양은 태그 helper 의 인자 순서 그대로다:
            //     `load_masked <src> <i> <mask> <passthrough>` · `store_masked <dst> <i> <v> <mask>`
            //   ★ 부동 레인은 아직 안 받는다 — 비트↔double 변환이 끼면 뜻이 하나가 아니다.
            case IRW_VLOADM: {
                if (st.n < 4 || st.k[st.n-1] != K_VEC || st.k[st.n-2] != K_MASK ||
                    st.k[st.n-3] != K_INT || st.k[st.n-4] != K_SL) return false;
                unsigned ln = (unsigned)((in->a >> 8) & 0xff), ez = (unsigned)(in->a & 0xff);
                if (!lw_v_ok(ln, ez)) return false;
                if (st.ve[st.n-4] != (unsigned char)ez) return false;
                if (st.vn[st.n-1] != (unsigned char)ln || st.vn[st.n-2] != (unsigned char)ln) return false;
                if ((in->a & 0x10000) || st.fl[st.n-1] || st.fl[st.n-4]) return false;
                unsigned char lnn = st.vn[st.n-1], ezz = st.ve[st.n-1];
                st.n -= 3; st.k[st.n-1] = K_VEC; st.vn[st.n-1] = lnn; st.ve[st.n-1] = ezz;
                st.fl[st.n-1] = 0; st.o[st.n-1] = -1; break;
            }
            case IRW_VSTOREM: {
                if (st.n < 4 || st.k[st.n-1] != K_MASK || st.k[st.n-2] != K_VEC ||
                    st.k[st.n-3] != K_INT || st.k[st.n-4] != K_SL) return false;
                unsigned ln = (unsigned)st.vn[st.n-2], ez = (unsigned)st.ve[st.n-2];
                if (!lw_v_ok(ln, ez)) return false;
                const bool mbyte_ = st.ve[st.n-4] <= 1 && st.fl[st.n-4] == 0 && ez > 1;   // ★ 2026-11-01 — 바이트 슬라이스(VM: 한도 = 바이트 / 레인 폭)
                if (st.ve[st.n-4] != (unsigned char)ez && !mbyte_) return false;
                if (st.vn[st.n-1] != (unsigned char)ln) return false;
                if (st.fl[st.n-2] || st.fl[st.n-4]) return false;
                st.n -= 3; st.k[st.n-1] = K_INT; st.ve[st.n-1] = 0;
                st.fl[st.n-1] = 0; st.o[st.n-1] = -1; break;
            }
            case IRW_RADD: case IRW_RMUL: case IRW_RMIN: case IRW_RMAX:
                if (st.n < 1 || st.k[st.n-1] != K_VEC) return false;
                // ★ 부동 벡터의 축약은 **f64** 를, 정수 벡터는 정수를 낸다.
                st.k[st.n-1] = st.fl[st.n-1] == CBE_FL_FLT ? K_FLT : K_INT; st.o[st.n-1] = -1; break;
            case IRW_MANY: case IRW_MALL:
                if (st.n < 1 || st.k[st.n-1] != K_MASK) return false;
                st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_UNM:
                if (st.n < 1) return false;
                if (st.k[st.n-1] == K_VEC) {   // ★ 2026-11-03 (W-20261103-2113) — 레인마다 sqrt · abs · floor · ceil(lw_vun 과 같다)
                    const int sel_ = (int)(in->a & 0xf);
                    if (sel_ > 3) return false;
                    if (sel_ == 0 && st.fl[st.n-1] != CBE_FL_FLT) return false;   // 정수 레인의 sqrt 는 실행 중에 멈춘다 — 그 멈춤은 태그 경로에 둔다
                    st.o[st.n-1] = -1; break;
                }
                if ((in->a & 0xf) == 4) {   // nonzero_of — **정수 → option**
                    if (st.k[st.n-1] != K_INT) return false;
                    st.k[st.n-1] = K_OPT; st.o[st.n-1] = -1; break;
                }
                if ((in->a & 0xf) == 5 || (in->a & 0xf) == 6) {   // sum/sum_fast — **부동 슬라이스 → f64**
                    if (st.k[st.n-1] != K_SL || st.fl[st.n-1] != CBE_FL_FLT) return false;
                    st.k[st.n-1] = K_FLT; st.o[st.n-1] = -1; st.fl[st.n-1] = 0; break;
                }
                if ((in->a & 0xf) == 0) {   // sqrt — **f64 전용**
                    if (st.k[st.n-1] != K_FLT) return false;
                    break;
                }
                if (st.k[st.n-1] != K_INT && st.k[st.n-1] != K_FLT) return false;
                st.o[st.n-1] = -1; break;
            case IRW_BINM:
                if (st.n < 2 || st.k[st.n-1] != st.k[st.n-2]) return false;
                if (st.k[st.n-1] == K_VEC) {   // ★ 2026-11-03 — 레인마다 min · max(정수 · 부동), fmod · pow(부동 레인만 — 정수 레인은 실행 중에 멈춘다)
                    if (st.vn[st.n-1] != st.vn[st.n-2] || st.ve[st.n-1] != st.ve[st.n-2] || st.fl[st.n-1] != st.fl[st.n-2]) return false;
                    const int sel_ = (int)(in->a & 0xff);
                    if (sel_ > 3 || ((sel_ == 0 || sel_ == 3) && st.fl[st.n-1] != CBE_FL_FLT)) return false;
                    st.n--; st.o[st.n-1] = -1; break;
                }
                if ((in->a & 0xff) == 0) { if (st.k[st.n-1] != K_FLT) return false; }   // fmod — f64 전용
                else if (st.k[st.n-1] != K_INT && st.k[st.n-1] != K_FLT) return false;
                st.n--; st.o[st.n-1] = -1; break;
            case IRW_BAND: case IRW_BOR: case IRW_BXOR:
            case IRW_SHL: case IRW_SHR: case IRW_WSHL: case IRW_WSHR:
            case IRW_ROTL: case IRW_ROTR:
            case IRW_CLMULLO: case IRW_CLMULHI: case IRW_MULHI:
                if (st.n < 2 || st.k[st.n-1] != K_INT || st.k[st.n-2] != K_INT) return false;
                st.n--; st.o[st.n-1] = -1; break;
            case IRW_BNOT: case IRW_POPCNT: case IRW_CLZ: case IRW_CTZ: case IRW_BSWAP:
                if (st.n < 1 || st.k[st.n-1] != K_INT) return false;
                st.o[st.n-1] = -1; break;
            case IRW_ALOAD:   // atomic: [slice, i] → int
                if (st.n < 2 || st.k[st.n-1] != K_INT || st.k[st.n-2] != K_SL) return false;
                if (st.fl[st.n-2]) return false;   // ★ 부호·부동 원소의 원자 읽기는 태그 경로(부호를 늘리지 않는다)
                st.n--; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_ASTORE: case IRW_AADD: case IRW_ASUB: case IRW_AAND:
            case IRW_AOR: case IRW_AXOR: case IRW_ASWAP:   // [slice, i, v] → int
                if (st.n < 3 || st.k[st.n-1] != K_INT || st.k[st.n-2] != K_INT || st.k[st.n-3] != K_SL)
                    return false;
                if (st.fl[st.n-3]) return false;
                st.n -= 2; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_ACAS:   // [slice, i, exp, des] → int(0/1)
                if (st.n < 4 || st.k[st.n-1] != K_INT || st.k[st.n-2] != K_INT ||
                    st.k[st.n-3] != K_INT || st.k[st.n-4] != K_SL) return false;
                if (st.fl[st.n-4]) return false;
                st.n -= 3; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_AFENCE:
                // ★ 방출기와 **같은 규약**: fence 도 자리 하나를 민다. 시뮬레이터가 이걸
                //   모르면 스택 모형이 어긋나 그 op 이 조용히 **태그 경로로 떨어진다**
                //   (실측 `--why-slow` 1 / 10 — 답은 맞는데 느려진다. 그래서 안 보인다).
                if (st.n >= (int)(sizeof st.k / sizeof st.k[0])) return false;
                st.o[st.n] = -1; st.k[st.n++] = K_INT; break;
            case IRW_BITCAST: {
                if (st.n < 1) return false;
                unsigned char in_k = st.k[st.n-1];
                if (in_k != K_INT && in_k != K_FLT) return false;
                st.k[st.n-1] = (in->a & 0x10000) ? K_FLT : K_INT;   // 출력 flt 비트
                st.o[st.n-1] = -1; break;
            }
            case IRW_BNEW:
                if (st.n < 1 || st.k[st.n-1] != K_INT) return false;
                st.k[st.n-1] = K_BSET; st.o[st.n-1] = -1; break;
            case IRW_SWAP: {
                if (st.n < 3 || (st.k[st.n-3] != K_SL && st.k[st.n-3] < K_SVIEW) ||   // ★ 2026-11-01 — 구조체 배열도(원소 = 구조체 바이트)
                    st.k[st.n-2] != K_INT || st.k[st.n-1] != K_INT) return false;
                st.n -= 3; st.k[st.n] = K_INT; st.o[st.n] = -1; st.n++; break;   // ★ 0 을 남긴다
            }
            case IRW_SCOPY: {   // ★ 2026-10-31 — (받는 쪽, 주는 쪽) 두 슬라이스(바이트 · 타입 · 구조체 배열 · 줄) → 0. 참조(K_REF)는 태그 경로가 따라간다
                if (st.n < 2) return false;
                for (int z = 1; z <= 2; z++) {
                    unsigned char kk_ = st.k[st.n - z];
                    if (kk_ != K_SL && kk_ != K_ROWS && kk_ < K_SVIEW) return false;
                }
                st.n -= 2; st.k[st.n] = K_INT; st.o[st.n] = -1; st.ve[st.n] = 0; st.vn[st.n] = 0; st.fl[st.n] = 0; st.n++; break;
            }
            case IRW_XCHK: {    // ★ REQ-0036 — 두 조각(바이트 · 타입 · 구조체 배열 · 줄 · 뷰) → 0. 겹치면 멈춘다(이미터와 짝)
                if (st.n < 2) return false;
                for (int z = 1; z <= 2; z++) {
                    unsigned char kk_ = st.k[st.n - z];
                    if (kk_ != K_SL && kk_ != K_ROWS && kk_ < K_VIEW) return false;
                }
                st.n -= 2; st.k[st.n] = K_INT; st.o[st.n] = -1; st.ve[st.n] = 0; st.vn[st.n] = 0; st.fl[st.n] = 0; st.n++; break;
            }
            case IRW_SUBSLICE: {
                // ★ 2026-10-31 (W-20261031-1118) — **구조체 원소 슬라이스의 조각**: 보폭은 그 배치의 크기다. 종류(어느 배치인가)는 그대로다.
                if (st.n >= 3 && st.k[st.n-3] >= K_SVIEW && st.k[st.n-2] == K_INT && st.k[st.n-1] == K_INT) {
                    if (!ir->structs[st.k[st.n-3] - K_SVIEW].total) return false;
                    st.n -= 2; st.o[st.n-1] = -1; break;
                }
                // ★ 2026-11-03 (W-20261103-1143) — 할당기 바이트 위 그림자의 뷰를 바이트로 자른다(`drop n. use al.` 이 돌려줄 바이트 — VM 과 같은 바이트 슬라이스)
                if (st.n >= 3 && st.k[st.n-3] >= K_VIEW && st.k[st.n-3] < K_SVIEW && ir->structs[st.k[st.n-3] - K_VIEW].is_shadow &&
                    st.k[st.n-2] == K_INT && st.k[st.n-1] == K_INT) {
                    st.n -= 2; st.k[st.n-1] = K_SL; st.ve[st.n-1] = 1; st.vn[st.n-1] = 0; st.fl[st.n-1] = 0; st.o[st.n-1] = -1; break;
                }
                if (st.n < 3 || st.k[st.n-3] != K_SL ||
                    st.k[st.n-2] != K_INT || st.k[st.n-1] != K_INT) return false;
                // ★★★★★ **원소 폭이 1 이 아니어도 된다** (2026-08-19). 여기서 거절하면
                //   `subslice` 를 쓰는 op 이 **통째로** 해석기로 떨어진다 — 실측으로
                //   `lib/ed25519.low` 12 op · `tests/prog/lockbox.low` 17 op 이 그랬다
                //   (크립토는 속도가 뜻을 갖는 자리다). 색인이 이미 `i·esz` 로 도는데
                //   부분 슬라이스만 못 할 이유가 없다: **포인터 산술의 같은 곱셈**이다.
                //   ☞ `.n` 은 **원소 수**이므로 경계 검사는 그대로고, 곱하는 것은 포인터뿐이다.
                unsigned char ez = st.ve[st.n-3];
                if (ez == 0) return false;              // 폭을 모르면 곱할 수가 없다
                st.n -= 2; st.k[st.n-1] = K_SL; st.ve[st.n-1] = ez; st.o[st.n-1] = -1; break;
            }
            case IRW_SNEW:
                if (st.n < 1 || st.k[st.n-1] != K_INT) return false;
                st.k[st.n-1] = K_STK; st.o[st.n-1] = -1; break;
            case IRW_SPUSH:
                // `push s x` — s 의 **집**을 알아야 한다.
                if (st.n < 2 || st.k[st.n-2] != K_STK || st.k[st.n-1] != K_INT) return false;
                if (st.o[st.n-2] < 0) return false;
                st.n--; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_SPOP_INTO:
                if (st.n < 1 || st.k[st.n-1] != K_STK) return false;
                if (st.o[st.n-1] < 0) return false;
                if (in->a >= CBE_LOCMAX) return cbe_locals_over(in->a);
                if (slot[in->a] == 0xff) { slot[in->a] = K_INT; lk[in->a] = K_INT; }
                else if (slot[in->a] != K_INT) return false;   // 스택의 원소는 **정수**다
                st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_CONTAINS:
                if (st.n < 2 || st.k[st.n-2] != K_BSET || st.k[st.n-1] != K_INT) return false;
                st.n--; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_BREMOVE:
                // ★ `remove b k` 는 **b 를 바꾼다** — add 와 똑같이 **출신 지역**을 알아야 내릴 수 있다.
                //   (값 복사본에서 비트를 지우면 그 변경이 사라진다 — VM 은 제자리 변이라 갈렸다.)
                if (st.n < 2 || st.k[st.n-2] != K_BSET || st.k[st.n-1] != K_INT) return false;
                if (st.o[st.n-2] < 0) return false;
                st.n--; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_BUNION: case IRW_BINTER: case IRW_BDIFF:
                if (st.n < 2 || st.k[st.n-2] != K_BSET || st.k[st.n-1] != K_BSET) return false;
                st.n--; st.k[st.n-1] = K_BSET; st.o[st.n-1] = -1; break;
            case IRW_BSUBSET:
                if (st.n < 2 || st.k[st.n-2] != K_BSET || st.k[st.n-1] != K_BSET) return false;
                st.n--; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_BEMPTY:
                if (st.n < 1 || st.k[st.n-1] != K_BSET) return false;
                st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_BCOMPL:
                if (st.n < 1 || st.k[st.n-1] != K_BSET) return false;
                st.k[st.n-1] = K_BSET; st.o[st.n-1] = -1; break;
            case IRW_COUNT:
                if (st.n < 1) return false;
                if (st.k[st.n-1] == K_STK) { if (st.o[st.n-1] < 0) return false; }
                else if (st.k[st.n-1] != K_BSET) return false;
                st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_STR: {   // ★ 2026-11-01 — 원소 폭은 방출과 같은 자리에서(접두 리터럴 `u"…"` 은 2) · 자리에 남은 옛 표식을 지운다
                const proven_u8 ew_ = ir->strew ? ir->strew[in->a] : 1;
                st.o[st.n] = -1; st.ve[st.n] = ew_ ? ew_ : 1; st.vn[st.n] = 0; st.fl[st.n] = 0; st.k[st.n++] = K_SL; break; }
            // ★★ 2026-10-31 (W-20261031-1836) — **틀 안 나열 자리**: 이 op 의 C 틀의 바이트 줄 한 구간(0 으로 채우거나 상수 본을 베낀다) — 쓸 수 있는 바이트 슬라이스.
            //   예산은 틀 창 몫(wbytes)과 함께 센다(아래 cbe_scalar_body — VM 과 같은 수).
            case IRW_BFILL:   // 받은 바이트 슬라이스 → 같은 자리의 앞 len 바이트(길이가 모자라면 멈춘다)
                if (st.n < 1 || st.k[st.n-1] != K_SL || st.ve[st.n-1] != 1) return false;
                st.o[st.n-1] = -1; st.fl[st.n-1] = 0; break;
            case IRW_LBUF: case IRW_LBUFC:
                if (!d->lbuf_size) return false;
                st.o[st.n] = -1; st.ve[st.n] = 1; st.vn[st.n] = 0; st.fl[st.n] = 0; st.k[st.n++] = K_SL; break;
            // ★ 기저 주소 위의 블록도 **바이트 슬라이스**다 — 뒤이은 IRW_VIEW 가 그것을 먹는다.
            case IRW_RESBLK:
            case IRW_MMIOBLK: st.o[st.n] = -1; st.ve[st.n] = 1; st.k[st.n++] = K_SL; break;
            case IRW_VIEW: {
                // ★ 뷰는 **바이트 슬라이스 위**에 선다 ⇒ 이 def 의 슬라이스는 바이트여야 한다.
                if (st.n < 1 || st.k[st.n-1] != K_SL || st.ve[st.n-1] != 1) {
                    low_bfmt(g_sub, sizeof g_sub, "a view over kind {} (element width {}) — views stand on bytes", PROVEN_ARG((unsigned)(st.n ? st.k[st.n-1] : 0)), PROVEN_ARG((unsigned)(st.n ? st.ve[st.n-1] : 0))); return false; }
                int sx = (int)(in->a & 0xffff);
                if (!cbe_view_ok(ir, sx) || !ir->structs[sx].viewable) {   // ★ 뷰는 8필드·중첩 제약이 없다
                    low_bfmt(g_sub, sizeof g_sub, "a view of `{}`, which the fast path does not read as bytes{}", low_sv((const char *)ir->structs[sx].name.ptr, (int)ir->structs[sx].name.size), proven_arg_cstr(ir->structs[sx].is_shadow ? " (an allocator layout)" : "")); return false; }
                st.k[st.n-1] = (unsigned char)(K_VIEW + sx); st.o[st.n-1] = -1; break;
            }
            case IRW_TRYVIEW: {
                // ★ try_view → some(view) | none. 뷰를 **감싼 option**(lw_r.s)이 된다.
                //   내부 종류 표식: ve=sidx+1, fl=1(뷰). 스칼라/레코드 option 과 구분된다.
                if (st.n < 1 || st.k[st.n-1] != K_SL || st.ve[st.n-1] != 1) return false;
                int sx = (int)(in->a & 0xffff);
                if (!cbe_view_ok(ir, sx) || !ir->structs[sx].viewable) return false;
                st.k[st.n-1] = K_OPT; st.ve[st.n-1] = (unsigned char)(sx + 1); st.fl[st.n-1] = 1;
                st.o[st.n-1] = -1; break;
            }
            case IRW_EXTERN: {
                // ★ 가변인자 씨 호출(호출 지점, argc>0)은 인자가 **스택**에 있다 — 빠른 경로의 슬롯
                //   모델과 안 맞는다. **태그 경로로 내린다**(가변인자 printf 는 성능 자리가 아니다).
                if ((proven_u64)in->a >> 16) return false;
                // ★ extern.call — 파라미터를 **직접 읽어** C 함수를 부른다(스택에서 꺼내지 않는다).
                //   반환은 long long(K_INT). 파라미터 타입은 스칼라/부동/슬라이스/cap 만 허용.
                const low_ir_def_t *E = &ir->defs[in->a & 0xffff];
                for (proven_size_t q = 0; q < E->nparams && q < LOW_MAX_PARAMS; q++) {
                    if ((E->param_cap >> q) & 1u) continue;
                    if ((E->param_struct >> q) & 1u || (E->param_opt >> q) & 1u
                        || (E->param_vec >> q) & 1u || (E->param_bset >> q) & 1u
                        || (E->param_ufn >> q) & 1u) return false;  // C 로 안 편다(태그 경로 — 콜백은 lowv .p)
                }
                // ★ f64 반환 → 부동 스택, 정수 반환 → 정수 스택. 생 포인터(cstr)는 **슬라이스 칸**(RFC-0137 P3 — 포인터를 정수에 안 싣는다)
                if (E->out_ptr) { st.o[st.n] = -1; st.ve[st.n] = 1; st.fl[st.n] = 0; st.k[st.n++] = K_SL; break; }
                st.o[st.n] = -1; st.k[st.n++] = cbe_ret_flt(E) ? K_FLT : K_INT; break;
            }
            case IRW_CSTR2STR:
                // ★★★ **cstr(포인터 스칼라) → str(바이트 슬라이스)** (RFC-0068 S4 · C2).
                if (st.n < 1 || st.k[st.n-1] != K_SL) return false;   // ★ RFC-0137 P3 — cstr 는 슬라이스 칸으로 들어온다
                st.k[st.n-1] = K_SL; st.ve[st.n-1] = 1; st.o[st.n-1] = -1; break;
            case IRW_STR2CSTR:
                // ★★★ **str_buf(바이트 슬라이스) → cstr(포인터 스칼라)** (RFC-0068 S4 · C4).
                if (st.n < 1 || st.k[st.n-1] != K_SL) return false;   // 슬라이스여야(구조체 뷰는 태그 경로로)
                st.k[st.n-1] = K_SL; st.ve[st.n-1] = 1; st.o[st.n-1] = -1; break;   // ★ RFC-0137 P3 — cstr 도 슬라이스 칸
            case IRW_FNREF: return false;   // ★ 콜백 참조는 lowv .p 에 심볼 주소를 담는다 — 태그 경로로
            // ★ asm 문장(RFC-0042 D11) — 값은 **없다**(정수 0 을 민다). 피연산자가 있으면 여기 못 온다.
            case IRW_ASM:
                if ((proven_size_t)in->a < ir->nasms && ir->asms[in->a].nops > 0) return false;
                st.o[st.n] = -1; st.k[st.n++] = K_INT; break;
            case IRW_DROP:  if (st.n < 1) return false; st.n--; break;
            // ★★★ 2a — 이 op 이 띄운 액터 앞으로 넣는다 · 그 액터를 비운다 · 우편함 전체를 비운다. 받는 값은 단위(정수 0)다.
            case IRW_ASEND: case IRW_DRAIN: {
                const bool snd_ = in->w == IRW_ASEND;
                const proven_size_t c_ = snd_ ? IR_CALL_IDX(in->a) : 0;
                if (snd_ && !cbe_fa_hok(ir, c_, sc, g_sub, sizeof g_sub)) return false;
                const int np_ = snd_ ? (int)ir->defs[c_].nparams : 1;
                if (st.n < np_) return false;
                const int b_ = st.n - np_;
                const unsigned char ak_ = st.k[b_];
                if (!(ak_ >= K_REC && ak_ < K_VIEW) || !ir->structs[ak_ - K_REC].is_actor_state ||
                    (snd_ && ak_ != (unsigned char)(K_REC + ir->defs[c_].param_sidx[0]))) {
                    low_bfmt(g_sub, sizeof g_sub, "`{}` on an actor the fast path does not hold as a record", proven_arg_cstr(snd_ ? "spawn send" : "drain")); return false; }
                if (!(st.fl[b_] & CBE_FL_NOID) || st.o[b_] < 0 || st.o[b_] >= 64 || (proven_size_t)st.o[b_] < d->nparams) {
                    low_bfmt(g_sub, sizeof g_sub, "`{}` on an actor this op did not spawn into a local of its own", proven_arg_cstr(snd_ ? "spawn send" : "drain")); return false; }
                if (!snd_ && cbe_struct_has_sl(ir, ak_ - K_REC)) { low_bfmt(g_sub, sizeof g_sub, "`drain` on an actor whose state has a slice field"); return false; }
                for (int q = 1; q < np_; q++) if (st.k[b_ + q] != K_INT) { low_bfmt(g_sub, sizeof g_sub, "`spawn send` argument {} is not an integer", PROVEN_ARG((int)(q))); return false; }
                faloc_ |= 1ull << st.o[b_];
                if (snd_ && c_ < 512) g_qthunk[c_] = true;
                st.n = b_;
                st.o[st.n] = -1; st.ve[st.n] = 0; st.vn[st.n] = 0; st.fl[st.n] = 0; st.k[st.n++] = K_INT; break;
            }
            case IRW_SCHED: st.o[st.n] = -1; st.ve[st.n] = 0; st.vn[st.n] = 0; st.fl[st.n] = 0; st.k[st.n++] = K_INT; break;
            case IRW_WRAP_SOME: case IRW_WRAP_OK:
                if (st.n < 1) return false;
                // ★★★ option·result 는 **레코드**도 감쌀 수 있다 — lw_r 에 .r[8] 슬롯이 있다.
                //   내부 종류를 ve 에 새긴다(레코드면 sidx+1, 스칼라면 0). parse_header 가
                //   `ok make header` 로 정확히 이 모양이다(result<record, error>).
                if (st.k[st.n-1] >= K_REC && st.k[st.n-1] < K_VIEW) {
                    // ★★★★★ **감싼 레코드의 슬라이스 칸은 아직 안 맞는다** (2026-08-30, WO-0158).
                    //   `lw_r` 에 평행 칸(`rsl`)을 붙이고 wrap/unwrap 이 그것을 나르게 했지만,
                    //   열어 보니 스윕이 **`slice index out of bounds`** 로 답했다 —
                    //   `lw_r` 는 **값으로 오간다**(레지스터 두 개가 설계였다). 64 바이트에
                    //   16 칸짜리 슬라이스 배열을 더하면 그 값이 커지고, 감싼 값이 호출을
                    //   건널 때 칸이 **어디까지 살아 있는지**가 아직 정리되지 않았다.
                    //   ⇒ §4 규율대로 관문을 닫아 둔다. 평행 칸 자체는 남겨 둔다 —
                    //     다음 단계가 그 위에서 시작한다.
                    //   ☞ *값으로 오가는 것에 칸을 더하면, 그 값이 지나는 모든 자리를
                    //     함께 세어야 한다 — 프레임 안에서만 맞으면 아직 맞은 것이 아니다.*
                    // ★★ 2026-11-01 (W-20261031-2053) — 연다. 칸이 지나는 자리를 다 셌다: 싸기 · 지역에 담기/꺼내기(`wssl` ↔ `wlocsl`) ·
                    //   벗기기(→ `rssl`) · 돌려주기(→ 스레드마다 하나인 `lw_ret_rsl`) · 돌려받기(→ `wssl`). `lw_r` 에 칸을 붙이지 않는다.
                    if (!cbe_rec_ok(ir, st.k[st.n-1] - K_REC) && !cbe_rec_slice_ok(ir, st.k[st.n-1] - K_REC)) return false;
                    if (st.fl[st.n-1] & CBE_FL_NOID) { low_bfmt(g_sub, sizeof g_sub, "`some` of an actor spawned in this op"); return false; }
                    unsigned char rmk = (unsigned char)(st.k[st.n-1] - K_REC + 1);
                    // ★ 레코드-감싼 option/result 는 뷰가 아니다 → fl=0(이미터 WRAP_SOME/OK 와 일치시켜야
                    //   자격 판정과 코드생성이 같은 상태를 본다 — some_value 뷰-오라우팅 SEGV 수정).
                    st.k[st.n-1] = K_OPT; st.ve[st.n-1] = rmk; st.fl[st.n-1] = 0; st.o[st.n-1] = -1; break;
                }
                // ★★ 2026-11-01 (W-20261101-1828) — **구조체 뷰를 감싼다**(`some v` — 할당기 바이트 위에 지은 마디 · 트리 칸). `try.view` 가 이미 내는
                //   모양이다(ve = sidx+1 · fl = 1 · 값은 `ws[].s` 의 (포인터, 길이)). option 만.
                if (st.k[st.n-1] >= K_VIEW && st.k[st.n-1] < K_SVIEW && in->w == IRW_WRAP_SOME) {
                    const int vsx_ = st.k[st.n-1] - K_VIEW;
                    if (vsx_ >= 255) return false;
                    st.k[st.n-1] = K_OPT; st.ve[st.n-1] = (unsigned char)(vsx_ + 1); st.fl[st.n-1] = 1; st.vn[st.n-1] = 0; st.o[st.n-1] = -1; break;
                }
                // ★★★★ WO-0222 — **슬라이스를 감싼다**(`some v`). option 만(result 는 아직).
                // ★★ 2026-11-01 (W-20261031-2053) — 원소 폭이 1 이 아닌 슬라이스도(`option slice u32`). 폭은 `vn`(0 = 1), 부호 · 부동 표식은 `fl` 에 싣는다
                //   — `ve` 는 0 으로 둔다(표식 없는 K_OPT 의 none 과 맞추는 규칙들이 `ve` 를 본다).
                if (st.k[st.n-1] == K_SL && in->w == IRW_WRAP_SOME) {
                    const unsigned char w_ = st.ve[st.n-1] ? st.ve[st.n-1] : 1;
                    if (w_ != 1 && w_ != 2 && w_ != 4 && w_ != 8) { low_bfmt(g_sub, sizeof g_sub, "`some` of a slice whose element width is {}", PROVEN_ARG((unsigned)(w_))); return false; }
                    st.k[st.n-1] = K_OPTSL; st.ve[st.n-1] = 0; st.vn[st.n-1] = (unsigned char)(w_ > 1 ? w_ : 0); st.o[st.n-1] = -1; break;
                }
                // ★ 수를 싼 option · result 를 다시 싼다 → 겹(K_OPT2). 안 태그는 `t2` 로 옮긴다.
                if (st.k[st.n-1] == K_OPT && st.ve[st.n-1] == 0 && st.fl[st.n-1] == 0) {
                    st.k[st.n-1] = K_OPT2; st.o[st.n-1] = -1; break; }
                if (st.k[st.n-1] != K_INT) { low_bfmt(g_sub, sizeof g_sub, "`{}` of kind {}", proven_arg_cstr(in->w == IRW_WRAP_OK ? "ok" : "some"), PROVEN_ARG((unsigned)(st.k[st.n-1]))); return false; }
                st.k[st.n-1] = K_OPT; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; st.o[st.n-1] = -1; break;
            case IRW_WRAP_NONE: case IRW_WRAP_ERR:
                st.o[st.n] = -1; st.ve[st.n] = 0; st.fl[st.n] = 0; st.k[st.n++] = K_OPT; break;
            // ★★★★ WO-0222 — 뿌리에서 깎는다: 정수 하나 → option(바이트 슬라이스). 영역의 표식·되감기는 정수 커서다.
            case IRW_ALLOCB:
                if (st.n < 1 || st.k[st.n-1] != K_INT) return false;
                st.k[st.n-1] = K_OPTSL; st.ve[st.n-1] = 0; st.vn[st.n-1] = 0; st.fl[st.n-1] = 0; st.o[st.n-1] = -1; break;
            case IRW_RMARK:
                st.o[st.n] = -1; st.ve[st.n] = 0; st.fl[st.n] = 0; st.k[st.n++] = K_INT; break;
            case IRW_RRESET:
                if (st.n < 1 || st.k[st.n-1] != K_INT) return false;
                st.n--; break;
            case IRW_ISSOME: case IRW_ISOK: case IRW_ISERR:
                if (st.n >= 1 && st.k[st.n-1] == K_OPTSL && in->w == IRW_ISSOME) {
                    st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break; }
                if (st.n < 1 || (st.k[st.n-1] != K_OPT && st.k[st.n-1] != K_OPT2)) return false;
                st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_SOMEVAL: case IRW_OKVAL:
                if (st.n >= 1 && st.k[st.n-1] == K_OPTSL && in->w == IRW_SOMEVAL) {   // ★ WO-0222 — 슬라이스(폭은 vn · 표식은 fl)
                    st.k[st.n-1] = K_SL; st.ve[st.n-1] = st.vn[st.n-1] ? st.vn[st.n-1] : 1; st.vn[st.n-1] = 0; st.o[st.n-1] = -1; break; }
                if (st.n >= 1 && st.k[st.n-1] == K_OPT2) {   // 겹을 한 겹 벗긴다 → 수를 싼 option · result
                    st.k[st.n-1] = K_OPT; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; st.o[st.n-1] = -1; break; }
                if (st.n < 1 || st.k[st.n-1] != K_OPT) return false;
                // ★ 감싼 값을 꺼낸다(ve 표식): fl=1 이면 **뷰**, 아니면 **레코드**, ve=0 이면 정수.
                if (st.ve[st.n-1]) {
                    unsigned char inner = st.fl[st.n-1]
                        ? (unsigned char)(K_VIEW + st.ve[st.n-1] - 1)
                        : (unsigned char)(K_REC + st.ve[st.n-1] - 1);
                    st.k[st.n-1] = inner; st.fl[st.n-1] = 0; st.o[st.n-1] = -1; break;
                }
                st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_ERRVAL:
                if (st.n < 1 || (st.k[st.n-1] != K_OPT && st.k[st.n-1] != K_OPT2)) return false;
                st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_ELSE_NONE: case IRW_ELSE_ERR:
                if (st.n < 1 || st.k[st.n-1] != K_OPT) return false;
                break;
            case IRW_TRY:
                // ★ `try` 는 **오류를 조기 반환**한다 ⇒ 이 op 의 반환은 **감싼 값**이어야 한다.
                if (st.n < 1) return false;
                if (st.k[st.n-1] == K_INT) break;            // 평범한 값에 try = 항등
                if (st.k[st.n-1] != K_OPT) return false;
                st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
            case IRW_MAKE: {
                proven_size_t mk = (proven_size_t)in->a;
                if (mk >= ir->nmakes || mk > 200) return false;
                int sx = cbe_make_sidx(ir, in->a);
                // ★ RFC-0106 단계 3 — 슬라이스 필드를 든 레코드도 **만들 수 있다**.
                if (!cbe_rec_ok_f(ir, sx, true) && !cbe_rec_nest_ok(ir, sx) && !cbe_rec_slice_ok(ir, sx) && !cbe_rec_arr_ok(ir, sx)) return false;
                proven_size_t nf = ir->makes[mk].nfields;
                if (nf != ir->structs[sx].nf && !(ir->structs[sx].is_penum && cbe_penum_ok(ir, sx))) return false;   // 부분 초기화는 안 내린다(짐을 싣는 열거의 갈래 값만 — 다른 갈래 칸은 0)
                for (proven_size_t z = 0; z < nf; z++)
                    if (cbe_slot_by_name(ir, sx, ir->makes[mk].fields[z]) < 0) return false;
                if ((proven_size_t)st.n < nf) return false;
                // ★ 필드의 종류는 **슬롯의 것**과 맞아야 한다: 중첩은 K_REC+sidx, 부동은 K_FLT, 나머지는 K_INT.
                const bool zero_ = cbe_make_zero(ir, d, j, sx, nf);   // `spawn actor` — 칸마다 const 0
                if (zero_ && cbe_rec_arr_ok(ir, sx)) return false;   // ★ 배열 칸을 가진 액터 상태는 태그 경로에 둔다
                if (cbe_abox(ir, d, j, sx, nf)) {   // ★ 태그 쪽 레코드로 짓는다 — 그 자리만 든다(2026-11-01)
                    st.n -= (int)nf; st.o[st.n] = -1; st.ve[st.n] = (unsigned char)(sx + 1); st.vn[st.n] = 0; st.fl[st.n] = 0;
                    st.k[st.n++] = K_AREF;
                    break;
                }
                for (proven_size_t q = 0; q < nf && !zero_; q++) {
                    int sl = cbe_slot_by_name(ir, sx, ir->makes[mk].fields[q]);
                    { const int ob_ = cbe_obox_sidx(ir, &ir->structs[sx].f[sl]);   // ★ 2026-11-01 — 소유 칸: 값 구조체 레코드를 받는다(태그 쪽으로 새로 짓는다)
                      if (ob_ >= 0) {
                          const unsigned char hk_ = st.k[st.n - nf + q];
                          if (hk_ >= K_REC && hk_ < K_VIEW && cbe_struct_same(ir, hk_ - K_REC, ob_) && cbe_make_for_struct(ir, ob_) >= 0) continue;
                          low_bfmt(g_sub, sizeof g_sub, "field `{}` of `{}` owns a struct; the value is kind {}",
                                   low_sv((const char *)ir->makes[mk].fields[q].ptr, (int)ir->makes[mk].fields[q].size),
                                   low_sv((const char *)ir->structs[sx].name.ptr, (int)ir->structs[sx].name.size), PROVEN_ARG((unsigned)(hk_)));
                          return false;
                      } }
                    // ★★ 2026-11-01 — 액터를 가리키는 칸: 칸에서 읽은 것(K_AREF)이나, 제 자리를 가진 액터 상태 레코드를 받는다.
                    { const int as_ = cbe_aref_sidx(ir, &ir->structs[sx].f[sl]);
                      if (as_ >= 0) {
                          const unsigned char hk_ = st.k[st.n - nf + q];
                          if (hk_ == K_AREF && st.ve[st.n - nf + q] == (unsigned char)(as_ + 1)) continue;
                          if (hk_ == (unsigned char)(K_REC + as_)) {
                              if (st.fl[st.n - nf + q] & CBE_FL_NOID) {
                                  low_bfmt(g_sub, sizeof g_sub, "field `{}` of `{}` would hold an actor spawned in this op — the fast path has no tagged handle for it",
                                           low_sv((const char *)ir->makes[mk].fields[q].ptr, (int)ir->makes[mk].fields[q].size),
                                           low_sv((const char *)ir->structs[sx].name.ptr, (int)ir->structs[sx].name.size));
                                  return false; }
                              if (ir->structs[as_].nf > CBE_AID || as_ >= 64 || !g_aid_need[as_]) return false;
                              continue;
                          }
                          low_bfmt(g_sub, sizeof g_sub, "field `{}` of `{}` holds an actor; the value is kind {}",
                                   low_sv((const char *)ir->makes[mk].fields[q].ptr, (int)ir->makes[mk].fields[q].size),
                                   low_sv((const char *)ir->structs[sx].name.ptr, (int)ir->structs[sx].name.size), PROVEN_ARG((unsigned)(hk_)));
                          return false;
                      } }
                    if (ir->structs[sx].f[sl].arrn) {   // ★ 2026-11-01 (W-20261101-1828) — 배열 칸: 그 바이트를 베낀다(길이는 방출이 잰다 — VM 과 같다)
                        if (st.k[st.n - nf + q] == K_SL || st.k[st.n - nf + q] == K_ROWS) continue;   // ★ 줄의 나열도(바이트 수를 방출이 잰다)
                        low_bfmt(g_sub, sizeof g_sub, "array field `{}` of `{}` wants a slice, the value is kind {}",
                                 low_sv((const char *)ir->makes[mk].fields[q].ptr, (int)ir->makes[mk].fields[q].size),
                                 low_sv((const char *)ir->structs[sx].name.ptr, (int)ir->structs[sx].name.size), PROVEN_ARG((unsigned)(st.k[st.n - nf + q])));
                        return false;
                    }
                    { const int no_ = cbe_f_nopt(ir, &ir->structs[sx].f[sl]);   // ★ 2026-11-03 (W-20261103-1143) — 마디를 가리키는 칸: option(그 마디의 뷰) · none
                      if (no_ >= 0) {
                          const int ai_ = st.n - (int)nf + (int)q;
                          if (st.k[ai_] == K_OPT && ((st.ve[ai_] == 0 && st.fl[ai_] == 0) || (st.fl[ai_] == 1 && st.ve[ai_] == (unsigned char)(no_ + 1)))) continue;
                          low_bfmt(g_sub, sizeof g_sub, "field `{}` of `{}` takes an option of a node built in allocator bytes",
                                   low_sv((const char *)ir->makes[mk].fields[q].ptr, (int)ir->makes[mk].fields[q].size),
                                   low_sv((const char *)ir->structs[sx].name.ptr, (int)ir->structs[sx].name.size));
                          return false;
                      } }
                    unsigned char want = ir->structs[sx].f[sl].sidx >= 0
                                         ? (unsigned char)(K_REC + ir->structs[sx].f[sl].sidx)
                                       : (ir->structs[sx].f[sl].boxed && ir->structs[sx].f[sl].elem) ? K_SL
                                       : ir->structs[sx].f[sl].flt ? K_FLT : K_INT;
                    if (st.k[st.n - nf + q] != want) {
                        low_bfmt(g_sub, sizeof g_sub, "field `{}` of `{}` wants kind {}, the value is kind {}",
                                 low_sv((const char *)ir->makes[mk].fields[q].ptr, (int)ir->makes[mk].fields[q].size),
                                 low_sv((const char *)ir->structs[sx].name.ptr, (int)ir->structs[sx].name.size), PROVEN_ARG((unsigned)(want)), PROVEN_ARG((unsigned)(st.k[st.n - nf + q])));
                        return false; }
                }
                st.n -= (int)nf;
                st.o[st.n] = -1; st.ve[st.n] = 0; st.vn[st.n] = 0;
                st.fl[st.n] = zero_ ? CBE_FL_NOID : 0;   // ★ 갓 띄운 액터 — 제 자리(태그 쪽 레코드)가 없다
                st.k[st.n++] = (unsigned char)(K_REC + sx);
                break;
            }
            case IRW_FIELD: {
                if (st.n < 1) return false;
                unsigned char fk = st.k[st.n-1];
                if (fk >= K_VIEW) {   // ★ 뷰 필드는 오프셋으로 읽는다 — 8필드 상한 없음
                    int sx = fk - K_VIEW;
                    int sl = cbe_view_slot(ir, sx, in->a);
                    if (sl < 0) return false;
                    // ★★★ 중첩 구조체 필드를 읽으면 **중첩 뷰**가 된다(오프셋만 더한다).
                    if (ir->structs[sx].f[sl].sidx >= 0) {
                        int isx = (int)ir->structs[sx].f[sl].sidx;
                        st.k[st.n-1] = (unsigned char)(K_VIEW + isx); st.o[st.n-1] = -1; break;
                    }
                    // ★★ 2026-10-31 (W-20261031-1118) — **그림자 배치의 슬라이스 칸**(주소 + 길이)을 읽는다 → 바이트 위의 슬라이스(K_SL).
                    //   번역 때 지은 메시지 표(`field idx MSGS. i . en .`)가 이 꼴이다. 전에는 이 칸도 정수로 보고 다음 `len` 에서 거절했다
                    //   (태그 경로로 갔다 — 틀린 값은 아니었다). 원소가 수인 칸만 연다(구조체 · 줄의 슬라이스 칸은 그대로 태그 경로).
                    {
                        const low_ir_sfield_t *F_ = &ir->structs[sx].f[sl];
                        if (ir->structs[sx].is_shadow && F_->bxs1) {   // ★ 2026-11-02 (W-20261102-0024 O4) — 마디의 주소 칸 → option(그 마디의 뷰)(VM 과 같다 · 0 = none)
                            if (F_->bxs1 - 1 >= 254 || K_VIEW + F_->bxs1 - 1 >= K_SVIEW) return false;
                            st.k[st.n-1] = K_OPT; st.ve[st.n-1] = (unsigned char)F_->bxs1; st.fl[st.n-1] = 1; st.vn[st.n-1] = 0; st.o[st.n-1] = -1;
                            break;
                        }
                        if (F_->bxself) return false;
                        if (F_->arrn) {   // ★ 2026-11-01 — 뷰의 배열 칸: 그 바이트를 보는 슬라이스(VM 의 vm_arr_view — 무복사)
                            if (!cbe_f_arr(F_)) return false;
                            st.k[st.n-1] = cbe_arr_k(F_); st.ve[st.n-1] = F_->arresz; st.vn[st.n-1] = (unsigned char)cbe_arr_rb(F_); st.fl[st.n-1] = cbe_arr_fl(F_); st.o[st.n-1] = -1;
                            break;
                        }
                        if (F_->slmeta) {
                            const unsigned ez_ = (unsigned)(F_->slmeta & 0xff);
                            if (!ir->structs[sx].is_shadow || (F_->slmeta & (IR_STRUCT_BIT | IR_ROW_BIT)) || !ez_ || ez_ > 8) return false;
                            if ((F_->slmeta & IR_FLT_BIT) && ez_ != 4 && ez_ != 8) return false;
                            st.k[st.n-1] = K_SL; st.ve[st.n-1] = (unsigned char)ez_; st.vn[st.n-1] = 0; st.o[st.n-1] = -1;
                            st.fl[st.n-1] = (F_->slmeta & IR_FLT_BIT) ? CBE_FL_FLT : ((F_->slmeta & IR_SGN_BIT) && ez_ < 8) ? CBE_FL_SGN : 0;
                            break;
                        }
                        if (F_->boxed) return false;
                    }
                    st.k[st.n-1] = ir->structs[sx].f[sl].flt ? K_FLT : K_INT;   // ★ 부동 필드 → f64
                    st.o[st.n-1] = -1; break;
                }
                int sx = (fk >= K_REC) ? fk - K_REC : -1;
                if (sx < 0) return false;
                int rsl = cbe_rec_slot(ir, sx, in->a);
                if (rsl < 0) return false;
                { const int ob_ = cbe_obox_sidx(ir, &ir->structs[sx].f[rsl]);   // ★ 2026-11-01 — 소유한 값 구조체 칸: 베낀 레코드
                  if (ob_ >= 0) {
                      if (st.o[st.n-1] >= CBE_LOCMAX) return false;
                      st.k[st.n-1] = (unsigned char)(K_REC + ob_); st.ve[st.n-1] = 0; st.vn[st.n-1] = 0; st.fl[st.n-1] = 0; st.o[st.n-1] = -1;
                      break;
                  } }
                { const int no_ = cbe_f_nopt(ir, &ir->structs[sx].f[rsl]);   // ★ 2026-11-03 — 마디를 가리키는 칸: 주소 → option(마디의 뷰)
                  if (no_ >= 0) {
                      st.k[st.n-1] = K_OPT; st.ve[st.n-1] = (unsigned char)(no_ + 1); st.fl[st.n-1] = 1; st.vn[st.n-1] = 0; st.o[st.n-1] = -1;
                      break;
                  } }
                { const int as_ = cbe_aref_sidx(ir, &ir->structs[sx].f[rsl]);   // ★ 2026-11-01 — 액터를 가리키는 칸
                  if (as_ >= 0) {
                      if (st.o[st.n-1] >= CBE_LOCMAX) return false;
                      st.k[st.n-1] = K_AREF; st.ve[st.n-1] = (unsigned char)(as_ + 1); st.vn[st.n-1] = 0; st.fl[st.n-1] = 0; st.o[st.n-1] = -1;
                      break;
                  } }
                // ★★★ **슬라이스 필드를 읽는다** (RFC-0106 단계 1). 그 값은 바이트 버퍼가
                //   아니라 **평행 칸**에 산다 ⇒ 어느 **파라미터**의 레코드인지 알아야 한다.
                //   모르면(지역에서 만든 레코드 등) 내리지 않는다 — 단계 3 의 몫이다.
                if (ir->structs[sx].f[rsl].arrn) {   // ★ 2026-11-01 (W-20261101-1828) — 배열 칸: 그 지역 레코드의 바이트를 보는 슬라이스
                    // 지역에 든 레코드만 — 스택 임시(`rsb`)의 바이트는 다음 푸시가 덮는다. 구획은 검사층이 블록 밖으로 못 나가게 한다(E-LIT-ESCAPE).
                    if (!cbe_f_arr(&ir->structs[sx].f[rsl]) || st.o[st.n-1] < 0 || st.o[st.n-1] >= CBE_LOCMAX) {
                        low_bfmt(g_sub, sizeof g_sub, "an array field read from a record that no local holds"); return false; }
                    st.k[st.n-1] = cbe_arr_k(&ir->structs[sx].f[rsl]); st.ve[st.n-1] = ir->structs[sx].f[rsl].arresz;
                    st.vn[st.n-1] = (unsigned char)cbe_arr_rb(&ir->structs[sx].f[rsl]);
                    st.fl[st.n-1] = cbe_arr_fl(&ir->structs[sx].f[rsl]); st.o[st.n-1] = -1;
                    break;
                }
                if (ir->structs[sx].f[rsl].boxed) {
                    if (!ir->structs[sx].f[rsl].elem) return false;
                    // ★ 출신이 지역이면 `rlocsl`, 방금 만든 것(-1)이면 `rssl` 에서 읽는다(단계 3).
                    if (st.o[st.n-1] >= CBE_LOCMAX) return false;
                    st.k[st.n-1] = K_SL;
                    st.ve[st.n-1] = ir->structs[sx].f[rsl].elem;
                    st.vn[st.n-1] = 0; st.fl[st.n-1] = cbe_sf_fl(&ir->structs[sx].f[rsl]); st.o[st.n-1] = -1;
                    break;
                }
                st.k[st.n-1] = ir->structs[sx].f[rsl].sidx >= 0
                               ? (unsigned char)(K_REC + ir->structs[sx].f[rsl].sidx)   // ★ 중첩 레코드 필드
                             : ir->structs[sx].f[rsl].flt ? K_FLT : K_INT;              // ★ 부동 필드 → f64
                st.o[st.n-1] = -1; break;
            }
            case IRW_FSTORE: {
                if (st.n < 2) return false;
                // ★★★★★ RFC-0106 단계 4 — **슬라이스 필드에 슬라이스를 쓴다.**
                //   값이 슬라이스이고 슬롯이 슬라이스 필드면 평행 칸에 담는다.
                //   ★★★★★ **여기를 열어 보았고 스윕이 되돌리게 했다** (2026-08-30, WO-0158).
                //   슬라이스 필드에 쓰면 태그 경로 999(-73)까지 내려갔지만 스윕이
                //   **`len needs a slice`** 로 답했다: 평행 칸에 담은 슬라이스가 **경계로
                //   되나갈 때** 아직 태그 값으로 복원되지 않는 자리가 있다(레코드가 그
                //   함수를 넘어 흐를 때). ⇒ §4 규율대로 닫아 둔다.
                //   ☞ *쓰기는 읽기의 거울이 아니다 — 쓴 값은 **그 함수를 넘어 살아야** 하고,
                //     그때 필요한 것은 칸이 아니라 **경계에서의 복원**이다.*
                // ★★★★★ **쓰기는 아직 닫혀 있다** (2026-08-30, WO-0159).
                //   반환 경계의 복원(`cbe_emit_rec_recon`)을 실은 뒤 다시 열어 봤지만
                //   스윕이 여전히 **`len needs a slice`** 로 답했다. 즉 나가는 길이
                //   **반환만이 아니다** — 쓴 레코드는 `mut` 파라미터의 되쓰기로도,
                //   호출 인자로도 나간다. 그 세 자리가 **같은 복원**을 공유해야 한다.
                //   ⇒ §4 규율대로 닫아 둔다. 열면 태그 경로 1072 → **999**(-73).
                //   ☞ *나가는 길이 셋이면 복원도 셋이다. 하나만 놓으면 나머지 둘에서 샌다.*
                //   ★★★★★ **세 번째 나가는 길이 남았다** (2026-08-30, WO-0160).
                //   반환 복원(WO-0159)과 **호출 인자**(이 커밋)를 이은 뒤 다시 열어 봤지만
                //   스윕이 `vm_allocbytes.low:borrowed` 에서 **`len needs a slice`** 를 냈다 —
                //   **actor 상태**에 쓴 슬라이스가 그 핸들러를 넘어 나갈 때 복원이 없다.
                //   ⇒ §4 규율대로 닫아 둔다. 열면 태그 경로 1072 → **999**(-73).
                //   ☞ *나가는 길을 하나씩 이으면, 남은 길이 **정확히 하나씩** 드러난다 —
                //     그것이 이 되돌림들이 낭비가 아닌 까닭이다.*
                // ★★★★★ **네 번째로 연다** (2026-08-31, WO-0162) — 나가는 세 길을 다 이었다.
                //   이미터는 진작 준비돼 있었다(`rlocsl[..][..] = ss[--ssp]`). 거절한 것은
                //   **종류 시뮬레이터**뿐이다 — 자격과 방출이 갈리면 진단이 엉뚱한 데를 가리킨다.
                if (st.k[st.n-1] == K_ROWS) {   // ★ 2026-11-02 (W-20261102-0024) — 줄의 나열을 배열 안의 배열 칸에 통째로 쓴다(바이트 수가 같아야 — 방출이 잰다)
                    if (st.k[st.n-2] >= K_REC && st.k[st.n-2] < K_VIEW) {
                        const int sxr_ = st.k[st.n-2] - K_REC, slr_ = cbe_rec_slot(ir, sxr_, in->a);
                        if (slr_ >= 0 && ir->structs[sxr_].f[slr_].arrn && cbe_f_arr(&ir->structs[sxr_].f[slr_]) && st.o[st.n-2] >= 0) { st.n -= 2; break; }
                    } else if (st.k[st.n-2] >= K_VIEW && st.k[st.n-2] < K_SVIEW) {
                        const int vsr_ = st.k[st.n-2] - K_VIEW, slr_ = cbe_view_slot(ir, vsr_, in->a);
                        if (slr_ >= 0 && ir->structs[vsr_].f[slr_].arrn && cbe_f_arr(&ir->structs[vsr_].f[slr_])) { st.n -= 2; break; }
                    }
                    return false;
                }
                if (st.k[st.n-1] == K_SL && st.k[st.n-2] >= K_REC && st.k[st.n-2] < K_VIEW) {
                    int sxw_ = st.k[st.n-2] - K_REC;
                    int slw_ = cbe_rec_slot(ir, sxw_, in->a);
                    if (slw_ < 0) return false;
                    if (ir->structs[sxw_].f[slw_].arrn) {   // ★ 2026-11-01 — 배열 칸에 쓰기: 제자리에 베낀다(지역에 든 레코드만)
                        if (!cbe_f_arr(&ir->structs[sxw_].f[slw_]) || st.o[st.n-2] < 0) return false;
                        st.n -= 2; break;
                    }
                    // ★ 슬라이스 필드여야 하고, **원소 폭을 알아야** 한다(모르면 되돌릴 수 없다).
                    if (!ir->structs[sxw_].f[slw_].boxed || !ir->structs[sxw_].f[slw_].elem) return false;
                    // ★ 어느 **지역**인지 알아야 `rlocsl` 에 쓴다 — 이미터가 그 번호를 쓴다.
                    if (st.o[st.n-2] < 0) return false;
                    st.n -= 2; break;
                }
                // ★ 2026-11-01 — 그림자 뷰의 슬라이스 칸에 슬라이스를 쓴다: (주소, 길이) 두 낱말(VM 과 같다 — 길이는 원소 수)
                if (st.k[st.n-1] == K_SL && st.k[st.n-2] >= K_VIEW && st.k[st.n-2] < K_SVIEW) {
                    const int vsx_ = st.k[st.n-2] - K_VIEW;
                    const int vsl_ = cbe_view_slot(ir, vsx_, in->a);
                    if (vsl_ >= 0 && cbe_f_arr(&ir->structs[vsx_].f[vsl_])) { st.n -= 2; break; }   // ★ 뷰의 배열 칸에 쓰기 — 제자리에 베낀다
                    if (vsl_ < 0 || !ir->structs[vsx_].is_shadow || !ir->structs[vsx_].f[vsl_].slmeta) return false;
                    const unsigned ez_ = (unsigned)(ir->structs[vsx_].f[vsl_].slmeta & 0xff);
                    if ((st.ve[st.n-1] ? st.ve[st.n-1] : 1) != ez_) { low_bfmt(g_sub, sizeof g_sub, "a slice of element width {} into a slice field of width {}", PROVEN_ARG((unsigned)(st.ve[st.n-1])), PROVEN_ARG((unsigned)(ez_))); return false; }
                    st.n -= 2; break;
                }
                // ★ 2026-11-02 (W-20261102-0024 O4) — 마디의 주소 칸에 option(마디의 뷰) · none 을 쓴다(주소 한 칸 · none = 0 — VM 과 같다)
                if (st.k[st.n-1] == K_OPT && st.k[st.n-2] >= K_VIEW && st.k[st.n-2] < K_SVIEW) {
                    const int vsx_ = st.k[st.n-2] - K_VIEW, vsl_ = cbe_view_slot(ir, vsx_, in->a);
                    if (vsl_ < 0 || !ir->structs[vsx_].is_shadow || !ir->structs[vsx_].f[vsl_].bxs1) return false;
                    const bool none_ = st.ve[st.n-1] == 0 && st.fl[st.n-1] == 0, node_ = st.fl[st.n-1] == 1 && st.ve[st.n-1] == ir->structs[vsx_].f[vsl_].bxs1;
                    if (!none_ && !node_) { low_bfmt(g_sub, sizeof g_sub, "a node field takes an option of a node built in allocator bytes"); return false; }
                    st.n -= 2; break;
                }
                // ★ 2026-11-03 (W-20261103-1143) — 레코드의 «마디를 가리키는 칸»에 option(마디의 뷰) · none 을 쓴다(지역에 든 레코드 — 바이트 자리에 주소)
                if (st.k[st.n-1] == K_OPT && st.k[st.n-2] >= K_REC && st.k[st.n-2] < K_VIEW) {
                    const int sxn_ = st.k[st.n-2] - K_REC, sln_ = cbe_rec_slot(ir, sxn_, in->a);
                    const int no_ = sln_ >= 0 ? cbe_f_nopt(ir, &ir->structs[sxn_].f[sln_]) : -1;
                    if (no_ < 0 || st.o[st.n-2] < 0) return false;
                    const bool none_ = st.ve[st.n-1] == 0 && st.fl[st.n-1] == 0, node_ = st.fl[st.n-1] == 1 && st.ve[st.n-1] == (unsigned char)(no_ + 1);
                    if (!none_ && !node_) { low_bfmt(g_sub, sizeof g_sub, "a node field takes an option of a node built in allocator bytes"); return false; }
                    st.n -= 2; break;
                }
                if (st.k[st.n-1] != K_INT) return false;
                unsigned char fk = st.k[st.n-2];
                if (fk >= K_VIEW) {
                    // ★ 뷰에 쓰는 것은 **바이트 버퍼에 쓰는 것**이다 — 별칭이 없다(포인터가 곧
                    //   그 자리다). 그래서 출신을 안 따져도 된다.
                    int vsl = cbe_view_slot(ir, fk - K_VIEW, in->a);
                    if (vsl < 0) return false;
                    if (ir->structs[fk - K_VIEW].f[vsl].sidx >= 0) return false;  // 중첩 필드에 정수 쓰기는 안 낸다
                    if (ir->structs[fk - K_VIEW].f[vsl].flt) return false;       // ★ 부동 칸에 쓰기 — 방출은 정수 스택에서 꺼낸다(안 낸다 · 2026-10-31)
                    st.n -= 2; break;
                }
                if (fk < K_REC) return false;
                if (cbe_rec_slot(ir, fk - K_REC, in->a) < 0) return false;
                if (ir->structs[fk - K_REC].f[cbe_rec_slot(ir, fk - K_REC, in->a)].arrn) return false;   // 배열 칸에 수는 안 쓴다
                // ★ 어느 **지역**의 레코드인지 알아야 한다 — 거기에 직접 쓴다.
                if (st.o[st.n-2] < 0) return false;
                st.n -= 2; break;
            }
            case IRW_PANIC:  st.n = 0; live = false; break;   // ★ 2026-11-01 — `panic` 은 돌아오지 않는다: 뒤의 `drop` 은 도달 불가(방출도 같다)
            case IRW_CAST:
                if (st.n < 1) return false;
                if ((in->a & 0xff) == 0) {   // → f64 · f32(IR_CAST_F32)
                    if (st.k[st.n-1] != K_INT && st.k[st.n-1] != K_FLT) return false;
                    st.k[st.n-1] = K_FLT;
                } else if (in->a & 0x40000) {   // narrow_try → option
                    if (st.k[st.n-1] != K_INT && st.k[st.n-1] != K_FLT) return false;
                    st.k[st.n-1] = K_OPT; st.ve[st.n-1] = 0; st.vn[st.n-1] = 0; st.fl[st.n-1] = 0;   // ★ 수를 싼 option — 자리에 남은 옛 표식을 지운다(2026-11-01)
                } else {            // → 정수 폭
                    if (st.k[st.n-1] != K_INT && st.k[st.n-1] != K_FLT) return false;
                    st.k[st.n-1] = K_INT;
                }
                st.o[st.n-1] = -1; break;
            case IRW_INDEX:
                if (st.n < 2 || st.k[st.n-1] != K_INT) return false;
                if (st.k[st.n-2] == K_ROWS) {   // 줄 하나 — 그 줄을 보는 슬라이스(원소 폭 · 표식은 그대로)
                    const unsigned sub_ = CBE_FL_RSUB(st.fl[st.n-2]);
                    if (sub_) {   // ★ 2026-11-02 — 줄이 다시 줄의 나열이다: 아래 줄의 나열(K_ROWS · 그 줄 꼴)
                        if (sub_ >= ir->nrowsh || !ir->rowsh_rb[sub_] || ir->rowsh_rb[sub_] > 255 || ir->rowsh_sub[sub_] > 63) return false;
                        st.n--; st.vn[st.n-1] = (unsigned char)ir->rowsh_rb[sub_];
                        st.fl[st.n-1] = (unsigned char)((st.fl[st.n-1] & CBE_FL_MARK) | (ir->rowsh_sub[sub_] << 2)); st.o[st.n-1] = -1;
                        break;
                    }
                    st.n--; st.k[st.n-1] = K_SL; st.vn[st.n-1] = 0; st.o[st.n-1] = -1;
                    break;
                }
                if (st.k[st.n-2] >= K_SVIEW) {
                    // ★★★ 구조체 배열의 원소는 **구조체 뷰**다(무복사) — sidx 를 물려준다.
                    int sx = st.k[st.n-2] - K_SVIEW;
                    st.n--; st.k[st.n-1] = (unsigned char)(K_VIEW + sx); st.o[st.n-1] = -1;
                    break;
                }
                if (st.k[st.n-2] != K_SL) return false;
                // ★ float 원소 슬라이스(view_array f32/f64) → 결과는 **부동 스칼라**(K_FLT·fs 스택).
                //   이미터가 fs 로 내니 시뮬레이터도 K_FLT 로 봐야 자격/kind 가 일치(안 그러면 VM≠native).
                if (st.fl[st.n-2] == CBE_FL_FLT) { st.n--; st.k[st.n-1] = K_FLT; st.fl[st.n-1] = 0; st.ve[st.n-1] = 0; break; }
                st.n--; st.k[st.n-1] = K_INT; st.fl[st.n-1] = 0; st.ve[st.n-1] = 0; break;
            case IRW_ISTORE:
                             // ★ 2026-11-01 (W-20261101-1150) — 부동 원소 슬라이스에 부동 값을 쓴다(지역 나열 · `map` 의 결과 칸).
                             if (st.n >= 3 && st.k[st.n-3] == K_SL && st.k[st.n-2] == K_INT && st.k[st.n-1] == K_FLT &&
                                 st.fl[st.n-3] == CBE_FL_FLT && (st.ve[st.n-3] == 4 || st.ve[st.n-3] == 8)) { st.n -= 3; break; }
                             // ★ 2026-11-01 — 구조체 배열의 한 칸에 같은 구조체 값(레코드 · 뷰)을 쓴다: 그 바이트를 베낀다(배치가 같다).
                             if (st.n >= 3 && st.k[st.n-3] >= K_SVIEW && st.k[st.n-2] == K_INT) {
                                 const int ex_ = st.k[st.n-3] - K_SVIEW;
                                 const unsigned char vk_ = st.k[st.n-1];
                                 if (!ir->structs[ex_].is_shadow && cbe_rec_ok(ir, ex_) &&
                                     (vk_ == (unsigned char)(K_REC + ex_) || vk_ == (unsigned char)(K_VIEW + ex_))) { st.n -= 3; break; }
                             }
                             if (st.n < 3 || st.k[st.n-3] != K_SL ||
                                 st.k[st.n-2] != K_INT || st.k[st.n-1] != K_INT) return false;
                             st.n -= 3; break;
            case IRW_ADD: case IRW_SUB: case IRW_MUL: case IRW_DIV:
                if (st.n >= 2 && st.k[st.n-1] == K_VEC && st.k[st.n-2] == K_VEC) {
                    if (st.vn[st.n-1] != st.vn[st.n-2] || st.ve[st.n-1] != st.ve[st.n-2]) return false;
                    if (st.fl[st.n-1] != st.fl[st.n-2]) return false;   // 부동성도 같아야
                    st.n--; st.o[st.n-1] = -1; break;   // 레인별 연산 — 벡터가 남는다(부동성 유지)
                }
                // ★ `add <bitset> <k>` 는 **비트를 세운다** — 그 비트셋의 **출신 지역**에 쓴다.
                if (in->w == IRW_ADD && st.n >= 2 && st.k[st.n-2] == K_BSET) {
                    if (st.k[st.n-1] != K_INT) return false;
                    if (st.o[st.n-2] < 0) return false;   // 어느 지역인지 모르면 안 내린다
                    st.n--; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; break;
                }
                // ★ **두 종류가 다 있다.** 정수면 정수, f64 면 f64 — 섞이면 안 내린다.
                if (st.n < 2 || st.k[st.n-1] != st.k[st.n-2]) return false;
                if (st.k[st.n-1] != K_INT && st.k[st.n-1] != K_FLT) return false;
                // ★ chk_*(0x40000) 는 오버플로를 **값으로**(none) — option 을 남긴다.
                if ((in->w != IRW_DIV) && (in->a & 0x40000) && st.k[st.n-1] == K_INT) {
                    st.n--; st.k[st.n-1] = K_OPT; st.o[st.n-1] = -1;
                    st.ve[st.n-1] = 0; st.vn[st.n-1] = 0; st.fl[st.n-1] = 0;   // ★ 수를 싼 option — 옛 표식(레코드를 쌌다는 ve)을 물려받지 않는다(2026-11-01)
                    break;
                }
                st.n--; break;
            case IRW_EQ: case IRW_NE: case IRW_LT: case IRW_LE: case IRW_GT: case IRW_GE:
                if (st.n < 2 || st.k[st.n-1] != st.k[st.n-2]) return false;
                if (st.k[st.n-1] == K_VEC) {
                    // ★ 벡터의 비교는 **마스크**를 낸다.
                    if (st.vn[st.n-1] != st.vn[st.n-2] || st.ve[st.n-1] != st.ve[st.n-2]) return false;
                    if (st.fl[st.n-1] != st.fl[st.n-2]) return false;   // ★ 2026-11-01 — 부동 · 부호가 같아야(방출이 그 표식으로 레인을 읽는다)
                    unsigned char ln = st.vn[st.n-1];
                    st.n--; st.k[st.n-1] = K_MASK; st.vn[st.n-1] = ln; st.o[st.n-1] = -1;
                    break;
                }
                if (st.k[st.n-1] != K_INT && st.k[st.n-1] != K_FLT) return false;
                st.n--; st.k[st.n-1] = K_INT; break;   // 비교의 결과는 **정수**다
            case IRW_MOD:
                // ★ 2026-11-03 (W-20261103-2113) — 정수 레인의 `mod` 는 레인마다 스칼라 `mod`(나누는 수의 부호 · 0 레인은 멈춘다 — lw_vmod1, VM 과 같다)
                if (st.n >= 2 && st.k[st.n-1] == K_VEC && st.k[st.n-2] == K_VEC) {
                    if (st.vn[st.n-1] != st.vn[st.n-2] || st.ve[st.n-1] != st.ve[st.n-2] || st.fl[st.n-1] != st.fl[st.n-2]) return false;
                    if (st.fl[st.n-1] == CBE_FL_FLT) return false;   // 부동 레인의 `mod` 는 검사가 거절한다(E-TYPE-KIND)
                    st.n--; st.o[st.n-1] = -1; break;
                }
                /* fall through */
            case IRW_AND: case IRW_OR:
                if (st.n < 2 || st.k[st.n-1] != K_INT || st.k[st.n-2] != K_INT) return false;
                st.n--; break;
            case IRW_NEG:
                if (st.n >= 1 && st.k[st.n-1] == K_VEC) { st.o[st.n-1] = -1; break; }   // ★ 2026-11-03 — 레인마다(정수는 감는다 · RFC-0040 D8)
                if (st.n < 1 || (st.k[st.n-1] != K_INT && st.k[st.n-1] != K_FLT)) return false;
                break;
            case IRW_NOT:
                if (st.n < 1 || st.k[st.n-1] != K_INT) return false;
                break;
            case IRW_SELECT:
                if (st.n < 3) return false;
                a1 = st.k[st.n-1]; a2 = st.k[st.n-2]; a3 = st.k[st.n-3];
                if (a3 == K_MASK && a1 == K_VEC && a2 == K_VEC) {
                    if (st.vn[st.n-1] != st.vn[st.n-2] || st.vn[st.n-3] != st.vn[st.n-1]) return false;
                    if (st.fl[st.n-1] != st.fl[st.n-2] || st.ve[st.n-1] != st.ve[st.n-2]) return false;
                    unsigned char ln = st.vn[st.n-1], ez = st.ve[st.n-1], vfl_ = st.fl[st.n-1];
                    st.n -= 2; st.k[st.n-1] = K_VEC; st.vn[st.n-1] = ln; st.ve[st.n-1] = ez;
                    st.fl[st.n-1] = vfl_;   // ★ 2026-11-01 — 벡터의 표식(부동 · 부호)을 낸다 — 전에는 마스크 자리의 것(0)이 남았다
                    st.o[st.n-1] = -1;
                    break;
                }
                if (a3 != K_INT || a1 != a2 || a1 == K_W128) return false;   // ★ W-20261105-2304 — 128 비트 고르기는 방출이 모른다
                st.n -= 2; st.k[st.n-1] = a1; break;
            case IRW_SWITCH: {   // ★ MM9-B2 빠른 경로 — 값(K_INT) 을 pop 하고, 뒤따르는 N+1 개 테이블 BR
                //   각각에 팝된 상태를 전파한다(SWITCH 는 그 BR 로 계산 점프한다). 그러면 각 BR·본문이
                //   올바른 종류 스택을 갖는다. 이로써 점프 테이블 def 도 **빠른(typed) 경로**에 남는다.
                if (st.n < 1 || st.k[st.n-1] != K_INT) return false;
                st.n--;
                proven_i64 nsw = in->a;
                for (proven_i64 c = 0; c <= nsw && (proven_size_t)(j + 1 + c) < d->ncode; c++)
                    if (!at[j + 1 + c].set) at[j + 1 + c] = st;
                live = false;
                break;
            }
            case IRW_BRZ:
            case IRW_BR: {
                if (in->w == IRW_BRZ) {
                    if (st.n < 1 || st.k[st.n-1] != K_INT) return false;
                    st.n--;
                } else live = false;   // 무조건 분기 뒤는 죽어 있다
                proven_i64 t = in->a;
                if (t < 0 || (proven_size_t)t >= d->ncode) return false;
                if (at[t].set) { if (at[t].n != st.n) return false;
                                 for (int q = 0; q < st.n; q++) if (at[t].k[q] != st.k[q]) return false; }
                else { at[t] = st; }
                break;
            }
            case IRW_RET: {
                unsigned char rk = st.n ? st.k[st.n-1] : K_INT;
                if (rk == K_INT && cbe_out_w128(d)) rk = K_W128;   // ★ W-20261105-2304 — 128 비트를 돌려주는 op 의 수 상수(위 절반 0 — VM 과 같다)
                // ★ 2026-11-01 — 갓 띄운 액터(제 자리 없음)를 돌려주면 부른 쪽이 그것을 칸에 담을 수 없다 — 담는 유닛이면 거절한다.
                if (st.n && rk >= K_REC && rk < K_VIEW && (st.fl[st.n-1] & CBE_FL_NOID) && rk - K_REC < 64 && g_aid_need[rk - K_REC]) {
                    low_bfmt(g_sub, sizeof g_sub, "returns an actor spawned in this op (the caller could not keep it in a container)"); return false; }
                // ★ option 이 레코드를 감쌌으면 그 표식을 반환에 기록한다(어댑터가 LWV_OK 로 되돌린다).
                // ★ 2026-11-01 (W-20261101-1828) — 구조체 뷰를 싼 option(fl=1 — `some <뷰>` · `try.view`)은 돌려주지 않는다: 반환 표식(g_retopt)은
                //   레코드만 말한다 — 받는 쪽이 그것을 레코드로 벗겨 `cr_.r` 을 읽었다(vm_vecgen 의 `at` — VM 42 · 네이티브 8).
                if (rk == K_OPT && st.n && st.ve[st.n-1] && st.fl[st.n-1]) {
                    const int vs_ = (int)st.ve[st.n-1] - 1;
                    if (st.fl[st.n-1] == 1 && vs_ >= 0 && (proven_size_t)vs_ < ir->nstructs && ir->structs[vs_].is_shadow && K_VIEW + vs_ < K_SVIEW &&
                        (!g_ret_optnode || g_ret_optnode == st.ve[st.n-1]) && !g_ret_optrec) {
                        g_ret_optnode = st.ve[st.n-1];   // ★ 2026-11-03 — 마디의 뷰를 싼 option: 받는 쪽은 그 표식(g_retnode)으로 안다
                        st.ve[st.n-1] = 0;               //   아래 g_ret_optrec 에 레코드로 적히지 않게
                    } else {
                        low_bfmt(g_sub, sizeof g_sub, "returns an option of a struct view (the caller would unwrap it as a record)"); return false; }
                }
                if (rk == K_OPT && st.n && st.ve[st.n-1]) { if (g_ret_optnode) { low_bfmt(g_sub, sizeof g_sub, "two `return`s wrap different kinds in an option"); return false; } g_ret_optrec = st.ve[st.n-1]; }
                // ★ 슬라이스를 **돌려줄 수 있다** — 다만 그 뜻(원소 수 vs 바이트)이 흔들리면 안 되므로
                //   바이트 슬라이스 def 에서만. 구조체·뷰 반환은 아직 안 내린다.
                if (rk == K_ROWS && st.n) {   // ★ 2026-11-01 — 줄의 나열을 돌려준다(배열 답의 결과 자리): 원소 폭 · 줄 바이트 · 표식
                    if (!g_ret_veset) { g_ret_ve = st.ve[st.n-1]; g_ret_fl = st.fl[st.n-1]; g_ret_rb = st.vn[st.n-1]; g_ret_veset = true; }
                    else if (g_ret_ve != st.ve[st.n-1] || g_ret_fl != st.fl[st.n-1] || g_ret_rb != st.vn[st.n-1]) {
                        low_bfmt(g_sub, sizeof g_sub, "two `return`s give rows of different shapes"); return false; }
                }
                if ((rk == K_SL || rk == K_OPTSL) && st.n) {   // 폭 · 표식을 적는다 — 반환이 여럿이면 같아야 한다(K_OPTSL 의 폭은 vn)
                    const unsigned char ve_ = rk == K_OPTSL ? (st.vn[st.n-1] ? st.vn[st.n-1] : 1) : (st.ve[st.n-1] ? st.ve[st.n-1] : 1), fl_ = st.fl[st.n-1];
                    if (!g_ret_veset) { g_ret_ve = ve_; g_ret_fl = fl_; g_ret_veset = true; }
                    else if (g_ret_ve != ve_ || g_ret_fl != fl_) {
                        low_bfmt(g_sub, sizeof g_sub, "two `return`s give slices of different element widths"); return false; }
                }
                // ★★★ **반환하는 슬라이스가 타입 슬라이스(원소폭>1)면 빠른 경로 밖으로.** cbe_def_esz 는
                //   **파라미터** 폭만 본다 — `mk4 input b (slice u8) . output slice u32 . do return
                //   view_array u32 b .` 은 param 이 바이트라 esz=1 로 통과했지만, **반환은 u32 슬라이스**
                //   (view_array 가 st.ve=4 로 표시)다. 그러면 호출부가 결과 슬롯을 ve=1(바이트)로 물려
                //   `index (mk4 c) i` 가 stride 1 로 읽어 VM(원소 i)≠native(바이트 i) 발산했다.
                //   실제 반환 슬라이스의 추적된 원소폭으로 판정한다 → 태그 경로로(부호형 i32 가 이미
                //   그렇게 옳게 도는 것과 같은 자리). 바이트 슬라이스 반환(ve=1)은 그대로 빠른 경로.

                // ★ `slice i8` 을 돌려주면 받는 쪽 슬롯이 부호 표식(fl=2)을 모른다 — 태그 경로로(2026-10-04).

                // ★★★ **구조체 뷰를 돌려줄 수 있다** — 뷰는 (포인터,길이)라 lw_r.s 에 담기고,
                //   받는 쪽은 **오프셋으로 읽는다**(원소 수 vs 바이트 갈림이 없다). K_SVIEW(구조체
                //   슬라이스)는 아직 — 그건 byte-carry 가 걸린다.
                if (rk >= K_VIEW && rk < K_SVIEW) {
                    if (*retk != 0xff && *retk != rk) return false;
                    *retk = rk; st.n = 0; live = false; break;
                }
                // ★★★ **구조체 슬라이스(K_SVIEW)도 돌려줄 수 있다** — (포인터, 원소 수)로 담고
                //   sidx 는 r_.v 로. 받는 쪽은 len/index 를 원소 수로 본다(어댑터가 stride 로 복원).
                if (rk >= K_SVIEW) {
                    if (*retk != 0xff && *retk != rk) return false;
                    *retk = rk; st.n = 0; live = false; break;
                }
                // ★ WO-0222 — option(슬라이스)를 돌려주는 op 은 `return none .` 도 적는다: none(안쪽 표식 없는 K_OPT)은 같은 종류다.
                if (rk == K_OPT && st.n && st.ve[st.n-1] == 0 && st.fl[st.n-1] == 0 && *retk == K_OPTSL) rk = K_OPTSL;
                if (rk == K_OPTSL && *retk == K_OPT && g_ret_optrec == 0) *retk = K_OPTSL;
                // ★ 겹 option 을 돌려주는 op 은 `return error e .` · `return none .` 도 적는다(안이 빈 바깥 값 — 표식 없는 K_OPT).
                if (rk == K_OPT && st.n && st.ve[st.n-1] == 0 && st.fl[st.n-1] == 0 && *retk == K_OPT2) rk = K_OPT2;
                if (rk == K_OPT2 && *retk == K_OPT && g_ret_optrec == 0) *retk = K_OPT2;
                if (rk != K_INT && rk != K_OPT && rk != K_OPTSL && rk != K_OPT2 && rk != K_FLT && rk != K_W128 && rk != K_SL && rk != K_ROWS && rk < K_REC) {
                    low_bfmt(g_sub, sizeof g_sub, "returns a kind the fast path has no shape for ({})", PROVEN_ARG((unsigned)(rk))); return false; }
                if (rk >= K_VIEW) return false;
                // ★ RFC-0106 단계 4 — 슬라이스 필드를 든 레코드도 **돌려줄 수 있다**
                //   (경계 복원이 `cbe_emit_rec_recon` 에 실렸다).
                // ★ 2026-11-01 (W-20261101-1828) — 배열 칸 레코드를 매개변수로 받는 op 은 수 · 수 레코드만 돌려준다(칸 슬라이스가 부른 쪽의 임시를 넘어 살 수 있다)
                if (cbe_def_arrpar(ir, d) && rk != K_INT && rk != K_FLT && rk != K_W128 && !(rk >= K_REC && rk < K_VIEW && !cbe_struct_has_sl(ir, rk - K_REC)) &&
                    !(rk >= K_REC && rk < K_VIEW && cbe_rec_arr_ok(ir, rk - K_REC))) {
                    low_bfmt(g_sub, sizeof g_sub, "takes a struct with array fields and returns kind {} (a field slice could outlive the caller's record)", PROVEN_ARG((unsigned)(rk))); return false; }
                if (rk >= K_REC && cbe_make_for_struct(ir, rk - K_REC) < 0 && !cbe_penum_ok(ir, rk - K_REC)) {
                    low_bfmt(g_sub, sizeof g_sub, "returns a RECORD with no `lit` site to rebuild it at the boundary"); return false; }
                if (*retk != 0xff && *retk != rk) {
                    low_bfmt(g_sub, sizeof g_sub, "two `return`s give different kinds ({} and {})", PROVEN_ARG((unsigned)(*retk)), PROVEN_ARG((unsigned)(rk))); return false; } // 두 종류를 섞어 돌려주면 **안 내린다**
                *retk = rk;
                st.n = 0; live = false; break;
            }
            case IRW_CALL: {
                proven_size_t c = IR_CALL_IDX(in->a);
                if (c >= ir->ndefs) return false;
                const low_ir_def_t *ce = &ir->defs[c];
                if (sc && !sc[c]) return false;   // ★ 방출 단계에서는 sc 가 없다(이미 통과가 보장됐다)
                // ★ 슬라이스를 넘기려면 **원소 폭이 같아야** 한다 — 우리 lw_sl 의 `n` 은
                //   **원소 수**이고, 그 뜻은 폭에 달려 있다. 다르면 넘기지 않는다.
                for (proven_size_t q = 0; q < ce->nparams && q < LOW_MAX_PARAMS; q++)
                    if ((ce->param_slice >> q) & 1u) {
                        unsigned char have_k = st.k[st.n - ce->nparams + q];
                        if (ce->param_selem[q]) {
                            // ★ 구조체 슬라이스 파라미터 — 원소 폭이 아니라 **sidx** 가 맞아야 한다.
                            if (have_k != (unsigned char)(K_SVIEW + ce->param_selem[q] - 1)) {
                                low_bfmt(g_sub, sizeof g_sub, "struct-slice argument {} has the wrong element type", PROVEN_ARG((int)((int)q)));
                                return false;
                            }
                            continue;
                        }
                        if (ce->param_rowb[q] && !g_no_elemsl) {   // ★ 2026-11-01 — 줄의 나열: 같은 줄 모양이어야 한다
                            const int ai_ = st.n - (int)ce->nparams + (int)q;
                            if (have_k != K_ROWS || st.ve[ai_] != ce->param_rowe[q] || st.vn[ai_] != ce->param_rowb[q] || st.fl[ai_] != (unsigned char)(ce->param_rowfl[q] | (ce->param_rowsub[q] << 2))) {
                                low_bfmt(g_sub, sizeof g_sub, "row-slice argument {} has a different row shape", PROVEN_ARG((int)((int)q))); return false; }
                            continue;
                        }
                        unsigned char want_e = (unsigned char)cbe_pslice_w(ce, q);   // ★ i64(164) 도 8 · f32/f64 는 param_esz — 폭 규칙은 한 자리
                        if (!want_e || st.ve[st.n - ce->nparams + q] != want_e) {
                            low_bfmt(g_sub, sizeof g_sub, "slice widths differ (arg {}: {}, wants {})", PROVEN_ARG((int)((int)q)),
                                     PROVEN_ARG((unsigned)(st.ve[st.n - ce->nparams + q])), PROVEN_ARG((unsigned)(want_e)));
                            return false;
                        }
                    }
                // ★★★ **구조체를 호출로 넘긴다.** 이걸 막아 뒀더니 `lum(q)` 하나가
                //   **사슬 전체**를 느린 경로로 끌어내렸다 — 그리고 그것이 **현실의 모양**이다
                //   (구조체를 만들어 함수에 넘긴다). 막는 것 하나가 이득 전부를 지운다.
                // ★★★ **피호출자가 구조체 파라미터를 바꾼다면**(액터 핸들러가 그 모양이다),
                //   그 쓰기는 **호출자에게 보여야** 한다 ⇒ 레코드의 **집**을 알아야 하고,
                //   호출 뒤에 그 집에 **되쓴다**. 집이 없으면(즉석 make) 아무도 못 보므로 무해하다.
                if (cbe_pmut(ce))
                    for (proven_size_t q = 0; q < ce->nparams && q < LOW_MAX_PARAMS; q++) {
                        if (!((ce->param_struct >> q) & 1u)) continue;
                        // ★ 뷰는 **집이 바이트**다 — 지역 슬롯이 없어도 되쓸 곳이 있다.
                        if (st.k[st.n - ce->nparams + q] >= K_VIEW
                            && st.k[st.n - ce->nparams + q] < K_SVIEW) continue;
                        if (st.k[st.n - ce->nparams + q] == K_AREF) continue;   // ★ 칸이 가리키는 액터 — 집은 태그 쪽 레코드다(부르고 되쓴다)
                        // ★ 2026-11-01 (W-20261101-1828) — `mut` 이 아닌 매개변수는 피호출자가 못 쓴다(검사층) — 집이 없어도 된다(`append v. make pt …`).
                        //   액터 핸들러는 상태를 `mut` 없이 바꾸므로 그대로 묻는다.
                        if (!ce->is_actor && !((ce->param_mutw >> q) & 1u)) continue;
                        if (st.o[st.n - ce->nparams + q] < 0) {
                            low_bfmt(g_sub, sizeof g_sub, "callee mutates a struct with no home");
                            return false;
                        }
                    }
                for (proven_size_t q = 0; q < ce->nparams && q < LOW_MAX_PARAMS; q++)
                    if ((ce->param_struct >> q) & 1u && cbe_pnode(ir, ce, q) < 0) {
                        // ★★ 2026-10-31 (W-20261031-2053) — 슬라이스 칸을 가진 레코드도 넘긴다(RFC-0106 의 남은 길: 호출 인자).
                        //   칸은 `crsl[매개변수 번호]` 로 가고, 바꾸는 피호출자가 쓴 칸은 부른 뒤 호출자의 지역 칸으로 되받는다(방출).
                        if (!cbe_rec_ok(ir, (int)ce->param_sidx[q]) && !cbe_rec_slice_ok(ir, (int)ce->param_sidx[q]) &&
                            !cbe_rec_arr_ok(ir, (int)ce->param_sidx[q])) return false;
                        unsigned char hk = st.k[st.n - ce->nparams + q];
                        if (cbe_rec_arr_ok(ir, (int)ce->param_sidx[q]) && hk != (unsigned char)(K_REC + ce->param_sidx[q])) return false;   // ★ 배열 칸 레코드는 레코드로만(2026-11-01)
                        // ★ 레코드는 그대로, **뷰**는 경계에서 슬롯으로 구체화해 넘긴다(비변형 한정 —
                        //   피호출자가 필드에 쓰면 그 쓰기가 뷰 바이트에 안 보이니까).
                        if (hk == (unsigned char)(K_VIEW + ce->param_sidx[q])) {
                            // ★★★★ **뷰를 변형하는 피호출자에게도 넘긴다** (RFC-0039 §9-3, 2026-08-02).
                            //   그전까지 여기서 거절했다 — 뷰는 경계에서 레코드 슬롯으로 **복사**되고,
                            //   피호출자의 쓰기가 그 복사본에만 남으니까. 그래서 잡 큐 조작을
                            //   라이브러리로 뽑는 순간 그 op 이 태그 경로로 떨어졌고, **태그 op 하나가
                            //   프로그램 전체에 인터프리터를 끌고 왔다**(실측: .text 5442 · bss 1,081,360
                            //   대 .text 260 · bss 8). 리프 규칙이 표면에서 살고 코드젠에서 죽었다.
                            //   ⇒ 레코드가 이미 하는 것을 뷰에게도 한다: **복사해 넣고, 부르고, 되쓴다.**
                            //     레코드의 집은 지역 슬롯이고 뷰의 집은 **바이트 그 자체**다 — 주소는
                            //     이미 손에 있다(뷰의 저장소는 슬라이스와 같다).
                        } else if (hk == K_AREF) {
                            // ★★ 2026-11-01 — **칸이 가리키는 액터에게 말한다**: 그 레코드를 풀어 빠른 몸을 부르고 되쓴다(방출).
                            const int as_ = (int)st.ve[st.n - ce->nparams + q] - 1;
                            if (as_ != (int)ce->param_sidx[q] || !ir->structs[as_].is_actor_state) return false;
                        } else if (hk != (unsigned char)(K_REC + ce->param_sidx[q]) &&
                                   !(hk >= K_REC && hk < K_VIEW && cbe_struct_same(ir, hk - K_REC, (int)ce->param_sidx[q])))
                            return false;   // 다른 구조체를 넘기면 안 내린다(모양이 같은 별칭은 넘긴다 — 2026-11-01)
                        else if ((st.fl[st.n - ce->nparams + q] & CBE_FL_NOID) && ce->param_sidx[q] < 64 &&
                                 c < 512 && ((g_acap[c] >> ce->param_sidx[q]) & 1u)) {
                            low_bfmt(g_sub, sizeof g_sub, "an actor spawned in this op is handed to an op that may keep it in a container's field");
                            return false;
                        }
                    }
                if ((proven_size_t)st.n < ce->nparams) return false;
                for (proven_size_t q = 0; q < ce->nparams; q++) {
                    unsigned char have = st.k[st.n - ce->nparams + q];
                    unsigned char want = (((ce->param_slice >> q) & 1u) && ce->param_selem[q])
                                         ? (unsigned char)(K_SVIEW + ce->param_selem[q] - 1)  // ★ 구조체 슬라이스
                                       : (((ce->param_slice >> q) & 1u) && ce->param_rowb[q] && !g_no_elemsl) ? K_ROWS   // ★ 줄의 나열
                                       : ((ce->param_slice >> q) & 1u) ? K_SL
                                       : ((ce->param_struct >> q) & 1u)
                                         ? (unsigned char)(K_REC + ce->param_sidx[q])
                                       : ((ce->param_bset >> q) & 1u) ? K_BSET
                                       : ((ce->param_opt >> q) & 1u) ? (ce->param_optsl[q] ? K_OPTSL : K_OPT)
                                       : ((ce->param_vec >> q) & 1u) ? K_VEC
                                       : ((ce->param_w128 >> q) & 1u) ? K_W128   // ★ W-20261105-2304
                                       : ((ce->param_flt >> q) & 1u) ? K_FLT : K_INT;
                    if (want == K_W128 && q < LOW_MAX_PARAMS && ce->prng[q].has_rng) {
                        low_bfmt(g_sub, sizeof g_sub, "a 128-bit argument with a declared range (the range check reads one 64-bit slot)"); return false; }
                    // ★★ 2026-11-02 (W-20261102-0024 O4) — 마디의 뷰 매개변수는 그 그림자의 뷰만 받는다 · `option (owned 마디)` 는 그 뷰를 싼 option 이나 none
                    if (cbe_pnode(ir, ce, q) >= 0) {
                        if (have != (unsigned char)(K_VIEW + cbe_pnode(ir, ce, q))) { low_bfmt(g_sub, sizeof g_sub, "argument {} is not a node in allocator bytes", PROVEN_ARG((int)((int)q))); return false; }
                        continue;
                    }
                    if (cbe_pnodeopt(ir, ce, q) >= 0) {
                        const int ai_ = st.n - (int)ce->nparams + (int)q;
                        if (have == K_OPT && ((st.ve[ai_] == 0 && st.fl[ai_] == 0) || (st.fl[ai_] == 1 && st.ve[ai_] == (unsigned char)(cbe_pnodeopt(ir, ce, q) + 1)))) continue;
                        low_bfmt(g_sub, sizeof g_sub, "argument {} is not an option of a node in allocator bytes", PROVEN_ARG((int)((int)q))); return false;
                    }
                    // ★ 구조체 파라미터는 레코드 또는 (같은 구조체의) 뷰를 받는다 — 위에서 검증됨.
                    if (((ce->param_struct >> q) & 1u) && have == (unsigned char)(K_VIEW + ce->param_sidx[q]))
                        continue;
                    if (((ce->param_struct >> q) & 1u) && have == K_AREF) continue;   // ★ 위에서 검증됨(칸이 가리키는 액터)
                    if (((ce->param_struct >> q) & 1u) && have >= K_REC && have < K_VIEW &&
                        cbe_struct_same(ir, have - K_REC, (int)ce->param_sidx[q])) continue;   // ★ 모양이 같은 별칭(위에서 검증됨)
                    // ★ 벡터는 레인 수·원소 폭도 맞아야 한다.
                    if (want == K_VEC) {
                        if (have != K_VEC) { low_bfmt(g_sub, sizeof g_sub, "argument {} is not a vector", PROVEN_ARG((int)((int)q))); return false; }
                        const unsigned char vfl_ = ((ce->param_vflt >> q) & 1u) ? CBE_FL_FLT : ((ce->param_vsgn >> q) & 1u) ? CBE_FL_SGN : 0;
                        if (st.vn[st.n - ce->nparams + q] != ce->param_vlanes[q] ||
                            st.ve[st.n - ce->nparams + q] != ce->param_ebits[q] || st.fl[st.n - ce->nparams + q] != vfl_) {
                            low_bfmt(g_sub, sizeof g_sub, "vector argument {} has a different shape", PROVEN_ARG((int)((int)q)));
                            return false;
                        }
                        continue;
                    }
                    if (want == K_OPTSL) {   // ★ 2026-11-01 — option(슬라이스): 폭이 같아야 한다 · `none`(표식 없는 K_OPT)도 받는다
                        const int ai_ = st.n - (int)ce->nparams + (int)q;
                        if (have == K_OPT && st.ve[ai_] == 0 && st.fl[ai_] == 0) continue;
                        if (have != K_OPTSL || st.fl[ai_] || (st.vn[ai_] ? st.vn[ai_] : 1) != ce->param_optsl[q]) {
                            low_bfmt(g_sub, sizeof g_sub, "option-slice argument {} has another shape", PROVEN_ARG((int)((int)q))); return false; }
                        continue;
                    }
                    if (want == K_W128 && have == K_INT) continue;   // ★ W-20261105-2304 — 수 인자는 위 절반 0 으로(VM 과 같다)
                    if (have != want) {
                        low_bfmt(g_sub, sizeof g_sub, "argument {} has the wrong kind ({} vs {})",
                                 PROVEN_ARG((int)((int)q)), PROVEN_ARG((int)((int)have)), PROVEN_ARG((int)((int)want)));
                        return false;
                    }
                    // ★ 비트셋은 **참조로** 넘어간다 ⇒ 그 **집**을 알아야 되받을 수 있다.
                    if (want == K_BSET && st.o[st.n - ce->nparams + q] < 0) return false;
                }
                st.n -= (int)ce->nparams;
                // ★★★ **피호출자가 무엇을 돌려주는지**가 슬롯의 종류를 정한다.
                //   여기서 늘 K_INT 를 밀어 넣고 있었다 ⇒ 슬라이스를 돌려주는 op 을 부르면
                //   그 다음 `len` 이 **정수에 len 을 건다**고 판단해 **전체가 거절**됐다.
                //   (그래서 BFS 가 통째로 느린 경로에 있었다.)
                { unsigned char rk = (c < 512) ? g_retk[c] : K_INT;
                  // ★★★ **재귀 호출**은 자기 자신을 부른다 — 그 반환 종류를 **아직 정하는 중**이라
                  //   0xff 다. 여기서 거절하면 재귀 op 은 **영원히** 못 내려간다(fib·deep 이 그랬다).
                  //   ⇒ 자기 호출이면 **이 op 의 반환 종류로 가정**한다. 틀리면 RET 검사가 잡는다
                  //   (반환이 정말 다르면 retk 가 어긋나 최종적으로 거절된다 — 낙관은 안전하다).
                  proven_size_t self = (proven_size_t)(d - ir->defs);
                  if (rk == 0xff && c == self) rk = (*retk == 0xff) ? K_INT : *retk;
                  if (c == self && rk == K_SL && !g_ret_veset) { low_bfmt(g_sub, sizeof g_sub, "a recursive call returns a slice of a width not yet known"); return false; }
                  if (rk == 0xff) { low_bfmt(g_sub, sizeof g_sub, "callee's return kind unknown"); return false; }
                  st.o[st.n] = -1;
                  // ★★★ **호출 결과는 스택 잔재를 물려받으면 안 된다.**
                  //   여기서 `o` 만 지우고 `ve`/`vn`/`fl` 은 그대로 뒀다 ⇒ 그 슬롯에 **전에 있던
                  //   값의 부가정보**가 남았다. 그리고 RET 이 그것을 *"option 이 감싼 구조체의
                  //   인덱스"* 로 읽어(`g_ret_optrec = st.ve[…]`) **존재하지 않는 레코드를 재구성**
                  //   하는 C 를 냈다 — `box_32719` 같은 **미선언 식별자**가 박힌, 컴파일조차 안 되는
                  //   코드다. VM 은 멀쩡했다.
                  //   ⇒ 부가정보는 **피호출자의 기록에서** 온다. 스택 메모리에서 줍지 않는다.
                  st.ve[st.n] = 0; st.vn[st.n] = 0; st.fl[st.n] = 0;
                  if (rk == K_OPT && c < 512) st.ve[st.n] = g_retopt[c];   // option 이 레코드를 감쌌으면 sidx+1
                  if (rk == K_OPT && c < 512 && c != self && g_retnode[c]) { st.ve[st.n] = g_retnode[c]; st.fl[st.n] = 1; }   // ★ 2026-11-03 — 마디의 뷰를 싼 option
                  if (rk == K_SL) {                                         // 반환 슬라이스의 폭 · 표식은 피호출자의 기록에서(2026-10-31)
                      const bool me_ = c == self;
                      st.ve[st.n] = me_ ? g_ret_ve : (c < 512 && g_retve[c] ? g_retve[c] : 1);
                      st.fl[st.n] = me_ ? g_ret_fl : (c < 512 ? g_retfl[c] : 0);
                  }
                  if (rk == K_ROWS) {                                       // ★ 2026-11-01 — 줄의 나열: 원소 폭 · 줄 바이트 · 표식도 피호출자의 기록에서
                      const bool me_ = c == self;
                      if (me_ && !g_ret_veset) { low_bfmt(g_sub, sizeof g_sub, "a recursive call returns rows of a shape not yet known"); return false; }
                      st.ve[st.n] = me_ ? g_ret_ve : (c < 512 ? g_retve[c] : 1);
                      st.vn[st.n] = me_ ? g_ret_rb : (c < 512 ? g_retrb[c] : 0);
                      st.fl[st.n] = me_ ? g_ret_fl : (c < 512 ? g_retfl[c] : 0);
                  }
                  if (rk == K_OPTSL) {                                      // ★ 2026-11-01 — 슬라이스 option 의 폭(vn) · 표식도 같은 기록에서
                      const bool me_ = c == self;
                      if (me_ && !g_ret_veset) { low_bfmt(g_sub, sizeof g_sub, "a recursive call returns a slice option of a width not yet known"); return false; }
                      const unsigned char w_ = me_ ? g_ret_ve : (c < 512 && g_retve[c] ? g_retve[c] : 1);
                      st.vn[st.n] = (unsigned char)(w_ > 1 ? w_ : 0);
                      st.fl[st.n] = me_ ? g_ret_fl : (c < 512 ? g_retfl[c] : 0);
                  }
                  st.k[st.n++] = rk; }
                break;
            }
            // ★★★ **호스트 잎** (2026-10-31 · W-20261031-2053) — 파일 · 디렉터리 · 경로 · 소켓 · 인자 · 환경 · 표준 입출력.
            //   빠른 경로가 받아 주는 낱말 목록(cbe_fast_word)에는 처음부터 있었는데 여기 case 가 없어 `default` 로 거절됐다
            //   (그 낱말을 쓰는 op 은 다 태그 경로였다). 두 경로가 같은 함수(`lw_h_*`, 프렐류드)를 부른다.
            //   바이트를 다루는 자리는 바이트 슬라이스만 받는다(원소 폭 1 · 부동 아님).
            case IRW_ARGC:
                            if (st.n >= 56) return false;
                            st.o[st.n] = -1; st.ve[st.n] = 0; st.fl[st.n] = 0; st.k[st.n++] = K_INT; break;
            case IRW_ARGV:   // 정수 → option(바이트 슬라이스)
                            if (st.n < 1 || st.k[st.n-1] != K_INT) return false;
                            st.k[st.n-1] = K_OPTSL; st.ve[st.n-1] = 0; st.vn[st.n-1] = 0; st.fl[st.n-1] = 0; st.o[st.n-1] = -1; break;
            case IRW_ENVGET:   // 바이트 → option(바이트 슬라이스)
                            if (st.n < 1 || !CBE_BYTES(st, st.n-1)) return false;
                            st.k[st.n-1] = K_OPTSL; st.ve[st.n-1] = 0; st.vn[st.n-1] = 0; st.fl[st.n-1] = 0; st.o[st.n-1] = -1; break;
            case IRW_WRITE:    // (정수, 바이트) → 정수
                            if (st.n < 2 || !CBE_BYTES(st, st.n-1) || st.k[st.n-2] != K_INT) return false;
                            st.n--; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_RDIN: case IRW_FREAD: case IRW_FWRITE: case IRW_DREAD: case IRW_NSEND: case IRW_NRECV:   // (정수, 바이트) → option 정수
                            if (st.n < 2 || !CBE_BYTES(st, st.n-1) || st.k[st.n-2] != K_INT) return false;
                            st.n--; st.k[st.n-1] = K_OPT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_FOPEN:    // (바이트, 정수) → option 정수
                            if (st.n < 2 || st.k[st.n-1] != K_INT || !CBE_BYTES(st, st.n-2)) return false;
                            st.n--; st.k[st.n-1] = K_OPT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_DOPEN: case IRW_FTYPE: case IRW_LTYPE: case IRW_NRESOLVE:   // 바이트 → option 정수
                            if (st.n < 1 || !CBE_BYTES(st, st.n-1)) return false;
                            st.k[st.n-1] = K_OPT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_DMAKE: case IRW_PREMOVE:   // 바이트 → 정수(1 / 0)
                            if (st.n < 1 || !CBE_BYTES(st, st.n-1)) return false;
                            st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_PRENAME:   // (바이트, 바이트) → 정수
                            if (st.n < 2 || !CBE_BYTES(st, st.n-1) || !CBE_BYTES(st, st.n-2)) return false;
                            st.n--; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_FCLOSE: case IRW_DCLOSE: case IRW_NCLOSE:   // 정수 → 정수
                            if (st.n < 1 || st.k[st.n-1] != K_INT) return false;
                            st.o[st.n-1] = -1; break;
            case IRW_NLISTEN: case IRW_NPORT: case IRW_NACCEPT:   // 정수 → option 정수
                            if (st.n < 1 || st.k[st.n-1] != K_INT) return false;
                            st.k[st.n-1] = K_OPT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_NCONNECT:   // (정수, 정수) → option 정수
                            if (st.n < 2 || st.k[st.n-1] != K_INT || st.k[st.n-2] != K_INT) return false;
                            st.n--; st.k[st.n-1] = K_OPT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_FSEEK:   // (정수, 정수, 정수) → option 정수
                            if (st.n < 3 || st.k[st.n-1] != K_INT || st.k[st.n-2] != K_INT || st.k[st.n-3] != K_INT) return false;
                            st.n -= 2; st.k[st.n-1] = K_OPT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_NPAIR: case IRW_TTYSIZE:   // → option 정수
                            if (st.n >= 56) return false;
                            st.o[st.n] = -1; st.ve[st.n] = 0; st.fl[st.n] = 0; st.k[st.n++] = K_OPT; break;
            // ★★ 2026-11-01 (W-20261101-1150) — 터미널 · 자식 프로세스 잎과 `prefetch`(태그 경로와 같은 런타임 · 같은 뜻)
            case IRW_TTYRAW: case IRW_PKILL:   // 정수 → 정수
                            if (st.n < 1 || st.k[st.n-1] != K_INT) return false;
                            st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_TTYREAD: case IRW_PSPAWN:   // 바이트 → option 정수
                            if (st.n < 1 || !CBE_BYTES(st, st.n-1)) return false;
                            st.k[st.n-1] = K_OPT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.vn[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_PREAD:   // (정수, 바이트) → option 정수
                            if (st.n < 2 || !CBE_BYTES(st, st.n-1) || st.k[st.n-2] != K_INT) return false;
                            st.n--; st.k[st.n-1] = K_OPT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.vn[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_PPOLL: case IRW_PWAIT:   // 정수 → option 정수
                            if (st.n < 1 || st.k[st.n-1] != K_INT) return false;
                            st.k[st.n-1] = K_OPT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.vn[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            case IRW_PREFETCH:   // (슬라이스, 정수) → 0
                            if (st.n < 2 || st.k[st.n-1] != K_INT || st.k[st.n-2] != K_SL) return false;
                            st.n--; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.vn[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            // ★ `value_or` 의 지연 분기 — option · result 둘 다 «값이 들어 있나». 수를 싼 것 · 바이트 슬라이스를 싼 것만.
            case IRW_HASVAL:
                            if (st.n < 1) return false;
                            if (!((st.k[st.n-1] == K_OPT && st.ve[st.n-1] == 0) || st.k[st.n-1] == K_OPTSL ||
                                  (st.k[st.n-1] == K_OPT && st.fl[st.n-1] == 1 && st.ve[st.n-1] && K_VIEW + st.ve[st.n-1] - 1 < K_SVIEW))) return false;   // ★ 2026-11-03 — 마디의 뷰를 싼 option(`drop` 이 묻는다 · 방출은 태그만 본다)
                            st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break;
            // ★ `value_or` 는 지연 분기로 낮아진다(low_ir.c): 값이 있을 때만 VALOR 에 오고, 그때 기본값 자리에는 **같은 option** 이 다시 온다.
            //   그래서 (option, 정수) 와 (option, 같은 option) 둘 다 받는다. 수를 싼 것 → 수 · 바이트 슬라이스를 싼 것 → 바이트 슬라이스.
            case IRW_VALOR:
                            if (st.n >= 2 && st.k[st.n-2] == K_OPT && st.ve[st.n-2] == 0 && st.fl[st.n-2] == 0 &&
                                (st.k[st.n-1] == K_INT || (st.k[st.n-1] == K_OPT && st.ve[st.n-1] == 0 && st.fl[st.n-1] == 0))) {
                                st.n--; st.k[st.n-1] = K_INT; st.o[st.n-1] = -1; st.ve[st.n-1] = 0; st.fl[st.n-1] = 0; break; }
                            if (st.n >= 2 && st.k[st.n-2] == K_OPTSL) {
                                const unsigned char w_ = st.vn[st.n-2] ? st.vn[st.n-2] : 1, f_ = st.fl[st.n-2];
                                const bool same_ = (st.k[st.n-1] == K_OPTSL && st.vn[st.n-1] == st.vn[st.n-2] && st.fl[st.n-1] == f_) ||
                                                   (st.k[st.n-1] == K_SL && (st.ve[st.n-1] ? st.ve[st.n-1] : 1) == w_ && st.fl[st.n-1] == f_);
                                if (same_) { st.n--; st.k[st.n-1] = K_SL; st.o[st.n-1] = -1; st.ve[st.n-1] = w_; st.vn[st.n-1] = 0; st.fl[st.n-1] = f_; break; }
                            }
                            if (st.n >= 2) low_bfmt(g_sub, sizeof g_sub, "`value_or` of kinds {} (ve {}) and {}", PROVEN_ARG((unsigned)(st.k[st.n-2])), PROVEN_ARG((unsigned)(st.ve[st.n-2])), PROVEN_ARG((unsigned)(st.k[st.n-1])));
                            return false;
            default: return false;
        }
    }
    if (fa_di_ < 512) g_faloc[fa_di_] = faloc_;
    return true;
}

static bool cbe_kind_ok(const low_ir_t *ir, const low_ir_def_t *d, const bool *sc) {
    // ★★★ **참조를 받을 수 있는 op 은 빠른 경로에 못 내린다** (2026-07-19).
    //   참조를 따라가는 것은 태그 값(LWV_REF)의 성질이고, 빠른 경로는 레코드를 **정수 슬롯으로
    //   펴서** 경계 어댑터가 필드를 정수로 읽는다 — 참조가 오면 그 어댑터가 무너진다.
    //   ☞ 그리고 이것이 *"생 포인터로 낮추면 음성 트랩이 사라진다"* 에 대한 답이다:
    //     **안 낮춘다.** 속도를 포기하는 자리를 이름으로 표시하고, 안전을 지킨다.
    //     (`--why-slow` 가 이 op 이 왜 느린 경로에 남았는지 말한다.)
    // ★★★★★ **사유를 여기서 적는다 — 안 적으면 앞 op 의 사유를 물려받는다.**
    //   (2026-08-30, WO-0153) `g_kw`/`g_kj`/`g_sub` 는 전역이고, 이 자리에서 그냥 빠지면
    //   `--why-slow` 가 **직전 op 의 "`ret` at 42"** 를 이 op 의 사유로 찍었다. 실제로
    //   `alloc` 의 액터 핸들러들이 그 거짓 이름표를 달고 있었고, 나는 그 수를 보고
    //   *"다음 단계는 `ret` 이다"* 라고 판단할 뻔했다.
    //   ☞ *전역 상태로 진단을 나르면, 말 안 한 자리는 침묵이 아니라 **남의 말**이 된다.*
    g_kw = (int)IRW_RET; g_kj = -1; g_sub[0] = 0;
    if (d->takes_ref_arg) {
        low_bfmt(g_sub, sizeof g_sub, "it takes a `ref` argument (the tagged path owns following refs)");
        return false;
    }
    kstack_t *at = cbe_at_ensure(0, d->ncode);
    if (!at || !cbe_lbuf_ensure(d->nlocals)) {
        low_bfmt(g_sub, sizeof g_sub, "the compiler ran out of memory sizing its kind tables for this op");
        return false;
    }
    unsigned char *lk = g_lb.lk, *slot = g_lb.slot, *rmap = g_lb.rmap,
                  *lez = g_lb.lez, *lln = g_lb.lln, *lfl = g_lb.lfl;
    unsigned char retk = 0xff;
    for (proven_size_t i = 0; i < d->ncode; i++) at[i].set = false;
    for (proven_size_t i = 0; i < g_lb.cap; i++) { slot[i] = 0xff; rmap[i] = 0xff; lez[i] = 0; lln[i] = 0; lfl[i] = 0; }
    cbe_local_kinds(d, lk, g_lb.cap);
    cbe_nodeview_seed(ir, d, lk, lez, lfl, g_lb.cap);   // ★ 2026-11-02 (W-20261102-0024 O4) — 마디의 뷰 매개변수
    for (proven_size_t q = 0; q < d->nparams && q < g_lb.cap; q++)   // ★ 2026-11-01 — option(슬라이스) 파라미터의 원소 폭(방출과 같은 규약)
        if (((d->param_opt >> q) & 1u) && d->param_optsl[q] > 1) lln[q] = d->param_optsl[q];
    for (proven_size_t q = 0; q < d->nparams && q < g_lb.cap; q++) {
        slot[q] = lk[q];
        if ((d->param_slice >> q) & 1u) {
            if (d->param_selem[q]) {   // ★ 구조체 슬라이스: (단계 1) 처음부터 원소 뷰 / (옛 규약) 바이트로 받아 view.array 가 나눈다
                if (!cbe_rec_ok(ir, d->param_selem[q] - 1)) return false;
                lez[q] = g_no_elemsl ? 1 : 0;
            } else if (d->param_rowb[q] && !g_no_elemsl) {   // ★ 2026-11-01 — 줄의 나열: 원소 폭 · 줄 바이트 · 표식
                lez[q] = d->param_rowe[q]; lln[q] = d->param_rowb[q]; lfl[q] = (unsigned char)(d->param_rowfl[q] | (d->param_rowsub[q] << 2));   // ★ 아래 줄 꼴(2026-11-02)
            } else {
                // ★★★ **슬라이스 파라미터는 바이트로 시작한다**(lez=1) — 폭이 무엇이든.
                //   본문의 `view.array` 가 원소폭을 설정한다(그래야 VARRAY 가 항등이 아니라
                //   실제로 원소 배열로 만든다). 어댑터도 **바이트 그대로** 넘긴다 — 두 층이
                //   같은 규칙을 봐야 한다(구조체 슬라이스와 같은 방식). 폭 자체는 알아야
                //   하강 가능 여부를 판단하므로 검사만 한다.
                proven_u8 eb = d->param_ebits[q];
                int w = cbe_pslice_w(d, q);
                if (!w) return false;
                lez[q] = g_no_elemsl ? 1 : (unsigned char)w;   // ★ RFC-0109 단계 1: 원소 폭으로 시작한다
                if (eb > 100 && eb < 164) lfl[q] = 2;   // ★ 부호 있는 원소(i8·i16·i32) — 본문의 view.array 도 같은 표식을 단다
                if ((d->param_sflt >> q) & 1u) lfl[q] = CBE_FL_FLT;   // ★ 2026-11-01 — 부동 원소
            }
        } else if ((d->param_struct >> q) & 1u) {
            // ★★★ **구조체 파라미터도 정수 슬롯에 담을 수 있어야 한다** (2026-07-19, lib/alloc.low 이 찾았다).
            //   여기서 `cbe_rec_ok` 를 **안 물었다** — 슬라이스 원소(위)에만 물었다. 그래서
            //   `state mem mut slice u8 . .` 를 가진 actor 의 핸들러가 빠른 경로로 내려갔고,
            //   경계 어댑터가 **모든 필드를 정수로 읽었다**:
            //       ar[0][0] = lw_want_int(lw_field(a[0], 0), "ints");   ← mem 은 슬라이스다
            //   ⇒ 네이티브가 `panic: ints` 로 죽었다. VM 은 태그 값이라 멀쩡했으므로
            //     **오라클이 갈렸다** — 그리고 그것이 이 결함을 볼 수 있게 한 유일한 이유다.
            //   ★ 인자 **있는** 핸들러는 다른 이유로 태그 경로에 남아 안 밟혔다. 그래서
            //     `vm_alloc.low`(스칼라 state)로는 절대 보이지 않았다 — **모양이 없으면 검사도 없다.**
            //   ⇒ 담을 수 없으면 **안 내린다**. 태그 경로가 이것을 옳게 처리한다.
            // ★★★★★ **단계 1 이 이 자리를 빠뜨렸다** (2026-08-30, WO-0153).
            //   파라미터 관문(위)만 느슨하게 하고 여기를 그대로 두어, 423 op 이
            //   *"`ret` at N"* 이라는 **아무 말도 아닌 사유**를 달고 남아 있었다
            //   (종류 추론은 끝까지 돌았고, 거절은 그 뒤 여기서 났다).
            //   ☞ *같은 관문을 두 곳에 두면, 한 곳만 고쳤을 때 진단이 엉뚱한 데를 가리킨다.*
            // ★★★★★ **여기를 열어 보았고, 차등 스윕이 되돌리게 했다** (2026-08-30, WO-0153).
            //   위 파라미터 관문은 단계 1 에서 느슨해졌지만 **이 자리는 그대로 두었다**.
            //   열어 보니 태그 경로가 1184 → 1033 으로 줄었는데 — **26 건이 발산했다**
            //   (`vm_io` 13 · `kvstore` 13: `stands` 가 VM 0, 네이티브 2).
            //   ⇒ RFC-0106 §4 의 규율대로 **없던 일로 한다.** 이 관문은 경계가 레코드를
            //     **되쓸 수 있어야**(mut 파라미터의 write-back) 열 수 있고, 그것이 단계 2 다.
            //   ☞ *관문이 둘이면 하나만 열었을 때 진단이 엉뚱한 데를 가리키고,
            //     둘 다 열면 아직 못 지키는 약속을 한다. 순서는 스윕이 정한다.*
            // ★ 단계 2 가 되쓰기를 갈랐으므로 여기도 느슨한 관문을 쓴다(RFC-0106).
            // ★★★★★ **관문은 아직 닫아 둔다** (2026-08-30, WO-0154 — 골든이 되돌리게 했다).
            //   되쓰기(위)와 레이아웃(아래 low_ir.c)을 고친 뒤 열어 보니 차등 스윕은
            //   **초록**인데 골든이 `jobq`·`entity` 등에서 **`panic: ints`** 를 냈다:
            //   경계가 `lw_field(a[q], <이름 색인>)` 로 읽는 값이 **런타임 레코드에서는
            //   슬라이스**인 자리가 남아 있다 — 즉 *IR 이 아는 struct 모양*과 *실제로
            //   건네진 레코드의 모양*이 아직 어긋난다(제네릭 인스턴스·액터 상태 쪽).
            //   ⇒ §4 규율대로 **이 단계는 여기까지**다. 남은 것은 그 어긋남을 좁히는 일이고,
            //     그때 이 한 줄만 바꾸면 된다.
            //   ☞ *스윕이 초록이어도 골든이 빨강이면 그 단계는 아직 안 선 것이다 —
            //     관문이 하나가 아니라는 뜻이고, 그것이 넓은 관문을 두는 까닭이다.*
            // ★★★★★ **관문은 아직 닫혀 있다** (2026-08-30, WO-0155). 단계 2 에서 결함 **셋**을
            //   고쳤고(되쓰기가 슬라이스를 지우던 것 · 레이아웃이 boxed 에서 멈추던 것 ·
            //   크기 0 필드를 스칼라로 보던 것) 그때마다 골든이 한 자리씩 앞으로 갔다.
            //   남은 것은 **모양 번역의 왕복**이다: 들어올 때 `VARRAY`(원소 수)를 바이트로
            //   바꿔 싣는데, 되쓸 때 같은 모양으로 되돌리는 것이 아직 안 맞는다
            //   (재현: `vm_pool.copy_and_release` — VM 55, 네이티브 93/94).
            //   ⇒ §4 규율대로 닫아 둔다. 이 한 줄만 바꾸면 다시 열린다.
            //   ☞ *경계는 값을 옮기는 곳이 아니라 **모양을 번역**하는 곳이고,
            //     번역은 **왕복**이어야 한다 — 한쪽만 번역하면 뜻이 샌다.*
            if (cbe_pnode(ir, d, q) < 0 && !cbe_rec_slice_ok(ir, d->param_sidx[q]) &&
                !(!cbe_pmut(d) && cbe_rec_nested_ok(ir, d->param_sidx[q], 0)) &&
                !cbe_rec_arr_ok(ir, d->param_sidx[q])) {   /* 관문 — RFC-0106 단계 2 · 안의 구조체 · 부동 칸은 2026-10-31(바꾸지 않는 op) · 배열 칸 레코드는 2026-11-01(몸이 칸 슬라이스를 못 내보내는 op — cbe_arrpar_body_ok) */
                low_bfmt(g_sub, sizeof g_sub, "parameter {} is a struct the fast frame cannot hold", PROVEN_ARG((size_t)(q)));
                g_kj = -1;
                return false;
            }
        } else if ((d->param_vec >> q) & 1u) {   // ★ 벡터 파라미터 — 원소 폭·레인 수를 지역에 새긴다
            lez[q] = d->param_ebits[q]; lln[q] = d->param_vlanes[q];
            lfl[q] = ((d->param_vflt >> q) & 1u) ? CBE_FL_FLT : ((d->param_vsgn >> q) & 1u) ? CBE_FL_SGN : 0;   // ★ 2026-11-01 — 레인의 부동 · 부호
        }
    }
    g_ret_optrec = 0; g_ret_optnode = 0; g_ret_veset = false; g_ret_ve = 1; g_ret_fl = 0; g_ret_rb = 0;
    if (!cbe_kind_run(ir, d, sc, at, lk, slot, &retk, rmap, lez, lln, lfl)) return false;
    proven_size_t di = (proven_size_t)(d - ir->defs);
    if (di < 512) { g_retk[di] = (retk == 0xff) ? K_INT : retk; g_retopt[di] = g_ret_optrec; g_retnode[di] = retk == K_OPT ? g_ret_optnode : 0; g_retve[di] = g_ret_ve; g_retfl[di] = g_ret_fl; g_retrb[di] = g_ret_rb; }
    return true;
}

// ★★★ **빠른 본문** — `lowv` 가 한 번도 안 나온다. 정수는 `long long`, 바이트 슬라이스는
//   `(포인터, 길이)`. 계약(범위·깊이·오버플로)은 **그대로 다** 있다 — 싸게 있을 뿐이다.
// ★ 반환 직전에 **구조체 파라미터를 되쓴다** — 참조로 받았으니 참조로 돌려줘야 한다.
static void cbe_wb(const low_ir_t *ir, const low_ir_def_t *d, low_out_t *out) {
    (void)ir;
    if (cbe_pmut(d)) {
        proven_size_t nr = 0;
        for (proven_size_t q = 0; q < d->nparams; q++) {
            if ((d->param_slice >> q) & 1u) continue;
            if (!((d->param_struct >> q) & 1u)) continue;
            (void)nr++;   // ★ 되쓸 것이 없다 — 피호출자와 호출자가 **같은 바이트**를 본다.
        }
    }
    // ★ 비트셋 파라미터는 **언제나** 되쓴다 — 참조로 받았으니 참조로 돌려준다(비용은 store 하나).
    { proven_size_t nb = 0;
      for (proven_size_t q = 0; q < d->nparams; q++)
          if ((d->param_bset >> q) & 1u)
              low_fpf(out, "    ab[{}] = loc[{}];\n", PROVEN_ARG((size_t)((size_t)nb++)), PROVEN_ARG((size_t)((size_t)q))); }
}

// ★ FFI 슬라이스 원소의 C 타입 — 전엔 폭에 상관없이 `unsigned char` 였다(`slice u32` 도!).
//   C 는 그것을 **잘못된 포인터 타입**으로 받았다(f64 반환과 같은 부류의 거짓말). 이제 폭으로
//   정직하게 낸다. 구조체 원소 슬라이스(param_selem)나 미지 폭은 바이트(unsigned char)로 둔다.
//   ★ 길이(.n)는 이미 **원소 수**다(경계에서 view_array 가 바이트를 폭으로 나눠 둔다) — 그대로 넘긴다.
static const char *cbe_slice_ctype(proven_u8 ebits, proven_u8 selem) {
    if (selem) return "unsigned char";      // 구조체 슬라이스 — C 는 바이트로 본다
    switch (ebits) {
        case 8:   return "uint8_t";
        case 108: return "int8_t";
        case 16:  return "uint16_t";
        case 32:  return "uint32_t";
        case 64:  return "uint64_t";
        case 116: return "int16_t";          // ★ 부호형 다바이트도 정직하게 — 전엔 unsigned char 로
        case 132: return "int32_t";          //   폭·부호를 떨궈 --emit-h 헤더가 구현 불가능한 시그니처였다
        case 164: return "int64_t";          //   (slice i32 를 unsigned char* 로·길이는 원소수 → C 가 오독)
        default:  return "unsigned char";    // 미지 폭 — 바이트로 (거짓말하지 않는다)
    }
}

// ★ extern 의 **출력**이 부동인가 — 전엔 반환을 언제나 `long long` 으로 선언·수신했다.
//   f64 를 반환하는 C 함수를 그렇게 받으면 **경계에서 타입이 어긋난다**(UB). out_tyname 으로 본다.
static bool cbe_ret_flt(const low_ir_def_t *d) {
    return proven_u8str_view_eq(d->out_tyname, proven_u8str_view_from_cstr("f64"))
        || proven_u8str_view_eq(d->out_tyname, proven_u8str_view_from_cstr("f32"));
}
// ★★★ **extern 의 반환 C 타입을 폭으로 정직하게** (RFC-0068 후속 · f64→double 과 같은 규율).
//   ~~반환을 언제나 long long 으로 선언~~ 하던 것이 f64 를 잘못된 레지스터로 읽었고(그건 닫혔다),
//   **정수 폭도 같은 문제**다: `int printf(...)` 을 `long long printf(...)` 로 선언하면 **stdio.h 와
//   충돌해 컴파일이 안 된다** — 즉 실제 libc 심볼(printf·atoi·strcmp…)을 **직접 못 부른다**.
//   ⇒ 선언된 출력 폭 그대로 C 타입을 낸다. 값은 여전히 long long 슬롯으로 받는다(더 좁은 정수는
//   그 프로토타입 덕에 컴파일러가 부호/영 확장을 정확히 한다 — 반환 수신 코드는 안 바꾼다).
static const char *cbe_ret_ctype(const low_ir_def_t *d) {
    if (d->out_ptr)    return "const char *";     // cstr — 널종단 char*
    if (cbe_ret_flt(d)) return "double ";          // f64/f32 (f32 도 double 로 — 기존 동작 유지)
    proven_u8str_view_t t = d->out_tyname;
    if (proven_u8str_view_eq(t, proven_u8str_view_from_cstr("i8")))    return "signed char ";
    if (proven_u8str_view_eq(t, proven_u8str_view_from_cstr("u8")))    return "unsigned char ";
    if (proven_u8str_view_eq(t, proven_u8str_view_from_cstr("i16")))   return "short ";
    if (proven_u8str_view_eq(t, proven_u8str_view_from_cstr("u16")))   return "unsigned short ";
    // ★ 32 비트는 `int32_t` — 호스트에서는 `int` 와 같은 타입이라 libc 원형과 부딪치지 않고, `int` 가 16 비트인 AVR 에서도 폭이 맞다(RFC-0054 §8-7 A2).
    if (proven_u8str_view_eq(t, proven_u8str_view_from_cstr("i32")))   return "int32_t ";
    if (proven_u8str_view_eq(t, proven_u8str_view_from_cstr("u32")))   return "uint32_t ";
    if (proven_u8str_view_eq(t, proven_u8str_view_from_cstr("u64"))
     || proven_u8str_view_eq(t, proven_u8str_view_from_cstr("usize"))) return "unsigned long long ";
    return "long long ";                            // i64 · idiff · 기본
}

static void cbe_scalar_body_raw(const low_ir_t *ir, const low_ir_def_t *d, low_out_t *out);
// ★★★ RFC-0150 §8-10 (2026-10-22) — **지역을 선언 폭의 C 타입으로** 낸다.
//   재 보니(§4.11 보탬) 값 스택(`st[sp++]`)은 gcc 가 이미 지운다 — 그것을 지역 변수로 바꿔도 0.608 → 0.608 ms.
//   느린 것은 누산기가 `long long` 칸에 살아 셈마다 폭을 다시 펴는 것이었다(`movl %eax, %eax` 가 의존 사슬에 든다 ·
//   벡터화기는 «reduction: unknown pattern»). 지역을 `uint32_t` 로 두면 검사판 0.604 → 0.318 ms(손으로 쓴 C 0.317),
//   감기판은 gcc 가 벡터화한다(0.09 ms).
//   ★ 건전성: 좁은 칸에 담으면 폭 밖의 값은 잘린다. 그래서 그 지역에 **쓰는 자리가 모두** 폭에 맞는 값을 담는다고
//     보일 때만 바꾼다 — 바로 앞 명령이 같은 폭 · 같은 부호의 셈(넘치면 멈춤 · 감기 · 포화 — 결과가 늘 폭 안)이거나
//     폭에 드는 상수이고, 그 저장이 갈래가 모이는 자리가 아닐 때. 방출된 본문에서 그 지역에 쓰는 글자를 다시 세어
//     (`loc[K] =` · `|=` · `&loc[K]` …) 그 수가 맞을 때만 이름을 바꾼다. 하나라도 모르는 쓰기가 있으면 옛 칸 그대로다.
static unsigned short g_fitn[64];     // 지역마다: 폭에 맞는다고 보인 저장의 수
static unsigned short g_wide[512];    // def 마다: 선언 폭이 좁은데 `long long` 으로 남은 지역의 수(`--why-slow`)
unsigned low_cbe_wide_of(proven_size_t i) { return i < 512 ? g_wide[i] : 0; }
static bool cbe_fits_meta(long long v, unsigned m) {
    const int bits = (int)(m & 0xff); const bool sg = (m & IR_TY_SIGNED) != 0;
    if (sg) return v >= -(1ll << (bits - 1)) && v <= (1ll << (bits - 1)) - 1;
    return v >= 0 && v <= (long long)((1ull << bits) - 1);
}
// 폭 v(bits | 부호)의 값이 모두 폭 m 에 드는가.
static bool cbe_meta_within(unsigned v, unsigned m) {
    const unsigned b = v & 0xff, B = m & 0xff; const bool s = (v & IR_TY_SIGNED) != 0, S = (m & IR_TY_SIGNED) != 0;
    if (!b || !B) return false;
    return s ? (S && B >= b) : (S ? B > b : B >= b);
}
// 바로 앞 명령이 이 지역의 선언 폭에 맞는 값을 냈는가(그 저장이 갈래가 모이는 자리가 아닐 때만 묻는다).
//   idxm = 바로 앞 명령이 슬라이스 칸을 읽었으면 그 칸의 폭(아니면 0).
static bool cbe_store_fits(const low_ir_def_t *d, proven_size_t j, unsigned m, unsigned idxm) {
    if (!m || j == 0) return false;
    if (idxm) return cbe_meta_within(idxm, m);
    const low_ir_ins_t *pv = &d->code[j - 1];
    if (pv->w == IRW_CONST) return cbe_fits_meta((long long)pv->a, m);
    if (pv->w != IRW_ADD && pv->w != IRW_SUB && pv->w != IRW_MUL) return false;
    const long long a = (long long)pv->a;
    if (!(a & IR_TY_KNOWN) || (a & (IR_TY_FLT | IR_POL_CHK | IR_POL_PROVEN))) return false;
    return (a & 0xff) == (long long)(m & 0xff) && ((a & IR_TY_SIGNED) != 0) == ((m & IR_TY_SIGNED) != 0);
}
// 본문에서 `loc[K]` 에 쓰는 글자를 센다. 모르는 꼴(주소 · 증감)은 큰 수를 내서 바꾸지 못하게 한다.
static unsigned cbe_loc_writes(const char *body, int k) {
    char pat[24]; low_bfmt(pat, sizeof pat, "loc[{}]", PROVEN_ARG((int)(k)));
    const size_t ln = strlen(pat); unsigned n = 0;
    for (const char *p = body; (p = strstr(p, pat)); p += ln) {
        if (p != body) { char c = p[-1]; if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') continue; }
        if (p != body && (p[-1] == '&' || (p - body >= 2 && (p[-2] == '+' || p[-2] == '-') && p[-1] == p[-2]))) return 1000;
        const char *q = p + ln; while (*q == ' ') q++;
        if ((q[0] == '+' || q[0] == '-') && q[1] == q[0]) return 1000;
        if (q[0] == '=' && q[1] != '=') { n++; continue; }
        if (q[0] && strchr("+-*/%|&^", q[0]) && q[1] == '=') { n++; continue; }
        if ((q[0] == '<' || q[0] == '>') && q[1] == q[0] && q[2] == '=') { n++; continue; }
    }
    return n;
}

// ★★★ **프레임은 호출마다 0 으로 채워진다 — 그러니 안 쓰는 칸을 잡으면 그대로 비용이다** (2026-07-27).
//
//   전에는 지역이 n 개면 **여섯 종류 모두** n 칸씩 잡고 전부 0 초기화했다: `rloc[n][8]` 과
//   `vloc[n][8]` 만 해도 n×128 바이트다. fib 처럼 **정수만 쓰는 재귀**는 호출마다 1KB 넘는
//   memset 을 지고 돌았다(벤치에서 C 의 10배 — 그 절반이 이것이었다).
//
//   ⇒ **본문을 먼저 만들고, 본문이 실제로 만지는 종류만 잡는다.** 종류를 여기서 다시 추론하면
//     그 추론이 두 벌이 되고 두 벌은 반드시 갈린다(교훈 7) — 그래서 **방출된 글자**를 본다.
//     이것이 유일한 답이다: 본문이 `rloc[` 를 안 쓰면 `rloc` 는 없어도 된다.
static bool cbe_uses(const char *body, const char *name) {
    size_t ln = strlen(name);
    for (const char *p = body; (p = strstr(p, name)); p += ln) {
        // `loc[` 는 `sloc[`·`rloc[` … 의 꼬리이기도 하다 — 앞 글자가 이름의 일부면 건너뛴다.
        if (p != body) { char c = p[-1]; if ((c >= 'a' && c <= 'z') || c == '_') continue; }
        return true;
    }
    return false;
}
// 본문이 만지는 인자 배열을 읽는다. 못 읽으면 **전부 넘긴다**(안전한 쪽).
static unsigned cbe_argmask_of(const char *body) {
    unsigned m = 0;
    if (cbe_uses(body, "ai[")) m |= LWA_AI;
    if (cbe_uses(body, "as[")) m |= LWA_AS;
    if (cbe_uses(body, "af[")) m |= LWA_AF;
    if (cbe_uses(body, "ar[")) m |= LWA_AR;
    if (cbe_uses(body, "ab[")) m |= LWA_AB;
    if (cbe_uses(body, "ao[")) m |= LWA_AO;
    if (cbe_uses(body, "av[")) m |= LWA_AV;
    if (cbe_uses(body, "rsl[")) m |= LWA_RSL;   // ★ 레코드의 슬라이스 필드(RFC-0106 단계 1)
    return m;
}
// 시그니처의 파라미터 목록 — 마스크가 고른 것만. 비면 `void`.
// ★★★★★ **호출 깊이 원장은 인자다** (2026-08-16, WO-0056 P3).
//   전에는 메모리 카운터(`_Thread_local int lw_depth`)를 호출마다 **읽고 썼다**. 그것이
//   fib(32) 에서 호출 비용의 **3분의 1** 이었다(×3.65 → ×2.40 실측).
//   ★ 원인 진단을 한 번 틀렸다: 처음엔 **TLS** 가 비싼 줄 알았는데, 평범한 `static` 전역으로
//     바꿔도 **똑같았다**(13.86 vs 14.01 ms). 비싼 것은 스레드지역이라는 것이 아니라
//     **메모리라는 것**이다 — 재귀 호출 사이에 읽기-수정-쓰기가 끼면 레지스터에 못 산다.
//   ⇒ 인자로 넘긴다. 이름을 그대로 `lw_depth` 로 두어 **본문 코드가 한 글자도 안 바뀐다**:
//     `++lw_depth > 128` 도 `lw_depth--` 도 그대로 서고, 다만 그 대상이 이 프레임의 것이 된다.
//     전역은 태그 경로(`lw_op_*`)가 계속 쓰고, 빠른 경로로 들어갈 때 **그 값을 이어받는다**
//     — 그래야 두 경로를 오가도 깊이가 과소평가되지 않는다(과소평가 = clean panic 대신 segfault).
//   ★★ 세는 규율은 **바뀌지 않는다**: 재귀가 불가능한 op 은 전에도 안 셌고 지금도 안 센다.
// ★★★ **아무도 읽을 수 없는 것은 넘기지 않는다** (2026-08-16, WO-0056 P3).
//   `g_needdepth` 는 이미 *"이 op 에서 재귀 가능한 op 에 닿을 수 있는가"* 의 **도달 폐포**다
//   (위 `cbe_mark_depth` 의 고정점). 거짓이면 그 op 도, 그 op 이 부르는 어떤 op 도 원장을
//   보지 않는다 — 그러면 인자를 뗀다.
//   ★ 건전성: 원장을 보는 op 을 부르는 쪽은 **반드시** 이 판정이 참이다(도달 폐포이므로).
//     즉 "받는 쪽은 원하는데 넘길 것이 없다" 는 경우가 **생길 수 없다**.
//   ★★ 값(실측 A/B, 같은 기계·같은 세션 3회 최선): 균일하게 모두에게 인자를 주면 호출이
//     잦은 프로그램이 값을 치른다 — http **+3.3%**. 뗀 뒤 **−0.2%** 로 돌아온다.
//     (sort 는 +5.0% 그대로다: 그 op 들이 전부 재귀에 닿아 어차피 인자를 받는다.)
//   ★★★ **여기서 한 번 틀렸다.** 처음엔 인자만 떼고 아래 그림자 지역을 안 남겨서, 반환 자리의
//     `lw_depth--` 가 **스레드지역 전역에 닿았다** — json 이 59.4 → 73.0 ms 로 무너졌다.
//     그리고 그 측정을 기계에 부하가 걸린 동안 해서 *"이 아이디어는 값이 없다"* 고 잘못
//     판정했다. **부하 중에 잰 수로 설계를 정하지 마라** — 조용해진 뒤 다시 재니 답이 뒤집혔다.
static bool cbe_depparam(proven_size_t i) { return i >= 512 || g_needdepth[i]; }
// ★★★ **작은 op 은 `inline` 이라고 말해 준다** (2026-08-29, WO-0140 — 실측이 골랐다).
//   방출된 빠른 경로는 C 컴파일러의 비용 모형에는 **커 보인다**: 스택 기계 모양이라 짧은 op 도
//   줄 수가 는다. 그래서 gcc 가 인라인을 포기하고, 원소마다 부르는 순회에서 그 값이 크게 나온다
//   — 같은 프로그램에서 op 하나에 `inline` 을 붙이자 **78 → 40 ms** 였다(같은 답 3067).
//   ★ `always_inline` 은 **안 쓴다**: 값은 같고(41 ms) 코드 팽창 위험만 진다. 여기서 하는 것은
//     *"이건 작다"* 고 말해 주는 것뿐이고, 무엇을 실제로 인라인할지는 C 컴파일러가 정한다.
//   ★★ 임계값을 두는 이유: 큰 op 까지 힌트를 주면 컴파일 시간과 코드 크기를 대가로 치른다.
//     이 저장소가 같은 부류(깊이 인자)에서 배운 것이다 — *균일하게 주면 호출이 잦은 프로그램이
//     값을 치른다*. 64 는 **측정 대상이 된 op 들이 들어오는 자리**이고, 넘으면 안 준다.
#define CBE_INLINE_MAX_CODE 64
static bool cbe_inline_hint(proven_size_t i) {
    if (i >= 512 || !g_ir_for_inline) return false;
    const low_ir_def_t *d = &g_ir_for_inline->defs[i];
    // ★★ W-20261104-1345 — 입구의 겹침 검사(짝마다 네 낱말 · REQ-0036)는 **몸의 크기가 아니다.** 그것을 세었더니 `mont_mul` · `pmul` ·
    //   `q_pmul` 이 이 문턱을 넘어 `inline` 을 잃었다. 곡선 셈(vm_curve_by 네 폭, 각 3 회)에서 **어느 하나만으로는 안 돌아왔다**: 검사를
    //   지우기만 하면 425–444 ms(지운 자리가 낱말 수로 남아 문턱을 그대로 넘었다), 문턱만 고치면 428–433 ms(풀어 넣은 몸에 검사가 그대로다),
    //   둘 다 하면 368–375 ms(검사 전 364–377 · 검사 427–428). 지운 짝(`CONST 0 · DROP` 두 벌 — C 에는 아무것도 안 남는다)도 남은 짝도 빼고 센다.
    const proven_size_t xq = (proven_size_t)d->nxpairs * 4u;
    return d->lowered && (d->ncode > xq ? d->ncode - xq : 0) <= CBE_INLINE_MAX_CODE;
}
static void cbe_put_params(low_out_t *out, unsigned m, bool dep) {
    int n = 0;
    if (m & LWA_AI) { low_puts(out, n++ ? ", const long long *restrict ai" : "const long long *restrict ai"); }
    if (m & LWA_AS) { low_puts(out, n++ ? ", const lw_sl *restrict as" : "const lw_sl *restrict as"); }
    if (m & LWA_AF) { low_puts(out, n++ ? ", const double *restrict af" : "const double *restrict af"); }
    if (m & LWA_AR) { low_puts(out, n++ ? ", unsigned char *const *ar" : "unsigned char *const *ar"); }
    if (m & LWA_AB) { low_puts(out, n++ ? ", long long *restrict ab" : "long long *restrict ab"); }
    if (m & LWA_AO) { low_puts(out, n++ ? ", const lw_r *restrict ao" : "const lw_r *restrict ao"); }
    if (m & LWA_AV) { low_puts(out, n++ ? ", const lw_v *av" : "const lw_v *av"); }
    // ★★★★★ **`rsl` 은 읽기 전용이 아니다** (2026-08-31, WO-0162).
    //   파라미터의 평행 칸은 들어올 때 지역 칸으로 **한 방향 복사**됐고(`memcpy(rlocsl…)`),
    //   되쓰기는 `rsl` 을 읽는다 — 그래서 핸들러가 **자기 상태의 슬라이스 필드에 쓴 것이
    //   호출자에게 안 보였다**(스윕: `len needs a slice` 98 건).
    //   ⇒ `ar` 이 그러하듯 **쓸 수 있게** 한다. 구조체 파라미터는 참조로 오고, 그 참조에는
    //     바이트 자리뿐 아니라 **평행 칸도 딸려 있다**.
    //   ☞ *한 방향 복사는 「같은 것」이 아니라 「그때의 사본」이다.*
    if (m & LWA_RSL) { low_puts(out, n++ ? ", lw_sl *restrict rsl" : "lw_sl *restrict rsl"); }
    if (dep) low_puts(out, n++ ? ", int lw_depth" : "int lw_depth");
    if (!n) low_puts(out, "void");
}
// 호출 자리의 인자 목록 — **이름은 자리마다 다르다**: 호출자 지역은 `ci·cs·cf·car·cab·cao·cav`,
// 파라미터는 `ai·as·af·ar·ab·ao·av`. 한 접두사로 만들려다 `aab` 같은 없는 이름을 냈다(컴파일 실패).
// ⇒ 두 표를 **그대로** 적는다. 규칙을 짜맞추는 것보다 적어 두는 편이 안전하다.
static void cbe_put_args(low_out_t *out, unsigned m, bool caller, const char *dep) {   // dep = NULL 이면 안 넘긴다
    static const char *cn[8] = { "ci", "cs", "cf", "car", "cab", "cao", "cav", "crsl" };
    static const char *pn[8] = { "ai", "as", "af", "ar",  "ab",  "ao",  "av", "rsl" };
    static const unsigned bit[8] = { LWA_AI, LWA_AS, LWA_AF, LWA_AR, LWA_AB, LWA_AO, LWA_AV, LWA_RSL };
    int n = 0;
    for (int q = 0; q < 8; q++)
        if (m & bit[q]) low_fpf(out, "{}{}", proven_arg_cstr(n++ ? ", " : ""), proven_arg_cstr(caller ? cn[q] : pn[q]));
    // ★ 깊이는 **자리마다 출처가 다르다**: 빠른 경로 안의 호출은 자기 깊이를 이어 주고,
    //   태그 래퍼는 전역 원장을(이미 자기 몫을 세어 두었다), 바깥 C 진입점은 0 에서 시작한다.
    if (dep) low_fpf(out, "{}{}", proven_arg_cstr(n ? ", " : ""), proven_arg_cstr(dep));
}
static void cbe_scalar_body(const low_ir_t *ir, const low_ir_def_t *d, low_out_t *out) {
    // ★ W-20261113-0024 — 본문을 proven 메모리 통로에 모은다(전엔 open_memstream · 그 버퍼는 C 의 free 로 돌려줬다).
    low_out_t *m = low_out_mem();
    if (!m) { cbe_scalar_body_raw(ir, d, out); return; }   // 못 잡으면 옛길 — 느릴 뿐 틀리지 않는다
    cbe_scalar_body_raw(ir, d, m);
    if (low_out_failed(m)) { (void)low_out_close(m); cbe_scalar_body_raw(ir, d, out); return; }   // 메모리가 모자랐다 — 옛길
    const char *buf = (const char *)low_out_mem_view(m).ptr;   // NUL 로 끝난다(proven u8str)
    const char *mark = strstr(buf, "@@LWFRAME@@\n");
    if (!mark) { low_puts(out, buf); (void)low_out_close(m); return; }
    low_write(out, buf, (size_t)(mark - buf));
    size_t n = d->nlocals ? d->nlocals : 1;
    const char *body = mark + strlen("@@LWFRAME@@\n");
    // ★ RFC-0150 §8-10 — 폭에 맞는 값만 담는다고 보인 지역은 `loc[K]` 를 그 폭의 C 지역 `nlocK_` 로 바꾼다(위 g_fitn).
    char *nbody = NULL; unsigned narrowed[64] = {0}; unsigned nwide = 0;
    {
        const proven_size_t di = (proven_size_t)(d - ir->defs);
        bool any = false;
        for (proven_size_t k = 0; k < d->nlocals && k < 64; k++) {
            if (!d->loc_meta[k]) continue;
            char pat[24]; low_bfmt(pat, sizeof pat, "loc[{}]", PROVEN_ARG((int)((int)k)));
            bool used = false;
            for (const char *p = body; (p = strstr(p, pat)); p += strlen(pat))
                if (p == body || !((p[-1] >= 'a' && p[-1] <= 'z') || (p[-1] >= 'A' && p[-1] <= 'Z') || (p[-1] >= '0' && p[-1] <= '9') || p[-1] == '_')) { used = true; break; }
            if (!used) continue;
            if (g_fitn[k] && cbe_loc_writes(body, (int)k) == g_fitn[k]) { narrowed[k] = d->loc_meta[k]; any = true; }
            else if (k >= d->nparams) nwide++;   // 파라미터는 들어올 때 한 번 쓰인다 — 그것은 세지 않는다
        }
        if (di < 512) g_wide[di] = (unsigned short)nwide;
        if (any) {
            size_t bl = strlen(body), cap = bl + bl / 4 + 64, o = 0;
            nbody = (char *)low_alloc(cap);   // ★ W-20261112-0207 — proven 힙
            for (const char *p = body; nbody && *p; ) {
                if (o + 32 >= cap) { size_t oc_ = cap; cap *= 2; char *t = (char *)low_realloc(nbody, oc_, cap); if (!t) { low_free(nbody); nbody = NULL; break; } nbody = t; }
                if (p[0] == 'l' && p[1] == 'o' && p[2] == 'c' && p[3] == '[' &&
                    (p == body || !((p[-1] >= 'a' && p[-1] <= 'z') || (p[-1] >= 'A' && p[-1] <= 'Z') || (p[-1] >= '0' && p[-1] <= '9') || p[-1] == '_'))) {
                    char *e; unsigned long k = strtoul(p + 4, &e, 10);
                    if (e != p + 4 && *e == ']' && k < 64 && narrowed[k]) { o += (size_t)low_bfmt(nbody + o, cap - o, "nloc{}_", PROVEN_ARG((unsigned long long)(k)));   /* 전엔 sprintf — 칸은 위에서 32 바이트 남겨 둔다 */ p = e + 1; continue; }
                }
                nbody[o++] = *p++;
            }
            if (nbody) { nbody[o] = 0; body = nbody; }
            else memset(narrowed, 0, sizeof narrowed);
        }
    }
    low_puts(out, "    ");
    for (size_t k = 0; k < 64; k++) if (narrowed[k])
        low_fpf(out, "{}int{}_t nloc{}_ = 0; ", proven_arg_cstr((narrowed[k] & IR_TY_SIGNED) ? "" : "u"), PROVEN_ARG((unsigned)(narrowed[k] & 0xffu)), PROVEN_ARG((size_t)(k)));
    if (cbe_uses(body, "loc["))  low_fpf(out, "long long loc[{}] = {{0}}; ", PROVEN_ARG((size_t)(n)));
    if (cbe_uses(body, "sloc[")) low_fpf(out, "lw_sl sloc[{}] = {{{{0,0}}}}; ", PROVEN_ARG((size_t)(n)));
    // ★ 2026-10-31 — 틀 안 나열의 바이트 줄과 스택 예산(태그 경로 · VM 과 같은 수 — 부를 때마다 새것, 재귀도 안전)
    // ★★ 2026-10-31 (W-20261031-2053) — 예산은 **틀 안 나열 + 값 구조체 틀 창**(lbuf_size + wbytes)이다. VM(low_vm.c)과 태그 경로가 그렇게 센다.
    //   빠른 경로는 틀 창을 짓지 않지만(레코드는 C 틀의 rbuf 에 산다) 같은 수를 센다 — 그래야 깊은 되부름이 같은 자리에서 멈춘다.
    //   전에는 나열 몫만 셌고(값 구조체만 짓는 op 은 아무것도 안 셌다), 그래서 둘을 다 가진 op 을 태그 경로로 보냈다.
    if (d->lbuf_size && cbe_uses(body, "lw_lb"))
        low_fpf(out, "unsigned char lw_lb[{}] __attribute__((aligned(16))); ", PROVEN_ARG((size_t)((size_t)d->lbuf_size)));
    if (d->lbuf_size || d->wbytes)
        low_fpf(out, "long lw_sbg_ __attribute__((cleanup(lw_sb_restore))) = lw_sbytes;\n"
                     "    if (LW_UNLIKELY((lw_sbytes += {}L) > LW_SBUDGET)) lw_panic(\"the list literals and struct values living on this call chain need more than the stack budget "
                     "(RFC-0135 · RFC-0132 P4e) — keep a recursion with local lists or structs shallow, or take them from an allocator\");\n    ",
                PROVEN_ARG((size_t)((size_t)d->lbuf_size + (size_t)d->wbytes)));
    // ★★★★ **레코드 지역은 이제 "어디를 가리키나" 다** (RFC-0039 §9-4 단계 2, 2026-08-03).
    //   구조체 파라미터는 **호출자의 바이트를 그대로 가리킨다** — 복사가 없고, 피호출자가
    //   쓰면 호출자에게 **즉시** 보이므로 되쓰기도 없다. 스스로 만든 레코드만 `rbuf` 에 산다.
    //   ☞ `rloc[q]` 라는 **글자는 그대로**다(포인터 + 오프셋도 `rloc[q] + off`) — 그래서
    //     이 큰 전환이 사용처를 거의 안 건드린다. 바이트로 합쳐 둔 값이 여기서 나온다.
    if (cbe_uses(body, "rloc[")) low_fpf(out, "unsigned char *rloc[{}] = {{0}}; _Alignas(8) unsigned char rbuf[{}][LW_RECB] = {{{{0}}}}; ", PROVEN_ARG((size_t)(n)), PROVEN_ARG((size_t)(n)));
    // ★ 2a — 빠른 경로가 띄워 메시지를 받는 액터의 번호. op 을 떠날 때(어느 return 이든) «죽었다» 고 적는다 — 남은 메시지가 끝나면 자리가 돌아간다.
    { const proven_size_t di_ = (proven_size_t)(d - ir->defs);
      if (di_ < 512) for (int k = 0; k < 64; k++) if ((g_faloc[di_] >> k) & 1u)
          low_fpf(out, "int fa{}_ __attribute__((cleanup(lw_fa_cleanup))) = -1; ", PROVEN_ARG((int)(k))); }
    if (cbe_uses(body, "rlocsl[")) low_fpf(out, "lw_sl rlocsl[{}][LW_RECF]; (void)rlocsl; ", PROVEN_ARG((size_t)(n)));
    if (cbe_uses(body, "floc[")) low_fpf(out, "double floc[{}] = {{0}}; ", PROVEN_ARG((size_t)(n)));
    if (cbe_uses(body, "lw_hloc[")) low_fpf(out, "lw_u128 lw_hloc[{}] = {{0}}; ", PROVEN_ARG((size_t)(n)));   // ★ W-20261105-2304 — 128 비트 지역
    if (cbe_uses(body, "lw_hs[")) low_puts(out, "lw_u128 lw_hs[16]; int lw_hsp = 0; (void)lw_hsp; ");
    if (cbe_uses(body, "wloc[")) low_fpf(out, "lw_r wloc[{}] = {{{{0,0,{{0,0}},{{0}},0}}}}; ", PROVEN_ARG((size_t)(n)));
    if (cbe_uses(body, "wlocsl[")) low_fpf(out, "lw_sl wlocsl[{}][LW_RECF]; (void)wlocsl; ", PROVEN_ARG((size_t)(n)));
    if (cbe_uses(body, "wssl[")) low_puts(out, "lw_sl wssl[16][LW_RECF]; (void)wssl; ");
    // ★ 벡터 지역도 **본문이 실제로 쓰는 자리까지만**. 다른 지역 배열과 달리 여기는 칸 하나가
    //   **64 바이트**(`lw_v` = 최대 폭 u32×16 공용체)라, 안 쓰는 슬롯 하나가 곧 64 B 다.
    //   ☞ 슬롯 번호는 그대로 둔다(빽빽하게 다시 매기면 방출 자리 전부를 고쳐야 한다) —
    //     **가장 높은 번호 + 1** 까지만 잡는다. 색인이 전부 리터럴이라 본문에서 정확히 읽힌다.
    if (cbe_uses(body, "vloc[")) {
        size_t vn = 0;
        for (const char *q = body; (q = strstr(q, "vloc[")); q += 5) {
            char *e; unsigned long ix = strtoul(q + 5, &e, 10);
            if (e != q + 5 && *e == ']' && ix + 1 > vn) vn = ix + 1;
        }
        if (!vn || vn > n) vn = n;          // 못 읽었으면 옛 값 — 작게 잡는 실수는 안 한다
        low_fpf(out, "lw_v vloc[{}] = {{{{{{0}}}}}}; ", PROVEN_ARG((size_t)(vn)));
    }
    low_puts(out, "\n");
    // ★ 벡터 스택의 칸 수를 여기서 박는다 — **증가만 센 값**이라 상계이고, 넘침은 방출된
    //   검사가 잡는다. 바닥은 2(한 칸짜리 연산도 있다), 천장은 옛 값 16(더 커지지 않는다).
    {
        int vsn = g_vspush < 2 ? 2 : (g_vspush > 16 ? 16 : g_vspush);
        const bool clamped = g_vspush > 16;      // 잘라 냈으면 상계가 아니다 ⇒ 검사를 낸다
        char want[32]; low_bfmt(want, sizeof want, "{}", PROVEN_ARG((int)(vsn)));
        const char *guard = clamped
            ? "    if (LW_UNLIKELY(vsp >= (int)(sizeof vs / sizeof vs[0])))"
              " lw_panic(\"vector stack overflow\");\n"
            : "";
        // ★★★★★ **평행 칸도 쓰는 자리에만 낸다** (2026-08-30, WO-0158 — `check-frames` 가 잡았다).
        //   `rssl[16][16]` 은 **4 KB** 다. 늘 내면 슬라이스 레코드를 안 쓰는 op 의 프레임까지
        //   커진다(실측: `grep.low` 2704 → 8896 B, +229%). RFC-0106 §6 이 예고한 위험이다.
        //   ⇒ 본문이 `rssl[` 를 쓸 때만 16 칸, 아니면 **1 칸**(C 는 0 칸 배열을 안 받는다).
        //   ☞ *기능을 더할 때 드는 값은 그 기능을 **안 쓰는 자리**에서 가장 아프다.*
        const char *rn = strstr(body, "rssl[rsp") || strstr(body, "rssl[%d")
                       || strstr(body, "memcpy(rssl") ? "16" : "1";
        for (const char *q = body; *q; ) {
            const char *hn = strstr(q, "@@VSN@@"), *hg = strstr(q, "@@VSG@@");
            const char *hr = strstr(q, "@@RSSLN@@");
            const char *hit = NULL;
            for (const char *c2 = hn; c2; c2 = NULL) hit = c2;
            if (hg && (!hit || hg < hit)) hit = hg;
            if (hr && (!hit || hr < hit)) hit = hr;
            if (!hit) { low_puts(out, q); break; }
            low_write(out, q, (size_t)(hit - q));
            if (hit == hr) { low_puts(out, rn); q = hit + 9; continue; }
            low_puts(out, hit == hn ? want : guard);
            q = hit + 7;
        }
    }
    low_free(nbody);
    (void)low_out_close(m);   // 메모리 통로와 그 글을 proven 힙에 돌려준다
}

// ★★★★ **앞 바퀴 값 들고 가기** (RFC-0111 §8-22 ⓑ, WO-0200 — 스칼라 치환).
//   `s[k]` 를 쓰고 다음 바퀴에 `s[k-1]` 을 읽는 루프(dp 의 `cur`)에서 GCC 는 그 값을 레지스터로
//   넘기는 predictive commoning 을 **루프 안에 경계 검사가 있으면 서지 않는다**(§8-21 — 손 C 도 같다).
//   ⇒ 방출기가 대신한다: 루프 머리 **앞**(흘러드는 길)에서 `k-1` 을 한 번 검사하고 `s[k-1]` 을
//   미리 읽어 `cc` 에 두고(`cv` = 경계 안인가), 저장 뒤에 `cc = v`. 읽는 자리는 `cv` 면 `cc` 를,
//   아니면 **원래 그대로** 검사하고 읽는다 — 그러면 첫 바퀴에서 원래와 같은 자리에서 패닉한다.
//   ☞ **의미는 그대로다**: 검사는 없어지지 않았고(첫 바퀴의 `k-1` 은 머리 앞에서, 뒤 바퀴의 `k-1` 은
//     바로 앞 바퀴의 저장이 이미 검사했다), 루프 안의 메모리 쓰기는 그 저장 하나뿐이다.
//   ☞ 모양은 **정확히 이것만** 받는다(넓히지 않는다 — 좁아야 안전 논증이 읽힌다):
//     L = `load s; load k; const 1; sub; index` · S = `load s; load k; load v; index.store` ·
//     I = `load k; const 1; add; store k`(S 뒤) · 가장 안쪽 루프 · 머리로 오는 길은 흘러듦과 뒤 간선뿐 ·
//     L 이 S 앞 · 몸 안 가지는 빠져나가거나 S 앞으로 · 허용 낱말(호출 없음) · `s` 를 다시 안 씀.
//   손으로 입혀 잰 값(2026-09-11, arch-dev): dp ×2.20 → **×1.79**(소스 탐침 `left` ×1.80 과 같다).
//   처음 입힌 모양(`cv` 를 0 에서 시작해 저장 뒤 1)은 ×2.08 에 그쳤다 — 바퀴마다 바뀌는 깃발이면
//   GCC 가 사슬을 못 편다. 머리 앞에서 정한 깃발은 루프 안에서 **안 바뀐다**.
typedef struct { int h, L, S, s, k, esz; bool on; } cbe_carry_t;
#define CBE_NCARRY 8
static bool g_no_carry;
// ★ 기계 암호 명령의 **범위**는 빌드가 정한다(RFC-0119 §9-3): 0 소프트만 · 1 기계 명령만 · 2 둘 담고 시작할 때 한 번 고른다.
static int g_hw_clmul;
void low_cbe_set_hw_clmul(int v) { g_hw_clmul = v; }
static int g_hw_aes;
void low_cbe_set_hw_aes(int v) { g_hw_aes = v; }
static int g_hw_simd;
void low_cbe_set_hw_simd(int v) { g_hw_simd = v; }
static int g_hw_avx2;
void low_cbe_set_hw_avx2(int v) { g_hw_avx2 = v; }
static int g_hw_asm;
void low_cbe_set_hw_asm(int v) { g_hw_asm = v; }
static int g_hw_vaes;
void low_cbe_set_hw_vaes(int v) { g_hw_vaes = v; }   /* ★ X-0055 — `--hw vaes` 로만 켠다(auto 에 안 든다) */
/* g_conc_t0 — --conc-t0: 호스트에서도 T0 런타임 (WO-0206) · 선언은 위(2a 가 먼저 읽는다) */
void low_cbe_set_no_carry(bool v) { g_no_carry = v; }
void low_cbe_set_conc_t0(bool v) { g_conc_t0 = v; }
static bool cbe_ld_is(const low_ir_ins_t *c, int slot) { return c->w == IRW_LOAD && c->a == slot; }
static bool cbe_is_target(const low_ir_def_t *d, int lo, int hi) {   // [lo,hi] 안을 가리키는 가지가 있나
    for (proven_size_t q = 0; q < d->ncode; q++)
        if ((d->code[q].w == IRW_BR || d->code[q].w == IRW_BRZ) && d->code[q].a >= lo && d->code[q].a <= hi) return true;
    return false;
}
static int cbe_carry_scan(const low_ir_def_t *d, const unsigned char *lk, cbe_carry_t *cy) {
    const int n = (int)d->ncode; const low_ir_ins_t *c = d->code;
    if (g_no_carry || d->ncode > 4096) return 0;
    for (int q = 0; q < n; q++) if (c[q].w == IRW_SWITCH) return 0;   // 표 점프는 과녁을 셀 수 없다
    int ncy = 0;
    for (int B = 0; B < n && ncy < CBE_NCARRY; B++) {
        if (c[B].w != IRW_BR || c[B].a < 1 || c[B].a >= B) continue;
        const int h = (int)c[B].a;
        if (c[h-1].w == IRW_BR || c[h-1].w == IRW_RET) continue;       // 흘러들어오지 않는다
        bool bad = false; int S = -1, I = -1, L = -1;
        for (int q = 0; q < n && !bad; q++) {
            const bool jmp = c[q].w == IRW_BR || c[q].w == IRW_BRZ;
            if (jmp && c[q].a == h && q != B) bad = true;                // 머리로 오는 둘째 길(`continue` 등)
            if (jmp && (q < h || q > B) && c[q].a > h && c[q].a <= B) bad = true;   // 몸 가운데로 들어오는 길
            if (q < h || q > B) continue;
            switch (c[q].w) {
                case IRW_CONST: case IRW_LOAD: case IRW_STORE: case IRW_DROP:
                case IRW_ADD: case IRW_SUB: case IRW_MUL: case IRW_DIV: case IRW_MOD: case IRW_NEG:
                case IRW_AND: case IRW_OR: case IRW_NOT:
                case IRW_EQ: case IRW_NE: case IRW_LT: case IRW_LE: case IRW_GT: case IRW_GE:
                case IRW_RET: case IRW_LEN: case IRW_INDEX: break;
                case IRW_CAST: break;   // RFC-0149 — 첨자(`usize`)를 값과 셈하려면 `cast` 가 낀다. 값 하나를 값 하나로 바꿀 뿐 메모리는 건드리지 않는다
                case IRW_BR: case IRW_BRZ:
                    if (q != B && c[q].a >= h && c[q].a <= q) bad = true;    // 안쪽 루프 — 가장 안쪽만
                    break;
                case IRW_ISTORE: if (S >= 0) bad = true; S = q; break;       // 메모리 쓰기는 하나뿐
                default: bad = true;                                        // 호출 · 맞바꿈 · 자르기 …
            }
        }
        if (bad || S < h + 3) continue;
        if (!(c[S-3].w == IRW_LOAD && c[S-2].w == IRW_LOAD && c[S-1].w == IRW_LOAD)) continue;
        const int s = (int)c[S-3].a, k = (int)c[S-2].a, v = (int)c[S-1].a;
        if (s < 0 || s >= 32 || k < 0 || k >= 32 || v < 0 || v >= 32 || s == k) continue;
        if (lk[s] != K_SL || lk[k] != K_INT || lk[v] != K_INT) continue;
        if (cbe_is_target(d, S - 2, S)) continue;
        for (int q = h; q <= B && !bad; q++) {
            if (c[q].w != IRW_STORE) continue;
            if (c[q].a == s) bad = true;                                    // 슬라이스를 루프에서 다시 쓴다
            else if (c[q].a == k) {
                if (I >= 0 || q < S + 4 || !cbe_ld_is(&c[q-3], k) || c[q-2].w != IRW_CONST ||
                    c[q-2].a != 1 || c[q-1].w != IRW_ADD) bad = true;       // 한 바퀴에 정확히 +1, S 뒤
                I = q;
            }
        }
        if (bad || I < 0 || cbe_is_target(d, I - 2, I)) continue;
        for (int q = h; q < B && !bad; q++)                                 // S 는 매 바퀴 돈다
            if ((c[q].w == IRW_BR || c[q].w == IRW_BRZ) && c[q].a >= h && c[q].a <= B &&
                (q >= S - 3 || c[q].a > S - 3)) bad = true;
        if (bad) continue;
        for (int q = h + 4; q < S - 3 && L < 0; q++)
            if (c[q].w == IRW_INDEX && cbe_ld_is(&c[q-4], s) && cbe_ld_is(&c[q-3], k) &&
                c[q-2].w == IRW_CONST && c[q-2].a == 1 && c[q-1].w == IRW_SUB && !cbe_is_target(d, q - 3, q)) L = q;
        if (L < 0) continue;
        cy[ncy++] = (cbe_carry_t){ h, L, S, s, k, 0, false };
    }
    return ncy;
}

static void cbe_scalar_body_raw(const low_ir_t *ir, const low_ir_def_t *d, low_out_t *out) {
    g_vspush = 0;                     // ★ 이 함수가 벡터를 몇 번 밀어 넣는지 센다
    memset(g_fitn, 0, sizeof g_fitn);
    proven_size_t idx_at = (proven_size_t)-1; unsigned idx_m = 0;   // ★ §8-10 — 마지막으로 정수 칸을 읽은 자리와 그 칸의 폭
    kstack_t *at = cbe_at_ensure(0, d->ncode);
    if (!at || !cbe_lbuf_ensure(d->nlocals)) { low_puts(out, "    lw_panic(\"compiler out of memory\");\n"); return; }
    unsigned char *lk = g_lb.lk, *slot = g_lb.slot;
    for (proven_size_t i = 0; i < d->ncode; i++) at[i].set = false;
    for (proven_size_t i = 0; i < g_lb.cap; i++) slot[i] = 0xff;
    cbe_local_kinds(d, lk, g_lb.cap);
    for (proven_size_t q = 0; q < d->nparams && q < g_lb.cap; q++) slot[q] = lk[q];
    unsigned char rk_ = 0xff, *rmap = g_lb.rmap, *lez = g_lb.lez, *lln = g_lb.lln, *lfl = g_lb.lfl;
    for (proven_size_t q = 0; q < g_lb.cap; q++) { rmap[q] = 0xff; lez[q] = 0; lln[q] = 0; lfl[q] = 0; }
    cbe_nodeview_seed(ir, d, lk, lez, lfl, g_lb.cap);   // ★ 2026-11-02 — 마디의 뷰 매개변수(kind_ok 와 같다)
    for (proven_size_t q = 0; q < d->nparams && q < g_lb.cap; q++) slot[q] = lk[q];
    for (proven_size_t q = 0; q < d->nparams && q < g_lb.cap; q++)   // ★ 2026-11-01 — option(슬라이스) 파라미터의 원소 폭(vn — 1 이면 0)
        if (((d->param_opt >> q) & 1u) && d->param_optsl[q] > 1) lln[q] = d->param_optsl[q];
    for (proven_size_t q = 0; q < d->nparams && q < g_lb.cap; q++) {
        // ★★★ 슬라이스 파라미터는 **바이트로 반입**된다(lez=1) — kind_ok 와 **같은 규약**이어야
        //   한다. 원소 폭으로 두면 본문의 view_array 가 이 pre-pass 에서 **실패**하고(ve≠1),
        //   그러면 그 뒤 지역(벡터·타입 슬라이스)의 종류가 안 채워져 **정수로 로드**된다
        //   (pass_vec 이 벡터를 정수로 넘겨 쓰레기를 읽었다 — 차분 스윕이 놓친 자리).
        if ((d->param_slice >> q) & 1u) {
            lez[q] = g_no_elemsl ? 1 : d->param_selem[q] ? 0 : (unsigned char)cbe_pslice_w(d, q);   // ★ 단계 1 — kind_ok 와 같은 규약
            if (!d->param_selem[q] && d->param_ebits[q] > 100 && d->param_ebits[q] < 164) lfl[q] = 2;   // ★ kind_ok 와 같은 부호 표식
            if (!d->param_selem[q] && ((d->param_sflt >> q) & 1u)) lfl[q] = CBE_FL_FLT;   // ★ kind_ok 와 같은 부동 표식(2026-11-01)
            if (!d->param_selem[q] && d->param_rowb[q] && !g_no_elemsl) { lez[q] = d->param_rowe[q]; lln[q] = d->param_rowb[q]; lfl[q] = (unsigned char)(d->param_rowfl[q] | (d->param_rowsub[q] << 2)); }   // ★ 줄의 나열
        }
        else if ((d->param_vec >> q) & 1u) {   // ★ 벡터 파라미터 — 원소 폭·레인 수
            lez[q] = d->param_ebits[q]; lln[q] = d->param_vlanes[q];
            lfl[q] = ((d->param_vflt >> q) & 1u) ? CBE_FL_FLT : ((d->param_vsgn >> q) & 1u) ? CBE_FL_SGN : 0;   // ★ 2026-11-01 — 레인의 부동 · 부호
        }
    }
    // ★★ 2026-10-31 (W-20261031-2053) — 반환 표식을 **비우고** 돌린다(cbe_kind_ok 와 같게). 전에는 앞서 잰 다른 op 의 값(`g_ret_optrec`)이
    //   남아 있어서, 그 값을 보는 규칙(슬라이스 option · 겹 option 의 `return error …` 맞추기)이 여기서만 다르게 갈렸고, 그러면 추적이
    //   중간에 멈춘 채(답을 버렸다) 지역 종류표가 반만 찬 C 가 나갔다 — `lib/pem.low` 의 `unwrap` 이 겹 option 지역을 수로 읽었다
    //   (네이티브 «is_ok needs a result», VM 은 멀쩡했다). 판정 때 통과한 op 이 여기서 멈추면 처리기의 결함이다 — 그렇게 말한다.
    g_ret_optrec = 0; g_ret_optnode = 0; g_ret_veset = false; g_ret_ve = 1; g_ret_fl = 0; g_ret_rb = 0;
    if (!cbe_kind_run(ir, d, NULL, at, lk, slot, &rk_, rmap, lez, lln, lfl))   // lk/slot/rmap/lez 를 채운다
        low_fpf(low_stderr(), "lowentc: internal: the fast path's kind trace stopped while emitting `{}` (it passed when the op was judged){}{}\n",
                low_sv((const char *)d->name.ptr, (int)d->name.size), proven_arg_cstr(g_sub[0] ? " — " : ""), proven_arg_cstr(g_sub));
    cbe_carry_t cy[CBE_NCARRY];
    const int ncy = cbe_carry_scan(d, lk, cy);   // ★ §8-22 ⓑ — 앞 바퀴 값을 들고 갈 루프

    // ★ 분기 목표 표도 **명령 수만큼** 잡는다(X-0028) — 4096 은 판단이 아니라 배열 크기였다.
    bool *target = (bool *)low_zalloc(d->ncode + 1, sizeof *target);   // ★ W-20261112-0207 — proven 힙
    if (!target) { low_puts(out, "    lw_panic(\"compiler out of memory\");\n"); return; }
    for (proven_size_t j = 0; j < d->ncode; j++)
        if (d->code[j].w == IRW_BR || d->code[j].w == IRW_BRZ)
            if (d->code[j].a >= 0 && (proven_size_t)d->code[j].a < d->ncode) target[d->code[j].a] = true;

    // ★ 프레임 선언은 **본문을 보고** 정한다 — 아래 감싸개가 이 표식을 바꿔 넣는다.
    //   여기서 종류를 미리 추론하면 그 추론이 **두 벌**이 되고, 두 벌은 갈린다(교훈 7).
    low_puts(out, "@@LWFRAME@@\n");
    // ★ encode 자리마다 **프레임 버퍼** 하나. 태그 경로의 **8칸 풀**이 여기서 사라진다.
    {
        int nenc = 0;
        for (proven_size_t j = 0; j < d->ncode; j++) if (d->code[j].w == IRW_ENCODE) nenc++;
        // ★★★ **버퍼는 반환보다 오래 살아야 한다.** 처음엔 프레임에 뒀고, 그러자 `encode` 가
        //   돌려준 슬라이스가 **반환과 함께 늘어졌다**(쓰레기 바이트). 태그 경로가 풀을 쓴
        //   이유가 정확히 그것이다. ⇒ **스레드 지역 정적** 버퍼. 스레드마다 자기 것을 갖는다
        //   (전역이면 `parallel` 워커들이 서로의 버퍼를 밟는다 — DRF 를 지킨다).
        if (nenc) low_fpf(out, "    static _Thread_local unsigned char encb[{}][64];"
                               " (void)encb;\n", PROVEN_ARG((int)(nenc)));
    }
    // ★ 스택 지역만 배열을 받는다 — 전부 주면 프레임이 32KB 가 된다(재귀에서 치명적).
    int *smap = g_lb.smap; int nstk = 0;
    for (proven_size_t q = 0; q < g_lb.cap; q++) smap[q] = -1;
    for (proven_size_t q = 0; q < d->nlocals && q < g_lb.cap; q++)
        if (lk[q] == K_STK) smap[q] = nstk++;
    if (nstk) {
        low_fpf(out, "    long long sstk[{}][{}]; int sn[{}] = {{0}}; (void)sstk;\n",
                PROVEN_ARG((int)(nstk)), PROVEN_ARG((int)(K_STK_CAP)), PROVEN_ARG((int)(nstk)));
        // ★ 스택별 선언 capacity(SNEW 가 담고 SPUSH 가 본다). 기본은 상한(안전).
        low_fpf(out, "    long long scap[{}]; for (int q_ = 0; q_ < {}; q_++) scap[q_] = {}; (void)scap;\n",
                PROVEN_ARG((int)(nstk)), PROVEN_ARG((int)(nstk)), PROVEN_ARG((int)(K_STK_CAP)));
    }
    { proven_size_t ni = 0, ns = 0, nf2 = 0, nr = 0, nb2 = 0, nao2 = 0, nav2 = 0;
      for (proven_size_t q = 0; q < d->nparams; q++) {
          if ((d->param_slice >> q) & 1u) low_fpf(out, "    sloc[{}] = as[{}];\n", PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)ns++)));
          else if ((d->param_struct >> q) & 1u) {
              // ★★★ 구조체 파라미터는 **참조**로 온다(`ar`) — 액터 핸들러가 인스턴스의 필드를
              //   올리면 그 쓰기가 **호출자에게 보여야** 하기 때문이다.
              // ★ 파라미터의 슬라이스 칸을 **지역 칸으로 옮긴다**(RFC-0106 단계 3).
              //   그러면 `field` 읽기는 파라미터든 지역이든 **한 자리**만 본다.
              if (cbe_pnode(ir, d, q) >= 0)   // ★ 2026-11-02 — 마디의 뷰: 바이트를 가리키는 주소 하나(지역은 슬라이스 칸에 산다)
                  low_fpf(out, "    sloc[{}].p = ar[{}]; sloc[{}].n = {};\n", PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)nr++)), PROVEN_ARG((size_t)((size_t)q)),
                          PROVEN_ARG((unsigned)((unsigned)ir->structs[cbe_pnode(ir, d, q)].total)));
              else if (cbe_struct_has_sl(ir, (int)d->param_sidx[q]))
                  low_fpf(out, "    rloc[{}] = ar[{}]; memcpy(rlocsl[{}], &rsl[{} * LW_RECF], sizeof rlocsl[0]);\n",
                          PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)nr++)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)q)));
              else low_fpf(out, "    rloc[{}] = ar[{}];\n", PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)nr++)));
          }
          else if ((d->param_opt >> q) & 1u) low_fpf(out, "    wloc[{}] = ao[{}];\n", PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)nao2++)));  // ★ option·result 파라미터
          else if ((d->param_vec >> q) & 1u) low_fpf(out, "    vloc[{}] = av[{}];\n", PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)nav2++)));  // ★ 벡터 파라미터
          else if ((d->param_flt >> q) & 1u) low_fpf(out, "    floc[{}] = af[{}];\n", PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)nf2++)));
          else if ((d->param_bset >> q) & 1u) {
              // ★★★ 비트셋 파라미터는 **참조**로 온다 — BFS 의 `visited` 집합처럼,
              //   피호출자가 세운 비트가 **호출자에게 보여야** 하기 때문이다.
              low_fpf(out, "    loc[{}] = ab[{}];\n", PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)nb2++)));
          }
          else if ((d->param_w128 >> q) & 1u) {   // ★ W-20261105-2304 — 128 비트 입력은 정수 칸 둘(아래 · 위)로 온다
              low_fpf(out, "    lw_hloc[{}] = LW_WOF(ai[{}], ai[{}]);\n", PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)ni)), PROVEN_ARG((size_t)((size_t)ni + 1))); ni += 2; }
          else                            low_fpf(out, "    loc[{}] = ai[{}];\n",  PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)ni++)));
      } }
    if (g_cur_di >= 512 || g_needdepth[g_cur_di])
        low_puts(out, "    if (LW_UNLIKELY(++lw_depth > 128)) lw_panic(\"call depth limit exceeded\");\n");
    else
        // ★ 재귀가 불가능한 op — **세지 않는다**(전에도 그랬다). 이제 원장이 파라미터라
        // ★ **가릴 것은 반드시 가린다.** 반환 자리들은 여전히 `lw_depth--` 를 낸다:
        //   인자를 받았으면 그 파라미터가 줄고(죽은 감소 — 컴파일러가 지운다), 인자를 안
        //   받았으면 **가릴 지역이 없으면 스레드지역 전역에 닿는다**. 실측으로 그 구멍이
        //   json 을 59.4 → 73.0 ms 로 무너뜨렸다 — 세지도 않는 함수가 반환마다 TLS 에 썼다.
        //   표현은 하나, 비용은 0 이려면 **두 경우 다** 지역이 있어야 한다.
        low_puts(out, cbe_depparam(g_cur_di) ? "    (void)lw_depth;\n"
                                      : "    int lw_depth = 0; (void)lw_depth;\n");
    for (int q = 0; q < ncy; q++)
        low_fpf(out, "    long long lw_cv{} = 0, lw_cc{} = 0; (void)lw_cv{}; (void)lw_cc{};\n", PROVEN_ARG((int)(q)), PROVEN_ARG((int)(q)), PROVEN_ARG((int)(q)), PROVEN_ARG((int)(q)));
    low_puts(out, /* ★★★★ **구조체 스택도 "어디를 가리키나" 다** (RFC-0039 §9-4, 2026-08-03).
 *   지역을 그렇게 바꾼 뒤에도 스택은 저장이라, 필드 하나 읽으려고 `memcpy(rs[rsp++], rloc[0], 8)`
 *   로 **복사했다가 다시 읽었다** — 그 복사가 세 라이브러리 op 마다 `memcpy` 호출로 남았다
 *   (`post` 78 B · `take` 80 B · `pending` 92 B — 필드 하나 만지는 op 치고 터무니없다).
 *   ⇒ 지역에서 밀 때는 **가리키기만** 한다. 스스로 만든 것만 뒷집(`rsb`)에 산다. */
"    long long st[64]; lw_sl ss[16]; unsigned char *rs[16]; _Alignas(8) unsigned char rsb[16][LW_RECB];\n"
        /* ★★★★★ RFC-0106 단계 3 — 레코드의 **슬라이스 필드**는 바이트 버퍼가 아니라
           이 평행 칸에 산다. 스택 자리마다 하나(`rssl`), 지역마다 하나(`rlocsl`). */
        "    lw_sl rssl[@@RSSLN@@][LW_RECF]; (void)rssl;\n"
"    lw_r ws[16]; double fs[64];\n"
          // ★ 자리표 — 본문을 다 내고 나서야 몇 칸이 필요한지 안다(위 g_vspush 주석).
          "    lw_v vs[@@VSN@@];\n"
          "    int sp = 0, ssp = 0, rsp = 0, wsp = 0, fsp = 0, vsp = 0;\n"
          "    (void)vs; (void)vsp;\n"
          "    (void)st; (void)ss; (void)rs; (void)rsb; (void)ws; (void)fs;\n"
          "    (void)sp; (void)ssp; (void)rsp; (void)wsp; (void)fsp;\n");

    // 종류를 다시 돌리며 방출한다 — 어느 스택에서 꺼낼지는 시뮬레이터가 안다.
    kstack_t ks = { {0},{0},{0},{0},{0}, 0, true };
    bool live2 = true;
    int encn = 0;
    kstack_t *at2 = cbe_at_ensure(1, d->ncode);
    if (!at2) { low_free(target); low_puts(out, "    lw_panic(\"compiler out of memory\");\n"); return; }
    for (proven_size_t i = 0; i < d->ncode; i++) at2[i].set = false;

    proven_u32 lastln_ = 0; proven_u16 lastfid_ = 0;
    for (proven_size_t j = 0; j < d->ncode; j++) {
        const low_ir_ins_t *in = &d->code[j];
        cbe_line(out, ir, in, &lastln_, &lastfid_);
        // ★★★ **도달 불가 코드는 찍지 않는다.** 시뮬레이터는 건너뛰는데 방출기는 계속 찍었고,
        //   그러자 종류 스택이 **음수**로 내려가 쓰레기 종류가 구조체 배열을 **벗어난 인덱스**로
        //   쓰였다 — **컴파일러가 조용히 죽었다**(json.low). 두 층이 **같은 규칙**을 봐야 한다.
        if (at2[j].set) { ks = at2[j]; live2 = true; }
        else if (!live2) continue;
        if (ks.n < 0) ks.n = 0;
        // ★ §8-22 ⓑ — 머리 **앞**(흘러드는 길에서만 지난다): `k-1` 을 한 번 검사하고 `s[k-1]` 을 미리 읽는다.
        for (int q = 0; q < ncy; q++) if ((int)j == cy[q].h) {
            const int ez = lez[cy[q].s] <= 0 ? 1 : lez[cy[q].s];
            cy[q].on = lfl[cy[q].s] == 0 && (ez == 1 || ez == 2 || ez == 4 || ez == 8);
            if (!cy[q].on) continue;
            cy[q].esz = ez;
            low_fpf(out, "    {{ unsigned long long u_ = (unsigned long long)loc[{}] - 1ull; lw_sl s_ = sloc[{}];\n"
                         "      lw_cv{} = u_ < (unsigned long long)s_.n;\n", PROVEN_ARG((int)(cy[q].k)), PROVEN_ARG((int)(cy[q].s)), PROVEN_ARG((int)(q)));
            if (ez == 1) low_fpf(out, "      lw_cc{} = lw_cv{} ? (long long)s_.p[u_] : 0; }}\n", PROVEN_ARG((int)(q)), PROVEN_ARG((int)(q)));
            else low_fpf(out, "      lw_cc{} = lw_cv{} ? (long long)lw_ld_le(s_.p + (size_t)u_ * {}, {}) : 0; }}\n",
                         PROVEN_ARG((int)(q)), PROVEN_ARG((int)(q)), PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ez)));
        }
        if (target[j]) low_fpf(out, "L{}:\n", PROVEN_ARG((size_t)((size_t)j)));
        { const int w128_ = cbe_w128_step(&ks, in, out);   // ★ W-20261105-2304 — 128 비트 낱말(종류 실행과 같은 함수)
          if (w128_ == 1) continue;
          if (w128_ == 0) { low_puts(out, "    lw_panic(\"fast path: a 128-bit word the kind run did not accept\");\n"); continue; } }
        switch (in->w) {
            case IRW_CONST: low_fpf(out, "    st[sp++] = {}ll;\n", PROVEN_ARG((long long)((long long)in->a))); ks.o[ks.n] = -1; ks.k[ks.n++] = K_INT; break;
            case IRW_LOAD:
                // ★★ 2026-11-02 (W-20261102-0024) — 종류 실행처럼 **모든** 갈래가 지역의 표식을 먼저 읽는다(갈래가 고쳐 쓸 수는 있다).
                //   정수 · 가림막 갈래가 이것을 안 해서 가림막의 레인 수가 그 칸의 옛 값이었다(`store` 뒤 `lane_all` 이 늘 참).
                ks.ve[ks.n] = lez[in->a]; ks.vn[ks.n] = lln[in->a]; ks.fl[ks.n] = lfl[in->a];
                if (lk[in->a] == K_SL || lk[in->a] == K_ROWS || lk[in->a] == K_AREF || lk[in->a] >= K_VIEW) {
                    low_fpf(out, "    ss[ssp++] = sloc[{}];\n", PROVEN_ARG((long long)((long long)in->a)));
                    ks.o[ks.n] = (signed char)in->a; ks.k[ks.n] = lk[in->a];
                    ks.ve[ks.n] = lez[in->a]; ks.vn[ks.n] = lln[in->a]; ks.fl[ks.n] = lfl[in->a]; ks.n++;
                }
                else if (lk[in->a] == K_FLT) {
                    low_fpf(out, "    fs[fsp++] = floc[{}];\n", PROVEN_ARG((long long)((long long)in->a)));
                    ks.o[ks.n] = (signed char)in->a; ks.k[ks.n++] = K_FLT;
                }
                else if (lk[in->a] == K_W128) {   // ★ W-20261105-2304
                    low_fpf(out, "    lw_hs[lw_hsp++] = lw_hloc[{}];\n", PROVEN_ARG((long long)((long long)in->a)));
                    ks.o[ks.n] = (signed char)in->a; ks.ve[ks.n] = 0; ks.vn[ks.n] = 0; ks.fl[ks.n] = 0; ks.k[ks.n++] = K_W128;
                }
                else if (lk[in->a] == K_REF) {
                    low_puts(out, "    /* ref local: a name — nothing to load */\n");
                    ks.o[ks.n] = (signed char)rmap[in->a]; ks.k[ks.n++] = K_REF;
                }
                else if (lk[in->a] == K_VEC) {
                    // ★ 벡터도 **지역에 담긴다**(`var va be vec u32 4 load …`).
                    //   이 자리가 없어서 벡터가 **정수 스택**에서 꺼내졌다 — 조용히 틀린 레인.
                    // ★★★ **값의 실제 폭만 나른다** (RFC-0040 §9-4, 2026-08-02 실측).
                    //   칸은 가장 넓은 벡터에 맞춰 64바이트다. 통째로 대입하면 `vec u32 4`
                    //   (16바이트) 하나를 옮기는 데 **64바이트를 복사한다** — 네 배 대역이고,
                    //   그 크기 때문에 컴파일러가 레지스터에 얹지 못하고 스택으로 돌린다.
                    //   레인 수·폭은 컴파일 시각에 알려져 있으니 그만큼만 옮긴다.
                    g_vspush++;
                    low_puts(out, "@@VSG@@");
                    low_fpf(out, "    vs[vsp++] = vloc[{}];\n", PROVEN_ARG((long long)((long long)in->a)));
                    ks.o[ks.n] = -1; ks.k[ks.n] = K_VEC;
                    ks.vn[ks.n] = lln[in->a]; ks.ve[ks.n] = lez[in->a]; ks.fl[ks.n] = lfl[in->a];
                    ks.n++;
                }
                else if (lk[in->a] == K_OPT || lk[in->a] == K_OPTSL || lk[in->a] == K_OPT2) {
                    // ★ option·result 도 **지역에 담긴다** — `var r be result u8 e g n .`
                    //   이 자리가 없어서 감싼 값이 **정수 스택**에서 꺼내졌다(스택 붕괴).
                    if (cbe_optrec_sl(ir, lk[in->a], lez[in->a], lfl[in->a]))
                        low_fpf(out, "    memcpy(wssl[wsp], wlocsl[{}], sizeof wssl[0]); ws[wsp++] = wloc[{}];\n", PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((long long)((long long)in->a)));
                    else
                    low_fpf(out, "    ws[wsp++] = wloc[{}];\n", PROVEN_ARG((long long)((long long)in->a)));
                    ks.o[ks.n] = (signed char)in->a; ks.ve[ks.n] = lez[in->a]; ks.fl[ks.n] = lfl[in->a];
                    ks.vn[ks.n] = lk[in->a] == K_OPTSL ? lln[in->a] : 0;   // ★ 슬라이스 option 의 원소 폭(2026-11-01)
                    ks.k[ks.n++] = lk[in->a];   // ★ 내부 종류 표식(레코드·뷰) 복원 · WO-0222 슬라이스 option
                }
                else if (lk[in->a] >= K_REC) {
                    // ★ 그 타입의 **실제 크기**만 옮긴다 — 칸이 64 라고 64 를 나를 이유가 없다.
                    if (cbe_struct_has_sl(ir, lk[in->a] - K_REC))
                        low_fpf(out, "    memcpy(rssl[rsp], rlocsl[{}], sizeof rssl[0]); rs[rsp++] = rloc[{}];\n",
                                PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((long long)((long long)in->a)));
                    else low_fpf(out, "    rs[rsp++] = rloc[{}];\n", PROVEN_ARG((long long)((long long)in->a)));
                    ks.o[ks.n] = (signed char)in->a; ks.k[ks.n++] = lk[in->a];
                }
                else if (lk[in->a] == K_STK) {
                    // ★ 스택은 **집이 곧 그 자신**이다 — 실을 값이 없다. 자리만 민다.
                    low_puts(out, "    st[sp++] = 0;\n");
                    ks.o[ks.n] = (signed char)in->a; ks.k[ks.n++] = K_STK;
                }
                else {
                    // ★ 정수와 **비트셋**은 같은 곳(`loc`)에 산다 — 그러나 **종류는 다르다.**
                    //   여기서 K_INT 로 밀어 넣었더니 `add b k`(비트 세우기)가 **정수 덧셈**이 됐다.
                    //   (그리고 그것을 **비트셋 픽스처를 만들자마자** 잡았다 — 교훈 6.)
                    low_fpf(out, "    st[sp++] = loc[{}];\n", PROVEN_ARG((long long)((long long)in->a)));
                    ks.o[ks.n] = (signed char)in->a; ks.k[ks.n++] = lk[in->a];
                }
                break;
            case IRW_STORE: {
                unsigned char kk = ks.k[--ks.n];
                if (kk == K_INT && in->a >= 0 && in->a < 64 && ((d->loc_w128 >> in->a) & 1u)) {   // ★ W-20261105-2304 — 종류 실행과 짝
                    low_puts(out, "    lw_hs[lw_hsp++] = (lw_u128)(unsigned long long)st[--sp];\n"); kk = K_W128; }
                // ★★★ **지역이 원소폭을 기억해야 한다.** 이 방출기는 여태 `lez` 를 파라미터에만
                //   채웠다 — 그래서 `let b be slice u16 u"AB" .` 처럼 **타입 있는 슬라이스를
                //   지역에 담았다가 다시 꺼내면** 폭이 1 로 되돌아가고 `index` 가 바이트를 읽었다.
                //   (실측: VM 42 · 네이티브 94 — 같은 결함 부류가 이 저장소에 이미 있었다.)
                //   ☞ 시뮬레이터(`cbe_kind_run`)는 이 규약을 이미 지키고 있었다. **두 반쪽이
                //     같은 것을 다르게 알고 있었고**, 방출기 쪽이 덜 알았다.
                if (kk == K_SL || kk == K_ROWS || kk == K_VEC || kk == K_MASK || kk >= K_VIEW || kk == K_OPT || kk == K_OPTSL || kk == K_AREF) {
                    lez[in->a] = ks.ve[ks.n]; lln[in->a] = ks.vn[ks.n]; lfl[in->a] = ks.fl[ks.n];
                }
                if (kk == K_SL || kk == K_ROWS || kk == K_AREF || kk >= K_VIEW) low_fpf(out, "    sloc[{}] = ss[--ssp];\n", PROVEN_ARG((long long)((long long)in->a)));
                else if (kk == K_FLT) low_fpf(out, "    floc[{}] = fs[--fsp];\n", PROVEN_ARG((long long)((long long)in->a)));
                else if (kk == K_W128) low_fpf(out, "    lw_hloc[{}] = lw_hs[--lw_hsp];\n", PROVEN_ARG((long long)((long long)in->a)));   // ★ W-20261105-2304
                else if (kk == K_VEC) low_fpf(out, "    vloc[{}] = vs[--vsp];\n", PROVEN_ARG((long long)((long long)in->a)));
                else if (cbe_optrec_sl(ir, kk, ks.ve[ks.n], ks.fl[ks.n]))
                    low_fpf(out, "    wloc[{}] = ws[--wsp]; memcpy(wlocsl[{}], wssl[wsp], sizeof wlocsl[0]);\n", PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((long long)((long long)in->a)));
                else if (kk == K_OPT || kk == K_OPTSL || kk == K_OPT2) low_fpf(out, "    wloc[{}] = ws[--wsp];\n", PROVEN_ARG((long long)((long long)in->a)));
                else if (kk == K_REF) low_puts(out, "    /* ref stored: the name is static */\n");
                else if (kk >= K_REC && cbe_struct_has_sl(ir, kk - K_REC)) low_fpf(out, "    rloc[{}] = rbuf[{}]; memcpy(rloc[{}], rs[--rsp], {});"
                                              " memcpy(rlocsl[{}], rssl[rsp], sizeof rssl[0]);\n",
                                              PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((long long)((long long)in->a)),
                                              PROVEN_ARG((unsigned)((unsigned)ir->structs[kk - K_REC].total)), PROVEN_ARG((long long)((long long)in->a)));
                else if (kk >= K_REC && g_cur_di < 512 && in->a < 64 && ((g_faloc[g_cur_di] >> in->a) & 1u))   // ★ 2a — 그 액터는 자기 자리에 산다
                    low_fpf(out, "    lw_fa_home(&fa{}_, &rloc[{}]); memcpy(rloc[{}], rs[--rsp], {});\n",
                            PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((unsigned)((unsigned)ir->structs[kk - K_REC].total)));
                else if (kk >= K_REC) low_fpf(out, "    rloc[{}] = rbuf[{}]; memcpy(rloc[{}], rs[--rsp], {});\n",
                                              PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((long long)((long long)in->a)),
                                              PROVEN_ARG((unsigned)((unsigned)ir->structs[kk - K_REC].total)));
                else if (kk == K_STK) low_fpf(out, "    scap[{}] = st[sp-1]; sp--; sn[{}] = 0;\n", PROVEN_ARG((int)(smap[in->a])), PROVEN_ARG((int)(smap[in->a])));
                else {
                    low_fpf(out, "    loc[{}] = st[--sp];\n", PROVEN_ARG((long long)((long long)in->a)));
                    if (kk == K_INT && in->a >= 0 && in->a < 64 && (proven_size_t)in->a < d->nlocals && lk[in->a] == K_INT &&
                        !target[j] && cbe_store_fits(d, j, d->loc_meta[in->a], (j > 0 && idx_at == j - 1) ? idx_m : 0) &&
                        g_fitn[in->a] < 60000)
                        g_fitn[in->a]++;
                }
                break;
            }
            case IRW_LEN:
                low_puts(out, "    st[sp++] = (long long)ss[--ssp].n;\n");
                ks.k[ks.n-1] = K_INT; break;
            // ★★★ **내용 계약 검사** (RFC-0111 §8-11) — 진입에서 슬라이스를 한 번 훑는다.
            //   태그 경로의 `lw_elemck` 와 **같은 뜻**이어야 한다: 원소는 리틀엔디언으로 조립하고
            //   부호 있는 정수로 견준다(u64 의 최상위 비트가 선 값은 음수로 보인다 — VM 도 그렇다).
            //   ★ 비용을 숨기지 않는다: **O(원소 수)** 이고 op 진입마다 한 번이다. 그 대신
            //     안쪽 루프의 넘침 검사가 사라진다 — 그 거래가 이 절의 값이다.
            case IRW_ELEMCK: {
                int esz_ = (int)ks.ve[ks.n-2]; if (esz_ <= 0) esz_ = 1;
                int cmp_ = (int)(in->a & 3);
                const char *rel_ = cmp_ == 0 ? "<" : cmp_ == 1 ? "<=" : cmp_ == 2 ? ">" : ">=";
                low_fpf(out,
                    "    {{ long long n_ = st[--sp]; lw_sl s_ = ss[--ssp]; long long ok_ = 1;\n"
                    "      for (size_t q_ = 0; q_ < s_.n; q_++) {{\n"
                    "        long long e_ = (long long)lw_ld_le(s_.p + q_ * {}, {});\n"
                    "        if (!(e_ {} n_)) {{ ok_ = 0; break; }}\n"
                    "      }}\n"
                    "      st[sp++] = ok_; }}\n", PROVEN_ARG((int)(esz_)), PROVEN_ARG((int)(esz_)), proven_arg_cstr(rel_));
                ks.n--; ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1;
                ks.ve[ks.n-1] = 0; ks.fl[ks.n-1] = 0; break;
            }
            case IRW_CSTR2STR:
                // ★★★ **cstr → str** (RFC-0068 S4 · C2) — 포인터 스칼라(정수 스택)를 뷰 슬라이스로.
                //   길이를 몰라 strlen 으로 스캔한다(O(n) — 이름이 비용을 말한다). **복사 0**(뷰다).
                //   ★ RFC-0137 P3 — cstr 는 슬라이스 칸에 포인터로 실려 온다(정수를 거치지 않는다). 길이만 다시 센다.
                low_puts(out, "    { const char *p_ = (const char *)ss[ssp-1].p;\n"
                      "      ss[ssp-1].n = p_ ? strlen(p_) : 0; }\n");
                ks.k[ks.n-1] = K_SL; ks.ve[ks.n-1] = 1; ks.o[ks.n-1] = -1; break;
            case IRW_STR2CSTR:
                // ★★★ **str_buf → cstr** (RFC-0068 S4 · C4) — 바이트 버퍼의 base 포인터를 cstr 로.
                //   **O(1)·복사 0**(널종단은 sb_as_cstr 가 봉인해 뒀다·D2). 정수 스칼라로 실린다.
                //   ★ RFC-0137 P3 — 슬라이스 칸에 그대로 남는다(포인터를 정수에 싣지 않는다). 방출 0.
                ks.k[ks.n-1] = K_SL; ks.ve[ks.n-1] = 1; ks.o[ks.n-1] = -1; break;
            case IRW_INDEX: {
                if (ks.k[ks.n-2] == K_ROWS) {   // ★ 줄 하나를 보는 슬라이스 — 포인터를 i × 줄 바이트만큼 옮기고 길이는 줄 안 원소 수
                    const unsigned rb_ = ks.vn[ks.n-2], ez_ = ks.ve[ks.n-2] ? ks.ve[ks.n-2] : 1;
                    low_puts(out, "    { long long i_ = st[--sp]; lw_sl s_ = ss[ssp-1];\n");
                    if (!(in->a & IR_POL_PROVEN))
                        low_puts(out, "      if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ >= s_.n)) lw_panic(\"slice index out of bounds\");\n");
                    const unsigned sub_ = CBE_FL_RSUB(ks.fl[ks.n-2]);
                    if (sub_) {   // ★ 2026-11-02 — 아래 줄의 나열: 길이는 줄 수(줄 바이트 / 아래 줄 바이트) · 종류 실행과 같다
                        const unsigned crb_ = ir->rowsh_rb[sub_];
                        low_fpf(out, "      ss[ssp-1].p = s_.p + (size_t)i_ * {}u; ss[ssp-1].n = {}u; }}\n", PROVEN_ARG((unsigned)(rb_)), PROVEN_ARG((unsigned)(rb_ / crb_)));
                        ks.n--; ks.vn[ks.n-1] = (unsigned char)crb_;
                        ks.fl[ks.n-1] = (unsigned char)((ks.fl[ks.n-1] & CBE_FL_MARK) | (ir->rowsh_sub[sub_] << 2)); ks.o[ks.n-1] = -1;
                        break;
                    }
                    low_fpf(out, "      ss[ssp-1].p = s_.p + (size_t)i_ * {}u; ss[ssp-1].n = {}u; }}\n", PROVEN_ARG((unsigned)(rb_)), PROVEN_ARG((unsigned)(rb_ / ez_)));
                    ks.n--; ks.k[ks.n-1] = K_SL; ks.vn[ks.n-1] = 0; ks.o[ks.n-1] = -1;
                    break;
                }
                if (ks.k[ks.n-2] >= K_SVIEW) {
                    // ★★★ 구조체 배열의 원소 = **구조체 뷰**(무복사). 오프셋 = i × stride.
                    int sx = ks.k[ks.n-2] - K_SVIEW;
                    int stride = (int)ir->structs[sx].total;
                    low_puts(out, "    { long long i_ = st[--sp]; lw_sl s_ = ss[ssp-1];\n");
                    if (!(in->a & IR_POL_PROVEN))
                        low_puts(out, "      if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ >= s_.n))"
                              " lw_panic(\"slice index out of bounds\");\n");
                    low_fpf(out, "      ss[ssp-1].p = s_.p + (size_t)i_ * {}; ss[ssp-1].n = {}; }}\n",
                            PROVEN_ARG((int)(stride)), PROVEN_ARG((int)(stride)));
                    ks.n--; ks.k[ks.n-1] = (unsigned char)(K_VIEW + sx); ks.o[ks.n-1] = -1;
                    break;
                }
                int esz = (int)ks.ve[ks.n-2]; if (esz <= 0) esz = 1;
                bool eflt = ks.fl[ks.n-2] == CBE_FL_FLT;   // ★ float 원소(view_array f32/f64) — fs 스택으로 내야
                bool esgn = ks.fl[ks.n-2] == CBE_FL_SGN;   // ★ 부호 있는 원소(i8·i16·i32) — 읽고 부호를 늘린다(태그 경로 lw_index 와 같은 뜻)
                int cq = -1;   // ★ §8-22 ⓑ — 앞 바퀴가 이 값을 들고 왔으면 그것을 민다
                for (int q = 0; q < ncy; q++)
                    if ((int)j == cy[q].L && cy[q].on && !eflt && ks.k[ks.n-2] == K_SL &&
                        ks.o[ks.n-2] == cy[q].s && esz == cy[q].esz) cq = q;
                low_puts(out, "    { long long i_ = st[--sp]; lw_sl s_ = ss[--ssp];\n");
                if (cq >= 0) low_fpf(out, "      if (LW_UNLIKELY(!lw_cv{})) {{\n", PROVEN_ARG((int)(cq)));
                if (!(in->a & IR_POL_PROVEN))
                    low_puts(out, "      if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ >= s_.n))"
                          " lw_panic(\"slice index out of bounds\");\n");
                if (esz == 1) low_puts(out, esgn ? "      st[sp++] = (long long)(signed char)s_.p[i_];\n"
                                         : "      st[sp++] = (long long)s_.p[i_];\n");
                else {
                    // ★ 런타임(VARRAY)은 원소를 **리틀엔디언으로 조립**한다. 같은 뜻을 같은
                    //   방식으로 낸다 — 그러면 **어느 호스트에서도** 답이 같고, LE 기계에서는
                    //   gcc 가 이것을 **한 번의 로드**로 접는다(공짜다).
                    low_fpf(out, "      unsigned long long x_ = lw_ld_le(s_.p + (size_t)i_ * {}, {});\n",
                            PROVEN_ARG((int)(esz)), PROVEN_ARG((int)(esz)));
                    // ★ float 원소는 **fs 스택**으로 — 태그 경로 lw_index 와 같은 lw_b2f. 안 그러면 raw
                    //   정수 비트를 정수 스택에 밀어 VM≠native(index(view_array f32)=1.5 대신 비트값).
                    if (eflt) low_fpf(out, "      fs[fsp++] = lw_b2f(x_, {});\n", PROVEN_ARG((int)(esz)));
                    else if (esgn) low_fpf(out, "      st[sp++] = (long long)(int{}_t)x_;\n", PROVEN_ARG((int)(esz * 8)));
                    else      low_puts(out, "      st[sp++] = (long long)x_;\n");
                }
                if (!eflt && (esz == 1 || esz == 2 || esz == 4)) {   // 읽은 값은 그 칸의 폭에 든다(부호 있는 칸은 부호를 늘렸다)
                    idx_at = j; idx_m = (unsigned)(IR_TY_KNOWN | (esgn ? IR_TY_SIGNED : 0) | (esz * 8));
                }
                if (cq >= 0) low_fpf(out, "      }} else st[sp++] = lw_cc{};\n", PROVEN_ARG((int)(cq)));
                low_puts(out, "    }\n");
                // ★ index 는 **둘 빼고 하나 민다**(net −1). 여기서 −2 로 적었더니 `wrap.ok` 가
                //   **아래 슬롯을 덮었고**, `result` 를 돌려주는 op 이 **정수 0** 을 돌려줬다.
                ks.n--; ks.k[ks.n-1] = eflt ? K_FLT : K_INT; ks.o[ks.n-1] = -1; ks.ve[ks.n-1] = 0; ks.fl[ks.n-1] = 0;
                break;
            }
            // ★★★★★ **해시 — 빠른 경로 방출** (2026-08-18, 위 종류 시뮬레이터의 짝).
            //   태그 경로와 **같은 함수**를 부른다(`lw_sha256`) — 두 뒤끝이 같은 답을 내는
            //   가장 확실한 방법은 같은 코드를 부르는 것이다. 다른 것은 값이 오는 자리뿐:
            //   태그는 `lowv`, 여기는 `ss[]`(포인터+길이 그대로).
            //   ★ 쓰기 대상의 const 를 벗긴다 — 빠른 경로가 `index.store` 에서 이미 하는 일이다.
            case IRW_AESCTR: {
                low_puts(out, "    { lw_sl d_ = ss[--ssp], s_ = ss[--ssp], c_ = ss[--ssp], k_ = ss[--ssp];\n"
                      "      st[sp++] = lw_aes_ctr_x(k_.p, k_.n, (void *)c_.p, c_.n, s_.p, s_.n,"
                      " (void *)d_.p, d_.n); }\n");
                ks.n -= 4; ks.k[ks.n] = K_INT; ks.o[ks.n] = -1; ks.ve[ks.n] = 0; ks.fl[ks.n] = 0; ks.n++;
                break;
            }
            case IRW_CHACHA20: {
                low_puts(out, "    { lw_sl d_ = ss[--ssp], s_ = ss[--ssp], c_ = ss[--ssp], k_ = ss[--ssp];\n"
                      "      st[sp++] = lw_chacha20_x(k_.p, k_.n, (void *)c_.p, c_.n, s_.p, s_.n,"
                      " (void *)d_.p, d_.n); }\n");
                ks.n -= 4; ks.k[ks.n] = K_INT; ks.o[ks.n] = -1; ks.ve[ks.n] = 0; ks.fl[ks.n] = 0; ks.n++;
                break;
            }
            case IRW_POLY1305: {
                low_puts(out, "    { lw_sl d_ = ss[--ssp], t_ = ss[--ssp];\n"
                      "      st[sp++] = lw_poly1305_x((void *)t_.p, t_.n, d_.p, d_.n); }\n");
                ks.n -= 2; ks.k[ks.n] = K_INT; ks.o[ks.n] = -1; ks.ve[ks.n] = 0; ks.fl[ks.n] = 0; ks.n++;
                break;
            }
            case IRW_AESGCM: {
                low_puts(out, "    { lw_sl d_ = ss[--ssp], s_ = ss[--ssp], z_ = ss[--ssp],\n"
                      "            h_ = ss[--ssp], c_ = ss[--ssp], k_ = ss[--ssp];\n"
                      "      st[sp++] = lw_aes_gcm_x(k_.p, k_.n, (void *)c_.p, c_.n, h_.p, h_.n,\n"
                      "                              (void *)z_.p, z_.n, s_.p, s_.n, (void *)d_.p, d_.n); }\n");
                ks.n -= 6; ks.k[ks.n] = K_INT; ks.o[ks.n] = -1; ks.ve[ks.n] = 0; ks.fl[ks.n] = 0; ks.n++;
                break;
            }
            case IRW_CHAPOLY: {
                low_puts(out, "    { lw_sl d_ = ss[--ssp], s_ = ss[--ssp], t_ = ss[--ssp],\n"
                      "            c_ = ss[--ssp], k_ = ss[--ssp];\n"
                      "      st[sp++] = lw_chacha_poly_x(k_.p, k_.n, (void *)c_.p, c_.n,\n"
                      "                                  (void *)t_.p, t_.n, s_.p, s_.n, (void *)d_.p, d_.n); }\n");
                ks.n -= 5; ks.k[ks.n] = K_INT; ks.o[ks.n] = -1; ks.ve[ks.n] = 0; ks.fl[ks.n] = 0; ks.n++;
                break;
            }
            case IRW_AESROUND: case IRW_AESLAST: {
                low_fpf(out, "    {{ lw_sl r_ = ss[--ssp], t_ = ss[--ssp];\n"
                             "      st[sp++] = lw_aes_round_x((void *)t_.p, t_.n, r_.p, r_.n, {}); }}\n",
                        PROVEN_ARG((int)(in->w == IRW_AESLAST ? 1 : 0)));
                ks.n -= 2; ks.k[ks.n] = K_INT; ks.o[ks.n] = -1; ks.ve[ks.n] = 0; ks.fl[ks.n] = 0; ks.n++;
                break;
            }
            case IRW_GHASH: {
                low_puts(out, "    { lw_sl d_ = ss[--ssp], z_ = ss[--ssp], h_ = ss[--ssp];\n"
                      "      st[sp++] = lw_ghash_x(h_.p, h_.n, (void *)z_.p, z_.n, d_.p, d_.n); }\n");
                ks.n -= 3; ks.k[ks.n] = K_INT; ks.o[ks.n] = -1; ks.ve[ks.n] = 0; ks.fl[ks.n] = 0; ks.n++;
                break;
            }
            case IRW_SHA256: case IRW_SHA512: case IRW_SHA384: {
                const char *fn = in->w == IRW_SHA256 ? "lw_sha256"
                               : (in->w == IRW_SHA384 ? "lw_sha384" : "lw_sha512");
                low_fpf(out, "    {{ lw_sl d_ = ss[--ssp], s_ = ss[--ssp];\n"
                             "      st[sp++] = {}(s_.p, s_.n, (unsigned char *)d_.p, d_.n); }}\n", proven_arg_cstr(fn));
                ks.n -= 2; ks.k[ks.n] = K_INT; ks.o[ks.n] = -1; ks.n++;
                break;
            }
            case IRW_SAMESL:
                low_puts(out, "    { lw_sl b_ = ss[--ssp]; lw_sl a_ = ss[--ssp]; st[sp++] = (a_.p == b_.p && a_.n == b_.n) ? 1 : 0; }\n");
                ks.n -= 2; ks.k[ks.n] = K_INT; ks.o[ks.n] = -1; ks.ve[ks.n] = 0; ks.fl[ks.n] = 0; ks.n++; break;
            case IRW_CRC32: case IRW_HASH64:
                low_fpf(out, "    {{ lw_sl s_ = ss[--ssp]; st[sp++] = (long long){}(s_.p, s_.n); }}\n",
                        proven_arg_cstr(in->w == IRW_CRC32 ? "lw_crc32" : "lw_hash64"));
                ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1; break;
            // ★ 난수·시계 — 잎 하나를 부를 뿐이다. 태그와 **같은 함수**라 값이 같다.
            //   (권한은 **진입점**이 검사한다 — op 자리의 일이 아니다. `vm_capdet` 이 그 증인.)
            case IRW_RANDBYTES:
                // ★★★ **이 자리는 넉 달 동안 C 로 안 내려갔다** (2026-09-07 수리).
                //   부르는 이름이 `lw_randbytes`(인자 셋)인데 프렐류드가 정의한 것은
                //   `lw_rand_bytes`(인자 둘)다 — 즉 `random.bytes` 를 쓰는 프로그램은
                //   **네이티브 빌드가 아예 안 됐다**(VM 으로는 잘 돌아서 아무도 몰랐다).
                //   같은 파일의 다른 자리(vec 채우기)는 옳은 이름을 쓴다 — **한 뜻이 두 곳에서
                //   갈린 것**이고, 갈린 쪽에 시험이 없었다(교훈 7).
                //   ⇒ 골든이 이제 `prog/mcpi.low` 를 **실제로 cc 에 건다**.
                low_puts(out, "    { lw_sl d_ = ss[--ssp];\n"
                      "      st[sp++] = lw_rand_bytes((unsigned char *)d_.p, d_.n); }\n");
                ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1; break;
            // ★ 레인 순열 — `lw_vperm` 의 정의를 그대로 옮긴다(0 reverse · 1 rotate · 2 shuffle).
            //   제자리로 못 한다(원본 레인을 읽으면서 쓰므로) ⇒ 칸 하나를 빌려 옮기고 되돌린다.
            case IRW_VREVERSE: case IRW_VROTATE: case IRW_VSHUFFLE: {
                int ln = (int)ks.vn[ks.n-1], ez = (int)ks.ve[ks.n-1];
                const char *m = lw_vmem((unsigned)ez);
                low_fpf(out, "    {{ lw_v t_ = vs[vsp-1];\n"
                             "      for (int k_ = 0; k_ < {}; k_++) {{\n", PROVEN_ARG((int)(ln)));
                if (in->w == IRW_VREVERSE)
                    low_fpf(out, "        int s_ = {} - 1 - k_;\n", PROVEN_ARG((int)(ln)));
                else if (in->w == IRW_VROTATE)
                    low_fpf(out, "        int s_ = (int)(((unsigned long long)k_ + {}ull) % {}u);\n",
                            PROVEN_ARG((unsigned long long)((unsigned long long)in->a)), PROVEN_ARG((int)(ln)));
                else
                    low_fpf(out, "        int s_ = (int)(({}ll >> (4 * k_)) & 0xf);\n"
                                 "        if (LW_UNLIKELY(s_ >= {})) lw_panic(\"shuffle lane index out of range\");\n",
                            PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((int)(ln)));
                low_fpf(out, "        vs[vsp-1].{}[k_] = t_.{}[s_]; }} }}\n", proven_arg_cstr(m), proven_arg_cstr(m));
                break;
            }
            // ★ 평균 — `lw_vavg` 의 정의 그대로 `(a+b+1)>>1`. 레인이 제 폭의 칸에 담기므로
            //   태그 경로가 손으로 하던 `& em` 되자르기는 **C 의 대입이 공짜로** 해 준다.
            //   다만 **더할 때는 넓혀서** 더한다 — u8 레인 둘의 합이 칸을 넘친다.
            // ★ 마스크 적재 — **켜진 레인만** 읽고 나머지는 통과 벡터 그대로다. 통과 벡터가
            //   이미 `vs[vsp-1]` 에 있으므로 **제자리에서** 켜진 레인만 덮어쓴다.
            //   ☞ 경계 검사는 **켜진 레인마다** 한다(태그 helper 와 같은 규율): 꺼진 레인은
            //     읽지 않으므로 경계 밖이어도 된다 — 그것이 마스크 적재의 존재 이유다.
            case IRW_VLOADM: {
                int ln = (int)((in->a >> 8) & 0xff), ez = (int)(in->a & 0xff);
                const char *m = lw_vmem((unsigned)ez);
                low_fpf(out, "    {{ long long m_ = st[--sp], i_ = st[--sp]; lw_sl s_ = ss[--ssp];\n"
                             "      for (int k_ = 0; k_ < {}; k_++) {{\n"
                             "        if (!((m_ >> k_) & 1)) continue;\n"
                             "        if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ + (unsigned long long)k_ >= s_.n))"
                             " lw_panic(\"load_masked active lane out of bounds\");\n"
                             "        vs[vsp-1].{}[k_] = lw_ld_le(s_.p + ((size_t)i_ + (size_t)k_) * {}, {}); }} }}\n",
                        PROVEN_ARG((int)(ln)), proven_arg_cstr(m), PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ez)));
                { unsigned char lnn = ks.vn[ks.n-1], ezz = ks.ve[ks.n-1];
                  ks.n -= 3; ks.k[ks.n-1] = K_VEC; ks.vn[ks.n-1] = lnn; ks.ve[ks.n-1] = ezz;
                  ks.fl[ks.n-1] = 0; ks.o[ks.n-1] = -1; }
                break;
            }
            case IRW_VSTOREM: {
                int ln = (int)ks.vn[ks.n-2], ez = (int)ks.ve[ks.n-2];
                if (ez <= 0) ez = 1;
                const char *m = lw_vmem((unsigned)ez);
                const bool mbyte_ = ks.ve[ks.n-4] <= 1 && ks.fl[ks.n-4] == 0 && ez > 1;   // ★ 2026-11-01 — 바이트 슬라이스(종류 실행과 같다)
                low_fpf(out, "    {{ long long m_ = st[--sp]; lw_v v_ = vs[--vsp];"
                             " long long i_ = st[--sp]; lw_sl s_ = ss[--ssp];{}\n"
                             "      for (int k_ = 0; k_ < {}; k_++) {{\n"
                             "        if (!((m_ >> k_) & 1)) continue;\n"
                             "        if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ + (unsigned long long)k_ >= s_.n))"
                             " lw_panic(\"store_masked active lane out of bounds\");\n"
                             "        lw_st_le((unsigned char *)s_.p + ((size_t)i_ + (size_t)k_) * {}, {},"
                             " (unsigned long long)v_.{}[k_]); }}\n"
                             "      st[sp++] = 0; }}\n", proven_arg_cstr(mbyte_ ? (ez == 2 ? " s_.n /= 2;" : ez == 4 ? " s_.n /= 4;" : " s_.n /= 8;") : ""), PROVEN_ARG((int)(ln)), PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ez)), proven_arg_cstr(m));
                ks.n -= 3; ks.k[ks.n-1] = K_INT; ks.ve[ks.n-1] = 0;
                ks.fl[ks.n-1] = 0; ks.o[ks.n-1] = -1;
                break;
            }
            case IRW_VAVG: {
                int ln = (int)ks.vn[ks.n-1];
                const char *m = lw_vmem((unsigned)ks.ve[ks.n-1]);
                low_fpf(out, "    {{ vsp--; for (int k_ = 0; k_ < {}; k_++)\n"
                             "        vs[vsp-1].{}[k_] = (unsigned long long)((unsigned long long)vs[vsp-1].{}[k_]"
                             " + (unsigned long long)vs[vsp].{}[k_] + 1ull) >> 1; }}\n", PROVEN_ARG((int)(ln)), proven_arg_cstr(m), proven_arg_cstr(m), proven_arg_cstr(m));
                ks.n--; ks.o[ks.n-1] = -1; break;
            }
            case IRW_RNGNEXT:
                low_puts(out, "    st[sp-1] = lw_rng_next(st[sp-1]);\n"); break;
            case IRW_TIMESLEEP:
                low_puts(out, "    st[sp-1] = lw_time_sleep(st[sp-1]);\n"); break;
            case IRW_TIMENOW:
                low_puts(out, "    st[sp++] = lw_time_now();\n");
                ks.k[ks.n] = K_INT; ks.o[ks.n] = -1; ks.n++; break;
            case IRW_TIMELOCAL:
                low_puts(out, "    st[sp++] = lw_time_local();\n");
                ks.k[ks.n] = K_INT; ks.o[ks.n] = -1; ks.n++; break;
            // ★★★ **호스트 잎** (2026-10-31 · W-20261031-2053) — 태그 경로와 같은 함수(`lw_h_*`)를 부른다.
            //   option 은 (태그, 값) 쌍이다: 1 = some · 2 = none. 바이트 슬라이스 option 은 `ws[].s`.
#define CBE_KSET(i_, k_) do { ks.k[(i_)] = (k_); ks.o[(i_)] = -1; ks.ve[(i_)] = 0; ks.vn[(i_)] = 0; ks.fl[(i_)] = 0; } while (0)
#define CBE_WSOPT(call_) "      if (" call_ ") { ws[wsp].v = r_; ws[wsp].tag = 1; } else { ws[wsp].v = 0; ws[wsp].tag = 2; } wsp++; }\n"
// 같은 글을 proven 서식(low_fpf) 안에서 쓸 때 — 중괄호를 두 겹으로(`{{` `}}` 가 글자 `{` `}` 다).
#define CBE_WSOPT_F(call_) "      if (" call_ ") {{ ws[wsp].v = r_; ws[wsp].tag = 1; }} else {{ ws[wsp].v = 0; ws[wsp].tag = 2; }} wsp++; }}\n"
            case IRW_ARGC:
                low_puts(out, "    st[sp++] = (long long)lw_g_nargs;\n");
                CBE_KSET(ks.n, K_INT); ks.n++; break;
            case IRW_ARGV:
                low_puts(out, "    { long long i_ = st[--sp]; lw_sl s_ = { 0, 0 };\n"
                      "      if (lw_h_argv(i_, &s_)) { ws[wsp].s = s_; ws[wsp].v = 0; ws[wsp].tag = 1; } else { ws[wsp].v = 0; ws[wsp].tag = 2; } wsp++; }\n");
                CBE_KSET(ks.n-1, K_OPTSL); break;
            case IRW_ENVGET:
                low_puts(out, "    { lw_sl n_ = ss[--ssp]; lw_sl s_ = { 0, 0 };\n"
                      "      if (lw_h_envget(n_.p, n_.n, &s_)) { ws[wsp].s = s_; ws[wsp].v = 0; ws[wsp].tag = 1; } else { ws[wsp].v = 0; ws[wsp].tag = 2; } wsp++; }\n");
                CBE_KSET(ks.n-1, K_OPTSL); break;
            case IRW_WRITE:
                low_puts(out, "    { lw_sl b_ = ss[--ssp]; long long f_ = st[--sp]; st[sp++] = lw_h_write(f_, b_.p, b_.n); }\n");
                ks.n--; CBE_KSET(ks.n-1, K_INT); break;
            case IRW_RDIN: case IRW_FREAD: case IRW_DREAD: case IRW_NRECV:
                low_fpf(out, "    {{ lw_sl b_ = ss[--ssp]; long long h_ = st[--sp]; long long r_ = 0;\n"
                             CBE_WSOPT_F("{}(h_, (unsigned char *)b_.p, b_.n, &r_)"),
                        proven_arg_cstr(in->w == IRW_RDIN ? "lw_h_rdin" : in->w == IRW_FREAD ? "lw_h_fread" : in->w == IRW_DREAD ? "lw_h_dread" : "lw_h_nrecv"));
                ks.n--; CBE_KSET(ks.n-1, K_OPT); break;
            case IRW_FWRITE: case IRW_NSEND:
                low_fpf(out, "    {{ lw_sl b_ = ss[--ssp]; long long h_ = st[--sp]; long long r_ = 0;\n"
                             CBE_WSOPT_F("{}(h_, b_.p, b_.n, &r_)"),
                        proven_arg_cstr(in->w == IRW_FWRITE ? "lw_h_fwrite" : "lw_h_nsend"));
                ks.n--; CBE_KSET(ks.n-1, K_OPT); break;
            case IRW_FOPEN:
                low_puts(out, "    { long long m_ = st[--sp]; lw_sl p_ = ss[--ssp]; long long r_ = 0;\n"
                      CBE_WSOPT("lw_h_fopen(p_.p, p_.n, m_, &r_)"));
                ks.n--; CBE_KSET(ks.n-1, K_OPT); break;
            case IRW_DOPEN: case IRW_NRESOLVE:
                low_fpf(out, "    {{ lw_sl p_ = ss[--ssp]; long long r_ = 0;\n"
                             CBE_WSOPT_F("{}(p_.p, p_.n, &r_)"),
                        proven_arg_cstr(in->w == IRW_DOPEN ? "lw_h_dopen" : "lw_h_nresolve"));
                CBE_KSET(ks.n-1, K_OPT); break;
            // ★★ 2026-11-01 (W-20261101-1150) — 터미널 · 자식 프로세스 · prefetch. 런타임은 태그 경로의 것 그대로(`lw_tty_*` · `lw_proc_*`),
            //   다만 option 을 박스로 싸지 않고 (태그, 값) 쌍으로 낸다 — 되풀이 안에서 불러도 박스 풀을 안 쓴다.
            case IRW_TTYRAW:
                low_puts(out, "    st[sp-1] = lw_tty_raw(st[sp-1]);\n");
                CBE_KSET(ks.n-1, K_INT); break;
            case IRW_TTYREAD:
                low_puts(out, "    { lw_sl b_ = ss[--ssp]; ssize_t g_ = read(0, (void *)(uintptr_t)b_.p, b_.n);\n"
                      "      if (g_ < 0) { ws[wsp].v = 0; ws[wsp].tag = 2; } else { ws[wsp].v = (long long)g_; ws[wsp].tag = 1; } wsp++; }\n");
                CBE_KSET(ks.n-1, K_OPT); break;
            case IRW_TTYSIZE:
                low_puts(out, "    { struct winsize w_;\n"
                      "      if (!isatty(1) || ioctl(1, TIOCGWINSZ, &w_) != 0 || w_.ws_col == 0) { ws[wsp].v = 0; ws[wsp].tag = 2; }\n"
                      "      else { ws[wsp].v = ((long long)w_.ws_row << 32) | (long long)w_.ws_col; ws[wsp].tag = 1; } wsp++; }\n");
                CBE_KSET(ks.n, K_OPT); ks.n++; break;
            case IRW_PSPAWN:
                low_puts(out, "    { lw_sl a_ = ss[--ssp]; long long h_ = lw_proc_spawn(a_.p, a_.n);\n"
                      "      if (h_ < 0) { ws[wsp].v = 0; ws[wsp].tag = 2; } else { ws[wsp].v = h_; ws[wsp].tag = 1; } wsp++; }\n");
                CBE_KSET(ks.n-1, K_OPT); break;
            case IRW_PREAD:
                low_puts(out, "    { lw_sl d_ = ss[--ssp]; long long h_ = st[--sp]; long long r_ = lw_proc_read(h_, (unsigned char *)(uintptr_t)d_.p, d_.n);\n"
                      "      if (r_ < 0) { ws[wsp].v = 0; ws[wsp].tag = 2; } else { ws[wsp].v = r_; ws[wsp].tag = 1; } wsp++; }\n");
                ks.n--; CBE_KSET(ks.n-1, K_OPT); break;
            case IRW_PPOLL: case IRW_PWAIT:
                low_fpf(out, "    {{ long long h_ = st[--sp]; unsigned long long s_ = 0; int r_ = {}(h_, &s_);\n"
                             "      if (r_ < 0) {{ ws[wsp].v = 0; ws[wsp].tag = 2; }} else {{ if (r_ == 0) s_ = 0xFFFFFFFFFFFFFFFFull;"
                             " ws[wsp].v = (long long)s_; ws[wsp].tag = 1; }} wsp++; }}\n",
                        proven_arg_cstr(in->w == IRW_PPOLL ? "lw_proc_poll" : "lw_proc_wait"));
                CBE_KSET(ks.n-1, K_OPT); break;
            case IRW_PKILL:
                low_puts(out, "    st[sp-1] = lw_proc_kill(st[sp-1]) ? 1 : 0;\n");
                CBE_KSET(ks.n-1, K_INT); break;
            case IRW_PREFETCH:
                low_puts(out, "    { long long i_ = st[--sp]; lw_sl s_ = ss[--ssp];\n"
                      "      if (s_.p && (unsigned long long)i_ < s_.n) __builtin_prefetch(s_.p + i_, 0, 1);\n"
                      "      st[sp++] = 0; }\n");
                ks.n--; CBE_KSET(ks.n-1, K_INT); break;
            case IRW_FTYPE: case IRW_LTYPE:
                low_fpf(out, "    {{ lw_sl p_ = ss[--ssp]; long long r_ = 0;\n"
                             CBE_WSOPT_F("lw_h_ftype(p_.p, p_.n, {}, &r_)"),
                        PROVEN_ARG((int)(in->w == IRW_LTYPE ? 1 : 0)));
                CBE_KSET(ks.n-1, K_OPT); break;
            case IRW_DMAKE: case IRW_PREMOVE:
                low_fpf(out, "    {{ lw_sl p_ = ss[--ssp]; st[sp++] = {}(p_.p, p_.n); }}\n",
                        proven_arg_cstr(in->w == IRW_DMAKE ? "lw_h_dmake" : "lw_h_premove"));
                CBE_KSET(ks.n-1, K_INT); break;
            case IRW_PRENAME:
                low_puts(out, "    { lw_sl n_ = ss[--ssp]; lw_sl o_ = ss[--ssp]; st[sp++] = lw_h_prename(o_.p, o_.n, n_.p, n_.n); }\n");
                ks.n--; CBE_KSET(ks.n-1, K_INT); break;
            case IRW_FCLOSE: case IRW_DCLOSE: case IRW_NCLOSE:
                low_fpf(out, "    st[sp-1] = {}(st[sp-1]);\n",
                        proven_arg_cstr(in->w == IRW_FCLOSE ? "lw_h_fclose" : in->w == IRW_DCLOSE ? "lw_h_dclose" : "lw_h_nclose"));
                CBE_KSET(ks.n-1, K_INT); break;
            case IRW_NLISTEN: case IRW_NPORT: case IRW_NACCEPT:
                low_fpf(out, "    {{ long long h_ = st[--sp]; long long r_ = 0;\n"
                             CBE_WSOPT_F("{}(h_, &r_)"),
                        proven_arg_cstr(in->w == IRW_NLISTEN ? "lw_h_nlisten" : in->w == IRW_NPORT ? "lw_h_nport" : "lw_h_naccept"));
                CBE_KSET(ks.n-1, K_OPT); break;
            case IRW_NCONNECT:
                low_puts(out, "    { long long p_ = st[--sp]; long long a_ = st[--sp]; long long r_ = 0;\n"
                      CBE_WSOPT("lw_h_nconnect(a_, p_, &r_)"));
                ks.n--; CBE_KSET(ks.n-1, K_OPT); break;
            case IRW_FSEEK:
                low_puts(out, "    { long long w_ = st[--sp]; long long o_ = st[--sp]; long long h_ = st[--sp]; long long r_ = 0;\n"
                      CBE_WSOPT("lw_h_fseek(h_, o_, w_, &r_)"));
                ks.n -= 2; CBE_KSET(ks.n-1, K_OPT); break;
            case IRW_NPAIR:
                low_puts(out, "    { long long r_ = 0;\n"
                      CBE_WSOPT("lw_h_npair(&r_)"));
                CBE_KSET(ks.n, K_OPT); ks.n++; break;
            case IRW_HASVAL:
                low_puts(out, "    { int t_ = ws[--wsp].tag; st[sp++] = (t_ == 1 || t_ == 3) ? 1 : 0; }\n");
                CBE_KSET(ks.n-1, K_INT); break;
            case IRW_VALOR:
                if (ks.k[ks.n-2] == K_OPTSL) {   // 바이트 슬라이스 — 기본값은 슬라이스이거나 같은 option
                    if (ks.k[ks.n-1] == K_OPTSL)
                        low_puts(out, "    { lw_r d_ = ws[--wsp]; lw_r r_ = ws[--wsp]; ss[ssp++] = (r_.tag == 1) ? r_.s : d_.s; }\n");
                    else
                        low_puts(out, "    { lw_sl d_ = ss[--ssp]; lw_r r_ = ws[--wsp]; ss[ssp++] = (r_.tag == 1) ? r_.s : d_; }\n");
                    { const unsigned char w_ = ks.vn[ks.n-2] ? ks.vn[ks.n-2] : 1, f_ = ks.fl[ks.n-2];
                      ks.n--; CBE_KSET(ks.n-1, K_SL); ks.ve[ks.n-1] = w_; ks.fl[ks.n-1] = f_; }
                    break;
                }
                if (ks.k[ks.n-1] == K_OPT)
                    low_puts(out, "    { lw_r d_ = ws[--wsp]; lw_r r_ = ws[--wsp]; st[sp++] = (r_.tag == 1 || r_.tag == 3) ? r_.v : d_.v; }\n");
                else
                    low_puts(out, "    { long long d_ = st[--sp]; lw_r r_ = ws[--wsp]; st[sp++] = (r_.tag == 1 || r_.tag == 3) ? r_.v : d_; }\n");
                ks.n--; CBE_KSET(ks.n-1, K_INT); break;
#undef CBE_WSOPT
#undef CBE_KSET
            case IRW_ISTORE: {
                int esz = (int)ks.ve[ks.n-3]; if (esz <= 0) esz = 1;
                if (ks.k[ks.n-3] >= K_SVIEW) {   // ★ 2026-11-01 — 구조체 배열의 한 칸 ← 구조체 값(레코드 바이트 · 뷰 바이트)
                    const int ex_ = ks.k[ks.n-3] - K_SVIEW, stride_ = (int)ir->structs[ex_].total;
                    if (ks.k[ks.n-1] >= K_VIEW) low_puts(out, "    { const unsigned char *v_ = ss[--ssp].p;");
                    else low_puts(out, "    { const unsigned char *v_ = rs[--rsp];");
                    low_puts(out, " long long i_ = st[--sp]; lw_sl s_ = ss[--ssp];\n");
                    if (!(in->a & IR_POL_PROVEN))
                        low_puts(out, "      if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ >= s_.n))"
                              " lw_panic(\"slice index out of bounds\");\n");
                    low_fpf(out, "      memmove((unsigned char *)s_.p + (size_t)i_ * {}, v_, {}); }}\n", PROVEN_ARG((int)(stride_)), PROVEN_ARG((int)(stride_)));
                    ks.n -= 3; break;
                }
                if (ks.k[ks.n-1] == K_FLT) {   // ★ 2026-11-01 — 부동 값을 그 폭의 비트로(VM 과 같게 f32 는 f32 로 반올림 — lw_f2b)
                    low_puts(out, "    { double v_ = fs[--fsp]; long long i_ = st[--sp]; lw_sl s_ = ss[--ssp];\n");
                    if (!(in->a & IR_POL_PROVEN))
                        low_puts(out, "      if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ >= s_.n))"
                              " lw_panic(\"slice index out of bounds\");\n");
                    low_fpf(out, "      lw_st_le((unsigned char *)s_.p + (size_t)i_ * {}, {}, lw_f2b(v_, {})); }}\n", PROVEN_ARG((int)(esz)), PROVEN_ARG((int)(esz)), PROVEN_ARG((int)(esz)));
                    ks.n -= 3; break;
                }
                low_puts(out, "    { long long v_ = st[--sp], i_ = st[--sp]; lw_sl s_ = ss[--ssp];\n");
                if (!(in->a & IR_POL_PROVEN))
                    low_puts(out, "      if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ >= s_.n))"
                          " lw_panic(\"slice index out of bounds\");\n");
                // ★★★ **쓰기도 읽기와 같은 근거로 증명된다** (2026-07-30).
                //   IR 은 `index` 와 `index.store` 를 **같은 관계 사실**로 증명한다(RFC-0055 D5 ·
                //   `idx_no_check` Qed): `i < len(s)` 이고 `i ≥ 0` 이면 안전하다. **태그 경로는
                //   이미 그것을 쓰고 있었다**(`lw_istore_nc`) — 빠른 경로만 표시를 버렸다.
                //
                //   ★ 어제 이 자리를 고쳤다가 차분 스윕 10건 발산을 보고 "`a` 가 필드 번호와
                //     비트를 다툰다" 고 적고 되돌렸다. **그 진단이 틀렸다**: `ir_glued_place` 는
                //     인덱스 쓰기에서 `*fid = 0` 을 주므로 ISTORE 의 `a` 는 **언제나 0** 이고,
                //     그 발산의 진짜 원인은 같은 커밋에 있던 `wrap_*` 변경(폭 미상 → 0 마스킹)이었다.
                //     ⇒ 원인을 하나만 바꿔 확인하지 않고 **두 변경을 함께 보고 결론**을 냈다.
                //     같은 픽스처로 다시 재 보니 이 변경의 방출 차분은 **0** 이다.
                if (esz == 1) low_puts(out, "      ((unsigned char *)s_.p)[i_] = (unsigned char)v_;\n");
                else
                    low_fpf(out, "      lw_st_le((unsigned char *)s_.p + (size_t)i_ * {}, {},"
                                 " (unsigned long long)v_);\n", PROVEN_ARG((int)(esz)), PROVEN_ARG((int)(esz)));
                // ★ §8-22 ⓑ — 방금 쓴 값이 다음 바퀴의 `s[k-1]` 이다(읽는 쪽 `index` 가 내는 값 그대로:
                //   원소 폭으로 잘린 부호 없는 값). 폭이 어긋나면 들고 가지 않는다 — 읽는 쪽이 원래대로 읽는다.
                for (int q = 0; q < ncy; q++) if ((int)j == cy[q].S && cy[q].on) {
                    if (esz != cy[q].esz) low_fpf(out, "      lw_cv{} = 0;\n", PROVEN_ARG((int)(q)));
                    else if (esz == 8)   low_fpf(out, "      lw_cc{} = v_;\n", PROVEN_ARG((int)(q)));
                    else low_fpf(out, "      lw_cc{} = (long long)((unsigned long long)v_ & 0x{:x}ull);\n",
                                 PROVEN_ARG((int)(q)), PROVEN_ARG((unsigned long long)((1ull << (esz * 8)) - 1)));
                }
                low_puts(out, "    }\n");
                ks.n -= 3; break;
            }
            case IRW_VARRAY: {
                if (in->a & 0x80000) {   // ★ 줄의 배열(K_ROWS · 종류 실행과 짝) — 바이트 길이를 줄 바이트로 나눈다
                    const unsigned rb_ = (unsigned)(((proven_u64)in->a >> 32) & 0xffffffu), ez_ = (unsigned)(in->a & 0xff);
                    const unsigned sub_ = (unsigned)(((proven_u64)in->a & IR_ROWSUB_MASK) >> IR_ROWSUB_SHIFT);
                    const unsigned char fl_ = (unsigned char)(((in->a & 0x10000) ? CBE_FL_FLT : ((in->a & 0x20000) && ez_ < 8) ? CBE_FL_SGN : 0) | (sub_ << 2));
                    if (ks.k[ks.n-1] == K_ROWS && ks.vn[ks.n-1] == rb_ && ks.ve[ks.n-1] == ez_ && ks.fl[ks.n-1] == fl_) break;
                    low_fpf(out, "    if (LW_UNLIKELY(ss[ssp-1].n % {}u)) lw_panic(\"an array of rows needs a whole number of rows\");\n"
                                 "    ss[ssp-1].n /= {}u;\n", PROVEN_ARG((unsigned)(rb_)), PROVEN_ARG((unsigned)(rb_)));
                    ks.k[ks.n-1] = K_ROWS; ks.ve[ks.n-1] = (unsigned char)ez_; ks.vn[ks.n-1] = (unsigned char)rb_; ks.fl[ks.n-1] = fl_;
                    break;
                }
                // ★ RFC-0109 단계 1 — 이미 그 모양이면 **방출 0**(시뮬레이터와 같은 규칙).
                if (!g_no_elemsl && (in->a & 0x40000) && ks.k[ks.n-1] == (unsigned char)(K_SVIEW + ((in->a >> 20) & 0xff))) break;
                if (!g_no_elemsl && !(in->a & 0x40000) && ks.k[ks.n-1] == K_SL && ks.ve[ks.n-1] > 1 &&
                    ks.ve[ks.n-1] == (unsigned char)(in->a & 0xff)) {
                    if (in->a & 0x10000) ks.fl[ks.n-1] = CBE_FL_FLT;
                    else if ((in->a & 0x20000) && (in->a & 0xff) < 8) ks.fl[ks.n-1] = CBE_FL_SGN;   // ★ 시뮬레이터와 같은 규칙
                    break;
                }
                if (in->a & 0x40000) {
                    // ★★★ **구조체 배열** — 바이트 길이를 **원소(구조체) 크기**로 나눈다.
                    int sx = (int)((in->a >> 20) & 0xff);
                    int stride = (int)ir->structs[sx].total;
                    low_fpf(out, "    if (LW_UNLIKELY(ss[ssp-1].n % {}u))"
                                 " lw_panic(\"view_array: slice length is not a multiple of the element size\");\n"
                                 "    ss[ssp-1].n /= {}u;\n", PROVEN_ARG((int)(stride)), PROVEN_ARG((int)(stride)));
                    ks.k[ks.n-1] = (unsigned char)(K_SVIEW + sx); ks.ve[ks.n-1] = 0;
                    break;
                }
                // ★★★ 바이트 슬라이스를 **타입 배열**로 본다: 길이가 **원소 수**가 된다.
                int ez = (int)(in->a & 0xff);
                low_fpf(out, "    if (LW_UNLIKELY(ss[ssp-1].n % {}u))"
                             " lw_panic(\"view_array: slice length is not a multiple of the element size\");\n"
                             "    ss[ssp-1].n /= {}u;\n", PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ez)));
                ks.ve[ks.n-1] = (unsigned char)ez;
                if (in->a & 0x10000) ks.fl[ks.n-1] = CBE_FL_FLT;   // ★ 부동 원소 표식
                else if ((in->a & 0x20000) && ez < 8) ks.fl[ks.n-1] = CBE_FL_SGN;   // ★ 부호 있는 원소 표식
                break;
            }
            case IRW_SCOPY: {
                // ★★ 2026-10-31 (W-20261031-1610) — 내용 복사(겹쳐도 옳게): 두 쪽의 **바이트 수**가 같아야 한다(태그 경로 lw_scopy · VM 과 같은 뜻).
                //   바이트 수 = 원소 수 × 폭 — 폭은 슬롯이 안다(슬라이스 ve · 구조체 배열 배치 크기 · 줄 바이트).
                unsigned w_[2];
                for (int z = 0; z < 2; z++) {
                    unsigned char kk_ = ks.k[ks.n - 1 - z];
                    w_[z] = kk_ >= K_SVIEW ? (unsigned)ir->structs[kk_ - K_SVIEW].total : kk_ == K_ROWS ? (unsigned)ks.vn[ks.n - 1 - z]
                          : (ks.ve[ks.n - 1 - z] ? (unsigned)ks.ve[ks.n - 1 - z] : 1u);
                }
                low_fpf(out, "    {{ lw_sl s_ = ss[--ssp], d_ = ss[--ssp]; size_t sb_ = s_.n * {}u, db_ = d_.n * {}u;\n"
                             "      if (LW_UNLIKELY(sb_ != db_)) lw_panic(\"copy: the two slices are not the same length\");\n"
                             "      memmove((void *)d_.p, s_.p, db_); st[sp++] = 0; }}\n", PROVEN_ARG((unsigned)(w_[0])), PROVEN_ARG((unsigned)(w_[1])));
                ks.n -= 2; ks.o[ks.n] = -1; ks.k[ks.n] = K_INT; ks.ve[ks.n] = 0; ks.vn[ks.n] = 0; ks.fl[ks.n] = 0; ks.n++;
                break;
            }
            case IRW_XCHK: {
                // ★★★ REQ-0036 — 두 조각의 **바이트 자리**를 견준다(태그 경로 lw_xchk · VM 과 같은 뜻). 바이트 수 = 원소 수 × 폭 —
                //   폭은 슬롯이 안다(슬라이스 ve · 구조체 배열 배치 크기 · 줄 바이트 · 뷰는 바이트).
                unsigned w_[2];
                for (int z = 0; z < 2; z++) {
                    unsigned char kk_ = ks.k[ks.n - 1 - z];
                    w_[z] = kk_ >= K_SVIEW ? (unsigned)ir->structs[kk_ - K_SVIEW].total : kk_ >= K_VIEW ? 1u
                          : kk_ == K_ROWS ? (unsigned)ks.vn[ks.n - 1 - z] : (ks.ve[ks.n - 1 - z] ? (unsigned)ks.ve[ks.n - 1 - z] : 1u);
                }
                low_fpf(out, "    {{ lw_sl y_ = ss[--ssp], x_ = ss[--ssp];"
                             " lw_xchk_b((uintptr_t)x_.p, x_.n * {}u, (uintptr_t)y_.p, y_.n * {}u, {}); st[sp++] = 0; }}\n",
                        PROVEN_ARG((unsigned)(w_[1])), PROVEN_ARG((unsigned)(w_[0])), PROVEN_ARG((int)((int)in->a)));
                ks.n -= 2; ks.o[ks.n] = -1; ks.k[ks.n] = K_INT; ks.ve[ks.n] = 0; ks.vn[ks.n] = 0; ks.fl[ks.n] = 0; ks.n++;
                break;
            }
            case IRW_SWAP: {
                // ★ 맞바꾸기 — 원소 크기(ve)만큼 바이트를 바꾼다. 임시 복사 없이 제자리다.
                unsigned ez = (ks.n >= 3 && ks.ve[ks.n-3]) ? ks.ve[ks.n-3] : 1;
                if (ks.n >= 3 && ks.k[ks.n-3] >= K_SVIEW) ez = (unsigned)ir->structs[ks.k[ks.n-3] - K_SVIEW].total;   // ★ 구조체 배열 — 보폭은 구조체 크기
                // ★ 구간 분석이 두 색인을 `i < len(s)` 로 증명했으면(IR_POL_PROVEN) 검사를 내지 않는다 — `index` 와 같다.
                low_puts(out, "    { long long j_ = st[--sp], i_ = st[--sp]; lw_sl s_ = ss[--ssp];\n");
                if (!(in->a & IR_POL_PROVEN))
                    low_puts(out, "      if (LW_UNLIKELY(i_ < 0 || j_ < 0 || (unsigned long long)i_ >= s_.n || (unsigned long long)j_ >= s_.n))"
                          " lw_panic(\"swap index out of bounds\");\n");
                low_fpf(out, "      unsigned char *a_ = (unsigned char *)s_.p + (size_t)i_ * {}u;\n"
                             "      unsigned char *b_ = (unsigned char *)s_.p + (size_t)j_ * {}u;\n"
                             "      unsigned char t_[{}u]; memcpy(t_, a_, {}u); memmove(a_, b_, {}u); memcpy(b_, t_, {}u); }}\n",
                        PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ez)));
                // ★★ 바이트를 하나씩 바꾸던 고리를 크기 고정 복사로 바꿨다 (2026-10-03). a_ 와 b_ 는 같을 수
                //   있어서(i = j) gcc 가 그 고리를 낱말 하나로 합치지 못했다 — 8바이트 원소 맞바꾸기가 바이트
                //   읽기·쓰기 16번이었고, 퀵정렬이 손으로 쓴 맞바꾸기보다 ×2.6 느렸다(같은 세션 실측).
                //   memmove 가 i = j 를 바르게 다루고, 상수 크기라 gcc 가 낱말 읽기·쓰기로 접는다.
                low_puts(out, "    st[sp++] = 0;\n");   // ★ 뒤따르는 drop 과 짝을 맞춘다
                ks.n -= 3; ks.k[ks.n] = K_INT; ks.o[ks.n] = -1; ks.n++; break;
            }
            case IRW_SUBSLICE: {
                // ★ 부분 슬라이스는 **포인터 산술**이다 — 복사가 아니다.
                //   ★★ 원소 폭을 곱한다(`.n` 은 원소 수, 포인터는 바이트) — 색인과 같은 규약.
                const bool svw_ = ks.k[ks.n-3] >= K_SVIEW;   // 구조체 원소 — 보폭은 배치의 크기(종류 실행과 짝)
                const bool vw_ = ks.k[ks.n-3] >= K_VIEW && !svw_;   // ★ 2026-11-03 — 그림자의 뷰 → 바이트 슬라이스(종류 실행과 짝)
                int sez = svw_ ? (int)ir->structs[ks.k[ks.n-3] - K_SVIEW].total : vw_ ? 1 : (int)ks.ve[ks.n-3]; if (sez <= 0) sez = 1;
                low_puts(out, "    { long long hi_ = st[--sp], lo_ = st[--sp]; lw_sl s_ = ss[ssp-1];\n"
                      "      if (LW_UNLIKELY(lo_ < 0 || hi_ < lo_ || (unsigned long long)hi_ > s_.n))"
                      " lw_panic(\"subslice out of bounds\");\n");
                if (sez == 1)
                    low_puts(out, "      ss[ssp-1].p = s_.p + lo_; ss[ssp-1].n = (size_t)(hi_ - lo_); }\n");
                else
                    low_fpf(out, "      ss[ssp-1].p = s_.p + (size_t)lo_ * {};"
                                 " ss[ssp-1].n = (size_t)(hi_ - lo_); }}\n", PROVEN_ARG((int)(sez)));
                ks.n -= 2; if (!svw_) ks.k[ks.n-1] = K_SL; if (vw_) { ks.ve[ks.n-1] = 1; ks.vn[ks.n-1] = 0; ks.fl[ks.n-1] = 0; } ks.o[ks.n-1] = -1; break;
            }
            case IRW_BAND: case IRW_BOR: case IRW_BXOR:
            case IRW_SHL: case IRW_SHR: case IRW_WSHL: case IRW_WSHR:
            case IRW_ROTL: case IRW_ROTR: case IRW_CLMULLO: case IRW_CLMULHI: case IRW_MULHI: {
                int op2 = in->w == IRW_BAND ? 0 : in->w == IRW_BOR ? 1 : in->w == IRW_BXOR ? 2
                        : in->w == IRW_SHL ? 3 : in->w == IRW_SHR ? 4 : in->w == IRW_WSHL ? 5
                        : in->w == IRW_WSHR ? 6 : in->w == IRW_ROTL ? 7
                        : in->w == IRW_CLMULLO ? 9 : in->w == IRW_CLMULHI ? 10 : in->w == IRW_MULHI ? 11 : 8;
                low_fpf(out, "    st[sp-2] = lw_bit2({}, {}, st[sp-2], st[sp-1]); sp--;\n",
                        PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((int)(op2)));
                ks.n--; ks.o[ks.n-1] = -1; break;
            }
            case IRW_BNOT: case IRW_POPCNT: case IRW_CLZ: case IRW_CTZ: case IRW_BSWAP: {
                int op1 = in->w == IRW_BNOT ? 0 : in->w == IRW_POPCNT ? 1 : in->w == IRW_CLZ ? 2
                        : in->w == IRW_CTZ ? 3 : 4;
                low_fpf(out, "    st[sp-1] = lw_bit1({}, {}, st[sp-1]);\n", PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((int)(op1)));
                ks.o[ks.n-1] = -1; break;
            }
            // ★★★ **level-3 atomic** (RFC-0018) — 슬라이스는 ss[], 인덱스/값은 st[].
            //   esz 는 슬롯의 ve[](구조체 슬라이스면 stride, 아니면 원소 폭). 기본 8.
            case IRW_ALOAD: {
                int e = (int)ks.ve[ks.n-2]; if (e <= 0) e = 8;
                int mo = (int)(in->a & 7);
                low_fpf(out, "    {{ long long i_=st[--sp]; lw_sl s_=ss[--ssp]; if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ >= s_.n)) lw_panic(\"atomic index out of bounds\");"
                             " st[sp++]=lw_at_rmw((unsigned char*)s_.p,i_,{},0,0,{}); }}\n", PROVEN_ARG((int)(e)), PROVEN_ARG((int)(mo)));
                ks.n--; ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1; break;
            }
            case IRW_ASTORE: case IRW_AADD: case IRW_ASUB: case IRW_AAND:
            case IRW_AOR: case IRW_AXOR: case IRW_ASWAP: {
                int op2 = in->w==IRW_ASTORE?1:in->w==IRW_AADD?2:in->w==IRW_ASUB?3:in->w==IRW_AAND?4
                        :in->w==IRW_AOR?5:in->w==IRW_AXOR?6:7;
                int e = (int)ks.ve[ks.n-3]; if (e <= 0) e = 8;
                int mo = (int)(in->a & 7);
                low_fpf(out, "    {{ long long v_=st[--sp], i_=st[--sp]; lw_sl s_=ss[--ssp]; if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ >= s_.n)) lw_panic(\"atomic index out of bounds\");"
                             " st[sp++]=lw_at_rmw((unsigned char*)s_.p,i_,{},{},v_,{}); }}\n", PROVEN_ARG((int)(e)), PROVEN_ARG((int)(op2)), PROVEN_ARG((int)(mo)));
                ks.n -= 2; ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1; break;
            }
            case IRW_ACAS: {
                int e = (int)ks.ve[ks.n-4]; if (e <= 0) e = 8;
                int mo = (int)(in->a & 7);
                low_fpf(out, "    {{ long long d_=st[--sp], x_=st[--sp], i_=st[--sp]; lw_sl s_=ss[--ssp]; if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ >= s_.n)) lw_panic(\"atomic index out of bounds\");"
                             " st[sp++]=lw_at_cas((unsigned char*)s_.p,i_,{},x_,d_,{}); }}\n", PROVEN_ARG((int)(e)), PROVEN_ARG((int)(mo)));
                ks.n -= 3; ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1; break;
            }
            case IRW_AFENCE:
                // ★★★★★ **fence 도 자리 하나를 민다** (2026-08-07).
                //   이 IR 의 규약은 *"모든 op 은 무언가를 민다"* 이고, 문장 자리에서는 호출자가
                //   `IRW_DROP` 으로 그것을 버린다. `AFENCE` 만 **아무것도 안 밀었다** ⇒
                //   문장으로 쓰면 네이티브가 밀지도 않은 것을 빼서 **스택이 한 칸 어긋났다**
                //   (실측: `atomic_store 21 · fence · atomic_load` 가 VM 21 · 네이티브 **0**).
                //   ☞ VM 은 `case IRW_DROP: if (sp) sp--;` 로 **가드가 있어 견뎠다** — 그
                //     가드가 불균형을 **가리고 있었다.** 두 뒤끝이 같은 IR 을 다르게 견디면,
                //     견디는 쪽이 결함을 숨긴다.
                low_fpf(out, "    atomic_thread_fence(lw_mo({}));\n", PROVEN_ARG((int)((int)(in->a & 7))));
                low_puts(out, "    st[sp++] = 0;\n");
                ks.o[ks.n] = -1; ks.k[ks.n++] = K_INT; break;
            case IRW_BITCAST: {
                bool out_f = (in->a & 0x10000) != 0;
                bool in_f  = (ks.k[ks.n-1] == K_FLT);
                if (in_f && out_f) { /* f64→f64: 항등 (폭 안 바꿈) */ }
                else if (in_f && !out_f) {
                    low_fpf(out, "    st[sp++] = lw_bitcast_fi({}, fs[--fsp]);\n", PROVEN_ARG((long long)((long long)in->a)));
                    ks.k[ks.n-1] = K_INT;
                } else if (!in_f && out_f) {
                    low_fpf(out, "    fs[fsp++] = lw_bitcast_if({}, st[--sp]);\n", PROVEN_ARG((long long)((long long)in->a)));
                    ks.k[ks.n-1] = K_FLT;
                } else {
                    low_fpf(out, "    st[sp-1] = lw_bitcast_ii({}, st[sp-1]);\n", PROVEN_ARG((long long)((long long)in->a)));
                }
                ks.o[ks.n-1] = -1; break;
            }
            case IRW_SNEW:
                // ★ 새 스택 — 집(홈 지역)은 바로 다음 STORE 가 정한다. 여기선 용량 상한만 본다.
                //   선언 capacity 는 값-스택에 남겨 STORE 가 scap[집] 에 정착시킨다(VM 과 대칭).
                low_fpf(out, "    if (LW_UNLIKELY(st[sp-1] > {})) lw_panic(\"stack capacity exceeded\");\n",
                             PROVEN_ARG((int)(K_STK_CAP)));
                ks.k[ks.n-1] = K_STK; ks.o[ks.n-1] = -1; break;
            case IRW_SPUSH: {
                int hs = smap[ks.o[ks.n-2]];
                low_fpf(out, "    {{ long long x_ = st[--sp]; sp--;"
                             " if (LW_UNLIKELY(sn[{}] >= scap[{}])) lw_panic(\"stack capacity exceeded\");"
                             " sstk[{}][sn[{}]++] = x_; st[sp++] = 0; }}\n", PROVEN_ARG((int)(hs)), PROVEN_ARG((int)(hs)), PROVEN_ARG((int)(hs)), PROVEN_ARG((int)(hs)));
                ks.n--; ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1; break;
            }
            case IRW_SPOP_INTO: {
                int hs = smap[ks.o[ks.n-1]];
                low_fpf(out, "    {{ sp--; if (sn[{}] == 0) st[sp++] = 0;"
                             " else {{ loc[{}] = sstk[{}][--sn[{}]]; st[sp++] = 1; }} }}\n",
                        PROVEN_ARG((int)(hs)), PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((int)(hs)), PROVEN_ARG((int)(hs)));
                ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1; break;
            }
            case IRW_ENCODE: {
                int sx = (int)(in->a & 0xffff);
                const low_ir_struct_t *S = &ir->structs[sx];
                // ★★★★ **encode 도 구간 복사 하나가 됐다** (2026-08-03 — 표현 통합).
                //   레코드가 이미 그 타입의 **바이트 레이아웃**이므로, 직렬화할 것이 없다.
                //   그전에는 필드마다 슬롯에서 꺼내 바이트로 흩뿌렸다 — 같은 값을 두 표현
                //   사이에서 옮기는 일이었고, 표현이 하나가 되면서 그 일 자체가 사라졌다.
                low_fpf(out, "    {{ unsigned char *e_ = encb[{}]; rsp--;\n", PROVEN_ARG((int)(encn)));
                low_fpf(out, "      memcpy(e_, rs[rsp], {});\n", PROVEN_ARG((unsigned)((unsigned)S->total)));
                low_fpf(out, "      ss[ssp].p = e_; ss[ssp].n = {}; ssp++; }}\n", PROVEN_ARG((unsigned)((unsigned)S->total)));
                encn++;
                // ★ encode 결과는 **바이트 슬라이스** → ve=1. 시뮬레이터(2217)는 박는데 이미터가 빠뜨려,
                //   이 슬롯에 앞서 있던 `slice u64`(ve=8)의 원소폭을 물려받아 뒤 op 인자에서 n*=8 로
                //   길이가 뻥튀기됐다(VM≠native: blen(encode …) 이 2 대신 16). IRW_STR(3604)과 같은 부류.
                ks.k[ks.n-1] = K_SL; ks.o[ks.n-1] = -1; ks.ve[ks.n-1] = 1;
                break;
            }
            case IRW_REF: case IRW_MREF:
                // ★★★ 참조는 **이름**이다 — 실을 값이 없다. 대상은 종류가 기억한다.
                low_puts(out, "    /* ref: a name for a local — nothing to load */\n");
                ks.o[ks.n] = (signed char)(in->a & 0xffff);
                ks.k[ks.n++] = K_REF;
                break;
            case IRW_DEREF:
                low_fpf(out, "    st[sp++] = loc[{}];\n", PROVEN_ARG((int)((int)ks.o[ks.n-1])));
                ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1;
                break;
            case IRW_SPLAT: {
                // ★ splat — 한 값이 모든 레인에.
                int ln = (int)((in->a >> 8) & 0xff);
                if (in->a & 0x10000) {
                    // ★ 부동 splat — f64 스택 값을 원소 폭 비트로 눌러 모든 레인에.
                    int ez = (int)(in->a & 0xff);
                    g_vspush++; low_puts(out, "@@VSG@@");
                    low_fpf(out, "    {{ unsigned long long x_ = lw_f2b(fs[--fsp], {});"
                                 " for (int k_ = 0; k_ < {}; k_++) vs[vsp].{}[k_] = x_; vsp++; }}\n",
                            PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ln)), proven_arg_cstr(lw_vmem((unsigned)ez)));
                    ks.fl[ks.n-1] = 1;
                } else {
                    g_vspush++; low_puts(out, "@@VSG@@");
                    low_fpf(out, "    {{ unsigned long long x_ = (unsigned long long)st[--sp];"
                                 " for (int k_ = 0; k_ < {}; k_++) vs[vsp].{}[k_] = x_; vsp++; }}\n",
                            PROVEN_ARG((int)(ln)), proven_arg_cstr(lw_vmem((unsigned)(in->a & 0xff))));
                    ks.fl[ks.n-1] = (in->a & IR_SGN_BIT) ? CBE_FL_SGN : 0;   // ★ 부호 있는 레인의 표식(종류 실행과 같다 · 2026-11-01)
                }
                ks.k[ks.n-1] = K_VEC; ks.vn[ks.n-1] = (unsigned char)ln;
                ks.ve[ks.n-1] = (unsigned char)(in->a & 0xff); ks.o[ks.n-1] = -1;
                break;
            }
            case IRW_VLOAD: {
                // ★★★ **벡터 적재 = 레인만큼의 로드.** 태그 경로는 여기서 **32칸짜리 벡터 풀**을
                //   쓴다 — **여섯 번째 유한 풀**이다. 여기서는 프레임의 배열이다.
                int ln = (int)((in->a >> 8) & 0xff), ez = (int)(in->a & 0xff);
                int dz = cbe_def_esz(d) > 0 ? cbe_def_esz(d) : 1;
                // ★★★★★ **증명됐으면 재지 않는다** (2026-08-16, WO-0056 P2 · RFC-0053).
                //   구간 분석이 `i ≥ 0 ∧ i + L ≤ len(s)` 를 증명하면 IR 에 `IR_POL_PROVEN` 이
                //   붙는다 — `index` 가 이미 그렇게 하고 있었고, 벡터 경로만 못 받고 있었다.
                //   ★ 증명이 안 서면 검사는 **그대로 남는다**: 빌드 모드로 끄는 것이 아니다
                //     (RFC-0052 P3 — 모드가 아니라 계약이 증명하면 제거).
                const bool vproven = (in->a & IR_POL_PROVEN) != 0;
                char vchk[128];
                if (vproven) vchk[0] = 0;
                else low_bfmt(vchk, sizeof vchk,
                              "      if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ + {} > limit_))"
                              " lw_panic(\"vector load out of bounds\");\n", PROVEN_ARG((int)(ln)));
                g_vspush++; low_puts(out, "@@VSG@@");
                const bool vbyte_ = ks.ve[ks.n-2] <= 1 && ks.fl[ks.n-2] == 0 && ez > 1;   // ★ 2026-11-01 — 종류 실행과 같다
                char vlim[48]; if (vbyte_) low_bfmt(vlim, sizeof vlim, " limit_ /= {};", PROVEN_ARG((int)(ez))); else vlim[0] = 0;
                low_fpf(out, "    {{ long long i_ = st[--sp]; lw_sl s_ = ss[--ssp];\n"
                             "      size_t limit_ = s_.n; (void)limit_;{}\n"
                             "{}"
                             // ★★★ **레인마다 바이트를 세지 않는다** (2026-08-02 실측 수리).
                             //   전에는 레인마다 `for (b_ = esz; b_--;) x_ = (x_<<8)|p_[b_]` 였다 —
                             //   4레인 u32 로드 하나가 **바이트 16번 + 시프트**였다. 그래서
                             //   명시적 벡터 커널이 **평범한 스칼라 C 보다 ×16.29 느렸다**
                             //   (내적 1M: C 0.546ms · LOW 8.886ms). 2026-07-29 에 스칼라 원소
                             //   읽기는 `lw_ld_le`(LE 호스트에선 memcpy 하나)로 고쳤는데
                             //   **벡터 경로만 그 수리를 못 받았다** — 같은 결함이 두 곳에 있었고
                             //   한 곳만 고쳐졌다. 리틀엔디언 표현은 규범(RFC-0025)이고 바꾸지
                             //   않는다; 바꾸는 것은 **그 값을 얻는 방법**뿐이다.
                             // ★★ 그리고 **레인이 제 폭에 담기면서**(RFC-0040 §9) 대상 배열이
                             //   원본과 **같은 레이아웃**이 됐다.
                             //
                             // ★★★★ **그런데 "컴파일러가 통째로 접는다" 는 틀렸다** (2026-08-16 실측).
                             //   여기 있던 주석이 그렇게 적어 두었지만, 디스어셈블을 보면 GCC 는
                             //   레인별 4바이트 memcpy 를 **연속한 16바이트 로드로 보지 못한다**:
                             //   `movd` 넷 + `punpcklqdq` 둘 + `shufps` 하나로 벡터를 **조립한다**.
                             //   ⇒ 그 조립이 simd 내적의 **29%** 였다(1.010 → 0.719 ms).
                             //   *경고를 적는 것과 지키는 것이 다르듯, 최적화를 기대하는 것과
                             //    최적화가 일어나는 것도 다르다 — 재기 전에는 모른다.*
                             //
                             //   ⇒ 리틀엔디안에서는 **통째로 옮긴다**. 레인 배치가 메모리 배치와
                             //   같으므로 `memcpy` 한 번이 위 루프와 **바이트가 동일**하다. 빅엔디안
                             //   에서는 옛 길 그대로다 — `lw_ld_le` 와 **똑같은 갈림**이고, 그래서
                             //   어디서 컴파일해도 답이 같다(RFC-0025 는 표현의 규범이지 *얻는
                             //   방법*의 규범이 아니다). 벗어난 레인 자리는 두 길 모두 안 건드린다.
                             "#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__\n"
                             "      memcpy(vs[vsp].u8, s_.p + (size_t)i_ * {}, {});\n"
                             "#else\n"
                             "      for (int k_ = 0; k_ < {}; k_++)\n"
                             "        vs[vsp].{}[k_] = lw_ld_le(s_.p + ((size_t)i_ + (size_t)k_) * {}, {});\n"
                             "#endif\n"
                             "      vsp++; }}\n",
                        proven_arg_cstr(vlim),
                        proven_arg_cstr(vchk),
                        PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ln * ez)), PROVEN_ARG((int)(ln)), proven_arg_cstr(lw_vmem((unsigned)ez)), PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ez)));
                (void)dz;
                ks.n--; ks.k[ks.n-1] = K_VEC; ks.vn[ks.n-1] = (unsigned char)ln;
                ks.ve[ks.n-1] = (unsigned char)ez; ks.fl[ks.n-1] = (in->a & 0x10000) ? CBE_FL_FLT : (in->a & IR_SGN_BIT) ? CBE_FL_SGN : 0;   // ★ 종류 실행과 같다(2026-11-01)
                ks.o[ks.n-1] = -1;
                break;
            }
            case IRW_VSTORE: {
                // ★★★ **`VLOAD` 의 거울** (2026-08-17, SIMD-0001). 레인/폭은 벡터가 실어 온다.
                //   여기가 없어서 벡터를 쓰는 op 은 통째로 태그 경로였다.
                int ln = (int)ks.vn[ks.n-1], ez = (int)ks.ve[ks.n-1];
                if (ez <= 0) ez = 1;
                // ★ 증명됐으면 재지 않는다 — 읽기와 **같은 여백 규칙**(RFC-0053 부록 B).
                //   ☞ 쓰기는 읽기보다 위험하다(남의 바이트를 밟는다). 그래서 증명이 **안 서면**
                //     검사는 그대로 남고, 그 두 짝을 골든이 증인으로 잡는다.
                const bool sproven = (in->a & IR_POL_PROVEN) != 0;
                char schk[128];
                if (sproven) schk[0] = 0;
                else low_bfmt(schk, sizeof schk,
                              "      if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ + {} > limit_))"
                              " lw_panic(\"vector store out of bounds\");\n", PROVEN_ARG((int)(ln)));
                const bool vsbyte_ = ks.ve[ks.n-3] <= 1 && ks.fl[ks.n-3] == 0 && ez > 1;   // ★ 2026-11-01 — 바이트 슬라이스(종류 실행과 같다)
                low_fpf(out, "    {{ lw_v v_ = vs[--vsp]; long long i_ = st[--sp];"
                             " lw_sl s_ = ss[--ssp];\n"
                             "      size_t limit_ = s_.n{}; (void)limit_;\n"
                             "{}"
                             // 리틀엔디안이면 레인 배치가 메모리 배치와 같다 ⇒ 바이트가 동일하다.
                             "#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__\n"
                             "      memcpy((unsigned char *)s_.p + (size_t)i_ * {}, v_.u8, {});\n"
                             "#else\n"
                             "      for (int k_ = 0; k_ < {}; k_++)\n"
                             "        lw_st_le((unsigned char *)s_.p + ((size_t)i_ + (size_t)k_) * {},"
                             " {}, (unsigned long long)v_.{}[k_]);\n"
                             "#endif\n"
                             "    }}\n"
                             // ★ 그리고 **0 을 남긴다** — 뒤에 `drop` 이 온다(위 시뮬레이터 주석).
                             "    st[sp++] = 0;\n",
                        proven_arg_cstr(vsbyte_ ? (ez == 2 ? " / 2" : ez == 4 ? " / 4" : " / 8") : ""), proven_arg_cstr(schk), PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ln * ez)), PROVEN_ARG((int)(ln)), PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ez)), proven_arg_cstr(lw_vmem((unsigned)ez)));
                ks.n -= 2; ks.k[ks.n-1] = K_INT; ks.ve[ks.n-1] = 0;
                ks.fl[ks.n-1] = 0; ks.o[ks.n-1] = -1;
                break;
            }
            case IRW_RADD: case IRW_RMUL: case IRW_RMIN: case IRW_RMAX: {
                int ln = (int)ks.vn[ks.n-1];
                if (ks.fl[ks.n-1] == CBE_FL_FLT) {
                    // ★★★ **부동 벡터 축약 → f64.** 레인 비트를 double 로 보고 접는다.
                    int ez = (int)ks.ve[ks.n-1]; if (ez != 4 && ez != 8) ez = 4;
                    const char *finit = in->w == IRW_RMUL ? "1.0" : "0.0";
                    low_fpf(out, "    {{ vsp--; double a_ = {};\n"
                                 "      for (int k_ = 0; k_ < {}; k_++) {{ double x_ = lw_b2f(vs[vsp].{}[k_], {});\n",
                            proven_arg_cstr((in->w == IRW_RMIN || in->w == IRW_RMAX) ? "0.0" : finit), PROVEN_ARG((int)(ln)),
                            proven_arg_cstr(lw_vmem((unsigned)ez)), PROVEN_ARG((int)(ez)));
                    if (in->w == IRW_RADD) low_puts(out, "        a_ += x_;");
                    else if (in->w == IRW_RMUL) low_puts(out, "        a_ *= x_;");
                    else if (in->w == IRW_RMIN) low_puts(out, "        if (k_ == 0 || x_ < a_) a_ = x_;");
                    else low_puts(out, "        if (k_ == 0 || x_ > a_) a_ = x_;");
                    low_puts(out, " }\n      fs[fsp++] = a_; }\n");
                    ks.k[ks.n-1] = K_FLT; ks.o[ks.n-1] = -1; ks.fl[ks.n-1] = 0;
                    break;
                }
                if (ks.fl[ks.n-1] == CBE_FL_SGN) {
                    // ★★ 2026-11-01 (W-20261101-1828) — 부호 있는 레인: 부호를 늘려 접는다 · 합 · 곱이 레인 타입에 안 들어가면 멈춘다(VM 과 같다).
                    const int ez_ = (int)ks.ve[ks.n-1];
                    const char *rm_ = lw_vmem((unsigned)ez_), *sc_ = ez_ == 1 ? "int8_t" : ez_ == 2 ? "int16_t" : ez_ == 4 ? "int32_t" : "int64_t";
                    low_fpf(out, "    {{ long long a_ = {}; vsp--;\n      for (int k_ = 0; k_ < {}; k_++) {{ long long x_ = (long long)({})vs[vsp].{}[k_];\n",
                            proven_arg_cstr(in->w == IRW_RMUL ? "1" : "0"), PROVEN_ARG((int)(ln)), proven_arg_cstr(sc_), proven_arg_cstr(rm_));
                    if (in->w == IRW_RMIN) low_puts(out, "        if (k_ == 0 || x_ < a_) a_ = x_; }\n");
                    else if (in->w == IRW_RMAX) low_puts(out, "        if (k_ == 0 || x_ > a_) a_ = x_; }\n");
                    else {
                        const unsigned bw_ = (unsigned)ez_ * 8u;
                        char rng_[96] = "";
                        if (bw_ < 64) low_bfmt(rng_, sizeof rng_, " || a_ < -(1ll << {}) || a_ > (1ll << {}) - 1", PROVEN_ARG((unsigned)(bw_ - 1)), PROVEN_ARG((unsigned)(bw_ - 1)));
                        low_fpf(out, "        if (__builtin_{}_overflow(a_, x_, &a_){}) lw_panic(\"integer overflow at the declared width\"); }}\n",
                                proven_arg_cstr(in->w == IRW_RADD ? "add" : "mul"), proven_arg_cstr(rng_));
                    }
                    low_puts(out, "      st[sp++] = a_; }\n");
                    ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1; ks.fl[ks.n-1] = 0;
                    break;
                }
                const char *init = in->w == IRW_RMUL ? "1ull"
                                 : in->w == IRW_RMIN ? "~0ull" : "0ull";
                const char *rm = lw_vmem((unsigned)ks.ve[ks.n-1]);
                low_fpf(out, "    {{ unsigned long long a_ = {}; vsp--;\n", proven_arg_cstr(init));
                // ★ 결과의 타입은 레인의 타입이다(정본 §6.3.7 (2)) — 합 · 곱이 그 폭에 안 들어가면 멈춘다(VM 과 같다)
                if (in->w == IRW_RADD || in->w == IRW_RMUL) {
                    unsigned bw = (unsigned)ks.ve[ks.n-1] * 8u;
                    low_fpf(out, "      for (int k_ = 0; k_ < {}; k_++) {{ if (__builtin_{}_overflow(a_, (unsigned long long)vs[vsp].{}[k_], &a_){}) lw_panic(\"integer overflow at the declared width\"); }}\n",
                            PROVEN_ARG((int)(ln)), proven_arg_cstr(in->w == IRW_RADD ? "add" : "mul"), proven_arg_cstr(rm), proven_arg_cstr(bw < 64 ? (bw == 8 ? " || (a_ >> 8)" : bw == 16 ? " || (a_ >> 16)" : " || (a_ >> 32)") : ""));
                }
                else if (in->w == IRW_RMIN)
                    low_fpf(out, "      for (int k_ = 0; k_ < {}; k_++) if (vs[vsp].{}[k_] < a_) a_ = vs[vsp].{}[k_];\n", PROVEN_ARG((int)(ln)), proven_arg_cstr(rm), proven_arg_cstr(rm));
                else
                    low_fpf(out, "      for (int k_ = 0; k_ < {}; k_++) if (vs[vsp].{}[k_] > a_) a_ = vs[vsp].{}[k_];\n", PROVEN_ARG((int)(ln)), proven_arg_cstr(rm), proven_arg_cstr(rm));
                low_puts(out, "      st[sp++] = (long long)a_; }\n");
                ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1;
                break;
            }
            case IRW_MANY: case IRW_MALL: {
                int ln = (int)ks.vn[ks.n-1];
                if (in->w == IRW_MANY)
                    low_puts(out, "    st[sp-1] = (st[sp-1] != 0);\n");
                else
                    low_fpf(out, "    st[sp-1] = ((st[sp-1] & ((1ll << {}) - 1))"
                                 " == ((1ll << {}) - 1));\n", PROVEN_ARG((int)(ln)), PROVEN_ARG((int)(ln)));
                ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1;
                break;
            }
            case IRW_UNM:
                // ★ 수치 프렐류드 — 정수와 f64 가 **정적으로 갈린다**(태그 디스패치가 없다).
                if (ks.k[ks.n-1] == K_VEC) {   // ★ 2026-11-03 (W-20261103-2113) — 레인마다(lw_vun 과 같다): 부동은 libm · 정수 abs 는 감는다(D8) · 정수 floor/ceil 은 항등
                    const int ln_ = (int)ks.vn[ks.n-1], ez_ = (int)ks.ve[ks.n-1], sel_ = (int)(in->a & 0xf); const char *m_ = lw_vmem((unsigned)ez_);
                    if (ks.fl[ks.n-1] == CBE_FL_FLT) {
                        const char *fn_ = sel_ == 0 ? "sqrt" : sel_ == 1 ? "fabs" : sel_ == 2 ? "floor" : "ceil";
                        low_fpf(out, "    for (int k_ = 0; k_ < {}; k_++) vs[vsp-1].{}[k_] = lw_f2b({}(lw_b2f(vs[vsp-1].{}[k_], {})), {});\n", PROVEN_ARG((int)(ln_)), proven_arg_cstr(m_), proven_arg_cstr(fn_), proven_arg_cstr(m_), PROVEN_ARG((int)(ez_)), PROVEN_ARG((int)(ez_)));
                    } else if (sel_ == 1 && ks.fl[ks.n-1] == CBE_FL_SGN) {
                        const char *sc_ = ez_ == 1 ? "int8_t" : ez_ == 2 ? "int16_t" : ez_ == 4 ? "int32_t" : "int64_t";
                        low_fpf(out, "    for (int k_ = 0; k_ < {}; k_++) if (({})vs[vsp-1].{}[k_] < 0) vs[vsp-1].{}[k_] = 0ull - (unsigned long long)vs[vsp-1].{}[k_];\n", PROVEN_ARG((int)(ln_)), proven_arg_cstr(sc_), proven_arg_cstr(m_), proven_arg_cstr(m_), proven_arg_cstr(m_));
                    }   // 부호 없는 abs · 정수 floor · ceil 은 그대로다
                    ks.o[ks.n-1] = -1;
                    break;
                }
                if ((in->a & 0xf) == 5 || (in->a & 0xf) == 6) {
                    // ★★★ **부동 슬라이스 축약** — sum(Neumaier 보정) / sum_fast(축차).
                    //   슬라이스는 이미 view_array 로 **원소 수**가 됐다(ss.n). 원소를 리틀엔디언
                    //   으로 읽어 lw_b2f 로 double 로 본다. VM 과 **같은 알고리즘**이다.
                    int ez = (int)ks.ve[ks.n-1]; if (ez != 4 && ez != 8) ez = 8;
                    low_fpf(out, "    {{ lw_sl s_ = ss[--ssp]; double acc_ = 0.0, comp_ = 0.0;\n"
                                 "      for (size_t k_ = 0; k_ < s_.n; k_++) {{\n"
                                 "        unsigned long long raw_ = 0; const unsigned char *pb_ = s_.p + k_ * {};\n"
                                 "        for (int b_ = {}; b_-- > 0; ) raw_ = (raw_ << 8) | pb_[b_];\n"
                                 "        double x_ = lw_b2f(raw_, {});\n", PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ez)));
                    if ((in->a & 0xf) == 6)
                        low_puts(out, "        acc_ += x_; }\n");   // 축차 — O(n·ε)
                    else
                        low_puts(out, "        double t_ = acc_ + x_;\n"
                              "        comp_ += (fabs(acc_) >= fabs(x_)) ? (acc_ - t_) + x_ : (x_ - t_) + acc_;\n"
                              "        acc_ = t_; }\n");   // Kahan-Babuška-Neumaier
                    low_fpf(out, "      fs[fsp++] = {}; }}\n", proven_arg_cstr((in->a & 0xf) == 6 ? "acc_" : "acc_ + comp_"));
                    ks.k[ks.n-1] = K_FLT; ks.o[ks.n-1] = -1; ks.fl[ks.n-1] = 0;
                } else if ((in->a & 0xf) == 4) {   // nonzero_of: 0 → none · 그 밖 → some(v)
                    low_puts(out, "    { long long v_ = st[--sp];"
                          " if (v_ == 0) { ws[wsp].v = 0; ws[wsp].tag = 2; }"
                          " else { ws[wsp].v = v_; ws[wsp].tag = 1; } wsp++; }\n");
                    ks.k[ks.n-1] = K_OPT; ks.o[ks.n-1] = -1;
                } else if (ks.k[ks.n-1] == K_FLT) {
                    // ★ 초월 함수 여섯(RFC-0090 N2)이 여기 붙는다 — libm 이 준다.
                    //   방출 C 는 이미 `-lm` 으로 링크한다(`cc file.c -lm`).
                    int sel_ = (int)(in->a & 0xf);
                    const char *fn = sel_ == 0 ? "sqrt" : sel_ == 1 ? "fabs"
                                   : sel_ == 2 ? "floor" : sel_ == 3 ? "ceil"
                                   : sel_ == 7 ? "sin" : sel_ == 8 ? "cos"
                                   : sel_ == 9 ? "exp" : sel_ == 10 ? "log"
                                   : sel_ == 11 ? "round" : "ceil";
                    low_fpf(out, "    fs[fsp-1] = {}(fs[fsp-1]);\n", proven_arg_cstr(fn));
                } else if ((in->a & 0xf) == 1) {   // abs (정수)
                    // ★ abs(폭-MIN) 은 그 폭에 안 맞는다 — 폭을 알 때만 트랩(태그 경로와 같은 규율).
                    if ((in->a & 0x200000) && (in->a & 0x400000)) {
                        int wb = (int)((in->a >> 24) & 0xff);
                        if (wb) low_fpf(out, "    if (st[sp-1] == {}ll)"
                                             " lw_panic(\"abs overflow: |MIN| does not fit the declared width\");\n",
                                        PROVEN_ARG((long long)((long long)(wb >= 64 ? INT64_MIN : -((long long)1 << (wb - 1))))));
                    }
                    low_puts(out, "    if (st[sp-1] < 0) st[sp-1] = (long long)(0ull -"
                          " (unsigned long long)st[sp-1]);\n");
                }   // floor/ceil 은 정수에서 항등이다(태그 경로와 같다)
                break;
            case IRW_BINM:
                // ★★★★ **`in->a == 1 ? "<" : ">"` 는 갈래가 둘뿐이라고 가정한다.**
                //   `pow`(3)를 더하자 그것이 조용히 **max 로 떨어졌다**: VM 은 1024,
                //   네이티브는 10 — 두 뒤끝이 갈렸다(실측으로 잡았다). 삼항의 `else` 가
                //   **나머지 전부**를 삼키는 자리는 새 갈래가 늘 때마다 거짓말이 된다.
                //   ⇒ 갈래를 이름으로 적는다.
                if (ks.k[ks.n-1] == K_VEC) {   // ★ 2026-11-03 (W-20261103-2113) — 레인마다(lw_vbinm 과 같다): 정수는 부호 표식대로 견준다
                    const int ln_ = (int)ks.vn[ks.n-1], ez_ = (int)ks.ve[ks.n-1], sel_ = (int)(in->a & 0xff); const char *m_ = lw_vmem((unsigned)ez_);
                    if (ks.fl[ks.n-1] == CBE_FL_FLT) {
                        const char *ex_ = sel_ == 0 ? "fmod(p_, q_)" : sel_ == 3 ? "pow(p_, q_)" : sel_ == 1 ? "(p_ < q_ ? p_ : q_)" : "(p_ > q_ ? p_ : q_)";
                        low_fpf(out, "    {{ vsp--; for (int k_ = 0; k_ < {}; k_++) {{ double p_ = lw_b2f(vs[vsp-1].{}[k_], {}), q_ = lw_b2f(vs[vsp].{}[k_], {});\n"
                                     "        vs[vsp-1].{}[k_] = lw_f2b({}, {}); }} }}\n", PROVEN_ARG((int)(ln_)), proven_arg_cstr(m_), PROVEN_ARG((int)(ez_)), proven_arg_cstr(m_), PROVEN_ARG((int)(ez_)), proven_arg_cstr(m_), proven_arg_cstr(ex_), PROVEN_ARG((int)(ez_)));
                    } else {
                        const char *sc_ = ks.fl[ks.n-1] == CBE_FL_SGN ? (ez_ == 1 ? "int8_t" : ez_ == 2 ? "int16_t" : ez_ == 4 ? "int32_t" : "int64_t") : "unsigned long long";
                        low_fpf(out, "    {{ vsp--; for (int k_ = 0; k_ < {}; k_++) {{ const int lt_ = ({})vs[vsp-1].{}[k_] < ({})vs[vsp].{}[k_];\n"
                                     "        if ({}lt_) vs[vsp-1].{}[k_] = vs[vsp].{}[k_]; }} }}\n", PROVEN_ARG((int)(ln_)), proven_arg_cstr(sc_), proven_arg_cstr(m_), proven_arg_cstr(sc_), proven_arg_cstr(m_), proven_arg_cstr(sel_ == 1 ? "!" : ""), proven_arg_cstr(m_), proven_arg_cstr(m_));
                    }
                    ks.n--; ks.o[ks.n-1] = -1;
                    break;
                }
                if (ks.k[ks.n-1] == K_FLT) {
                    if ((in->a & 0xff) == 0) low_puts(out, "    fs[fsp-2] = fmod(fs[fsp-2], fs[fsp-1]); fsp--;\n");
                    else if ((in->a & 0xff) == 3) low_puts(out, "    fs[fsp-2] = pow(fs[fsp-2], fs[fsp-1]); fsp--;\n");
                    else low_fpf(out, "    fs[fsp-2] = (fs[fsp-2] {} fs[fsp-1]) ? fs[fsp-2] : fs[fsp-1];"
                                      " fsp--;\n", proven_arg_cstr((in->a & 0xff) == 1 ? "<" : ">"));
                } else if (in->a & IR_BINM_UNS)   // ★ RFC-0151 §8-9 — 부호 없는 정수는 부호 없이 견준다(u64 의 위쪽 절반)
                    low_fpf(out, "    st[sp-2] = ((unsigned long long)st[sp-2] {} (unsigned long long)st[sp-1]) ? st[sp-2] : st[sp-1]; sp--;\n",
                            proven_arg_cstr((in->a & 0xff) == 1 ? "<" : ">"));
                else
                    low_fpf(out, "    st[sp-2] = (st[sp-2] {} st[sp-1]) ? st[sp-2] : st[sp-1]; sp--;\n",
                            proven_arg_cstr((in->a & 0xff) == 1 ? "<" : ">"));
                ks.n--; ks.o[ks.n-1] = -1; break;
            case IRW_BNEW:
                // ★ 비트셋 = **빈 마스크**. 박스도 풀도 없다.
                low_puts(out, "    if (LW_UNLIKELY(st[sp-1] > 64)) lw_panic(\"bitset width > 64\");\n"
                      "    st[sp-1] = 0;\n");
                ks.k[ks.n-1] = K_BSET; ks.o[ks.n-1] = -1; break;
            case IRW_CONTAINS: {
                int cw = (in->a & 0x800000) ? (int)(in->a & 0x7f) : 64;   // ★ 선언 폭(로워링이 구움)
                low_fpf(out, "    {{ long long k_ = st[--sp];"
                      " if (LW_UNLIKELY(k_ < 0 || k_ >= {})) lw_panic(\"bitset element out of range\");"
                      " st[sp-1] = (long long)((((unsigned long long)st[sp-1]) >> k_) & 1ull); }}\n", PROVEN_ARG((int)(cw)));
                ks.n--; ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1; break;
            }
            // ★ 집합 연산 — 전부 **분기 없는 워드 연산**(RFC-0010 §6.7.1 S2). VM 과 같은 규칙.
            case IRW_BREMOVE: {
                // ★ add 와 대칭 — **출신 지역**의 마스크에서 비트를 지운다(값 복사본이 아니라).
                int rw = (in->a & 0x800000) ? (int)(in->a & 0x7f) : 64;   // ★ 선언 폭
                low_fpf(out, "    {{ long long k_ = st[--sp]; sp--;"
                             " if (LW_UNLIKELY(k_ < 0 || k_ >= {})) lw_panic(\"bitset element out of range\");"
                             " loc[{}] &= ~(1ll << k_); st[sp++] = 0; }}\n", PROVEN_ARG((int)(rw)), PROVEN_ARG((int)((int)ks.o[ks.n-2])));
                ks.n--; ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1; break;
            }
            case IRW_BUNION:
                low_puts(out, "    { unsigned long long y_ = (unsigned long long)st[--sp];"
                      " st[sp-1] = (long long)(((unsigned long long)st[sp-1]) | y_); }\n");
                ks.n--; ks.k[ks.n-1] = K_BSET; ks.o[ks.n-1] = -1; break;
            case IRW_BINTER:
                low_puts(out, "    { unsigned long long y_ = (unsigned long long)st[--sp];"
                      " st[sp-1] = (long long)(((unsigned long long)st[sp-1]) & y_); }\n");
                ks.n--; ks.k[ks.n-1] = K_BSET; ks.o[ks.n-1] = -1; break;
            case IRW_BDIFF:
                low_puts(out, "    { unsigned long long y_ = (unsigned long long)st[--sp];"
                      " st[sp-1] = (long long)(((unsigned long long)st[sp-1]) & ~y_); }\n");
                ks.n--; ks.k[ks.n-1] = K_BSET; ks.o[ks.n-1] = -1; break;
            case IRW_BSUBSET:
                low_puts(out, "    { unsigned long long y_ = (unsigned long long)st[--sp];"
                      " st[sp-1] = (long long)((((unsigned long long)st[sp-1]) & ~y_) == 0ull); }\n");
                ks.n--; ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1; break;
            case IRW_BEMPTY:
                low_puts(out, "    st[sp-1] = (long long)(((unsigned long long)st[sp-1]) == 0ull);\n");
                ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1; break;
            case IRW_BCOMPL:
                // ★ 폭은 **하강이 박아 준 immediate** 다 — 네이티브는 런타임에 폭을 모른다.
                if ((long long)in->a >= 64 || (long long)in->a <= 0)
                    low_puts(out, "    st[sp-1] = (long long)(~(unsigned long long)st[sp-1]);\n");
                else
                    low_fpf(out, "    st[sp-1] = (long long)((~(unsigned long long)st[sp-1]) & ((1ull << {}) - 1ull));\n",
                            PROVEN_ARG((long long)((long long)in->a)));
                ks.k[ks.n-1] = K_BSET; ks.o[ks.n-1] = -1; break;
            case IRW_COUNT:
                if (ks.k[ks.n-1] == K_STK)
                    low_fpf(out, "    st[sp-1] = sn[{}];\n", PROVEN_ARG((int)(smap[ks.o[ks.n-1]])));
                else
                    low_puts(out, "    st[sp-1] = (long long)__builtin_popcountll((unsigned long long)st[sp-1]);\n");
                ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1; break;
            case IRW_FCONST:
                // ★ f64 리터럴 — **비트를 그대로** 심는다(정확히 같은 값).
                low_fpf(out, "    {{ unsigned long long b_ = {}ull; double d_;"
                             " memcpy(&d_, &b_, 8); fs[fsp++] = d_; }}\n",
                        PROVEN_ARG((unsigned long long)((unsigned long long)in->a)));
                ks.o[ks.n] = -1; ks.k[ks.n++] = K_FLT; break;
            case IRW_ADD: case IRW_SUB: case IRW_MUL: vecdiv_:
                if (ks.n >= 2 && ks.k[ks.n-1] == K_VEC && ks.k[ks.n-2] == K_VEC) {
                    int ln = (int)ks.vn[ks.n-1], ez = (int)ks.ve[ks.n-1];
                    const char *o2 = in->w == IRW_ADD ? "+" : in->w == IRW_SUB ? "-" : in->w == IRW_DIV ? "/" : "*";
                    if (ks.fl[ks.n-1] == CBE_FL_FLT) {   // ★ 2026-11-01 — 부동 표식만(부호 표식 2 를 부동으로 읽지 않는다)
                        // ★★★ **부동 레인 산술** — 비트를 double 로 보고 연산해 다시 비트로.
                        const char *fm = lw_vmem((unsigned)ez);
                        low_fpf(out, "    {{ vsp--; for (int k_ = 0; k_ < {}; k_++) {{\n"
                                     "        double r_ = lw_b2f(vs[vsp-1].{}[k_], {}) {} lw_b2f(vs[vsp].{}[k_], {});\n"
                                     "        vs[vsp-1].{}[k_] = lw_f2b(r_, {}); }} }}\n",
                                PROVEN_ARG((int)(ln)), proven_arg_cstr(fm), PROVEN_ARG((int)(ez)), proven_arg_cstr(o2), proven_arg_cstr(fm), PROVEN_ARG((int)(ez)), proven_arg_cstr(fm), PROVEN_ARG((int)(ez)));
                        ks.n--; ks.o[ks.n-1] = -1;
                        break;
                    }
                    if (in->w == IRW_DIV) {   // ★ 2026-11-02 (W-20261102-0024 ⓐ) — 정수 레인의 `div` 는 레인마다 스칼라 `div`(0 · MIN / -1 은 멈춘다 · VM 과 같다)
                        low_fpf(out, "    {{ vsp--; for (int k_ = 0; k_ < {}; k_++)\n"
                                     "        vs[vsp-1].{}[k_] = lw_vdiv1(vs[vsp-1].{}[k_], vs[vsp].{}[k_], {}, {}); }}\n",
                                PROVEN_ARG((int)(ln)), proven_arg_cstr(lw_vmem((unsigned)ez)), proven_arg_cstr(lw_vmem((unsigned)ez)), proven_arg_cstr(lw_vmem((unsigned)ez)), PROVEN_ARG((int)(ez)), PROVEN_ARG((int)(ks.fl[ks.n-1] == CBE_FL_SGN)));
                        ks.n--; ks.o[ks.n-1] = -1;
                        break;
                    }
                    // ★ 정수 레인별 산술 — 원소 폭으로 **감싼다**(태그 경로와 같은 뜻).
                    //   ★★ 그 감싸기가 이제 **공짜다**(RFC-0040 §9): 레인이 제 폭의 칸에 담기니
                    //     C 의 대입이 자르고, 손으로 붙이던 `& ((1<<8*ez)-1)` 이 사라진다.
                    low_fpf(out, "    {{ vsp--; for (int k_ = 0; k_ < {}; k_++)\n"
                                 "        vs[vsp-1].{}[k_] = vs[vsp-1].{}[k_] {} vs[vsp].{}[k_]; }}\n",
                            PROVEN_ARG((int)(ln)), proven_arg_cstr(lw_vmem((unsigned)ez)), proven_arg_cstr(lw_vmem((unsigned)ez)), proven_arg_cstr(o2), proven_arg_cstr(lw_vmem((unsigned)ez)));
                    ks.n--; ks.o[ks.n-1] = -1;
                    break;
                }
                if (in->w == IRW_ADD && ks.k[ks.n-2] == K_BSET) {
                    // ★★★ `add b k` 는 **b 를 바꾼다** — 그 **출신 지역**의 마스크에 비트를 세운다.
                    //   (태그 경로는 박스를 통해 그렇게 한다. 값 복사본에 세우면 사라진다.)
                    int aw = (in->a & 0x800000) ? (int)(in->a & 0x7f) : 64;   // ★ 선언 폭
                    low_fpf(out, "    {{ long long k_ = st[--sp]; sp--;"
                                 " if (LW_UNLIKELY(k_ < 0 || k_ >= {})) lw_panic(\"bitset element out of range\");"
                                 " loc[{}] |= 1ll << k_; st[sp++] = 0; }}\n", PROVEN_ARG((int)(aw)), PROVEN_ARG((int)((int)ks.o[ks.n-2])));
                    ks.n--; ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1;
                    break;
                }
                if (ks.k[ks.n-1] == K_FLT)
                    low_fpf(out, "    fs[fsp-2] = lw_fround({}, fs[fsp-2] {} fs[fsp-1]); fsp--;\n",
                            PROVEN_ARG((long long)((long long)in->a)),
                            proven_arg_cstr(in->w == IRW_ADD ? "+" : in->w == IRW_SUB ? "-" : "*"));
                else if (in->a & 0x40000) {
                    // ★ chk_* 는 오버플로를 **값으로** 낸다 — some(합) | none. lw_arith_raw 가 ovf 를 준다.
                    low_fpf(out, "    {{ int ov_; long long r_ = lw_arith_raw({}, {}, st[sp-2], st[sp-1], &ov_); sp -= 2;"
                                 " if (ov_) {{ ws[wsp].tag = 2; ws[wsp].v = 0; }}"
                                 " else {{ ws[wsp].tag = 1; ws[wsp].v = r_; }} wsp++; }}\n",
                            PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((int)(in->w == IRW_ADD ? 0 : in->w == IRW_SUB ? 1 : 2)));
                    ks.n--; ks.k[ks.n-1] = K_OPT; ks.o[ks.n-1] = -1;
                    ks.ve[ks.n-1] = 0; ks.vn[ks.n-1] = 0; ks.fl[ks.n-1] = 0;   // ★ 종류 실행과 같다(2026-11-01)
                    break;
                }
                // ★★★★★ **증명한 것을 방출이 안 쓰고 있었다** (2026-08-18).
                //   구간 분석이 3,117 개의 산술 검사를 지웠는데(소진 래칫), 빠른 경로는
                //   `IR_POL_PROVEN` 을 **한 번도 안 봤다** — 증명된 곱도 `lw_arith_i` 를 불렀다.
                //   ☞ 그 함수는 넘침을 **찾고** 나서 `!ovf` 면 값을 낸다. 그러니 증명은
                //     *검사를 지운 것*이 아니라 *지웠다고 IR 에 적어 둔 것*이었다.
                //   ★★ 그리고 그것이 **LICM 을 막는다**: 넘치면 `lw_panic` 이므로 그 연산은
                //     부작용이 있고, GCC 는 부작용 있는 연산을 루프 밖으로 못 끌어올린다.
                //     `matmul` 의 `i*n` 이 안쪽 루프에서 매 바퀴 다시 곱해지던 이유다.
                //   ★★★ 건전성: 증명은 *"선언 폭에서 넘치지 않는다"* 이므로 수학적 결과가
                //     그 폭에 담긴다 ⇒ u64 로 계산하고 되돌려도 **비트가 같다**(부호·폭 무관).
                //     u64 로 하는 이유는 부호형 오버플로가 UB 라서다 — 증명이 있어도
                //     **UB 를 적지 않는다**(증명이 틀렸을 때 컴파일러가 무엇이든 할 자유를
                //     주는 것과, 틀린 수를 내는 것은 다르다. 뒤쪽이 낫다).
                // ☘ **증명된 산술을 raw 로 내는 길은 재 보고 되돌렸다**(2026-08-18). ±1% 였다
        //   (matmul 11.96/12.09 · tsp 13.86/13.77 · sort 3.42/3.42 · sieve 3.71/3.86,
        //    `wrap` 판은 5.011/5.018 로 사실상 동일). GCC 가 `lw_arith_i` 를 인라인하고
        //   상수 meta 로 가지를 접는다 — **얻는 것 없이 산술 의미가 두 곳에 생긴다.**
        //   VM≡native 오라클이 지키는 자리를 굳이 둘로 늘리지 않는다.
                else
                    low_fpf(out, "    st[sp-2] = lw_arith_i({}, {}, st[sp-2], st[sp-1]); sp--;\n",
                            PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((int)(in->w == IRW_ADD ? 0 : in->w == IRW_SUB ? 1 : 2)));
                ks.n--; break;
            case IRW_MAKE: {
                // ★★★ **박스가 없다.** 필드 값이 스택에서 곧장 슬롯으로 들어간다.
                //   (태그 경로는 여기서 **64개짜리 풀**에서 레코드를 할당한다 — 루프에서
                //    구조체를 만들면 **고갈된다**. 값이어야 할 것이 박스였다.)
                // ★ `make` 의 필드 **순서**와 구조체 선언의 **슬롯 순서**는 다를 수 있다.
                //   이름으로 맞춘다 — 그리고 그 이름은 **여기서 사라진다**.
                int sx = cbe_make_sidx(ir, in->a);
                proven_size_t nf = ir->makes[in->a].nfields;
                if (cbe_abox(ir, d, j, sx, nf)) {   // ★ 2026-11-01 — 태그 쪽 레코드로 짓는다(칸은 모두 0 — 태그 경로의 `spawn` 과 같다)
                    low_fpf(out, "    {{ static const lowv z_[{}]; sp -= {}; ss[ssp++] = lw_aref_in(lw_make({}, {}, z_)); }}\n",
                            PROVEN_ARG((size_t)(nf ? nf : (proven_size_t)1)), PROVEN_ARG((size_t)(nf)), PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((size_t)(nf)));
                    ks.n -= (int)nf; ks.o[ks.n] = -1; ks.k[ks.n] = K_AREF; ks.ve[ks.n] = (unsigned char)(sx + 1); ks.vn[ks.n] = 0; ks.fl[ks.n] = 0; ks.n++;
                    break;
                }
                // ★★★ **평탄 임시 버퍼**에 필드를 놓고 마지막에 레코드로 민다. 중첩 필드는 그
                //   잎들을 **연속 슬롯**에 복사한다(rs[] 스택에서 pop). 부동은 fs[], 정수는 st[].
                //   역순 pop — 두 스택의 내부 순서가 유지되므로.
                // ★ `LW_RECB` 다 — 아래가 `sizeof tmp_` 만큼 `rsb`(역시 LW_RECB) 로 복사한다. 64 로 두면 넘친다(WO-0177).
                low_puts(out, "    { _Alignas(8) unsigned char tmp_[LW_RECB] = {0};\n");
                // ★ 2026-11-01 — 평행 칸도 임시 칸에 모았다가 마지막에 민다: 액터 레코드를 꺼내면(`rsp--`) 스택 자리가 옮겨 가므로
                //   그 사이에 `rssl[rsp]` 에 바로 쓰면 다른 자리에 쓴다.
                const bool msl_ = cbe_struct_has_sl(ir, sx) && !cbe_make_zero(ir, d, j, sx, nf);
                if (msl_) low_puts(out, "      lw_sl tsl_[LW_RECF] = {{0,0}};\n");
                if (cbe_make_zero(ir, d, j, sx, nf)) {   // ★ `spawn actor` — 상수 0 을 버리고 수 칸은 0 바이트 · 슬라이스 칸은 «아직 안 세움» 표지
                    low_fpf(out, "      sp -= {};\n", PROVEN_ARG((size_t)((size_t)nf)));
                    for (proven_size_t q = 0; q < nf; q++) {
                        int sl = cbe_slot_by_name(ir, sx, ir->makes[in->a].fields[q]);
                        if (sl >= 0 && ir->structs[sx].f[sl].boxed && ir->structs[sx].f[sl].elem)
                            low_fpf(out, "      rssl[rsp][{}].p = LW_UNINIT; rssl[rsp][{}].n = 0;\n", PROVEN_ARG((int)(sl)), PROVEN_ARG((int)(sl)));
                    }
                    if (cbe_struct_has_sl(ir, sx) && sx < 64 && g_aid_need[sx])   // 제 자리가 없다(종류 실행이 CBE_FL_NOID 로 막는다)
                        low_fpf(out, "      rssl[rsp][{}].p = 0; rssl[rsp][{}].n = 0;\n", PROVEN_ARG((int)(CBE_AID)), PROVEN_ARG((int)(CBE_AID)));
                } else
                for (proven_size_t q = nf; q-- > 0; ) {
                    int sl = cbe_slot_by_name(ir, sx, ir->makes[in->a].fields[q]);
                    // ★★★★ **바이트 오프셋이다** (2026-08-03 — 표현 통합). 그전에는 `flat`,
                    //   즉 잎을 편 **슬롯 번호**였다. 바이트가 되면 중첩은 그냥 **구간**이라
                    //   잎을 세는 일(`cbe_leaves`)도, 슬롯을 펴는 일(`cbe_flat_off`)도 없다.
                    const low_ir_sfield_t *F = &ir->structs[sx].f[sl];
                    if (cbe_obox_sidx(ir, F) >= 0) {   // ★ 2026-11-01 — 소유 칸: 빠른 레코드로 태그 쪽 레코드를 새로 짓는다(VM 이 `make` 에서 베끼는 것과 같다)
                        const int ob_ = cbe_obox_sidx(ir, F);
                        const int omk_ = cbe_make_for_struct(ir, ob_);
                        const low_ir_struct_t *T_ = &ir->structs[ob_];
                        low_fpf(out, "      rsp--; {{ lowv ov_[{}];\n", PROVEN_ARG((size_t)((size_t)(T_->nf ? T_->nf : 1))));
                        for (proven_size_t z2 = 0; z2 < ir->makes[omk_].nfields && z2 < IR_MAKE_MAXF; z2++) {
                            const int s2 = cbe_slot_by_name(ir, ob_, ir->makes[omk_].fields[z2]);
                            const low_ir_sfield_t *G = &T_->f[s2];
                            if (cbe_obox_sidx1(ir, G, false) >= 0)   // ★ 2026-11-02 — 안쪽의 소유 칸: 그 레코드의 평행 칸에 든 자리를 넘긴다
                                low_fpf(out, "        ov_[{}] = lw_aref_out(rssl[rsp][{}]);\n", PROVEN_ARG((size_t)((size_t)z2)), PROVEN_ARG((int)(s2)));
                            else if (low_sfield_sgn(G) && G->size < 8)
                                low_fpf(out, "        ov_[{}] = lw_int((long long)(lw_ld_f(rs[rsp] + {}, {}, {}) << {}) >> {});\n",
                                        PROVEN_ARG((size_t)((size_t)z2)), PROVEN_ARG((unsigned)((unsigned)G->off)), PROVEN_ARG((int)((int)G->size)), PROVEN_ARG((int)(G->be ? 1 : 0)), PROVEN_ARG((int)(64 - 8 * (int)G->size)), PROVEN_ARG((int)(64 - 8 * (int)G->size)));
                            else
                                low_fpf(out, "        ov_[{}] = lw_int((long long)lw_ld_f(rs[rsp] + {}, {}, {}));\n",
                                        PROVEN_ARG((size_t)((size_t)z2)), PROVEN_ARG((unsigned)((unsigned)G->off)), PROVEN_ARG((int)((int)G->size)), PROVEN_ARG((int)(G->be ? 1 : 0)));
                        }
                        low_fpf(out, "        tsl_[{}] = lw_aref_in(lw_make({}, {}, ov_)); }}\n", PROVEN_ARG((int)(sl)), PROVEN_ARG((int)(omk_)), PROVEN_ARG((size_t)((size_t)ir->makes[omk_].nfields)));
                        continue;
                    }
                    if (cbe_f_nopt(ir, F) >= 0) {   // ★ 2026-11-03 (W-20261103-1143) — 마디를 가리키는 칸: option(마디의 뷰)이면 그 주소, none 이면 0
                        low_fpf(out, "      {{ lw_r w_ = ws[--wsp]; const unsigned char *p_ = w_.tag == 1 ? w_.s.p : 0; memcpy(tmp_ + {}, &p_, sizeof p_); }}\n", PROVEN_ARG((unsigned)((unsigned)F->off)));
                        continue;
                    }
                    if (cbe_aref_sidx(ir, F) >= 0) {   // ★ 2026-11-01 — 액터를 가리키는 칸: 칸에서 읽은 자리 그대로, 또는 액터 레코드의 제 자리
                        if (ks.k[ks.n - nf + q] == K_AREF) low_fpf(out, "      tsl_[{}] = ss[--ssp];\n", PROVEN_ARG((int)(sl)));
                        else low_fpf(out, "      rsp--; tsl_[{}] = rssl[rsp][{}];\n", PROVEN_ARG((int)(sl)), PROVEN_ARG((int)(CBE_AID)));
                        continue;
                    }
                    if (F->arrn) {   // ★ 2026-11-01 (W-20261101-1828) — 배열 칸: 그 바이트를 레코드 안에 베낀다(VM 의 `vm_arr_copy` — 길이가 꼭 같아야 한다)
                        const unsigned need_ = (unsigned)F->arrn * (unsigned)F->arresz;
                        const unsigned ve_ = ks.k[ks.n - nf + q] == K_ROWS ? (unsigned)ks.vn[ks.n - nf + q]                 // 줄의 나열의 `n` 은 줄 수다
                                           : ks.ve[ks.n - nf + q] ? ks.ve[ks.n - nf + q] : 1u;   // 슬라이스의 `n` 은 원소 수다
                        low_fpf(out, "      {{ lw_sl a_ = ss[--ssp]; if (LW_UNLIKELY(a_.n * {}u != {}u)) lw_panic(\"an array field takes exactly its length of elements\");"
                                     " memmove(tmp_ + {}, a_.p, {}); }}\n", PROVEN_ARG((unsigned)(ve_)), PROVEN_ARG((unsigned)(need_)), PROVEN_ARG((unsigned)((unsigned)F->off)), PROVEN_ARG((unsigned)(need_)));
                    } else if (F->sidx >= 0) {   // ★ 중첩 레코드 필드 — 그 구간을 통째로 놓는다
                        low_fpf(out, "      rsp--; memcpy(tmp_ + {}, rs[rsp], {});\n",
                                PROVEN_ARG((unsigned)((unsigned)F->off)), PROVEN_ARG((unsigned)((unsigned)ir->structs[F->sidx].total)));
                    } else if (F->boxed && F->elem) {
                        // ★★★★★ **만든 레코드의 슬라이스 필드**는 바이트가 아니라 평행 칸에
                        //   담는다(RFC-0106 단계 3). 바이트 자리는 비워 둔다 — 아무도 안 읽는다.
                        low_fpf(out, msl_ ? "      tsl_[{}] = ss[--ssp];\n" : "      rssl[rsp][{}] = ss[--ssp];\n", PROVEN_ARG((int)(sl)));
                    } else if (ks.k[ks.n - nf + q] == K_FLT)
                        low_fpf(out, "      lw_st_f(tmp_ + {}, {}, {}, lw_f2b(fs[--fsp], {}));\n",
                                PROVEN_ARG((unsigned)((unsigned)F->off)), PROVEN_ARG((int)((int)F->size)), PROVEN_ARG((int)(F->be ? 1 : 0)), PROVEN_ARG((int)((int)F->size)));
                    else
                        low_fpf(out, "      lw_st_f(tmp_ + {}, {}, {}, (unsigned long long)st[--sp]);\n",
                                PROVEN_ARG((unsigned)((unsigned)F->off)), PROVEN_ARG((int)((int)F->size)), PROVEN_ARG((int)(F->be ? 1 : 0)));
                }
                if (msl_) low_puts(out, "      memcpy(rssl[rsp], tsl_, sizeof tsl_);\n");
                low_puts(out, "      rs[rsp] = rsb[rsp]; memcpy(rs[rsp], tmp_, sizeof tmp_); rsp++; }\n");
                ks.n -= (int)nf; ks.o[ks.n] = -1; ks.k[ks.n++] = (unsigned char)(K_REC + sx);
                break;
            }
            case IRW_FIELD: {
                // ★★★ **이름이 컴파일 시점에 슬롯 번호(레코드) 또는 오프셋(뷰)이 된다** —
                //   런타임 `strcmp` 가 사라진다.
                unsigned char fk = ks.k[ks.n-1];
                // ★★★★★ **슬라이스 필드를 읽는다** (RFC-0106 단계 1). 그 값은 바이트 버퍼가
                //   아니라 **평행 칸**에 있다. 어느 파라미터의 레코드인지는 종류 실행이
                //   이미 확인했다(`ks.o` 가 파라미터 번호).
                if (fk >= K_REC && fk < K_VIEW) {
                    int sx0 = fk - K_REC;
                    int sl0 = cbe_rec_slot(ir, sx0, in->a);
                    const int ob0 = sl0 >= 0 ? cbe_obox_sidx(ir, &ir->structs[sx0].f[sl0]) : -1;
                    if (ob0 >= 0) {   // ★ 2026-11-01 — 소유 칸: 태그 쪽 레코드를 빠른 레코드로 베껴 낸다(옮겨 내는 것 — 되쓸 것이 없다)
                        if (ks.o[ks.n-1] >= 0) low_fpf(out, "    rsp--; {{ lowv ob_ = lw_aref_out(rlocsl[{}][{}]);\n", PROVEN_ARG((int)((int)ks.o[ks.n-1])), PROVEN_ARG((int)(sl0)));
                        else low_fpf(out, "    rsp--; {{ lowv ob_ = lw_aref_out(rssl[rsp][{}]);\n", PROVEN_ARG((int)(sl0)));
                        low_puts(out, "      if (LW_UNLIKELY(ob_.tag != LWV_REC)) lw_panic(\"field needs a record\");\n"
                              "      rs[rsp] = rsb[rsp]; memset(rs[rsp], 0, LW_RECB);\n");
                        if (cbe_obox_inner2_ok(ir, ob0)) cbe_emit_rec_unpack(out, ir, ob0, "ob_", "rs[rsp]", "rssl[rsp]", 0, true);   // ★ 안쪽의 소유 칸 자리도(2026-11-02)
                        else cbe_emit_rec_unpack(out, ir, ob0, "ob_", "rs[rsp]", "rssl", 0, false);
                        low_puts(out, "      rsp++; }\n");
                        ks.n--; ks.o[ks.n] = -1; ks.k[ks.n] = (unsigned char)(K_REC + ob0); ks.ve[ks.n] = 0; ks.vn[ks.n] = 0; ks.fl[ks.n] = 0;
                        ks.n++;
                        break;
                    }
                    const int no0 = sl0 >= 0 ? cbe_f_nopt(ir, &ir->structs[sx0].f[sl0]) : -1;
                    if (no0 >= 0) {   // ★ 2026-11-03 (W-20261103-1143) — 마디를 가리키는 칸: 0 이면 none, 아니면 그 마디의 뷰를 싼 option(종류 실행과 짝)
                        low_fpf(out, "    rsp--; {{ const unsigned char *p_; memcpy(&p_, rs[rsp] + {}, sizeof p_);"
                                     " if (p_) {{ ws[wsp].s.p = p_; ws[wsp].s.n = {}; ws[wsp].v = 0; ws[wsp].tag = 1; }} else {{ ws[wsp].v = 0; ws[wsp].tag = 2; }} wsp++; }}\n",
                                PROVEN_ARG((unsigned)((unsigned)ir->structs[sx0].f[sl0].off)), PROVEN_ARG((unsigned)((unsigned)ir->structs[no0].total)));
                        ks.n--; ks.o[ks.n] = -1; ks.k[ks.n] = K_OPT; ks.ve[ks.n] = (unsigned char)(no0 + 1); ks.vn[ks.n] = 0; ks.fl[ks.n] = 1;
                        ks.n++;
                        break;
                    }
                    const int as0 = sl0 >= 0 ? cbe_aref_sidx(ir, &ir->structs[sx0].f[sl0]) : -1;
                    if (as0 >= 0) {   // ★ 2026-11-01 — 액터를 가리키는 칸: 평행 칸의 자리를 그대로 꺼낸다
                        if (ks.o[ks.n-1] >= 0) low_fpf(out, "    rsp--; ss[ssp++] = rlocsl[{}][{}];\n", PROVEN_ARG((int)((int)ks.o[ks.n-1])), PROVEN_ARG((int)(sl0)));
                        else low_fpf(out, "    rsp--; ss[ssp++] = rssl[rsp][{}];\n", PROVEN_ARG((int)(sl0)));
                        ks.n--; ks.o[ks.n] = -1; ks.k[ks.n] = K_AREF; ks.ve[ks.n] = (unsigned char)(as0 + 1); ks.vn[ks.n] = 0; ks.fl[ks.n] = 0;
                        ks.n++;
                        break;
                    }
                    if (sl0 >= 0 && ir->structs[sx0].f[sl0].arrn) {   // ★ 2026-11-01 — 배열 칸: 지역 레코드의 바이트를 보는 슬라이스(무복사 — VM 의 `vm_arr_view`)
                        const low_ir_sfield_t *FA = &ir->structs[sx0].f[sl0];
                        low_fpf(out, "    rsp--; ss[ssp].p = rs[rsp] + {}; ss[ssp].n = {}; ssp++;\n",
                                PROVEN_ARG((unsigned)((unsigned)FA->off)), PROVEN_ARG((unsigned)(cbe_arr_len(FA))));   // `n` 은 원소 수(줄이면 줄 수) · 폭은 ve
                        ks.n--; ks.o[ks.n] = -1; ks.k[ks.n] = cbe_arr_k(FA); ks.ve[ks.n] = FA->arresz; ks.vn[ks.n] = (unsigned char)cbe_arr_rb(FA); ks.fl[ks.n] = cbe_arr_fl(FA);
                        ks.n++;
                        break;
                    }
                    if (sl0 >= 0 && ir->structs[sx0].f[sl0].boxed && ir->structs[sx0].f[sl0].elem) {
                        if (ks.o[ks.n-1] >= 0)
                            low_fpf(out, "    rsp--; ss[ssp++] = rlocsl[{}][{}];\n",
                                    PROVEN_ARG((int)((int)ks.o[ks.n-1])), PROVEN_ARG((int)(sl0)));
                        else   // 방금 만든 레코드 — 슬라이스는 **스택 칸**에 있다
                            low_fpf(out, "    rsp--; ss[ssp++] = rssl[rsp][{}];\n", PROVEN_ARG((int)(sl0)));
                        // ★ 2026-10-31 — 아직 안 세운 칸(갓 띄운 액터의 0)을 읽으면 VM 처럼 멈춘다(정본 §10.3 (1c) — 세우기 전에 읽는 것은 적합하지 않다).
                        low_puts(out, "    if (LW_UNLIKELY(ss[ssp-1].p == LW_UNINIT)) lw_panic(\"len needs a slice — a slice field of an actor was read before it was set (canon 10.3 (1c))\");\n");
                        ks.n--; ks.o[ks.n] = -1; ks.k[ks.n] = K_SL;
                        ks.ve[ks.n] = ir->structs[sx0].f[sl0].elem; ks.vn[ks.n] = 0; ks.fl[ks.n] = cbe_sf_fl(&ir->structs[sx0].f[sl0]);
                        ks.n++;
                        break;
                    }
                }
                if (fk >= K_VIEW) {
                    int sx = fk - K_VIEW;
                    const low_ir_struct_t *S = &ir->structs[sx];
                    int sl = cbe_view_slot(ir, sx, in->a);
                    if (S->f[sl].sidx >= 0) {
                        // ★★★ 중첩 뷰 — 포인터만 오프셋만큼 옮기고 길이를 중첩 크기로 좁힌다.
                        int isx = (int)S->f[sl].sidx;
                        low_fpf(out, "    ss[ssp-1].p += {}; ss[ssp-1].n = {};\n",
                                PROVEN_ARG((unsigned)((unsigned)S->f[sl].off)), PROVEN_ARG((int)((int)ir->structs[isx].total)));
                        ks.k[ks.n-1] = (unsigned char)(K_VIEW + isx); ks.o[ks.n-1] = -1;
                        break;
                    }
                    if (S->f[sl].arrn) {   // ★ 2026-11-01 — 뷰의 배열 칸: 오프셋만 옮기고 길이를 원소 수로(종류 실행과 짝)
                        low_fpf(out, "    ss[ssp-1].p += {}; ss[ssp-1].n = {};\n", PROVEN_ARG((unsigned)((unsigned)S->f[sl].off)), PROVEN_ARG((unsigned)(cbe_arr_len(&S->f[sl]))));
                        ks.k[ks.n-1] = cbe_arr_k(&S->f[sl]); ks.ve[ks.n-1] = S->f[sl].arresz; ks.vn[ks.n-1] = (unsigned char)cbe_arr_rb(&S->f[sl]); ks.fl[ks.n-1] = cbe_arr_fl(&S->f[sl]); ks.o[ks.n-1] = -1;
                        break;
                    }
                    if (S->is_shadow && S->f[sl].bxs1) {
                        // ★★ 2026-11-02 (W-20261102-0024 O4) — 마디의 주소 칸: 0 이면 none, 아니면 그 마디의 뷰를 싼 option(종류 실행과 짝 · VM 의 FIELD 와 같다)
                        low_fpf(out, "    {{ const unsigned char *p_; memcpy(&p_, ss[--ssp].p + {}, sizeof p_);"
                                     " if (p_) {{ ws[wsp].s.p = p_; ws[wsp].s.n = {}; ws[wsp].v = 0; ws[wsp].tag = 1; }} else {{ ws[wsp].v = 0; ws[wsp].tag = 2; }} wsp++; }}\n",
                                PROVEN_ARG((unsigned)((unsigned)S->f[sl].off)), PROVEN_ARG((unsigned)((unsigned)ir->structs[S->f[sl].bxs1 - 1].total)));
                        ks.k[ks.n-1] = K_OPT; ks.ve[ks.n-1] = (unsigned char)S->f[sl].bxs1; ks.fl[ks.n-1] = 1; ks.vn[ks.n-1] = 0; ks.o[ks.n-1] = -1;
                        break;
                    }
                    if (S->is_shadow && S->f[sl].slmeta) {
                        // ★★ 그림자 배치의 슬라이스 칸 — (주소, 길이)를 그 자리에서 읽어 슬라이스 칸으로 바꾼다(종류 실행과 짝 · 2026-10-31).
                        //   주소 자리의 너비는 처리기의 것(low_ptr_slot)이고 주소는 그 앞머리에 든다 — VM · 재배치 상수와 같은 배치다.
                        const unsigned ez_ = (unsigned)(S->f[sl].slmeta & 0xff);
                        low_fpf(out, "    {{ const unsigned char *q_ = ss[ssp-1].p + {}; const unsigned char *p_; unsigned long long n_;\n"
                                     "      memcpy(&p_, q_, sizeof p_); memcpy(&n_, q_ + {}, 8); ss[ssp-1].p = p_; ss[ssp-1].n = (size_t)n_; }}\n",
                                PROVEN_ARG((unsigned)((unsigned)S->f[sl].off)), PROVEN_ARG((unsigned)(low_ptr_slot())));
                        ks.k[ks.n-1] = K_SL; ks.ve[ks.n-1] = (unsigned char)ez_; ks.vn[ks.n-1] = 0; ks.o[ks.n-1] = -1;
                        ks.fl[ks.n-1] = (S->f[sl].slmeta & IR_FLT_BIT) ? CBE_FL_FLT : ((S->f[sl].slmeta & IR_SGN_BIT) && ez_ < 8) ? CBE_FL_SGN : 0;
                        break;
                    }
                    if (S->is_mmio) {
                        // ★ 디바이스 레지스터 — **폭이 맞는 volatile 접근 한 번**(RFC-0042 D1).
                        low_fpf(out, "    {{ unsigned long long x_ = lw_vld(ss[--ssp].p + {}, {}u);\n",
                                PROVEN_ARG((unsigned)((unsigned)S->f[sl].off)), PROVEN_ARG((unsigned)((unsigned)S->f[sl].size)));
                        if (S->f[sl].flt)
                            low_fpf(out, "      fs[fsp++] = lw_b2f(x_, {}); }}\n", PROVEN_ARG((int)((int)S->f[sl].size)));
                        else
                            low_puts(out, "      st[sp++] = (long long)x_; }\n");
                        ks.n--; ks.o[ks.n] = -1; ks.k[ks.n++] = S->f[sl].flt ? K_FLT : K_INT;
                        break;
                    }
                    low_fpf(out, "    {{ const unsigned char *q_ = ss[--ssp].p + {}; unsigned long long x_ = 0;\n",
                            PROVEN_ARG((unsigned)((unsigned)S->f[sl].off)));
                    if (S->f[sl].be)
                        low_fpf(out, "      for (int b_ = 0; b_ < {}; b_++) x_ = (x_ << 8) | q_[b_];\n",
                                PROVEN_ARG((int)((int)S->f[sl].size)));
                    else
                        low_fpf(out, "      for (int b_ = {}; b_-- > 0; ) x_ = (x_ << 8) | q_[b_];\n",
                                PROVEN_ARG((int)((int)S->f[sl].size)));
                    if (S->f[sl].flt)   // ★ 부동 필드 — 비트를 f64 로 본다.
                        low_fpf(out, "      fs[fsp++] = lw_b2f(x_, {}); }}\n", PROVEN_ARG((int)((int)S->f[sl].size)));
                    else if (low_sfield_sgn(&S->f[sl]) && S->f[sl].size < 8)   // ★ 부호 있는 칸은 부호를 늘린다(2026-10-31)
                        low_fpf(out, "      st[sp++] = (long long)(x_ << {}) >> {}; }}\n", PROVEN_ARG((int)(64 - 8 * (int)S->f[sl].size)), PROVEN_ARG((int)(64 - 8 * (int)S->f[sl].size)));
                    else
                        low_puts(out, "      st[sp++] = (long long)x_; }\n");
                    ks.n--; ks.o[ks.n] = -1; ks.k[ks.n++] = S->f[sl].flt ? K_FLT : K_INT;
                    break;
                } else {
                    int sx = fk - K_REC;
                    int sl = cbe_rec_slot(ir, sx, in->a);
                    const low_ir_sfield_t *F = &ir->structs[sx].f[sl];   // ★ 오프셋은 타입이 안다
                    if (F->sidx >= 0) {
                        // ★★★ 중첩 레코드 필드 — 그 **구간을 앞으로 옮겨** 서브레코드로 만든다.
                        //   (바이트가 되면서 "잎을 센다" 는 개념 자체가 없어졌다.)
                        // ★★★★ **가리키는 곳을 옮긴다** (2026-08-03). 전에는 잎을 앞으로
                        //   `memmove` 했는데, 스택이 포인터가 된 지금 그것은 **원본을 고치는**
                        //   짓이다(별칭이니까). 중첩은 부모 바이트의 한 구간이므로 **주소만**
                        //   옮기면 된다 — 더 맞고, 복사가 아예 없다.
                        low_fpf(out, "    rs[rsp-1] = rs[rsp-1] + {};\n", PROVEN_ARG((unsigned)((unsigned)F->off)));
                        ks.k[ks.n-1] = (unsigned char)(K_REC + F->sidx); ks.o[ks.n-1] = -1;
                        break;
                    }
                    if (F->flt)   // ★ 부동 필드 — 비트를 f64 로 본다.
                        low_fpf(out, "    fs[fsp++] = lw_b2f(lw_ld_f(rs[--rsp] + {}, {}, {}), {});\n",
                                PROVEN_ARG((unsigned)((unsigned)F->off)), PROVEN_ARG((int)((int)F->size)), PROVEN_ARG((int)(F->be ? 1 : 0)), PROVEN_ARG((int)((int)F->size)));
                    else if (low_sfield_sgn(F) && F->size < 8)   // ★ 부호 있는 칸은 부호를 늘린다(2026-10-31 — `i32` 칸의 −5 가 4294967291 이었다)
                        low_fpf(out, "    st[sp++] = (long long)(lw_ld_f(rs[--rsp] + {}, {}, {}) << {}) >> {};\n",
                                PROVEN_ARG((unsigned)((unsigned)F->off)), PROVEN_ARG((int)((int)F->size)), PROVEN_ARG((int)(F->be ? 1 : 0)), PROVEN_ARG((int)(64 - 8 * (int)F->size)), PROVEN_ARG((int)(64 - 8 * (int)F->size)));
                    else
                        low_fpf(out, "    st[sp++] = (long long)lw_ld_f(rs[--rsp] + {}, {}, {});\n",
                                PROVEN_ARG((unsigned)((unsigned)F->off)), PROVEN_ARG((int)((int)F->size)), PROVEN_ARG((int)(F->be ? 1 : 0)));
                    ks.n--; ks.o[ks.n] = -1; ks.k[ks.n++] = ir->structs[sx].f[sl].flt ? K_FLT : K_INT;
                    break;
                }
            }
            case IRW_FSTORE: if ((ks.k[ks.n-1] == K_SL || ks.k[ks.n-1] == K_ROWS) && ks.k[ks.n-2] >= K_REC && ks.k[ks.n-2] < K_VIEW) {   // ★ 줄의 나열은 배열 칸에만(종류 실행이 지킨다)
                // ★ 슬라이스 필드에 쓰기 — 평행 칸에 담는다(RFC-0106 단계 4).
                int sxw = ks.k[ks.n-2] - K_REC;
                int slw = cbe_rec_slot(ir, sxw, in->a);
                if (ir->structs[sxw].f[slw].arrn) {   // ★ 2026-11-01 — 배열 칸에 쓰기: 지역 레코드의 그 자리에 바이트를 베낀다(VM 처럼 제자리)
                    const low_ir_sfield_t *FA = &ir->structs[sxw].f[slw];
                    const unsigned need_ = (unsigned)FA->arrn * (unsigned)FA->arresz;
                    const unsigned ve_ = ks.k[ks.n-1] == K_ROWS ? (unsigned)ks.vn[ks.n-1] : ks.ve[ks.n-1] ? ks.ve[ks.n-1] : 1u;   // 슬라이스의 `n` 은 원소 수다(줄이면 줄 수)
                    low_fpf(out, "    {{ lw_sl a_ = ss[--ssp]; rsp--; if (LW_UNLIKELY(a_.n * {}u != {}u)) lw_panic(\"an array field takes exactly its length of elements\");"
                                 " memmove(rloc[{}] + {}, a_.p, {}); }}\n", PROVEN_ARG((unsigned)(ve_)), PROVEN_ARG((unsigned)(need_)), PROVEN_ARG((int)((int)ks.o[ks.n-2])), PROVEN_ARG((unsigned)((unsigned)FA->off)), PROVEN_ARG((unsigned)(need_)));
                    ks.n -= 2;
                    break;
                }
                low_fpf(out, "    rlocsl[{}][{}] = ss[--ssp]; rsp--;\n", PROVEN_ARG((int)((int)ks.o[ks.n-2])), PROVEN_ARG((int)(slw)));
                // ★★★★★ **지역 q 는 파라미터 q 다** (진입에서 `rloc[q] = ar[q]` 로 맺어진다).
                //   그러니 파라미터 레코드의 슬라이스 필드에 쓴 것은 **파라미터 칸에도** 남겨야
                //   호출자의 되쓰기가 본다. 안 그러면 핸들러가 쓴 슬라이스가 그 함수와 함께 죽는다.
                {   int lq_ = (int)ks.o[ks.n-2];
                    if (lq_ >= 0 && (proven_size_t)lq_ < d->nparams &&
                        ((d->param_struct >> lq_) & 1u))
                        low_fpf(out, "    rsl[{} * LW_RECF + {}] = rlocsl[{}][{}];\n",
                                PROVEN_ARG((int)(lq_)), PROVEN_ARG((int)(slw)), PROVEN_ARG((int)(lq_)), PROVEN_ARG((int)(slw)));
                }
                ks.n -= 2;
                break;
            } else if ((ks.k[ks.n-1] == K_SL || ks.k[ks.n-1] == K_ROWS) && ks.k[ks.n-2] >= K_VIEW && ks.k[ks.n-2] < K_SVIEW) {
                // ★ 2026-11-01 — 그림자 뷰의 슬라이스 칸: (주소, 길이)를 그 자리에 적는다(주소 자리는 low_ptr_slot · 길이는 그 뒤 8 바이트 — VM 의 배치)
                const int vsx_ = ks.k[ks.n-2] - K_VIEW;
                const int vsl_ = cbe_view_slot(ir, vsx_, in->a);
                if (ir->structs[vsx_].f[vsl_].arrn) {   // ★ 뷰의 배열 칸에 쓰기 — 길이가 같아야 하고 제자리에 베낀다(VM 의 vm_arr_copy)
                    const low_ir_sfield_t *FA = &ir->structs[vsx_].f[vsl_];
                    const unsigned need_ = (unsigned)FA->arrn * (unsigned)FA->arresz, ve_ = ks.k[ks.n-1] == K_ROWS ? (unsigned)ks.vn[ks.n-1] : ks.ve[ks.n-1] ? ks.ve[ks.n-1] : 1u;
                    low_fpf(out, "    {{ lw_sl v_ = ss[--ssp]; unsigned char *q_ = (unsigned char *)ss[--ssp].p + {};\n"
                                 "      if (LW_UNLIKELY(v_.n * {}u != {}u)) lw_panic(\"an array field takes exactly its length of elements\"); memmove(q_, v_.p, {}); }}\n",
                            PROVEN_ARG((unsigned)((unsigned)FA->off)), PROVEN_ARG((unsigned)(ve_)), PROVEN_ARG((unsigned)(need_)), PROVEN_ARG((unsigned)(need_)));
                    ks.n -= 2;
                    break;
                }
                low_fpf(out, "    {{ lw_sl v_ = ss[--ssp]; unsigned char *q_ = (unsigned char *)ss[--ssp].p + {}; unsigned long long n_ = (unsigned long long)v_.n;\n"
                             "      memcpy(q_, &v_.p, sizeof v_.p); memcpy(q_ + {}, &n_, 8); }}\n",
                        PROVEN_ARG((unsigned)((unsigned)ir->structs[vsx_].f[vsl_].off)), PROVEN_ARG((unsigned)(low_ptr_slot())));
                ks.n -= 2;
                break;
            } else if (ks.k[ks.n-1] == K_OPT && ks.k[ks.n-2] >= K_VIEW && ks.k[ks.n-2] < K_SVIEW) {
                // ★★ 2026-11-02 (W-20261102-0024 O4) — 마디의 주소 칸에 쓴다: some 이면 그 마디의 주소, none 이면 0(VM 의 FSTORE 와 같다)
                const int vsx_ = ks.k[ks.n-2] - K_VIEW, vsl_ = cbe_view_slot(ir, vsx_, in->a);
                low_fpf(out, "    {{ lw_r w_ = ws[--wsp]; const unsigned char *p_ = w_.tag == 1 ? w_.s.p : 0; memcpy((unsigned char *)ss[--ssp].p + {}, &p_, sizeof p_); }}\n",
                        PROVEN_ARG((unsigned)((unsigned)ir->structs[vsx_].f[vsl_].off)));
                ks.n -= 2;
                break;
            } else if (ks.k[ks.n-1] == K_OPT && ks.k[ks.n-2] >= K_REC && ks.k[ks.n-2] < K_VIEW) {
                // ★★ 2026-11-03 (W-20261103-1143) — 레코드의 «마디를 가리키는 칸»에 쓴다: 지역 레코드의 바이트 자리에 주소(none = 0 — 종류 실행과 짝)
                const int sxn_ = ks.k[ks.n-2] - K_REC, sln_ = cbe_rec_slot(ir, sxn_, in->a);
                low_fpf(out, "    {{ lw_r w_ = ws[--wsp]; const unsigned char *p_ = w_.tag == 1 ? w_.s.p : 0; memcpy(rloc[{}] + {}, &p_, sizeof p_); rsp--; }}\n",
                        PROVEN_ARG((int)((int)ks.o[ks.n-2])), PROVEN_ARG((unsigned)((unsigned)ir->structs[sxn_].f[sln_].off)));
                ks.n -= 2;
                break;
            } else if (ks.k[ks.n-2] >= K_VIEW) {
                // ★ 뷰에 쓰는 것은 **바이트 버퍼에 쓰는 것**이다 — 포인터가 곧 그 자리다.
                int sx = ks.k[ks.n-2] - K_VIEW;
                const low_ir_struct_t *S = &ir->structs[sx];
                int sl = cbe_view_slot(ir, sx, in->a);
                if (S->is_mmio) {
                    // ★ 디바이스 레지스터 — **폭이 맞는 volatile 쓰기 한 번**(RFC-0042 D1).
                    low_fpf(out, "    {{ long long v_ = st[--sp];"
                                 " lw_vst((unsigned char *)ss[--ssp].p + {}, {}u, (unsigned long long)v_); }}\n",
                            PROVEN_ARG((unsigned)((unsigned)S->f[sl].off)), PROVEN_ARG((unsigned)((unsigned)S->f[sl].size)));
                    ks.n -= 2;
                    break;
                }
                low_fpf(out, "    {{ long long v_ = st[--sp]; unsigned char *q_ = (unsigned char *)ss[--ssp].p + {};\n",
                        PROVEN_ARG((unsigned)((unsigned)S->f[sl].off)));
                if (S->f[sl].be)
                    low_fpf(out, "      for (int b_ = 0; b_ < {}; b_++)"
                                 " q_[b_] = (unsigned char)((unsigned long long)v_ >> (8 * ({} - 1 - b_)));\n",
                            PROVEN_ARG((int)((int)S->f[sl].size)), PROVEN_ARG((int)((int)S->f[sl].size)));
                else
                    low_fpf(out, "      for (int b_ = 0; b_ < {}; b_++)"
                                 " q_[b_] = (unsigned char)((unsigned long long)v_ >> (8 * b_));\n",
                            PROVEN_ARG((int)((int)S->f[sl].size)));
                low_puts(out, "    }\n");
                ks.n -= 2;
                break;
            } else {
                // ★★★ **지역에 직접 쓴다.** 태그 경로의 레코드는 **박스(참조)** 이므로
                //   `set (field q x) v .` 가 지역 `q` 에 보인다 — 값 복사본에 쓰면 **사라진다**.
                //   (그리고 그것을 **차등 스윕이 즉시 고발했다**: VM 은 f(2)=4, 네이티브는 3.)
                int sxw = ks.k[ks.n-2] - K_REC;
                int sl = cbe_rec_slot(ir, sxw, in->a);
                const low_ir_sfield_t *FW = &ir->structs[sxw].f[sl];
                low_fpf(out, "    lw_st_f(rloc[{}] + {}, {}, {}, (unsigned long long)st[--sp]); rsp--;\n",
                        PROVEN_ARG((int)((int)ks.o[ks.n-2])), PROVEN_ARG((unsigned)((unsigned)FW->off)), PROVEN_ARG((int)((int)FW->size)), PROVEN_ARG((int)(FW->be ? 1 : 0)));
                ks.n -= 2;
                break;
            }
            case IRW_VIEW: {
                // ★★★ **바이트 위의 무복사 구조체 뷰** — 헤더 파싱·MMIO 의 기본 모양.
                //   포인터는 그대로 두고, **계약(정렬·길이)만 확인**한다. 복사는 0바이트다.
                int sx = (int)(in->a & 0xffff);
                const low_ir_struct_t *S = &ir->structs[sx];
                if (S->align > 1)
                    low_fpf(out, "    if (LW_UNLIKELY((uintptr_t)ss[ssp-1].p % {}u))"
                                 " lw_panic(\"view: base address violates the struct's align contract\");\n",
                            PROVEN_ARG((unsigned)((unsigned)S->align)));
                low_fpf(out, "    if (LW_UNLIKELY(ss[ssp-1].n < {}u))"
                             " lw_panic(\"view: slice shorter than the layout\");\n", PROVEN_ARG((unsigned)((unsigned)S->total)));
                ks.k[ks.n-1] = (unsigned char)(K_VIEW + sx); ks.o[ks.n-1] = -1;
                break;
            }
            case IRW_TRYVIEW: {
                // ★ try_view — 정렬·길이 계약을 **트랩 대신 값으로** 검사한다: some(view) | none.
                int sx = (int)(in->a & 0xffff);
                const low_ir_struct_t *S = &ir->structs[sx];
                low_puts(out, "    { lw_sl b_ = ss[--ssp];\n");
                if (S->align > 1)
                    low_fpf(out, "      if (b_.n < {}u || ((uintptr_t)b_.p % {}u)", PROVEN_ARG((unsigned)((unsigned)S->total)), PROVEN_ARG((unsigned)((unsigned)S->align)));
                else
                    low_fpf(out, "      if (b_.n < {}u", PROVEN_ARG((unsigned)((unsigned)S->total)));
                {   // ★ RFC-0150 §8-31 — `bool` 칸의 바이트가 0 · 1 이 아니면 none(길이를 먼저 봤으므로 읽어도 된다 — `||` 는 차례대로).
                    //   폭을 적은 열거 칸이 있는 구조체는 빠른 길에 오지 않는다(cbe_view_ok) — 태그 경로의 lw_tryview 가 번호를 잰다.
                    low_ir_cfield_t cf[64]; proven_size_t nc = low_ir_struct_cfields(ir, (proven_size_t)sx, cf, 64);
                    for (proven_size_t q = 0; q < nc; q++) if (cf[q].isbool) low_fpf(out, " || ((const unsigned char *)b_.p)[{}] > 1", PROVEN_ARG((unsigned)((unsigned)cf[q].off)));
                }
                low_puts(out, ")");
                low_puts(out, " { ws[wsp].tag = 2; wsp++; }\n"
                      "      else { ws[wsp].s = b_; ws[wsp].tag = 1; wsp++; } }\n");
                ks.k[ks.n-1] = K_OPT; ks.ve[ks.n-1] = (unsigned char)(sx + 1); ks.fl[ks.n-1] = 1;
                ks.o[ks.n-1] = -1;
                break;
            }
            case IRW_EXTERN: {
                // ★★★ **extern.call** — C 함수를 직접 부른다. 파라미터를 지역(loc/sloc/floc)에서
                //   읽어 넘긴다(슬라이스는 포인터+길이, cap 은 안 넘긴다). 반환은 st[] 로.
                const low_ir_def_t *E = &ir->defs[in->a & 0xffff];
                bool eflt = cbe_ret_flt(E);   // ★ 반환이 f64 면 double 로 받아 부동 스택으로
                bool eptr = E->out_ptr;       // ★ 반환이 생 포인터(cstr)면 const char* 로 받아 스칼라로
                low_fpf(out, "    {{ {} cr_ = ", proven_arg_cstr(eptr ? "const char *" : eflt ? "double" : "long long"));
                low_write(out, E->link_name.ptr, E->link_name.size);
                low_putc(out, '(');
                { proven_size_t nn = 0;
                  for (proven_size_t q = 0; q < E->nparams; q++) {
                      if ((E->param_cap >> q) & 1u) continue;   // cap 은 C 로 안 간다
                      if ((E->param_uptr >> q) & 1u)            // ★ unsafe_ptr — 단일 생 포인터(길이 없음)
                          low_fpf(out, "{}(void *)sloc[{}].p", proven_arg_cstr(nn++ ? ", " : ""), PROVEN_ARG((size_t)((size_t)q)));
                      else if ((E->param_cstr >> q) & 1u)       // ★ cstr — 널종단 char* (슬라이스 칸의 포인터, RFC-0137 P3)
                          low_fpf(out, "{}(const char *)sloc[{}].p", proven_arg_cstr(nn++ ? ", " : ""), PROVEN_ARG((size_t)((size_t)q)));
                      else if ((E->param_slice >> q) & 1u)
                          low_fpf(out, "{}({}{} *)sloc[{}].p, sloc[{}].n", proven_arg_cstr(nn++ ? ", " : ""), proven_arg_cstr(((E->param_mutw >> q) & 1u) ? "" : "const "),
                                  proven_arg_cstr(cbe_slice_ctype(E->param_ebits[q], E->param_selem[q])), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)q)));
                      else if ((E->param_flt >> q) & 1u)
                          low_fpf(out, "{}floc[{}]", proven_arg_cstr(nn++ ? ", " : ""), PROVEN_ARG((size_t)((size_t)q)));
                      else
                          low_fpf(out, "{}loc[{}]", proven_arg_cstr(nn++ ? ", " : ""), PROVEN_ARG((size_t)((size_t)q)));
                  } }
                // ★ 부동 반환은 **부동 스택**(fs)으로, 생 포인터·정수 반환은 정수 스택(st)으로 —
                //   포인터는 기계어 한 칸이라 정수 스칼라로 실린다(K_INT). str_from_cstr 가 그것을 읽는다.
                if (eflt)      { low_puts(out, "); fs[fsp++] = cr_; }\n"); ks.o[ks.n] = -1; ks.k[ks.n++] = K_FLT; }
                else if (eptr) { low_puts(out, "); ss[ssp].p = (const unsigned char *)cr_; ss[ssp].n = 0; ssp++; }\n");   // ★ RFC-0137 P3 — 포인터 칸
                                 ks.o[ks.n] = -1; ks.ve[ks.n] = 1; ks.fl[ks.n] = 0; ks.k[ks.n++] = K_SL; }
                else           { low_puts(out, "); st[sp++] = cr_; }\n"); ks.o[ks.n] = -1; ks.k[ks.n++] = K_INT; }
                break;
            }
            case IRW_BFILL: {   // ★ 할당기 바이트를 나열의 본으로(태그 경로와 같은 뜻 · 같은 멈춤)
                unsigned long long len = (unsigned long long)in->a >> 32, si1 = (unsigned long long)in->a & 0xffffffffull;
                low_fpf(out, "    {{ if (LW_UNLIKELY(ss[ssp-1].n < {}ULL)) lw_upanic(\"the allocator gave fewer bytes than the list needs\"); ", PROVEN_ARG((unsigned long long)(len)));
                if (si1) low_fpf(out, "memcpy((void *)ss[ssp-1].p, lw_strs[{}].p, {}); ", PROVEN_ARG((unsigned long long)(si1 - 1)), PROVEN_ARG((unsigned long long)(len)));
                else low_fpf(out, "memset((void *)ss[ssp-1].p, 0, {}); ", PROVEN_ARG((unsigned long long)(len)));
                low_fpf(out, "ss[ssp-1].n = {}; }}\n", PROVEN_ARG((unsigned long long)(len)));
                ks.o[ks.n-1] = -1; ks.fl[ks.n-1] = 0; break;
            }
            case IRW_LBUF: {   // ★ 틀 안 나열 자리(종류 실행과 짝) — 0 으로 채운 바이트
                unsigned long long off = (unsigned long long)in->a & 0xffffffffull, len = (unsigned long long)in->a >> 32;
                low_fpf(out, "    memset(lw_lb + {}, 0, {}); ss[ssp].p = lw_lb + {}; ss[ssp].n = {}; ssp++;\n", PROVEN_ARG((unsigned long long)(off)), PROVEN_ARG((unsigned long long)(len)), PROVEN_ARG((unsigned long long)(off)), PROVEN_ARG((unsigned long long)(len)));
                ks.o[ks.n] = -1; ks.ve[ks.n] = 1; ks.vn[ks.n] = 0; ks.fl[ks.n] = 0; ks.k[ks.n++] = K_SL; break;
            }
            case IRW_LBUFC: {  // ★ 틀 안 자리를 상수 본으로 채운다
                unsigned long long off = (unsigned long long)in->a & 0xffffffffull, si = (unsigned long long)in->a >> 32;
                unsigned long long len = (unsigned long long)ir->strs[si].size;
                low_fpf(out, "    memcpy(lw_lb + {}, lw_strs[{}].p, {}); ss[ssp].p = lw_lb + {}; ss[ssp].n = {}; ssp++;\n", PROVEN_ARG((unsigned long long)(off)), PROVEN_ARG((unsigned long long)(si)), PROVEN_ARG((unsigned long long)(len)), PROVEN_ARG((unsigned long long)(off)), PROVEN_ARG((unsigned long long)(len)));
                ks.o[ks.n] = -1; ks.ve[ks.n] = 1; ks.vn[ks.n] = 0; ks.fl[ks.n] = 0; ks.k[ks.n++] = K_SL; break;
            }
            case IRW_STR: {
                // ★ 문자열 리터럴은 슬라이스다 — 태그도 박스도 없다.
                low_fpf(out, "    ss[ssp].p = lw_strs[{}].p; ss[ssp].n = lw_strs[{}].n; ssp++;\n",
                        PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((long long)((long long)in->a)));
                // ★ 원소폭을 **박는다**. 이걸 안 박으면 슬롯에 남은 **낡은 ve**(예: u64
                //   슬라이스의 8)를 물려받아, op 호출 인자에서 `cs.n *= 8` 로 길이가 뻥튀기된다
                //   (slice-unit-divergence: rehash 뒤 sm_get("apple") 가 리터럴 끝을 넘어 읽음).
                //   ★ 접두 리터럴(`u"…"`·`U"…"`)이 오면서 이 값이 **1 고정이 아니게 됐다** —
                //     그 결함이 다시 살아날 수 있는 정확한 자리다.
                proven_u8 ew = ir->strew ? ir->strew[in->a] : 1;
                ks.o[ks.n] = -1; ks.ve[ks.n] = ew ? ew : 1; ks.vn[ks.n] = 0; ks.fl[ks.n] = 0; ks.k[ks.n++] = K_SL; break;
            }
            case IRW_RESBLK:
                // ★★★★ RFC-0039 §9-2 갈래① — 블록은 **링커가 잡아 준 칸**에서 선다.
                //   `mmio` 가 주소를 **적는다**면 이쪽은 주소를 **받는다**. 둘 다 호출자가
                //   줄 것이 없다는 점이 같고, 그래서 ISR 이 쓸 수 있다.
                low_fpf(out, "    ss[ssp].p = lw_res_{}; ss[ssp].n = {}u; ssp++;\n",
                        PROVEN_ARG((size_t)((size_t)in->a)), PROVEN_ARG((unsigned)((unsigned)ir->structs[in->a].total)));
                ks.o[ks.n] = -1; ks.ve[ks.n] = 1; ks.k[ks.n++] = K_SL; break;
            case IRW_MMIOBLK:
                // ★★★ RFC-0042 §8-2 — 블록은 **타입이 아는 주소**에서 선다. 호출자가 줄 것이 없다.
                //   `volatile` 은 접근 지점(read_volatile/write_volatile)이 이미 붙인다 — 여기서는
                //   포인터만 낸다. 주소는 컴파일 상수라 한 칸도 안 쓴다.
                //   ★ RFC-0137 P4 — 능력 기계에서는 블록 크기로 경계까지 좁힌다(LW_MMIO_SPAN). 그 밖에서는 LW_MMIO_PTR 과 같다.
                low_fpf(out, "    ss[ssp].p = LW_MMIO_SPAN({}ULL, {}u); ss[ssp].n = {}u; ssp++;\n",
                        PROVEN_ARG((unsigned long long)((unsigned long long)ir->structs[in->a].mmio_base)),
                        PROVEN_ARG((unsigned)((unsigned)ir->structs[in->a].total)),
                        PROVEN_ARG((unsigned)((unsigned)ir->structs[in->a].total)));
                ks.o[ks.n] = -1; ks.ve[ks.n] = 1; ks.k[ks.n++] = K_SL; break;
            case IRW_DROP: {
                // ★ **어느 스택에서 버리는가**는 슬롯의 종류가 안다.
                unsigned char dk = ks.k[--ks.n];
                if (dk == K_SL || dk == K_ROWS || dk == K_AREF || dk >= K_VIEW) low_puts(out, "    ssp--;\n");
                else if (dk == K_FLT) low_puts(out, "    fsp--;\n");
                else if (dk == K_W128) low_puts(out, "    lw_hsp--;\n");   // ★ W-20261105-2304
                else if (dk == K_VEC) low_puts(out, "    vsp--;\n");
                else if (dk == K_REF) low_puts(out, "    /* ref dropped: nothing on a stack */\n");
                else if (dk == K_OPT || dk == K_OPTSL || dk == K_OPT2) low_puts(out, "    wsp--;\n");
                else if (dk >= K_REC) low_puts(out, "    rsp--;\n");
                else low_puts(out, "    sp--;\n");
                break;
            }
            // ★★★ 2a — 빠른 `spawn send`: 칸에 처리 코드의 빠른 판(어댑터) · 그 액터의 바이트 · 정수 인자를 넣는다(cbe_kind_run 이 모양을 보였다).
            //   검사의 차례와 말은 태그 경로(아래 IRW_ASEND)와 같다 — 묶인 우편함이 찼나, 칸이 다 찼나.
            case IRW_ASEND: {
                const proven_size_t c_ = IR_CALL_IDX(in->a);
                const low_ir_def_t *h_ = &ir->defs[c_];
                const int np_ = (int)h_->nparams, b_ = ks.n - np_, home_ = ks.o[b_];
                const low_ir_struct_t *S_ = &ir->structs[h_->param_sidx[0]];
                low_fpf(out, "    {{ int box_ = fa{}_;\n"
                             "      if (LW_UNLIKELY(box_ < 0)) lw_panic(\"internal: an actor spawned on the fast path has no home (2a)\");\n", PROVEN_ARG((int)(home_)));
                if (S_->mailbox_kind == 1)
                    low_fpf(out, "      if (lw_amb_pend(box_) >= {}ll) lw_panic(\"the actor's bounded mailbox is full — "
                                 "`spawn send` would exceed `mailbox bounded N` pending messages (drain it first, or raise N)\");\n",
                            PROVEN_ARG((long long)((long long)S_->mailbox_cap)));
                low_puts(out, "      if (lw_amb_full())"
                      " lw_panic(\"the task/message queue is exhausted — too many messages and tasks wait at once"
                      " (docs/runtime-pools.md; on the freestanding floor raise it with -DLW_T0_MBOX=N)\");\n");
                low_puts(out, "      int qi_ = lw_amb_push(box_); lw_ambox[qi_].qf = lw_q_"); put_sym(out, h_);
                low_fpf(out, "; lw_ambox[qi_].nargs = {}; LW_SET_NOY(qi_, 1);\n", PROVEN_ARG((int)(np_ - 1)));
                for (int g = 0; g + 1 < np_; g++)
                    low_fpf(out, "      lw_ambox[qi_].args[{}] = lw_int(st[sp-{}]);\n", PROVEN_ARG((int)(g)), PROVEN_ARG((int)(np_ - 1 - g)));
                low_fpf(out, "      sp -= {}; lw_ambox[qi_].self = rs[--rsp]; }}\n    st[sp++] = 0;\n", PROVEN_ARG((int)(np_ - 1)));
                ks.n = b_; ks.o[ks.n] = -1; ks.k[ks.n] = K_INT; ks.ve[ks.n] = 0; ks.vn[ks.n] = 0; ks.fl[ks.n] = 0; ks.n++;
                break;
            }
            case IRW_DRAIN: {
                const int home_ = ks.o[ks.n - 1];
                // ★ 처리 코드는 이 op 의 깊이 + 1 에서 돈다(VM 의 depth + 1) — 전역에 지역 깊이를 적어 둔다(W-20261110-2349)
                low_fpf(out, "    rsp--; {{ int dsv_ = lw_depth_tls(); lw_depth_tls_set(lw_depth); lw_drain(fa{}_); lw_depth_tls_set(dsv_); }} st[sp++] = 0;\n", PROVEN_ARG((int)(home_)));
                ks.n--; ks.o[ks.n] = -1; ks.k[ks.n] = K_INT; ks.ve[ks.n] = 0; ks.vn[ks.n] = 0; ks.fl[ks.n] = 0; ks.n++;
                break;
            }
            case IRW_SCHED:
                low_puts(out, "    { int dsv_ = lw_depth_tls(); lw_depth_tls_set(lw_depth); lw_schedule(); lw_depth_tls_set(dsv_); } st[sp++] = 0;\n");
                ks.o[ks.n] = -1; ks.k[ks.n] = K_INT; ks.ve[ks.n] = 0; ks.vn[ks.n] = 0; ks.fl[ks.n] = 0; ks.n++;
                break;
            case IRW_WRAP_SOME:
                if (ks.k[ks.n-1] >= K_REC && ks.k[ks.n-1] < K_VIEW) {   // ★ 레코드를 감싼다 → ws[].r
                    int wsx = ks.k[ks.n-1] - K_REC;
                    if (cbe_optrec_sl(ir, K_OPT, (unsigned char)(wsx + 1), 0))   // ★ 슬라이스 칸도 함께 싼다(평행 칸 wssl)
                        low_fpf(out, "    memcpy(ws[wsp].r, rs[--rsp], sizeof ws[0].r); memcpy(wssl[wsp], rssl[rsp], sizeof wssl[0]); ws[wsp].tag = 1; wsp++;\n");
                    else
                    low_fpf(out, "    memcpy(ws[wsp].r, rs[--rsp], sizeof ws[0].r); ws[wsp].tag = 1; wsp++;\n");
                    // ★ 레코드를 감싼 option 은 **뷰가 아니다** → fl=0. 안 지우면 슬롯에 남은 낡은 fl(예:
                    //   `len view_array f32` 가 남긴 fl=1)을 물려받아 some_value 가 뷰 분기(ss[])로 새어
                    //   레코드 첫 필드를 포인터로 역참조한다(native SEGV · VM≠native). 시뮬레이터도 동일.
                    ks.k[ks.n-1] = K_OPT; ks.ve[ks.n-1] = (unsigned char)(wsx + 1); ks.fl[ks.n-1] = 0;
                    break;
                }
                if (ks.k[ks.n-1] >= K_VIEW && ks.k[ks.n-1] < K_SVIEW) {   // ★ 2026-11-01 — 구조체 뷰를 감싼다(`try.view` 와 같은 모양)
                    const int vsx_ = ks.k[ks.n-1] - K_VIEW;
                    low_puts(out, "    ws[wsp].s = ss[--ssp]; ws[wsp].v = 0; ws[wsp].tag = 1; wsp++;\n");
                    ks.k[ks.n-1] = K_OPT; ks.ve[ks.n-1] = (unsigned char)(vsx_ + 1); ks.fl[ks.n-1] = 1; ks.vn[ks.n-1] = 0; ks.o[ks.n-1] = -1;
                    break;
                }
                if (ks.k[ks.n-1] == K_SL) {   // ★ WO-0222 — 슬라이스를 감싼다(폭 · 표식은 vn · fl 로 — 시뮬레이터와 같다)
                    low_puts(out, "    ws[wsp].s = ss[--ssp]; ws[wsp].v = 0; ws[wsp].tag = 1; wsp++;\n");
                    { const unsigned char w_ = ks.ve[ks.n-1] ? ks.ve[ks.n-1] : 1;
                      ks.k[ks.n-1] = K_OPTSL; ks.ve[ks.n-1] = 0; ks.vn[ks.n-1] = (unsigned char)(w_ > 1 ? w_ : 0); ks.o[ks.n-1] = -1; }
                    break;
                }
                if (ks.k[ks.n-1] == K_OPT && ks.ve[ks.n-1] == 0 && ks.fl[ks.n-1] == 0) {   // ★ 겹 — 안 태그를 t2 로
                    low_puts(out, "    ws[wsp-1].t2 = ws[wsp-1].tag; ws[wsp-1].tag = 1;\n");
                    ks.k[ks.n-1] = K_OPT2; ks.o[ks.n-1] = -1; break;
                }
                low_puts(out, "    ws[wsp].v = st[--sp]; ws[wsp].tag = 1; wsp++;\n");
                ks.k[ks.n-1] = K_OPT; ks.ve[ks.n-1] = 0; ks.fl[ks.n-1] = 0; break;
            // ★★★★ WO-0222 — 뿌리에서 깎는다. 태그 경로와 **같은 함수**(`lw_root_take`)·같은 규칙(음수 → none, 못 얻으면 none).
            //   박스 풀을 안 쓰므로 박스 가드도 없다.
            case IRW_ALLOCB:
                low_fpf(out, "    {{ long long n_ = st[--sp]; unsigned char *b_ = (n_ < 0 || LW_NOFIT(n_)) ? 0 : lw_root_take({}, (size_t)n_);\n"
                             "      if (!b_) {{ ws[wsp].v = 0; ws[wsp].tag = 2; }}\n"
                             "      else {{ ws[wsp].s.p = b_; ws[wsp].s.n = (size_t)n_; ws[wsp].v = 0; ws[wsp].tag = 1; }} wsp++; }}\n",
                        PROVEN_ARG((int)((int)in->a)));
                ks.k[ks.n-1] = K_OPTSL; ks.ve[ks.n-1] = 0; ks.vn[ks.n-1] = 0; ks.fl[ks.n-1] = 0; ks.o[ks.n-1] = -1; break;
            case IRW_RMARK:
                low_fpf(out, "    st[sp++] = (long long)lw_root_mark({});\n", PROVEN_ARG((int)((int)in->a)));
                ks.o[ks.n] = -1; ks.ve[ks.n] = 0; ks.fl[ks.n] = 0; ks.k[ks.n++] = K_INT; break;
            case IRW_RRESET:
                low_fpf(out, "    lw_root_reset({}, (size_t)st[--sp]);\n", PROVEN_ARG((int)((int)in->a)));
                ks.n--; break;
            case IRW_WRAP_OK:
                if (ks.k[ks.n-1] >= K_REC && ks.k[ks.n-1] < K_VIEW) {   // ★ 레코드를 감싼다 → ws[].r
                    int wsx = ks.k[ks.n-1] - K_REC;
                    if (cbe_optrec_sl(ir, K_OPT, (unsigned char)(wsx + 1), 0))   // ★ 슬라이스 칸도 함께 싼다(평행 칸 wssl)
                        low_fpf(out, "    memcpy(ws[wsp].r, rs[--rsp], sizeof ws[0].r); memcpy(wssl[wsp], rssl[rsp], sizeof wssl[0]); ws[wsp].tag = 3; wsp++;\n");
                    else
                    low_fpf(out, "    memcpy(ws[wsp].r, rs[--rsp], sizeof ws[0].r); ws[wsp].tag = 3; wsp++;\n");
                    // ★ 레코드를 감싼 result 도 뷰가 아니다 → fl=0(WRAP_SOME 과 같은 이유·같은 SEGV).
                    ks.k[ks.n-1] = K_OPT; ks.ve[ks.n-1] = (unsigned char)(wsx + 1); ks.fl[ks.n-1] = 0;
                    break;
                }
                if (ks.k[ks.n-1] == K_OPT && ks.ve[ks.n-1] == 0 && ks.fl[ks.n-1] == 0) {   // ★ 겹 — 안 태그를 t2 로
                    low_puts(out, "    ws[wsp-1].t2 = ws[wsp-1].tag; ws[wsp-1].tag = 3;\n");
                    ks.k[ks.n-1] = K_OPT2; ks.o[ks.n-1] = -1; break;
                }
                low_puts(out, "    ws[wsp].v = st[--sp]; ws[wsp].tag = 3; wsp++;\n");
                ks.k[ks.n-1] = K_OPT; ks.ve[ks.n-1] = 0; break;
            case IRW_WRAP_NONE:
                low_puts(out, "    ws[wsp].v = 0; ws[wsp].tag = 2; wsp++;\n");
                ks.o[ks.n] = -1; ks.ve[ks.n] = 0; ks.fl[ks.n] = 0; ks.k[ks.n++] = K_OPT; break;
            case IRW_WRAP_ERR:
                low_fpf(out, "    ws[wsp].v = {}ll; ws[wsp].tag = 4; wsp++;\n", PROVEN_ARG((long long)((long long)in->a)));
                ks.o[ks.n] = -1; ks.ve[ks.n] = 0; ks.k[ks.n++] = K_OPT; break;
            case IRW_ISSOME:
                low_puts(out, "    { lw_r w_ = ws[--wsp];"
                      " if (LW_UNLIKELY(w_.tag != 1 && w_.tag != 2)) lw_panic(\"is_some needs an option\");"
                      " st[sp++] = (w_.tag == 1); }\n");
                ks.k[ks.n-1] = K_INT; break;
            case IRW_ISOK: case IRW_ISERR:
                low_fpf(out, "    {{ lw_r w_ = ws[--wsp];"
                             " if (LW_UNLIKELY(w_.tag != 3 && w_.tag != 4)) lw_panic(\"{} needs a result\");"
                             " st[sp++] = (w_.tag == {}); }}\n",
                        proven_arg_cstr(in->w == IRW_ISOK ? "is_ok" : "is_error"), PROVEN_ARG((int)(in->w == IRW_ISOK ? 3 : 4)));
                ks.k[ks.n-1] = K_INT; break;
            case IRW_SOMEVAL:
                // ★★★ 2026-11-02 (W-20261102-0024) — 벗긴 값은 **스택 임시**다(종류 실행과 같다 — st.o = -1). 전에는 감싼 값이 든 지역 번호를
                //   물려받아, 그 값을 넘기는 부름이 그 번호의 레코드 칸(`rloc[o]` — 채운 적 없는 NULL)을 넘겼다: kvstore 의 `bench` 가
                //   `pool.release kv_bench_rec. p. some_value h. .` 에서 세그폴트로 죽었다(v1.25.1 · 그 전부터).
                if (ks.k[ks.n-1] == K_OPT2) {   // ★ 겹을 벗긴다 — 안 태그가 바깥이 된다
                    low_puts(out, "    { lw_r *w_ = &ws[wsp-1];"
                          " if (LW_UNLIKELY(w_->tag == 2)) lw_panic(\"some_value of none\");"
                          " if (LW_UNLIKELY(w_->tag != 1)) lw_panic(\"some_value needs an option\");"
                          " w_->tag = w_->t2; w_->t2 = 0; }\n");
                    ks.k[ks.n-1] = K_OPT; ks.ve[ks.n-1] = 0; ks.fl[ks.n-1] = 0; ks.o[ks.n-1] = -1; break;
                }
                if (ks.k[ks.n-1] == K_OPTSL) {   // ★ WO-0222 — 바이트 슬라이스를 꺼낸다
                    low_puts(out, "    { lw_r w_ = ws[--wsp];"
                          " if (LW_UNLIKELY(w_.tag == 2)) lw_panic(\"some_value of none\");"
                          " if (LW_UNLIKELY(w_.tag != 1)) lw_panic(\"some_value needs an option\");"
                          " ss[ssp++] = w_.s; }\n");
                    ks.k[ks.n-1] = K_SL; ks.ve[ks.n-1] = ks.vn[ks.n-1] ? ks.vn[ks.n-1] : 1; ks.vn[ks.n-1] = 0; ks.o[ks.n-1] = -1; break;
                }
                if (ks.ve[ks.n-1] && ks.fl[ks.n-1]) {   // ★ 뷰를 감쌌으면 ss[] 로 꺼낸다
                    unsigned char vx = (unsigned char)(K_VIEW + ks.ve[ks.n-1] - 1);
                    low_puts(out, "    { lw_r w_ = ws[--wsp];"
                          " if (LW_UNLIKELY(w_.tag == 2)) lw_panic(\"some_value of none\");"
                          " if (LW_UNLIKELY(w_.tag != 1)) lw_panic(\"some_value needs an option\");"
                          " ss[ssp++] = w_.s; }\n");
                    ks.k[ks.n-1] = vx; ks.fl[ks.n-1] = 0; ks.o[ks.n-1] = -1; break;
                }
                if (ks.ve[ks.n-1]) {   // ★ 레코드를 감쌌으면 rs[] 로 꺼낸다
                    unsigned char rx = (unsigned char)(K_REC + ks.ve[ks.n-1] - 1);
                    low_puts(out, "    { lw_r w_ = ws[--wsp];"
                          " if (LW_UNLIKELY(w_.tag == 2)) lw_panic(\"some_value of none\");"
                          " if (LW_UNLIKELY(w_.tag != 1)) lw_panic(\"some_value needs an option\");"
                          " rs[rsp] = rsb[rsp]; memcpy(rs[rsp], w_.r, LW_RECB); rsp++; }\n");
                    if (cbe_optrec_sl(ir, K_OPT, ks.ve[ks.n-1], 0))   // ★ 싼 레코드의 슬라이스 칸을 벗긴 자리로(방금 꺼낸 ws 자리 = wsp)
                        low_puts(out, "    memcpy(rssl[rsp-1], wssl[wsp], sizeof rssl[0]);\n");
                    ks.k[ks.n-1] = rx; ks.o[ks.n-1] = -1; break;
                }
                low_puts(out, "    { lw_r w_ = ws[--wsp];"
                      " if (LW_UNLIKELY(w_.tag == 2)) lw_panic(\"some_value of none\");"
                      " if (LW_UNLIKELY(w_.tag != 1)) lw_panic(\"some_value needs an option\");"
                      " st[sp++] = w_.v; }\n");
                ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1; break;
            case IRW_OKVAL:
                if (ks.k[ks.n-1] == K_OPT2) {   // ★ 겹을 벗긴다 — 안 태그가 바깥이 된다
                    low_puts(out, "    { lw_r *w_ = &ws[wsp-1];"
                          " if (LW_UNLIKELY(w_->tag == 4)) lw_panic(\"ok_value of an error\");"
                          " if (LW_UNLIKELY(w_->tag != 3)) lw_panic(\"ok_value needs a result\");"
                          " w_->tag = w_->t2; w_->t2 = 0; }\n");
                    ks.k[ks.n-1] = K_OPT; ks.ve[ks.n-1] = 0; ks.fl[ks.n-1] = 0; ks.o[ks.n-1] = -1; break;
                }
                if (ks.ve[ks.n-1] && ks.fl[ks.n-1]) {   // ★ 뷰를 감쌌으면 ss[] 로 꺼낸다
                    unsigned char vx = (unsigned char)(K_VIEW + ks.ve[ks.n-1] - 1);
                    low_puts(out, "    { lw_r w_ = ws[--wsp];"
                          " if (LW_UNLIKELY(w_.tag == 4)) lw_panic(\"ok_value of an error\");"
                          " if (LW_UNLIKELY(w_.tag != 3)) lw_panic(\"ok_value needs a result\");"
                          " ss[ssp++] = w_.s; }\n");
                    ks.k[ks.n-1] = vx; ks.fl[ks.n-1] = 0; ks.o[ks.n-1] = -1; break;
                }
                if (ks.ve[ks.n-1]) {   // ★ 레코드를 감쌌으면 rs[] 로 꺼낸다
                    unsigned char rx = (unsigned char)(K_REC + ks.ve[ks.n-1] - 1);
                    low_puts(out, "    { lw_r w_ = ws[--wsp];"
                          " if (LW_UNLIKELY(w_.tag == 4)) lw_panic(\"ok_value of an error\");"
                          " if (LW_UNLIKELY(w_.tag != 3)) lw_panic(\"ok_value needs a result\");"
                          " rs[rsp] = rsb[rsp]; memcpy(rs[rsp], w_.r, LW_RECB); rsp++; }\n");
                    if (cbe_optrec_sl(ir, K_OPT, ks.ve[ks.n-1], 0))
                        low_puts(out, "    memcpy(rssl[rsp-1], wssl[wsp], sizeof rssl[0]);\n");
                    ks.k[ks.n-1] = rx; ks.o[ks.n-1] = -1; break;
                }
                low_puts(out, "    { lw_r w_ = ws[--wsp];"
                      " if (LW_UNLIKELY(w_.tag == 4)) lw_panic(\"ok_value of an error\");"
                      " if (LW_UNLIKELY(w_.tag != 3)) lw_panic(\"ok_value needs a result\");"
                      " st[sp++] = w_.v; }\n");
                ks.k[ks.n-1] = K_INT; ks.o[ks.n-1] = -1; break;
            case IRW_ERRVAL:
                low_puts(out, "    { lw_r w_ = ws[--wsp];"
                      " if (LW_UNLIKELY(w_.tag == 3)) lw_panic(\"error_value of an ok\");"
                      " if (LW_UNLIKELY(w_.tag != 4)) lw_panic(\"error_value needs a result\");"
                      " st[sp++] = w_.v; }\n");
                ks.k[ks.n-1] = K_INT; break;
            case IRW_ELSE_NONE:
                low_puts(out, "    if (ws[wsp-1].tag == 4) { ws[wsp-1].tag = 2; ws[wsp-1].v = 0; }"
                      " else ws[wsp-1].tag = 1;\n");
                break;
            case IRW_ELSE_ERR:
                low_fpf(out, "    if (ws[wsp-1].tag == 2) {{ ws[wsp-1].tag = 4; ws[wsp-1].v = {}ll; }}"
                             " else ws[wsp-1].tag = 3;\n", PROVEN_ARG((long long)((long long)in->a)));
                break;
            case IRW_TRY:
                // ★★★ **오류를 조기 반환한다 — 그리고 깊이 원장을 되돌린다.**
                //   태그 경로는 그것을 **안 했다**: 오류를 128번 넘게 전파하면 죽었다.
                //   같은 실수를 여기서 반복하지 않는다(그리고 게이트가 지킨다).
                if (ks.k[ks.n-1] == K_OPT) {
                    low_puts(out, "    { lw_r w_ = ws[--wsp];"
                          " if (w_.tag == 4) { lw_depth--; return w_; }"
                          " st[sp++] = w_.v; }\n");
                    ks.k[ks.n-1] = K_INT;
                }
                break;
            case IRW_ASSERT:
                // ★★★ **계약은 빠른 경로에서도 계약이다** — 그리고 여기서도 **차갑다**.
                //   증명된 출구 검사는 **실제로 지운다**(그것이 계약이 사 주는 성능이다).
                if ((in->a & 3) && (in->a & IR_POL_PROVEN)) low_puts(out, "    sp--;\n");
                else low_fpf(out, "    if (LW_UNLIKELY(!st[sp-1])) lw_panic(\"{}\"); sp--;\n",
                             proven_arg_cstr((in->a & 4) ? LW_EXPECT_MSG   // ★ 2026-09-27 — expect 는 계약이 아니다(VM 의 E-TEST-FAIL)
                           : (in->a & 8) ? "an errors `when` condition is TRUE but the op returned normally"
                           : (in->a & 2) ? "errors `when` condition is false on this error path"
                           : (in->a & 1) ? "ensures violated at exit" : "requires violated at entry"));
                ks.n--; break;
            case IRW_PANIC:
                if (in->a == IR_PANIC_SINKFULL) { low_puts(out, "    lw_panic(\"" LW_SINKFULL_MSG "\");\n"); ks.n = 0; live2 = false; break; }   // ★ X-0065
                if (in->a == IR_PANIC_ENUMCAST) { low_puts(out, "    lw_panic(\"" LW_ENUMCAST_MSG "\");\n"); ks.n = 0; live2 = false; break; }
                if (in->a == IR_PANIC_NODEKIDS) { low_puts(out, "    lw_panic(\"" LW_NODEKIDS_MSG "\");\n"); ks.n = 0; live2 = false; break; }
                low_puts(out, "    lw_upanic(\"the program called `panic`\");\n"); ks.n = 0; live2 = false; break;
            // ★★★ **asm 문장 — 빠른 경로** (RFC-0042 D11). 피연산자 없는 형태만 여기 온다
            //   (있으면 cbe_mark_scalar 가 이 def 을 태그 경로로 보내며 **이유를 말한다**).
            //   ☞ 타깃 대조는 이미 검사 단계가 했다(RFC-0041 D9) — 여기는 백스톱이다.
            case IRW_ASM: {
                const low_ir_asm_t *A = &ir->asms[in->a];
                // ★ 이 자리는 값을 돌려줄 수 없다(void 이미터). 그래서 백스톱은 **C 가 거절하게**
                //   만든다 — 조용히 다른 ISA 의 명령을 싣느니 `#error` 로 서게 한다.
                if (!cbe_asm_target_ok(A->target)) {
                    low_fpf(low_stderr(), "E-ASM-TARGET: this asm is written for `{}` and this build "
                            "targets `{}`\n",
                            low_sv((const char *)A->target.ptr, (int)A->target.size), proven_arg_cstr(low_ir_target()->name));
                    low_puts(out, "\n#error \"E-ASM-TARGET: assembly for another target reached emission\"\n");
                }
                low_puts(out, "    __asm__ __volatile__ (\n        ");
                cbe_asm_tmpl(out, A);
                low_puts(out, "\n        : : : ");
                { proven_size_t n = 0;
                  for (proven_size_t k = 0; k < A->nclob; k++) {
                      if (proven_u8str_view_eq(A->clob[k], proven_u8str_view_from_cstr("flags")))
                          low_fpf(out, "{}\"cc\"", proven_arg_cstr(n++ ? ", " : ""));
                      else low_fpf(out, "{}\"{}\"", proven_arg_cstr(n++ ? ", " : ""),
                                   low_sv((const char *)A->clob[k].ptr, (int)A->clob[k].size));
                  } }
                low_puts(out, ");\n    st[sp++] = 0;\n");
                ks.o[ks.n] = -1; ks.k[ks.n++] = K_INT;
                break;
            }
            case IRW_CAST:
                if ((in->a & 0xff) == 0) {                 // → f64 · f32(IR_CAST_F32)
                    // ★ RFC-0157 §8-13 — f32 가 대상이면 한 번에 f32 로 반올림한다(VM 의 IRW_CAST 와 같은 식)
                    const bool to32 = (in->a & IR_CAST_F32) != 0;
                    if (ks.k[ks.n-1] == K_INT) {
                        const bool uns_ = (in->a & IR_SRC_UNS) != 0;   // ★ RFC-0157 §8-14 — 부호 없는 원천은 부호 없이 읽는다(VM 과 같다)
                        low_puts(out, uns_ ? (to32 ? "    fs[fsp++] = (double)(float)(unsigned long long)st[--sp];\n" : "    fs[fsp++] = (double)(unsigned long long)st[--sp];\n")
                                   : (to32 ? "    fs[fsp++] = (double)(float)st[--sp];\n" : "    fs[fsp++] = (double)st[--sp];\n"));
                        ks.k[ks.n-1] = K_FLT;
                    } else if (to32) low_puts(out, "    fs[fsp-1] = (double)(float)fs[fsp-1];\n");   // 이미 f64 면 항등
                } else if (in->a & 0x40000) {              // narrow_try → some|none (ws[])
                    if (ks.k[ks.n-1] == K_FLT)   // ★ 2026-11-11 — 부동 원천은 참값으로 잰다(lw_narrow_fits_f · VM 과 같다)
                        low_fpf(out, "    {{ long long x_; if (lw_narrow_fits_f({}, fs[--fsp], &x_)) {{ ws[wsp].tag = 1; ws[wsp].v = x_; }}"
                                     " else {{ ws[wsp].tag = 2; ws[wsp].v = 0; }} wsp++; }}\n", PROVEN_ARG((long long)((long long)in->a)));
                    else
                    low_fpf(out, "    {{ long long x_ = st[--sp];"
                                 " if (lw_narrow_fits({}, x_)) {{ ws[wsp].tag = 1; ws[wsp].v = x_; }}"
                                 " else {{ ws[wsp].tag = 2; ws[wsp].v = 0; }} wsp++; }}\n", PROVEN_ARG((long long)((long long)in->a)));
                    ks.k[ks.n-1] = K_OPT; ks.ve[ks.n-1] = 0; ks.vn[ks.n-1] = 0; ks.fl[ks.n-1] = 0;
                } else if (ks.k[ks.n-1] == K_FLT) {        // f64 → 정수 폭
                    low_fpf(out, "    st[sp++] = lw_cast_fi({}, fs[--fsp]);\n", PROVEN_ARG((long long)((long long)in->a)));   /* ★ 2026-11-11 — 참값으로 잰다 */
                    ks.k[ks.n-1] = K_INT;
                } else
                    low_fpf(out, "    st[sp-1] = lw_cast_i({}, st[sp-1]);\n", PROVEN_ARG((long long)((long long)in->a)));
                break;
            case IRW_NEG:
                if (ks.k[ks.n-1] == K_VEC) {   // ★ 2026-11-03 (W-20261103-2113) — 레인마다: 정수는 감는다(제 폭의 칸에 담으면 C 가 자른다) · 부동은 부호를 뒤집는다
                    const int ln_ = (int)ks.vn[ks.n-1], ez_ = (int)ks.ve[ks.n-1]; const char *m_ = lw_vmem((unsigned)ez_);
                    if (ks.fl[ks.n-1] == CBE_FL_FLT)
                        low_fpf(out, "    for (int k_ = 0; k_ < {}; k_++) vs[vsp-1].{}[k_] = lw_f2b(-lw_b2f(vs[vsp-1].{}[k_], {}), {});\n", PROVEN_ARG((int)(ln_)), proven_arg_cstr(m_), proven_arg_cstr(m_), PROVEN_ARG((int)(ez_)), PROVEN_ARG((int)(ez_)));
                    else
                        low_fpf(out, "    for (int k_ = 0; k_ < {}; k_++) vs[vsp-1].{}[k_] = 0ull - (unsigned long long)vs[vsp-1].{}[k_];\n", PROVEN_ARG((int)(ln_)), proven_arg_cstr(m_), proven_arg_cstr(m_));
                    ks.o[ks.n-1] = -1;
                    break;
                }
                if (ks.k[ks.n-1] == K_FLT) low_puts(out, "    fs[fsp-1] = -fs[fsp-1];\n");
                else low_fpf(out, "    st[sp-1] = lw_negs_i({}, st[sp-1]);\n", PROVEN_ARG((long long)((long long)in->a)));   // 넘치면 멈춘다 (RFC-0052 D4)
                break;
            case IRW_DIV:
                if (ks.n >= 2 && ks.k[ks.n-1] == K_VEC && ks.k[ks.n-2] == K_VEC) goto vecdiv_;   // ★ 2026-11-01 — 부동 레인의 나눗셈(전에는 정수 나눗셈으로 떨어졌다)
                if (ks.k[ks.n-1] == K_FLT)
                    low_fpf(out, "    fs[fsp-2] = lw_fround({}, fs[fsp-2] / fs[fsp-1]); fsp--;\n",
                            PROVEN_ARG((long long)((long long)in->a)));
                else
                    low_fpf(out, "    st[sp-2] = lw_divs_i({}, st[sp-2], st[sp-1]); sp--;\n", PROVEN_ARG((long long)((long long)in->a)));
                ks.n--; break;
            case IRW_MOD:
                if (ks.n >= 2 && ks.k[ks.n-1] == K_VEC && ks.k[ks.n-2] == K_VEC) {   // ★ 2026-11-03 — 레인마다 스칼라 `mod`(종류 실행과 짝 · lw_vmod1 은 태그 경로와 같은 함수)
                    const int ln_ = (int)ks.vn[ks.n-1], ez_ = (int)ks.ve[ks.n-1]; const char *m_ = lw_vmem((unsigned)ez_);
                    low_fpf(out, "    {{ vsp--; for (int k_ = 0; k_ < {}; k_++)\n"
                                 "        vs[vsp-1].{}[k_] = lw_vmod1(vs[vsp-1].{}[k_], vs[vsp].{}[k_], {}, {}); }}\n",
                            PROVEN_ARG((int)(ln_)), proven_arg_cstr(m_), proven_arg_cstr(m_), proven_arg_cstr(m_), PROVEN_ARG((int)(ez_)), PROVEN_ARG((int)(ks.fl[ks.n-1] == CBE_FL_SGN)));
                    ks.n--; ks.o[ks.n-1] = -1;
                    break;
                }
                low_fpf(out, "    st[sp-2] = lw_mods_i({}, st[sp-2], st[sp-1]); sp--;\n", PROVEN_ARG((long long)((long long)in->a))); ks.n--; break;
            case IRW_EQ: case IRW_NE:
                if (ks.k[ks.n-1] == K_VEC) goto veccmp_;   // ★ 2026-11-01 — 벡터의 `eq` · `ne` 도 마스크다(전에는 정수 비교로 떨어져 엉뚱한 칸을 견줬다)
                if (ks.k[ks.n-1] == K_FLT)
                    low_fpf(out, "    st[sp++] = (fs[fsp-2] {} fs[fsp-1]); fsp -= 2;\n",
                            proven_arg_cstr(in->w == IRW_EQ ? "==" : "!="));
                else
                    low_fpf(out, "    st[sp-2] = (st[sp-2] {} st[sp-1]); sp--;\n",
                            proven_arg_cstr(in->w == IRW_EQ ? "==" : "!="));
                ks.n--; ks.k[ks.n-1] = K_INT; break;
            case IRW_LT: case IRW_LE: case IRW_GT: case IRW_GE:
                if (ks.k[ks.n-1] == K_VEC) {
                  veccmp_:;
                    int ln = (int)ks.vn[ks.n-1], ez_ = (int)ks.ve[ks.n-1];
                    const char *c2 = in->w == IRW_LT ? "<" : in->w == IRW_LE ? "<="
                                   : in->w == IRW_GT ? ">" : in->w == IRW_GE ? ">=" : in->w == IRW_EQ ? "==" : "!=";
                    const char *cm = lw_vmem((unsigned)ez_);
                    // ★★ 2026-11-01 (W-20261101-1828) — 레인을 그 표식대로 읽는다(VM 과 같다): 부동이면 비트를 double 로(전에는 비트를 무부호로 견줘
                    //   −1.0 < 0 이 거짓이었다), 부호 있는 레인이면 부호를 늘려서.
                    char la_[96], lb_[96];
                    if (ks.fl[ks.n-1] == CBE_FL_FLT) {
                        low_bfmt(la_, sizeof la_, "lw_b2f(vs[vsp].{}[k_], {})", proven_arg_cstr(cm), PROVEN_ARG((int)(ez_))); low_bfmt(lb_, sizeof lb_, "lw_b2f(vs[vsp+1].{}[k_], {})", proven_arg_cstr(cm), PROVEN_ARG((int)(ez_)));
                    } else if (ks.fl[ks.n-1] == CBE_FL_SGN) {
                        const char *sc_ = ez_ == 1 ? "int8_t" : ez_ == 2 ? "int16_t" : ez_ == 4 ? "int32_t" : "int64_t";
                        low_bfmt(la_, sizeof la_, "(long long)({})vs[vsp].{}[k_]", proven_arg_cstr(sc_), proven_arg_cstr(cm)); low_bfmt(lb_, sizeof lb_, "(long long)({})vs[vsp+1].{}[k_]", proven_arg_cstr(sc_), proven_arg_cstr(cm));
                    } else {
                        low_bfmt(la_, sizeof la_, "vs[vsp].{}[k_]", proven_arg_cstr(cm)); low_bfmt(lb_, sizeof lb_, "vs[vsp+1].{}[k_]", proven_arg_cstr(cm));
                    }
                    low_fpf(out, "    {{ long long m_ = 0; vsp -= 2;\n"
                                 "      for (int k_ = 0; k_ < {}; k_++)"
                                 " if ({} {} {}) m_ |= 1ll << k_;\n"
                                 "      st[sp++] = m_; }}\n", PROVEN_ARG((int)(ln)), proven_arg_cstr(la_), proven_arg_cstr(c2), proven_arg_cstr(lb_));
                    { unsigned char lnn = ks.vn[ks.n-1];
                      ks.n--; ks.k[ks.n-1] = K_MASK; ks.vn[ks.n-1] = lnn; ks.o[ks.n-1] = -1; }
                    break;
                }
                if (ks.k[ks.n-1] == K_FLT)
                    low_fpf(out, "    st[sp++] = (fs[fsp-2] {} fs[fsp-1]); fsp -= 2;\n",
                            proven_arg_cstr(in->w == IRW_LT ? "<" : in->w == IRW_LE ? "<=" : in->w == IRW_GT ? ">" : ">="));
                else
                    low_fpf(out, "    st[sp-2] = lw_cmps_i({}, {}, st[sp-2], st[sp-1]); sp--;\n",
                            PROVEN_ARG((long long)((long long)in->a)),
                            PROVEN_ARG((int)(in->w == IRW_LT ? 2 : in->w == IRW_LE ? 3 : in->w == IRW_GT ? 4 : 5)));
                ks.n--; ks.k[ks.n-1] = K_INT; break;
            case IRW_AND:   low_puts(out, "    st[sp-2] = (st[sp-2] && st[sp-1]); sp--;\n"); ks.n--; break;
            case IRW_OR:    low_puts(out, "    st[sp-2] = (st[sp-2] || st[sp-1]); sp--;\n"); ks.n--; break;
            case IRW_NOT:   low_puts(out, "    st[sp-1] = !st[sp-1];\n"); break;
            case IRW_SELECT:
                if (ks.k[ks.n-1] == K_VEC && ks.k[ks.n-3] == K_MASK) {
                    int ln = (int)ks.vn[ks.n-1];
                    const char *sm = lw_vmem((unsigned)ks.ve[ks.n-1]);
                    low_fpf(out, "    {{ long long m_ = st[--sp]; vsp -= 2;\n"
                                 "      for (int k_ = 0; k_ < {}; k_++)\n"
                                 "        vs[vsp].{}[k_] = (m_ >> k_) & 1 ? vs[vsp].{}[k_] : vs[vsp+1].{}[k_];\n"
                                 "      vsp++; }}\n", PROVEN_ARG((int)(ln)), proven_arg_cstr(sm), proven_arg_cstr(sm), proven_arg_cstr(sm));
                    { unsigned char lnn = ks.vn[ks.n-1], ezz = ks.ve[ks.n-1], vfl_ = ks.fl[ks.n-1];
                      ks.n -= 2; ks.k[ks.n-1] = K_VEC; ks.vn[ks.n-1] = lnn; ks.ve[ks.n-1] = ezz;
                      ks.fl[ks.n-1] = vfl_; ks.o[ks.n-1] = -1; }   // ★ 2026-11-01 — 벡터의 표식(종류 실행과 같다)
                    break;
                }
                low_puts(out, "    st[sp-3] = st[sp-3] ? st[sp-2] : st[sp-1]; sp -= 2;\n"); ks.n -= 2; break;
            case IRW_BR:
                low_fpf(out, "    goto L{};\n", PROVEN_ARG((long long)((long long)in->a)));
                if (!at2[in->a].set) at2[in->a] = ks;
                live2 = false;
                break;
            case IRW_BRZ:
                low_fpf(out, "    if (st[--sp] == 0) goto L{};\n", PROVEN_ARG((long long)((long long)in->a)));
                ks.n--;
                if (!at2[in->a].set) at2[in->a] = ks;
                break;
            case IRW_SWITCH: {   // ★ MM9-B2 빠른 경로 — 점프 테이블 = typed C switch. 뒤 BR 런(span+1)을
                //   소비하고, 각 본문 타깃에 팝된 종류 상태를 전파한다(BR 을 안 찍으므로 직접).
                long long span = in->a;
                if (ks.n > 0) ks.n--;   // switch 값 pop
                low_puts(out, "    switch (st[--sp]) {\n");
                for (long long c = 0; c < span && (proven_size_t)(j + 1 + c) < d->ncode; c++) {
                    proven_i64 tgt = d->code[j + 1 + c].a;
                    low_fpf(out, "      case {}ll: goto L{};\n", PROVEN_ARG((long long)(c)), PROVEN_ARG((long long)((long long)tgt)));
                    if ((proven_size_t)tgt < d->ncode && !at2[tgt].set) at2[tgt] = ks;
                }
                if ((proven_size_t)(j + 1 + span) < d->ncode) {
                    proven_i64 dtgt = d->code[j + 1 + span].a;
                    low_fpf(out, "      default: goto L{};\n", PROVEN_ARG((long long)((long long)dtgt)));
                    if ((proven_size_t)dtgt < d->ncode && !at2[dtgt].set) at2[dtgt] = ks;
                }
                low_puts(out, "    }\n");
                live2 = false;
                j += (proven_size_t)span + 1;   // 테이블 BR 소비
                break;
            }
            case IRW_RET:
                live2 = false;
                cbe_wb(ir, d, out);
                if (ks.n && cbe_optrec_sl(ir, ks.k[ks.n-1], ks.ve[ks.n-1], ks.fl[ks.n-1]))   // ★ 싼 레코드의 슬라이스 칸 → 반환 칸
                    low_puts(out, "    memcpy(lw_ret_rsl, wssl[wsp-1], sizeof lw_ret_rsl); ws[wsp-1].rs = lw_ret_rsl; lw_depth--; return ws[wsp-1];\n");
                else if (ks.n && (ks.k[ks.n-1] == K_OPT || ks.k[ks.n-1] == K_OPTSL || ks.k[ks.n-1] == K_OPT2))
                    low_puts(out, "    lw_depth--; return ws[wsp-1];\n");
                else if (ks.n && ks.k[ks.n-1] == K_FLT)
                    low_puts(out, "    { lw_r r_; r_.v = 0; r_.f = fs[fsp-1]; r_.tag = 5; lw_depth--; return r_; }\n");
                else if (ks.n && ks.k[ks.n-1] == K_W128)   // ★ W-20261105-2304 — 128 비트는 바이트 칸으로(tag 11 · 받는 쪽이 memcpy 로 되읽는다)
                    low_puts(out, "    { lw_r r_ = {0}; lw_u128 w_ = lw_hs[lw_hsp-1]; memcpy(r_.r, &w_, sizeof w_); r_.tag = 11; lw_depth--; return r_; }\n");
                else if (ks.n && ks.k[ks.n-1] == K_INT && cbe_out_w128(d))   // 수 상수를 돌려준다 — 위 절반 0(종류 실행과 짝)
                    low_puts(out, "    { lw_r r_ = {0}; lw_u128 w_ = (lw_u128)(unsigned long long)st[sp-1]; memcpy(r_.r, &w_, sizeof w_); r_.tag = 11; lw_depth--; return r_; }\n");
                else if (ks.n && (ks.k[ks.n-1] == K_SL || ks.k[ks.n-1] == K_ROWS))   // ★ 줄의 나열도 (포인터, 줄 수) — 경계가 줄 모양으로 되짓는다(2026-11-01)
                    low_puts(out, "    { lw_r r_; r_.v = 0; r_.f = 0; r_.s = ss[ssp-1]; r_.tag = 6;"
                          " lw_depth--; return r_; }\n");
                else if (ks.n && ks.k[ks.n-1] >= K_VIEW && ks.k[ks.n-1] < K_SVIEW)
                    // ★ 구조체 뷰 반환 — (포인터,길이)는 r_.s 로, sidx 는 r_.v 로(태그 8).
                    low_fpf(out, "    {{ lw_r r_; r_.v = {}; r_.f = 0; r_.s = ss[ssp-1]; r_.tag = 8;"
                                 " lw_depth--; return r_; }}\n", PROVEN_ARG((int)((int)(ks.k[ks.n-1] - K_VIEW))));
                else if (ks.n && ks.k[ks.n-1] >= K_SVIEW)
                    // ★ 구조체 슬라이스 반환 — (포인터, 원소 수)는 r_.s 로, sidx 는 r_.v 로(태그 9).
                    low_fpf(out, "    {{ lw_r r_; r_.v = {}; r_.f = 0; r_.s = ss[ssp-1]; r_.tag = 9;"
                                 " lw_depth--; return r_; }}\n", PROVEN_ARG((int)((int)(ks.k[ks.n-1] - K_SVIEW))));
                else if (ks.n && ks.k[ks.n-1] >= K_REC && ks.k[ks.n-1] < K_VIEW) {
                    // ★ 반환하는 레코드가 **슬라이스 필드를 들 때만** 그 칸을 가리킨다
                    //   (RFC-0106 단계 4). 아무 레코드에나 `rssl` 을 쓰면 그 이름 때문에
                    //   16 칸이 잡히고, `check-frames` 가 +345% 로 문다 — 실제로 물렸다.
                    //   ☞ *참조하는 것만으로도 값이 든다: 안 쓰는 자리에서 이름을 꺼내지 않는다.*
                    const bool has_sl = cbe_struct_has_sl(ir, ks.k[ks.n-1] - K_REC);
                    // ★★ 2026-10-31 (W-20261031-2053) — 칸을 **스레드마다 하나인 반환 칸**(`lw_ret_rsl`)에 베껴 두고 그것을 가리킨다.
                    //   전에는 이 함수의 `rssl` 을 가리켰다 — 돌아오면 사라지는 자리다. 빠른 경로의 부른 쪽은 그 칸을 아예 안 읽어
                    //   레코드의 슬라이스 칸이 빈 채로 이어졌다(VM 8 · 네이티브 5 — 빠른 함수끼리 이런 레코드를 돌려줄 때, v1.24.0 에도 있었다).
                    //   부른 쪽은 돌아온 **바로 다음에** 제 칸으로 옮기므로(다른 부름보다 먼저) 하나로 넉넉하다.
                    if (has_sl)
                        low_puts(out, "    { lw_r r_ = {0}; memcpy(r_.r, rs[rsp-1], sizeof r_.r); memcpy(lw_ret_rsl, rssl[rsp-1], sizeof lw_ret_rsl);"
                              " r_.rs = lw_ret_rsl; r_.tag = 7; lw_depth--; return r_; }\n");
                    else
                        low_puts(out, "    { lw_r r_ = {0}; memcpy(r_.r, rs[rsp-1], sizeof r_.r); r_.tag = 7;"
                              " lw_depth--; return r_; }\n");
                }
                else
                    if (cbe_ret_scalar(g_cur_di))
                        low_puts(out, "    { lw_depth--; return sp ? st[sp-1] : 0; }\n");
                    else
                        low_puts(out, "    { lw_r r_; r_.v = sp ? st[sp-1] : 0; r_.f = 0; r_.tag = 0; lw_depth--; return r_; }\n");
                ks.n = 0; break;
            case IRW_CALL: {
                const low_ir_def_t *ce = &ir->defs[IR_CALL_IDX(in->a)];
                proven_size_t ni = 0, ns = 0, nfl = 0, nbs = 0, nao = 0, nav = 0;
                for (proven_size_t q = 0; q < ce->nparams; q++) {
                    if ((ce->param_slice >> q) & 1u) ns++;
                    else if ((ce->param_struct >> q) & 1u) { /* ★ 레코드는 `car` 로 간다 */ }
                    else if ((ce->param_bset >> q) & 1u) nbs++;   // ★ 비트셋은 `cab` 로 (참조)
                    else if ((ce->param_flt >> q) & 1u) nfl++;
                    else if ((ce->param_opt >> q) & 1u) nao++;    // ★ option·result 는 `cao` 로
                    else if ((ce->param_vec >> q) & 1u) nav++;    // ★ 벡터는 `cav` 로
                    else if ((ce->param_w128 >> q) & 1u) ni += 2;  // ★ W-20261105-2304 — 128 비트는 정수 칸 둘
                    else ni++;
                }
                // ★★★ **계약은 빠른 경로에서도 계약이다** (RFC-0055 D3).
                if (!(in->a & IR_POL_PROVEN))
                    for (proven_size_t pi = 0; pi < ce->nparams && pi < LOW_MAX_PARAMS; pi++) {
                        if (!ce->prng[pi].has_rng || ((ce->param_slice >> pi) & 1u)) continue;
                        proven_size_t back = 0;
                        for (proven_size_t q = pi + 1; q < ce->nparams; q++)
                            if (!((ce->param_slice >> q) & 1u) && !((ce->param_w128 >> q) & 1u)) back++;   // ★ 128 비트는 `st` 에 없다
                        low_fpf(out,
                            "    if (LW_UNLIKELY(st[sp-{}] < {}ll || st[sp-{}] > {}ll))"
                            " lw_panic(\"argument is outside the parameter's declared range\");\n",
                            PROVEN_ARG((size_t)((size_t)(back + 1))), PROVEN_ARG((long long)((long long)ce->prng[pi].rlo)),
                            PROVEN_ARG((size_t)((size_t)(back + 1))), PROVEN_ARG((long long)((long long)ce->prng[pi].rhi)));
                    }
                proven_size_t nrs = 0;
                for (proven_size_t q = 0; q < ce->nparams; q++)
                    if ((ce->param_struct >> q) & 1u) nrs++;
                proven_size_t cidx0 = IR_CALL_IDX(in->a);
                unsigned cam = (cidx0 < 512) ? g_argmask[cidx0] : LWA_ALL;
                low_puts(out, "    {");
                if (cam & LWA_AI) low_fpf(out, " long long ci[{}]; (void)ci;", PROVEN_ARG((size_t)((size_t)(ni ? ni : 1))));
                if (cam & LWA_AS) low_fpf(out, " lw_sl cs[{}]; (void)cs;", PROVEN_ARG((size_t)((size_t)(ns ? ns : 1))));
                if (cam & LWA_AF) low_fpf(out, " double cf[{}]; (void)cf;", PROVEN_ARG((size_t)((size_t)(nfl ? nfl : 1))));
                if (cam & LWA_AR) low_fpf(out, " unsigned char *car[{}]; (void)car;", PROVEN_ARG((size_t)((size_t)(nrs ? nrs : 1))));
                if (cam & LWA_AB) low_fpf(out, " long long cab[{}]; (void)cab;", PROVEN_ARG((size_t)((size_t)(nbs ? nbs : 1))));
                if (cam & LWA_AO) low_fpf(out, " lw_r cao[{}]; (void)cao;", PROVEN_ARG((size_t)((size_t)(nao ? nao : 1))));
                if (cam & LWA_AV) low_fpf(out, " lw_v cav[{}]; (void)cav;", PROVEN_ARG((size_t)((size_t)(nav ? nav : 1))));
                // ★ 레코드의 슬라이스 칸(RFC-0106) — 호출자도 지어 넘긴다. 레코드 인자를
                //   넘기는 자리는 아래에서 이 칸을 채운다(안 채우면 0 이고, 그 자리는
                //   슬라이스 필드를 안 읽는 op 이다).
                // ★ 2026-10-31 — 칸의 자리는 **매개변수 번호**다(피호출자는 `rsl[q * LW_RECF]` 를 읽고 쓴다 · 경계 어댑터도 q 로 채운다).
                //   전에는 레코드 인자의 차례(br)로 채웠다 — 레코드가 첫 매개변수가 아니면 엇갈렸을 자리다(슬라이스 칸 레코드는 이 길이 막혀 있었다).
                if (cam & LWA_RSL) low_fpf(out, " lw_sl crsl[{} * LW_RECF] = {{{{0,0}}}}; (void)crsl;",
                                           PROVEN_ARG((size_t)((size_t)(ce->nparams ? ce->nparams : 1))));
                // ★★ 2026-11-01 (W-20261101-1150) — 칸이 가리키는 액터에게 말한다: 그 태그 쪽 레코드(`cbv`)를 이 자리의 버퍼(`cbx`)로 풀어 부르고 되쓴다.
                for (proven_size_t q = 0; q < ce->nparams; q++)
                    if (((ce->param_struct >> q) & 1u) && ks.k[ks.n - ce->nparams + q] == K_AREF)
                        low_fpf(out, " _Alignas(8) unsigned char cbx{}_[LW_RECB] = {{0}}; lowv cbv{}_;", PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)q)));
                low_puts(out, "\n");
                { proven_size_t bi = ni, bs = ns, bf = nfl, bb = nbs, bao = nao, bav = nav;
                  for (proven_size_t q = ce->nparams; q-- > 0; ) {
                      if ((ce->param_vec >> q) & 1u) {
                          // ★ 벡터 인자 — vs[] 레인을 그대로 넘긴다. 폭은 **피호출자의 선언**이
                          //   말한다(§9-4: 칸 전체가 아니라 값의 크기만 나른다).
                          low_fpf(out, "      vsp--; cav[{}] = vs[vsp];\n", PROVEN_ARG((size_t)((size_t)(--bav))));
                          continue;
                      }
                      if ((ce->param_opt >> q) & 1u) {
                          // ★ option·result 인자 — ws[] 의 (태그,값) 쌍을 그대로 넘긴다.
                          low_fpf(out, "      cao[{}] = ws[--wsp];\n", PROVEN_ARG((size_t)((size_t)(--bao))));
                          continue;
                      }
                      if ((ce->param_slice >> q) & 1u) {
                          // ★★★ 피호출자는 슬라이스를 **바이트로** 받아 view.array 로 원소 배열을 만든다.
                          //   그런데 우리 ss[]는 이미 view.array 된 **원소 수**일 수 있다 ⇒ 원소폭을
                          //   곱해 **바이트로 되돌린다**(안 하면 피호출자가 또 나눠 트랩한다 —
                          //   `pass_typed → take_typed` 에서 차등 스윕이 잡은 결함).
                          unsigned char ak = ks.k[ks.n - ce->nparams + q];
                          unsigned char sve = ks.ve[ks.n - ce->nparams + q];
                          if (!g_no_elemsl) {
                              // ★ RFC-0109 단계 1 — 피호출자는 **원소 단위**로 받는다. 우리 슬롯이 이미 그 모양이면
                              //   그대로, 아직 바이트(레코드 필드에서 꺼낸 것 따위)면 **여기가 경계**다: 검사하고 나눈다.
                              if (ce->param_selem[q]) {
                                  int sx_ = ce->param_selem[q] - 1, stride_ = (int)ir->structs[sx_].total;
                                  proven_size_t b_ = --bs;   // ★ 인자 평가 순서에 기대지 않는다(-Wsequence-point 가 잡았다, WO-0179)
                                  if (ak == (unsigned char)(K_SVIEW + sx_))
                                      low_fpf(out, "      cs[{}] = ss[--ssp];\n", PROVEN_ARG((size_t)(b_)));
                                  else
                                      low_fpf(out, "      cs[{}] = ss[--ssp]; if (LW_UNLIKELY(cs[{}].n % {}u))"
                                                   " lw_panic(\"view_array: slice length is not a multiple of the element size\"); cs[{}].n /= {}u;\n",
                                              PROVEN_ARG((size_t)(b_)), PROVEN_ARG((size_t)(b_)), PROVEN_ARG((int)(stride_)), PROVEN_ARG((size_t)(b_)), PROVEN_ARG((int)(stride_)));
                              } else {
                                  int w_ = cbe_pslice_w(ce, q); if (w_ <= 0) w_ = 1;
                                  proven_size_t b_ = --bs;
                                  if (w_ <= 1 || sve == (unsigned char)w_)
                                      low_fpf(out, "      cs[{}] = ss[--ssp];\n", PROVEN_ARG((size_t)(b_)));
                                  else
                                      low_fpf(out, "      cs[{}] = ss[--ssp]; if (LW_UNLIKELY(cs[{}].n % {}u))"
                                                   " lw_panic(\"view_array: slice length is not a multiple of the element size\"); cs[{}].n /= {}u;\n",
                                              PROVEN_ARG((size_t)(b_)), PROVEN_ARG((size_t)(b_)), PROVEN_ARG((int)(w_)), PROVEN_ARG((size_t)(b_)), PROVEN_ARG((int)(w_)));
                              }
                              continue;
                          }
                          proven_size_t b0_ = --bs;   // ★ 같은 병(인자 평가 순서) — 옛 규약 자리도 고친다
                          if (ak >= K_SVIEW) {   // ★ 구조체 슬라이스 — stride 로 바이트 복원
                              int stride = (int)ir->structs[ak - K_SVIEW].total;
                              low_fpf(out, "      cs[{}] = ss[--ssp]; cs[{}].n *= {};\n", PROVEN_ARG((size_t)(b0_)), PROVEN_ARG((size_t)(b0_)), PROVEN_ARG((int)(stride)));
                          } else if (sve > 1)
                              low_fpf(out, "      cs[{}] = ss[--ssp]; cs[{}].n *= {};\n", PROVEN_ARG((size_t)(b0_)), PROVEN_ARG((size_t)(b0_)), PROVEN_ARG((unsigned)((unsigned)sve)));
                          else
                              low_fpf(out, "      cs[{}] = ss[--ssp];\n", PROVEN_ARG((size_t)(b0_)));
                      }
                      else if ((ce->param_bset >> q) & 1u) {
                          // ★ 비트셋의 **집**에서 마스크를 싣는다(그리고 호출 뒤 되받는다).
                          int home = (int)ks.o[ks.n - ce->nparams + q];
                          low_fpf(out, "      sp--; cab[{}] = loc[{}];\n", PROVEN_ARG((size_t)((size_t)(--bb))), PROVEN_ARG((int)(home)));
                      }
                      else if ((ce->param_flt >> q) & 1u) low_fpf(out, "      cf[{}] = fs[--fsp];\n", PROVEN_ARG((size_t)((size_t)(--bf))));
                      else if ((ce->param_struct >> q) & 1u) {
                          // ★ 레코드가 그대로 인자가 된다 — 박스도 없다.
                          //   (callee 가 그 필드에 쓰는 op 이면 위에서 이미 거절했다.)
                          proven_size_t br = 0;
                          for (proven_size_t z = 0; z < q; z++) if ((ce->param_struct >> z) & 1u) br++;
                          unsigned char ak = ks.k[ks.n - ce->nparams + q];
                          if (ak == K_AREF) {
                              // ★★ 2026-11-01 — 칸이 가리키는 액터: 비워 둔 손잡이(0)면 VM 처럼 멈춘다(정본 §10.2 (1d)).
                              char v0[32], b0[32];
                              low_bfmt(v0, sizeof v0, "cbv{}_", PROVEN_ARG((size_t)((size_t)q))); low_bfmt(b0, sizeof b0, "cbx{}_", PROVEN_ARG((size_t)((size_t)q)));
                              low_fpf(out, "      {} = lw_aref_out(ss[--ssp]);"
                                           " if (LW_UNLIKELY({}.tag != LWV_REC)) lw_panic(\"`send` on actor handle 0 — this actor was never spawned\");"
                                           " car[{}] = {};\n", proven_arg_cstr(v0), proven_arg_cstr(v0), PROVEN_ARG((size_t)((size_t)br)), proven_arg_cstr(b0));
                              cbe_emit_rec_unpack(out, ir, (int)ce->param_sidx[q], v0, b0, "crsl", (size_t)q, (cam & LWA_RSL) != 0);
                          } else if (ak >= K_VIEW && ak < K_SVIEW) {
                              // ★★★★ **경계가 사라졌다** (2026-08-03 — 표현 통합).
                              //   레코드도 이제 바이트이므로 뷰를 "푸는" 일이 없다: 같은
                              //   레이아웃의 **구간 복사 하나**다. 그전에는 필드마다 읽고,
                              //   부른 뒤 필드마다 되썼다 — reactor 에서 그 코드가 340 B 였다.
                              // ★★★★ **복사가 없다** — 뷰의 바이트를 그대로 가리킨다(단계 2).
                              low_fpf(out, "      car[{}] = (unsigned char *)ss[--ssp].p;\n", PROVEN_ARG((size_t)((size_t)br)));
                          } else
                              // ★★★ 피호출자가 바꾸는 레코드는 **집을 직접 가리켜야** 한다.
                              //   스택의 복사본을 가리키면 그 쓰기를 호출자의 지역이 못 본다 —
                              //   차등 스윕이 즉시 고발했다(`vm_tier.low:both` VM 21 · native 0).
                              //   되쓰기를 없앤 대가로 **무엇을 가리키느냐**가 뜻을 지게 됐다.
                              { int rhome = (int)ks.o[ks.n - ce->nparams + q];
                                // ★★★★★ **슬라이스 칸도 함께 넘긴다** (RFC-0106 단계 4).
                                //   레코드의 바이트만 넘기면 피호출자는 슬라이스 필드를
                                //   **빈 칸**으로 읽는다. 집이 있으면 그 지역 칸에서,
                                //   없으면 스택 칸에서 옮긴다.
                                const bool asl_ = cbe_struct_has_sl(ir, ak - K_REC);
                                if (cbe_pmut(ce) && rhome >= 0) {
                                    low_fpf(out, "      rsp--; car[{}] = rloc[{}];\n", PROVEN_ARG((size_t)((size_t)br)), PROVEN_ARG((int)(rhome)));
                                    if (asl_)
                                        low_fpf(out, "      memcpy(&crsl[{} * LW_RECF], rlocsl[{}], sizeof rlocsl[0]);\n",
                                                PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((int)(rhome)));
                                } else {
                                    low_fpf(out, "      rsp--; car[{}] = rs[rsp];\n", PROVEN_ARG((size_t)((size_t)br)));
                                    if (asl_) {
                                        if (rhome >= 0)
                                            low_fpf(out, "      memcpy(&crsl[{} * LW_RECF], rlocsl[{}], sizeof rlocsl[0]);\n",
                                                    PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((int)(rhome)));
                                        else
                                            low_fpf(out, "      memcpy(&crsl[{} * LW_RECF], rssl[rsp], sizeof rssl[0]);\n",
                                                    PROVEN_ARG((size_t)((size_t)q)));
                                    }
                                } }
                      }
                      else if ((ce->param_w128 >> q) & 1u) {   // ★ W-20261105-2304 — 아래 64 · 위 64 비트(받는 쪽 LW_WOF)
                          bi -= 2;
                          if (ks.k[ks.n - ce->nparams + q] == K_INT)   // 수 인자 — 위 절반 0(종류 실행과 짝)
                              low_fpf(out, "      ci[{}] = st[--sp]; ci[{}] = 0;\n", PROVEN_ARG((size_t)((size_t)bi)), PROVEN_ARG((size_t)((size_t)bi + 1)));
                          else
                          low_fpf(out, "      {{ lw_u128 w_ = lw_hs[--lw_hsp]; ci[{}] = LW_WLO(w_); ci[{}] = LW_WHI(w_); }}\n", PROVEN_ARG((size_t)((size_t)bi)), PROVEN_ARG((size_t)((size_t)bi + 1)));
                      }
                      else                             low_fpf(out, "      ci[{}] = st[--sp];\n",  PROVEN_ARG((size_t)((size_t)(--bi))));
                  } }
                proven_size_t cidx = IR_CALL_IDX(in->a);
                bool cwrapped = (cidx < 512 && (g_retk[cidx] == K_OPT || g_retk[cidx] == K_OPTSL || g_retk[cidx] == K_OPT2));
                bool cflt = (cidx < 512 && g_retk[cidx] == K_FLT);
                bool csl  = (cidx < 512 && (g_retk[cidx] == K_SL || g_retk[cidx] == K_ROWS));   // ★ 줄의 나열도 (포인터, 줄 수)(2026-11-01)
                bool cview = (cidx < 512 && g_retk[cidx] >= K_VIEW && g_retk[cidx] < K_SVIEW);
                bool cstsl = (cidx < 512 && g_retk[cidx] >= K_SVIEW);   // 구조체 슬라이스 반환
                bool crec = (cidx < 512 && g_retk[cidx] >= K_REC && g_retk[cidx] < K_VIEW);
                bool cw128 = (cidx < 512 && g_retk[cidx] == K_W128);   // ★ W-20261105-2304
                bool cscal = cbe_ret_scalar((proven_size_t)cidx);
                // ★ 2026-11-01 — 피호출자가 태그 쪽 액터 레코드를 짓고(g_abox) 수를 돌려주면, 그 레코드는 아무도 못 본다 — VM 처럼(rec_wm)
                //   부른 뒤 풀을 되감는다. 안 그러면 되풀이 안에서 부른 op 이 풀을 다 쓴다(VM 은 되감아 계속 돈다).
                const bool crwm_ = cidx < 512 && g_abox[cidx] && (cscal || cflt || g_retk[cidx] == K_INT);
                if (crwm_) low_puts(out, "      int rwm_ = lw_nrec;\n");
                low_puts(out, cscal ? "      long long crv_ = lw_s_" : "      lw_r cr_ = lw_s_");
                put_sym(out, ce); low_puts(out, "(");
                cbe_put_args(out, cam, true, cbe_depparam((proven_size_t)cidx) ? "lw_depth" : NULL);
                low_puts(out, ");\n");
                if (crwm_) low_puts(out, "      LW_REC_REWIND(rwm_);\n");
                if (cbe_pmut(ce))   // ★ 2026-11-01 — 칸이 가리키는 액터에 쓴 것을 그 태그 쪽 레코드로 되쓴다
                    for (proven_size_t q = 0; q < ce->nparams; q++)
                        if (((ce->param_struct >> q) & 1u) && ks.k[ks.n - ce->nparams + q] == K_AREF) {
                            char v0[32], b0[32];
                            low_bfmt(v0, sizeof v0, "cbv{}_", PROVEN_ARG((size_t)((size_t)q))); low_bfmt(b0, sizeof b0, "cbx{}_", PROVEN_ARG((size_t)((size_t)q)));
                            cbe_emit_rec_writeback(out, ir, (int)ce->param_sidx[q], v0, b0, "crsl", (size_t)q, (cam & LWA_RSL) != 0);
                        }
                // ★★★★ **되쓸 것이 없다** (RFC-0039 §9-4 단계 2, 2026-08-03).
                //   피호출자와 호출자가 **같은 바이트**를 본다 — 인자로 넘긴 것이 주소이기
                //   때문이다. 그전에는 복사해 넣고, 부르고, 되썼다: 그 왕복이 reactor 에서
                //   340 B 였다. 배타성(EXCL)이 별칭을 막으므로 "즉시 보인다" 와 "반환 때
                //   보인다" 는 관측상 같아야 하고, **그것을 차등 스윕이 확인한다.**
                // ★★★ **되받는다** — 피호출자가 세운 비트가 호출자에게 보여야 한다.
                { proven_size_t bw = 0;
                  for (proven_size_t q = 0; q < ce->nparams; q++)
                      if ((ce->param_bset >> q) & 1u) {
                          int home = (int)ks.o[ks.n - ce->nparams + q];
                          low_fpf(out, "      loc[{}] = cab[{}];\n", PROVEN_ARG((int)(home)), PROVEN_ARG((size_t)((size_t)bw++)));
                      } }
                // ★★ 2026-10-31 (W-20261031-2053) — **레코드의 슬라이스 칸도 되받는다.** 바이트는 주소로 넘어가 피호출자의 쓰기가 곧 보이지만,
                //   슬라이스 칸은 `crsl` 에 베껴 넘긴 값이다. 바꾸는 피호출자(액터 핸들러 · mut 매개변수)가 그 칸에 쓴 것을 호출자의 집으로 옮기고,
                //   그 집이 이 op 의 매개변수면 이 op 의 `rsl` 에도 남긴다(그래야 한 겹 위의 호출자도 본다).
                if (cbe_pmut(ce) && (cam & LWA_RSL))
                    for (proven_size_t q = 0; q < ce->nparams && q < LOW_MAX_PARAMS; q++) {
                        if (!((ce->param_struct >> q) & 1u)) continue;
                        const unsigned char ak2 = ks.k[ks.n - ce->nparams + q];
                        const int home2 = (int)ks.o[ks.n - ce->nparams + q];
                        if (ak2 < K_REC || ak2 >= K_VIEW || home2 < 0) continue;
                        if (!cbe_struct_has_sl(ir, ak2 - K_REC)) continue;
                        low_fpf(out, "      memcpy(rlocsl[{}], &crsl[{} * LW_RECF], sizeof rlocsl[0]);\n", PROVEN_ARG((int)(home2)), PROVEN_ARG((size_t)((size_t)q)));
                        if ((proven_size_t)home2 < d->nparams && ((d->param_struct >> home2) & 1u))
                            low_fpf(out, "      memcpy(&rsl[{} * LW_RECF], rlocsl[{}], sizeof rlocsl[0]);\n", PROVEN_ARG((int)(home2)), PROVEN_ARG((int)(home2)));
                    }
                if (cwrapped && cidx < 512 && cbe_optrec_sl(ir, K_OPT, g_retopt[cidx], 0))   // ★ 돌려받은 싼 레코드의 슬라이스 칸
                    low_puts(out, "      if ((cr_.tag == 1 || cr_.tag == 3) && cr_.rs) memcpy(wssl[wsp], cr_.rs, sizeof wssl[0]); else memset(wssl[wsp], 0, sizeof wssl[0]); ws[wsp++] = cr_; }\n");
                else if (cwrapped)   low_puts(out, "      ws[wsp++] = cr_; }\n");
                else if (cflt)  low_puts(out, "      fs[fsp++] = cr_.f; }\n");
                else if (cw128) low_puts(out, "      { lw_u128 w_; memcpy(&w_, cr_.r, sizeof w_); lw_hs[lw_hsp++] = w_; } }\n");
                else if (csl)   low_puts(out, "      ss[ssp++] = cr_.s; }\n");
                else if (cview) low_puts(out, "      ss[ssp++] = cr_.s; }\n");   // ★ 구조체 뷰 반환 = 슬라이스로 받는다
                else if (cstsl) low_puts(out, "      ss[ssp++] = cr_.s; }\n");   // ★ 구조체 슬라이스 반환
                else if (crec) {
                    // ★ 2026-10-31 — 돌려받은 레코드의 슬라이스 칸을 제 칸으로 옮긴다(피호출자가 `lw_ret_rsl` 에 둔 것 — 위 RET).
                    const bool rsl_ = cbe_struct_has_sl(ir, g_retk[cidx] - K_REC);
                    if (rsl_) low_puts(out, "      rs[rsp] = rsb[rsp]; memcpy(rs[rsp], cr_.r, LW_RECB);"
                                    " if (cr_.rs) memcpy(rssl[rsp], cr_.rs, sizeof rssl[0]); else memset(rssl[rsp], 0, sizeof rssl[0]); rsp++; }\n");
                    else low_puts(out, "      rs[rsp] = rsb[rsp]; memcpy(rs[rsp], cr_.r, LW_RECB); rsp++; }\n");
                }
                else if (cscal) low_puts(out, "      st[sp++] = crv_; }\n");
                else            low_puts(out, "      st[sp++] = cr_.v; }\n");
                ks.n -= (int)ce->nparams;
                ks.o[ks.n] = -1;
                if (cview || cstsl) { ks.k[ks.n] = g_retk[cidx]; ks.ve[ks.n] = 0; ks.vn[ks.n] = 0; ks.n++; }
                else {
                    // ★★★ 호출 결과는 스택 잔재를 물려받으면 안 된다 — 시뮬레이터(cbe_kind_run)와
                    //   **동일하게** 정규화한다: ve/vn/fl 을 지우고, option 이 **레코드를 감쌌으면**
                    //   ve=g_retopt(sidx+1) · 반환 슬라이스는 ve=1. 전엔 csl 만 ve=1 을 박아 나머지가
                    //   낡은 부가정보를 물려받았다 — 특히 **K_OPT-레코드 반환을 round-trip 없이 바로
                    //   소비**하면(`field (ok_value (mkres v)) x`) 낡은 ve(0)로 some_value/ok_value 가
                    //   스칼라 추출로 새어 뒤 field 가 rs[-1] 을 읽었다(ASan OOB · VM≠native).
                    ks.ve[ks.n] = 0; ks.vn[ks.n] = 0; ks.fl[ks.n] = 0;
                    if (cwrapped && cidx < 512) ks.ve[ks.n] = g_retopt[cidx];
                    if (cwrapped && cidx < 512 && g_retk[cidx] == K_OPT && g_retnode[cidx] && cidx != (proven_size_t)(d - ir->defs)) { ks.ve[ks.n] = g_retnode[cidx]; ks.fl[ks.n] = 1; }   // ★ 2026-11-03 — 종류 실행과 짝
                    if (cidx < 512 && g_retk[cidx] == K_OPTSL) {   // ★ 슬라이스 option 의 폭 · 표식(2026-11-01)
                        ks.ve[ks.n] = 0; ks.vn[ks.n] = (unsigned char)(g_retve[cidx] > 1 ? g_retve[cidx] : 0); ks.fl[ks.n] = g_retfl[cidx]; }
                    else if (csl && g_retk[cidx] == K_ROWS) { ks.ve[ks.n] = g_retve[cidx]; ks.vn[ks.n] = g_retrb[cidx]; ks.fl[ks.n] = g_retfl[cidx]; }
                    else if (csl) { ks.ve[ks.n] = g_retve[cidx] ? g_retve[cidx] : 1; ks.fl[ks.n] = g_retfl[cidx]; }
                    ks.k[ks.n++] = cwrapped ? g_retk[cidx] : cflt ? K_FLT : cw128 ? K_W128 : csl ? g_retk[cidx]
                             : crec ? g_retk[cidx] : K_INT;
                }
                break;
            }
            default: low_puts(out, "    lw_panic(\"fast path: unreachable word\");\n"); break;
        }
    }
    cbe_wb(ir, d, out);
    if (cbe_ret_scalar(g_cur_di)) low_puts(out, "    { lw_depth--; return 0; }\n");
    else low_puts(out, "    { lw_r r_; r_.v = 0; r_.f = 0; r_.tag = 0; lw_depth--; return r_; }\n");
    cbe_line_end(out);
    low_free(target);
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **C ABI / FFI** (RFC-0063) — 이 언어를 **섬으로 두지 않는다.**
//
//   `extern` op 은 **몸이 C 에 있다.** 우리는 C 안을 못 본다 — 그러면 무엇을 하나?
//
//   ★★ **C 가 넘나드는 문을 지킨다.** `requires` 는 **부르기 전에**, `ensures` 는 **돌아온 뒤에**
//     검사된다(그 검사는 IR 이 이미 낸다 — 계약은 계약이다). 그래서 이 언어의 FFI 는
//     **계약을 가진 FFI** 다. 보통의 언어는 여기서 포기한다("unsafe 니까").
//
//   ★ 타입 사상은 **좁고 정직하다**: 정수 · f64 · `slice u8` → **포인터 + 길이**(인자 둘).
//     C 에는 슬라이스가 없다. **없는 것을 있는 척하지 않는다.**
// ★★ RFC-0054 §8-9 (2026-10-25 소유자 «다 추천대로») — **정수 인자도 선언 폭으로**(돌려주는 값과 같은 규율). 전엔 늘 `long long` 이라
//   `uint8_t` · `int` 를 받는 C 함수와 원형이 어긋났다 — x86-64 에서는 레지스터의 아래 절반이 우연히 맞고, AVR 에서는 자리가 틀린다.
//   8 · 16 · 32 비트 정수만 좁힌다(지역의 선언 폭 loc_meta — 매개변수는 첫 지역들이다). 64 비트 · bool · 열거는 그대로다.
static const char *cbe_ctype(const low_ir_def_t *d, proven_size_t q) {
    if ((d->param_flt >> q) & 1u) return "double";
    const proven_u16 m = q < sizeof d->loc_meta / sizeof d->loc_meta[0] ? d->loc_meta[q] : 0;
    if (m & IR_TY_KNOWN) {
        const bool sg = (m & IR_TY_SIGNED) != 0; const unsigned b = m & 0xff;
        if (b == 8)  return sg ? "int8_t" : "uint8_t";
        if (b == 16) return sg ? "int16_t" : "uint16_t";
        if (b == 32) return sg ? "int32_t" : "uint32_t";
    }
    return "long long";
}

static void cbe_extern_proto(const low_ir_t *ir, const low_ir_def_t *d, low_out_t *out) {
    (void)ir;
    low_puts(out, "extern ");
    // ★★★ **반환 타입을 폭으로 정직하게** — f64→double · cstr→`const char *` · 정수는 선언된 폭
    //   그대로(i32→int …). long long 으로 뭉뚱그리면 `int printf` 같은 실제 libc 심볼과 충돌해
    //   **직접 못 부른다**(f64→double fix 와 같은 규율 · cbe_ret_ctype).
    low_puts(out, cbe_ret_ctype(d));
    low_write(out, d->link_name.ptr, d->link_name.size);
    low_putc(out, '(');
    proven_size_t n = 0;
    for (proven_size_t q = 0; q < d->nparams; q++) {
        // ★★★ **능력은 C 로 안 넘어간다.** 그것은 **권리**이지 값이 아니다 — C 에는 그것을
        //   뜻하는 것이 없다. (FFI 를 짓다가 처음으로 그 질문을 하게 됐다.)
        if ((d->param_cap >> q) & 1u) continue;
        if ((d->param_uptr >> q) & 1u) {   // ★ `unsafe_ptr` — 구조체 바이트의 단일 생 포인터(길이 없음)
            low_fpf(out, "{}void *", proven_arg_cstr(n++ ? ", " : ""));   // void* 는 어떤 struct* 와도 ABI 호환
            continue;
        }
        if ((d->param_ufn >> q) & 1u) {    // ★ `unsafe_fn` — 콜백(함수 포인터). void* 로(어떤 fn* 와도 ABI 호환)
            low_fpf(out, "{}void *", proven_arg_cstr(n++ ? ", " : ""));
            continue;
        }
        if ((d->param_struct >> q) & 1u) {   // ★ 구조체 **by-value** — C 컴파일러가 SysV 분류
            low_fpf(out, "{}struct lw_sty_{}", proven_arg_cstr(n++ ? ", " : ""), PROVEN_ARG((unsigned)((unsigned)d->param_sidx[q])));
            continue;
        }
        if ((d->param_cstr >> q) & 1u) {   // ★ cstr — 널종단 char* 단일 포인터(길이 없음), RFC-0068 S4 · 안에서는 슬라이스 칸(RFC-0137 P3)
            low_fpf(out, "{}const char *", proven_arg_cstr(n++ ? ", " : ""));
            continue;
        }
        if ((d->param_slice >> q) & 1u) {
            // ★ 쓸 수 있는 슬라이스(`mut slice t`)는 `const` 없이 나간다 — C 가 그 칸에 쓴다(2026-10-11, extern-mut-slice-const).
            low_fpf(out, "{}{}{} *, size_t", proven_arg_cstr(n++ ? ", " : ""), proven_arg_cstr(((d->param_mutw >> q) & 1u) ? "" : "const "),
                    proven_arg_cstr(cbe_slice_ctype(d->param_ebits[q], d->param_selem[q])));   // ★ 폭으로 정직한 포인터 타입
            continue;
        }
        low_fpf(out, "{}{}", proven_arg_cstr(n++ ? ", " : ""), proven_arg_cstr(cbe_ctype(d, q)));
    }
    if (d->is_variadic) low_puts(out, n ? ", ..." : "...");   // ★ 가변인자 C 함수 (RFC-0063 §5)
    else if (!n) low_puts(out, "void");
    low_puts(out, ");\n");
}

// ★★★ **`lowent.h`** — C 가 우리를 부르려면 **선언이 필요하다**(RFC-0063 D3).
//   그 헤더를 **도구가 낸다**: 손으로 적으면 그것이 곧 **두 번째 표현**이고, 갈린다(교훈 7).
// ★★★ **C 로 나갈 수 있는 이름인가.** `rect.area` 는 **C 식별자가 아니다**(점).
//   그리고 이름을 정제해서 내보내면 그것이 곧 **두 번째 이름**이다 — 갈린다.
//   ⇒ **평범한 이름만** 내보낸다. 나머지는 `export extern` 을 쓰라고 하면 된다.
static bool cbe_c_name_ok(proven_u8str_view_t n) {
    if (!n.size) return false;
    for (proven_size_t i = 0; i < n.size; i++) {
        proven_byte_t c = n.ptr[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
                  (i && c >= '0' && c <= '9');
        if (!ok) return false;
    }
    return true;
}

// ★★★ **판정은 한 번 계산되고 여러 번 읽힌다** (RFC-0089 R1 · 단계 C, 2026-08-03).
//
//   ☞ 그 전에는 *"이 export op 이 실제로 C 심볼을 내는가"* 가 **세 곳에서 각자** 재구성됐다:
//     `--emit-h`(헤더) · export 래퍼 방출 · 그 래퍼의 전방 선언. 헤더 쪽은 `sc_tab`·`g_retk`
//     (= 하강 여부·반환 종류)를 **볼 수가 없어서** 조건을 두 벌로 적었고, 갈렸다:
//     실측 — `export fn mk … output slice u32` 에 대해 헤더는 `long long mk(…)` 를 약속하는데
//     방출된 C 에는 `mk` 가 **없다**. 믿고 링크하면 undefined reference 다(리뷰 B-01).
//   ⇒ 여기서 **한 번** 계산해 `low_ir_plan_t` 에 적고, 세 자리는 그것을 **읽는다**.
static bool pure_tab[512], sc_tab[512];
static low_ir_plan_t g_plan[512];
static char          g_plan_why[512][400];   // ★ 2026-10-31 — 160 바이트가 한글 까닭을 글자 가운데에서 잘라 --diag-json 이 UTF-8 이 아닌 줄을 냈다
static const low_ir_t *g_planned_for;   // 같은 IR 이면 다시 안 센다(계산은 부작용이 크다)
// ★ C 이름이 겹쳤는가 — 계획이 판정하고, 방출 시작점이 그것을 읽어 **멈춘다**(단계 W).
static bool g_abi_dup;
static char g_abi_dup_msg[320];

static void cbe_hdr_structs(low_out_t *out, const low_ir_t *ir, int phase);
// ★ RFC-0132 P4d — 이 구조체가 C 구조체(`struct lw_sty_N`)로 나가는가: 방출이 그것을 내는 조건과 같다.
static bool cbe_sty_ok(const low_ir_t *ir, proven_size_t s) {
    if (!(s < ir->nstructs && ir->structs[s].viewable && !ir->structs[s].is_shadow && ir->structs[s].total <= K_REC_BYTES &&
          !ir->structs[s].is_mmio && !ir->structs[s].is_actor_state)) return false;
    // ★ RFC-0151 §9.4.1 — 128 비트 칸은 C 경계를 넘지 않는다(소유자 — 열지 않는다): 그런 구조체는 C 구조체로 내보내지 않는다
    for (proven_size_t q = 0; q < ir->structs[s].nf; q++) if (ir->structs[s].f[q].size == 16 && !ir->structs[s].f[q].arrn) return false;
    return true;
}
// ★ RFC-0132 P4d — 내보내는 C 심볼의 서명은 **한 곳**에서 적는다. 헤더 · 앞 선언 · 정의가 이것을 부른다(전엔 세 자리가 따로 적었다).
//   named = 정의(매개변수 이름을 붙인다).
static void cbe_put_export_sig(low_out_t *out, const low_ir_def_t *d, const low_ir_plan_t *pl, bool named) {
    if (pl->ret_sty1) low_fpf(out, "struct lw_sty_{} ", PROVEN_ARG((int)(pl->ret_sty1 - 1)));
    else low_puts(out, pl->ret_flt ? "double " : "long long ");
    put_export_name(out, d);
    low_putc(out, '(');
    proven_size_t n = 0;
    for (proven_size_t q = 0; q < d->nparams; q++) {
        if ((d->param_cap >> q) & 1u) continue;   // 능력은 C 에 없다
        const char *sep = n++ ? ", " : "";
        if ((d->param_slice >> q) & 1u) {
            if (named) low_fpf(out, "{}const unsigned char *p{}, size_t n{}", proven_arg_cstr(sep), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)q)));
            else low_fpf(out, "{}const unsigned char *, size_t", proven_arg_cstr(sep));
        } else if ((d->param_flt >> q) & 1u) {
            if (named) low_fpf(out, "{}double x{}", proven_arg_cstr(sep), PROVEN_ARG((size_t)((size_t)q))); else low_fpf(out, "{}double", proven_arg_cstr(sep));
        } else if ((d->param_struct >> q) & 1u) {
            if (named) low_fpf(out, "{}struct lw_sty_{} s{}", proven_arg_cstr(sep), PROVEN_ARG((unsigned)((unsigned)d->param_sidx[q])), PROVEN_ARG((size_t)((size_t)q)));
            else low_fpf(out, "{}struct lw_sty_{}", proven_arg_cstr(sep), PROVEN_ARG((unsigned)((unsigned)d->param_sidx[q])));
        } else {   // ★ RFC-0054 §8-9 — 선언 폭(cbe_ctype)
            if (named) low_fpf(out, "{}{} x{}", proven_arg_cstr(sep), proven_arg_cstr(cbe_ctype(d, q)), PROVEN_ARG((size_t)((size_t)q))); else low_fpf(out, "{}{}", proven_arg_cstr(sep), proven_arg_cstr(cbe_ctype(d, q)));
        }
    }
    if (!n) low_puts(out, "void");
    low_putc(out, ')');
}
void low_cbe_plan(const low_ir_t *ir) {
    g_ir_for_inline = ir;
    if (g_planned_for == ir) return;
    for (proven_size_t i = 0; i < 512; i++) {
        pure_tab[i] = false; sc_tab[i] = false;
        g_plan[i] = (low_ir_plan_t){ 0 }; g_plan_why[i][0] = 0;
    }
    cbe_mark_pure(ir, pure_tab);
    // ★★★ **변형(pmut) 전이 폐포를 먼저** — 종류 시뮬레이터의 자격 판정이 cbe_pmut 을 읽는다.
    cbe_mark_pmut(ir); cbe_mark_upanic(ir); cbe_mark_mdel(ir);
    cbe_mark_aref(ir);             // ★ 2026-11-01 — 액터를 가리키는 칸(담기 · 칸을 거친 말 · 풀어 든 사본) — 자격 판정이 읽는다
    cbe_mark_scalar(ir, sc_tab);   // ★ g_retk(반환 종류)도 여기서 채워진다
    cbe_mark_vbytes(ir);           // ★ 이 유닛이 실제로 쓰는 최대 벡터 폭 — 칸 크기를 정한다
    cbe_mark_pools(ir, sc_tab);    // ★ 전부 하강한 유닛은 레코드·박스 풀이 필요 없다(가드가 지킨다)
    for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        low_ir_plan_t *pl = &g_plan[i];
        pl->valid     = true;
        pl->lowered   = sc_tab[i];
        pl->emittable = cbe_emittable(d);
        pl->ret_flt   = (g_retk[i] == K_FLT);
        // **외부 심볼이 실제로 나오는가** — 이 한 줄이 곧 헤더가 약속해도 되는 조건이다.
        const char *why = NULL;
        if (!d->is_export || d->is_extern)   why = NULL;              // 애초에 내보내는 op 이 아니다
        else if (!pl->emittable)             why = d->lowered ? "본문이 C 백엔드에 담기엔 크다(ncode > 4096)"
                                                             : "본문이 S5 코어 밖이라 하강되지 않았다";
        // ★ RFC-0134: 점 이름(`rect.area`)도 경로 이름을 갖는다. 남는 것은 `link` 이름이 C 식별자가 아닌 경우와
        //   단형화 인스턴스(`op#T` — 틀의 이름으로 내보내지 않는다)뿐이다.
        else if (d->link_name.size && !cbe_c_name_ok(d->link_name))
                                             why = "`link` 이름이 C 식별자가 아니다";
        else if (memchr(d->name.ptr, '#', d->name.size))
                                             why = "단형화 인스턴스다 — 틀의 이름으로는 C 심볼을 내지 않는다";
        // ★★★ WO-0222 — `main` 은 C 의 진입점이 이미 쓰는 이름이다. 진입 op 이 **빠른 경로로 내려가면** 그 C 심볼이
        //   `long long main(void)` 로 나가 런처의 `int main(int, char **)` 와 부딪혔다(태그 경로 main 은 심볼을 안 내서 가려져 있었다).
        //   진입 op 은 런처가 이름표로 부른다 — C 로 따로 내보낼 이유가 없다.
        else if (d->name.size == 4 && memcmp(d->name.ptr, "main", 4) == 0)
                                             why = "이름이 C 의 `main` 과 겹친다 — 진입 op 은 런처가 부른다";
        else if (!sc_tab[i])                 why = "타입 하강이 안 됐다 — 태그 경로 op 은 C 심볼을 내지 않는다(`--why-slow` 가 그 이유를 말한다)";
        else if (d->param_w128 || cbe_out_w128(d))   // ★ W-20261105-2304 — 128 비트가 빠른 경로에 선 뒤에도 C 경계는 넘지 않는다
                                             why = "입력이나 결과가 128 비트 정수다 — C 경계를 넘지 않는다(RFC-0151 §9.4.1)";
        else if (g_retk[i] != K_INT && g_retk[i] != K_FLT && !(g_retk[i] >= K_REC && g_retk[i] < K_VIEW && cbe_sty_ok(ir, (proven_size_t)(g_retk[i] - K_REC))))
                                             why = "반환이 정수 · 부동 · C 구조체가 아니다 — C 경계로 낼 모양이 아직 없다(슬라이스 · option 반환, 바이트 배치가 없는 구조체)";
        // ★★★ RFC-0132 P4d (2026-10-24) — 매개변수도 C 로 낼 모양이 있어야 한다. 전엔 구조체 · 벡터 · option · 비트셋 매개변수가
        //   C 서명에 `long long` 으로 나갔고, 감싸개는 그 값을 버린 채 0 으로 채운 칸에서 읽었다 — C 에서 무엇을 넘기든 답이 같았다
        //   (조용히 틀린 답). 이제 구조체는 `struct lw_sty_N` 으로 받고, 나머지는 심볼을 내지 않고 까닭을 말한다.
        if (!why && d->is_export && !d->is_extern)
            for (proven_size_t q = 0; q < d->nparams && !why; q++) {
                if ((d->param_cap >> q) & 1u) continue;
                if (((d->param_vec >> q) & 1u) || ((d->param_opt >> q) & 1u) || ((d->param_bset >> q) & 1u))
                    why = "매개변수가 벡터 · option · 비트셋이다 — C 경계로 낼 모양이 아직 없다";
                else if (((d->param_struct >> q) & 1u) && !cbe_sty_ok(ir, d->param_sidx[q]))
                    why = "구조체 매개변수에 C 구조체(바이트 배치)가 없다 — 슬라이스 · owned 칸을 가진 구조체";
            }
        pl->ret_sty1 = (!why && g_retk[i] >= K_REC && g_retk[i] < K_VIEW) ? (int)(g_retk[i] - K_REC) + 1 : 0;
        pl->exported_symbol = (d->is_export && !d->is_extern && why == NULL);
        if (why && d->is_export && !d->is_extern) {
            low_bfmt(g_plan_why[i], sizeof g_plan_why[0], "{}", proven_arg_cstr(why));
            {   // 넘쳐 잘렸으면 글자 경계까지 물린다(이어지는 바이트 10xxxxxx 를 남기지 않는다)
                size_t L_ = strlen(g_plan_why[i]);
                if (L_ + 1 >= sizeof g_plan_why[0]) {
                    while (L_ && ((unsigned char)g_plan_why[i][L_ - 1] & 0xC0) == 0x80) L_--;
                    if (L_ && (unsigned char)g_plan_why[i][L_ - 1] >= 0xC0) L_--;
                    g_plan_why[i][L_] = 0;
                }
            }
            pl->why = g_plan_why[i];
        }
    }
    // ★★★ **C 이름이 겹치면 거절한다 — 도구는 이름을 발명하지 않는다** (단계 W).
    //   C 의 이름공간은 평평하다: 한 심볼이 두 op 을 뜻할 수 없다. 단계 U ③ 이 다른 모듈의
    //   같은 이름을 허용했으므로 이 자리는 **실제로 일어난다**(`qual_a.pick` · `qual_b.pick`).
    //   ⇒ 저자가 `link "…" .` 로 가르거나(RFC-0063), 아니면 여기서 멈춘다.
    //   ☞ 조용히 지어내면 그 이름은 **아무도 약속한 적 없는 공개 이름**이 된다.
    for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++) {
        if (!g_plan[i].exported_symbol) continue;
        // ★ RFC-0134: **최종 C 이름**으로 댄다. 경로 이름끼리는 한 이름 통(X-0079) 덕에 겹칠 수 없고,
        //   겹침은 `link` 이름끼리나 `link` 이름과 경로 이름 사이에서만 난다.
        char cbi[512]; proven_size_t li = cbe_export_cname(&ir->defs[i], cbi, sizeof cbi);
        proven_u8str_view_t ni = { .ptr = (const proven_u8 *)cbi, .size = li };
        for (proven_size_t j = i + 1; j < ir->ndefs && j < 512; j++) {
            if (!g_plan[j].exported_symbol) continue;
            char cbj[512]; proven_size_t lj = cbe_export_cname(&ir->defs[j], cbj, sizeof cbj);
            proven_u8str_view_t nj = { .ptr = (const proven_u8 *)cbj, .size = lj };
            if (!proven_u8str_view_eq(ni, nj)) continue;
            g_abi_dup = true;
            low_bfmt(g_abi_dup_msg, sizeof g_abi_dup_msg,
                     "two exports would be the SAME C symbol `{}` (from modules `{}` and "
                     "`{}`) — C's namespace is FLAT, and the tool will not invent a name for "
                     "you: a C symbol is a promise to another language. Give one an explicit "
                     "name: `link \"…\" .`",
                     low_sv((const char *)ni.ptr, (int)ni.size),
                     low_sv((const char *)ir->defs[i].owner_mod.ptr, (int)ir->defs[i].owner_mod.size),
                     low_sv((const char *)ir->defs[j].owner_mod.ptr, (int)ir->defs[j].owner_mod.size));
            break;
        }
        if (g_abi_dup) break;
    }
    g_planned_for = ir;
}
// ★ 읽는 쪽 — **없는 판정은 없다고 말한다**(범위 밖 def 은 심볼을 안 낸다).
const low_ir_plan_t *low_cbe_plan_of(proven_size_t di) {
    static const low_ir_plan_t none = { 0 };
    return di < 512 ? &g_plan[di] : &none;
}
// ★★★ **구조체 by-value 경계가 쓰는 struct 의 C 정의를 헤더에 낸다** (2026-07-23 · RFC-0132 P4d 2026-10-24 에 내보내는 쪽을 더했다).
//   extern 이 받는 구조체와, 내보내는 C 심볼이 받거나 돌려주는 구조체. 헤더의 **내보내는 선언보다 먼저** 낸다(전엔 뒤에 있어 쓸 자리에서
//   타입이 없었다). 생성 C 의 struct 정의와 **같은 폭·순서**다 — 두 번역단위가 같은 `struct lw_sty_N` 을 보므로 갈리면 컴파일 에러다.
//   phase 0 = 내보내는 심볼이 쓰는 구조체(그 선언보다 먼저) · phase 1 = extern 만 쓰는 구조체(원래 자리 — 기준선 헤더의 글이 그대로다).
static bool cbe_hdr_struct_used(const low_ir_t *ir, proven_size_t s, bool exports) {
    for (proven_size_t i = 0; i < ir->ndefs; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        const low_ir_plan_t *pl = low_cbe_plan_of(i);
        bool on = exports ? pl->exported_symbol : (d->is_extern && d->param_struct && cbe_c_name_ok(d->link_name));
        if (!on) continue;
        if (exports && pl->ret_sty1 == (int)s + 1) return true;
        for (proven_size_t q = 0; q < d->nparams; q++)
            if (((d->param_struct >> q) & 1u) && d->param_sidx[q] == s) return true;
    }
    return false;
}
static void cbe_hdr_structs(low_out_t *out, const low_ir_t *ir, int phase) {
    for (proven_size_t s = 0; s < ir->nstructs; s++) {
        bool ex = cbe_hdr_struct_used(ir, s, true);
        bool used = phase == 0 ? ex : (!ex && cbe_hdr_struct_used(ir, s, false));
        if (!used) continue;
        low_fpf(out, "struct lw_sty_{} {{ ", PROVEN_ARG((size_t)(s)));
        for (proven_size_t i = 0; i < ir->structs[s].nf; i++) {
            const low_ir_sfield_t *fi_ = &ir->structs[s].f[i];
            proven_u8 sz = fi_->arrn ? fi_->arresz : fi_->size;   // ★ T2b-3d ⓓ — 배열 칸은 C 배열(`int8_t f1[4];`)
            bool fl_ = fi_->arrn ? (fi_->arrmeta & IR_FLT_BIT) != 0 : fi_->flt;
            const char *ct = fl_ ? (sz == 4 ? "float" : "double")
                           : sz == 1 ? "int8_t" : sz == 2 ? "int16_t" : sz == 4 ? "int32_t" : "int64_t";
            if (fi_->arrn) low_fpf(out, "{} f{}[{}]; ", proven_arg_cstr(ct), PROVEN_ARG((size_t)(i)), PROVEN_ARG((unsigned)((unsigned)fi_->arrn)));
            else low_fpf(out, "{} f{}; ", proven_arg_cstr(ct), PROVEN_ARG((size_t)(i)));
        }
        low_puts(out, "};\n");
    }
}

int low_cbe_emit_header(const low_ir_t *ir, low_out_t *out) {
    int n = 0, nx = 0, skipped = 0;
    // ★★★ **헤더도 같은 판정을 쓴다** (RFC-0089 R1) — 그러려면 그 판정이 여기서도 **계산돼 있어야**
    //   한다. 전에는 헤더 경로가 하강 분석을 아예 안 돌려서 `sc_tab`·`g_retk` 를 **볼 수가 없었고**,
    //   그래서 조건을 두 벌로 적게 됐다. 계산은 한 번이고(같은 IR 이면 재사용), 독자는 셋이다.
    low_cbe_plan(ir);
    // ★★★ **헤더가 자기 눈금을 적는다** (호환성 epoch · 2026-08-05).
    //   이 종이를 받아 가는 사람은 우리 저장소를 안 읽는다 — 읽는 것은 **이 파일뿐**이다.
    //   그러니 *"이것이 어느 원점의 표면이고, 그 원점이 무엇을 약속하는가"* 를 여기 적는다.
    //   ☞ 그리고 이 줄은 기준선(`docs/abi-surface.h`)에도 그대로 들어가므로, epoch 를 옮기면
    //     `check-abi` 가 **표면 diff 로** 그것을 본다 — 눈금은 조용히 움직일 수 없다.
    low_puts(out, "/* generated by lowentc --emit-h — do not edit.\n"
          "   ★ 이 헤더는 **도구가 낸다**: 손으로 적으면 시그니처가 두 곳에 살고, 갈린다.\n"
          "\n"
          "   lowent ABI epoch: " LOW_ABI_EPOCH " (" LOW_ABI_EPOCH_STATUS ")\n"
          "     PROMISED:  this surface is recorded and gated — it cannot change SILENTLY.\n"
          "     NOT YET:   that it will not change. The language is still in development,\n"
          "                so the surface MAY still move; it will be ANNOUNCED when it does.\n"
          "     ☞ an epoch is the ORIGIN you measure compatibility from — hence a date. */\n");
    // ★ RFC-0134 N2 — 헤더 가드도 모듈마다: `LW_<길이><모듈>_H`. 전엔 모두 `LOWENT_H` 라 두 Lowent 헤더를 함께
    //   넣으면 둘째가 통째로 사라졌다. 모듈은 내보내는 op 의 첫 모듈(없으면 첫 def 의 모듈)이다.
    {
        proven_u8str_view_t gm = { 0 };
        for (proven_size_t i = 0; i < ir->ndefs && !gm.size; i++)
            if (low_cbe_plan_of(i)->exported_symbol) gm = ir->defs[i].owner_mod;
        for (proven_size_t i = 0; i < ir->ndefs && !gm.size; i++) gm = ir->defs[i].owner_mod;
        if (gm.size) low_fpf(out, "#ifndef LW_{}{}_H\n#define LW_{}{}_H\n", PROVEN_ARG((size_t)((size_t)gm.size)), low_sv((const char *)gm.ptr, (int)gm.size), PROVEN_ARG((size_t)((size_t)gm.size)), low_sv((const char *)gm.ptr, (int)gm.size));
        else low_puts(out, "#ifndef LW_H\n#define LW_H\n");
    }
    low_puts(out, "#include <stddef.h>\n#include <stdint.h>\n"
          "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
    cbe_hdr_structs(out, ir, 0);   // ★ RFC-0132 P4d — 내보내는 심볼의 구조체는 그 선언보다 먼저
    for (proven_size_t i = 0; i < ir->ndefs; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        // ★★★ **헤더와 오브젝트는 같은 판별자를 써야 한다** (2026-08-02 실측 수리).
        //   여기는 `is_export && !is_extern` 만 봤고, **래퍼를 실제로 내는 자리**(아래 6257 근처)는
        //   거기에 `cbe_emittable(d)` 를 **하나 더** 걸고 있었다. 두 조건이 갈리면 헤더가
        //   **오브젝트에 없는 심볼을 약속한다** — 믿고 링크하면 실패한다.
        //   ☞ 실측: `blink.low` 의 `export proc start`(cap 을 받는다)에 대해
        //     `--emit-h` 는 `long long start(void);` 를 냈는데 `--emit-c` 는 정의를 안 냈다.
        //   ⇒ 같은 술어를 쓴다. 헤더가 약속하는 것과 오브젝트가 주는 것은 **한 뜻**이어야 한다.
        // ★★★ **헤더는 판정을 읽는다 — 다시 세지 않는다** (RFC-0089 R1 · 단계 C).
        //   그 전에는 여기서 `is_export && !is_extern && cbe_emittable` 만 봤고, 래퍼를 실제로
        //   내는 자리는 거기에 **하강 여부와 반환 종류를 더** 걸었다 ⇒ 헤더가 오브젝트에 없는
        //   심볼을 약속했다. 이제 **한 기록**이 그 판정이고, 세 자리가 그것을 읽는다.
        const low_ir_plan_t *pl = low_cbe_plan_of(i);
        if (!pl->exported_symbol) {
            // ☞ **조용히 빠지지 않는다**: export 라고 적었는데 심볼이 안 나오면 그 사실과 이유를 말한다.
            if (d->is_export && !d->is_extern && pl->why) {
                low_puts(out, "/* not exported: ");
                low_write(out, d->name.ptr, d->name.size);
                low_fpf(out, " \u2014 {} */\n", proven_arg_cstr(pl->why));
                low_puts(low_stderr(), "lowentc: W-EXPORT-NOSYM: op '");
                low_write(low_stderr(), d->name.ptr, d->name.size);
                low_fpf(low_stderr(), "' is `export` but NO C symbol is emitted for it \u2014 {}. "
                                "The header therefore does NOT declare it: a header that promises a "
                                "symbol the object does not define fails at LINK time\n", proven_arg_cstr(pl->why));
                skipped++;
            }
            continue;
        }
        // ★★★ float 반환은 **double** 로 선언한다 — 전엔 언제나 `long long` 이라, `output f64` 인
        //   export op 이 헤더엔 `long long`(정수 레지스터)·정의엔 `double`(부동 레지스터)로 갈렸다.
        //   헤더를 믿는 C 소비자(콜백 등록 포함)가 정수 레지스터에서 반환을 읽어 쓰레기를 냈다.
        //   래퍼(g_retk==K_FLT?double)·정의는 정직했는데 헤더만 빠졌다 — 시그니처가 두 곳에 살고 갈렸다.
        cbe_put_export_sig(out, d, pl, false);   // ★ RFC-0132 P4d — 서명은 한 곳에서
        low_puts(out, ";\n");
        n++;
    }

    // ★★★ **그리고 `extern` 도 낸다 — 이 헤더가 존재하는 이유가 바로 여기다** (2026-07-20).
    //
    //   ☞ **전엔 `extern` 을 헤더에서 빼고 있었다**(`!d->is_export || d->is_extern` 로 걸렀다).
    //     그래서 C 쪽 정의를 쓰는 사람에게는 **포함할 것이 없었고**, 시그니처가 갈려도
    //     아무도 몰랐다. 실측(2026-07-20): `lw_c_add(long long, long long)` 을 인자 **하나**로
    //     선언하니 `check: ok` + `cc` 경고 0 + **실행마다 달라지는 쓰레기 값**이 나왔다.
    //     ⇒ 이 저장소에서 **조용히 틀린 답이 나오는 유일한 자리**였다.
    //
    //   ★★★ 그리고 이 함수의 머리 주석이 그 원칙을 **이미** 적어 두고 있었다:
    //     *"손으로 적으면 시그니처가 두 곳에 살고, 갈린다"*. 정확히 그 일이 났다 —
    //     원칙은 있었고, **물리는 방향에만 적용을 안 했다.** (교훈 7 이 자기 자리에서 물렸다.)
    //
    //   ⇒ 이제 C 쪽 정의 파일이 이 헤더를 **include 하면** 불일치가 **컴파일 에러**가 된다.
    //     ☞ 정직히: **include 를 강제할 수는 없다.** C 는 번역 단위를 건너 검사하지 않는다.
    //       도구가 할 수 있는 것은 **검사받을 수단을 주는 것**까지이고, 그것을 여기서 한다.
    //       (libc 처럼 표준 헤더에 이미 있는 심볼은 생성 C 가 그 헤더를 포함하므로 **이미** 잡힌다.)
    // ★★★ **구조체 by-value extern 이 쓰는 struct 의 C 정의를 헤더에 낸다** (2026-07-23).
    //   ☞ 전엔 이 정의가 없어(생성 C 에만 있었다) struct by-value extern 을 통째로 건너뛰고
    //     "이 경계는 검사받지 못한다"고 주석만 달았다. 그런데 **viewable 구조체는 필드가 전부
    //     sized 스칼라**(경계에서 바이트로 짓는 규칙 · line ~1339)라, 그 정의는 stdint/float/double
    //     뿐이고 **헤더 안전**하다(중첩·내부 타입 없음). ⇒ 정의를 내면 struct by-value extern 도
    //     다른 extern 과 똑같이 include 로 시그니처가 검사된다 — 헤더가 존재하는 바로 그 이유다.
    //   생성 C 의 struct 정의(line ~4332)와 **같은 폭·순서**로 낸다(두 곳이 갈리면 안 되지만, C
    //     컴파일러가 두 번역단위에서 같은 `struct lw_sty_N` 를 보므로 불일치는 컴파일 에러가 된다).
    cbe_hdr_structs(out, ir, 1);   // extern 만 쓰는 구조체(내보내는 쪽이 쓰는 것은 위에서 이미 냈다 — RFC-0132 P4d)
    for (proven_size_t i = 0; i < ir->ndefs; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        if (!d->is_extern) continue;
        if (!cbe_c_name_ok(d->link_name)) continue;
        if (!nx) low_puts(out, "\n/* ── `extern` op: C 쪽 정의는 이 헤더를 include 하라 ──\n"
                       "   그러면 시그니처 불일치가 컴파일 에러가 된다. 안 하면 아무도 안 잡는다. */\n");
        cbe_extern_proto(ir, d, out);
        nx++;
    }
    low_puts(out, "\n#ifdef __cplusplus\n}\n#endif\n#endif\n");
    return n + nx;
}

// ★★★ **feature→link** (RFC-0013 §11.3) — 프로그램이 동시성 op 을 쓰는가. 안 쓰면 코루틴 런타임
//   (LW_CONC — ucontext·스케줄러·채널·await)을 **아예 방출하지 않는다**(P1 pay-as-you-go · 임베디드 호환).
// ★★★ **이 def 가 동료를 필요로 하는가** — 채널 op 을 (호출을 따라) 쓰는가.
//   A5 의 `concurrent` 효과와 **같은 판정**이고, 그것이 곧 **진짜 스레드 자격**이다:
//   동료가 필요 없으면 *"어떤 executor 로도 만족된다"*(RFC-0022 D-A) — 자기 OS 스레드 포함.
// ★★ 액터 핸들러의 다시 세우기 상한 — `failure restart max N` 이면 N · `always` 면 4096(안전 상한) ·
//   정책이 없거나 `never` 면 −1(경계를 세우지 않는다 — 패닉이 그대로 위로 간다). VM 의 vm_actor_exec 와 같은 수.
//   기다리는 `send` 와 우편함(`spawn send` · `try spawn send`)이 같은 값을 쓴다(W-20261109-1922).
static long long cbe_restart_max(const low_ir_t *ir, const low_ir_def_t *h) {
    if (!h->is_actor || h->nparams == 0) return -1;
    proven_size_t sidx = h->param_sidx[0];
    if (sidx >= ir->nstructs) return -1;
    proven_u8 fp = ir->structs[sidx].failure_policy;
    if (fp == 1) return (long long)ir->structs[sidx].failure_max;
    if (fp == 3) return 4096;
    return -1;
}

// ★★★ 2단계 2b (2026-11-10 · W-20261110-1025) — 이 처리 코드가 **멈춰 설 수 있는가**(부르는 op 까지 따라간다).
//   멈춰 서는 낱말은 여섯: yield · 채널 넣기/빼기 · await · reactor 읽기/쓰기(EAGAIN 이면 lw_io_wait 로 넘긴다).
//   하나도 없으면 `schedule` 이 그 메시지를 그린스레드 없이 **같은 차례에서** 바로 돌린다 — 한 액터는 한 번에 하나라
//   (정본 §10.5 (4a)) 끝까지 돌아도 답이 같고, 문맥을 세우고 바꾸는 값(메시지 하나 약 1 µs)이 빠진다.
//   모르면(깊이 8 을 넘는 부름 · 되부름) **멈춰 설 수 있다**고 본다.
static bool cbe_may_suspend(const low_ir_t *ir, proven_size_t d, int depth) {
    if (depth > 8 || d >= ir->ndefs) return true;
    for (proven_size_t i = 0; i < ir->defs[d].ncode; i++) {
        low_irw_t w = ir->defs[d].code[i].w;
        if (w == IRW_YIELD || w == IRW_CHRECV || w == IRW_CHSEND || w == IRW_AWAIT || w == IRW_RREAD || w == IRW_RWRITE) return true;
        // ★★ `drain` 은 그 액터의 처리 코드를 **이 자리에서** 돌린다 — 그것이 yield 하면 여기서 멈춰 선다. 어느 처리 코드인지는
        //   실행할 때 정해지므로 «멈춰 설 수 있다» 로 본다. (2026-11-10 v1.31.0 에서 빠져 있었다: `schedule` 로 온 메시지가 yield 하는
        //   액터를 drain 하면 네이티브가 죽었다 — VM 은 5. 기록 docs/known-defects/drain-in-place-handler-yields.md)
        if (w == IRW_DRAIN) return true;
        // ★★ 2026-11-11 (W-20261111-0940 O7) — 액터에게 보내는 기다리는 `send` 는 그 액터를 다른 흐름이 쥐고 있으면 **기다린다**(LW_SEND_ENTER 가
        //   양보한다) — 그 자리에서(그린스레드 없이) 도는 처리 코드는 양보할 문맥이 없다. `try send` 는 기다리지 않는다.
        if (w == IRW_CALL && !(ir->defs[d].code[i].a & IR_CALL_TRY) && IR_CALL_IDX(ir->defs[d].code[i].a) < ir->ndefs &&
            ir->defs[IR_CALL_IDX(ir->defs[d].code[i].a)].is_actor) return true;
        if (w == IRW_CALL && cbe_may_suspend(ir, IR_CALL_IDX(ir->defs[d].code[i].a), depth + 1)) return true;
    }
    return false;
}

static bool cbe_needs_peer(const low_ir_t *ir, proven_size_t d, int depth) {
    if (depth > 8 || d >= ir->ndefs) return true;          // 모르면 **안전한 쪽**(협조적)으로
    for (proven_size_t i = 0; i < ir->defs[d].ncode; i++) {
        low_irw_t w = ir->defs[d].code[i].w;
        if (w == IRW_CHRECV || w == IRW_CHSEND || w == IRW_AWAIT) return true;
        // ★★★ 2026-11-11 (W-20261111-0940 O4) — **우편함을 만지는 태스크도 진짜 스레드로 안 보낸다.** 우편함(lw_ambox · lw_amb_*)과
        //   빠른 액터의 자리(lw_fa_mem · lw_fa_own)는 프로세스에 하나다 — 워커 둘이 같이 넣고 빼면 줄이 깨진다. 실측(64 태스크가
        //   저마다 띄운 액터에 `spawn send` + `drain`, LOWENT_THREADS=1): 10 번 중 2 번 죽었고, 더 긴 판은 6 번 중 5 번 죽고 1 번은
        //   틀린 답이었다. 액터에게 기다리는 `send`(처리 코드 부름)도 같다 — 넘겨받은 액터를 두 워커가 같이 고친다. «다르게 굴 수 있는
        //   것은 안 보낸다»(cancel_on_error 와 같은 규율) — 답은 협조적 실행의 답 그대로다.
        if (w == IRW_ASEND || w == IRW_TASEND || w == IRW_DRAIN || w == IRW_SCHED || w == IRW_TSPAWN) return true;
        if (w == IRW_CALL) {
            proven_size_t c = IR_CALL_IDX(ir->defs[d].code[i].a);
            if (c < ir->ndefs && ir->defs[c].is_actor) return true;
            if (cbe_needs_peer(ir, c, depth + 1)) return true;
        }
    }
    return false;
}

static bool cbe_uses_proc(const low_ir_t *ir) {   // ★ RFC-0136 — 프로세스 잎을 쓰는가
    for (proven_size_t d = 0; d < ir->ndefs; d++)
        for (proven_size_t i = 0; i < ir->defs[d].ncode; i++) {
            low_irw_t w = ir->defs[d].code[i].w;
            if (w == IRW_PSPAWN || w == IRW_PREAD || w == IRW_PPOLL || w == IRW_PWAIT || w == IRW_PKILL) return true;
        }
    return false;
}
static bool cbe_uses_reactor(const low_ir_t *ir) {
    for (proven_size_t d = 0; d < ir->ndefs; d++)
        for (proven_size_t i = 0; i < ir->defs[d].ncode; i++)
            if (ir->defs[d].code[i].w == IRW_RNEW || ir->defs[d].code[i].w == IRW_RREAD ||
                ir->defs[d].code[i].w == IRW_RWRITE) return true;
    return false;
}

// ★ 이 def 이 **벡터 슬롯을 쓸 수 있는가**. 아니면 되감기 코드를 **한 줄도 안 낸다** —
//   안 그러면 벡터를 안 쓰는 프리스탠딩 op 이 `lw_vec_outside` 를 통해 박스·레코드·스택 풀을
//   **끌고 들어온다**(bss 0 이 계약인 자리에서 그것은 회귀다). 벡터는 splat/load·벡터 파라미터·
//   벡터 반환으로만 들어온다 — 셋 다 정적으로 보인다.
// ★ 이 def 에 **풀 되감기**를 낼 수 있는가(F1). C 는 호출자의 `loc[]` 를 훑을 수 없으므로,
//   `mut_ref` 로 남의 지역에 써 넣을 수 있는 op 에는 **아예 안 낸다** — 유일한 안전 구멍을
//   정적으로 막는다(그 대가로 그런 op 은 옛 규율 그대로 단조 증가한다: 정직한 보수).
static bool cbe_pool_gc_ok(const low_ir_def_t *d) {
    if (d->takes_ref_arg) return false;
    for (proven_size_t j = 0; j < d->ncode; j++)
        if (d->code[j].w == IRW_REF || d->code[j].w == IRW_MREF) return false;
    return true;
}
static bool cbe_uses_vec_1(const low_ir_def_t *d) {
    if (d->param_vec) return true;
    for (proven_size_t j = 0; j < d->ncode; j++)
        switch (d->code[j].w) {
            case IRW_SPLAT: case IRW_VLOAD: case IRW_VLOADM: case IRW_VSTORE: case IRW_VSTOREM:
            case IRW_VAVG:  case IRW_SELECT: case IRW_VREVERSE: case IRW_VROTATE: case IRW_VSHUFFLE:
            case IRW_RADD:  case IRW_RMUL: case IRW_RMIN: case IRW_RMAX: case IRW_MANY: case IRW_MALL:
                return true;
            default: break;
        }
    return false;
}
// ★★★ **판정은 호출 그래프의 고정점이다** (RFC-0089 후속 F2, 2026-08-04).
//   단계 B 는 이 판정을 **한 단계**만 봤다: 벡터를 **그냥 통과시키기만** 하는 op(부르고 돌려주고,
//   자기는 벡터 낱말을 하나도 안 쓰는)이 루프에 끼면 그 프레임은 안 되감았다 — 안전하지만
//   완전하지 않았고, RFC-0089 §9-2 에 그렇게 적어 두었다. 이제 **부르는 쪽으로 전파**한다.
static bool g_vecfn[512]; static const low_ir_t *g_vecfn_for;
static bool cbe_uses_vec(const low_ir_def_t *d);
static void cbe_vecfn_all(const low_ir_t *ir) {
    if (g_vecfn_for == ir) return;
    for (proven_size_t i = 0; i < 512; i++) g_vecfn[i] = false;
    for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++) g_vecfn[i] = cbe_uses_vec_1(&ir->defs[i]);
    for (bool moved = true; moved; ) {          // 고정점: 부르는 쪽으로 번진다
        moved = false;
        for (proven_size_t i = 0; i < ir->ndefs && i < 512; i++) {
            if (g_vecfn[i]) continue;
            const low_ir_def_t *d = &ir->defs[i];
            for (proven_size_t j = 0; j < d->ncode; j++)
                if (d->code[j].w == IRW_CALL && IR_CALL_IDX(d->code[j].a) < 512 && g_vecfn[IR_CALL_IDX(d->code[j].a)]) {
                    g_vecfn[i] = true; moved = true; break;
                }
        }
    }
    g_vecfn_for = ir;
}
static const low_ir_t *g_vecfn_ir;   // 방출 중인 IR(고정점 표를 읽으려면 필요하다)
static bool cbe_uses_vec(const low_ir_def_t *d) {
    if (!g_vecfn_ir) return cbe_uses_vec_1(d);
    proven_size_t i = (proven_size_t)(d - g_vecfn_ir->defs);
    return (i < 512 && i < g_vecfn_ir->ndefs) ? g_vecfn[i] : cbe_uses_vec_1(d);
}
static bool cbe_uses_conc(const low_ir_t *ir) {
    for (proven_size_t d = 0; d < ir->ndefs; d++)
        for (proven_size_t i = 0; i < ir->defs[d].ncode; i++)
            switch (ir->defs[d].code[i].w) {
                case IRW_ASEND: case IRW_TASEND: case IRW_DRAIN: case IRW_SCHED:
                case IRW_TSPAWN: case IRW_YIELD: case IRW_CHNEW: case IRW_CHSEND:
                case IRW_CHRECV: case IRW_AWAIT: case IRW_CANCELSCOPE:
                    return true;
                default: break;
            }
    return false;
}

int low_cbe_emit(const low_ir_t *ir, low_out_t *out) {
    if (!ir || !out) return -1;
    // ★★★ **겹치는 C 이름으로는 내지 않는다** (단계 W). 계획이 이미 판정해 뒀다.
    //   ☞ 내고 나서 `cc` 가 redefinition 으로 죽는 것보다, **여기서 이름을 대며 멈추는** 것이
    //     낫다 — 실패는 원인 가까이에서 나야 한다.
    low_cbe_plan(ir);
    if (g_abi_dup) {
        low_fpf(low_stderr(), "E-ABI-NAME-DUP: {}\n", proven_arg_cstr(g_abi_dup_msg));
        return -1;
    }
    low_puts(out, "/* generated by lowentc --emit-c (S5 C backend) — do not edit */\n");
    // ★★★ **곱하고 더하기를 몰래 하나로 합치지 않는다** (2026-11-08 · 정본 §6.2.4 (2a) · RFC-0156 §10 · BTS LD-11).
    //   gcc 의 기본은 `-ffp-contract=fast` 다 — FMA 명령이 있는 기계(`-march=x86-64-v3` · aarch64 · Cortex-M4F)에서
    //   `a*b+c` 가 반올림 한 번짜리 `vfmadd` 가 되어 VM 과 다른 비트를 낸다(실측: 이 머리 없이 1 개).
    //   gcc 는 표준 지시 `STDC FP_CONTRACT` 를 따르지 않고 `GCC optimize` 를 따른다 · clang 은 기본으로 합치고 표준 지시를 따른다.
    //   생성 C 는 사용자가 교차 컴파일러로 직접 짓는 것이 정상 경로라, 명령줄 깃발이 아니라 **소스 머리**에 둔다.
    //   컴파일러마다 **하나만** 낸다 — gcc 는 `-Wall` 에서 모르는 표준 지시를 경고한다.
    low_puts(out, "#if defined(__clang__)\n#pragma clang fp contract(off)\n"
          "#elif defined(__GNUC__)\n#pragma GCC optimize (\"fp-contract=off\")\n"
          "#else\n#pragma STDC FP_CONTRACT OFF\n#endif\n");
    // ★★★ 얼로케이터 아레나(A3). 크기는 **low_ir.h 한 곳**에서 온다 — VM 이 쓰는 그 상수다.
    //   풀 크기를 양쪽에 따로 적었다가 어긋난 전례가 있다(lw_boxes[128] vs VM_MAXBOX 4096) ⇒
    //   여기서는 숫자를 **베끼지 않고 흘려보낸다**(§0).
    // ★★★★★ **베어메탈에서는 풀을 작게 잡는다 — 그리고 손잡이를 준다** (X-0029 ⓐ · WO-0208).
    //   실측(2026-09-13): 프리스탠딩 액터 프로그램의 bss 3,568,980 B 가운데 **동시성은 10.6 KB**
    //   뿐이고 나머지는 값 풀이었다(레코드 1.61 MB · `stack` 1.58 MB · 박스 197 KB · 아레나 64 KB).
    //   Cortex-M4 의 흔한 RAM 이 64~256 KB 이므로 그 방출은 **지어지기만 하고 안 올라간다.**
    //   ★ 왜 작게 잡아도 되는가: 다섯 풀 **모두 가드가 있다** — 넘치면 조용히 남의 바이트를 밟는
    //     대신 `lw_panic` 으로 말하고 멈춘다. 그러니 «작게 잡고 넘치면 거절» 이 베어메탈의 정직한
    //     모형이다(정적 할당). 모자라면 `-D` 로 키운다 — 거절 문구가 그 손잡이 이름을 말한다.
    //   ★★ **호스트는 한 바이트도 안 바뀐다.**
    //   ☞ 줄이는 것은 **개수**뿐이다. `stack` 하나의 칸 수(128)는 프로그램이 선언한 계약이라
    //     건드리지 않는다 — 그것을 줄이면 같은 소스가 타깃에 따라 다르게 동작한다.
    {
        unsigned boxp = (unsigned)g_boxpool;
        int recp = g_recpool, stkp = 256, bsetp = 4096;
        if (low_ir_target()->no_heap) { boxp = 64u; recp = 64; stkp = 8; bsetp = 64; }
        // ★★★★ **두 뿌리** (RFC-0112 D3 · WO-0211).
        //   · 고정 창 — 호스티드는 정적 저장소(`.bss`), 프리스탠딩은 **링커가 주는 경계** 사이다
        //     (`LW_FIXED_LINKER` 1 — 보드의 링커 스크립트가 `__lw_fixed_start`·`__lw_fixed_end` 를 정한다.
        //     `--emit-ldscript` 가 기본 조각을 낸다). 실행 파일에 창의 크기가 박히지 않는다.
        //   · 힙 — **이 단위가 자라는 뿌리를 쓸 때만** 표를 낸다(`LW_HEAP_ROOT`). 안 쓰는 프로그램은
        //     한 바이트도 안 낸다(값을 치르는 것은 쓰는 쪽이다). 프리스탠딩에서는 늘 0 이다.
        bool uses_heap = false;
        for (proven_size_t i = 0; i < ir->ndefs && !uses_heap; i++)
            for (proven_size_t j = 0; j < ir->defs[i].ncode; j++) {
                low_irw_t w = ir->defs[i].code[j].w;
                if ((w == IRW_ALLOCB || w == IRW_RMARK || w == IRW_RRESET) && ir->defs[i].code[j].a == 1) {
                    uses_heap = true; break;
                }
            }
        if (low_ir_target()->no_heap) uses_heap = false;
        low_fpf(out, "#ifndef LW_FIXED_LINKER\n#define LW_FIXED_LINKER {}\n#endif\n",
                PROVEN_ARG((int)(low_ir_target()->no_heap ? 1 : 0)));
        low_fpf(out, "#ifndef LW_FIXED_BYTES\n#define LW_FIXED_BYTES {}u\n#endif\n",
                PROVEN_ARG((unsigned)(low_ir_target()->no_heap ? (unsigned)LOW_FIXED_BYTES_BOARD : (unsigned)LOW_FIXED_BYTES)));
        low_fpf(out, "#ifndef LW_HEAP_ROOT\n#define LW_HEAP_ROOT {}\n#endif\n", PROVEN_ARG((int)(uses_heap ? 1 : 0)));
        low_fpf(out, "#ifndef LW_HEAP_CHUNK\n#define LW_HEAP_CHUNK {}u\n#endif\n", PROVEN_ARG((unsigned)((unsigned)LOW_HEAP_CHUNK)));
        low_fpf(out, "#ifndef LW_BOXPOOL\n#define LW_BOXPOOL {}u\n#endif\n", PROVEN_ARG((unsigned)(boxp)));
        low_fpf(out, "#ifndef LW_RECPOOL\n#define LW_RECPOOL {}\n#endif\n", PROVEN_ARG((int)(recp)));
        low_fpf(out, "#ifndef LW_STKPOOL\n#define LW_STKPOOL {}\n#endif\n", PROVEN_ARG((int)(stkp)));
        low_fpf(out, "#ifndef LW_BSETPOOL\n#define LW_BSETPOOL {}\n#endif\n", PROVEN_ARG((int)(bsetp)));
    }
    // ★★★ **타깃이 헤더를 정한다** (2026-08-01). `no_heap` = OS 가 없는 프로파일이다 —
    //   스레드도 파일계도 소켓도 stdio 도 **거기 없다**. 있는 척하면 실기 툴체인에서
    //   컴파일조차 안 된다(실측: `<dirent.h> not supported`).
    bool freestanding = low_ir_target()->no_heap;
    if (freestanding) low_puts(out, "#define LW_FREESTANDING 1\n");
    // ★ 호스트이지만 POSIX 가 아닌 칸(Windows)도 표시한다 — POSIX 전용 도우미 블록이
    //   이 매크로로 닫힌다. 잎 자체는 이미 `E-TARGET-LEAF` 로 거절됐으므로 여기 오는 것은
    //   *POSIX 를 안 쓰는 프로그램* 뿐이다.
    else {
        // ★★ Windows 는 **호스티드이지만 POSIX 가 아니다** — 두 축은 직교한다.
        //   처음엔 `LW_NOPOSIX` 로 `LW_HOSTED` 를 **대체**했다가 `lw_fmt_f_hosted`
        //   (snprintf 하나, 완전히 이식 가능)가 사라져 mingw 가 암시적 선언으로 울었다.
        //   *"POSIX 가 아니다"* 는 *"stdio 도 없다"* 가 아니다.
        low_puts(out, "#define LW_HOSTED 1\n");
        if (!low_ir_target()->posix) low_puts(out, "#define LW_NOPOSIX 1\n");
    }
    {   // ★ RFC-0151 — 128 비트 정수를 쓰는 단위에만 그 도우미를 낸다(안 쓰는 프로그램 · 프리스탠딩 방출을 그대로 둔다)
        bool w = false;
        for (proven_size_t q = 0; q < ir->ndefs && !w; q++) {
            if (ir->defs[q].param_w128 || cbe_out_w128(&ir->defs[q])) w = true;
            for (proven_size_t j = 0; j < ir->defs[q].ncode && !w; j++) if (cbe_is_w128(&ir->defs[q].code[j])) w = true;
        }
        if (w) low_puts(out, "#define LW_W128 1\n");
    }
    // ★ 세 칸이다: 프리스탠딩(아무것도 없음) · Windows(표준 C 만) · POSIX(다 있음).
    low_puts(out, freestanding ? LW_HDR_FREE
                       : (low_ir_target()->posix ? LW_HDR_HOSTED : LW_HDR_WIN));
    // ★ 주입기는 **호스티드에서만**: 프리스탠딩엔 getenv 도 파일계도 없다(그쪽엔 주입할 잎이 없다).
    if (!freestanding) low_puts(out, LW_HOSTFAULT_C);
    // ★★★ **option/result 운반체의 레코드 칸은 이 단위의 최대 구조체만큼만** (WO-0177, 2026-09-03).
    //   `lw_r` 은 `r[64]` 를 값으로 들고 다녔다 — 120 byte 가 반환·푸시마다 복사됐다. 찌르개 churn 에서
    //   `create`/`resolve` 가 option 을 돌려주는 자리마다 그 값을 냈고, 칸을 8 로 줄이니 명령 **−10%**
    //   (시간 118 ns, 비트 헬퍼 인라인과 합쳐 136 → 93 ns · run-2026-09-03-b). WO-0140 이 "무관" 이라
    //   판정한 것은 **option 을 안 돌려주는** 순회였다 — 운반체의 값은 그것을 나르는 op 에만 붙는다.
    //   ⇒ 64 를 박지 않고 **이 단위가 만드는 가장 큰 구조체**(8 의 배수, 8 이상, 64 이하)로 낸다.
    //     크기는 `ir->structs[].total` 이 이미 안다 — 새로 세지 않는다.
    {
        unsigned recb = 8;
        for (proven_size_t si = 0; si < ir->nstructs; si++) {
            unsigned t = (unsigned)ir->structs[si].total; t = (t + 7u) & ~7u;
            if (t > recb) recb = t;
        }
        if (recb > 64) recb = 64;
        low_fpf(out, "#define LW_RECB {}\n", PROVEN_ARG((unsigned)(recb)));
        low_fpf(out, "#define LW_PTRSLOT {}\n", PROVEN_ARG((unsigned)(low_ptr_slot())));   // ★ RFC-0137 P3 — 슬라이스 칸의 포인터 자리 크기(길이는 그 뒤 8 바이트)
        // ★ RFC-0137 P4 (D6 · 정본 §7.6.1 (3)) — 장치 주소에서 포인터를 만드는 **유일한 자리**. 능력 포인터 기계에서는 플랫폼이 준
        //   «장치 메모리에 닿는 능력»(`lw_mmio_root`)의 주소만 바꿔 **파생**한다 — 정수만으로 포인터를 만들지 않는다. 그 밖의 기계에서는
        //   지금과 같은 정수 → 포인터다(권한 `cap mmio` 는 표지일 뿐 뜻이 같다).
        low_puts(out, "#if defined(__CHERI_PURE_CAPABILITY__)\n"
              "extern void *lw_mmio_root;   /* the platform provides a capability covering device memory (RFC-0137 D6) */\n"
              "#  define LW_MMIO_PTR(a) ((unsigned char *)__builtin_cheri_address_set(lw_mmio_root, (a)))\n"
              "#  define LW_MMIO_SPAN(a, n) ((unsigned char *)__builtin_cheri_bounds_set(LW_MMIO_PTR(a), (n)))\n"
              "#else\n"
              "#  define LW_MMIO_PTR(a) ((unsigned char *)(uintptr_t)(a))\n"
              "#  define LW_MMIO_SPAN(a, n) LW_MMIO_PTR(a)\n"
              "#endif\n");
    }
    // ★★★ **파라미터 한도는 방출 C 에서도 수 하나다** (WO-0204). 전에는 인자를 나르는 배열마다
    //   `16` 이 글자로 박혀 있었다 — 컴파일러 쪽 한도를 올려도 방출 쪽은 안 따라오고, 그 어긋남은
    //   **배열 밖 쓰기**로 나타난다(병렬 op 이 실제로 그랬다: `lw_ptask.a[8]` 에 아홉 칸을 채웠다).
    //   이제 `LW_MAXP` 하나가 컴파일러의 `LOW_MAX_PARAMS` 에서 나온다.
    low_fpf(out, "#define LW_MAXP {}\n", PROVEN_ARG((int)((int)LOW_MAX_PARAMS)));
    low_fpf(out, "#define LW_HW_CLMUL {}\n", PROVEN_ARG((int)(g_hw_clmul)));
    low_fpf(out, "#define LW_HW_AES {}\n", PROVEN_ARG((int)(g_hw_aes)));
    low_fpf(out, "#define LW_HW_SSE2 {}\n", PROVEN_ARG((int)(g_hw_simd)));
    low_fpf(out, "#define LW_HW_AVX2 {}\n", PROVEN_ARG((int)(g_hw_avx2)));
    low_fpf(out, "#define LW_HW_ASM {}\n", PROVEN_ARG((int)(g_hw_asm)));
    low_fpf(out, "#define LW_HW_VAES {}\n", PROVEN_ARG((int)(g_hw_vaes)));
    {   // ★ RFC-0155 §8-6 (2026-10-30) — 줄 꼴 표: 칸 k 의 아래 줄 바이트 · 그 아래 칸(0 = 원소). `lw_index` 가 줄의 줄을 낼 때 읽는다(VM 과 같은 표)
        low_puts(out, "static const struct { unsigned rb; unsigned sub; } lw_rowsh[] = { {0u,0u}");
        for (proven_size_t q = 1; q < ir->nrowsh; q++) low_fpf(out, ", {{{}u,{}u}}", PROVEN_ARG((unsigned)((unsigned)ir->rowsh_rb[q])), PROVEN_ARG((unsigned)((unsigned)ir->rowsh_sub[q])));
        low_fpf(out, " }};\n#define LW_NROWSH {}u\n", PROVEN_ARG((size_t)(ir->nrowsh ? ir->nrowsh : (proven_size_t)1)));
    }
    {   // ★ 2026-11-01 (W-20261031-2053) — 호스트 잎의 공용 함수(`lw_h_*`)는 그 잎을 쓰는 단위에만 낸다. 경로 버퍼(4 KB)를 가진 것은 op 안으로
        //   펼치지 않으므로(`noinline`) `static inline` 일 수 없다 — 그러면 `-O0` 에서 안 쓰는 단위에도 나와 libc 의존을 끌고 온다.
        bool hl = false;
        for (proven_size_t q = 0; q < ir->ndefs && !hl; q++)
            for (proven_size_t j = 0; j < ir->defs[q].ncode && !hl; j++)
                switch (ir->defs[q].code[j].w) {
                    case IRW_WRITE: case IRW_RDIN: case IRW_ARGV: case IRW_ENVGET:
                    case IRW_FOPEN: case IRW_FREAD: case IRW_FWRITE: case IRW_FCLOSE: case IRW_FSEEK:
                    case IRW_DOPEN: case IRW_DREAD: case IRW_DCLOSE: case IRW_FTYPE: case IRW_LTYPE:
                    case IRW_DMAKE: case IRW_PREMOVE: case IRW_PRENAME:
                    case IRW_NPAIR: case IRW_NSEND: case IRW_NRECV: case IRW_NCLOSE: case IRW_NLISTEN:
                    case IRW_NPORT: case IRW_NCONNECT: case IRW_NACCEPT: case IRW_NRESOLVE: hl = true; break;
                    default: break;
                }
        if (hl) low_puts(out, "#define LW_HLEAF 1\n");
    }
    low_puts(out, LW_PRELUDE);
    if (cbe_uses_proc(ir)) low_puts(out, LW_PROC_RT);   // ★ RFC-0136 — 쓰는 단위에만
    // ★★★ 동시성 런타임은 **쓸 때만** (pay-as-you-go) — 그리고 **reactor 를 쓸 때도** 낸다.
    //   ☞ 처음엔 reactor 를 별도 블록으로 떼었는데, 스케줄러가 `lw_io_poll_once` 를 부르고
    //     reactor 가 `lw_gthr` 를 보므로 **서로를 참조한다.** 쪼개면 순서가 안 나온다.
    //   ⇒ 한 블록으로 둔다. **정직한 대가**: `r_read` 만 쓰는 프로그램도 코루틴 런타임을
    //     끌고 온다. 그러나 reactor 의 존재 이유가 *"스케줄러에 양보한다"* 이므로
    //     둘은 애초에 한 기계다 — 쪼개는 것이 오히려 거짓 분리였다.
    if (cbe_uses_conc(ir) || cbe_uses_reactor(ir)) {
        // ★★★ **타깃이 런타임을 정한다** (X-0027 ⓑ · WO-0206). 프리스탠딩에는 `ucontext` 도
        //   `pthread` 도 `epoll` 도 없다 — 그 타깃에는 협력 floor(T0)를 낸다. 전에는 호스트용
        //   런타임을 그대로 내고 **조용히 성공**했고, 그 C 는 컴파일 단계에서 깨졌다.
        if (low_ir_target()->no_heap || g_conc_t0) {
            // ★ 인자 칸은 **이 프로그램이 실제로 쓰는 최대 인자 수**다 — `LW_MAXP`(32)로 잡으면
            //   베어메탈 우편함이 그 자리에서 수십 배가 된다(소유자에게 한 약속이다).
            proven_size_t ma = 1;
            for (proven_size_t d2 = 0; d2 < ir->ndefs; d2++)
                if (ir->defs[d2].nparams > ma) ma = ir->defs[d2].nparams;
            if (ma > LOW_MAX_PARAMS) ma = LOW_MAX_PARAMS;
            low_fpf(out, "#define LW_T0_ARGS {}\n", PROVEN_ARG((size_t)((size_t)ma)));
            low_puts(out, LW_CONC_T0);
        } else low_puts(out, LW_CONC);
    }
    // ★ 2026-11-10 (W-20261110-1548) — 수를 돌려준 부름 뒤에 레코드를 돌려받는다. 우편함이 있는 런타임은 메시지가 남은 액터의 레코드를
    //   돌려받지 않는다(그 런타임이 LW_REC_REWIND 를 정한다 · VM vm_rec_rewind) — 없으면 그대로 되감는다.
    // ★ 2026-11-11 (O7) — 기다리는 `send` 의 쥐기 · 기다리기는 그린스레드가 있는 런타임(LW_CONC)에만 있다. 없으면(동시성 없음 · T0) 늘 안 바쁘다.
    low_puts(out, "#ifndef LW_SEND_ENTER\n#define LW_SEND_ENTER(b) ((void)(b), 0)\n#define LW_SEND_LEAVE(b, p) ((void)(b), (void)(p))\n#define LW_ACTOR_BUSY(b) ((void)(b), 0)\n#endif\n");
    low_puts(out, "#ifndef LW_REC_REWIND\n#define LW_REC_REWIND(w) (lw_nrec = (w))\n#endif\n"
          "#ifndef LW_GC_MBOX_MARK\n#define LW_GC_MBOX_MARK() ((void)0)\n#define LW_GC_MBOX_MOVE(i, j) ((void)0)\n#define LW_GC_MBOX_FIX() ((void)0)\n#endif\n");
    // ★ `lw_v` 는 **유닛마다 크기가 다르다**(위 `cbe_mark_vbytes`). 프렐류드보다 먼저 낸다 —
    //   뒤의 모든 것이 이 타입을 쓴다.
    low_fpf(out, "typedef union {{ unsigned long long u64[{}]; uint32_t u32[{}];\n"
                 "                unsigned short u16[{}]; unsigned char u8[{}]; }} lw_v;\n",
            PROVEN_ARG((int)(g_vbytes / 8 ? g_vbytes / 8 : 1)), PROVEN_ARG((int)(g_vbytes / 4 ? g_vbytes / 4 : 1)),
            PROVEN_ARG((int)(g_vbytes / 2 ? g_vbytes / 2 : 1)), PROVEN_ARG((int)(g_vbytes)));
    low_puts(out, LW_PRELUDE2);

    // make-site + error-name tables (referenced by lw_render and the bodies)
    low_puts(out, "static const char *lw_mk_fields[][16] = {");
    for (proven_size_t m = 0; m < ir->nmakes; m++) {
        low_puts(out, m ? ", {" : " {");
        for (proven_size_t f = 0; f < ir->makes[m].nfields; f++) {
            low_puts(out, f ? ", \"" : " \"");
            put_view(out, ir->makes[m].fields[f]);
            low_puts(out, "\"");
        }
        low_puts(out, " }");
    }
    low_puts(out, ir->nmakes ? " };\n" : " { 0 } };\n");
    low_puts(out, "static const char *lw_errs[] = {");
    for (proven_size_t e = 0; e < ir->nerrs; e++) {
        low_puts(out, e ? ", \"" : " \"");
        put_view(out, ir->errs[e]);
        low_puts(out, "\"");
    }
    low_puts(out, ir->nerrs ? " };\n" : " \"\" };\n");
    low_puts(out, "static const char *lw_fnames[] = {");
    for (proven_size_t e = 0; e < ir->nfields; e++) {
        low_puts(out, e ? ", \"" : " \"");
        put_view(out, ir->fields[e]);
        low_puts(out, "\"");
    }
    low_puts(out, ir->nfields ? " };\n" : " \"\" };\n");
    // struct layout tables (for view/try_view + view field reads)
    low_fpf(out, "static const int lw_st_nf[] = {{");
    for (proven_size_t s = 0; s < ir->nstructs; s++) low_fpf(out, "{}{}", proven_arg_cstr(s ? ", " : " "), PROVEN_ARG((size_t)(ir->structs[s].nf)));
    low_puts(out, ir->nstructs ? " };\n" : " 0 };\n");
    low_fpf(out, "static const int lw_st_total[] = {{");
    for (proven_size_t s = 0; s < ir->nstructs; s++) low_fpf(out, "{}{}", proven_arg_cstr(s ? ", " : " "), PROVEN_ARG((unsigned)((unsigned)ir->structs[s].total)));
    low_puts(out, ir->nstructs ? " };\n" : " 0 };\n");
    // ★ 2026-11-03 (W-20261103-1143) — 빠른 레코드의 «마디를 가리키는 칸»(주소 · 0 = none)을 태그 값 option(마디의 뷰)으로 — lw_field 의 마디 칸과 같은 모양
    low_puts(out, "static inline lowv lw_nopt_out(const unsigned char *at, int sh) { const unsigned char *pp_; memcpy(&pp_, at, sizeof pp_);\n"
          "    if (!pp_) { lowv n_ = {0}; n_.tag = LWV_NONE; return n_; }\n"
          "    if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
          "    lowv w_ = {0}; w_.tag = LWV_VIEW; w_.p = pp_; w_.box = sh; w_.n = (size_t)lw_st_total[sh];\n"
          "    lw_boxes[lw_nbox] = w_; lowv o_ = {0}; o_.tag = LWV_SOME; o_.box = lw_nbox++; return o_; }\n");
    // ★ RFC-0150 §8-31 — 제약 있는 칸(태그 경로의 lw_tryview 가 잰다): 구조체 s 의 칸은 lw_cf_*[lw_st_cfb[s] .. lw_st_cfb[s+1]).
    //   bool 칸은 lo == hi == -1, 폭을 적은 열거 칸은 lw_en_num / lw_en_vi 의 [lo, hi) 가 갈래들이다.
    {
        low_ir_cfield_t all[1024]; proven_size_t tot = 0, starts[257] = { 0 };
        for (proven_size_t s2 = 0; s2 < ir->nstructs && s2 < 256; s2++) {
            starts[s2] = tot;
            tot += low_ir_struct_cfields(ir, s2, all + tot, 1024 - tot);
        }
        starts[ir->nstructs < 256 ? ir->nstructs : 256] = tot;
        low_puts(out, "static const int lw_cf_off[] = {");  for (proven_size_t q = 0; q < tot; q++) low_fpf(out, "{}{}", proven_arg_cstr(q ? ", " : " "), PROVEN_ARG((unsigned)((unsigned)all[q].off)));  low_puts(out, tot ? " };\n" : " 0 };\n");
        low_puts(out, "static const int lw_cf_sz[] = {");   for (proven_size_t q = 0; q < tot; q++) low_fpf(out, "{}{}", proven_arg_cstr(q ? ", " : " "), PROVEN_ARG((unsigned)((unsigned)all[q].size))); low_puts(out, tot ? " };\n" : " 0 };\n");
        low_puts(out, "static const int lw_cf_be[] = {");   for (proven_size_t q = 0; q < tot; q++) low_fpf(out, "{}{}", proven_arg_cstr(q ? ", " : " "), PROVEN_ARG((int)(all[q].be ? 1 : 0))); low_puts(out, tot ? " };\n" : " 0 };\n");
        low_puts(out, "static const int lw_cf_lo[] = {");   for (proven_size_t q = 0; q < tot; q++) low_fpf(out, "{}{}", proven_arg_cstr(q ? ", " : " "), PROVEN_ARG((int)(all[q].isbool ? -1 : (int)all[q].elo))); low_puts(out, tot ? " };\n" : " 0 };\n");
        low_puts(out, "static const int lw_cf_hi[] = {");   for (proven_size_t q = 0; q < tot; q++) low_fpf(out, "{}{}", proven_arg_cstr(q ? ", " : " "), PROVEN_ARG((int)(all[q].isbool ? -1 : (int)all[q].ehi))); low_puts(out, tot ? " };\n" : " 0 };\n");
        low_puts(out, "static const int lw_st_cfb[] = {");
        for (proven_size_t s2 = 0; s2 <= ir->nstructs && s2 <= 256; s2++) low_fpf(out, "{}{}", proven_arg_cstr(s2 ? ", " : " "), PROVEN_ARG((size_t)(starts[s2])));
        low_puts(out, " };\n");
        low_puts(out, "static const long long lw_en_num[] = {");
        for (proven_size_t q = 0; q < ir->nenum_tab; q++) low_fpf(out, "{}{}LL", proven_arg_cstr(q ? ", " : " "), PROVEN_ARG((long long)((long long)ir->enum_tab_num[q])));
        low_puts(out, ir->nenum_tab ? " };\n" : " 0 };\n");
        low_puts(out, "static const long long lw_en_vi[] = {");
        for (proven_size_t q = 0; q < ir->nenum_tab; q++) low_fpf(out, "{}{}LL", proven_arg_cstr(q ? ", " : " "), PROVEN_ARG((long long)((long long)ir->enum_tab_vi[q])));
        low_puts(out, ir->nenum_tab ? " };\n" : " 0 };\n");
        // 번호 ↔ 순번 — [lo, hi) 가 비면(열거 칸이 아니면) 그대로. 짝이 없으면 멈춘다(VM 의 E-VM-CAST 와 같은 말)
        low_puts(out, "static unsigned long long lw_en_tovi_r(int lo, int hi, unsigned long long x) {\n"
              "    if (lo == hi) return x;\n"
              "    for (int q = lo; q < hi; q++) if ((unsigned long long)lw_en_num[q] == x) return (unsigned long long)lw_en_vi[q];\n"
              "    lw_panic(\"the bytes under an enum field hold a number no variant has\"); }\n"
              "static unsigned long long lw_en_tonum_r(int lo, int hi, unsigned long long x) {\n"
              "    if (lo == hi) return x;\n"
              "    for (int q = lo; q < hi; q++) if ((unsigned long long)lw_en_vi[q] == x) return (unsigned long long)lw_en_num[q];\n"
              "    lw_panic(\"an enum field holds no variant\"); }\n"
              "static int lw_cf_ok(int q, const unsigned char *p) {\n"
              "    p += lw_cf_off[q];\n"
              "    if (lw_cf_lo[q] < 0) return p[0] <= 1;\n"
              "    unsigned long long x = 0; int sz = lw_cf_sz[q];\n"
              "    if (lw_cf_be[q]) { for (int k = 0; k < sz; k++) x = (x << 8) | p[k]; } else { for (int k = sz; k-- > 0; ) x = (x << 8) | p[k]; }\n"
              "    for (int e = lw_cf_lo[q]; e < lw_cf_hi[q]; e++) if ((unsigned long long)lw_en_num[e] == x) return 1;\n"
              "    return 0; }\n");
    }
    low_fpf(out, "static const int lw_st_align[] = {{");
    for (proven_size_t s = 0; s < ir->nstructs; s++) low_fpf(out, "{}{}", proven_arg_cstr(s ? ", " : " "), PROVEN_ARG((unsigned)((unsigned)ir->structs[s].align)));
    low_puts(out, ir->nstructs ? " };\n" : " 0 };\n");
    // ★ **이 블록이 디바이스인가** (RFC-0042 D1) — 태그 경로의 lw_vread/lw_fstore 가 이것을 보고
    //   바이트 루프 대신 **폭이 맞는 volatile 접근 한 번**을 쓴다. 빠른 경로는 컴파일 시점에 안다.
    low_fpf(out, "static const int lw_st_mmio[] = {{");
    for (proven_size_t s = 0; s < ir->nstructs; s++) low_fpf(out, "{}{}", proven_arg_cstr(s ? ", " : " "), PROVEN_ARG((int)(ir->structs[s].is_mmio ? 1 : 0)));
    low_puts(out, ir->nstructs ? " };\n" : " 0 };\n");
    // ★ 필드가 **전부 sized 스칼라**인 struct 만 바이트로 지을 수 있다(actor 인스턴스처럼 레코드
    //   참조 필드를 가진 struct 는 아니다). VM 은 경계에서 이것을 E-VM-VIEW 로 거부한다 —
    //   네이티브도 같아야 한다(안 그러면 차등 스윕이 정당하게 둘을 컴파일러 버그로 고발한다).
    // ★★★★ **예약된 정적 블록의 실물** (RFC-0039 §9-2 갈래①). `reserve .` 를 단 타입마다
    //   **정확히 한 칸**을 잡는다 — 타입이 곧 인스턴스다. 링커가 `.bss` 에 자리를 주고,
    //   프로그램 수명 내내 거기 있다(초기값 0 은 C 가 보장한다).
    //   ☞ 왜 `_Alignas` 인가: 그 블록은 곧 `view` 로 열리고, view 는 정렬 계약을 **런타임에
    //     검사한다**(RFC-0025 §8-2, E-VM-ALIGN). 도구가 잡아 주는 칸이 그 계약을 어기면
    //     자기가 낸 프로그램을 자기가 트랩시킨다.
    for (proven_size_t s = 0; s < ir->nstructs; s++) {
        if (!ir->structs[s].is_reserve) continue;
        unsigned al = ir->structs[s].align ? (unsigned)ir->structs[s].align : 8u;
        low_fpf(out, "_Alignas({}) static unsigned char lw_res_{}[{}];\n",
                PROVEN_ARG((unsigned)(al)), PROVEN_ARG((size_t)(s)), PROVEN_ARG((unsigned)((unsigned)ir->structs[s].total)));
    }
    low_fpf(out, "static const int lw_st_viewable[] = {{");
    for (proven_size_t s = 0; s < ir->nstructs; s++) low_fpf(out, "{}{}", proven_arg_cstr(s ? ", " : " "), PROVEN_ARG((int)(ir->structs[s].viewable ? 1 : 0)));
    low_puts(out, ir->nstructs ? " };\n" : " 0 };\n");
    // ★★★ **구조체 by-value FFI** (RFC-0063 §5) — viewable 구조체마다 **진짜 C struct** 를 낸다.
    //   그러면 씨의 함수에 **값으로** 넘길 때 **C 컴파일러가 SysV AMD64 분류**를 한다(작은 건
    //   레지스터로, 큰 건 숨은 포인터로) — 우리가 분류기를 쓸 필요가 없다. Lowent 의 비-packed
    //   레이아웃은 C-natural(필드를 폭으로 정렬)이라 **바이트 레이아웃이 C 와 맞는다**.
    for (proven_size_t s = 0; s < ir->nstructs; s++) {
        if (!ir->structs[s].viewable || ir->structs[s].is_shadow) continue;   // ★ RFC-0135 D13 — 그림자는 C 타입으로 안 나간다
        low_fpf(out, "struct lw_sty_{} {{ ", PROVEN_ARG((size_t)(s)));
        for (proven_size_t i = 0; i < ir->structs[s].nf; i++) {
            const low_ir_sfield_t *fi_ = &ir->structs[s].f[i];
            proven_u8 sz = fi_->arrn ? fi_->arresz : fi_->size;   // ★ T2b-3d ⓓ — 배열 칸은 C 배열(`int8_t f1[4];`)
            bool fl_ = fi_->arrn ? (fi_->arrmeta & IR_FLT_BIT) != 0 : fi_->flt;
            const char *ct = fl_ ? (sz == 4 ? "float" : "double")
                           : sz == 1 ? "int8_t" : sz == 2 ? "int16_t" : sz == 4 ? "int32_t" : "int64_t";
            if (fi_->arrn) low_fpf(out, "{} f{}[{}]; ", proven_arg_cstr(ct), PROVEN_ARG((size_t)(i)), PROVEN_ARG((unsigned)((unsigned)fi_->arrn)));
            else low_fpf(out, "{} f{}; ", proven_arg_cstr(ct), PROVEN_ARG((size_t)(i)));
        }
        low_puts(out, "};\n");
        // ★★★ **레코드 → struct 값 실체화.** Lowent 에서 `make` 로 지은 구조체는 **레코드**다(풀에
        //   저장, .p 는 없다). 씨에 값으로 넘기려면 필드를 모아 진짜 struct 를 만들어야 한다 —
        //   안 그러면 `*(struct*).p` 가 **널을 역참조**한다(조합 테스트가 그 segfault 를 잡았다).
        //   바이트 뷰(CLI·view)면 .p 가 이미 연속이라 그대로 읽는다.
        low_fpf(out, "static struct lw_sty_{} lw_sty_{}_of(lowv v) {{\n", PROVEN_ARG((size_t)(s)), PROVEN_ARG((size_t)(s)));
        low_fpf(out, "    if (v.tag == LWV_REC) {{ lowrec *r_ = LW_RECV(v); struct lw_sty_{} s_ = {{0}};\n", PROVEN_ARG((size_t)(s)));
        for (proven_size_t i = 0; i < ir->structs[s].nf; i++) {
            if (ir->structs[s].f[i].arrn)                    // ★ T2b-3d ⓓ — 레코드의 배열 칸 바이트를 그대로
                low_fpf(out, "        memcpy(s_.f{}, r_->f[{}].p, sizeof s_.f{});\n", PROVEN_ARG((size_t)(i)), PROVEN_ARG((size_t)(i)), PROVEN_ARG((size_t)(i)));
            else if (ir->structs[s].f[i].flt)
                low_fpf(out, "        s_.f{} = lw_fval(r_->f[{}]);\n", PROVEN_ARG((size_t)(i)), PROVEN_ARG((size_t)(i)));
            else if (ir->structs[s].f[i].ename.size)   // ★ RFC-0150 §8-31 — 열거 칸은 갈래의 번호로 넘긴다
                low_fpf(out, "        s_.f{} = (long long)lw_en_tonum_r({}, {}, (unsigned long long)r_->f[{}].i);\n", PROVEN_ARG((size_t)(i)),
                        PROVEN_ARG((unsigned)((unsigned)ir->structs[s].f[i].elo)), PROVEN_ARG((unsigned)((unsigned)ir->structs[s].f[i].ehi)), PROVEN_ARG((size_t)(i)));
            else
                low_fpf(out, "        s_.f{} = r_->f[{}].i;\n", PROVEN_ARG((size_t)(i)), PROVEN_ARG((size_t)(i)));
        }
        low_fpf(out, "        return s_; }}\n    return *(const struct lw_sty_{} *)v.p;\n}}\n", PROVEN_ARG((size_t)(s)));
    }
    low_puts(out, "static const char *lw_st_fname[][16] = {");
    for (proven_size_t s = 0; s < ir->nstructs; s++) {
        low_puts(out, s ? ", {" : " {");
        for (proven_size_t i = 0; i < ir->structs[s].nf; i++) {
            low_puts(out, i ? ", \"" : " \"");
            put_view(out, ir->structs[s].f[i].name);
            low_puts(out, "\"");
        }
        low_puts(out, " }");
    }
    low_puts(out, ir->nstructs ? " };\n" : " { 0 } };\n");
    for (int t = 0; t < 14; t++) {
        const char *nm = t == 13 ? "lw_st_fsgn"                             /* ★ 2026-10-31 부호 있는 정수 칸(8 바이트 아래) — 바이트에서 읽을 때 부호를 늘린다 */
                       : t == 12 ? "lw_st_farrb"                            /* ★ RFC-0155 §8-3 배열 안의 배열 칸: 줄 바이트(0=줄이 아님) */
                       : t == 11 ? "lw_st_fsg16"                            /* ★ RFC-0151 §9.4.1 16 바이트 칸이 부호 있는가(i128) */
                       : t == 10 ? "lw_st_fbx"                              /* ★ RFC-0150 §8-12 스스로를 품는 칸(그림자): 가리키는 배치 + 1(0=아님) */
                       : t == 0 ? "lw_st_fsize" : t == 1 ? "lw_st_foff" : t == 2 ? "lw_st_fbe"
                       : t == 3 ? "lw_st_fflt" : t == 4 ? "lw_st_fsidx"   // ★ 중첩 구조체 필드의 인덱스(-1=스칼라)
                       : t == 5 ? "lw_st_farrn" : t == 6 ? "lw_st_farrm"  // ★ T2b-3d ⓓ 배열 칸: 원소 수 · 원소 메타(0=배열 칸 아님)
                       : t == 7 ? "lw_st_fslm"                             // ★ RFC-0135 D13 슬라이스 칸(그림자): 원소 메타(0=아님)
                       : t == 8 ? "lw_st_felo" : "lw_st_fehi";             // ★ RFC-0150 §8-31 열거 칸의 (번호, 순번) 구간(같으면 열거 칸 아님)
        low_fpf(out, "static const int {}[][16] = {{", proven_arg_cstr(nm));
        for (proven_size_t s = 0; s < ir->nstructs; s++) {
            low_puts(out, s ? ", {" : " {");
            for (proven_size_t i = 0; i < ir->structs[s].nf; i++)
                low_fpf(out, "{}{}", proven_arg_cstr(i ? ", " : " "),
                        PROVEN_ARG((int)(t == 0 ? (int)ir->structs[s].f[i].size :
                        t == 1 ? (int)ir->structs[s].f[i].off :
                        t == 2 ? (int)(ir->structs[s].f[i].be ? 1 : 0) :
                        t == 3 ? (int)(ir->structs[s].f[i].flt ? 1 : 0) :
                        t == 4 ? (int)ir->structs[s].f[i].sidx :
                        t == 5 ? (int)ir->structs[s].f[i].arrn :
                        t == 6 ? (int)ir->structs[s].f[i].arrmeta :
                        t == 7 ? (int)(ir->structs[s].is_shadow ? ir->structs[s].f[i].slmeta : 0) :
                        t == 8 ? (int)ir->structs[s].f[i].elo : t == 9 ? (int)ir->structs[s].f[i].ehi :
                        t == 10 ? (int)(ir->structs[s].is_shadow ? ir->structs[s].f[i].bxs1 : 0) :
                        t == 12 ? (int)(((proven_u64)ir->structs[s].f[i].arrmeta >> 32) & 0xffffffu) :
                        t == 13 ? (int)(low_sfield_sgn(&ir->structs[s].f[i]) && ir->structs[s].f[i].size < 8) :
                        (int)(ir->structs[s].f[i].size == 16 && ir->structs[s].f[i].cname.size && ir->structs[s].f[i].cname.ptr[0] == (proven_u8)'i'))));
            low_puts(out, " }");
        }
        low_puts(out, ir->nstructs ? " };\n" : " { 0 } };\n");
    }
    /* ★ 읽기도 **volatile** 이다 — 디바이스 레지스터 읽기는 **부수효과가 있을 수 있고**
       (읽으면 플래그가 지워지는 레지스터가 흔하다), 두 번 읽으면 **두 번 일어나야** 한다.
       뷰 읽기는 이미 바이트 루프라 최적화 여지가 거의 없다 ⇒ **값은 정확성, 비용은 0에 가깝다.** */
    // ★★★ **디바이스 레지스터 접근** (RFC-0042 D1 — 이 RFC 의 첫 문장이 바로 이것이다):
    //   *"각 접근은 정확히 한 번, 쓴 순서대로 일어나야 한다."*
    //
    //   보통 뷰의 필드는 **바이트 루프**로 읽고 쓴다 — 엔디언에 이식성이 있고, 버퍼에는 그게 옳다.
    //   디바이스 레지스터에는 **틀리다**: u32 레지스터에 바이트를 네 번 대는 것은 한 번 대는
    //   것과 **다른 일**이다(많은 주변장치가 폭이 맞는 단일 접근을 요구하고, 부분 쓰기가 래치를
    //   반만 흔든다). 폭·정렬·엔디언은 `E-MMIO-FIELD` 가 선언 자리에서 이미 좁혀 놓았다.
    low_puts(out, "static unsigned long long lw_vld(const unsigned char *p, unsigned sz) {\n"
          "    switch (sz) {\n"
          "    case 1: return (unsigned long long)*(const volatile uint8_t *)p;\n"
          "    case 2: return (unsigned long long)*(const volatile uint16_t *)p;\n"
          "    case 4: return (unsigned long long)*(const volatile uint32_t *)p;\n"
          "    default: return (unsigned long long)*(const volatile uint64_t *)p; } }\n"
          "static void lw_vst(unsigned char *p, unsigned sz, unsigned long long v) {\n"
          "    switch (sz) {\n"
          "    case 1: *(volatile uint8_t *)p = (uint8_t)v; break;\n"
          "    case 2: *(volatile uint16_t *)p = (uint16_t)v; break;\n"
          "    case 4: *(volatile uint32_t *)p = (uint32_t)v; break;\n"
          "    default: *(volatile uint64_t *)p = v; break; } }\n");
    low_puts(out, "static unsigned long long lw_vread(lowv v, int i) {\n"
          "    const volatile unsigned char *p = v.p + lw_st_foff[v.box][i];\n"
          "    unsigned long long x = 0; int sz = lw_st_fsize[v.box][i];\n"
          /* ★ 디바이스면 폭이 맞는 단일 volatile 로드 — 바이트 루프가 아니다. */
          "    if (lw_st_mmio[v.box]) return lw_en_tovi_r(lw_st_felo[v.box][i], lw_st_fehi[v.box][i], lw_vld((const unsigned char *)p, (unsigned)sz));\n"
          "    if (lw_st_fbe[v.box][i]) { for (int k = 0; k < sz; k++) x = (x << 8) | p[k]; }\n"
          "    else { for (int k = sz; k-- > 0; ) x = (x << 8) | p[k]; }\n"
          "    return lw_en_tovi_r(lw_st_felo[v.box][i], lw_st_fehi[v.box][i], x);\n"
          "}\n"
          "static double lw_b2f(unsigned long long b, int sz) {\n"
          "    if (sz == 4) { float f; uint32_t u = (uint32_t)b; memcpy(&f, &u, 4); return (double)f; }\n"
          "    double d; memcpy(&d, &b, 8); return d; }\n"
          /* ★★★ **aggregate 슬라이스 저장** (RFC-0080 — AST 아레나): 레코드의 필드를 원소의
             구조체 레이아웃 오프셋에 인코딩한다(VM lw_istore ISTORE 와 대칭·이름으로 매칭). */
          "static void lw_istore_agg(lowv s, long long k, lowv v) {\n"
          /* ★★★ **읽은 것을 다시 쓸 수 있어야 한다**(2026-07-26): 구조체 슬라이스의 원소를 읽으면
             VIEW 가 나오는데 저장이 REC 만 받아 `set (index s i) (index s j)` — 맞바꾸기가
             불가능했다. VM 과 대칭으로 VIEW 를 받아 바이트를 옮긴다(겹칠 수 있어 memmove). */
          "    unsigned char *dst_ = (unsigned char *)s.p + (size_t)k * (size_t)s.box;\n"
          "    if (v.tag == LWV_VIEW) {\n"
          "        size_t nb_ = (size_t)s.box; if ((size_t)v.n < nb_) nb_ = (size_t)v.n;\n"
          "        memmove(dst_, (const void *)v.p, nb_); return;\n"
          "    }\n"
          "    if (v.tag != LWV_REC) lw_panic(\"aggregate slice store needs a record or a view\");\n"
          "    int sidx = (int)((s.i >> 20) & 0xff);\n"
          "    unsigned char *base = (unsigned char *)s.p + (size_t)k * (size_t)s.box;\n"
          "    for (int b = 0; b < s.box; b++) base[b] = 0;\n"
          "    lowrec *rr = LW_RECV(v);\n"
          "    for (int fi = 0; fi < lw_st_nf[sidx]; fi++)\n"
          "        for (int q = 0; q < rr->nf; q++)\n"
          "            if (strcmp(lw_mk_fields[rr->mk][q], lw_st_fname[sidx][fi]) == 0) {\n"
          "                if (lw_st_farrn[sidx][fi]) { /* T2b-3d: an array field — its bytes go in place */\n"
          "                    size_t need = (size_t)lw_st_farrn[sidx][fi] * (size_t)(lw_st_farrm[sidx][fi] & 0xff); lowv fv = rr->f[q];\n"
          "                    size_t have = fv.tag == LWV_SLICE ? fv.n : fv.tag == LWV_VARRAY ? fv.n * (size_t)fv.box : 0;\n"
          "                    if (have != need) lw_panic(\"an array field takes exactly its length of elements\");\n"
          "                    memmove(base + lw_st_foff[sidx][fi], fv.p, need); break; }\n"
          "                if (lw_st_fslm[sidx][fi]) { /* 2026-10-31: a slice field of a struct in a list (shadow layout) — (address, length) */\n"
          "                    lowv fv = rr->f[q]; const unsigned char *pp_ = fv.p; unsigned long long pn_ = (unsigned long long)fv.n;\n"
          "                    if (fv.tag != LWV_SLICE && fv.tag != LWV_VARRAY) lw_panic(\"a slice field takes a slice\");\n"
          "                    memcpy(base + lw_st_foff[sidx][fi], &pp_, sizeof pp_); memcpy(base + lw_st_foff[sidx][fi] + LW_PTRSLOT, &pn_, 8); break; }\n"
          "                if (lw_st_fsize[sidx][fi] == 16) { lw_put16(base + lw_st_foff[sidx][fi], rr->f[q], lw_st_fbe[sidx][fi]); break; }   /* RFC-0151 §9.4.1 */\n"
          "                unsigned long long x = (rr->f[q].tag == LWV_FLT)\n"
          "                    ? lw_f2b(lw_b2f((unsigned long long)rr->f[q].i, 8), lw_st_fsize[sidx][fi])\n"
          "                    : (unsigned long long)rr->f[q].i;\n"
          "                x = lw_en_tonum_r(lw_st_felo[sidx][fi], lw_st_fehi[sidx][fi], x);\n"
          "                unsigned char *fp = base + lw_st_foff[sidx][fi]; int sz = lw_st_fsize[sidx][fi];\n"
          "                if (lw_st_fbe[sidx][fi]) { for (int c = 0; c < sz; c++) fp[c] = (unsigned char)(x >> (8*(sz-1-c))); }\n"
          "                else { for (int c = 0; c < sz; c++) fp[c] = (unsigned char)(x >> (8*c)); }\n"
          "                break;\n"
          "            }\n"
          "}\n"
          "static unsigned long long lw_f2b(double d, int sz) {\n"
          "    if (sz == 4) { float f = (float)d; uint32_t u; memcpy(&u, &f, 4); return u; }\n"
          "    unsigned long long b; memcpy(&b, &d, 8); return b; }\n"
          /* ★★★ 참조를 따라간다 — 참조가 op 경계를 넘을 수 있게 된 조각(2026-07-19). */
          /*   ☞ **정직하게**: 네이티브에는 프레임 세대도 빌림 스택도 없다(`lw_deref` 도 이미 그렇다). */
          /*     동적 안전(E-VM-DANGLING · E-VM-EXCL · E-VM-READONLY)은 **VM 이 지킨다** — RFC-0017 의 */
          /*     동적 검증기 V1 이 그 역할이고, 네이티브는 정적 분석을 믿는다. 그래서 그 트랩들은 */
          /*     골든에서 **VM 에만** 주장한다(양 백엔드에 주장하면 그것이 거짓말이 된다). */
          "static lowv lw_field(lowv r, int fi) {\n"
          "    if (r.tag == LWV_REF) r = *r.q;\n"
          "    if (r.tag == LWV_VIEW) {\n"
          "        for (int i = 0; i < lw_st_nf[r.box]; i++)\n"
          "            if (strcmp(lw_st_fname[r.box][i], lw_fnames[fi]) == 0) {\n"
          "                /* T2b-3d: an ARRAY field lies inline — hand back a view of its bytes (zero-copy) */\n"
          "                if (lw_st_farrn[r.box][i]) { lowv v = {0}; int m_ = lw_st_farrm[r.box][i], e_ = m_ & 0xff;\n"
          "                    v.p = r.p + lw_st_foff[r.box][i];\n"
          "                    if (m_ & 0x80000) { int rb_ = lw_st_farrb[r.box][i]; /* RFC-0155 §8-3: an array of arrays — a view of its rows */\n"
          "                        v.tag = LWV_VARRAY; v.i = m_ & (0x80000 | 0x30000 | 0xff | 0x0ff00000); v.box = rb_; v.n = rb_ ? (size_t)lw_st_farrn[r.box][i] * (size_t)e_ / (size_t)rb_ : 0; }\n"
          "                    else if (e_ == 1 && !(m_ & 0x30000)) { v.tag = LWV_SLICE; v.n = (size_t)lw_st_farrn[r.box][i]; }\n"
          "                    else { v.tag = LWV_VARRAY; v.i = m_ & 0x30000; v.box = e_; v.n = (size_t)lw_st_farrn[r.box][i]; }\n"
          "                    return v; }\n"
          "                /* RFC-0150 §8-12: a self-owning node field — one address (0 = none) */\n"
          "                if (lw_st_fbx[r.box][i]) { const unsigned char *pp_; memcpy(&pp_, r.p + lw_st_foff[r.box][i], sizeof pp_);\n"
          "                    if (!pp_) { lowv n_ = {0}; n_.tag = LWV_NONE; return n_; }\n"
          "                    if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
          "                    lowv w_ = {0}; w_.tag = LWV_VIEW; w_.p = pp_; w_.box = lw_st_fbx[r.box][i] - 1; w_.n = (size_t)lw_st_total[w_.box];\n"
          "                    lw_boxes[lw_nbox] = w_; lowv o_ = {0}; o_.tag = LWV_SOME; o_.box = lw_nbox++; return o_; }\n"
          "                /* RFC-0135 D13: a SLICE field of a struct built in allocator bytes — (address, length) */\n"
          "                if (lw_st_fslm[r.box][i]) { lowv v = {0}; int m_ = lw_st_fslm[r.box][i], e_ = m_ & 0xff;\n"
          "                    const unsigned char *pp_; unsigned long long pn_; memcpy(&pp_, r.p + lw_st_foff[r.box][i], sizeof pp_); memcpy(&pn_, r.p + lw_st_foff[r.box][i] + LW_PTRSLOT, 8);\n"
          "                    v.p = pp_; v.n = (size_t)pn_;   /* RFC-0137 P3: the pointer is copied as a pointer (aligned), never through an integer */\n"
          "                    if (e_ == 1 && !(m_ & 0x30000)) v.tag = LWV_SLICE; else { v.tag = LWV_VARRAY; v.i = m_ & 0x30000; v.box = e_; }\n"
          "                    return v; }\n"
          "                /* a NESTED struct field: hand back the inner VIEW (zero-copy) */\n"
          "                if (lw_st_fsidx[r.box][i] >= 0) { lowv v = {0}; v.tag = LWV_VIEW;\n"
          "                    v.p = r.p + lw_st_foff[r.box][i]; v.n = (size_t)lw_st_fsize[r.box][i];\n"
          "                    v.box = lw_st_fsidx[r.box][i]; return v; }\n"
          "                if (lw_st_fsize[r.box][i] == 16) return lw_get16(r.p + lw_st_foff[r.box][i], lw_st_fbe[r.box][i]);   /* RFC-0151 §9.4.1 */\n"
          "                return lw_st_fflt[r.box][i] ? lw_flt(lw_b2f(lw_vread(r, i), lw_st_fsize[r.box][i]))\n"
          "                                            : lw_int(lw_st_fsgn[r.box][i] ? (long long)(lw_vread(r, i) << (64 - 8 * lw_st_fsize[r.box][i])) >> (64 - 8 * lw_st_fsize[r.box][i]) : (long long)lw_vread(r, i));\n"
          "            }\n"
          "        lw_panic(\"no such field on this record\");\n"
          "    }\n"
          "    if (r.tag != LWV_REC) lw_panic(\"field access needs a record/view\");\n"
          "    lowrec *rr = LW_RECV(r);\n"
          "    for (int i = 0; i < rr->nf; i++)\n"
          "        if (strcmp(lw_mk_fields[rr->mk][i], lw_fnames[fi]) == 0) return rr->f[i];\n"
          "    lw_panic(\"no such field on this record\");\n"
          "    return lw_int(0);\n"
          "}\n"
          "static inline lowv lw_field0(lowv r, int fi) { /* 2026-11-04: an enum variant's record has only that variant's fields — the rest read as 0 */\n"
          "    if (r.tag == LWV_REF) r = *r.q;\n"
          "    if (r.tag == LWV_REC) { lowrec *rr = LW_RECV(r);\n"
          "        for (int i = 0; i < rr->nf; i++) if (strcmp(lw_mk_fields[rr->mk][i], lw_fnames[fi]) == 0) return rr->f[i];\n"
          "        return lw_int(0); }\n"
          "    return lw_field(r, fi); }\n"
          "static void lw_fstore(lowv r, int fi, lowv v) {\n"
          "    if (r.tag == LWV_REF) { lowv *hs_ = r.q; r = *r.q; if (LW_POOLED(v)) LW_REFW_NOTE(r.tag == LWV_REC && r.box >= 0 ? hs_ : 0); }\n"
          /* ★ 2026-11-11 — 참조 너머의 레코드에 쓴 풀 값(lw_store 의 ★). W-20261111-2031 D4: 풀에 사는 레코드면 그 레코드를 든 칸을 적는다 —
             되감는 틀이 그 칸을 뿌리로 올리면 레코드와 그 칸들이 함께 산다(lw_gc_mark 가 레코드의 칸을 훑는다). 틀 창의 레코드(번호 < 0)는
             부른 쪽 틀의 창이라 이 틀이 못 훑는다 — 전처럼 0(그 뒤로 되감지 않는다). */
          /* ★★★ **뷰의 필드에 쓴다** — 그것이 곧 **디바이스 레지스터 쓰기**다(RFC-0042 D1).
             그리고 **volatile** 이다: 각 접근은 **정확히 한 번**, **쓴 순서대로**.
             `volatile` 이 없으면 컴파일러가 그것을 **지우거나 합치거나 재정렬**하고,
             그러면 **하드웨어가 틀린 일을 한다.** (VM 은 최적화를 안 하니 안 보였다 —
             네이티브에서만 틀렸을 것이다: **가장 나쁜 종류의 결함**이다.) */
          "    if (r.tag == LWV_VIEW) {\n"
          "        for (int i = 0; i < lw_st_nf[r.box]; i++)\n"
          "            if (strcmp(lw_st_fname[r.box][i], lw_fnames[fi]) == 0) {\n"
          "                volatile unsigned char *p = (volatile unsigned char *)(void *)(r.p + lw_st_foff[r.box][i]);\n"
          "                if (lw_st_farrn[r.box][i]) { size_t need_ = (size_t)lw_st_farrn[r.box][i] * (size_t)(lw_st_farrm[r.box][i] & 0xff), have_;\n"
          "                    if (v.tag == LWV_SLICE) have_ = v.n; else if (v.tag == LWV_VARRAY) have_ = v.n * (size_t)v.box; else lw_panic(\"an array field takes an array value\");\n"
          "                    if (have_ != need_) lw_panic(\"an array field takes exactly its length of elements\");\n"
          "                    memmove((void *)p, v.p, need_); return; }\n"
          "                if (lw_st_fbx[r.box][i]) { /* RFC-0150 §8-12: a node field holds one address (0 = none) */\n"
          "                    const unsigned char *pp_ = 0;\n"
          "                    if (v.tag == LWV_SOME) { lowv in_ = lw_boxes[v.box]; if (in_.tag != LWV_VIEW) lw_panic(\"a node field takes a node built in allocator bytes\"); pp_ = in_.p; }\n"
          "                    else if (v.tag != LWV_NONE) lw_panic(\"a node field takes an option\");\n"
          "                    memcpy((void *)p, &pp_, sizeof pp_); return; }\n"
          "                if (lw_st_fslm[r.box][i]) { /* RFC-0135 D13: a slice field holds (address, length) */\n"
          "                    if (v.tag != LWV_SLICE && v.tag != LWV_VARRAY) lw_panic(\"a slice field takes a slice\");\n"
          "                    const unsigned char *pp_ = v.p; unsigned long long pn_ = (unsigned long long)v.n;\n"
          "                    memcpy((void *)p, &pp_, sizeof pp_); memcpy((void *)(p + LW_PTRSLOT), &pn_, 8); return; }\n"
          "                if (lw_st_fsize[r.box][i] == 16) { lw_put16(p, v, lw_st_fbe[r.box][i]); return; }   /* RFC-0151 §9.4.1 */\n"
          "                unsigned long long x = lw_en_tonum_r(lw_st_felo[r.box][i], lw_st_fehi[r.box][i], (unsigned long long)v.i);\n"
          "                int sz = lw_st_fsize[r.box][i];\n"
          /* ★ 디바이스면 폭이 맞는 단일 volatile 저장 — 바이트 네 번은 한 번과 다른 일이다. */
          "                if (lw_st_mmio[r.box]) { lw_vst((unsigned char *)(void *)p, (unsigned)sz, x); return; }\n"
          "                if (lw_st_fbe[r.box][i]) { for (int k = 0; k < sz; k++) p[k] = (unsigned char)(x >> (8*(sz-1-k))); }\n"
          "                else { for (int k = 0; k < sz; k++) p[k] = (unsigned char)(x >> (8*k)); }\n"
          "                return;\n"
          "            }\n"
          "        lw_panic(\"no such register in this block\");\n"
          "    }\n"
          "    if (r.tag != LWV_REC) lw_panic(\"set field needs a record\");\n"
          "    lowrec *rr = LW_RECV(r);\n"
          "    for (int i = 0; i < rr->nf; i++)\n"
          "        if (strcmp(lw_mk_fields[rr->mk][i], lw_fnames[fi]) == 0) { if (!lw_fstore_arr(rr, i, v)) rr->f[i] = v; return; }\n"
          "    lw_panic(\"no such field on this record\");\n"
          "}\n"
          "static int lw_aligned(lowv b, int s) {\n"
          "    int a = lw_st_align[s];\n"
          "    return a <= 1 || ((uintptr_t)(const void *)b.p % (uintptr_t)a) == 0;\n"
          "}\n"
          "static lowv lw_view(lowv b, int s) {\n"
          "    if (b.tag != LWV_SLICE) lw_panic(\"view needs a byte slice\");\n"
          "    if (!lw_aligned(b, s)) lw_panic(\"view: base address violates the struct's align contract\");\n"
          "    if (b.n < (size_t)lw_st_total[s]) lw_panic(\"view: slice shorter than the layout\");\n"
          "    lowv v = {0}; v.tag = LWV_VIEW; v.p = b.p; v.n = b.n; v.box = s; return v;\n"
          "}\n"
          "static lowv lw_tryview(lowv b, int s) {\n"
          "    if (b.tag != LWV_SLICE) lw_panic(\"view needs a byte slice\");\n"
          "    if (b.n < (size_t)lw_st_total[s] || !lw_aligned(b, s)) { lowv n = {0}; n.tag = LWV_NONE; return n; }\n"
          "    for (int q = lw_st_cfb[s]; q < lw_st_cfb[s + 1]; q++)\n"   /* ★ RFC-0150 §8-31 — bool 칸이 0 · 1 이 아니거나 열거 칸의 번호가 어느 갈래의 것도 아니면 none */
          "        if (!lw_cf_ok(q, (const unsigned char *)b.p)) { lowv n = {0}; n.tag = LWV_NONE; return n; }\n"
          "    if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
          "    lw_boxes[lw_nbox] = lw_view(b, s);\n"
          "    lowv r = {0}; r.tag = LWV_SOME; r.box = lw_nbox++; return r;\n"
          "}\n"
          "static lowv lw_isok(lowv v) {\n"
          "    if (v.tag != LWV_OK && v.tag != LWV_ERR) lw_panic(\"is_ok needs a result\");\n"
          "    return lw_int(v.tag == LWV_OK);\n"
          "}\n"
          "static lowv lw_iserr(lowv v) {\n"
          "    if (v.tag != LWV_OK && v.tag != LWV_ERR) lw_panic(\"is_error needs a result\");\n"
          "    return lw_int(v.tag == LWV_ERR);\n"
          "}\n"
          "static lowv lw_okval(lowv v) {\n"
          "    if (v.tag == LWV_ERR) lw_panic(\"ok_value of an error\");\n"
          "    if (v.tag != LWV_OK) lw_panic(\"ok_value needs a result\");\n"
          "    return lw_boxes[v.box];\n"
          "}\n"
          "static lowv lw_errval(lowv v) {\n"
          "    if (v.tag == LWV_OK) lw_panic(\"error_value of an ok\");\n"
          "    if (v.tag != LWV_ERR) lw_panic(\"error_value needs a result\");\n"
          "    return lw_int(v.i);\n"
          "}\n"
          "static lowv lw_issome(lowv v) {\n"
          "    if (v.tag != LWV_SOME && v.tag != LWV_NONE) lw_panic(\"is_some needs an option\");\n"
          "    return lw_int(v.tag == LWV_SOME);\n"
          "}\n"
          "static lowv lw_someval(lowv v) {\n"
          "    if (v.tag == LWV_NONE) lw_panic(\"some_value of none\");\n"
          "    if (v.tag != LWV_SOME) lw_panic(\"some_value needs an option\");\n"
          "    return lw_boxes[v.box];\n"
          "}\n"
          "alignas(64) static unsigned char lw_encbuf[8][64]; static int lw_nenc;\n"
          "static lowv lw_encode(lowv v, int s) {\n"
          "    if (lw_nenc >= 8 || lw_st_total[s] > 64) lw_panic(\"encode buffer pool exhausted (8 buffers x 64 B, never rewound — docs/runtime-pools.md)\");\n"
          "    unsigned char *ob = lw_encbuf[lw_nenc];\n"
          "    memset(ob, 0, (size_t)lw_st_total[s]);\n"
          "    for (int i = 0; i < lw_st_nf[s]; i++) {\n"
          "        unsigned long long x = 0; int found = 0;\n"
          "        if (lw_st_farrn[s][i]) { /* T2b-3d: an array field — copy its bytes into place */\n"
          "            size_t need = (size_t)lw_st_farrn[s][i] * (size_t)(lw_st_farrm[s][i] & 0xff); const unsigned char *src = 0;\n"
          "            if (v.tag == LWV_REC) { lowrec *rr = LW_RECV(v);\n"
          "                for (int j = 0; j < rr->nf && !src; j++) if (strcmp(lw_mk_fields[rr->mk][j], lw_st_fname[s][i]) == 0) {\n"
          "                    lowv fv = rr->f[j]; size_t have = fv.tag == LWV_SLICE ? fv.n : fv.tag == LWV_VARRAY ? fv.n * (size_t)fv.box : 0;\n"
          "                    if (have != need) lw_panic(\"encode: an array field holds the wrong length\"); src = fv.p; } }\n"
          "            else if (v.tag == LWV_VIEW) { for (int j = 0; j < lw_st_nf[v.box] && !src; j++)\n"
          "                if (strcmp(lw_st_fname[v.box][j], lw_st_fname[s][i]) == 0 && (size_t)lw_st_farrn[v.box][j] * (size_t)(lw_st_farrm[v.box][j] & 0xff) == need)\n"
          "                    src = v.p + lw_st_foff[v.box][j]; }\n"
          "            if (!src) lw_panic(\"encode: value lacks an array field of that length\");\n"
          "            memmove(ob + lw_st_foff[s][i], src, need); continue; }\n"
          "        if (lw_st_fsize[s][i] == 16) { lowv w_ = {0}; int g_ = 0;   /* RFC-0151 §9.4.1 — 16 바이트 칸 */\n"
          "            if (v.tag == LWV_REC) { lowrec *rr = LW_RECV(v);\n"
          "                for (int j = 0; j < rr->nf && !g_; j++) if (strcmp(lw_mk_fields[rr->mk][j], lw_st_fname[s][i]) == 0) { w_ = rr->f[j]; g_ = 1; } }\n"
          "            else if (v.tag == LWV_VIEW) { for (int j = 0; j < lw_st_nf[v.box] && !g_; j++)\n"
          "                if (strcmp(lw_st_fname[v.box][j], lw_st_fname[s][i]) == 0 && lw_st_fsize[v.box][j] == 16) { w_ = lw_get16(v.p + lw_st_foff[v.box][j], lw_st_fbe[v.box][j]); g_ = 1; } }\n"
          "            if (!g_) lw_panic(\"encode: value lacks a 16-byte layout field\");\n"
          "            lw_put16(ob + lw_st_foff[s][i], w_, lw_st_fbe[s][i]); continue; }\n"
          "        if (v.tag == LWV_REC) {\n"
          "            lowrec *rr = LW_RECV(v);\n"
          "            for (int j = 0; j < rr->nf && !found; j++)\n"
          "                if (strcmp(lw_mk_fields[rr->mk][j], lw_st_fname[s][i]) == 0) {\n"
          "                    if (lw_st_fflt[s][i]) {\n"
          "                        double d = rr->f[j].tag == LWV_FLT ? lw_fval(rr->f[j])\n"
          "                                 : rr->f[j].tag == LWV_INT ? (double)rr->f[j].i\n"
          "                                 : (lw_panic(\"encode: float field needs a number\"), 0.0);\n"
          "                        x = lw_f2b(d, lw_st_fsize[s][i]);\n"
          "                    } else {\n"
          "                        if (rr->f[j].tag != LWV_INT) lw_panic(\"encode needs integer fields\");\n"
          "                        x = (unsigned long long)rr->f[j].i;\n"
          "                    }\n"
          "                    found = 1;\n"
          "                }\n"
          "        } else if (v.tag == LWV_VIEW) {\n"
          "            for (int j = 0; j < lw_st_nf[v.box] && !found; j++)\n"
          "                if (strcmp(lw_st_fname[v.box][j], lw_st_fname[s][i]) == 0) { x = lw_vread(v, j);\n"
          "                    if (lw_st_fflt[s][i] && lw_st_fflt[v.box][j] && lw_st_fsize[s][i] != lw_st_fsize[v.box][j])\n"
          "                        x = lw_f2b(lw_b2f(x, lw_st_fsize[v.box][j]), lw_st_fsize[s][i]);\n"
          "                    found = 1; }\n"
          "        } else lw_panic(\"encode needs a record/view\");\n"
          "        if (!found) lw_panic(\"encode: value lacks a layout field\");\n"
          "        x = lw_en_tonum_r(lw_st_felo[s][i], lw_st_fehi[s][i], x);\n"
          "        unsigned char *p = ob + lw_st_foff[s][i]; int sz = lw_st_fsize[s][i];\n"
          "        if (lw_st_fbe[s][i]) { for (int k = sz; k-- > 0; ) { p[k] = (unsigned char)x; x >>= 8; } }\n"
          "        else { for (int k = 0; k < sz; k++) { p[k] = (unsigned char)x; x >>= 8; } }\n"
          "    }\n"
          "    lowv r = {0}; r.tag = LWV_SLICE; r.p = ob; r.n = (size_t)lw_st_total[s]; lw_nenc++;\n"
          "    return r;\n"
          "}\n");
    // ★ 값 렌더링은 **stdio** 다(snprintf). 프리스탠딩엔 찍을 콘솔이 없으므로 스텁을 낸다 —
    //   몸이 부르더라도 링크는 된다(그리고 아무것도 안 쓴다).
    if (freestanding)
        low_puts(out, "static void lw_render(lowv v, char *out, size_t cap) { (void)v; if (cap) out[0] = 0; }\n");
    else
        low_puts(out, LW_RENDER);

    // string literal table (raw bytes — 접두 리터럴의 코드 유닛도 **리틀엔디언 바이트**다)
    //   ★ 원소 타입 배열(`uint16_t lw_strN[]`)로 내지 **않는다**: 런타임은 타입 있는 슬라이스를
    //     `lw_ld_le` 로 읽으므로(VM 은 같은 뜻의 `(x<<8)|p[k]`), 바이트가 곧 규약이다.
    //     원소 타입으로 내면 빅엔디안 타깃에서 C 가 제 순서로 깔고 lw_ld_le 는 LE 로 읽어 **갈린다**.
    for (proven_size_t s = 0; s < ir->nstrs; s++) {
        if (ir->strrel && ir->strrel[s]) continue;   // 주소를 품은 상수는 아래(가리키는 항목이 먼저 서야 한다)
        low_fpf(out, "static const unsigned char lw_str{}[] = {{", PROVEN_ARG((size_t)(s)));
        for (proven_size_t i = 0; i < ir->strs[s].size; i++)
            low_fpf(out, "{}{}", proven_arg_cstr(i ? "," : " "), PROVEN_ARG((unsigned)((unsigned)ir->strs[s].ptr[i])));
        low_puts(out, ir->strs[s].size ? ", 0 };\n" : " 0 };\n");
    }
    // ★★ RFC-0015 §6.3 (2026-10-31 · W-20261031-0342) — **주소를 품은 상수**: 번역 때 돈 `fn` 이 낸 구조체 배열의 슬라이스 칸(주소 + 길이).
    //   주소 자리는 C 의 주소 상수로 적는다 — 정적 초기값이라 링커가 옮겨 주고, 포인터 폭은 대상의 것이다(32 비트 대상은 주소 4 바이트 뒤에 채움).
    //   나머지 바이트는 VM 이 지은 그대로다. 바이트로 읽으므로(memcpy) 칸 사이를 C 가 채우지 않게 packed 로 둔다.
    for (proven_size_t s = 0; s < ir->nstrs; s++) {
        if (!ir->strrel || !ir->strrel[s]) continue;
        const unsigned slot = low_ptr_slot(), pb = low_ir_target()->ptr_bytes ? (unsigned)low_ir_target()->ptr_bytes : 8u;
        proven_size_t cur = 0, m = 0;
        low_fpf(out, "static const struct __attribute__((packed, aligned(8))) {{");
        for (proven_size_t r = 0; r < ir->nsrel; r++) {   // 표는 칸 차례로 적혀 있다(답의 원소 · 칸 차례)
            if (ir->srel[r].si != (proven_u32)s) continue;
            if (ir->srel[r].off > cur) low_fpf(out, " unsigned char b{}[{}];", PROVEN_ARG((size_t)(m)), PROVEN_ARG((size_t)((size_t)(ir->srel[r].off - cur))));
            low_fpf(out, " const unsigned char *p{};", PROVEN_ARG((size_t)(m)));
            if (slot > pb) low_fpf(out, " unsigned char q{}[{}];", PROVEN_ARG((size_t)(m)), PROVEN_ARG((unsigned)(slot - pb)));
            cur = ir->srel[r].off + slot; m++;
        }
        if (ir->strs[s].size > cur) low_fpf(out, " unsigned char b{}[{}];", PROVEN_ARG((size_t)(m)), PROVEN_ARG((size_t)((size_t)(ir->strs[s].size - cur))));
        low_fpf(out, " }} lw_rel{} = {{", PROVEN_ARG((size_t)(s)));
        cur = 0; bool first = true;
        #define LW_RB(from, to) do { low_puts(out, first ? " {" : ", {"); first = false; \
            for (proven_size_t i_ = (from); i_ < (to); i_++) low_fpf(out, "{}{}", proven_arg_cstr(i_ > (from) ? "," : " "), PROVEN_ARG((unsigned)((unsigned)ir->strs[s].ptr[i_]))); low_puts(out, " }"); } while (0)
        for (proven_size_t r = 0; r < ir->nsrel; r++) {
            if (ir->srel[r].si != (proven_u32)s) continue;
            if (ir->srel[r].off > cur) LW_RB(cur, ir->srel[r].off);
            low_fpf(out, "{} lw_str{}", proven_arg_cstr(first ? "" : ","), PROVEN_ARG((unsigned)((unsigned)ir->srel[r].tsi))); first = false;
            if (slot > pb) low_puts(out, ", { 0 }");
            cur = ir->srel[r].off + slot;
        }
        if (ir->strs[s].size > cur) LW_RB(cur, ir->strs[s].size);
        #undef LW_RB
        low_puts(out, " };\n");
        // 배치가 VM 이 지은 바이트와 한 바이트도 다르지 않아야 한다 — 대상의 포인터 폭이 자리(LW_PTRSLOT)를 넘으면 번역이 멈춘다
        low_fpf(out, "_Static_assert(sizeof lw_rel{} == {}, \"a constant holding addresses must keep its layout\");\n", PROVEN_ARG((size_t)(s)), PROVEN_ARG((size_t)((size_t)ir->strs[s].size)));
    }
    // ★ `n` 은 **원소 수**다(바이트 수가 아니다) — `len u"AB"` 가 2 인 이유가 여기 있다.
    //   `w` 는 원소폭: 슬라이스를 바이트로 되돌릴 때 곱해진다.
    low_puts(out, "static const struct { const unsigned char *p; size_t n; unsigned w; } lw_strs[] = {");
    for (proven_size_t s = 0; s < ir->nstrs; s++) {
        proven_u8 ew = ir->strew ? ir->strew[s] : 1;
        if (ir->strrel && ir->strrel[s])
            low_fpf(out, "{}{{ (const unsigned char *)(const void *)&lw_rel{}, {}, {} }}", proven_arg_cstr(s ? ", " : " "), PROVEN_ARG((size_t)(s)), PROVEN_ARG((size_t)(ir->strs[s].size / (ew ? ew : 1))), PROVEN_ARG((unsigned)((unsigned)ew)));
        else
        low_fpf(out, "{}{{ lw_str{}, {}, {} }}",
                proven_arg_cstr(s ? ", " : " "), PROVEN_ARG((size_t)(s)), PROVEN_ARG((size_t)(ir->strs[s].size / (ew ? ew : 1))), PROVEN_ARG((unsigned)((unsigned)ew)));
    }
    low_puts(out, ir->nstrs ? " };\n" : " { 0, 0, 1 } };\n");
    // ★ 폭 1 = 여태처럼 바이트 슬라이스. 폭 2·4 = **타입 있는 배열**(VM 의 VMV_VARRAY 와 같은
    //   모양) — 새 값 종류를 만들지 않는다.
    low_puts(out, "static lowv lw_strv(int i) { lowv v = {0}; v.p = lw_strs[i].p; v.n = lw_strs[i].n;\n"
          "    if (lw_strs[i].w <= 1) { v.tag = LWV_SLICE; } else { v.tag = LWV_VARRAY; v.box = (int)lw_strs[i].w; }\n"
          "    return v; }\n");
    // ★★ RFC-0132 T2b-3b — **배열 칸의 표**: (make 번호, 칸 자리, 원소 수, 원소 폭, 부동·부호). VM 의 vm_arr_* 와 같은 규칙.
    low_puts(out, "static const struct { int mk, slot, n, esz; long long meta; } lw_arrf[] = {");
    for (proven_size_t mk = 0; mk < ir->nmakes; mk++)
        for (proven_size_t s = 0; s < ir->nstructs; s++) {
            if (!proven_u8str_view_eq(ir->structs[s].name, ir->makes[mk].type_name)) continue;
            for (proven_size_t q = 0; q < ir->makes[mk].nfields; q++)
                for (proven_size_t z = 0; z < ir->structs[s].nf; z++)
                    if (ir->structs[s].f[z].arrn && proven_u8str_view_eq(ir->structs[s].f[z].name, ir->makes[mk].fields[q]))
                        low_fpf(out, " {{{}, {}, {}, {}, {}LL}},", PROVEN_ARG((size_t)((size_t)mk)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((unsigned)((unsigned)ir->structs[s].f[z].arrn)),
                                PROVEN_ARG((unsigned)((unsigned)ir->structs[s].f[z].arresz)),
                                PROVEN_ARG((long long)((long long)((ir->structs[s].f[z].arrmeta & IR_ROW_BIT)   // ★ RFC-0155 §8-3 — 배열 안의 배열 칸은 줄 표시 · 원소 폭 · 줄 바이트도 싣는다
                                            ? (ir->structs[s].f[z].arrmeta & (IR_ROW_BIT | 0x30000 | 0xff | IR_ROWSUB_MASK | ((proven_i64)0xffffff << 32)))
                                            : (ir->structs[s].f[z].arrmeta & 0x30000)))));
            break;
        }
    // ★ 배열 칸 바이트의 최대(한 레코드) — 프리스탠딩은 힙이 없으므로 자리마다 이만큼의 정적 버퍼를 둔다.
    size_t rbmax = 0;
    for (proven_size_t s2 = 0; s2 < ir->nstructs; s2++) {
        size_t t2 = 0;
        for (proven_size_t z = 0; z < ir->structs[s2].nf; z++)
            if (ir->structs[s2].f[z].arrn) t2 += (((size_t)ir->structs[s2].f[z].arrn * ir->structs[s2].f[z].arresz) + 7u) & ~(size_t)7u;
        if (t2 > rbmax) rbmax = t2;
    }
    // ★ X-0083 — make 마다 **값 구조체**인가(안에 들면 베낀다). VM 의 vm_mk_is_value 와 같은 가름.
    low_puts(out, " {-1, 0, 0, 0, 0} };\nstatic const unsigned char lw_mkval[] = {");
    for (proven_size_t mk = 0; mk < ir->nmakes; mk++) {
        int val = 0;
        for (proven_size_t s = 0; s < ir->nstructs; s++) {
            const low_ir_struct_t *st = &ir->structs[s];
            if (!proven_u8str_view_eq(st->name, ir->makes[mk].type_name)) continue;
            val = !(st->is_actor_state || st->is_mmio || st->is_reserve);
            for (proven_size_t q = 0; q < st->nf; q++) if (st->f[q].owned) val = 0;
            break;
        }
        low_fpf(out, "{},", PROVEN_ARG((int)(val)));
    }
    low_puts(out, " 0 };\n");
    if (rbmax) low_fpf(out, "#ifdef LW_FREESTANDING\nstatic unsigned char lw_rbuf[LW_RECPOOL][{}] __attribute__((aligned(8)));\n#endif\n", PROVEN_ARG((size_t)(rbmax)));
    if (!rbmax) {   // 배열 칸이 없다 — 힙에 닿는 코드를 아예 내지 않는다(프리스탠딩 프로필이 호스트 기호를 세어 거절한다)
        low_puts(out, "static void lw_rec_arrs(lowrec *r, int mk) { (void)r; (void)mk; }\n"
              "static lowv lw_rec_new(lw_fwin *w, lowrec **out) { lowv v = {0}; v.tag = LWV_REC;\n"
              "    if (w && w->n < w->cap) { lowrec *r = &w->r[w->n++]; *out = r; v.box = -1; v.p = (const unsigned char *)(void *)r; return v; }\n"
              "    if (lw_nrec >= lw_reclim) lw_panic(\"record pool exhausted — raise it with -DLW_RECPOOL=N (docs/runtime-pools.md)\");\n"
              "    v.box = lw_nrec++; *out = &lw_recs[v.box]; return v; }\n"
              "static lowv lw_rec_clone_w(lowv v, int depth, lw_fwin *w) {\n"
              "    if (v.tag != LWV_REC || !lw_mkval[LW_RECV(v)->mk]) return v;\n"
              "    if (depth > 16) lw_panic(\"records nested too deep to copy\");\n"
              "    lowrec *s = LW_RECV(v), *d; lowv nv = lw_rec_new(w, &d);\n"
              "    d->mk = s->mk; d->nf = s->nf; memcpy(d->f, s->f, sizeof(lowv) * (size_t)s->nf);\n"
              "    for (int f = 0; f < d->nf; f++) d->f[f] = lw_rec_clone_w(d->f[f], depth + 1, w);\n"
              "    return nv; }\n"
              "static lowv lw_rec_clone(lowv v, int depth) { return lw_rec_clone_w(v, depth, 0); }\n"
              "static void lw_rec_arrays_w(lowrec *r, int mk, lw_fwin *w) {\n"
              "    for (int f = 0; f < r->nf; f++) r->f[f] = lw_rec_clone_w(r->f[f], 1, w);\n"
              "    (void)mk; }\n"
              "static void lw_rec_arrays(lowrec *r, int mk) { lw_rec_arrays_w(r, mk, 0); }\n"
              "static int lw_fstore_arr(lowrec *r, int slot, lowv v) { (void)r; (void)slot; (void)v; return 0; }\n");
    } else low_puts(out, "static lowv lw_arr_view(int k, unsigned char *p) { lowv v = {0}; v.p = p;\n"
          "    if (lw_arrf[k].meta & 0x80000) { size_t rb_ = (size_t)((lw_arrf[k].meta >> 32) & 0xffffff); /* RFC-0155 §8-3: rows */\n"
          "        v.tag = LWV_VARRAY; v.i = lw_arrf[k].meta & (0x80000 | 0x30000 | 0xff | 0x0ff00000); v.box = (int)rb_; v.n = rb_ ? (size_t)lw_arrf[k].n * (size_t)lw_arrf[k].esz / rb_ : 0; }\n"
          "    else if (lw_arrf[k].esz == 1 && !lw_arrf[k].meta) { v.tag = LWV_SLICE; v.n = (size_t)lw_arrf[k].n; }\n"
          "    else { v.tag = LWV_VARRAY; v.n = (size_t)lw_arrf[k].n; v.box = lw_arrf[k].esz; v.i = lw_arrf[k].meta; }\n"
          "    return v; }\n"
          "static void lw_arr_copy(int k, lowv s, unsigned char *dst) { size_t need = (size_t)lw_arrf[k].n * (size_t)lw_arrf[k].esz, have = 0;\n"
          "    if (s.tag == LWV_SLICE) have = s.n; else if (s.tag == LWV_VARRAY) have = s.n * (size_t)s.box; else lw_panic(\"an array field takes an array value\");\n"
          "    if (have != need) lw_panic(\"an array field takes exactly its length of elements\");\n"
          "    memmove(dst, (const void *)s.p, need); }\n"
          "static void lw_rec_arrs(lowrec *r, int mk);\n"
          "static lowv lw_rec_new(lw_fwin *w, lowrec **out) { lowv v = {0}; v.tag = LWV_REC;\n"
          "    if (w && w->n < w->cap) { lowrec *r = &w->r[w->n++]; *out = r; v.box = -1; v.p = (const unsigned char *)(void *)r; return v; }\n"
          "    if (lw_nrec >= lw_reclim) lw_panic(\"record pool exhausted — raise it with -DLW_RECPOOL=N (docs/runtime-pools.md)\");\n"
          "    v.box = lw_nrec++; *out = &lw_recs[v.box]; return v; }\n"
          "static lowv lw_rec_clone_w(lowv v, int depth, lw_fwin *w) {\n"
          "    if (v.tag != LWV_REC || !lw_mkval[LW_RECV(v)->mk]) return v;\n"
          "    if (depth > 16) lw_panic(\"records nested too deep to copy\");\n"
          "    lowrec *s = LW_RECV(v), *d; lowv nv = lw_rec_new(w, &d);\n"
          "    d->mk = s->mk; d->nf = s->nf; memcpy(d->f, s->f, sizeof(lowv) * (size_t)s->nf);\n"
          "    for (int f = 0; f < d->nf; f++) d->f[f] = lw_rec_clone_w(d->f[f], depth + 1, w);\n"
          "    lw_rec_arrs(d, d->mk); return nv; }\n"
          "static lowv lw_rec_clone(lowv v, int depth) { return lw_rec_clone_w(v, depth, 0); }\n"
          "static void lw_rec_arrays_w(lowrec *r, int mk, lw_fwin *w) {\n"
          "    for (int f = 0; f < r->nf; f++) r->f[f] = lw_rec_clone_w(r->f[f], 1, w);\n"
          "    lw_rec_arrs(r, mk); }\n"
          "static void lw_rec_arrays(lowrec *r, int mk) { lw_rec_arrays_w(r, mk, 0); }\n"
          "static void lw_rec_arrs(lowrec *r, int mk) { size_t tot = 0; int any = 0;\n"
          "    for (int k = 0; lw_arrf[k].mk >= 0; k++) if (lw_arrf[k].mk == mk) { tot += (((size_t)lw_arrf[k].n * (size_t)lw_arrf[k].esz) + 7u) & ~(size_t)7u; any = 1; }\n"
          "    if (!any) return;\n"
          "    if (tot > r->rbc) {\n"
          "#ifdef LW_FREESTANDING\n"
          "        r->rb = lw_rbuf[r - lw_recs]; r->rbc = sizeof lw_rbuf[0];   /* 힙이 없다 — 자리마다 정적 버퍼 */\n"
          "        if (tot > r->rbc) lw_panic(\"a struct's array fields do not fit the record buffer\");\n"
          "#else\n"
          "        unsigned char *nb = (unsigned char *)realloc(r->rb, tot); if (!nb) lw_panic(\"out of memory for a struct's array fields\"); r->rb = nb; r->rbc = tot;\n"
          "#endif\n"
          "    }\n"
          "    size_t off = 0;\n"
          "    for (int k = 0; lw_arrf[k].mk >= 0; k++) if (lw_arrf[k].mk == mk) {\n"
          "        lw_arr_copy(k, r->f[lw_arrf[k].slot], r->rb + off); r->f[lw_arrf[k].slot] = lw_arr_view(k, r->rb + off);\n"
          "        off += (((size_t)lw_arrf[k].n * (size_t)lw_arrf[k].esz) + 7u) & ~(size_t)7u; } }\n"
          "static int lw_fstore_arr(lowrec *r, int slot, lowv v) {\n"
          "    for (int k = 0; lw_arrf[k].mk >= 0; k++) if (lw_arrf[k].mk == r->mk && lw_arrf[k].slot == slot) {\n"
          "        lw_arr_copy(k, v, (unsigned char *)(void *)r->f[slot].p); return 1; }\n"
          "    return 0; }\n");
    // ★★★ RFC-0132 P4e (§14.8) — **틀 창 · 올리기 · 되튐**. 창의 레코드는 그 op 의 C 틀에 산다. 밖으로 나가는 길(박스 · 그릇 · 통로 ·
    //   편지 · 태스크 · 남의 레코드 칸)에서는 풀로 올리고(lw_promote), 돌려줄 때는 되튐 창으로 베껴 부른 쪽이 제 창(없으면 풀)으로 옮긴다.
    //   ☞ 창 레코드의 바이트 버퍼는 틀이 준다(rbc = 이 단위의 가장 큰 배열 칸 몫) — lw_rec_arrs 의 늘리기 갈래에 닿지 않는다.
    low_fpf(out, "#define LW_RBMAX {}\n", PROVEN_ARG((size_t)(rbmax ? rbmax : (size_t)1)));
    low_puts(out, "static _Thread_local unsigned char lw_bnb[17][LW_RBMAX] __attribute__((aligned(8)));\n"
          "static void lw_win_init(lw_fwin *w, lowrec *r, unsigned char *rb, int cap) { w->r = r; w->n = 0; w->cap = cap;\n"
          "    for (int i = 0; i < cap; i++) { r[i].rb = rb + (size_t)i * LW_RBMAX; r[i].rbc = LW_RBMAX; } }\n"
          "static lowv lw_make_w(lw_fwin *w, int mk, int nf, const lowv *vals) {\n"
          "    if (!lw_mkval[mk]) w = 0;\n"
          "    lowrec *r; lowv v = lw_rec_new(w, &r);\n"
          "    r->mk = mk; r->nf = nf; memcpy(r->f, vals, sizeof(lowv) * (size_t)nf); lw_rec_arrays_w(r, mk, w);\n"
          "    return v; }\n"
          "static lowv lw_promote(lowv v) { return (v.tag == LWV_REC && v.box < 0) ? lw_rec_clone_w(v, 1, 0) : v; }\n"
          "static lowv lw_fw_ret(lw_fwin *w, lowv v) {\n"
          "    if (!lw_in_win(w, v)) return v;\n"
          "    if (!lw_bnw.r) lw_win_init(&lw_bnw, lw_bnr, &lw_bnb[0][0], 17);\n"
          "    lw_bnw.n = 0; return lw_rec_clone_w(v, 1, &lw_bnw); }\n"
          "static lowv lw_fw_take(lw_fwin *w, lowv v) { return lw_in_win(&lw_bnw, v) ? lw_rec_clone_w(v, 1, w) : v; }\n"
          "static lowv lw_fw_keep(lw_fwin *w, lowv r, lowv v) {\n"
          "    if (r.tag == LWV_REF) r = *r.q;\n"
          "    if (r.tag == LWV_REC && r.box < 0 && lw_in_win(w, r)) return v;\n"
          "    if (v.tag == LWV_VEC && r.tag == LWV_REC && r.box < 0) lw_vec_pinned = 1;\n"
          "    return lw_promote(v); }\n"
          "static signed char lw_fwmk_[64]; static int lw_fwmap_[64];\n"
          "static void lw_fw_mark(lw_fwin *w, lowv v) { if (!lw_in_win(w, v)) return;\n"
          "    int i = (int)((const lowrec *)(const void *)v.p - w->r); if (lw_fwmk_[i]) return; lw_fwmk_[i] = 1;\n"
          "    for (int f = 0; f < w->r[i].nf; f++) lw_fw_mark(w, w->r[i].f[f]); }\n"
          "static void lw_fw_fix(lw_fwin *w, int n0, lowv *v) {\n"
          "    if (v->tag != LWV_REC || v->box >= 0) return;\n"
          "    const lowrec *r = (const lowrec *)(const void *)v->p; if (r < w->r || r >= w->r + n0) return;\n"
          "    v->p = (const unsigned char *)(void *)&w->r[lw_fwmap_[r - w->r]]; }\n"
          "static void lw_fw_gc(lw_fwin *w, lowv *loc, int nloc, lowv *st, int sp) {\n"
          "    if (!w->n) return;\n"
          "    int n0 = w->n; for (int i = 0; i < n0; i++) lw_fwmk_[i] = 0;\n"
          "    for (int i = 0; i < nloc; i++) lw_fw_mark(w, loc[i]);\n"
          "    for (int i = 0; i < sp; i++) lw_fw_mark(w, st[i]);\n"
          "    int top = 0; for (int i = 0; i < n0; i++) lw_fwmap_[i] = lw_fwmk_[i] ? top++ : -1;\n"
          "    for (int i = 0; i < n0; i++) if (lw_fwmap_[i] >= 0 && lw_fwmap_[i] != i) {\n"
          "        lowrec *d = &w->r[lw_fwmap_[i]]; unsigned char *ob = d->rb; size_t oc = d->rbc;\n"
          "        *d = w->r[i]; w->r[i].rb = ob; w->r[i].rbc = oc; }\n"
          "    for (int i = 0; i < nloc; i++) lw_fw_fix(w, n0, &loc[i]);\n"
          "    for (int i = 0; i < sp; i++) lw_fw_fix(w, n0, &st[i]);\n"
          "    for (int i = 0; i < top; i++) for (int f = 0; f < w->r[i].nf; f++) lw_fw_fix(w, n0, &w->r[i].f[f]);\n"
          "    w->n = top; }\n");
    // ★★★ RFC-0042 §8-2 — **레지스터 블록을 자기 기저 주소에서** (태그 경로). 주소는 컴파일 상수다.
    //   ★ RFC-0137 P4 — 기저는 **포인터**로 받는다(장치 주소는 LW_MMIO_PTR 이 만들고, 예약 블록은 배열 그대로). 정수를 거치지 않는다.
    low_puts(out, "static lowv lw_mmioblk(void *base, unsigned n) {\n"
          "    lowv v = {0}; v.tag = LWV_SLICE; v.p = (unsigned char *)base; v.n = n; return v; }\n");

    // forward declarations (recursion / any call order)
    // ★★★ **C 의 프로토타입** — `extern` op 마다(RFC-0063).
    //   ★ C 헤더를 `#include` 하지 않는다: **우리가 선언한 시그니처가 계약**이고,
    //     그것이 틀리면 **링커가 아니라 우리가 틀린 것**이다. 좁고 정직한 사상만 낸다.
    for (proven_size_t i = 0; i < ir->ndefs; i++)
        if (ir->defs[i].is_extern) cbe_extern_proto(ir, &ir->defs[i], out);

    // ★★★ **증명한 것을 말한다** — 그리고 **아는 타입으로 내린다.**
    //   ☞ 표는 이제 **`low_cbe_plan` 이 채운다**(RFC-0089 R1): 헤더 방출도 같은 것을 읽으므로
    //     판정이 두 벌로 갈릴 수가 없다. 여기서는 **읽기만** 한다.
    low_cbe_plan(ir);
    g_vecfn_ir = ir; cbe_vecfn_all(ir);   // ★ 벡터 판정을 호출 그래프의 고정점으로(F2)
    cbe_mark_depth(ir);            // ★ 원장이 **필요한** def 을 먼저 가린다
    // ★★★ **마스크는 본문을 보고 정한다 — 그래서 본문을 먼저 한 번 만든다.**
    //   호출 자리는 **피호출자의** 마스크를 알아야 하므로, 정의를 내리기 전에 전부 구해 둔다.
    //   이 헛도는 패스에서는 모두 LWA_ALL 이라 본문의 **자기 파라미터 사용**은 그대로 드러난다
    //   (호출 자리 글자는 `ci`·`cs`… 라 자기 파라미터 이름과 안 겹친다).
    for (proven_size_t i2 = 0; i2 < ir->ndefs && i2 < 512; i2++) g_argmask[i2] = LWA_ALL;
    for (proven_size_t i2 = 0; i2 < ir->ndefs && i2 < 512; i2++) {
        if (!sc_tab[i2]) continue;
        low_out_t *pm = low_out_mem();          // ★ W-20261113-0024 — proven 메모리 통로(전엔 open_memstream)
        if (!pm) continue;                       // 못 잡으면 전부 넘긴다 — 느릴 뿐 틀리지 않는다
        proven_size_t save = g_cur_di; g_cur_di = i2;
        cbe_scalar_body(ir, &ir->defs[i2], pm);
        g_cur_di = save;
        if (!low_out_failed(pm)) g_argmask[i2] = cbe_argmask_of((const char *)low_out_mem_view(pm).ptr);
        (void)low_out_close(pm);
        if (!g_argmask[i2]) g_argmask[i2] = LWA_AI;   // 빈 목록은 안 만든다(호출 표기가 단순해진다)
    }

    for (proven_size_t i = 0; i < ir->ndefs; i++) {
        if (!cbe_emittable(&ir->defs[i])) continue;
        low_puts(out, "static lowv lw_op_"); put_sym(out, &ir->defs[i]);
        // ★ `restrict` 는 **표준 C99** 다 — 방언이 아니다. 그리고 이 배열은 op 이 **혼자
        //   읽는다**(호출자가 스택에 세워 넘긴다) ⇒ 겹칠 수가 없다. 그 사실을 말한다.
        low_puts(out, "(const lowv *restrict a);\n");
        // ★ **export 함수 전방 선언** — `unsafe_fn <op>` 의 `&<op>` 가 그 export 함수의 정의보다
        //   먼저 나올 수 있다(콜백을 넘기는 op 이 위에서 하강한다). 그래서 여기서 미리 선언한다.
        if (low_cbe_plan_of(i)->exported_symbol) {   // ★ 같은 기록(RFC-0089 R1) — 세 번째 독자다
            const low_ir_def_t *d = &ir->defs[i];
            cbe_put_export_sig(out, d, low_cbe_plan_of(i), false);   // ★ RFC-0132 P4d — 서명은 한 곳에서
            low_puts(out, ";\n");
        }
        if (i < 512 && sc_tab[i]) {
            // ★★★ **작은 순수 op 은 `inline` 이라고 말해 준다** (2026-08-29, 실측).
            //   방출된 빠른 경로는 C 컴파일러가 **인라인하기에는 크다고 판단**하는 모양이었다:
            //   같은 프로그램에서 op 하나에 `inline` 을 붙이자 **78 → 40 ms** 였다(같은 답).
            //   `always_inline` 은 안 쓴다 — 같은 값을 내면서(41 ms) 코드 팽창의 위험만 진다.
            //   ☞ 이것은 **힌트**다: 무엇을 인라인할지는 C 컴파일러의 비용 모형이 정한다.
            low_puts(out, cbe_inline_hint(i) ? "static inline " : "static ");
            low_puts(out, cbe_ret_scalar(i) ? "long long lw_s_" : "lw_r lw_s_"); put_sym(out, &ir->defs[i]);
            low_puts(out, "("); cbe_put_params(out, i < 512 ? g_argmask[i] : LWA_ALL, cbe_depparam(i)); low_puts(out, ");\n");
            if (g_qthunk[i]) { low_puts(out, "static void lw_q_"); put_sym(out, &ir->defs[i]); low_puts(out, "(unsigned char *self_, const lowv *a_) LW_HNOINL;\n"); }
        }
    }


    int emitted = 0;
    for (proven_size_t i = 0; i < ir->ndefs; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        // ★ **정본이 이미 찍었다** (RFC-0012 dedup) — 본문만 건너뛴다. 이 op 은 사라지지 않는다:
        //   디스패치 표에 이름이 남고 정본 함수를 가리키므로 네이티브가 부를 수 있고 차등도 본다.
        if (d->canon_of) continue;
        if (!cbe_emittable(d)) {
            // ★★ 건너뛴다는 사실을 **생성된 C 파일 안의 주석**으로만 적고 있었다 —
            //   **아무도 읽지 않는다.** 그러면 네이티브 바이너리에 그 op 이 **없고**,
            //   부르면 "없는 op" 이라고 한다. 조용히 빠지는 것이 죄다(PRINCIPLES.md §0 교훈 2).
            //   ⇒ **말한다.** (그리고 이것은 차등 검증의 **사각지대**이기도 하다:
            //     VM 에는 있고 네이티브에는 없는 op 은 두 실행을 비교할 수가 없다.)
            low_puts(out, "/* skipped (not in the C backend yet): "); put_view(out, d->name); low_puts(out, " */\n");
            low_puts(low_stderr(), "lowentc: W-CBE-SKIP: op '");
            low_write(low_stderr(), d->name.ptr, d->name.size);
            low_fpf(low_stderr(), "' is NOT in the native build ({}) — calling it on the native binary "
                            "will say \"no such op\", and the VM/native differential cannot see it\n",
                    proven_arg_cstr(!d->lowered ? "body outside the S5 core"
                                : "uses an op this backend does not emit yet"));
            continue;
        }
        // branch targets need labels
        // ★ 태그 경로의 분기 목표 표도 **명령 수만큼** (X-0028 · WO-0205). 4096 은 판단이 아니었다.
        bool *target = (bool *)low_zalloc(d->ncode + 1, sizeof *target);   // ★ W-20261112-0207 — proven 힙
        if (!target) { low_puts(low_stderr(), "lowentc: out of memory\n"); return -1; }
        for (proven_size_t j = 0; j < d->ncode; j++)
            if ((d->code[j].w == IRW_BR || d->code[j].w == IRW_BRZ) && (proven_size_t)d->code[j].a < d->ncode)
                target[d->code[j].a] = true;

        // ★★ **`pure` 는 강제된 계약 위에서만 나간다**: `fn`(검사된 순수성) 이면서
        //   **트랩할 수 없는** op. 트랩은 관찰 가능한 일이고, `pure` 는 컴파일러에게
        //   *"결과를 안 쓰면 호출을 지워도 된다"* 고 말한다 — 트랩하는 op 에 붙이면
        //   **일어났어야 할 패닉이 사라진다.**
        // ★★★ **스칼라 전용 op — 자연스러운 C 로 내린다.** 태그도 박스도 12KB 프레임도 없다.
        if (i < 512 && sc_tab[i]) {
            if (pure_tab[i]) low_puts(out, "LW_PURE ");
            low_puts(out, cbe_inline_hint(i) ? "static inline " : "static ");
            low_puts(out, cbe_ret_scalar(i) ? "long long lw_s_" : "lw_r lw_s_"); put_sym(out, d);
            low_puts(out, "("); cbe_put_params(out, i < 512 ? g_argmask[i] : LWA_ALL, cbe_depparam(i));
            low_puts(out, ") {\n");
            { unsigned am_ = i < 512 ? g_argmask[i] : LWA_ALL;
              if (am_ & LWA_AI) low_puts(out, "    (void)ai;");  if (am_ & LWA_AS) low_puts(out, " (void)as;");
              if (am_ & LWA_AF) low_puts(out, " (void)af;");     if (am_ & LWA_AR) low_puts(out, " (void)ar;");
              if (am_ & LWA_AB) low_puts(out, " (void)ab;");     if (am_ & LWA_AO) low_puts(out, " (void)ao;");
              if (am_ & LWA_AV) low_puts(out, " (void)av;");     low_puts(out, "\n"); }
            g_cur_di = i;                    // ★ 반환 모양은 이 def 의 것이다
            cbe_scalar_body(ir, d, out);
            low_puts(out, "}\n");
            // ★★★ 2a — 빠른 `spawn send` 칸의 어댑터: 액터의 바이트를 그대로 가리키고 정수 인자를 펴서 빠른 판을 부른다(되쓰기가 없다).
            if (i < 512 && g_qthunk[i]) {
                const unsigned am_ = g_argmask[i];
                const proven_size_t ni_ = d->nparams > 1 ? d->nparams - 1 : 1;
                low_puts(out, "static void lw_q_"); put_sym(out, d); low_puts(out, "(unsigned char *self_, const lowv *a_) {\n");
                low_fpf(out, "    long long ai[{}]; lw_sl as[1]; double af[1]; unsigned char *ar[1]; long long ab[1] = {{0}}; lw_r ao[1]; lw_v av[1];"
                             " lw_sl rsl[{} * LW_RECF] = {{{{0,0}}}};"
                             " (void)ai; (void)as; (void)af; (void)ar; (void)ab; (void)ao; (void)av; (void)rsl; (void)a_;\n",
                        PROVEN_ARG((size_t)(ni_)), PROVEN_ARG((size_t)((size_t)(d->nparams ? d->nparams : 1))));
                for (proven_size_t q = 1; q < d->nparams; q++) low_fpf(out, "    ai[{}] = a_[{}].i;\n", PROVEN_ARG((size_t)((size_t)(q - 1))), PROVEN_ARG((size_t)((size_t)(q - 1))));
                low_puts(out, "    ar[0] = self_;\n    (void)lw_s_"); put_sym(out, d); low_puts(out, "(");
                cbe_put_args(out, am_, false, cbe_depparam(i) ? "lw_depth" : NULL);
                low_puts(out, ");\n}\n");
            }
            // ★ 얇은 어댑터 — 디스패치 표와 느린 경로의 호출자가 그대로 산다.
            //   태그 검사는 **여기 경계에서 한 번**만 한다(안쪽 루프에는 없다).
            low_puts(out, "static lowv lw_op_"); put_sym(out, d);
            low_puts(out, "(const lowv *restrict a) {\n");
            { proven_size_t ni = 0, ns = 0, nfl = 0, nao = 0, nav = 0;
              for (proven_size_t q = 0; q < d->nparams; q++) {
                  if ((d->param_slice >> q) & 1u) ns++;
                  else if ((d->param_struct >> q) & 1u) ni += ir->structs[d->param_sidx[q]].nf;
                  else if ((d->param_flt >> q) & 1u) nfl++;
                  else if ((d->param_opt >> q) & 1u) nao++;   // ★ option·result 파라미터
                  else if ((d->param_vec >> q) & 1u) nav++;   // ★ 벡터 파라미터
                  else if ((d->param_w128 >> q) & 1u) ni += 2;   // ★ W-20261105-2304 — 128 비트는 정수 칸 둘
                  else ni++;
              }
              proven_size_t nrs = 0, nbs = 0;
              for (proven_size_t q = 0; q < d->nparams; q++) {
                  if ((d->param_struct >> q) & 1u) nrs++;
                  if ((d->param_bset >> q) & 1u) nbs++;
              }
              // ☞ 이 래퍼들은 **프리스탠딩 이미지에서 죽은 코드**다(실측 2026-08-03: 여기서
              //   배열을 줄여도 `.text` 가 1 바이트도 안 움직인다 — 링커가 이미 통째로 버린다).
              //   호스트 빌드의 프레임은 줄겠지만 **그건 안 쟀다.** 안 잰 개선은 넣지 않는다.
              low_fpf(out, "    long long ai[{}]; lw_sl as[{}]; double af[{}]; _Alignas(8) unsigned char arb[{}][64] = {{{{0}}}}; unsigned char *ar[{}];"
                           " long long ab[{}] = {{0}}; lw_r ao[{}]; lw_v av[{}];"
                           " lw_sl rsl[{}] = {{{{0,0}}}};"          /* ★ RFC-0106 단계 1 — 레코드의 슬라이스 필드 */
                           " (void)ai; (void)as; (void)af; (void)ar; (void)ab; (void)ao; (void)av; (void)rsl;\n",
                      PROVEN_ARG((size_t)((size_t)(ni ? ni : 1))), PROVEN_ARG((size_t)((size_t)(ns ? ns : 1))), PROVEN_ARG((size_t)((size_t)(nfl ? nfl : 1))),
                      PROVEN_ARG((size_t)((size_t)(nrs ? nrs : 1))), PROVEN_ARG((size_t)((size_t)(nrs ? nrs : 1))), PROVEN_ARG((size_t)((size_t)(nbs ? nbs : 1))),
                      PROVEN_ARG((size_t)((size_t)(nao ? nao : 1))), PROVEN_ARG((size_t)((size_t)(nav ? nav : 1))),
                      PROVEN_ARG((size_t)((size_t)((d->nparams ? d->nparams : 1) * K_REC_MAX))));
              for (proven_size_t z = 0; z < (nrs ? nrs : 1); z++)
                  low_fpf(out, "    ar[{}] = arb[{}];\n", PROVEN_ARG((size_t)((size_t)z)), PROVEN_ARG((size_t)((size_t)z)));
              proven_size_t bi = 0, bs = 0, bf = 0, br = 0, bb = 0, bao = 0, bav = 0;
              for (proven_size_t q = 0; q < d->nparams; q++) {
                  if ((d->param_vec >> q) & 1u) {
                      // ★ 벡터 파라미터 — 경계에서 레인을 푼다(태그 경로의 벡터 박스에서).
                      //   ★ 레인은 이제 **제 폭 칸**에 들어간다(RFC-0040 §9) — 경계에서 푸는
                      //     자리도 같은 멤버를 써야 한다. 두 자리가 다른 폭을 보면 곧 갈린다.
                      low_fpf(out, "    for (int lk_ = 0; lk_ < {}; lk_++) av[{}].{}[lk_] = lw_vlane(a[{}], lk_);\n",
                              PROVEN_ARG((int)((int)d->param_vlanes[q])), PROVEN_ARG((size_t)((size_t)bav++)),
                              proven_arg_cstr(lw_vmem((unsigned)d->param_ebits[q])), PROVEN_ARG((size_t)((size_t)q)));
                      continue;
                  }
                  if ((d->param_opt >> q) & 1u) {
                      // ★ option·result 파라미터 — 경계에서 (태그,값) 쌍으로 푼다.
                      low_fpf(out, "    ao[{}] = lw_opt2r(a[{}]);\n", PROVEN_ARG((size_t)((size_t)bao++)), PROVEN_ARG((size_t)((size_t)q)));
                      continue;
                  }
                  if ((d->param_flt >> q) & 1u) {
                      // ★ f64 파라미터 — 경계에서 double 로 푼다.
                      low_fpf(out, "    af[{}] = lw_fval(a[{}]);\n", PROVEN_ARG((size_t)((size_t)bf++)), PROVEN_ARG((size_t)((size_t)q)));
                      continue;
                  }
                  if ((d->param_bset >> q) & 1u) {
                      // ★ 비트셋 파라미터 — 박스에서 **마스크**를 꺼낸다(그리고 뒤에 되쓴다).
                      low_fpf(out, "    if (a[{}].tag != LWV_BITSET) lw_panic(\"a bitset\");\n", PROVEN_ARG((size_t)((size_t)q)));
                      low_fpf(out, "    ab[{}] = (long long)lw_bsets[a[{}].box];\n", PROVEN_ARG((size_t)((size_t)bb++)), PROVEN_ARG((size_t)((size_t)q)));
                      continue;
                  }
                  if ((d->param_struct >> q) & 1u) {
                      // ★★★ **경계에서 한 번** 푼다 — 태그 경로의 `lw_field` 를 여기서만 쓴다.
                      //   그것이 REC(박스)든 VIEW(바이트)든 상관없다. **안쪽 루프에는 없다.**
                      const low_ir_struct_t *S = &ir->structs[d->param_sidx[q]];
                      if (cbe_pnode(ir, d, q) >= 0) {   // ★ 2026-11-02 (W-20261102-0024 O4) — 마디는 바이트 위의 뷰로만 온다(cbe_nodeview) — 아니면 크게 멈춘다
                          low_fpf(out, "    if (LW_UNLIKELY(a[{}].tag != LWV_VIEW)) lw_panic(\"a node built in allocator bytes\"); ar[{}] = (unsigned char *)a[{}].p;\n",
                                  PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)br)), PROVEN_ARG((size_t)((size_t)q)));
                          br++;
                          continue;
                      }
                      if (!cbe_rec_slice_ok(ir, (int)d->param_sidx[q]) && !cbe_rec_arr_ok(ir, (int)d->param_sidx[q])) {   // ★ 2026-10-31 — 안의 구조체 · 부동 칸(cbe_rec_nested_ok 가 연 것) · 배열 칸은 아래 풀기(2026-11-01)
                          char v0[32]; low_bfmt(v0, sizeof v0, "a[{}]", PROVEN_ARG((size_t)((size_t)q)));
                          cbe_emit_rec_in(out, ir, (int)d->param_sidx[q], br, 0, v0, 0);
                          br++;
                          continue;
                      }
                      { char v0[32], b0[32]; (void)S;
                        low_bfmt(v0, sizeof v0, "a[{}]", PROVEN_ARG((size_t)((size_t)q))); low_bfmt(b0, sizeof b0, "ar[{}]", PROVEN_ARG((size_t)((size_t)br)));
                        cbe_emit_rec_unpack(out, ir, (int)d->param_sidx[q], v0, b0, "rsl", (size_t)q, true); }
                      br++;
                      continue;
                  }
                  if (((d->param_slice >> q) & 1u) && d->param_selem[q]) {
                      // ★★★ 구조체 슬라이스 파라미터는 본문에서 **view.array 로 나뉜다.**
                      //   그러니 경계에서는 **바이트 길이**를 줘야 한다: SLICE 는 그대로, VARRAY 는
                      //   **원소 수 × 스트라이드로 되돌린다**(안 그러면 view.array 가 **또** 나눠
                      //   길이가 반씩 줄어든다 — 그리고 차등 스윕이 그것을 잡았다).
                      int stride = (int)ir->structs[d->param_selem[q]-1].total;
                      if (!g_no_elemsl)
                          // ★ RFC-0109 단계 1 — 빠른 프레임은 원소 단위를 받는다: VARRAY 는 그대로, 바이트 SLICE 는 **여기서** 검사·나눗셈.
                          low_fpf(out, "    if (a[{}].tag == LWV_VARRAY)"
                                       " {{ as[{}].p = a[{}].p; as[{}].n = a[{}].n; }}\n"
                                       "    else if (a[{}].tag == LWV_SLICE)"
                                       " {{ if (LW_UNLIKELY(a[{}].n % {}u)) lw_panic(\"view_array: slice length is not a multiple of the element size\");"
                                       " as[{}].p = a[{}].p; as[{}].n = a[{}].n / {}u; }}\n"
                                       "    else lw_panic(\"index needs a slice\");\n",
                                  PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)),
                                  PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((int)(stride)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((int)(stride)));
                      else
                      low_fpf(out, "    if (a[{}].tag == LWV_VARRAY)"
                                   " {{ as[{}].p = a[{}].p; as[{}].n = a[{}].n * {}; }}\n"
                                   "    else if (a[{}].tag == LWV_SLICE)"
                                   " {{ as[{}].p = a[{}].p; as[{}].n = a[{}].n; }}\n"
                                   "    else lw_panic(\"index needs a slice\");\n",
                              PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((int)(stride)),
                              PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)));
                      bs++;
                      continue;
                  }
                  if (((d->param_slice >> q) & 1u) && d->param_rowb[q] && !g_no_elemsl) {
                      // ★ 2026-11-01 (W-20261101-1150) — 줄의 나열: 줄 VARRAY 는 그대로(원소 수 = 줄 수), 바이트 슬라이스는 줄 바이트로 나눈다(lw_varray 와 같은 검사).
                      low_fpf(out, "    if (a[{}].tag == LWV_VARRAY && (a[{}].i & 0x80000) && a[{}].box == {}) {{ as[{}].p = a[{}].p; as[{}].n = a[{}].n; }}\n"
                                   "    else if (a[{}].tag == LWV_SLICE) {{ if (LW_UNLIKELY(a[{}].n % {}u)) lw_panic(\"an array of rows needs a whole number of rows\");"
                                   " as[{}].p = a[{}].p; as[{}].n = a[{}].n / {}u; }}\n"
                                   "    else lw_panic(\"index needs a slice\");\n",
                              PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((int)((int)d->param_rowb[q])), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)),
                              PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((int)((unsigned)d->param_rowb[q])), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((int)((unsigned)d->param_rowb[q])));
                      bs++;
                      continue;
                  }
                  if ((d->param_slice >> q) & 1u) {
                      // ★★★ **경계에서 한 번만 정규화한다**: 바이트 슬라이스면 길이를 원소 수로
                      //   나누고(나눠떨어지지 않으면 트랩 — 런타임과 **같은 말**), 이미 타입
                      //   배열이면 그대로 쓴다. 안쪽 루프에는 태그도 나눗셈도 없다.
                      // ★★★ **바이트 그대로** 넘긴다 — 본문 `view.array` 가 원소 배열로 본다.
                      //   VARRAY 로 오면 이미 원소 수이므로 **원소폭을 곱해 바이트로 되돌린다**
                      //   (안 그러면 view.array 가 또 나눠 길이가 반씩 준다 — 구조체 슬라이스에서
                      //   차등 스윕이 잡은 그 결함이다).
                      int ez = cbe_pslice_w(d, q); if (ez <= 0) ez = 1;   // ★ i64(164) 도 8 · f32/f64 는 param_esz — 폭 규칙은 cbe_pslice_w 한 자리
                      if (!g_no_elemsl) {
                          // ★ RFC-0109 단계 1 — VARRAY(원소 수)는 그대로, 바이트 SLICE 는 **여기가 경계**: 검사하고 나눈다.
                          low_fpf(out, "    if (a[{}].tag == LWV_VARRAY) {{ as[{}].p = a[{}].p; as[{}].n = a[{}].n; }}\n",
                                  PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)));
                          if (ez > 1)
                              low_fpf(out, "    else if (a[{}].tag == LWV_SLICE) {{ if (LW_UNLIKELY(a[{}].n % {}u))"
                                           " lw_panic(\"view_array: slice length is not a multiple of the element size\");"
                                           " as[{}].p = a[{}].p; as[{}].n = a[{}].n / {}u; }}\n",
                                      PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((int)(ez)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((int)(ez)));
                          else
                              low_fpf(out, "    else if (a[{}].tag == LWV_SLICE) {{ as[{}].p = a[{}].p; as[{}].n = a[{}].n; }}\n",
                                      PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)));
                      } else {
                      low_fpf(out, "    if (a[{}].tag == LWV_VARRAY) {{ as[{}].p = a[{}].p; as[{}].n = a[{}].n * {}; }}\n",
                              PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((int)(ez)));
                      low_fpf(out, "    else if (a[{}].tag == LWV_SLICE) {{ as[{}].p = a[{}].p; as[{}].n = a[{}].n; }}\n",
                              PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bs)), PROVEN_ARG((size_t)((size_t)q)));
                      }
                      low_fpf(out, "    else lw_panic(\"index needs a slice\");\n");
                      bs++;
                  } else if ((d->param_w128 >> q) & 1u) {   // ★ W-20261105-2304 — 태그 값의 아래(i) · 위(n) 절반
                      low_fpf(out, "    ai[{}] = lw_want_int(a[{}], \"ints\"); ai[{}] = (long long)a[{}].n;\n", PROVEN_ARG((size_t)((size_t)bi)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bi + 1)), PROVEN_ARG((size_t)((size_t)q))); bi += 2;
                  } else {
                      low_fpf(out, "    ai[{}] = lw_want_int(a[{}], \"ints\");\n", PROVEN_ARG((size_t)((size_t)bi)), PROVEN_ARG((size_t)((size_t)q))); bi++;
                  }
              } }
            // ★ 경계에서 **한 번** 태그 값으로 되돌린다(호출자가 태그 경로일 수 있으니).
            low_puts(out, cbe_ret_scalar(i) ? "    lw_r r_ = {0}; r_.v = lw_s_" : "    lw_r r_ = lw_s_");
            put_sym(out, d); low_puts(out, "(");
            cbe_put_args(out, i < 512 ? g_argmask[i] : LWA_ALL, false, cbe_depparam(i) ? "lw_depth" : NULL);
            low_puts(out, ");\n");
            { proven_size_t bw = 0;
              for (proven_size_t q = 0; q < d->nparams; q++)
                  if ((d->param_bset >> q) & 1u)
                      low_fpf(out, "    lw_bsets[a[{}].box] = (unsigned long long)ab[{}];\n",
                              PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)bw++))); }
            // ★★★ **되쓰기** — 액터 핸들러가 인스턴스의 필드를 올렸다면, 그 쓰기는
            //   **호출자(태그 경로)에게 보여야 한다.** 박스가 그것을 기대하고 있다.
            if (cbe_pmut(d)) {
                proven_size_t bw = 0;
                for (proven_size_t q = 0; q < d->nparams; q++) {
                    if (!((d->param_struct >> q) & 1u)) continue;
                    if (cbe_pnode(ir, d, q) >= 0) { bw++; continue; }   // ★ 2026-11-02 — 마디의 뷰: 피호출자가 그 바이트에 바로 썼다(되쓸 것이 없다)
                    { char v0[32], b0[32];
                      low_bfmt(v0, sizeof v0, "a[{}]", PROVEN_ARG((size_t)((size_t)q))); low_bfmt(b0, sizeof b0, "ar[{}]", PROVEN_ARG((size_t)((size_t)bw)));
                      cbe_emit_rec_writeback(out, ir, (int)d->param_sidx[q], v0, b0, "rsl", (size_t)q, true); }
                    bw++;
                }
            }
            if (i < 512 && g_retk[i] >= K_REC && g_retk[i] < K_VIEW) {
                // ★ 레코드 반환 — 경계에서 박스를 만든다(태그 경로가 그것을 기대한다).
                //   ★★★ 중첩 레코드면 **재귀로** 안쪽 박스부터 짓는다(평탄 슬롯 → 중첩 박스).
                int sx_ = g_retk[i] - K_REC;
                low_puts(out, "    if (r_.tag == 7) {\n");
                int nid = 0;
                int root = cbe_emit_rec_recon(out, ir, sx_, 0, &nid);
                low_fpf(out, "        {{ lowv rv_ = {{0}}; rv_.tag = LWV_REC; rv_.box = box_{}; return rv_; }}\n"
                             "    }}\n", PROVEN_ARG((int)(root)));
            }
            if (i < 512 && g_retk[i] >= K_SVIEW) {
                // ★ 구조체 슬라이스 반환(태그 9) — 태그 경로의 LWV_VARRAY 로 되돌린다.
                //   원소 수는 r_.s.n, stride 는 이 def 의 반환 구조체가 안다(box), sidx 는 v.i 에.
                int sx_ = g_retk[i] - K_SVIEW;
                int stride_ = (int)ir->structs[sx_].total;
                low_fpf(out, "    if (r_.tag == 9) {{ lowv sv_ = {{0}}; sv_.tag = LWV_VARRAY;"
                             " sv_.p = r_.s.p; sv_.n = r_.s.n; sv_.box = {}; sv_.i = 0x40000 | ({} << 20);"
                             " return sv_; }}\n", PROVEN_ARG((int)(stride_)), PROVEN_ARG((int)(sx_)));
            }
            if (i < 512 && g_retk[i] == K_OPT && g_retnode[i])   // ★ 2026-11-03 (W-20261103-1518) — 마디의 뷰를 싼 option: some 이면 그 바이트의 뷰를 싼다(태그 경로의 마디 값)
                low_fpf(out, "    if (r_.tag == 1) {{ if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                             "        lowv vv_ = {{0}}; vv_.tag = LWV_VIEW; vv_.p = r_.s.p; vv_.n = r_.s.n; vv_.box = {}; lw_boxes[lw_nbox] = vv_;\n"
                             "        lowv w_ = {{0}}; w_.tag = LWV_SOME; w_.box = lw_nbox++; return w_; }}\n", PROVEN_ARG((int)((int)g_retnode[i] - 1)));
            if (i < 512 && g_retk[i] == K_OPT && g_retopt[i]) {
                // ★★★ **레코드를 감싼 option/result 반환** — some/ok(태그 1·3)면 r_.r[] 에서
                //   (중첩) 레코드 박스를 짓고 그것을 감싼다. none/err 은 아래 일반 경로가 처리한다.
                int rsx = g_retopt[i] - 1;
                low_puts(out, "    if (r_.tag == 1 || r_.tag == 3) {\n");
                int nid = 0;
                int root = cbe_emit_rec_recon(out, ir, rsx, 0, &nid);
                low_fpf(out, "        if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                             "        {{ lowv rv_ = {{0}}; rv_.tag = LWV_REC; rv_.box = box_{};\n"
                             "          lw_boxes[lw_nbox] = rv_;\n"
                             "          lowv w_ = {{0}}; w_.tag = (r_.tag == 1) ? LWV_SOME : LWV_OK; w_.box = lw_nbox++; return w_; }} }}\n",
                        PROVEN_ARG((int)(root)));
            }
            if (i < 512 && g_retk[i] == K_OPT2) {
                // ★ 겹 option · result 반환 — 바깥이 some / ok 이면 안의 값(수를 싼 some · ok · none · error)을 먼저 짓고 그것을 싼다.
                low_puts(out, "    if (r_.tag == 1 || r_.tag == 3) { lowv in_ = {0};\n"
                      "        if (r_.t2 == 2) in_.tag = LWV_NONE;\n"
                      "        else if (r_.t2 == 4) in_ = lw_err((int)r_.v);\n"
                      "        else { if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                      "               lw_boxes[lw_nbox] = lw_int(r_.v); in_.tag = (r_.t2 == 1) ? LWV_SOME : LWV_OK; in_.box = lw_nbox++; }\n"
                      "        if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                      "        lw_boxes[lw_nbox] = in_;\n"
                      "        { lowv w_ = {0}; w_.tag = (r_.tag == 1) ? LWV_SOME : LWV_OK; w_.box = lw_nbox++; return w_; } }\n");
            }
            if (i < 512 && g_retk[i] == K_OPTSL && (g_retve[i] > 1 || g_retfl[i])) {
                // ★ 2026-11-01 — 원소 폭이 1 이 아닌 슬라이스 option: 태그 경로의 VARRAY(원소 수 · 폭 · 표식)를 싼다.
                low_fpf(out, "    if (r_.tag == 1) {{\n"
                      "        if (lw_nbox >= lw_boxlim) lw_panic(\"box pool exhausted — raise it with -DLW_BOXPOOL=N (docs/runtime-pools.md)\");\n"
                      "        {{ lowv sv_ = {{0}}; sv_.tag = LWV_VARRAY; sv_.p = r_.s.p; sv_.n = r_.s.n; sv_.box = {}; sv_.i = {}; lw_boxes[lw_nbox] = sv_; }}\n"
                      "        {{ lowv w_ = {{0}}; w_.tag = LWV_SOME; w_.box = lw_nbox++; return w_; }} }}\n",
                        PROVEN_ARG((int)((int)g_retve[i])), PROVEN_ARG((int)(g_retfl[i] == CBE_FL_FLT ? 0x10000 : g_retfl[i] == CBE_FL_SGN ? 0x20000 : 0)));
            } else
            if (i < 512 && g_retk[i] == K_OPTSL) {
                // ★★★★ WO-0222 — **바이트 슬라이스를 감싼 option 반환**: some 이면 태그 경로가 기대하는 박스(LWV_SLICE)를 짓는다.
                low_puts(out, "    if (r_.tag == 1) {\n"
                      "        if (lw_nbox >= lw_boxlim) lw_panic(\"box pool exhausted — raise it with -DLW_BOXPOOL=N (docs/runtime-pools.md)\");\n"
                      "        lw_boxes[lw_nbox] = (lowv){ .tag = LWV_SLICE, .p = r_.s.p, .n = r_.s.n };\n"
                      "        { lowv w_ = {0}; w_.tag = LWV_SOME; w_.box = lw_nbox++; return w_; } }\n");
            }
            if (i < 512 && g_retk[i] == K_ROWS)   // ★ 2026-11-01 — 줄의 나열 반환: 태그 경로의 VARRAY(줄 수 · 줄 바이트 · 줄 표식 · 원소 폭 · 표식) — lw_varray 와 같은 모양
                low_fpf(out, "    if (r_.tag == 6) {{ lowv sv_ = {{0}}; sv_.tag = LWV_VARRAY; sv_.p = r_.s.p; sv_.n = r_.s.n; sv_.box = {}; sv_.i = 0x80000 | {} | {} | {}; return sv_; }}\n",
                        PROVEN_ARG((int)((int)g_retrb[i])), PROVEN_ARG((int)((int)g_retve[i])), PROVEN_ARG((int)((g_retfl[i] & CBE_FL_MARK) == CBE_FL_FLT ? 0x10000 : (g_retfl[i] & CBE_FL_MARK) == CBE_FL_SGN ? 0x20000 : 0)),
                        PROVEN_ARG((int)((int)(CBE_FL_RSUB(g_retfl[i]) << IR_ROWSUB_SHIFT))));   // ★ 2026-11-02 — 아래 줄 꼴의 칸(세 겹 이상)
            if (i < 512 && g_retk[i] == K_SL && (g_retve[i] > 1 || g_retfl[i]))   // ★ 타입 있는 슬라이스 반환 — 태그 경로의 VARRAY(원소 수 · 폭 · 표식)로
                low_fpf(out, "    if (r_.tag == 6) {{ lowv sv_ = {{0}}; sv_.tag = LWV_VARRAY; sv_.p = r_.s.p; sv_.n = r_.s.n; sv_.box = {}; sv_.i = {}; return sv_; }}\n",
                        PROVEN_ARG((int)((int)g_retve[i])), PROVEN_ARG((int)(g_retfl[i] == CBE_FL_FLT ? 0x10000 : g_retfl[i] == CBE_FL_SGN ? 0x20000 : 0)));
            if (i < 512 && g_retk[i] == K_W128)   // ★ W-20261105-2304 — 128 비트 결과(바이트 칸 · tag 11)
                low_puts(out, "    if (r_.tag == 11) { lw_u128 w_; memcpy(&w_, r_.r, sizeof w_); return lw_wmk(w_); }\n");
            low_puts(out, "    if (r_.tag == 5) return lw_flt(r_.f);\n"
                  "    if (r_.tag == 6) { lowv sv_ = {0}; sv_.tag = LWV_SLICE; sv_.p = r_.s.p;"
                  " sv_.n = r_.s.n; return sv_; }\n"
                  "    if (r_.tag == 8) { lowv vv_ = {0}; vv_.tag = LWV_VIEW; vv_.p = r_.s.p;"
                  " vv_.n = r_.s.n; vv_.box = (int)r_.v; return vv_; }\n"   /* ★ 구조체 뷰 반환 */
                  "    if (r_.tag == 0) return lw_int(r_.v);\n"
                  "    if (r_.tag == 2) { lowv n_ = {0}; n_.tag = LWV_NONE; return n_; }\n"
                  "    if (r_.tag == 4) return lw_err((int)r_.v);\n"
                  "    if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                  "    lw_boxes[lw_nbox] = lw_int(r_.v);\n"
                  "    { lowv w_ = {0}; w_.tag = (r_.tag == 1) ? LWV_SOME : LWV_OK;"
                  " w_.box = lw_nbox++; return w_; }\n}\n");
            emitted++;
            continue;
        }
        // ★★★ 태그 경로로 나가는데 **이유가 없다면**, 진단이 조용히 비는 것이다 — 그것도 거짓말이다.
        //   ⇒ **그 op 을 다시 시뮬레이션**해서 종류 추론이 **어느 낱말에서 왜** 멈췄는지 잡는다.
        //   (시뮬레이터가 g_kj·g_kw·g_sub 를 채운다. 재는 것이 공짜다 — 이건 진단 경로일 뿐이다.)
        if (i < 512 && !g_why[i][0] && g_why_on) {
            g_kj = -1; g_kw = -1; g_sub[0] = 0;
            (void)cbe_kind_ok(ir, d, sc_tab);
            if (g_kj >= 0)
                low_bfmt(g_why[i], sizeof g_why[0], "the fast path cannot follow the value at `{}` (op {}){}{}",
                         proven_arg_cstr(g_kw >= 0 ? low_irw_name((low_irw_t)g_kw) : "?"), PROVEN_ARG((int)(g_kj)),
                         proven_arg_cstr(g_sub[0] ? " — " : ""), proven_arg_cstr(g_sub[0] ? g_sub : ""));
        }
        if (i < 512 && !g_why[i][0])
            low_bfmt(g_why[i], sizeof g_why[0], "the kind inference gave up (no stopping point found)");
        if (i < 512 && pure_tab[i]) low_puts(out, "LW_PURE ");
        low_puts(out, "static lowv lw_op_"); put_sym(out, d); low_puts(out, "(const lowv *restrict a) {\n");
        low_puts(out, "    if (++lw_depth > 128) lw_panic(\"call depth limit exceeded\");\n");
        low_fpf(out, "    lowv loc[{}]; memset(loc, 0, sizeof loc);\n",
                PROVEN_ARG((size_t)((size_t)(d->nlocals ? d->nlocals : 1))));
        // ★ RFC-0132 T2b-2 — 틀 안 나열 자리(§13.2 ⓐ·ⓒ): 이 op 의 C 틀에 바이트 줄 하나(부를 때마다 새것 — 재귀도 안전).
        // ★ RFC-0132 P4e (§14.8) — 틀 창: 값 구조체 레코드 칸(nwin). 예산은 틀 안 나열과 함께 센다(VM 과 같은 수).
        if (d->nwin)
            low_fpf(out, "    lowrec lw_fwr_[{}]; unsigned char lw_fwb_[{}][LW_RBMAX] __attribute__((aligned(8))); lw_fwin fw_;"
                         " lw_win_init(&fw_, lw_fwr_, &lw_fwb_[0][0], {});\n"
                         "    int lw_wl_ __attribute__((cleanup(lw_wl_restore))) = (lw_nwin_live++, 0); (void)lw_wl_;\n",
                    PROVEN_ARG((unsigned)((unsigned)d->nwin)), PROVEN_ARG((unsigned)((unsigned)d->nwin)), PROVEN_ARG((unsigned)((unsigned)d->nwin)));
        if (d->lbuf_size || d->wbytes) {
            if (d->lbuf_size) low_fpf(out, "    unsigned char lw_lb[{}] __attribute__((aligned(16)));\n", PROVEN_ARG((size_t)((size_t)d->lbuf_size)));
            // ★ RFC-0135 S0 · X-0084 — 틀 안 나열의 스택 예산. 반환하면 cleanup 이 되돌린다(VM 은 같은 수로 센다).
            low_fpf(out, "    long lw_sbg_ __attribute__((cleanup(lw_sb_restore))) = lw_sbytes;\n"
                         "    if (LW_UNLIKELY((lw_sbytes += {}L) > LW_SBUDGET)) lw_panic(\"the list literals and struct values living on this call chain need more than the stack budget "
                         "(RFC-0135 · RFC-0132 P4e) — keep a recursion with local lists or structs shallow, or take them from an allocator\");\n", PROVEN_ARG((size_t)((size_t)d->lbuf_size + (size_t)d->wbytes)));
        }
        low_fpf(out, "    for (int i = 0; i < {}; i++) loc[i] = a[i];\n", PROVEN_ARG((size_t)((size_t)d->nparams)));
        // ★★★★★ **태그 스택도 쓰는 만큼만** (2026-08-17, MEM-0003 — 프레임 래칫이 찾았다).
        //   `lowv st[256]` 은 이 파일 머리가 2026-07 에 이미 *"함수마다 12KB 스택 프레임"* 이라
        //   고발한 그 자리다. 그때 고친 것은 **빠른 경로**(스칼라 전용 op 을 `long long` 으로
        //   내렸다)였고, 빠른 경로로 못 내려간 op 은 이 줄을 그대로 지고 있었다 — 실측:
        //   `wordfreq` 의 `lw_op_open` **16,720 B**. 같은 결함이 세 곳(지역 종류 · 벡터 스택 ·
        //   여기)에 있었고 **둘만 고쳐졌다.**
        //   ⇒ 상계는 **IR 명령 수**다: 스택 기계에서 살아 있는 값은 저마다 그것을 민 명령이
        //     있고, 한 명령이 한 번에 미는 값은 하나를 넘지 않는다 ⇒ 동시 생존 수 ≤ ncode.
        //   ★ 넘침 검사를 새로 안 낸다 — 자르지 않으면 상계가 정확하고, 자르면 **오늘과 똑같은
        //     256** 이다. 어느 쪽도 오늘보다 나빠지지 않는다. (벡터 스택은 세는 법이 달라
        //     검사가 필요했다. **같은 문제가 아니면 같은 약을 안 쓴다.**)
        {
            size_t tsn = d->ncode ? d->ncode : 1;
            if (tsn < 8) tsn = 8;            // 아주 짧은 op 도 임시 몇 칸은 쓴다
            if (tsn > 256) tsn = 256;        // 옛 값 — 여기서는 더 못 줄인다
            low_fpf(out, "    lowv st[{}]; int sp = 0; (void)st; (void)sp;\n", PROVEN_ARG((size_t)(tsn)));
        }
        // ★ 이 프레임의 **벡터 풀 바닥**(RFC-0089 B) — 루프의 뒤 분기와 반환이 여기로 되감는다.
        //   벡터를 안 만지는 op 에는 **아무것도 안 낸다**(위 cbe_uses_vec 의 이유).
        const bool vecfn = cbe_uses_vec(d);
        if (vecfn) low_puts(out, "    int vb_ = lw_nvecpool; (void)vb_;\n");
        // ★ 네 풀의 바닥(F1) — 루프의 뒤 분기가 여기로 되감는다.
        // ★★ 2026-11-11 (W-20261111-0940 O1) — 참조를 쥔 op 도 되감는다. 다만 **이 틀이 선 뒤로 참조 너머에 풀 값을 쓴 일이 없을 때만**
        //   (lw_refw 가 그대로). 정본 §8.1 (3a) «풀은 루프를 한 바퀴 돌 때마다 되감긴다» — VM 은 늘 그랬다. 전엔 `W-POOL-LOOP` 로 알리고 멈췄다.
        const bool poolgc_ref = !cbe_pool_gc_ok(d);
        const bool poolgc = true;
        low_puts(out, "    int pbx_ = lw_nbox, prc_ = lw_nrec, psk_ = lw_nstk, pbs_ = lw_nbset, pvc_ = lw_nvecpool;\n"
              "    (void)pbx_; (void)prc_; (void)psk_; (void)pbs_; (void)pvc_;\n");
        if (poolgc_ref) low_puts(out, "    unsigned rwc_ = lw_refw; int esc_ = 0; lw_rfar rf_ = { {0}, 0 }; (void)rwc_; (void)esc_; (void)rf_;\n");
        char pgif_[160] = "";
        if (poolgc_ref) low_bfmt(pgif_, sizeof pgif_, "if (!esc_ && !(esc_ = !lw_refw_ok(&rwc_, loc, {}, &rf_))) ", PROVEN_ARG((size_t)((size_t)(d->nlocals ? d->nlocals : 1))));
        // 되감지 못하는 바퀴에도 벡터 풀은 전처럼 되감는다(참조를 쥔 op 은 전에 lw_vecgc 만 냈다)
        char pgel_[96] = ""; if (poolgc_ref && vecfn) low_bfmt(pgel_, sizeof pgel_, " else lw_vecgc(vb_, loc, {}, st, sp);", PROVEN_ARG((size_t)((size_t)(d->nlocals ? d->nlocals : 1))));
        proven_u32 lastln2_ = 0; proven_u16 lastfid2_ = 0;
        for (proven_size_t j = 0; j < d->ncode; j++) {
            const low_ir_ins_t *in = &d->code[j];
            cbe_line(out, ir, in, &lastln2_, &lastfid2_);
            if (target[j]) low_fpf(out, "L{}:\n", PROVEN_ARG((size_t)((size_t)j)));
            if (cbe_is_w128(in)) {   // ★ RFC-0151 — 128 비트 정수(프렐류드 lw_w_*)
                static const struct { low_irw_t w; int op; } WB[] = {
                    { IRW_ADD, 0 }, { IRW_SUB, 1 }, { IRW_MUL, 2 }, { IRW_DIV, 3 }, { IRW_MOD, 4 }, { IRW_LT, 5 }, { IRW_LE, 6 }, { IRW_GT, 7 },
                    { IRW_GE, 8 }, { IRW_BAND, 9 }, { IRW_BOR, 10 }, { IRW_BXOR, 11 }, { IRW_SHL, 12 }, { IRW_SHR, 13 }, { IRW_WSHL, 14 },
                    { IRW_WSHR, 15 }, { IRW_ROTL, 16 }, { IRW_ROTR, 17 } };
                static const struct { low_irw_t w; int op; } WU[] = {
                    { IRW_NEG, 0 }, { IRW_BNOT, 1 }, { IRW_POPCNT, 2 }, { IRW_CLZ, 3 }, { IRW_CTZ, 4 }, { IRW_BSWAP, 5 } };
                if (in->w == IRW_MK128) low_fpf(out, "    st[sp-1] = lw_w_make(st[sp-1], {}ull);\n", PROVEN_ARG((unsigned long long)((unsigned long long)in->a)));
                else if (in->w == IRW_CAST) low_fpf(out, "    st[sp-1] = lw_w_cast({}ll, st[sp-1]);\n", PROVEN_ARG((long long)((long long)in->a)));
                else {
                    bool done = false;
                    for (size_t q = 0; q < sizeof WB / sizeof WB[0] && !done; q++)
                        if (WB[q].w == in->w) { low_fpf(out, "    st[sp-2] = lw_w_bin({}ll, {}, st[sp-2], st[sp-1]); sp--;\n", PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((int)(WB[q].op))); done = true; }
                    for (size_t q = 0; q < sizeof WU / sizeof WU[0] && !done; q++)
                        if (WU[q].w == in->w) { low_fpf(out, "    st[sp-1] = lw_w_un({}ll, {}, st[sp-1]);\n", PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((int)(WU[q].op))); done = true; }
                }
                continue;
            }
            switch (in->w) {
                case IRW_CONST: low_fpf(out, "    st[sp++] = lw_int({}ll);\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_LOAD:  low_fpf(out, "    st[sp++] = loc[{}];\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_STORE: low_fpf(out, "    lw_store(&loc[{}], st[--sp]);\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_DROP:  low_puts(out, "    sp--;\n"); break;
                case IRW_NEG:   low_fpf(out, "    st[sp-1] = lw_negs({}, st[sp-1]);\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_FCONST: low_fpf(out, "    st[sp++] = lw_fltbits({}ll);\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_CAST:  low_fpf(out, "    st[sp-1] = lw_cast({}, st[sp-1]);\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_ASSERT:
                    // ★ 증명된 ensures 검사는 **실제로 제거한다**(그것이 계약이 사 주는 성능).
                    //   진입 requires 는 제거하지 않는다 — 분석이 그것을 사실로 심었으므로.
                    if ((in->a & 3) && (in->a & IR_POL_PROVEN)) { low_puts(out, "    sp--;\n"); break; }
                    // ★ 계약은 **지켜지라고** 있는 것이다 ⇒ 위반 가지는 **차갑다**.
                    //   컴파일러가 그것을 알면 뜨거운 경로에서 그 코드를 **치워 준다**.
                    low_fpf(out, "    if (LW_UNLIKELY(st[sp-1].tag != LWV_INT || !st[sp-1].i)) "
                                 "lw_panic(\"{}\"); sp--;\n",
                            proven_arg_cstr((in->a & 4) ? LW_EXPECT_MSG   // ★ 2026-09-27 — 위와 같다
                          : (in->a & 8) ? "an errors `when` condition is TRUE but the op returned normally"
                          : (in->a & 2) ? "errors `when` condition is false on this error path"
                                        : (in->a & 1) ? "ensures violated at exit"
                                                      : "requires violated at entry"));
                    break;
                case IRW_UNM:   low_fpf(out, "    st[sp-1] = lw_unm({}, st[sp-1]);\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_BINM:  low_fpf(out, "    st[sp-2] = lw_binm({}, st[sp-2], st[sp-1]); sp--;\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_NOT:   low_puts(out, "    st[sp-1] = lw_not(st[sp-1]);\n"); break;
                case IRW_ADD:   low_fpf(out, "    st[sp-2] = lw_arith({}, 0, st[sp-2], st[sp-1]); sp--;\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_SUB:   low_fpf(out, "    st[sp-2] = lw_arith({}, 1, st[sp-2], st[sp-1]); sp--;\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_MUL:   low_fpf(out, "    st[sp-2] = lw_arith({}, 2, st[sp-2], st[sp-1]); sp--;\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_DIV:   low_fpf(out, "    st[sp-2] = lw_divs({}, st[sp-2], st[sp-1]); sp--;\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_MOD:   low_fpf(out, "    st[sp-2] = lw_mods({}, st[sp-2], st[sp-1]); sp--;\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_AND:   low_puts(out, "    st[sp-2] = lw_landor(0, st[sp-2], st[sp-1]); sp--;\n"); break;
                case IRW_OR:    low_puts(out, "    st[sp-2] = lw_landor(1, st[sp-2], st[sp-1]); sp--;\n"); break;
                case IRW_EQ:    low_puts(out, "    st[sp-2] = lw_cmp(0, st[sp-2], st[sp-1]); sp--;\n"); break;
                case IRW_NE:    low_puts(out, "    st[sp-2] = lw_cmp(1, st[sp-2], st[sp-1]); sp--;\n"); break;
                case IRW_LT:    low_fpf(out, "    st[sp-2] = lw_cmps({}, 2, st[sp-2], st[sp-1]); sp--;\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_LE:    low_fpf(out, "    st[sp-2] = lw_cmps({}, 3, st[sp-2], st[sp-1]); sp--;\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_GT:    low_fpf(out, "    st[sp-2] = lw_cmps({}, 4, st[sp-2], st[sp-1]); sp--;\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_GE:    low_fpf(out, "    st[sp-2] = lw_cmps({}, 5, st[sp-2], st[sp-1]); sp--;\n", PROVEN_ARG((long long)((long long)in->a))); break;
                // ★ **뒤로 가는 분기가 곧 루프다** — 반복 하나가 만든 벡터는 거기서 죽는다(RFC-0089 B).
                //   앞으로 가는 분기에는 안 넣는다: 되감기는 어디서든 안전하지만 **공짜는 아니다**.
                case IRW_BR:
                    if ((proven_size_t)in->a <= j) {
                        if (poolgc) low_fpf(out, "    {}{{ {}lw_pool_gc(pbx_, prc_, psk_, pbs_, pvc_, loc, {}, st, sp, {}); }}{}\n", proven_arg_cstr(pgif_),
                                            proven_arg_cstr(d->nwin ? "lw_fw_gc(&fw_, loc, (int)(sizeof loc / sizeof *loc), st, sp); " : ""),
                                            PROVEN_ARG((size_t)((size_t)(d->nlocals ? d->nlocals : 1))), proven_arg_cstr(d->nwin ? "&fw_" : "0"), proven_arg_cstr(pgel_));
                        else if (vecfn) low_fpf(out, "    lw_vecgc(vb_, loc, {}, st, sp);\n", PROVEN_ARG((size_t)((size_t)(d->nlocals ? d->nlocals : 1))));
                    }
                    low_fpf(out, "    goto L{};\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_BRZ:
                    if (poolgc && (proven_size_t)in->a <= j)
                        low_fpf(out, "    if (lw_want_int(st[--sp], \"branch\") == 0) {{ {}{{ {}lw_pool_gc(pbx_, prc_, psk_, pbs_, pvc_, loc, {}, st, sp, {}); }}{} goto L{}; }}\n",
                                proven_arg_cstr(pgif_), proven_arg_cstr(d->nwin ? "lw_fw_gc(&fw_, loc, (int)(sizeof loc / sizeof *loc), st, sp); " : ""),
                                PROVEN_ARG((size_t)((size_t)(d->nlocals ? d->nlocals : 1))), proven_arg_cstr(d->nwin ? "&fw_" : "0"), proven_arg_cstr(pgel_), PROVEN_ARG((long long)((long long)in->a)));
                    else if (vecfn && (proven_size_t)in->a <= j)
                        low_fpf(out, "    if (lw_want_int(st[--sp], \"branch\") == 0) {{ lw_vecgc(vb_, loc, {}, st, sp); goto L{}; }}\n",
                                PROVEN_ARG((size_t)((size_t)(d->nlocals ? d->nlocals : 1))), PROVEN_ARG((long long)((long long)in->a)));
                    else
                        low_fpf(out, "    if (lw_want_int(st[--sp], \"branch\") == 0) goto L{};\n", PROVEN_ARG((long long)((long long)in->a)));
                    break;
                case IRW_SWITCH: {   // ★ MM9-B2 — 점프 테이블 = 진짜 C switch. 뒤 BR 런(span+1)을 소비한다.
                    long long span = in->a;
                    if (vecfn) low_fpf(out, "    lw_vecgc(vb_, loc, {}, st, sp);\n", PROVEN_ARG((size_t)((size_t)(d->nlocals ? d->nlocals : 1))));   // 표의 어느 칸은 뒤로 간다
                    low_puts(out, "    { long long _sv = lw_want_int(st[--sp], \"switch\"); switch (_sv) {\n");
                    for (long long cse = 0; cse < span && (proven_size_t)(j + 1 + cse) < d->ncode; cse++)
                        low_fpf(out, "      case {}ll: goto L{};\n", PROVEN_ARG((long long)(cse)), PROVEN_ARG((long long)((long long)d->code[j + 1 + cse].a)));
                    if ((proven_size_t)(j + 1 + span) < d->ncode)
                        low_fpf(out, "      default: goto L{};\n", PROVEN_ARG((long long)((long long)d->code[j + 1 + span].a)));
                    low_puts(out, "    } }\n");
                    j += (proven_size_t)span + 1;   // 테이블 BR 들을 건너뛴다(for 의 j++ 가 다음 실명령으로)
                    break;
                }
                // ★ 콜리의 벡터는 콜리와 함께 죽고, **돌려주는 하나**만 콜리의 바닥으로 내려온다(RFC-0089 B).
                case IRW_RET:   // ★ P4e — 틀 창의 레코드를 돌려주면 되튐 창으로 베낀다(부른 쪽이 옮긴다)
                    if (d->nwin)
                        low_puts(out, vecfn ? "    lw_depth--; return lw_vecret(vb_, lw_fw_ret(&fw_, sp ? st[sp-1] : lw_int(0)));\n"
                                    : "    lw_depth--; return lw_fw_ret(&fw_, sp ? st[sp-1] : lw_int(0));\n");
                    else
                        low_puts(out, vecfn ? "    lw_depth--; return lw_vecret(vb_, sp ? st[sp-1] : lw_int(0));\n"
                                    : "    lw_depth--; return sp ? st[sp-1] : lw_int(0);\n");
                    break;
                // ★★★ **인라인 asm — 여기서만 진짜가 된다** (RFC-0041).
                //   VM 은 이걸 못 돌리고 **못 돌린다고 말한다.** 네이티브는 **돈다.**
                //   두 뒤끝이 **다른 답을 주는 유일한 자리**이고, 그것이 정확히 **정직한 자리**다:
                //   기계 명령은 기계에서만 뜻이 있다.
                // ★★★ **비트 연산** (RFC-0064) — 계산은 **스칼라 핵 하나**에 있다(교훈 7).
                case IRW_BAND: case IRW_BOR: case IRW_BXOR:
                case IRW_SHL: case IRW_SHR: case IRW_WSHL: case IRW_WSHR:
                case IRW_ROTL: case IRW_ROTR: case IRW_CLMULLO: case IRW_CLMULHI: case IRW_MULHI: {
                    int op2 = in->w == IRW_BAND ? 0 : in->w == IRW_BOR ? 1 : in->w == IRW_BXOR ? 2
                            : in->w == IRW_SHL ? 3 : in->w == IRW_SHR ? 4 : in->w == IRW_WSHL ? 5
                            : in->w == IRW_WSHR ? 6 : in->w == IRW_ROTL ? 7
                            : in->w == IRW_CLMULLO ? 9 : in->w == IRW_CLMULHI ? 10 : in->w == IRW_MULHI ? 11 : 8;
                    low_fpf(out, "    st[sp-2] = lw_int(lw_bit2({}, {}, lw_want_int(st[sp-2], \"ints\"),"
                                 " lw_want_int(st[sp-1], \"ints\"))); sp--;\n", PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((int)(op2)));
                    break;
                }
                case IRW_BNOT: case IRW_POPCNT: case IRW_CLZ: case IRW_CTZ: case IRW_BSWAP: {
                    int op1 = in->w == IRW_BNOT ? 0 : in->w == IRW_POPCNT ? 1 : in->w == IRW_CLZ ? 2
                            : in->w == IRW_CTZ ? 3 : 4;
                    low_fpf(out, "    st[sp-1] = lw_int(lw_bit1({}, {}, lw_want_int(st[sp-1], \"ints\")));\n",
                            PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((int)(op1)));
                    break;
                }
                case IRW_FNREF: {
                    // ★ `unsafe_fn <op>` — export extern op 의 **C 심볼 주소**를 lowv .p 에 담는다.
                    //   그 심볼은 계약을 검사하는 경계 래퍼다(RFC-0066 §4). extern 호출이 (void*).p 로 넘긴다.
                    const low_ir_def_t *F = &ir->defs[in->a & 0xffff];
                    low_puts(out, "    { lowv fr = {0}; fr.p = (const unsigned char *)(void *)&");
                    put_export_name(out, F);   // ★ RFC-0134 — 헤더·정의와 **같은 이름**(경로 이름 또는 `link`)
                    low_puts(out, "; st[sp++] = fr; }\n");
                    break;
                }
                case IRW_EXTERN: {
                    proven_size_t vargc = (proven_size_t)((proven_u64)in->a >> 16);
                    const low_ir_def_t *E = &ir->defs[in->a & 0xffff];
                    bool eflt = cbe_ret_flt(E);   // ★ f64 반환은 double 로 받아 lw_flt 로 민다
                    bool eptr = E->out_ptr;       // ★ 생 포인터(cstr) 반환은 const char* → lw_int(포인터)
                    const char *crty = eptr ? "const char *" : eflt ? "double" : "long long";
                    const char *push = eptr ? "((lowv){ .tag = LWV_SLICE, .p = (const unsigned char *)cr_, .n = 0 })"   /* ★ RFC-0137 P3 — 포인터 칸 */
                                            : eflt ? "lw_flt(cr_)" : "lw_int(cr_)";
                    if (vargc) {
                        // ★★★ **가변인자 씨 호출** (RFC-0063 §5) — 인자가 **태그 스택**에 있다(호출 지점에서
                        //   밀렸다). 고정 인자는 param 정보로, **가변 인자는 정수(long long)로** 넘긴다.
                        //   ★ 첫 구현은 **정수 가변인자**다(`int sum(int n, ...)`). float(SSE)·문자열·포맷
                        //     인지 검사는 후속 — 정수 아닌 가변 인자는 lw_want_int 가 런타임에 정직히 트랩한다.
                        low_fpf(out, "    {{ {} cr_ = ", proven_arg_cstr(crty));
                        low_write(out, E->link_name.ptr, E->link_name.size);
                        low_putc(out, '(');
                        proven_size_t n = 0;
                        for (proven_size_t vi = 0; vi < vargc; vi++) {
                            proven_size_t off = vargc - vi;   // st[sp-off]
                            if (vi < E->nparams && ((E->param_cap >> vi) & 1u)) continue;   // cap 은 C 로 안 간다
                            const char *sep = n++ ? ", " : "";
                            if (vi < E->nparams && (((E->param_uptr >> vi) & 1u) || ((E->param_ufn >> vi) & 1u)))
                                low_fpf(out, "{}(void *)st[sp-{}].p", proven_arg_cstr(sep), PROVEN_ARG((size_t)(off)));   // 생 포인터·콜백
                            else if (vi < E->nparams && ((E->param_struct >> vi) & 1u))
                                low_fpf(out, "{}lw_sty_{}_of(st[sp-{}])", proven_arg_cstr(sep),
                                        PROVEN_ARG((unsigned)((unsigned)E->param_sidx[vi])), PROVEN_ARG((size_t)(off)));   // 구조체 by-value(레코드·뷰 둘 다)
                            else if (vi < E->nparams && ((E->param_cstr >> vi) & 1u))   // ★ cstr — 슬라이스 칸의 포인터(RFC-0137 P3)
                                low_fpf(out, "{}(const char *)st[sp-{}].p", proven_arg_cstr(sep), PROVEN_ARG((size_t)(off)));
                            else if (vi < E->nparams && ((E->param_slice >> vi) & 1u))
                                low_fpf(out, "{}({}{} *)st[sp-{}].p, st[sp-{}].n", proven_arg_cstr(sep), proven_arg_cstr(((E->param_mutw >> vi) & 1u) ? "" : "const "),
                                        proven_arg_cstr(cbe_slice_ctype(E->param_ebits[vi], E->param_selem[vi])), PROVEN_ARG((size_t)(off)), PROVEN_ARG((size_t)(off)));
                            else if (vi < E->nparams && ((E->param_flt >> vi) & 1u))
                                low_fpf(out, "{}lw_fval(st[sp-{}])", proven_arg_cstr(sep), PROVEN_ARG((size_t)(off)));
                            else
                                low_fpf(out, "{}lw_varg(st[sp-{}])", proven_arg_cstr(sep), PROVEN_ARG((size_t)(off)));   // ★ RFC-0137 P3 — 정수 또는 cstr(포인터 칸)
                        }
                        low_fpf(out, "); sp -= {}; st[sp++] = {}; }}\n", PROVEN_ARG((size_t)(vargc)), proven_arg_cstr(push));
                        break;
                    }
                    // ── 고정 arity extern (합성 본문) — 파라미터를 a[] 에서 읽는다 ──────────────
                    low_fpf(out, "    {{ {} cr_ = ", proven_arg_cstr(crty));
                    low_write(out, E->link_name.ptr, E->link_name.size);
                    low_putc(out, '(');
                    proven_size_t n = 0;
                    for (proven_size_t q = 0; q < E->nparams; q++) {
                        if ((E->param_cap >> q) & 1u) continue;   // ★ 능력은 C 로 안 간다
                        if ((E->param_uptr >> q) & 1u)            // ★ unsafe_ptr — 단일 생 포인터
                            low_fpf(out, "{}(void *)a[{}].p", proven_arg_cstr(n++ ? ", " : ""), PROVEN_ARG((size_t)((size_t)q)));
                        else if ((E->param_ufn >> q) & 1u)        // ★ unsafe_fn — 콜백(함수 포인터, .p 에 심볼 주소)
                            low_fpf(out, "{}(void *)a[{}].p", proven_arg_cstr(n++ ? ", " : ""), PROVEN_ARG((size_t)((size_t)q)));
                        else if ((E->param_struct >> q) & 1u)     // ★ 구조체 by-value — 바이트를 struct 로 읽는다
                            low_fpf(out, "{}lw_sty_{}_of(a[{}])", proven_arg_cstr(n++ ? ", " : ""),
                                    PROVEN_ARG((unsigned)((unsigned)E->param_sidx[q])), PROVEN_ARG((size_t)((size_t)q)));   // 레코드·뷰 둘 다 실체화
                        else if ((E->param_cstr >> q) & 1u)       // ★ cstr — 슬라이스 칸의 포인터(RFC-0137 P3)
                            low_fpf(out, "{}(const char *)a[{}].p", proven_arg_cstr(n++ ? ", " : ""), PROVEN_ARG((size_t)((size_t)q)));
                        else if ((E->param_slice >> q) & 1u)
                            low_fpf(out, "{}({}{} *)a[{}].p, a[{}].n", proven_arg_cstr(n++ ? ", " : ""), proven_arg_cstr(((E->param_mutw >> q) & 1u) ? "" : "const "),
                                    proven_arg_cstr(cbe_slice_ctype(E->param_ebits[q], E->param_selem[q])), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)q)));
                        else if ((E->param_flt >> q) & 1u)
                            low_fpf(out, "{}lw_fval(a[{}])", proven_arg_cstr(n++ ? ", " : ""), PROVEN_ARG((size_t)((size_t)q)));
                        else
                            low_fpf(out, "{}lw_want_int(a[{}], \"ints\")", proven_arg_cstr(n++ ? ", " : ""), PROVEN_ARG((size_t)((size_t)q)));
                    }
                    low_fpf(out, ");\n      st[sp++] = {}; }}\n", proven_arg_cstr(push));
                    break;
                }
                case IRW_ASM: {
                    const low_ir_asm_t *A = &ir->asms[in->a];
                    if (!cbe_asm_target_ok(A->target)) {
                        // ★ **조용히 빼지 않는다.** 다른 ISA 의 asm 을 못 본 척하면
                        //   그 op 은 **아무것도 안 하는 op** 이 되어 조용히 틀린다.
                        // ★★★ **실패는 −1 이다** (2026-08-02 수리). 여기는 `return 1` 이었고,
                        //   호출자는 `n > 0` 을 **성공**으로 읽는다 ⇒ 실패 표식이 *"1개 방출"* 과
                        //   구별되지 않아 **종료 코드가 0** 이었다. 오류를 찍고 0 으로 나가는 도구는
                        //   게이트를 눈멀게 한다. 한 반환값에 두 뜻을 담았던 것이 병인이다.
                        low_fpf(low_stderr(), "E-ASM-TARGET: this op is written for `{}` and this build "
                                "targets `{}`. Assembly is not portable and the tool will not "
                                "pretend it is — give the op a portable body, or build for that "
                                "target with `--target`\n",
                                low_sv((const char *)A->target.ptr, (int)A->target.size),
                                proven_arg_cstr(low_ir_target()->name));
                        return -1;
                    }
                    cbe_asm_stmt(out, A);
                    break;
                }
                case IRW_LEN:      low_puts(out, "    st[sp-1] = lw_len(st[sp-1]);\n"); break;
                case IRW_INDEX:    low_fpf(out, "    st[sp-2] = {}(st[sp-2], st[sp-1]); sp--;\n",
                                           proven_arg_cstr((in->a & IR_POL_PROVEN) ? "lw_index_nc" : "lw_index")); break;
                case IRW_ELEMCK:   low_fpf(out, "    st[sp-2] = lw_int(lw_elemck(st[sp-2], st[sp-1], {})); sp--;\n",
                                           PROVEN_ARG((int)((int)(in->a & 3)))); break;
                case IRW_ISTORE:   low_fpf(out, "    {}(st[sp-3], st[sp-2], st[sp-1]); sp -= 3;\n",
                                           proven_arg_cstr((in->a & IR_POL_PROVEN) ? "lw_istore_nc" : "lw_istore")); break;
                case IRW_SWAP:     low_puts(out, "    lw_swap(st[sp-3], st[sp-2], st[sp-1]); sp -= 3; st[sp++] = lw_int(0);\n"); break;
                case IRW_SUBSLICE: low_puts(out, "    st[sp-3] = lw_subslice(st[sp-3], st[sp-2], st[sp-1]); sp -= 2;\n"); break;
                // ★★★ **level-3 atomic** (RFC-0018) — place = 슬라이스 + 인덱스. esz 는 런타임 box.
                case IRW_ALOAD:
                    low_fpf(out, "    {{ lowv s_=st[sp-2]; long long i_=st[sp-1].i; int e_=s_.box?s_.box:8; if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ >= s_.n)) lw_panic(\"atomic index out of bounds\");"
                          " st[sp-2]=lw_int(lw_at_rmw((unsigned char*)s_.p,i_,e_,0,0,{})); sp--; }}\n", PROVEN_ARG((int)((int)(in->a & 7))));
                    break;
                case IRW_ASTORE: case IRW_AADD: case IRW_ASUB: case IRW_AAND:
                case IRW_AOR: case IRW_AXOR: case IRW_ASWAP: {
                    int op2 = in->w==IRW_ASTORE?1:in->w==IRW_AADD?2:in->w==IRW_ASUB?3:in->w==IRW_AAND?4
                            :in->w==IRW_AOR?5:in->w==IRW_AXOR?6:7;
                    low_fpf(out, "    {{ lowv s_=st[sp-3]; long long i_=st[sp-2].i, v_=st[sp-1].i;"
                                 " int e_=s_.box?s_.box:8; if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ >= s_.n)) lw_panic(\"atomic index out of bounds\");"
                                 " st[sp-3]=lw_int(lw_at_rmw((unsigned char*)s_.p,i_,e_,{},v_,{})); sp-=2; }}\n", PROVEN_ARG((int)(op2)), PROVEN_ARG((int)((int)(in->a & 7))));
                    break;
                }
                case IRW_ACAS:
                    low_fpf(out, "    {{ lowv s_=st[sp-4]; long long i_=st[sp-3].i, x_=st[sp-2].i, d_=st[sp-1].i;"
                          " int e_=s_.box?s_.box:8; if (LW_UNLIKELY(i_ < 0 || (unsigned long long)i_ >= s_.n)) lw_panic(\"atomic index out of bounds\");"
                          " st[sp-4]=lw_int(lw_at_cas((unsigned char*)s_.p,i_,e_,x_,d_,{})); sp-=3; }}\n", PROVEN_ARG((int)((int)(in->a & 7))));
                    break;
                case IRW_TIMENOW:
                    low_puts(out, "    st[sp++] = lw_int(lw_time_now());\n"); break;
                case IRW_TIMESLEEP:
                    low_puts(out, "    st[sp-1] = lw_int(lw_time_sleep(st[sp-1].i));\n"); break;
                case IRW_TIMELOCAL:
                    low_puts(out, "    st[sp++] = lw_int(lw_time_local());\n"); break;
                // ★ 해시 둘 — VM 과 **같은 상수·같은 순서**여야 한다(low_ir.c 의 짝 주석 참조).
                case IRW_HASH64:
                    low_puts(out, "    { lowv s_ = lw_thru(st[sp-1]); st[sp-1] = lw_int(lw_hash64(s_.p, s_.n)); }\n"); break;
                case IRW_CRC32:
                    low_puts(out, "    { lowv s_ = lw_thru(st[sp-1]); st[sp-1] = lw_int(lw_crc32(s_.p, s_.n)); }\n"); break;
                case IRW_SAMESL:   // ★ RFC-0112 D10 — VM 과 같다: 같은 시작 · 같은 길이
                    low_puts(out, "    { lowv b_ = lw_thru(st[sp-1]); lowv a_ = lw_thru(st[sp-2]); sp--;\n"
                          "      st[sp-1] = lw_int((a_.p == b_.p && a_.n == b_.n) ? 1 : 0); }\n"); break;
                // ★ VM 과 **같은 상수·같은 순서**여야 한다(splitmix64 — low_ir.c 의 짝 참조).
                case IRW_RNGNEXT:
                    low_puts(out, "    st[sp-1] = lw_int(lw_rng_next(st[sp-1].i));\n"); break;
                case IRW_SHA256:
                    low_puts(out, "    { lowv d_ = lw_thru(st[sp-1]); lowv s_ = lw_thru(st[sp-2]); sp--;\n"
                          "      st[sp-1] = lw_int(lw_sha256(s_.p, s_.n, (unsigned char *)d_.p, d_.n)); }\n"); break;
                case IRW_SHA512:
                    low_puts(out, "    { lowv d_ = lw_thru(st[sp-1]); lowv s_ = lw_thru(st[sp-2]); sp--;\n"
                          "      st[sp-1] = lw_int(lw_sha512(s_.p, s_.n, (unsigned char *)d_.p, d_.n)); }\n"); break;
                case IRW_SHA384:
                    low_puts(out, "    { lowv d_ = lw_thru(st[sp-1]); lowv s_ = lw_thru(st[sp-2]); sp--;\n"
                          "      st[sp-1] = lw_int(lw_sha384(s_.p, s_.n, (unsigned char *)d_.p, d_.n)); }\n"); break;
                case IRW_AESCTR:
                    low_puts(out, "    { lowv d_ = lw_thru(st[sp-1]); lowv s_ = lw_thru(st[sp-2]);\n"
                          "      lowv c_ = lw_thru(st[sp-3]); lowv k_ = lw_thru(st[sp-4]); sp -= 3;\n"
                          "      st[sp-1] = lw_int(lw_aes_ctr_x(k_.p, k_.n, (void *)c_.p, c_.n,\n"
                          "                                   s_.p, s_.n, (void *)d_.p, d_.n)); }\n"); break;
                case IRW_CHACHA20:
                    low_puts(out, "    { lowv d_ = lw_thru(st[sp-1]); lowv s_ = lw_thru(st[sp-2]);\n"
                          "      lowv c_ = lw_thru(st[sp-3]); lowv k_ = lw_thru(st[sp-4]); sp -= 3;\n"
                          "      st[sp-1] = lw_int(lw_chacha20_x(k_.p, k_.n, (void *)c_.p, c_.n,\n"
                          "                                   s_.p, s_.n, (void *)d_.p, d_.n)); }\n"); break;
                case IRW_POLY1305:
                    low_puts(out, "    { lowv d_ = lw_thru(st[sp-1]); lowv t_ = lw_thru(st[sp-2]); sp--;\n"
                          "      st[sp-1] = lw_int(lw_poly1305_x((void *)t_.p, t_.n, d_.p, d_.n)); }\n"); break;
                case IRW_AESGCM:
                    low_puts(out, "    { lowv d_ = lw_thru(st[sp-1]); lowv s_ = lw_thru(st[sp-2]);\n"
                          "      lowv z_ = lw_thru(st[sp-3]); lowv h_ = lw_thru(st[sp-4]);\n"
                          "      lowv c_ = lw_thru(st[sp-5]); lowv k_ = lw_thru(st[sp-6]); sp -= 5;\n"
                          "      st[sp-1] = lw_int(lw_aes_gcm_x(k_.p, k_.n, (void *)c_.p, c_.n, h_.p, h_.n,\n"
                          "                                     (void *)z_.p, z_.n, s_.p, s_.n,\n"
                          "                                     (void *)d_.p, d_.n)); }\n"); break;
                case IRW_CHAPOLY:
                    low_puts(out, "    { lowv d_ = lw_thru(st[sp-1]); lowv s_ = lw_thru(st[sp-2]);\n"
                          "      lowv t_ = lw_thru(st[sp-3]); lowv c_ = lw_thru(st[sp-4]);\n"
                          "      lowv k_ = lw_thru(st[sp-5]); sp -= 4;\n"
                          "      st[sp-1] = lw_int(lw_chacha_poly_x(k_.p, k_.n, (void *)c_.p, c_.n,\n"
                          "                                         (void *)t_.p, t_.n, s_.p, s_.n,\n"
                          "                                         (void *)d_.p, d_.n)); }\n"); break;
                case IRW_AESROUND: case IRW_AESLAST:
                    low_fpf(out, "    {{ lowv r_ = lw_thru(st[sp-1]); lowv t_ = lw_thru(st[sp-2]); sp--;\n"
                                 "      st[sp-1] = lw_int(lw_aes_round_x((void *)t_.p, t_.n, r_.p, r_.n, {})); }}\n",
                            PROVEN_ARG((int)(in->w == IRW_AESLAST ? 1 : 0)));
                    break;
                case IRW_GHASH:
                    low_puts(out, "    { lowv d_ = lw_thru(st[sp-1]); lowv z_ = lw_thru(st[sp-2]);\n"
                          "      lowv h_ = lw_thru(st[sp-3]); sp -= 2;\n"
                          "      st[sp-1] = lw_int(lw_ghash_x(h_.p, h_.n, (void *)z_.p, z_.n, d_.p, d_.n)); }\n"); break;
                case IRW_RANDBYTES:
                    low_puts(out, "    { lowv d_ = lw_thru(st[sp-1]); unsigned long hfk_ = 1;\n"
                          "      int hf_ = lw_hf_probe(\"random\", &hfk_);\n"
                          "      st[sp-1] = lw_int((hf_ == 1) ? 0 :\n"
                          "        lw_rand_bytes((unsigned char *)d_.p, (hf_ == 2) ? lw_hf_cap(d_.n, hfk_) : d_.n)); }\n"); break;
                case IRW_TTYRAW:
                    low_puts(out, "    st[sp-1] = lw_int(lw_tty_raw(st[sp-1].i));\n"); break;
                case IRW_TTYREAD:
                    low_puts(out, "    st[sp-1] = lw_tty_read(st[sp-1]);\n"); break;
                case IRW_TTYSIZE:
                    low_puts(out, "    st[sp++] = lw_tty_size();\n"); break;
                case IRW_PREFETCH:
                    // ★★★ **유일하게 승격된 특례** (RFC-0104 §8-14): 이식 가능 157.1 ms 대
                    //   특례 117.5 ms = **+25.2%**, 문턱 5% 를 넘는다. 그리고 순수 힌트라
                    //   **결과를 안 바꾼다** — VM 은 이 op 을 무시하고, 답은 여전히 같다.
                    low_puts(out, "    { long long i_=st[--sp].i; lowv s_=st[--sp];\n"
                          "      if (s_.p && (unsigned long long)i_ < s_.n) __builtin_prefetch(s_.p + i_, 0, 1);\n"
                          "      st[sp++] = lw_int(0); }\n"); break;
                case IRW_AFENCE:
                    // ★ 규약대로 자리 하나를 민다(위 빠른 경로의 주석 참조).
                    low_fpf(out, "    atomic_thread_fence(lw_mo({}));\n", PROVEN_ARG((int)((int)(in->a & 7))));
                    low_puts(out, "    st[sp++] = lw_int(0);\n"); break;
                case IRW_WRAP_OK:  low_puts(out, "    st[sp-1] = lw_wrap_ok(st[sp-1]);\n"); break;
                // ★ option 생성자 (RFC-0043 D5 — OOM 은 값이다)
                case IRW_WRAP_SOME:
                    // ★ 박스 풀의 끝을 본다 — lw_wrap_ok 와 같은 검사(전엔 이 자리만 없어 되감지 못하는 루프에서 풀 너머에 썼다 ·
                    //   docs/known-defects/inline-some-past-box-pool.md, 2026-10-25).
                    low_puts(out, "    { lowv v = lw_promote(st[--sp]); if (lw_nbox >= lw_boxlim) lw_panic(\"box pool exhausted — raise it with -DLW_BOXPOOL=N (docs/runtime-pools.md)\"); lw_boxes[lw_nbox] = v;\n"
                          "      st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; }\n");
                    break;
                case IRW_WRAP_NONE:
                    low_puts(out, "    st[sp++] = (lowv){ .tag = LWV_NONE };\n"); break;
                // ★ RFC-0030 D2′ — 프로그램 인자. VM 과 같은 의미: 범위 밖 = none.
                case IRW_ARGC:
                    low_puts(out, "    st[sp++] = (lowv){ .tag = LWV_INT, .i = lw_g_nargs };\n"); break;
                // ★ 환경 조회 — VM 과 **같은 environ** 을 훑는다(같은 환경이면 같은 답).
                case IRW_WRITE:
                    low_puts(out, "    { lowv bv = st[--sp]; lowv fv = st[--sp]; st[sp++] = lw_int(lw_h_write(fv.i, bv.p, bv.n)); }\n");
                    break;
                // ★★★★ RFC-0112 D3 — 피연산자가 뿌리다(0 고정 창 · 1 힙). VM 과 **같은 규칙**(`lw_root_take`).
                //   ★ 박스 풀 가드도 VM 과 같은 자리에서 먼저 본다(전엔 가드 없이 `lw_boxes[lw_nbox]` 에 썼다).
                case IRW_ALLOCB:
                    low_fpf(out, "    {{ lowv nv = st[--sp]; unsigned char *b_;\n"
                          "      if (nv.i < 0 || LW_NOFIT(nv.i)) st[sp++] = (lowv){{ .tag = LWV_NONE }};\n"
                          "      else if (lw_nbox >= lw_boxlim) lw_panic(\"box pool exhausted — raise it with -DLW_BOXPOOL=N (docs/runtime-pools.md)\");\n"
                          "      else if (!(b_ = lw_root_take({}, (size_t)nv.i))) st[sp++] = (lowv){{ .tag = LWV_NONE }};\n"
                          "      else {{ lw_boxes[lw_nbox] = (lowv){{ .tag = LWV_SLICE, .p = b_, .n = (size_t)nv.i }};\n"
                          "             st[sp++] = (lowv){{ .tag = LWV_SOME, .box = lw_nbox++ }}; }} }}\n", PROVEN_ARG((int)((int)in->a)));
                    break;
                // ★★★★ **영역의 표식과 되감기** (WO-0211 에서 찾음). 네이티브에는 이 두 낱말의 case 가
                //   **아예 없었다** — 표식은 아무것도 안 밀어 뒤따르는 STORE 가 스택 밑을 읽었고, 되감기는
                //   값을 밀기만 하고 되감지 않았다. 64 KiB 창 안에서만 돌던 시험이라 답이 같게 보였다.
                case IRW_RMARK:
                    low_fpf(out, "    st[sp++] = lw_int((long long)lw_root_mark({}));\n", PROVEN_ARG((int)((int)in->a)));
                    break;
                case IRW_RRESET:
                    low_fpf(out, "    lw_root_reset({}, (size_t)st[--sp].i);\n", PROVEN_ARG((int)((int)in->a)));
                    break;
                // ★ RFC-0069 A5 — 표준입력. VM 과 같은 의미: fd 0 만 · 안 들어가면 none.
                case IRW_RDIN:
                    low_puts(out, "    { lowv dv = st[--sp]; lowv fv = st[--sp]; long long r_ = 0;\n"
                          "      if (!lw_h_rdin(fv.i, (unsigned char *)(void *)(uintptr_t)dv.p, dv.n, &r_)) st[sp++] = (lowv){ .tag = LWV_NONE }; else {\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                // ★★★ **reactor** (RFC-0071 A1) — 네이티브도 **블로킹 폴백만**.
                //   ☞ A1 의 산물은 *"reactor 라는 자리"* 이지 성능이 아니다. 그 자리가 생겨도
                //     **답이 한 바이트도 안 바뀐다**는 것이 게이트이고, epoll(A2)·io_uring(A3)
                //     이 그 게이트를 깨면 즉시 보인다. **되는 것부터 세우고 빠르게 만든다.**
                case IRW_RNEW:
                    low_puts(out, "    { lowv dv = st[--sp]; lowv mv = st[--sp];\n"
                          "      long long need_ = dv.i * 24;\n"
                          "      if (dv.i <= 0 || (long long)mv.n < need_) st[sp++] = (lowv){ .tag = LWV_NONE };\n"
                          "      else { if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          // ★ 뒷받침을 기억한다 — completion 백엔드가 **여기에** 읽는다(D-B).
                          "             lw_rmem = (unsigned char *)(void *)(uintptr_t)mv.p; lw_rmem_n = mv.n;\n"
                          "             lw_boxes[lw_nbox] = lw_int(1);\n"
                          "             st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                // ★★★ **A2 — 논블로킹 + 양보.** 읽을 것이 없으면 **형제에게 넘긴다.**
                //   ☞ 의미는 A1 과 같다: 버퍼가 차면 `none`(더 남았다), EOF 면 `some(읽은 수)`.
                //     ★ **떠보기(peek)가 사라졌다** — 버퍼가 찼는지로 판정하므로 필요가 없다.
                //       A1 이 물어 온 데이터 손실의 **원인 자체가 없어졌다.**
                //   ★ fd 는 이제 **아무 것이나** 된다(A1 은 stdin 만). 그래야 파이프 둘로
                //     *"형제가 굶지 않는다"* 를 **인과로** 잴 수 있다(RFC-0071 A2).
                case IRW_RREAD:
                    low_puts(out, "    { lowv dv = st[--sp]; lowv fv = st[--sp]; lowv rv = st[--sp];\n"
                          "      if (rv.i != 1) lw_panic(\"not a reactor handle\");\n"
                          "      if (fv.i < 0) lw_panic(\"not a file descriptor\");\n"
                          "      { int fd_ = (int)fv.i; unsigned char *d_ = (unsigned char *)(void *)(uintptr_t)dv.p;\n"
                          "        size_t g_ = 0; int eof_ = 0;\n"
                          // ★★★ **reactor 의 뒷받침은 그 자신의 것이다** (RFC-0022 D-B, 2026-07-20 실측).
                          //   completion 백엔드는 **그 자리에** 읽고 나서 목적지로 옮긴다. 목적지가
                          //   그 자리와 겹치면 **덮어쓴다** — 실측: rcat 이 같은 슬라이스를 둘 다에
                          //   주었고, readiness 에선 무해했지만 AIO 에선 **내용이 망가졌다.**
                          //   ★★ **모든 백엔드에서 똑같이 거절한다** — 백엔드마다 다르게 굴면
                          //     *"어느 것을 끼워도 답이 같다"* 가 깨진다. 규칙이 백엔드보다 위다.
                          "        if (lw_rmem && d_ < lw_rmem + lw_rmem_n && lw_rmem < d_ + dv.n)\n"
                          "          lw_panic(\"the reactor's backing bytes are ITS OWN: a read destination may not overlap them (RFC-0022 D-B)\");\n"
                          "        lw_rb_pick();\n"
                          // ★★★ **completion 경로** — 제출하고 자고, 완료되면 수확한다.
                          //   readiness 와 **의미가 같아야 한다**: 버퍼가 차면 none, EOF 면 some(읽은 수).
                          "        if (lw_rb == LW_RB_AIO) {\n"
                          "          while (g_ < dv.n) {\n"
                          "            int s_ = lw_aio_submit(fd_, d_ + g_, dv.n - g_);\n"
                          "            if (s_ < 0) break;\n"                 /* 슬롯이 없다 → 아래 readiness 로 */
                          "            long r_ = lw_aio_reap(s_);\n"
                          "            while (r_ < 0) { lw_io_wait_aio(s_); r_ = lw_aio_reap(s_); }\n"
                          "            if (r_ == 0) { eof_ = 1; break; }\n"
                          "            g_ += (size_t)r_;\n"
                          "          }\n"
                          "          if (!eof_ && g_ >= dv.n) { st[sp++] = (lowv){ .tag = LWV_NONE }; }\n"
                          "          else { if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "                 lw_boxes[lw_nbox] = lw_int((long long)g_);\n"
                          "                 st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; }\n"
                          "        } else {\n"
                          "        lw_nonblock(fd_);\n"
                          "        while (g_ < dv.n) {\n"
                          "          ssize_t r_ = read(fd_, d_ + g_, dv.n - g_);\n"
                          "          if (r_ > 0) { g_ += (size_t)r_; continue; }\n"
                          "          if (r_ == 0) { eof_ = 1; break; }\n"
                          "          if (errno == EAGAIN || errno == EWOULDBLOCK) { lw_io_wait(fd_); continue; }\n"
                          "          if (errno == EINTR) continue;\n"
                          "          eof_ = 1; break;\n"
                          "        }\n"
                          "        if (!eof_) st[sp++] = (lowv){ .tag = LWV_NONE };\n"
                          "        else { if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "               lw_boxes[lw_nbox] = lw_int((long long)g_);\n"
                          "               st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } } } }\n");
                    break;
                // ★★★★ **쓰기 쪽** (2026-08-22, N4 단계1) — `r_read` 의 거울.
                //
                //   ☞ **부분 진행을 잃지 않는다.** 이 리프는 짧게 답하고 마는 것이 *아니라*
                //     준비성을 기다리며 **다 쓸 때까지 돈다**(EAGAIN → `lw_io_wait`, 그 사이
                //     다른 태스크가 돈다 — 그것이 reactor 를 타는 이유다). 그러나 도중에
                //     **실패하면 간 만큼을 답한다**(`none` 이 아니라). 그 수를 버리면 호출자는
                //     *"얼마나 갔는지"* 를 못 보고 **어디서부터 다시 보낼지 모른다.**
                //     ⇒ `none` 은 **한 바이트도 못 간** 실패에만 쓴다.
                //
                //   ★★ **AIO 백엔드를 안 탄다.** POSIX AIO 는 **파일 지향**이고, 이 리프가
                //     결국 실릴 자리는 소켓이다(N4 단계2). 그리고 읽기 쪽과 달리 쓰기는
                //     completion 으로 얻을 것이 없다 — 부분 쓰기를 그대로 답할 것이므로
                //     "다 될 때까지 자고 수확" 이 오히려 의미를 바꾼다.
                //     ⇒ readiness(poll·epoll)만 탄다. **안 되는 것을 되는 척하지 않는다**:
                //       RFC-0071 A3 의 *"세 백엔드가 바이트까지 같다"* 는 **읽기의 주장**이다.
                case IRW_RWRITE:
                    low_puts(out, "    { lowv sv = st[--sp]; lowv fv = st[--sp]; lowv rv = st[--sp];\n"
                          "      if (rv.i != 1) lw_panic(\"not a reactor handle\");\n"
                          "      if (fv.i < 0) lw_panic(\"not a file descriptor\");\n"
                          "      { int fd_ = (int)fv.i; const unsigned char *s_ = (const unsigned char *)(const void *)(uintptr_t)sv.p;\n"
                          "        size_t p_ = 0; int err_ = 0;\n"
                          "        lw_rb_pick();\n"
                          "        lw_nonblock(fd_);\n"
                          "        while (p_ < sv.n) {\n"
                          "          ssize_t w_ = write(fd_, s_ + p_, sv.n - p_);\n"
                          "          if (w_ > 0) { p_ += (size_t)w_; continue; }\n"
                          "          if (w_ < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { lw_io_wait(fd_); continue; }\n"
                          "          if (w_ < 0 && errno == EINTR) continue;\n"
                          "          err_ = 1; break;\n"
                          "        }\n"
                          "        if (err_ && p_ == 0) { st[sp++] = (lowv){ .tag = LWV_NONE }; }\n"
                          "        else { if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "               lw_boxes[lw_nbox] = lw_int((long long)p_);\n"
                          "               st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } } }\n");
                    break;
                // ★ RFC-0069 §6 스트림 리프 — VM 과 **같은 의미**(같은 libc 를 부른다).
                case IRW_FOPEN:
                    low_puts(out, "    { lowv mv = st[--sp]; lowv pv = st[--sp]; long long r_ = 0;\n"
                          "      if (!lw_h_fopen(pv.p, pv.n, mv.i, &r_)) st[sp++] = (lowv){ .tag = LWV_NONE }; else {\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                case IRW_FREAD:
                    low_puts(out, "    { lowv dv = st[--sp]; lowv hv = st[--sp]; long long r_ = 0;\n"
                          "      if (!lw_h_fread(hv.i, (unsigned char *)(void *)(uintptr_t)dv.p, dv.n, &r_)) st[sp++] = (lowv){ .tag = LWV_NONE }; else {\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                case IRW_FWRITE:
                    low_puts(out, "    { lowv bv = st[--sp]; lowv hv = st[--sp]; long long r_ = 0;\n"
                          "      if (!lw_h_fwrite(hv.i, bv.p, bv.n, &r_)) st[sp++] = (lowv){ .tag = LWV_NONE }; else {\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                case IRW_FCLOSE:
                    low_puts(out, "    { lowv hv = st[--sp]; st[sp++] = lw_int(lw_h_fclose(hv.i)); }\n");
                    break;
                // ★★★ 소켓 리프 (cap net) — VM 과 같은 의미(AF_UNIX socketpair · socks[i]=fd+1, 0=빈).
                case IRW_NPAIR:
                    low_puts(out, "    {  long long r_ = 0;\n"
                          "      if (!lw_h_npair(&r_)) st[sp++] = (lowv){ .tag = LWV_NONE }; else {\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                case IRW_NSEND:
                    low_puts(out, "    { lowv bv = st[--sp]; lowv hv = st[--sp]; long long r_ = 0;\n"
                          "      if (!lw_h_nsend(hv.i, bv.p, bv.n, &r_)) st[sp++] = (lowv){ .tag = LWV_NONE }; else {\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                case IRW_NRECV:
                    low_puts(out, "    { lowv dv = st[--sp]; lowv hv = st[--sp]; long long r_ = 0;\n"
                          "      if (!lw_h_nrecv(hv.i, (unsigned char *)(void *)(uintptr_t)dv.p, dv.n, &r_)) st[sp++] = (lowv){ .tag = LWV_NONE }; else {\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                case IRW_NCLOSE:
                    low_puts(out, "    { lowv hv = st[--sp]; st[sp++] = lw_int(lw_h_nclose(hv.i)); }\n");
                    break;
                // ★★★ 네트워크 면 — VM 과 같은 의미(127.0.0.1 TCP · 포트 0 = 임시포트).
                case IRW_NLISTEN:
                    low_puts(out, "    { lowv pv = st[--sp]; long long r_ = 0;\n"
                          "      if (!lw_h_nlisten(pv.i, &r_)) st[sp++] = (lowv){ .tag = LWV_NONE }; else {\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                case IRW_NPORT:
                    low_puts(out, "    { lowv hv = st[--sp]; long long r_ = 0;\n"
                          "      if (!lw_h_nport(hv.i, &r_)) st[sp++] = (lowv){ .tag = LWV_NONE }; else {\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                // ★★★ 주소를 받는다 (X-0032, 2026-09-18) — VM 과 **같은 의미**여야 한다.
                case IRW_NCONNECT:
                    low_puts(out, "    { lowv pv = st[--sp]; lowv av = st[--sp]; long long r_ = 0;\n"
                          "      if (!lw_h_nconnect(av.i, pv.i, &r_)) st[sp++] = (lowv){ .tag = LWV_NONE }; else {\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                // ★★★ 이름을 주소로 (X-0032) — VM 과 같은 의미: 첫 A 레코드 하나.
                case IRW_NRESOLVE:
                    low_puts(out, "    { lowv nv = st[--sp]; long long r_ = 0;\n"
                          "      if (!lw_h_nresolve(nv.p, nv.n, &r_)) st[sp++] = (lowv){ .tag = LWV_NONE }; else {\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                // ★★★ RFC-0136 — 프로세스 잎. 런타임은 LW_PROC_RT(쓰는 단위에만 나온다).
                case IRW_PSPAWN:
                    low_puts(out, "    { lowv av = st[--sp];\n"
                          "      long long h_ = lw_proc_spawn((const unsigned char *)(const void *)(uintptr_t)av.p, (size_t)av.n);\n"
                          "      if (h_ < 0) { st[sp++] = (lowv){ .tag = LWV_NONE }; }\n"
                          "      else { if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "             lw_boxes[lw_nbox] = lw_int(h_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                case IRW_PREAD:
                    low_puts(out, "    { lowv dv = st[--sp]; lowv hv = st[--sp];\n"
                          "      long long r_ = lw_proc_read(hv.i, (unsigned char *)(void *)(uintptr_t)dv.p, (size_t)dv.n);\n"
                          "      if (r_ < 0) { st[sp++] = (lowv){ .tag = LWV_NONE }; }\n"
                          "      else { if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "             lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                case IRW_PPOLL: case IRW_PWAIT:
                    low_fpf(out, "    {{ lowv hv = st[--sp]; unsigned long long s_ = 0;\n"
                                 "      int r_ = {}(hv.i, &s_);\n"
                                 "      if (r_ < 0) {{ st[sp++] = (lowv){{ .tag = LWV_NONE }}; }}\n"
                                 "      else {{ if (r_ == 0) s_ = 0xFFFFFFFFFFFFFFFFull;\n"
                                 "             if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                                 "             lw_boxes[lw_nbox] = lw_int((long long)s_); st[sp++] = (lowv){{ .tag = LWV_SOME, .box = lw_nbox++ }}; }} }}\n",
                            proven_arg_cstr(in->w == IRW_PPOLL ? "lw_proc_poll" : "lw_proc_wait"));
                    break;
                case IRW_PKILL:
                    low_puts(out, "    { lowv hv = st[--sp]; st[sp++] = lw_int(lw_proc_kill(hv.i) ? 1 : 0); }\n");
                    break;
                case IRW_NACCEPT:
                    low_puts(out, "    { lowv hv = st[--sp]; long long r_ = 0;\n"
                          "      if (!lw_h_naccept(hv.i, &r_)) st[sp++] = (lowv){ .tag = LWV_NONE }; else {\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                case IRW_FSEEK:
                    low_puts(out, "    { lowv wv = st[--sp], ov = st[--sp], hv = st[--sp]; long long r_ = 0;\n"
                          "      if (!lw_h_fseek(hv.i, ov.i, wv.i, &r_)) st[sp++] = (lowv){ .tag = LWV_NONE }; else {\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                case IRW_DOPEN:
                    low_puts(out, "    { lowv pv = st[--sp]; long long r_ = 0;\n"
                          "      if (!lw_h_dopen(pv.p, pv.n, &r_)) st[sp++] = (lowv){ .tag = LWV_NONE }; else {\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                case IRW_DREAD:
                    low_puts(out, "    { lowv dv = st[--sp]; lowv hv = st[--sp]; long long r_ = 0;\n"
                          "      if (!lw_h_dread(hv.i, (unsigned char *)(void *)(uintptr_t)dv.p, dv.n, &r_)) st[sp++] = (lowv){ .tag = LWV_NONE }; else {\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                case IRW_DCLOSE:
                    low_puts(out, "    { lowv hv = st[--sp]; st[sp++] = lw_int(lw_h_dclose(hv.i)); }\n");
                    break;
                case IRW_FTYPE: case IRW_LTYPE:
                    // ★ 한 함수가 둘을 낸다 — 다른 것은 **따라가는가** 하나뿐이다(stat / lstat · `lw_h_ftype`).
                    low_fpf(out, "    {{ lowv pv = st[--sp]; long long r_ = 0;\n"
                          "      if (!lw_h_ftype(pv.p, pv.n, {}, &r_)) st[sp++] = (lowv){{ .tag = LWV_NONE }}; else {{\n"
                          "      if (lw_nbox >= lw_boxlim) lw_panic(\"box pool\");\n"
                          "      lw_boxes[lw_nbox] = lw_int(r_); st[sp++] = (lowv){{ .tag = LWV_SOME, .box = lw_nbox++ }}; }} }}\n", PROVEN_ARG((int)(in->w == IRW_LTYPE ? 1 : 0)));
                    break;
                case IRW_DMAKE:
                    low_puts(out, "    { lowv pv = st[--sp]; st[sp++] = lw_int(lw_h_dmake(pv.p, pv.n)); }\n");
                    break;
                case IRW_PREMOVE:
                    low_puts(out, "    { lowv pv = st[--sp]; st[sp++] = lw_int(lw_h_premove(pv.p, pv.n)); }\n");
                    break;
                case IRW_PRENAME:
                    low_puts(out, "    { lowv nv = st[--sp], ov = st[--sp]; st[sp++] = lw_int(lw_h_prename(ov.p, ov.n, nv.p, nv.n)); }\n");
                    break;
                case IRW_ENVGET:
                    low_puts(out, "    { lowv nv = st[--sp]; lw_sl s_;\n"
                          "      if (!lw_h_envget(nv.p, nv.n, &s_)) st[sp++] = (lowv){ .tag = LWV_NONE };\n"
                          "      else { lw_boxes[lw_nbox] = (lowv){ .tag = LWV_SLICE, .p = s_.p, .n = s_.n };\n"
                          "             st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                case IRW_ARGV:
                    low_puts(out, "    { lowv iv = st[--sp]; lw_sl s_;\n"
                          "      if (!lw_h_argv(iv.i, &s_)) st[sp++] = (lowv){ .tag = LWV_NONE };\n"
                          "      else { lw_boxes[lw_nbox] = (lowv){ .tag = LWV_SLICE, .p = s_.p, .n = s_.n };\n"
                          "             st[sp++] = (lowv){ .tag = LWV_SOME, .box = lw_nbox++ }; } }\n");
                    break;
                case IRW_CSTR2STR:
                    // ★★★ **cstr → str** (RFC-0068 S4 · C2) — 널종단 C 문자열의 길이를 몰라 스캔한다
                    //   (strlen, O(n) — 이름이 비용을 말한다). 포인터는 정수 lowv 로 실려 있다(extern 반환).
                    //   ★ **뷰다** — 복사 0. 바이트는 C 쪽이 소유하고, str 은 그 위를 가리킨다(D4).
                    low_puts(out, "    { lowv cv = st[--sp]; const char *p_ = (const char *)cv.p;   /* RFC-0137 P3 — 포인터 칸 */\n"
                          "      st[sp++] = (lowv){ .tag = LWV_SLICE, .p = (const unsigned char *)p_, .n = p_ ? strlen(p_) : 0 }; }\n");
                    break;
                case IRW_STR2CSTR:
                    // ★★★ **str_buf → cstr** (RFC-0068 S4 · C4) — 바이트 버퍼의 base 포인터를 cstr 로
                    //   (O(1)·복사 0). 널종단은 봉인돼 있다(D2). 포인터는 정수 lowv 로 실린다.
                    //   ★ RFC-0137 P3 — cstr 는 슬라이스 값 그대로다(포인터를 정수에 싣지 않는다). 방출 0.
                    break;
                case IRW_WRAP_ERR: low_fpf(out, "    st[sp++] = lw_err({});\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_TRY:
                    // ★★★ **오류를 전파하는 조기 반환이 깊이 원장을 새게 했다.**
                    //   `lw_depth--` 를 안 했다 ⇒ 오류를 **128번 넘게 전파하면** 네이티브가
                    //   **"call depth limit exceeded"** 로 죽는다. VM 은 멀쩡히 200을 낸다.
                    //   **오류 전파는 이 언어의 기본 모양인데**, 그것을 루프에서 하는 픽스처가
                    //   **하나도 없어서** 아무도 몰랐다(교훈 6: 픽스처의 모양이 감사의 시야다).
                    // ★ 그리고 **같은 자리에 벡터 풀도 있다**(RFC-0089 B): 조기 반환이 되감기를
                    //   건너뛰면 오류를 루프에서 전파할 때 풀이 마른다 — 깊이 원장과 같은 새는 모양이다.
                    low_puts(out, vecfn ? "    { lowv v = st[--sp]; if (v.tag == LWV_ERR) { lw_depth--; return lw_vecret(vb_, v); }\n"
                                  "      st[sp++] = (v.tag == LWV_OK) ? lw_boxes[v.box] : v; }\n"
                                : "    { lowv v = st[--sp]; if (v.tag == LWV_ERR) { lw_depth--; return v; }\n"
                                  "      st[sp++] = (v.tag == LWV_OK) ? lw_boxes[v.box] : v; }\n");
                    break;
                case IRW_MAKE: {
                    proven_size_t nf = ir->makes[in->a].nfields;
                    if (d->nwin && nf == 0)   // ★ RFC-0132 P4e — 틀 창이 있으면 거기에(모자라면 풀)
                        low_fpf(out, "    st[sp++] = lw_make_w(&fw_, {}, 0, st);\n", PROVEN_ARG((long long)((long long)in->a)));
                    else if (d->nwin)
                        low_fpf(out, "    st[sp-{}] = lw_make_w(&fw_, {}, {}, &st[sp-{}]); sp -= {};\n",
                                PROVEN_ARG((size_t)(nf)), PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((size_t)(nf)), PROVEN_ARG((size_t)(nf)), PROVEN_ARG((size_t)(nf - 1)));
                    else if (nf == 0)
                        low_fpf(out, "    st[sp++] = lw_make({}, 0, st);\n", PROVEN_ARG((long long)((long long)in->a)));
                    else
                        low_fpf(out, "    st[sp-{}] = lw_make({}, {}, &st[sp-{}]); sp -= {};\n",
                                PROVEN_ARG((size_t)(nf)), PROVEN_ARG((long long)((long long)in->a)), PROVEN_ARG((size_t)(nf)), PROVEN_ARG((size_t)(nf)), PROVEN_ARG((size_t)(nf - 1)));
                    break;
                }
                case IRW_SNEW:      low_puts(out, "    st[sp-1] = lw_snew(st[sp-1]);\n"); break;
                case IRW_SPUSH:     low_puts(out, "    st[sp-2] = lw_spush(st[sp-2], st[sp-1]); sp--;\n"); break;
                case IRW_SPOP_INTO: low_fpf(out, "    st[sp-1] = lw_spop(st[sp-1], &loc[{}]);\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_BNEW:      low_puts(out, "    st[sp-1] = lw_bnew(st[sp-1]);\n"); break;
                case IRW_CONTAINS:  low_puts(out, "    st[sp-2] = lw_contains(st[sp-2], st[sp-1]); sp--;\n"); break;
                case IRW_COUNT:     low_puts(out, "    st[sp-1] = lw_count(st[sp-1]);\n"); break;
                case IRW_FIELD: low_fpf(out, "    st[sp-1] = lw_field(st[sp-1], {});\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_FSTORE: low_fpf(out, "    lw_fstore(st[sp-2], {}, lw_fw_keep({}, st[sp-2], st[sp-1])); sp -= 2;\n",
                                         PROVEN_ARG((long long)((long long)in->a)), proven_arg_cstr(d->nwin ? "&fw_" : "0")); break;   // ★ P4e — 남의 레코드 칸이면 풀로 올린다
                case IRW_VIEW:     low_fpf(out, "    st[sp-1] = lw_view(st[sp-1], {});\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_TRYVIEW:  low_fpf(out, "    st[sp-1] = lw_tryview(st[sp-1], {});\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_ISOK:     low_puts(out, "    st[sp-1] = lw_isok(st[sp-1]);\n"); break;
                case IRW_ISERR:    low_puts(out, "    st[sp-1] = lw_iserr(st[sp-1]);\n"); break;
                case IRW_OKVAL:    low_puts(out, "    st[sp-1] = lw_okval(st[sp-1]);\n"); break;
                case IRW_ERRVAL:   low_puts(out, "    st[sp-1] = lw_errval(st[sp-1]);\n"); break;
                case IRW_ISSOME:   low_puts(out, "    st[sp-1] = lw_issome(st[sp-1]);\n"); break;
                case IRW_SOMEVAL:  low_puts(out, "    st[sp-1] = lw_someval(st[sp-1]);\n"); break;
                case IRW_VALOR:    low_puts(out, "    st[sp-2] = lw_value_or(st[sp-2], st[sp-1]); sp--;\n"); break;
                case IRW_HASVAL:   low_puts(out, "    st[sp-1] = lw_int(lw_has_value(st[sp-1]));\n"); break;
                case IRW_SPOP:     low_puts(out, "    st[sp-1] = lw_poptot(st[sp-1]);\n"); break;
                case IRW_ENCODE:   low_fpf(out, "    st[sp-1] = lw_encode(st[sp-1], {});\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_VARRAY:   low_fpf(out, "    st[sp-1] = lw_varray(st[sp-1], {});\n", PROVEN_ARG((long long)((long long)in->a))); break;
                // ★ SPEC-004 §190 T0 — 무비용 비트 재해석. VM 과 **같은 규칙**(폭은 안 바꾼다).
                case IRW_BITCAST:  low_fpf(out, "    st[sp-1] = lw_bitcast(st[sp-1], {});\n", PROVEN_ARG((long long)((long long)in->a))); break;
                // ★ `panic` — 즉시 트랩. 네이티브도 **같은 자리에서** 멈춘다.
                case IRW_PANIC:    low_puts(out, in->a == IR_PANIC_SINKFULL ? "    lw_panic(\"" LW_SINKFULL_MSG "\");\n"   // ★ X-0065
                                             : in->a == IR_PANIC_ENUMCAST ? "    lw_panic(\"" LW_ENUMCAST_MSG "\");\n"
                                             : in->a == IR_PANIC_NODEKIDS ? "    lw_panic(\"" LW_NODEKIDS_MSG "\");\n"
                                             : "    lw_upanic(\"the program called `panic`\");\n"); break;
                // ★ SPEC-007 §28 — 채널 전환(값은 그대로, 채널만 바뀐다).
                case IRW_ELSE_NONE: low_puts(out, "    st[sp-1] = lw_else_none(st[sp-1]);\n"); break;
                case IRW_ELSE_ERR:  low_fpf(out, "    st[sp-1] = lw_else_err(st[sp-1], {});\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_STR:      low_fpf(out, "    st[sp++] = lw_strv({});\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_LBUF: {   // ★ T2b-2 — 틀 안 나열 자리: 0 으로 채운 쓸 수 있는 바이트 슬라이스
                    unsigned long long off = (unsigned long long)in->a & 0xffffffffull, len = (unsigned long long)in->a >> 32;
                    low_fpf(out, "    {{ lowv lv_ = {{0}}; memset(lw_lb + {}, 0, {}); lv_.tag = LWV_SLICE; lv_.p = lw_lb + {}; lv_.n = {}; st[sp++] = lv_; }}\n",
                            PROVEN_ARG((unsigned long long)(off)), PROVEN_ARG((unsigned long long)(len)), PROVEN_ARG((unsigned long long)(off)), PROVEN_ARG((unsigned long long)(len)));
                    break;
                }
                case IRW_SCOPY:    low_fpf(out, "    lw_scopy(st[sp-2], st[sp-1]); sp -= 2; st[sp++] = lw_int(0);\n"); break;
                case IRW_XCHK:     low_fpf(out, "    lw_xchk(st[sp-2], st[sp-1], {}); sp -= 2; st[sp++] = lw_int(0);\n", PROVEN_ARG((int)((int)in->a))); break;   // ★ REQ-0036
                case IRW_BFILL: {  // ★ §13.7 — 받은 바이트를 나열의 본으로 채운다
                    unsigned long long len = (unsigned long long)in->a >> 32, si1 = (unsigned long long)in->a & 0xffffffffull;
                    low_fpf(out, "    {{ lowv s_ = st[sp-1]; if (s_.n < {}ULL) lw_upanic(\"the allocator gave fewer bytes than the list needs\"); ", PROVEN_ARG((unsigned long long)(len)));
                    if (si1) low_fpf(out, "memcpy((void *)s_.p, lw_strs[{}].p, {}); ", PROVEN_ARG((unsigned long long)(si1 - 1)), PROVEN_ARG((unsigned long long)(len)));
                    else low_fpf(out, "memset((void *)s_.p, 0, {}); ", PROVEN_ARG((unsigned long long)(len)));
                    low_fpf(out, "s_.n = {}; st[sp-1] = s_; }}\n", PROVEN_ARG((unsigned long long)(len)));
                    break;
                }
                case IRW_LBUFC: {  // ★ T2b-3 — 틀 안 자리를 상수 본으로 채운다(풀의 바이트를 베낀다)
                    unsigned long long off = (unsigned long long)in->a & 0xffffffffull, si = (unsigned long long)in->a >> 32;
                    low_fpf(out, "    {{ lowv lv_ = {{0}}; memcpy(lw_lb + {}, lw_strs[{}].p, {}); lv_.tag = LWV_SLICE; lv_.p = lw_lb + {}; lv_.n = {}; st[sp++] = lv_; }}\n",
                            PROVEN_ARG((unsigned long long)(off)), PROVEN_ARG((unsigned long long)(si)), PROVEN_ARG((unsigned long long)((unsigned long long)ir->strs[si].size)), PROVEN_ARG((unsigned long long)(off)), PROVEN_ARG((unsigned long long)((unsigned long long)ir->strs[si].size)));
                    break;
                }
                case IRW_RESBLK:   low_fpf(out, "    st[sp++] = lw_mmioblk((void *)lw_res_{}, {}u);\n",   PROVEN_ARG((size_t)(// ★ RFC-0137 P4 — 배열 그대로
                                            (size_t)in->a)), PROVEN_ARG((unsigned)((unsigned)ir->structs[in->a].total))); break;
                case IRW_MMIOBLK:  low_fpf(out, "    st[sp++] = lw_mmioblk(LW_MMIO_SPAN({}ULL, {}u), {}u);\n",   PROVEN_ARG((unsigned long long)(// ★ RFC-0137 P4 — 경계까지
                                            (unsigned long long)ir->structs[in->a].mmio_base)),
                                            PROVEN_ARG((unsigned)((unsigned)ir->structs[in->a].total)),
                                            PROVEN_ARG((unsigned)((unsigned)ir->structs[in->a].total))); break;
                case IRW_REF:      low_fpf(out, "    st[sp++] = lw_ref(&loc[{}], 0);\n", PROVEN_ARG((long long)((long long)(in->a & 0xffff)))); break;
                case IRW_MREF:     low_fpf(out, "    st[sp++] = lw_ref(&loc[{}], 1);\n", PROVEN_ARG((long long)((long long)(in->a & 0xffff)))); break;
                case IRW_DEREF:    low_puts(out, "    st[sp-1] = lw_deref(st[sp-1]);\n"); break;
                case IRW_SPLAT:    low_fpf(out, "    st[sp-1] = lw_splat({}, st[sp-1]);\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_VLOAD:    low_fpf(out, "    st[sp-2] = lw_vload({}, st[sp-2], st[sp-1]); sp--;\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_VSTORE:   low_fpf(out, "    st[sp-3] = lw_vstore(st[sp-3], st[sp-2], st[sp-1]); sp -= 2;\n"); break;
                case IRW_VLOADM:   low_fpf(out, "    st[sp-4] = lw_vloadm({}, st[sp-4], st[sp-3], st[sp-2], st[sp-1]); sp -= 3;\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_VSTOREM:  low_fpf(out, "    st[sp-4] = lw_vstorem(st[sp-4], st[sp-3], st[sp-2], st[sp-1]); sp -= 3;\n"); break;
                case IRW_VAVG:     low_puts(out, "    st[sp-2] = lw_vavg(st[sp-2], st[sp-1]); sp--;\n"); break;
                case IRW_SELECT:   low_puts(out, "    st[sp-3] = lw_vselect(st[sp-3], st[sp-2], st[sp-1]); sp -= 2;\n"); break;
                case IRW_VREVERSE: low_puts(out, "    st[sp-1] = lw_vperm(0, 0, st[sp-1]);\n"); break;
                case IRW_VROTATE:  low_fpf(out, "    st[sp-1] = lw_vperm(1, {}, st[sp-1]);\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_VSHUFFLE: low_fpf(out, "    st[sp-1] = lw_vperm(2, {}, st[sp-1]);\n", PROVEN_ARG((long long)((long long)in->a))); break;
                case IRW_RADD:     low_puts(out, "    st[sp-1] = lw_vreduce(0, st[sp-1]);\n"); break;
                case IRW_RMUL:     low_puts(out, "    st[sp-1] = lw_vreduce(1, st[sp-1]);\n"); break;
                case IRW_RMIN:     low_puts(out, "    st[sp-1] = lw_vreduce(2, st[sp-1]);\n"); break;
                case IRW_RMAX:     low_puts(out, "    st[sp-1] = lw_vreduce(3, st[sp-1]);\n"); break;
                case IRW_MANY:     low_puts(out, "    st[sp-1] = lw_mred(0, st[sp-1]);\n"); break;
                case IRW_MALL:     low_puts(out, "    st[sp-1] = lw_mred(1, st[sp-1]);\n"); break;
                case IRW_CALL: {
                    const low_ir_def_t *callee = &ir->defs[IR_CALL_IDX(in->a)];
                    // ★ 비워 둔 액터(0)에 보낸 메시지는 멈춘다 — VM 의 E-VM-HANDLE-ZERO 와 같은 게이트(정수 0 만)
                    if (callee->is_actor && callee->nparams > 0)
                        low_fpf(out, "    if (st[sp-{}].tag == LWV_INT && st[sp-{}].i == 0) lw_panic(\"`send` on actor handle 0 \xe2\x80\x94 this actor was never spawned\");\n",
                                PROVEN_ARG((size_t)((size_t)callee->nparams)), PROVEN_ARG((size_t)((size_t)callee->nparams)));
                    // RFC-0055 D3: 증명 안 된 호출에는 파라미터 범위 검사를 방출한다
                    // (증명됐으면 아무것도 안 낸다 — 계약이 공짜).
                    if (!(in->a & IR_POL_PROVEN))
                        for (proven_size_t pi = 0; pi < callee->nparams && pi < LOW_MAX_PARAMS; pi++) {
                            if (!callee->prng[pi].has_rng) continue;
                            low_fpf(out,
                                "    {{ lowv _a = st[sp-{}]; if (_a.tag != LWV_INT || _a.i < {}ll || _a.i > {}ll)"
                                " lw_panic(\"argument is outside the parameter's declared range\"); }}\n",
                                PROVEN_ARG((size_t)((size_t)(callee->nparams - pi))),
                                PROVEN_ARG((long long)((long long)callee->prng[pi].rlo)), PROVEN_ARG((long long)((long long)callee->prng[pi].rhi)));
                        }
                    // ★★★ **bounded mailbox** — VM 의 IRW_CALL 과 **같은 규율**(low_ir.c). actor 핸들러
                    //   호출은 이 인스턴스로 온 메시지 하나다. `mailbox bounded N` 이면 재진입 in-flight
                    //   가 N 을 넘을 수 없다. 인스턴스 레코드 번호로 카운터를 만진다(진입 +1, 반환 −1).
                    bool has_mbox = false; proven_i64 mcap = 0;
                    if (callee->is_actor && callee->nparams > 0) {
                        proven_size_t sidx = callee->param_sidx[0];
                        if (sidx < ir->nstructs && ir->structs[sidx].mailbox_kind == 1) {
                            has_mbox = true; mcap = ir->structs[sidx].mailbox_cap;
                        }
                    }
                    // ★ 실제 인스턴스(LWV_REC)일 때만 — VM 이 `cargs[0].tag == VMV_REC` 로 거는 것과
                    //   같은 게이트다. 차등 스윕은 핸들러를 **합성 구조체 바이트**로 직접 부르는데, 그건
                    //   진짜 spawn 인스턴스가 아니라 `.box` 가 무의미하다. 게이트가 없으면 둘이 갈린다.
                    //   mbi_ 를 호출 전체에 걸쳐 살려 두려고 바깥 블록을 연다(닫는 건 반환 뒤).
                    if (has_mbox)
                        low_fpf(out, "    {{ int mbi_ = st[sp-{}].tag == LWV_REC ? st[sp-{}].box : -1;"
                                     " if (mbi_ >= 0 && ++lw_mbox[mbi_] > {}ll) {{ lw_mbox[mbi_]--; "
                                     "lw_panic(\"the actor's bounded mailbox is full — too many messages in flight "
                                     "to this instance at once (a re-entrant send exceeded `mailbox bounded N`)\"); }}\n",
                                PROVEN_ARG((size_t)((size_t)callee->nparams)), PROVEN_ARG((size_t)((size_t)callee->nparams)), PROVEN_ARG((long long)((long long)mcap)));
                    // ★★★ **restart 슈퍼비전** — actor 에 `failure restart` 정책이 있으면 fault 경계로
                    //   감싸 부른다(핸들러 panic → 상태 초기화 후 재시도, VM 과 같은 규율).
                    proven_i64 maxr = cbe_restart_max(ir, callee);
                    bool has_restart = maxr >= 0;
                    // ★★★ **레코드 풀을 회수한다** — VM 과 **같은 규율**(low_ir.c 의 IRW_CALL).
                    //   스칼라를 돌려주는 호출이 만든 레코드는 **아무도 못 본다** ⇒ 반납한다.
                    // ★★★ 2026-11-11 (W-20261111-0940 O7 · VM 의 IRW_CALL 과 같다) — 액터에게 보내는 기다리는 `send` 는 그 액터를 다른 흐름이 쥐고
                    //   멈춰 있으면 기다리고(LW_SEND_ENTER), 들어간 동안은 이 흐름이 쥔다(LW_SEND_LEAVE 가 놓는다) · `try send`(IR_CALL_TRY)는
                    //   기다리지 않고 `actor_busy` 를 값으로 낸다. 재진입(쥔 것이 이 사슬)은 기다리지 않는다.
                    const bool asnd_ = callee->is_actor && callee->nparams > 0;
                    const bool tsnd_ = asnd_ && (in->a & IR_CALL_TRY);
                    if (asnd_) {
                        low_fpf(out, "    {{ int sbx_ = st[sp-{}].tag == LWV_REC ? st[sp-{}].box : -1;\n", PROVEN_ARG((size_t)((size_t)callee->nparams)), PROVEN_ARG((size_t)((size_t)callee->nparams)));
                        if (tsnd_) low_fpf(out, "    if (LW_ACTOR_BUSY(sbx_)) {{ sp -= {}; st[sp++] = lw_err({}); }} else\n",
                                           PROVEN_ARG((size_t)((size_t)callee->nparams)), PROVEN_ARG((long long)((long long)IR_CALL_TRY_ERR(in->a))));
                        low_puts(out, "    { int syp_ = LW_SEND_ENTER(sbx_);\n");
                    }
                    // ★ 2026-11-11 (O1) — 부름이 참조로 풀 값을 부른 쪽에 써 넣었으면(lw_refw 가 움직였으면) 돌려받지 않는다(VM 의 refw_wm 과 같다)
                    low_fpf(out, "    {{ int wm_ = lw_nrec, wb_ = lw_nbset, wk_ = lw_nstk; unsigned rwr_ = lw_refw; lowv r = ");
                    if (has_restart) {
                        low_puts(out, "lw_restart_call(lw_op_"); put_sym(out, callee);
                        low_fpf(out, ", &st[sp-{}], st[sp-{}].tag == LWV_REC ? st[sp-{}].box : -1, {}ll)",
                                PROVEN_ARG((size_t)((size_t)callee->nparams)), PROVEN_ARG((size_t)((size_t)callee->nparams)), PROVEN_ARG((size_t)((size_t)callee->nparams)), PROVEN_ARG((long long)((long long)maxr)));
                    } else {
                        low_puts(out, "lw_op_"); put_sym(out, callee);
                        low_fpf(out, "(&st[sp-{}])", PROVEN_ARG((size_t)((size_t)(callee->nparams ? callee->nparams : 0))));
                    }
                    low_fpf(out, ";{} if ((r.tag == LWV_INT || r.tag == LWV_FLT) && lw_refw == rwr_) {{ LW_REC_REWIND(wm_); lw_nbset = wb_; lw_nstk = wk_; }}"
                                 " else r = lw_fw_take({}, r);"   /* ★ P4e — 되튐 창의 레코드를 내 창(없으면 풀)으로 */
                                 " sp -= {}; st[sp++] = {}; }}{}\n", proven_arg_cstr(asnd_ ? " LW_SEND_LEAVE(sbx_, syp_);" : ""),
                                 proven_arg_cstr(d->nwin ? "&fw_" : "0"), PROVEN_ARG((size_t)((size_t)callee->nparams)), proven_arg_cstr(tsnd_ ? "lw_wrap_ok(r)" : "r"), proven_arg_cstr(asnd_ ? " } }" : ""));
                    if (has_mbox) low_fpf(out, "    if (mbi_ >= 0) lw_mbox[mbi_]--; }}\n");   // 반환 — in-flight 하나 빠짐 (바깥 블록 닫음)
                    break;
                }
                // ★★★ **async send** (RFC-0009 call/cast 분리) — 핸들러를 부르지 않고 **큐에 넣는다**.
                //   [instance, args…] 팝 → lw_ambox 에 삽입 → 단위값 push. `drain` 이 나중에 비운다.
                //   VM 의 IRW_ASEND(low_ir.c)와 **같은 규율**: 진짜 인스턴스(LWV_REC)만 큐잉하고
                //   (합성 바이트는 `.box` 가 무의미 — 차등 스윕 게이트), `mailbox bounded N` 이면
                //   대기 중 메시지 수가 N 을 넘을 수 없다.
                case IRW_ASEND: {
                    const low_ir_def_t *h = &ir->defs[IR_CALL_IDX(in->a)];
                    size_t np = h->nparams ? h->nparams : 1;
                    bool amb = false; proven_i64 acap = 0;
                    if (h->is_actor && h->nparams > 0) {
                        proven_size_t sidx = h->param_sidx[0];
                        if (sidx < ir->nstructs && ir->structs[sidx].mailbox_kind == 1) {
                            amb = true; acap = ir->structs[sidx].mailbox_cap;
                        }
                    }
                    // ★ 진짜 인스턴스(LWV_REC)가 아니면 **트랩한다** — VM 의 E-VM-TYPE 과 같은 게이트.
                    //   합성 바이트(차등 스윕이 파라미터로 조작한 구조체)는 진짜 spawn 인스턴스가 아니다.
                    //   전엔 조용히 건너뛰어(box<0 이면 큐잉 skip) VM 은 거절하는데 네이티브는 0 을 냈다 — 발산.
                    low_fpf(out, "    {{ if (st[sp-{}].tag == LWV_INT && st[sp-{}].i == 0) lw_panic(\"`spawn send` on actor handle 0 \xe2\x80\x94 this actor was never spawned\");\n"
                                 "      if (st[sp-{}].tag != LWV_REC) lw_panic(\"`spawn send` needs a spawned "
                                 "actor instance (a fabricated struct is not a real instance)\");\n"
                                 "      int box_ = st[sp-{}].box;\n", PROVEN_ARG((size_t)(np)), PROVEN_ARG((size_t)(np)), PROVEN_ARG((size_t)(np)), PROVEN_ARG((size_t)(np)));
                    if (amb)
                        low_fpf(out, "      {{ int pend_ = lw_amb_pend(box_);\n"
                                     "        if (pend_ >= {}ll) lw_panic(\"the actor's bounded mailbox is full — "
                                     "`spawn send` would exceed `mailbox bounded N` pending messages (drain it first, or raise N)\"); }}\n",
                                PROVEN_ARG((long long)((long long)acap)));
                    // ★★★ **넣기 전에 센다** (WO-0207). 전에는 `lw_nambox++` 로 바로 썼다 — VM 은
                    //   `E-VM-MBOX` 로 거절하는데 네이티브는 **배열 밖에 썼다**. 호스트(4096)에서는
                    //   잠복이었고 프리스탠딩 floor(32 칸)에서는 현실이다. 크기는 `sizeof` 에서 온다 —
                    //   두 런타임(호스트·T0)이 같은 코드를 쓰고 새 상수도 안 생긴다.
                    low_fpf(out, "      if (lw_amb_full())"
                                 " lw_panic(\"the task/message queue is exhausted — too many messages and tasks wait at once"
                                 " (docs/runtime-pools.md; on the freestanding floor raise it with -DLW_T0_MBOX=N)\");\n");
                    low_fpf(out, "      {{ int qi_ = lw_amb_push(box_);"
                                 " lw_ambox[qi_].fn = lw_op_");
                    put_sym(out, h);
                    low_fpf(out, "; lw_ambox[qi_].nargs = {}; lw_ambox[qi_].rmax = {}; LW_SET_NOY(qi_, {});\n", PROVEN_ARG((size_t)(np - 1)), PROVEN_ARG((long long)((long long)cbe_restart_max(ir, h))),
                            PROVEN_ARG((int)(cbe_may_suspend(ir, IR_CALL_IDX(in->a), 0) ? 0 : 1)));
                    for (size_t g = 0; g + 1 < np; g++)
                        low_fpf(out, "        lw_ambox[qi_].args[{}] = lw_promote(st[sp-{}]);\n", PROVEN_ARG((size_t)(g)), PROVEN_ARG((size_t)(np - 1 - g)));
                    low_fpf(out, "      }}\n      sp -= {}; st[sp++] = lw_int(0); }}\n", PROVEN_ARG((size_t)(np)));
                    break;
                }
                // ★★★ **try spawn send** — 회복 가능한 backpressure. ASEND 와 같이 큐잉하되, 메일박스가
                //   차면 트랩 대신 `result` 의 err 를 민다(ok=넣었다 · err=찼다). VM 의 IRW_TASEND 와 같은 규율.
                case IRW_TASEND: {
                    const low_ir_def_t *h = &ir->defs[IR_CALL_IDX(in->a)];
                    size_t np = h->nparams ? h->nparams : 1;
                    proven_i64 eidx = (in->a >> 24) & 0xffffff;
                    bool amb = false; proven_i64 acap = 0;
                    if (h->is_actor && h->nparams > 0) {
                        proven_size_t sidx = h->param_sidx[0];
                        if (sidx < ir->nstructs && ir->structs[sidx].mailbox_kind == 1) {
                            amb = true; acap = ir->structs[sidx].mailbox_cap;
                        }
                    }
                    // ★ ASEND 와 같은 게이트 — 진짜 인스턴스가 아니면 트랩(VM 의 E-VM-TYPE 과 일치).
                    low_fpf(out, "    {{ if (st[sp-{}].tag == LWV_INT && st[sp-{}].i == 0) lw_panic(\"`try spawn send` on actor handle 0 \xe2\x80\x94 this actor was never spawned\");\n"
                                 "      if (st[sp-{}].tag != LWV_REC) lw_panic(\"`try spawn send` needs a "
                                 "spawned actor instance (a fabricated struct is not a real instance)\");\n"
                                 "      int box_ = st[sp-{}].box; int full_ = 0;\n", PROVEN_ARG((size_t)(np)), PROVEN_ARG((size_t)(np)), PROVEN_ARG((size_t)(np)), PROVEN_ARG((size_t)(np)));
                    if (amb)
                        low_fpf(out, "      {{ int pend_ = lw_amb_pend(box_); if (pend_ >= {}ll) full_ = 1; }}\n",
                                PROVEN_ARG((long long)((long long)acap)));
                    low_fpf(out, "      lowv res_;\n"
                                 "      if (full_) res_ = lw_err({});\n"
                                 "      else {{ if (lw_amb_full())"
                                 " lw_panic(\"the task/message queue is exhausted — too many messages and tasks wait at once"
                                 " (docs/runtime-pools.md; on the freestanding floor raise it with -DLW_T0_MBOX=N)\");\n"
                                 "        {{ int qi_ = lw_amb_push(box_);"
                                 " lw_ambox[qi_].fn = lw_op_", PROVEN_ARG((long long)((long long)eidx)));
                    put_sym(out, h);
                    low_fpf(out, "; lw_ambox[qi_].nargs = {}; lw_ambox[qi_].rmax = {}; LW_SET_NOY(qi_, {});\n", PROVEN_ARG((size_t)(np - 1)), PROVEN_ARG((long long)((long long)cbe_restart_max(ir, h))),
                            PROVEN_ARG((int)(cbe_may_suspend(ir, IR_CALL_IDX(in->a), 0) ? 0 : 1)));
                    for (size_t g = 0; g + 1 < np; g++)
                        low_fpf(out, "        lw_ambox[qi_].args[{}] = lw_promote(st[sp-{}]);\n", PROVEN_ARG((size_t)(g)), PROVEN_ARG((size_t)(np - 1 - g)));
                    low_fpf(out, "      }} res_ = lw_wrap_ok(lw_int(0)); }}\n"
                                 "      sp -= {}; st[sp++] = res_; }}\n", PROVEN_ARG((size_t)(np)));
                    break;
                }
                // ★★★ **drain** — 이 인스턴스의 메일박스를 FIFO 로 비운다(도착 순서). [instance] 팝 → 단위 push.
                case IRW_DRAIN:
                    low_fpf(out, "    {{ if (st[sp-1].tag == LWV_INT && st[sp-1].i == 0) lw_panic(\"`drain` on actor handle 0 \xe2\x80\x94 this actor was never spawned\");"
                                 " if (st[sp-1].tag != LWV_REC) lw_panic(\"`drain` needs a spawned actor instance (a fabricated struct is not a real instance)\");"
                                 " lw_drain(st[sp-1].box);"
                                 " sp -= 1; st[sp++] = lw_int(0); }}\n");
                    break;
                // ★★★ **schedule** — 전역 메일박스를 quiescence 까지 FIFO 로 비운다(인자 없음). 단위 push.
                case IRW_SCHED:
                    low_fpf(out, "    {{ lw_schedule(); st[sp++] = lw_int(0); }}\n");
                    break;
                // ★★★ **yield** (green thread) — 네이티브도 코루틴 스케줄러 안이면 진짜 중단(lw_gyield).
                //   태스크 밖(스케줄러 밖)이면 no-op. 블로킹 채널이 이 위에 서므로 native 도 진짜 yield 한다.
                case IRW_YIELD:
                    low_fpf(out, "    {{ lw_gyield(); st[sp++] = lw_int(0); }}\n");
                    break;
                // ★★★ **channel** — bounded FIFO. 핸들(정수 인덱스)을 민다.
                case IRW_CHNEW:
                    low_fpf(out, "    st[sp++] = lw_int(lw_chnew());\n");
                    break;
                // ★★★ **chsend** [ch, v] — 채널에 넣는다(full 이면 lw_gyield 로 블록). 단위 push.
                case IRW_CHSEND:
                    low_fpf(out, "    {{ lowv v_ = st[sp-1]; long long ci_ = (long long)st[sp-2].i; sp -= 2; st[sp++] = lw_chsend(ci_, v_); }}\n");
                    break;
                // ★★★ **chrecv** [ch] — 채널에서 뺀다(empty 면 lw_gyield 로 블록). 값 push.
                case IRW_CHRECV:
                    low_fpf(out, "    {{ long long ci_ = (long long)st[sp-1].i; sp -= 1; st[sp++] = lw_chrecv(ci_); }}\n");
                    break;
                // ★★★ **task spawn** (task_group) — op 를 태스크 큐(box=-1)에 넣는다. [args…] 팝 → 단위 push.
                case IRW_TSPAWN: {
                    const low_ir_def_t *h = &ir->defs[in->a];
                    size_t np = h->nparams;
                    low_fpf(out, "    {{ if (lw_njobs >= (int)(sizeof lw_jobs / sizeof *lw_jobs)"
                                 " || lw_amb_full())"
                                 " lw_panic(\"the task/job pool is exhausted — it is never rewound"
                                 " (docs/runtime-pools.md; on the freestanding floor raise it with -DLW_T0_JOBS=N)\");\n");
                    low_fpf(out, "      int jid_ = lw_njobs++; lw_jobs[jid_].done = 0; lw_jobs[jid_].result = lw_int(0);"
                                 " int qi_ = lw_amb_push(-1); lw_ambox[qi_].job = jid_; lw_ambox[qi_].fn = lw_op_");
                    put_sym(out, h);
                    low_fpf(out, "; lw_ambox[qi_].nargs = {}; lw_ambox[qi_].peer = {};\n",
                                 PROVEN_ARG((size_t)(np)), PROVEN_ARG((int)(cbe_needs_peer(ir, (proven_size_t)in->a, 0) ? 1 : 0)));
                    for (size_t g = 0; g < np; g++)
                        low_fpf(out, "      lw_ambox[qi_].args[{}] = lw_promote(st[sp-{}]);\n", PROVEN_ARG((size_t)(g)), PROVEN_ARG((size_t)(np - g)));
                    low_fpf(out, "      sp -= {}; st[sp++] = lw_int(jid_ + 1); }}\n", PROVEN_ARG((size_t)(np)));
                    break;
                }
                // ★★★ **await** — job 핸들 팝 → 완료까지 기다렸다 결과 push.
                case IRW_AWAIT:
                    low_fpf(out, "    {{ long long jid_ = (long long)st[sp-1].i; sp -= 1; st[sp++] = lw_await(jid_); }}\n");
                    break;
                // ★★★ **cancel_on_error** 스코프 — 전역만 세운다(스택 무변화).
                case IRW_CANCELSCOPE:
                    low_fpf(out, "    lw_cancel_scope = {};\n", PROVEN_ARG((long long)((long long)(in->a != 0))));
                    break;
            }
        }
        low_puts(out, "    return lw_int(0);\n}\n");
        cbe_line_end(out);
        low_free(target);
        emitted++;
    }

    // dispatch table + main (CLI-compatible with `lowentc --run`)
    // ★ 경계 진입 검사(RFC-0055 D6): 파라미터의 선언 범위를 엔트리 표에 싣는다.
    //   내부 호출은 호출 지점에서 증명·검사된다. 그러나 **바깥에서 들어온 값**(main·FFI)에는
    //   아무 방벽이 없었고, 구간 분석은 그 범위를 사실로 믿고 검사를 제거해 뒀다 —
    //   그래서 네이티브가 조용히 틀린 값을 냈다(u8 op 이 -2 를 냈다). 경계가 마지막 문이다.
    low_puts(out, "struct lw_prng { int has; long long lo, hi; };\n");
    // ★ `pf32` — 어느 파라미터가 **32 비트 부동소수**인가(결함 노트 #83). 경계에서 반올림해야
    //   프로그램 안의 `f32` 와 밖에서 들어온 `f32` 가 같은 정밀도를 갖는다(VM 도 같은 자리에서 한다).
    low_puts(out, "struct lw_entry { const char *name; lowv (*fn)(const lowv *); int nparams; unsigned pslice;"
          " unsigned pflt; unsigned pf32; unsigned pu64; unsigned pstruct; int ounsigned; unsigned char psidx[LW_MAXP];"
          " struct lw_prng prng[LW_MAXP]; struct lw_prng ptype[LW_MAXP]; unsigned pcap; unsigned pw128, pw128s; };\n");
    // ★★★ R2 — **진짜 스레드.** `parallel s split` op 마다 `<name>_par` 를 낸다:
    //   슬라이스를 K 조각으로 나누고 **각 조각을 pthread 로** 돌린 뒤, `reduce` 로 합친다.
    //   ★ 청크는 **부분 슬라이스**다 — 같은 버퍼를 가리키므로 쓰기가 그대로 반영된다.
    //     LowentPar.v(Qed)가 **병렬 = 순차**라고 말했으니, 결과는 순차와 **비트 동일**해야 한다.
    //     그리고 게이트가 그것을 **요구한다** — 증명은 구현이 그것을 지킬 때만 값이 있다.
    for (proven_size_t i = 0; i < ir->ndefs; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        if (!cbe_emittable(d) || !d->is_par) continue;
        if (d->par_param >= d->nparams || !((d->param_slice >> d->par_param) & 1u)) continue;
        low_puts(out, "typedef struct { lowv (*fn)(const lowv *); lowv a[LW_MAXP]; int np; lowv r; } lw_ptask;\n"
              "static void *lw_pthunk(void *p) { lw_ptask *t = p; t->r = lw_promote(t->fn(t->a)); return 0; }\n"   /* ★ P4e — 되튐 창은 일꾼 스레드의 것이다: 끝나기 전에 풀로 */
              // ★★ 조각 수 K 는 **하드웨어에서 읽는다.** 전에는 **4 로 못 박혀 있었다** — 그리고
              //   그 숫자는 **한 번도 측정된 적이 없었다.** 정확성만 검증하고 속도는 주장만 했다.
              //   재 봤더니(16-스레드 기계, 원소당 400k 반복):
              //     K=1 → 1.00x · K=2 → 2.00x · K=4 → **3.90x** · K=8 → 7.51x · K=16 → 7.65x
              //   K=4 는 기계의 **절반 이상을 놀리고 있었다.**
              //   고를 수 있는 이유는 하나뿐이다: **분할 오라클이 답의 K-무관성을 이미 증명한다**
              //   (DET-1 — K=2,3,4,n 정방향·역방향이 비트 단위로 같다). 그래서 K 는 **성능 손잡이**다.
              "#ifndef LW_HAS_NCPU\n#define LW_HAS_NCPU 1\n"
              "static int lw_ncpu(void) {\n"
              "    long n = sysconf(_SC_NPROCESSORS_ONLN);\n"
              "    if (n < 1) n = 1; if (n > 64) n = 64;\n"
              "    return (int)n;\n"
              "}\n#endif\n");
        break;   // 타입은 한 번만
    }
    for (proven_size_t i = 0; i < ir->ndefs; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        if (!cbe_emittable(d) || !d->is_par) continue;
        if (d->par_param >= d->nparams || !((d->param_slice >> d->par_param) & 1u)) continue;
        low_puts(out, "static lowv lw_par_"); put_sym(out, d);
        low_puts(out, "(const lowv *a) {\n");
        low_fpf(out, "    int P = {}, K = lw_ncpu();\n", PROVEN_ARG((unsigned)((unsigned)d->par_param)));
        low_puts(out, "    lowv s = a[P];\n"
              "    if (s.tag != LWV_SLICE) lw_panic(\"parallel: the split parameter must be a slice\");\n"
              "    if ((size_t)K > s.n) K = (int)s.n; if (K < 1) K = 1;\n"
              "    pthread_t th[64]; lw_ptask ts[64];\n"
              "    for (int c = 0; c < K; c++) {\n"
              "        size_t lo = (s.n * (size_t)c) / (size_t)K, hi = (s.n * (size_t)(c + 1)) / (size_t)K;\n");
        low_puts(out, "        ts[c].fn = lw_op_"); put_sym(out, d);
        low_fpf(out, "; ts[c].np = {};\n", PROVEN_ARG((size_t)((size_t)d->nparams)));
        low_fpf(out, "        for (int q = 0; q < {}; q++) ts[c].a[q] = a[q];\n", PROVEN_ARG((size_t)((size_t)d->nparams)));
        low_puts(out, "        lowv ch = {0}; ch.tag = LWV_SLICE; ch.p = s.p + lo; ch.n = hi - lo;\n"
              "        ts[c].a[P] = ch;\n"
              "        if (pthread_create(&th[c], 0, lw_pthunk, &ts[c])) lw_panic(\"pthread_create\");\n"
              "    }\n"
              "    for (int c = 0; c < K; c++) pthread_join(th[c], 0);\n"
              "    lowv acc = ts[0].r;\n");
        if (d->red_op) {
            const char *op = d->red_op == 1 ? "+" : d->red_op == 2 ? "*" : NULL;
            if (op)
                low_fpf(out, "    for (int c = 1; c < K; c++) acc = lw_int(lw_want_int(acc, \"reduce\") {} "
                             "lw_want_int(ts[c].r, \"reduce\"));\n", proven_arg_cstr(op));
            else
                low_fpf(out, "    for (int c = 1; c < K; c++) {{ long long x = lw_want_int(acc, \"reduce\"), "
                             "y = lw_want_int(ts[c].r, \"reduce\"); acc = lw_int({}); }}\n",
                        proven_arg_cstr(d->red_op == 3 ? "x < y ? x : y" : "x > y ? x : y"));
        } else {
            low_puts(out, "    /* no reduce: the op runs for its WRITES (disjoint by Bernstein). "
                  "The return is the sum of the chunks' returns (each is its own length). */\n"
                  "    { long long t = 0; for (int c = 0; c < K; c++) t += lw_want_int(ts[c].r, \"par\"); "
                  "acc = lw_int(t); }\n");
        }
        low_puts(out, "    return acc;\n}\n");
    }

    low_puts(out, "static const struct lw_entry lw_ops[] = {\n");
    for (proven_size_t i = 0; i < ir->ndefs; i++) {
        if (!cbe_emittable(&ir->defs[i])) continue;
        low_puts(out, "    { \""); put_view(out, ir->defs[i].name);
        low_puts(out, "\", lw_op_"); put_sym(out, &ir->defs[i]);
        low_fpf(out, ", {}, {}u, {}u, {}u, {}u, {}u, {}, {{", PROVEN_ARG((size_t)((size_t)ir->defs[i].nparams)),
                PROVEN_ARG((unsigned)(ir->defs[i].param_slice)), PROVEN_ARG((unsigned)(ir->defs[i].param_flt)), PROVEN_ARG((unsigned)(ir->defs[i].param_f32)),
                PROVEN_ARG((unsigned)(ir->defs[i].param_u64)), PROVEN_ARG((unsigned)(ir->defs[i].param_struct)),
                PROVEN_ARG((int)(// ★ 출력이 **부호 없는** 정수인가 — 표시를 VM 과 같게 한다(결함 노트 #69).
                cbe_out_w128(&ir->defs[i]) ? (ir->defs[i].out_tyname.ptr[0] == (proven_byte_t)'i' ? 3 : 2)   // ★ RFC-0151 — 128 비트 결과
                : (ir->defs[i].out_tyname.size && ir->defs[i].out_tyname.ptr[0] == (proven_byte_t)'u') ? 1 : 0)));
        for (proven_size_t p = 0; p < LOW_MAX_PARAMS; p++)
            low_fpf(out, "{}{}", proven_arg_cstr(p ? "," : ""), PROVEN_ARG((unsigned)((unsigned)ir->defs[i].param_sidx[p])));
        low_puts(out, "}, {");
        for (proven_size_t p = 0; p < LOW_MAX_PARAMS; p++)
        {
            low_fpf(out, "{}{{{},", proven_arg_cstr(p ? "," : ""), PROVEN_ARG((int)(ir->defs[i].prng[p].has_rng ? 1 : 0)));
            put_i64(out, ir->defs[i].prng[p].rlo); low_puts(out, ",");
            put_i64(out, ir->defs[i].prng[p].rhi); low_puts(out, "}");
        }
        low_puts(out, "}, {");
        // ★ 타입 자체의 범위도 **계약이다** — 분석이 그것을 믿고 검사를 지운다.
        for (proven_size_t p = 0; p < LOW_MAX_PARAMS; p++)
        {
            low_fpf(out, "{}{{{},", proven_arg_cstr(p ? "," : ""), PROVEN_ARG((int)(ir->defs[i].ptype[p].has_rng ? 1 : 0)));
            put_i64(out, ir->defs[i].ptype[p].rlo); low_puts(out, ",");
            put_i64(out, ir->defs[i].ptype[p].rhi); low_puts(out, "}");
        }
        low_fpf(out, "}}, {}u, {}u, {}u }},\n", PROVEN_ARG((unsigned)(ir->defs[i].param_cap)), PROVEN_ARG((unsigned)(ir->defs[i].param_w128)), PROVEN_ARG((unsigned)(ir->defs[i].param_w128s)));
        // ★★★ R2 — `parallel` op 은 **스레드판도** 등재한다: `<name>_par`.
        //   같은 인자로 부르면 pthread 로 쪼개 돈다. 결과는 **순차와 비트 동일**해야 한다
        //   (LowentPar.v Qed). 게이트가 그것을 **요구한다** — 그래야 증명이 값을 한다.
        if (ir->defs[i].is_par && ir->defs[i].par_param < ir->defs[i].nparams &&
            ((ir->defs[i].param_slice >> ir->defs[i].par_param) & 1u)) {
            low_puts(out, "    { \""); put_view(out, ir->defs[i].name); low_puts(out, "_par");
            low_puts(out, "\", lw_par_"); put_sym(out, &ir->defs[i]);
            low_fpf(out, ", {}, {}u, {}u, {}u, {{", PROVEN_ARG((size_t)((size_t)ir->defs[i].nparams)),
                    PROVEN_ARG((unsigned)(ir->defs[i].param_slice)), PROVEN_ARG((unsigned)(ir->defs[i].param_flt)), PROVEN_ARG((unsigned)(ir->defs[i].param_struct)));
            for (proven_size_t p = 0; p < LOW_MAX_PARAMS; p++)
                low_fpf(out, "{}{}", proven_arg_cstr(p ? "," : ""), PROVEN_ARG((unsigned)((unsigned)ir->defs[i].param_sidx[p])));
            low_puts(out, "}, {");
            for (proven_size_t p = 0; p < LOW_MAX_PARAMS; p++) {
                low_fpf(out, "{}{{{},", proven_arg_cstr(p ? "," : ""), PROVEN_ARG((int)(ir->defs[i].prng[p].has_rng ? 1 : 0)));
                put_i64(out, ir->defs[i].prng[p].rlo); low_puts(out, ",");
                put_i64(out, ir->defs[i].prng[p].rhi); low_puts(out, "}");
            }
            low_puts(out, "}, {");
            for (proven_size_t p = 0; p < LOW_MAX_PARAMS; p++) {
                low_fpf(out, "{}{{{},", proven_arg_cstr(p ? "," : ""), PROVEN_ARG((int)(ir->defs[i].ptype[p].has_rng ? 1 : 0)));
                put_i64(out, ir->defs[i].ptype[p].rlo); low_puts(out, ",");
                put_i64(out, ir->defs[i].ptype[p].rhi); low_puts(out, "}");
            }
            low_puts(out, "} },\n");
        }
    }
    if (emitted == 0) low_puts(out, "    { \"\", 0, 0, 0u, 0u, 0u, {0}, {{0,0,0}}, {{0,0,0}} },\n");
    low_puts(out, "};\n");
    // ═══════════════════════════════════════════════════════════════════════
    // ★★★ **C 가 우리를 부른다** (RFC-0063 D3) — `export extern`.
    //   C 헤더에 나가는 이름 그대로의 함수를 낸다. 그리고 그것은 **하강된 몸을 직접** 부른다
    //   (태그 어댑터를 안 거친다) ⇒ **비용 0**. FFI 가 타입 하강 위에 서는 이유다.
    for (proven_size_t i = 0; i < ir->ndefs; i++) {
        const low_ir_def_t *d = &ir->defs[i];
        // ★ **헤더와 같은 기록을 읽는다**(RFC-0089 R1) — 조건을 여기서 다시 적지 않는다.
        const low_ir_plan_t *pl = low_cbe_plan_of(i);
        if (!pl->exported_symbol) continue;
        cbe_put_export_sig(out, d, pl, true);   // ★ RFC-0132 P4d — 서명은 한 곳에서
        low_puts(out, " {\n");
        // ★★★★★ **한 배열을 두 가지 수로 세고 있었다** (2026-08-25 · RFC-0100 · WO-0080).
        //   크기를 재는 고리는 능력을 `continue` 로 **건너뛰고**, 채우는 고리는 `bi++` 로
        //   **세고 있었다**. 능력이 없으면 두 수가 우연히 같아 아무 일도 안 나고, 능력이
        //   하나라도 있으면 **범위 밖에 쓴다** — ASan 이 `stack-buffer-overflow` 로 물었다.
        //   불린 쪽은 `ai[0]`(초기화 안 된 값)을 능력 슬롯으로 읽기까지 했다.
        //
        //   ☞ 어느 쪽이 옳았나: **인덱스가 옳다.** 불리는 몸(`lw_s_…`)은 능력 슬롯을 포함해
        //     `loc[j] = ai[j]` 로 읽는다. 그러므로 배열은 **능력을 포함한 슬롯 수**여야 한다.
        //
        //   ★ 그래서 증상만 맞추지 않는다(수를 손으로 맞추면 다음에 또 갈린다). **채우기를
        //     먼저 글로 만들고, 그때 센 수로 선언을 찍는다** — 두 수가 **같은 고리 한 번**에서
        //     나오므로 구조적으로 갈릴 수가 없다. (`--emit-h` 결함을 닫은 것과 같은 방식:
        //     판정을 한 기록으로 모은다.)
        //   ★★ 그리고 능력 슬롯을 **0 으로 채운다.** 안 채우면 불린 쪽이 쓰레기를 읽는다 —
        //     C 에 안 나가는 것과 초기화하지 않는 것은 다른 일이다.
        { char fill[LOW_MAX_PARAMS * 160]; proven_size_t fl = 0;
          proven_size_t ni = 0, ns = 0, nfl = 0, nst = 0;
          for (proven_size_t q = 0; q < d->nparams && fl + 160 < sizeof fill; q++) {
              int w;
              if ((d->param_struct >> q) & 1u)   // ★ RFC-0132 P4d — C 구조체를 그 바이트 그대로 칸에(배치가 같다 — 아래에서 C 컴파일러가 잰다)
                  w = low_bfmt(fill + fl, sizeof fill - fl,
                               "    _Static_assert(sizeof(struct lw_sty_{}) == {}, \"lw_sty layout\"); memcpy(arb[{}], &s{}, sizeof s{}); ar[{}] = arb[{}];\n",
                               PROVEN_ARG((unsigned)((unsigned)d->param_sidx[q])), PROVEN_ARG((unsigned)((unsigned)ir->structs[d->param_sidx[q]].total)), PROVEN_ARG((size_t)((size_t)nst)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)nst)), PROVEN_ARG((size_t)((size_t)nst))), nst++;
              else if ((d->param_cap >> q) & 1u)
                  w = low_bfmt(fill + fl, sizeof fill - fl,
                               "    ai[{}] = 0;   /* cap: C 에 안 나가지만 슬롯은 있다 */\n",
                               PROVEN_ARG((size_t)((size_t)ni))), ni++;
              else if ((d->param_slice >> q) & 1u)
                  w = low_bfmt(fill + fl, sizeof fill - fl,
                               "    as[{}].p = p{}; as[{}].n = n{};\n",
                               PROVEN_ARG((size_t)((size_t)ns)), PROVEN_ARG((size_t)((size_t)q)), PROVEN_ARG((size_t)((size_t)ns)), PROVEN_ARG((size_t)((size_t)q))), ns++;
              else if ((d->param_flt >> q) & 1u)
                  w = low_bfmt(fill + fl, sizeof fill - fl,
                               "    af[{}] = x{};\n", PROVEN_ARG((size_t)((size_t)nfl)), PROVEN_ARG((size_t)((size_t)q))), nfl++;
              else
                  w = low_bfmt(fill + fl, sizeof fill - fl,
                               "    ai[{}] = x{};\n", PROVEN_ARG((size_t)((size_t)ni)), PROVEN_ARG((size_t)((size_t)q))), ni++;
              if (w > 0) fl += (proven_size_t)w;
          }
          low_fpf(out, "    long long ai[{}]; lw_sl as[{}]; double af[{}];"
                       " _Alignas(16) unsigned char arb[{}][64] = {{{{0}}}}; unsigned char *ar[{}] = {{ arb[0] }}; long long ab[1] = {{0}}; lw_r ao[1]; lw_v av[1];\n"
                       "    lw_sl rsl[LW_RECF] = {{{{0,0}}}};\n    (void)ai; (void)as; (void)af; (void)ar; (void)ab; (void)ao; (void)av; (void)rsl;\n",
                  PROVEN_ARG((size_t)((size_t)(ni ? ni : 1))), PROVEN_ARG((size_t)((size_t)(ns ? ns : 1))), PROVEN_ARG((size_t)((size_t)(nfl ? nfl : 1))), PROVEN_ARG((size_t)((size_t)(nst ? nst : 1))), PROVEN_ARG((size_t)((size_t)(nst ? nst : 1))));
          low_write(out, fill, fl); }
        // ★★ W-20261104-1345 — 몸에서 지운 겹침 검사(단위 안의 모든 부름 자리가 보증했다)를 **C 입구에서** 견준다.
        //   C 가 부르는 자리는 그 보증 밖이다. 몸에 남은 짝은 몸이 견주므로 여기서 다시 하지 않는다(같은 뜻 · 같은 문장).
        for (proven_size_t q = 0; q < d->nxpairs; q++) {
            const low_ir_xpair_t *x = &d->xpairs[q];
            if (!x->skipped) continue;
            proven_size_t ia = 0, ib = 0, k_ = 0;
            for (proven_size_t r = 0; r < d->nparams; r++) {
                const bool sl_ = !((d->param_struct >> r) & 1u) && !((d->param_cap >> r) & 1u) && ((d->param_slice >> r) & 1u);
                if (r == x->i) ia = k_;
                if (r == x->j) ib = k_;
                if (sl_) k_++;
            }
            low_fpf(out, "    lw_xchk_b((uintptr_t)as[{}].p, as[{}].n * {}u, (uintptr_t)as[{}].p, as[{}].n * {}u, {});   /* W-20261104-1345 */\n",
                    PROVEN_ARG((size_t)((size_t)ia)), PROVEN_ARG((size_t)((size_t)ia)), PROVEN_ARG((unsigned)((unsigned)d->param_esz[x->i])), PROVEN_ARG((size_t)((size_t)ib)), PROVEN_ARG((size_t)((size_t)ib)), PROVEN_ARG((unsigned)((unsigned)d->param_esz[x->j])), PROVEN_ARG((int)((int)x->mode)));
        }
        {
            unsigned am_ = i < 512 ? g_argmask[i] : LWA_ALL;
            low_puts(out, cbe_ret_scalar(i) ? "    return lw_s_" : "    lw_r r_ = lw_s_");
            put_sym(out, d); low_puts(out, "("); cbe_put_args(out, am_, false, cbe_depparam(i) ? "0" : NULL); low_puts(out, ");\n");
            if (pl->ret_sty1)   // ★ RFC-0132 P4d — 구조체는 값으로 돌려준다: 레코드 바이트를 C 구조체로(sret 은 C 컴파일러가 고른다)
                low_fpf(out, "    _Static_assert(sizeof(struct lw_sty_{}) == {}, \"lw_sty layout\");\n"
                             "    struct lw_sty_{} o_; memcpy(&o_, r_.r, sizeof o_); return o_;\n",
                        PROVEN_ARG((int)(pl->ret_sty1 - 1)), PROVEN_ARG((unsigned)((unsigned)ir->structs[pl->ret_sty1 - 1].total)), PROVEN_ARG((int)(pl->ret_sty1 - 1)));
            else if (!cbe_ret_scalar(i))
                low_puts(out, g_retk[i] == K_FLT ? "    return r_.f;\n" : "    return r_.v;\n");
            low_puts(out, "}\n");
        }
    }

    // ★★★ **엔트리 포인트** (RFC-0030) — `main` op 이 있으면 **진짜 실행 파일**을 낸다.
    //
    //   지금까지 C 백엔드가 낸 `main` 은 **테스트 디스패처**였다(`./bin <op> args…`).
    //   즉 이 언어로는 **라이브러리만** 만들 수 있었고 **프로그램은 만들 수 없었다.**
    //   RFC-0030 은 **채택**이었고 **구현이 0** 이었다.
    //
    //   D1: 엔트리 = **지정된 보통 op**(관례 이름 `main`). 보일러플레이트 0.
    //   D3: **출력이 곧 종료 상태**다 — 수동 `exit()` 불요.
    {
        const low_ir_def_t *entry = NULL;
        for (proven_size_t i = 0; i < ir->ndefs; i++)
            if (ir->defs[i].lowered && !ir->defs[i].is_test &&
                ir->defs[i].name.size == 4 &&
                memcmp(ir->defs[i].name.ptr, "main", 4) == 0) entry = &ir->defs[i];
        // ★★★ **프로그램이 되는 순간 op 디스패처를 잃었다 — 그리고 아무도 몰랐다.**
        //   차등 스윕이 **죽어 있었기 때문이다**: 심볼에 해시가 붙자(RFC-0062 §5) 스윕의
        //   정규식이 op 를 **하나도 못 찾고**, **0개를 재면서 "ok"** 를 찍었다.
        //   ⇒ 인자가 있으면 **디스패처**, 없으면 **프로그램**. 둘 다 된다.
        // ★★★★ **벡터 테이블 — 하드웨어가 읽는 유일한 목차** (RFC-0042 §8-4, 2026-08-01).
        //
        //   `vector N .` 은 지금까지 **검사만** 됐다(E-ISR-*). 그런데 검사만으로는 부팅하지
        //   않는다: Cortex-M 은 리셋 시 주소 0 에서 ① 초기 스택 포인터 ② Reset 핸들러 주소를
        //   읽고, 그 뒤 슬롯에서 각 인터럽트의 핸들러 주소를 읽는다. 그 표가 없으면 ISR 은
        //   **선언됐지만 아무도 못 부르는 함수**다 — 절이 장식이 된다.
        //
        //   ⇒ 도구가 표를 낸다. 낼 수 있는 이유는 **도구가 이미 벡터 번호를 알기 때문**이다
        //     (사람이 C 로 다시 적으면 그 번호가 두 곳에 살고, 두 곳은 갈린다 — 교훈 7).
        //
        //   ☞ **링커 스크립트는 내지 않는다.** 메모리 크기·플래시 시작 주소는 **보드의 것**이지
        //     프로그램의 것이 아니다. 프로그램이 아는 것은 *"이 인터럽트를 내가 받는다"* 뿐이고,
        //     도구는 아는 것만 말한다. 예시 스크립트는 `impl/tests/prog/cortex_m.ld` 에 있다.
        if (freestanding) {
            proven_size_t nisr = 0, maxv = 0;
            for (proven_size_t i = 0; i < ir->ndefs; i++)
                if (ir->defs[i].isr_vector >= 0 && ir->defs[i].lowered) {
                    nisr++;
                    if ((proven_size_t)ir->defs[i].isr_vector > maxv) maxv = (proven_size_t)ir->defs[i].isr_vector;
                }
            // ★★ **슬롯 1 을 프로그램이 주장했는가**(RFC-0042 D5-c). 주장했다면 `_lw_reset` 은
            //   **선언조차 하지 않는다** — 아무도 정의하지 않을 이름을 선언하는 것이 바로
            //   `--emit-h` 결함의 부류다(docs/known-defects/emit-h-declares-undefined-symbol.md).
            const low_ir_def_t *rst = NULL;
            for (proven_size_t i = 0; i < ir->ndefs; i++)
                if (ir->defs[i].isr_vector == 1 && ir->defs[i].lowered) rst = &ir->defs[i];
            if (nisr) {
                low_puts(out, "\n/* ★ RFC-0042 §8-4 — ISR vector table (freestanding). The hardware reads\n"
                      "   this and nothing else: slot 0 = initial SP, slot 1 = reset, then one slot\n"
                      "   per interrupt. A declared `vector N .` with no table is a handler nobody\n"
                      "   can call. The board's startup supplies _lw_stack_top (memory layout is the\n"
                      "   BOARD's, not the program's). */\n"
                      "extern void _lw_stack_top(void);\n");
                if (!rst)
                    low_puts(out, "/* ★ No op claims `vector 1 .`, so the reset entry is the board's too. */\n"
                          "void _lw_reset(void);\n");
                low_puts(out, "static void lw_isr_default(void) { for (;;) { } }\n");
                for (proven_size_t i = 0; i < ir->ndefs; i++) {
                    const low_ir_def_t *d = &ir->defs[i];
                    if (d->isr_vector < 0 || !d->lowered) continue;
                    low_fpf(out, "/* vector {}", PROVEN_ARG((int)((int)d->isr_vector)));
                    if (d->isr_priority >= 0) low_fpf(out, " · priority {}", PROVEN_ARG((int)((int)d->isr_priority)));
                    // ★★★ **트램폴린은 진짜 시그니처로 불러야 한다** (2026-08-02 고침).
                    //   이 자리는 `lw_s_<op>()` 를 **인자 없이** 불렀다. 그런데 빠른 경로의
                    //   스칼라 진입은 `(const long long *restrict ai, …)` 를 받는다(그 목록은
                    //   `g_argmask` 가 정한다) ⇒ **방출된 C 가 컴파일되지 않았다.** 벡터 표는
                    //   `1ec7a79` 에 실렸지만 골든이 `.isr_vector` **문자열만** 봤으므로 아무도
                    //   그것을 컴파일해 보지 않았다 — 표는 있는데 **번역이 안 되는 표**였다.
                    //   ⇒ 대표 프로그램이 실제로 `cc` 를 지나가면서 드러났다(RFC-0088 §4.2).
                    //   ★ 그리고 빠른 경로로 안 내려간 ISR 은 `lw_s_` 가 **아예 없다** —
                    //     그때는 언제나 존재하는 태그 어댑터를 부른다. ISR 은 인자가 없으므로
                    //     어댑터가 `a` 를 만지지 않는다.
                    low_puts(out, " */ void lw_isr_"); put_sym(out, (low_ir_def_t *)d);
                    low_puts(out, "(void) {\n");
                    if (i < 512 && sc_tab[i]) {
                        unsigned am_ = g_argmask[i];
                        low_puts(out, "    long long ai[1] = {0}; lw_sl as[1] = {{0,0}}; double af[1] = {0};\n"
                              "    _Alignas(8) unsigned char arb[1][64] = {{0}}; unsigned char *ar[1] = { arb[0] }; long long ab[1] = {0}; lw_r ao[1] = {0};\n"
                              "    lw_v av[1] = {{{0}}};\n"
                              "    lw_sl rsl[LW_RECF] = {{0,0}};\n"   /* RFC-0106 */
                              "    (void)ai; (void)as; (void)af; (void)ar; (void)ab; (void)ao; (void)av; (void)rsl;\n");
                        low_puts(out, "    (void)lw_s_"); put_sym(out, (low_ir_def_t *)d);
                        low_puts(out, "("); cbe_put_args(out, am_, false, cbe_depparam(i) ? "0" : NULL); low_puts(out, ");\n");
                    } else {
                        low_puts(out, "    (void)lw_op_"); put_sym(out, (low_ir_def_t *)d);
                        low_puts(out, "((const lowv *)0);\n");
                    }
                    low_puts(out, "}\n");
                }
                // ★★★★ **`vector 1 .` = 리셋 진입** (RFC-0042 D5-c, 2026-08-02 · 새 낱말 0).
                //
                //   Cortex-M 의 **슬롯 1 이 곧 리셋 벡터**다. 그러니 그것을 주장하는 op 이
                //   리셋 진입이고, **D5-b 의 논증이 그대로 선다**: 하드웨어가 불렀다는 사실이
                //   그 권한이다 — `cap mmio`·`cap machine` 을 파라미터로 받지 않는다.
                //
                //   ☞ 왜 필요했나(실측 2026-08-02, arch-dev): blink 의 `start` 는 cap 을 받는데
                //     **능력은 C 경계를 못 넘는다**(래퍼가 param_cap 을 건너뛴다). 그래서 실기
                //     오브젝트에 ISR·벡터표·NVIC enable 만 남고 **초기화 경로를 아무도 못 불렀다.**
                //     ISR 은 자기 존재 이유에 닿았는데 **리셋 진입이 못 닿고 있었다.**
                //   ★ 권한은 여전히 샐 수 없다 — 문이 정확히 하나 폭이다: 프로그램이 부를 수
                //     없고(E-ISR-CALLED) 인자가 없어 남에게 건넬 것도 없다(E-ISR-PARAMS).
                //   ★★ 아무도 슬롯 1 을 주장하지 않으면 **보드가 준다**(`_lw_reset`, 지금까지처럼).
                //      도구는 **아는 것만** 말한다.
                low_fpf(out, "__attribute__((used, section(\".isr_vector\")))\n"
                             "void (*const lw_vectors[{}])(void) = {{\n"
                             "    _lw_stack_top,\n", PROVEN_ARG((size_t)((size_t)(maxv + 2))));
                if (rst) { low_puts(out, "    lw_isr_"); put_sym(out, (low_ir_def_t *)rst); low_puts(out, ",  /* vector 1 — reset: the program claims it */\n"); }
                else       low_puts(out, "    _lw_reset,  /* vector 1 — reset: no op claims it, the BOARD supplies it */\n");
                for (proven_size_t v = 2; v <= maxv; v++) {
                    const low_ir_def_t *h = NULL;
                    for (proven_size_t i = 0; i < ir->ndefs; i++)
                        if (ir->defs[i].isr_vector == (proven_i32)v && ir->defs[i].lowered) h = &ir->defs[i];
                    if (h) { low_puts(out, "    lw_isr_"); put_sym(out, (low_ir_def_t *)h); low_puts(out, ",\n"); }
                    else     low_puts(out, "    lw_isr_default,\n");
                }
                low_puts(out, "};\n");

                // ★★★★ **NVIC 활성화 — 번호는 한 곳에만 산다** (RFC-0042 §8-4).
                //
                //   `vector N .` 은 이미 도구의 것이다. 그런데 그 인터럽트가 **실제로 오게 하려면**
                //   NVIC 의 비트를 켜야 하고, 그 비트 번호는 **같은 사실의 두 번째 표현**이다:
                //   외부 IRQ 번호 = 벡터 번호 − 16. 사람이 그것을 손으로 적으면 **두 곳에 살고,
                //   두 곳은 갈린다**(교훈 7 — 벡터 표를 도구가 내는 이유와 **정확히 같은 논증**).
                //
                //   ☞ 실측(2026-08-02): `vector 22`(EXTI0=IRQ6) 를 `vector 23` 으로 바꾸고 프로그램의
                //     `shl 1 6` 을 그대로 두면 **모든 게이트가 초록이고** 벡터 표도 옳은데, 보드에서
                //     그 인터럽트는 **영원히 안 온다.** 진단 하나 없이.
                //
                //   ⇒ 도구가 낸다. **언제 켜는가는 보드의 것**이므로(링커 스크립트를 안 내는 것과
                //     같은 규율) 리셋에 끼워 넣지 않고 **부를 수 있는 함수**로 낸다.
                //   ★ ISER 은 **1 을 쓰면 켜지고 0 은 무시된다** — 그래서 read-modify-write 가
                //     필요 없고, 이 쓰기가 남의 IRQ 를 건드리지 않는다(RCC 와 다른 점이다).
                //   ★ 코어 예외(벡터 < 16: SysTick·PendSV·fault…)는 NVIC 를 안 거친다 — 그래서
                //     여기에 안 나온다. 켤 것이 없으면 **빈 함수**를 낸다(없는 것도 말한다).
                low_puts(out, "\n/* ★ RFC-0042 §8-4 — NVIC enable for every DECLARED vector >= 16.\n"
                      "   external IRQ = vector - 16. The TOOL emits this because the tool already\n"
                      "   knows the vector numbers; an IRQ bit written by hand lives in two places,\n"
                      "   and two places drift. WHEN to enable is the board's call, so this is a\n"
                      "   function your startup calls — it is not injected into reset.\n"
                      "   ISER ignores zero bits, so this never disturbs another interrupt. */\n"
                      "void lw_nvic_enable_declared(void) {\n");
                {
                    bool any = false;
                    for (proven_size_t w = 0; w < 8; w++) {   // ISER0..7 = IRQ 0..255
                        proven_u32 bits = 0;
                        for (proven_size_t i = 0; i < ir->ndefs; i++) {
                            proven_i32 vn = ir->defs[i].isr_vector;
                            if (vn < 16 || !ir->defs[i].lowered) continue;
                            proven_u32 irq = (proven_u32)(vn - 16);
                            if (irq / 32u == (proven_u32)w) bits |= 1u << (irq % 32u);
                        }
                        if (!bits) continue;
                        any = true;
                        low_fpf(out, "    *(volatile uint32_t *)LW_MMIO_PTR(0x{:08X}UL) = 0x{:08X}UL;"   /* RFC-0137 P4 */
                                     "  /* ISER{} */\n",
                                PROVEN_ARG((unsigned long long)((unsigned long)(0xE000E100UL + 4UL * (unsigned long)w))),
                                PROVEN_ARG((unsigned long long)((unsigned long)bits)), PROVEN_ARG((size_t)((size_t)w)));
                    }
                    if (!any)
                        low_puts(out, "    /* nothing to enable: every declared vector is a CORE exception\n"
                              "       (< 16 — SysTick, PendSV, faults), which does not go through NVIC. */\n");
                }
                low_puts(out, "}\n");
            }
        }
        if (g_no_main) { /* ★ 라이브러리 — main 도 디스패처도 안 낸다 */ }
        // ★★★★ **프리스탠딩에는 CLI 가 없다** (RFC-0075 Stage 5 · 2026-08-15).
        //   디스패처는 `argc/argv` 를 읽고 `fprintf(stderr, …)` 로 쓰임새를 찍는다 — 베어메탈엔
        //   **셋 다 없다**. 그래서 이것을 무조건 내던 동안 `--target cortex_m` 방출은 **컴파일조차
        //   되지 않았다**(순수 프로그램 여섯 줄로 재현). 호스트 런타임 블록들은 이미 `LW_HOSTED`
        //   로 제대로 갇혀 있었고 — 전처리해 보면 하나도 안 남는다 — 새던 것은 **이 하나**였다.
        //   ☞ 프리스탠딩 방출이 내는 것은 **op 함수들**이다. 펌웨어가 그것을 부른다:
        //     진입점은 링커 스크립트와 벡터 테이블의 몫이지 이 방출기의 몫이 아니다.
        else if (low_ir_target()->no_heap) {
            low_puts(out, "/* ★ freestanding: no CLI dispatcher and no `main` — this target has no argc,\n"
                  "   no stdout and no exit status. The ops above are the surface; the firmware\n"
                  "   entry point belongs to the linker script, not to this emitter. */\n");
        }
        else {
        low_puts(out, LW_DISPATCH);
        if (entry && entry->param_cap) {
            // ★ D2′ — cap 을 받는 엔트리: **argv 는 프로그램의 것**이다. 디스패처(테스트 비계)를
            //   내지 않는다 — per-op 네이티브 검증이 필요한 픽스처는 엔트리를 cap 없이 둔다.
            low_puts(out, "/* ★ RFC-0030 D2\u2032 — entry with capabilities: argv belongs to the PROGRAM. */\n"
                  "int main(int argc, char **argv) {\n"
                  "    lw_g_nargs = argc - 1; lw_g_args = argv + 1;\n"
                  "    lowv a[LW_MAXP]; memset(a, 0, sizeof a);   /* cap 토큰 — 권한은 정적으로 갈렸다 */\n"
                  "    lowv r = lw_op_");
            put_sym(out, entry);
            low_puts(out, "(a);\n"
                  "    if (r.tag == LWV_ERR) {\n"
                  "        long long e = r.i; int c = (int)((e + 1) & 0xff); if (c == 0) c = 255;\n"
                  "        fprintf(stderr, \"exit: error variant %lld\\n\", e);\n"
                  "        return c;\n"
                  "    }\n"
                  "    if (r.tag == LWV_OK) return 0;\n"
                  "    return (int)(r.i & 0xff);\n"
                  "}\n");
        } else if (entry) {
            low_puts(out, "/* ★ RFC-0030 — the entry point. The op's OUTPUT is the EXIT STATUS. */\n"
                  "int main(int argc, char **argv) {\n"
                  "    if (argc >= 2) return lw_dispatch(argc, argv);\n"
                  "    lowv r = lw_op_");
            put_sym(out, entry);
            low_puts(out, "(0);\n"
                  /* ★ result<T>E 종료 매핑: ok→0 · error→변형 code(1..255, nonzero)+stderr. 태그로 구분한다 —
                     void/u8/i32/u32 는 LWV_INT 로 와서 그 값이 곧 코드(종전 동작 그대로). */
                  "    if (r.tag == LWV_ERR) {\n"
                  "        long long e = r.i; int c = (int)((e + 1) & 0xff); if (c == 0) c = 255;\n"
                  "        fprintf(stderr, \"exit: error variant %lld\\n\", e);\n"
                  "        return c;\n"
                  "    }\n"
                  "    if (r.tag == LWV_OK) return 0;\n"
                  "    return (int)(r.i & 0xff);\n"
                  "}\n");
        } else {
            low_puts(out, "int main(int argc, char **argv) { return lw_dispatch(argc, argv); }\n");
        }
        }
    }
    return emitted;
}
