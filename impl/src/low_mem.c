/* ★★ 처리기의 힙 — low_mem.h 의 ★. main.c 에 있던 `lw_heap` 를 옮겼다(파일마다 같은 할당기를 쓰게). */
#include "proven/heap.h"
#include "proven/alloc_check.h"
#include "low_mem.h"
#ifdef PROVEN_ALLOC_CHECK
#define LW_AC_CAP (1u << 21)
static proven_alloc_check_entry_t g_ac_rec[LW_AC_CAP];
#else
#define LW_AC_CAP 0u
static proven_alloc_check_entry_t *g_ac_rec;
#endif
static proven_alloc_check_t g_ac;
proven_allocator_t low_heap(void) {
    static bool made; static proven_allocator_t a;
    if (!made) { a = proven_alloc_checked(&g_ac, proven_heap_allocator(), g_ac_rec, LW_AC_CAP); made = true; }
    return a;
}
