/* Incluído à força em todo C do port (clang Xenon + CRT do XDK).
 * Completa o que a CRT do VS2010 não tem ou escreve diferente. */
#ifndef X360_COMPAT_H
#define X360_COMPAT_H
/* printf no lado do clang (x360_format.cpp, x360_printf.cpp; solução do SotC X360): o teste 14 do SotC no
 * console mostrou um registrador salvo corrompido depois de um va_list do clang entrar no _vsnprintf do XDK.
 * Os nomes da Microsoft são trocados antes dos headers do XDK; snprintf/vsnprintf mantêm o nome. */
#define _vsnprintf x360_vsnprintf_ms
#define _snprintf  x360_snprintf_ms
#define _vscprintf x360_vscprintf
#define vfprintf   x360_vfprintf
#define fprintf    x360_fprintf
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
int x360_vsnprintf_ms(char *buf, size_t n, const char *f, va_list ap);
int x360_snprintf_ms(char *buf, size_t n, const char *f, ...);
int x360_vscprintf(const char *f, va_list ap);
int x360_vfprintf(FILE *file, const char *f, va_list ap);
int x360_fprintf(FILE *file, const char *f, ...);
int x360_printf(const char *f, ...);
int snprintf(char *b, size_t n, const char *f, ...);
int vsnprintf(char *b, size_t n, const char *f, va_list ap);
#ifdef __cplusplus
}
#endif
#ifndef __cplusplus
#define printf x360_printf
#endif

/* Intrínsecos do MSVC que o clang não oferece com este alvo (ymfm usa no caminho _MSC_VER). */
static __inline unsigned char x360_bsr(unsigned long *i, unsigned long v) {
    if (!v) return 0; *i = 31u - (unsigned long)__builtin_clz((unsigned)v); return 1;
}
static __inline unsigned char x360_bsf(unsigned long *i, unsigned long v) {
    if (!v) return 0; *i = (unsigned long)__builtin_ctz((unsigned)v); return 1;
}
#define _BitScanReverse x360_bsr
#define _BitScanForward x360_bsf

#ifndef __cplusplus
/* C99 que a CRT do VS2010 não tem (no C++ a libc++ declara as suas). */
#include <math.h>
static __inline float  x360_fminf(float a, float b)   { return a < b ? a : b; }
static __inline float  x360_fmaxf(float a, float b)   { return a > b ? a : b; }
static __inline double x360_fmin(double a, double b)  { return a < b ? a : b; }
static __inline double x360_fmax(double a, double b)  { return a > b ? a : b; }
static __inline float  x360_roundf(float v)  { return v < 0 ? -floorf(-v + 0.5f) : floorf(v + 0.5f); }
static __inline double x360_round(double v)  { return v < 0 ? -floor(-v + 0.5) : floor(v + 0.5); }
static __inline long   x360_lroundf(float v) { return (long)x360_roundf(v); }
static __inline long   x360_lround(double v) { return (long)x360_round(v); }
static __inline float  x360_truncf(float v)  { return v < 0 ? -floorf(-v) : floorf(v); }
#define fminf  x360_fminf
#define fmaxf  x360_fmaxf
#define fmin   x360_fmin
#define fmax   x360_fmax
#define roundf x360_roundf
#define round  x360_round
#define lroundf x360_lroundf
#define lround x360_lround
#define truncf x360_truncf

/* POSIX: mkdir(path, mode) -> _mkdir(path); stat de arquivo comum e pasta. */
#include <direct.h>
#include <sys/types.h>
#include <sys/stat.h>
int x360_mkdir(const char *path);   /* posix_x360.c: CreateDirectory, either slash */
#define mkdir(p, m) x360_mkdir(p)
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
/* Sem processos no console: o seletor de arquivos do Linux (zenity) não existe. */
#define popen(c, m) ((FILE *)0)
#define pclose(f)   (0)
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#endif
