/* TNUX libc: stdio.h (só para o alvo WebAssembly): só a família snprintf */
#ifndef TNUX_STDIO_H
#define TNUX_STDIO_H
#include <stddef.h>
#include <stdarg.h>
int vsnprintf(char *d, size_t n, const char *fmt, va_list ap);
int snprintf(char *d, size_t n, const char *fmt, ...);
#endif
