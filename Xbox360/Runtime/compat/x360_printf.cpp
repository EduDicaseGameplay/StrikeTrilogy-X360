// C99 snprintf/vsnprintf e printf para o código do clang, sobre o formatador de x360_format.cpp (SotC):
// nenhum va_list passa do clang para a CRT do XDK. printf vai para o log do título.
#include <stddef.h>
#include <stdio.h>
extern "C" int x360_vsnprintf_c99(char *, size_t, const char *, __builtin_va_list);
extern "C" int x360_vfprintf(FILE *, const char *, __builtin_va_list);
extern "C" int vsnprintf(char *b, size_t n, const char *f, __builtin_va_list ap) { return x360_vsnprintf_c99(b, n, f, ap); }
// snprintf vem do builtins.c do OXDK, que chama __builtin_vsnprintf -> o vsnprintf acima.
extern "C" int x360_printf(const char *f, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, f);
    const int r = x360_vfprintf(stdout, f, ap);
    __builtin_va_end(ap);
    return r;
}
