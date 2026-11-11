/* TNUX libc: stdlib.h (só para o alvo WebAssembly) */
#ifndef TNUX_STDLIB_H
#define TNUX_STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void *calloc(size_t a, size_t b);
void *realloc(void *p, size_t n);
void free(void *p);
size_t tnux_heap_usado(void);
size_t tnux_heap_total(void);
long strtol(const char *s, char **fim, int base);
unsigned long strtoul(const char *s, char **fim, int base);
long long strtoll(const char *s, char **fim, int base);
double strtod(const char *s, char **fim);
int atoi(const char *s);
long atol(const char *s);
int abs(int v);
void qsort(void *base, size_t n, size_t tam, int (*cmp)(const void *, const void *));
void abort(void);
#endif
