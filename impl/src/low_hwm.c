#include "low_hwm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 표는 몇 개 안 된다(감사 대상 17). 넘치면 **그것도 말한다** — 계측이 조용히 자르면
// 계측을 못 믿는다(이 파일이 존재하는 이유가 바로 그 병이다).
#define HWM_MAX 64
static struct { const char *name; size_t hi, cap; } g[HWM_MAX];
static size_t g_n;
static int g_on = -1;
static int g_full;

int low_hwm_on(void) {
    if (g_on < 0) g_on = getenv("LOW_HWM") ? 1 : 0;
    return g_on;
}

void low_hwm_note(const char *name, size_t v, size_t cap) {
    for (size_t i = 0; i < g_n; i++)
        if (strcmp(g[i].name, name) == 0) { if (v > g[i].hi) g[i].hi = v; return; }
    if (g_n >= HWM_MAX) { g_full = 1; return; }
    g[g_n].name = name; g[g_n].hi = v; g[g_n].cap = cap; g_n++;
}

void low_hwm_dump(void) {
    if (!low_hwm_on()) return;
    for (size_t i = 0; i < g_n; i++)
        fprintf(stderr, "HWM\t%s\t%zu\t%zu\n", g[i].name, g[i].hi, g[i].cap);
    if (g_full) fprintf(stderr, "HWM\t(표가 넘쳤다 — 계측 자신이 자르고 있다)\t0\t0\n");
}
