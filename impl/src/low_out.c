/* ★★ low_out.h 의 ★ — 처리기가 내보내는 바이트의 통로를 proven 의 쓰기 도구로. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "low_out.h"
#include "low_mem.h"
#include "proven/fs.h"
#include "proven/stream.h"
#include "proven/sysio.h"

enum { LOW_OUT_STD = 1, LOW_OUT_FILE, LOW_OUT_MEM, LOW_OUT_NULL };
#define LOW_OUT_BUF 4096   /* stdio 가 관(pipe)과 파일에 쓰는 크기와 같다 */

struct low_out {
    int kind;
    bool line;                      /* 터미널: 새 줄을 쓰면 곧 내보낸다 */
    proven_writer_t w;
    proven_sysio_out_t sio;         /* 표준 출력의 버퍼 쓰기 도구 */
    proven_sysio_std_t std;         /* 표준 오류의 곧바로 쓰기 도구 */
    proven_file_t file;             /* 파일 */
    proven_writer_buffered_t bw;
    proven_writer_u8str_t ms;       /* 메모리 */
    proven_u8str_t mem;
    proven_size_t count;
    proven_err_t err;
    low_out_t *next_open;           /* 열린 파일 통로 목록(끝날 때 비운다 — stdio 처럼) */
    proven_byte_t buf[LOW_OUT_BUF];
};

static low_out_t *g_open_files;

static low_out_t g_std[2];
static bool g_std_ready[2];

static void low_out_at_exit(void) {
    for (low_out_t *f = g_open_files; f; f = f->next_open) (void)low_flush(f);   /* 닫지 않은 파일도 stdio 처럼 끝에 비운다 */
    low_flush_std();
}
static bool g_exit_hooked;
static void hook_exit(void) { if (!g_exit_hooked) { g_exit_hooked = true; atexit(low_out_at_exit); } }

static low_out_t *std_get(int which) {
    low_out_t *o = &g_std[which];
    if (!g_std_ready[which]) {
        g_std_ready[which] = true;
        memset(o, 0, sizeof *o);
        o->kind = LOW_OUT_STD;
        if (which == 0) {
            o->w = proven_sysio_stdout_buffered(&o->sio, (proven_mem_mut_t){ .ptr = o->buf, .size = sizeof o->buf });
            o->line = isatty(1) == 1;
            hook_exit();
        } else {
            o->w = proven_sysio_stderr_writer(&o->std);
        }
    }
    return o;
}
low_out_t *low_stdout(void) { return std_get(0); }
low_out_t *low_stderr(void) { return std_get(1); }

low_out_t *low_out_open(const char *path, const char *mode) {
    if (!path || !mode) return NULL;
    proven_fs_mode_t m;
    if (mode[0] == 'w') m = (proven_fs_mode_t)(PROVEN_FS_WRITE | PROVEN_FS_CREATE | PROVEN_FS_TRUNC);
    else if (mode[0] == 'a') m = (proven_fs_mode_t)(PROVEN_FS_WRITE | PROVEN_FS_CREATE | PROVEN_FS_APPEND);
    else return NULL;
    proven_result_file_t f = proven_fs_open(low_heap(), (proven_u8str_view_t){ (const proven_u8 *)path, (proven_size_t)strlen(path) }, m);
    if (!proven_is_ok(f.err)) return NULL;
    low_out_t *o = (low_out_t *)low_zalloc(1, sizeof *o);
    if (!o) { (void)proven_fs_close(f.value); return NULL; }
    o->kind = LOW_OUT_FILE;
    o->file = f.value;
    o->w = proven_writer_buffered(&o->bw, proven_writer_from_file(&o->file), (proven_mem_mut_t){ .ptr = o->buf, .size = sizeof o->buf });
    o->next_open = g_open_files; g_open_files = o; hook_exit();
    return o;
}

low_out_t *low_out_mem(void) {
    low_out_t *o = (low_out_t *)low_zalloc(1, sizeof *o);
    if (!o) return NULL;
    proven_result_u8str_t s = proven_u8str_create(low_heap(), 4096);
    if (!proven_is_ok(s.err)) { low_free(o); return NULL; }
    o->kind = LOW_OUT_MEM;
    o->mem = s.value;
    o->w = proven_writer_from_u8str(&o->ms, &o->mem, low_heap());
    return o;
}

low_out_t *low_out_null(void) {
    low_out_t *o = (low_out_t *)low_zalloc(1, sizeof *o);
    if (o) o->kind = LOW_OUT_NULL;
    return o;
}

proven_u8str_view_t low_out_mem_view(low_out_t *o) {
    if (!o || o->kind != LOW_OUT_MEM) return (proven_u8str_view_t){ (const proven_u8 *)"", 0 };
    return proven_u8str_as_view(&o->mem);
}
proven_size_t low_out_count(const low_out_t *o) { return o ? o->count : 0; }
bool low_out_failed(const low_out_t *o) { return !o || !proven_is_ok(o->err); }

