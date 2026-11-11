/*
 * TNUX SDK: o mínimo para escrever programas em C que rodam dentro do TNUX no navegador.
 *
 * Um programa do TNUX é um arquivo WebAssembly no padrão WASI. Com este cabeçalho você não
 * precisa de mais nada além do clang:
 *
 *     #include "tnuxsdk.h"
 *     int main(int argc, char **argv) { printf("Olá, %s!\n", argc > 1 ? argv[1] : "mundo"); return 0; }
 *
 *     make programas          (gera web/programas/NOME.wasm)
 *
 * Programas feitos com o wasi-sdk (libc completa), Rust (wasm32-wasip1), Go/TinyGo ou Zig também
 * rodam: é só copiar o .wasm para dentro do TNUX e dar chmod +x.
 */
#ifndef TNUXSDK_H
#define TNUXSDK_H
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#define WASI(nome) __attribute__((import_module("wasi_snapshot_preview1"), import_name(nome)))
typedef struct { const void *buf; uint32_t len; } tnux_iov;
WASI("fd_write") int __wasi_fd_write(int fd, const tnux_iov *iov, int n, uint32_t *escritos);
WASI("fd_read") int __wasi_fd_read(int fd, const tnux_iov *iov, int n, uint32_t *lidos);
WASI("fd_close") int __wasi_fd_close(int fd);
WASI("args_sizes_get") int __wasi_args_sizes_get(uint32_t *qtd, uint32_t *tam);
WASI("args_get") int __wasi_args_get(char **argv, char *buf);
WASI("proc_exit") _Noreturn void __wasi_proc_exit(int codigo);
WASI("path_open") int __wasi_path_open(int dirfd, int dirflags, const char *cam, uint32_t tam, int oflags,
                                       uint64_t direitos, uint64_t herdados, int fdflags, int *fd);
WASI("clock_time_get") int __wasi_clock_time_get(int id, uint64_t precisao, uint64_t *ns);
WASI("random_get") int __wasi_random_get(void *buf, uint32_t n);

static size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
void *memcpy(void *d, const void *s, size_t n) { char *a = d; const char *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, size_t n) { char *a = d; while (n--) *a++ = (char)c; return d; }
static int atoi(const char *s) { int v = 0, neg = 0; if (*s == '-') { neg = 1; s++; } while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return neg ? -v : v; }

static void escrever(int fd, const char *p, size_t n) {
    tnux_iov v = { p, (uint32_t)n };
    uint32_t w;
    __wasi_fd_write(fd, &v, 1, &w);
}
/* lê até n bytes da entrada (fd 0) ou de um arquivo aberto; devolve quantos leu (0 = fim) */
static int ler(int fd, char *p, size_t n) {
    tnux_iov v = { p, (uint32_t)n };
    uint32_t r = 0;
    if (__wasi_fd_read(fd, &v, 1, &r)) return -1;
    return (int)r;
}
/* abre um arquivo: modo "r" (ler), "w" (criar/zerar) ou "a" (acrescentar). Devolve o fd ou -1. */
static int abrir(const char *cam, const char *modo) {
    int fd = -1, oflags = 0, fdflags = 0;
    uint64_t direitos = 2;                       /* ler */
    if (modo[0] == 'w') { oflags = 1 | 8; direitos |= 64; }
    else if (modo[0] == 'a') { oflags = 1; fdflags = 1; direitos |= 64; }
    int dir = cam[0] == '/' ? 3 : 4;             /* 3 = raiz, 4 = diretório atual */
    if (__wasi_path_open(dir, 1, cam[0] == '/' ? cam + 1 : cam, (uint32_t)strlen(cam[0] == '/' ? cam + 1 : cam), oflags, direitos, direitos, fdflags, &fd)) return -1;
    return fd;
}
static void fechar(int fd) { __wasi_fd_close(fd); }

/* printf pequeno: %s %d %u %x %c %% e largura (ex.: %5d, %-10s, %02d) */
static int vfprintf_fd(int fd, const char *fmt, va_list ap) {
    char out[512]; size_t o = 0; int total = 0;
#define POE(ch) do { if (o == sizeof out) { escrever(fd, out, o); o = 0; } out[o++] = (ch); total++; } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') { POE(*fmt); continue; }
        fmt++;
        int esq = 0, zero = 0, larg = 0;
        if (*fmt == '-') { esq = 1; fmt++; }
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') larg = larg * 10 + (*fmt++ - '0');
        if (*fmt == 'l') fmt++;
        char tmp[24]; const char *s = tmp; int n = 0;
        if (*fmt == 's') { s = va_arg(ap, const char *); if (!s) s = "(null)"; n = (int)strlen(s); }
        else if (*fmt == 'c') { tmp[0] = (char)va_arg(ap, int); n = 1; }
        else if (*fmt == 'd' || *fmt == 'u' || *fmt == 'x') {
            unsigned v; int neg = 0;
            if (*fmt == 'd') { int d = va_arg(ap, int); neg = d < 0; v = neg ? 0u - (unsigned)d : (unsigned)d; } else v = va_arg(ap, unsigned);
            unsigned base = *fmt == 'x' ? 16 : 10;
            char *p = tmp + sizeof tmp;
            do { *--p = "0123456789abcdef"[v % base]; v /= base; } while (v);
            if (neg) *--p = '-';
            s = p; n = (int)(tmp + sizeof tmp - p);
        } else if (*fmt == '%') { tmp[0] = '%'; n = 1; }
        else if (!*fmt) break;
        if (!esq) for (int i = n; i < larg; i++) POE(zero ? '0' : ' ');
        for (int i = 0; i < n; i++) POE(s[i]);
        if (esq) for (int i = n; i < larg; i++) POE(' ');
    }
#undef POE
    if (o) escrever(fd, out, o);
    return total;
}
static int printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfprintf_fd(1, fmt, ap); va_end(ap); return r; }
static int eprintf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfprintf_fd(2, fmt, ap); va_end(ap); return r; }
static void puts_(const char *s) { escrever(1, s, strlen(s)); escrever(1, "\n", 1); }

int main(int argc, char **argv);

/* ponto de entrada do WASI: monta argc/argv e chama o main */
__attribute__((export_name("_start"))) void _start(void) {
    static char *argv[64];
    static char buf[4096];
    uint32_t qtd = 0, tam = 0;
    __wasi_args_sizes_get(&qtd, &tam);
    if (qtd > 63 || tam > sizeof buf) { qtd = 0; }
    else __wasi_args_get(argv, buf);
    argv[qtd] = NULL;
    __wasi_proc_exit(main((int)qtd, argv));
}

/* evita avisos de função não usada nos programas pequenos */
static inline void tnuxsdk_usa_tudo(void) { (void)strcmp; (void)atoi; (void)ler; (void)abrir; (void)fechar; (void)printf; (void)eprintf; (void)puts_; }
#endif
