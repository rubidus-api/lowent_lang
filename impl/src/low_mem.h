#ifndef LOW_MEM_H
#define LOW_MEM_H
/* ★★ 처리기의 메모리 — 모두 proven_c_lib 의 할당기로 받는다 (2026-11-12 · W-20261112-0207 · 소유자 «메모리 할당에 proven c lib 를 적극 사용»).
 *
 *   쓰임마다 고른다:
 *     · low_heap()      — 오래 살거나 크기가 늘어나는 것(VM 실행 구조 · 자라는 버퍼). proven 의 힙(`proven_heap_allocator`)이고,
 *                         `make alloccheck` 빌드(-DPROVEN_ALLOC_CHECK)에서는 블록을 아는 할당기로 감싸 남의 블록 해제 · 두 번 해제 ·
 *                         크기가 틀린 realloc 을 그 자리에서 멈춘다. 보통 빌드에서는 비용 0.
 *     · low_scratch_*   — 한 패스(함수 하나) 안에서만 쓰는 작업 배열. 필요한 바이트를 한 덩이로 받아 proven 아레나로 나눠 주고,
 *                         패스 끝에서 한 번에 돌려준다(배열마다 free 하지 않는다 — 짝은 open/close 다).
 *     · proven_pool     — 크기가 같은 블록을 자주 받고 돌려주는 것(그린스레드 스택).
 *   짝: low_alloc · low_zalloc · low_realloc ↔ low_free (모두 low_heap 에서) · low_scratch_open ↔ low_scratch_close.
 */
#include <stdbool.h>
#include <stdalign.h>
#include "proven/allocator.h"
#include "proven/arena.h"
#include "proven/types.h"

proven_allocator_t low_heap(void);

#define LOW_ALIGN ((proven_size_t)alignof(max_align_t))

static inline void *low_alloc(proven_size_t n) {
    proven_allocator_t h = low_heap();
    proven_result_mem_mut_t r = h.alloc_fn(h.ctx, n ? n : 1, LOW_ALIGN);
    return proven_is_ok(r.err) ? (void *)r.value.ptr : (void *)0;
}
/* 정렬을 지정해 받는다(`align n` 계약의 바이트 · 2 의 거듭제곱). 짝은 low_free. */
static inline void *low_alloc_aligned(proven_size_t n, proven_size_t al) {
    proven_allocator_t h = low_heap();
    proven_result_mem_mut_t r = h.alloc_fn(h.ctx, n ? n : 1, al < LOW_ALIGN ? LOW_ALIGN : al);
    return proven_is_ok(r.err) ? (void *)r.value.ptr : (void *)0;
}
static inline void *low_zalloc(proven_size_t n, proven_size_t sz) {
    if (sz && n > (proven_size_t)-1 / sz) return (void *)0;
    proven_size_t b = n * sz;
    unsigned char *p = (unsigned char *)low_alloc(b);
    if (p) for (proven_size_t i = 0; i < (b ? b : 1); i++) p[i] = 0;
    return p;
}
/* `old` 는 지금 블록의 바이트 수다(proven 의 realloc 은 그것을 받는다 — alloccheck 빌드가 맞는지 본다). 실패하면 옛 블록은 그대로다. */
static inline void *low_realloc(void *p, proven_size_t old, proven_size_t n) {
    if (!p) return low_alloc(n);
    proven_allocator_t h = low_heap();
    proven_result_mem_mut_t r = h.realloc_fn(h.ctx, p, old, n ? n : 1, LOW_ALIGN);
    return proven_is_ok(r.err) ? (void *)r.value.ptr : (void *)0;
}
static inline void low_free(void *p) {
    if (!p) return;
    proven_allocator_t h = low_heap();
    h.free_fn(h.ctx, p);
}

/* 한 패스의 작업 배열. open 에 그 패스가 받을 바이트의 합(배열마다 정렬 몫을 더해)을 준다. */
typedef struct { void *back; proven_arena_t a; } low_scratch_t;
static inline proven_size_t low_scratch_need(proven_size_t n, proven_size_t sz) {   /* 배열 하나의 몫(정렬 여유 포함) */
    return (sz && n > ((proven_size_t)-1 - LOW_ALIGN) / sz) ? (proven_size_t)-1 : (n ? n : 1) * (sz ? sz : 1) + LOW_ALIGN;
}
static inline bool low_scratch_open(low_scratch_t *s, proven_size_t bytes) {
    s->back = low_alloc(bytes);
    if (!s->back) { s->a = proven_arena_create((proven_mem_mut_t){ 0 }); return false; }
    s->a = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t *)s->back, .size = bytes });
    return true;
}
static inline void *low_scratch_zalloc(low_scratch_t *s, proven_size_t n, proven_size_t sz) {
    if (!s->back) return (void *)0;
    proven_size_t b = (n ? n : 1) * (sz ? sz : 1);
    proven_result_mem_mut_t r = proven_arena_alloc_aligned(&s->a, b, LOW_ALIGN);
    if (!proven_is_ok(r.err)) return (void *)0;
    for (proven_size_t i = 0; i < b; i++) r.value.ptr[i] = 0;
    return r.value.ptr;
}
static inline void low_scratch_close(low_scratch_t *s) {
    proven_arena_destroy(&s->a);
    low_free(s->back); s->back = (void *)0;
}
#endif