int low_flush(low_out_t *o) {
    if (!o) return -1;
    if (o->kind == LOW_OUT_NULL) return 0;
    proven_err_t e = proven_writer_flush(o->w);
    if (!proven_is_ok(e) && proven_is_ok(o->err)) o->err = e;
    return proven_is_ok(o->err) ? 0 : -1;
}

void low_flush_std(void) {
    if (g_std_ready[0]) (void)low_flush(&g_std[0]);
    if (g_std_ready[1]) (void)low_flush(&g_std[1]);
}

int low_out_close(low_out_t *o) {
    if (!o) return -1;
    int rc = low_flush(o);
    if (o->kind == LOW_OUT_STD) return rc;
    if (o->kind == LOW_OUT_FILE) {
        for (low_out_t **pp = &g_open_files; *pp; pp = &(*pp)->next_open) if (*pp == o) { *pp = o->next_open; break; }
        proven_err_t e = proven_fs_close(o->file);   /* ★ 쓴 파일의 닫기는 쓰기가 닿았는지의 마지막 답이다 */
        if (!proven_is_ok(e)) rc = -1;
    } else if (o->kind == LOW_OUT_MEM) {
        proven_u8str_destroy(low_heap(), &o->mem);
    }
    low_free(o);
    return rc;
}

size_t low_write(low_out_t *o, const void *p, size_t n) {
    if (!o) return 0;
    if (!n) return 0;
    if (o->kind == LOW_OUT_NULL) { o->count += n; return n; }
    if (!proven_is_ok(o->err)) return 0;
    proven_err_t e = proven_writer_write(o->w, (proven_mem_view_t){ .ptr = (const proven_byte_t *)p, .size = (proven_size_t)n });
    if (!proven_is_ok(e)) { o->err = e; return 0; }
    o->count += n;
    if (o->line && memchr(p, '\n', n)) (void)low_flush(o);
    return n;
}

int low_puts(low_out_t *o, const char *s) {
    size_t n = s ? strlen(s) : 0;
    return low_write(o, s, n) == n ? 0 : -1;
}

int low_putc(low_out_t *o, int c) {
    unsigned char b = (unsigned char)c;
    return low_write(o, &b, 1) == 1 ? (int)b : -1;
}

/* ── 표준 입력 ── */
static proven_sysio_std_t g_in_std;
static proven_reader_t g_in;
static bool g_in_ready;
static int g_in_push = -1;

static void in_ready(void) {
    if (!g_in_ready) { g_in_ready = true; g_in = proven_sysio_stdin_reader(&g_in_std); }
    if (g_std_ready[0] && g_std[0].line) (void)low_flush(&g_std[0]);   /* 터미널이면 물음이 먼저 나간다 */
}

size_t low_stdin_read(void *dst, size_t n) {
    in_ready();
    unsigned char *d = (unsigned char *)dst;
    size_t got = 0;
    if (n && g_in_push >= 0) { d[got++] = (unsigned char)g_in_push; g_in_push = -1; }
    while (got < n) {
        proven_result_size_t r = proven_reader_read(g_in, (proven_mem_mut_t){ .ptr = d + got, .size = (proven_size_t)(n - got) });
        if (!proven_is_ok(r.err) || r.value == 0) break;
        got += (size_t)r.value;
    }
    return got;
}

bool low_stdin_more(void) {
    if (g_in_push >= 0) return true;
    unsigned char b;
    if (low_stdin_read(&b, 1) == 1) { g_in_push = b; return true; }
    return false;
}

long long low_stdin_read_some(void *dst, size_t n) {
    in_ready();
    unsigned char *d = (unsigned char *)dst;
    if (n && g_in_push >= 0) { d[0] = (unsigned char)g_in_push; g_in_push = -1; return 1; }
    proven_result_size_t r = proven_fs_read(proven_sysio_stdin(), (proven_mem_mut_t){ .ptr = d, .size = (proven_size_t)n });
    if (r.err == PROVEN_ERR_EOF) return 0;   /* proven 은 끝을 값(PROVEN_ERR_EOF)으로 알린다 — read(0) 처럼 0 바이트 */
    return proven_is_ok(r.err) ? (long long)r.value : -1;
}

long long low_out_seek(low_out_t *o, long long off, int whence) {
    if (!o || o->kind != LOW_OUT_FILE || low_flush(o) != 0) return -1;   /* fseek 처럼: 먼저 비운다 */
    proven_result_u64_t r = proven_fs_seek(o->file, (proven_i64)off, (proven_fs_whence_t)whence);
    return proven_is_ok(r.err) ? (long long)r.val : -1;
}
