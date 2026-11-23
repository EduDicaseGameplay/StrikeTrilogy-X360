/* Strike Trilogy X360 system services: log file, crash logger, threads, events, time.
 * Derived from MDX360-Runtime platform/x360/mdx_x360_sys.c (console-tested), without the
 * Mega Drive pad part (Xbox360/Input/strike_input.c replaces it).
 * The log avoids the XDK's printf family on purpose: a clang va_list passed into _vsnprintf
 * corrupted a saved register on the console (SotC X360 test 14, noted in SoR2-X360). */
#include <xtl.h>
#include <string.h>
#include <stdlib.h>

#include "mdx_x360.h"

/* ---- log ---- */
static CRITICAL_SECTION g_log_cs;
static volatile LONG g_log_ready;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static const char *g_log_path = "game:\strike_log.txt";

void mdx_log_set_path(const char *path) { if (path) g_log_path = path; }

static void log_raw(const char *s, unsigned n) {
    DWORD w;
    if (InterlockedCompareExchange(&g_log_ready, 1, 0) == 0) {
        InitializeCriticalSection(&g_log_cs);
        g_log = CreateFileA(g_log_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
        InterlockedExchange(&g_log_ready, 2);
    }
    while (g_log_ready != 2) SwitchToThread();
    EnterCriticalSection(&g_log_cs);
    if (g_log != INVALID_HANDLE_VALUE) { WriteFile(g_log, s, n, &w, NULL); FlushFileBuffers(g_log); }
    LeaveCriticalSection(&g_log_cs);
}

void mdx_log(const char *text) {
    char line[512];
    unsigned n = (unsigned)strlen(text);
    if (n > sizeof line - 2) n = sizeof line - 2;
    memcpy(line, text, n);
    line[n++] = '\n';
    log_raw(line, n);
}

void mdx_log_int(const char *label, int v) {
    /* int only: a long long argument here made Xenia's JIT report "Invalid handling of constant". */
    char line[256], num[24];
    int k = 0, neg = v < 0;
    unsigned u = neg ? 0u - (unsigned)v : (unsigned)v;
    unsigned n = (unsigned)strlen(label);
    if (n > 200) n = 200;
    do { num[k++] = (char)('0' + u % 10); u /= 10; } while (u);
    memcpy(line, label, n);
    line[n++] = ' ';
    if (neg) line[n++] = '-';
    while (k) line[n++] = num[--k];
    line[n] = 0;
    mdx_log(line);
}

static void log_hex(const char *label, unsigned v) {
    char s[64];
    unsigned n = (unsigned)strlen(label);
    int i;
    if (n > 40) n = 40;
    memcpy(s, label, n);
    s[n++] = ' ';
    for (i = 7; i >= 0; i--) s[n++] = "0123456789ABCDEF"[(v >> (i * 4)) & 15];
    s[n] = 0;
    mdx_log(s);
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *e) {
    mdx_log("[crash] unhandled exception");
    if (e && e->ExceptionRecord) {
        log_hex("[crash] code", (unsigned)e->ExceptionRecord->ExceptionCode);
        log_hex("[crash] address", (unsigned)(uintptr_t)e->ExceptionRecord->ExceptionAddress);
    }
    if (e && e->ContextRecord) {
        log_hex("[crash] iar", (unsigned)e->ContextRecord->Iar);
        log_hex("[crash] lr", (unsigned)e->ContextRecord->Lr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void mdx_install_crash_logger(void) { SetUnhandledExceptionFilter(crash_filter); }

/* ---- threads and events ---- */
typedef struct { mdx_thread_fn fn; void *arg; } ThreadStart;

static DWORD WINAPI thread_main(LPVOID p) {
    ThreadStart t = *(ThreadStart *)p;
    free(p);
    t.fn(t.arg);
    return 0;
}

int mdx_thread_start(mdx_thread_fn fn, void *arg, int hw_thread, int stack_kb) {
    ThreadStart *t = (ThreadStart *)malloc(sizeof *t);
    HANDLE h;
    if (!t) return 0;
    t->fn = fn; t->arg = arg;
    h = CreateThread(NULL, (SIZE_T)(stack_kb > 0 ? stack_kb : 256) * 1024, thread_main, t, CREATE_SUSPENDED, NULL);
    if (!h) { free(t); return 0; }
    if (hw_thread >= 0 && hw_thread < 6) XSetThreadProcessor(h, (DWORD)hw_thread);
    ResumeThread(h);
    CloseHandle(h);
    return 1;
}
void *mdx_event_create(void) { return CreateEvent(NULL, FALSE, FALSE, NULL); }
void mdx_event_set(void *e) { SetEvent((HANDLE)e); }
void mdx_event_wait(void *e) { WaitForSingleObject((HANDLE)e, INFINITE); }

/* ---- time ---- */
uint32_t mdx_ticks(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (uint32_t)c.LowPart; }
uint32_t mdx_ticks_per_second(void) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (uint32_t)f.LowPart; }
