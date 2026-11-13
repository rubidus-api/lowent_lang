#ifndef LOW_OUT_H
#define LOW_OUT_H
/* ★★ 처리기가 내보내는 바이트의 통로 — 모두 proven_c_lib 의 쓰기 도구(writer) 위에 (2026-11-13 · W-20261113-0024 · 소유자 «단계B 이어서»).
 *
 *   low_out_t 하나가 FILE * 의 자리를 맡는다. 네 가지가 있다:
 *     · 표준 출력  low_stdout() — fd 1 위의 proven 버퍼 쓰기 도구. 터미널이면 줄마다 내보낸다(stdio 와 같다).
 *     · 표준 오류  low_stderr() — fd 2 위의 버퍼 없는 proven 쓰기 도구(stdio 의 stderr 처럼 곧바로 나간다).
 *     · 파일      low_out_open(path, "w" | "wb" | "a") — proven_fs_open + 버퍼 쓰기 도구. 짝은 low_out_close.
 *     · 메모리    low_out_mem() — proven u8str 쓰기 도구(open_memstream 의 자리). low_out_mem_view 로 읽고 low_out_close 로 돌려준다.
 *     · 버리기    low_out_null() — 쓴 바이트를 세기만 한다(fopen("/dev/null") 의 자리). 짝은 low_out_close.
 *
 *   ★ fd 1 에 쓰는 것은 이 하나뿐이다: 처리기의 글 · `--emit-c` · VM 이 내는 프로그램 출력이 모두 low_stdout() 을 지나므로 순서가 선다.
 *     비우는 자리: 끝날 때(atexit) · system / popen / fork 앞 · 표준 입력을 기다리기 앞 · `--fmt` 의 fd 바꿔치기 앞뒤 · VM 의 flush op.
 *   ★ 오류는 통로에 남는다(proven 의 버퍼 쓰기 도구가 한 번 실패하면 계속 실패를 답한다). 쓴 파일을 닫을 때 그 답을 본다 —
 *     low_out_close 가 -1 이면 그 파일은 다 쓰이지 않았다(전엔 fclose 의 답을 버리던 자리가 있었다).
 *
 *   돌려주는 값은 stdio 를 닮았다: low_puts → 0 / -1 · low_putc → 그 글자 / -1 · low_write → 받은 바이트 수 · low_flush → 0 / -1.
 */
#include <stdbool.h>
#include <stddef.h>
#include "proven/types.h"
#include "proven/u8str.h"

typedef struct low_out low_out_t;

low_out_t *low_stdout(void);
low_out_t *low_stderr(void);
low_out_t *low_out_open(const char *path, const char *mode);
low_out_t *low_out_mem(void);
low_out_t *low_out_null(void);
int  low_out_close(low_out_t *o);              /* 비우고 닫고 돌려준다 — 표준 통로에는 비우기만 한다 */
proven_u8str_view_t low_out_mem_view(low_out_t *o);   /* 메모리 통로의 지금까지 글(닫기 전까지 산다) */
proven_size_t low_out_count(const low_out_t *o);      /* 이 통로가 받은 바이트 수 */
bool low_out_failed(const low_out_t *o);

int    low_flush(low_out_t *o);
void   low_flush_std(void);                    /* 표준 출력 · 표준 오류를 비운다 — 자식 프로세스 · fd 바꿔치기 · 입력 기다리기 앞 */
size_t low_write(low_out_t *o, const void *p, size_t n);
int    low_puts(low_out_t *o, const char *s);
int    low_putc(low_out_t *o, int c);
long long low_out_seek(low_out_t *o, long long off, int whence);   /* 파일 통로: 비우고 옮긴다 · 새 위치 또는 -1 (whence 0 set · 1 cur · 2 end) */

/* 표준 입력 — VM 이 프로그램 대신 읽는다(W-20261113-0024 B4). proven 의 stdin 읽기 도구 위에 한 바이트 되돌림 칸을 둔다.
 *   low_stdin_read(dst, n) — fread 처럼 n 바이트가 차거나 입력이 끝날 때까지 읽는다(받은 수를 돌려준다).
 *   low_stdin_more()       — 더 읽을 것이 있는가: 한 바이트를 떠 보고 그 바이트는 되돌려 둔다(다음 읽기가 먼저 받는다).
 *   low_stdin_read_some(dst, n) — 한 번만 읽는다(read(0) 의 자리 · 터미널). 오류면 -1.
 *   ★ 기다리기 전에, 표준 출력이 터미널(줄 모드)이면 비운다 — stdio 가 하던 그대로다(물음을 찍고 답을 기다리는 프로그램). */
size_t low_stdin_read(void *dst, size_t n);
bool   low_stdin_more(void);
long long low_stdin_read_some(void *dst, size_t n);

/* 파일 읽기 — 처리기가 읽는 파일(소스 · 라이브러리 · include_text · pkg 파일 · 캐시)을 proven 의 파일로 (W-20261113-1122 · 소유자 «넵 진행»).
 *   low_read_file(path, max, &buf, &n, &rd_err) — 0 이면 읽었다(buf 는 proven 힙 · n+1 바이트 · 끝에 NUL · 짝은 low_free), -1 이면 열지 못했다.
 *     max == 0 → 파일 전체(크기를 먼저 잰다 — 크기를 모르면 -1). max > 0 → 많아야 max 바이트(전의 `fread(b, 1, max, f)`).
 *     읽다가 오류가 나면 *rd_err = true 이고 그때까지 읽은 것을 준다(전의 `ferror`).
 *   low_file_readable(path) — 읽으려고 열 수 있는가(전의 `fopen(p, "rb")` 뒤 `fclose`).
 *   low_lines_t / low_lines_next — 메모리의 글을 `fgets(ln, cap, f)` 와 같은 조각으로 준다: 많아야 cap-1 바이트, 줄바꿈까지. */
int  low_read_file(const char *path, proven_size_t max, proven_byte_t **buf, proven_size_t *n, bool *rd_err);
bool low_file_readable(const char *path);
typedef struct { const proven_byte_t *p; proven_size_t n, at; } low_lines_t;
bool low_lines_next(low_lines_t *L, char *ln, size_t cap);
/* 표준 출력을 잠시 메모리 통로로 돌린다(`--fmt` 가 자기 출력을 받아 다시 쓴다 — 전엔 fd 1 을 임시 파일로 바꿔치기했다). 앞의 통로를 돌려준다. */
low_out_t *low_stdout_redirect(low_out_t *to);
/* 디렉터리 목록(처리기가 표준 모듈 · 키를 찾는다) — proven 의 디렉터리 읽기. `.` · `..` 은 오지 않는다. 이름이 UTF-8 이 아닌 항목은 건너뛴다.
 *   ★ VM 이 프로그램 대신 하는 `dir_read` 는 여기를 쓰지 않는다: 내보내는 네이티브 런타임이 libc readdir 로 `.` · `..` 까지 주므로, VM 도
 *     libc 그대로 둬야 두 쪽이 같은 답을 낸다(low_vm.c). */
typedef struct { void *d; } low_dir_t;
bool low_dir_open(low_dir_t *d, const char *path);
bool low_dir_next(low_dir_t *d, char *name, size_t cap);   /* 다음 이름(NUL 로 끝남) · 끝이면 false */
void low_dir_close(low_dir_t *d);
#endif
