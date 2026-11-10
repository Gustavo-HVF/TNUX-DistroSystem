/*
 * TNUX libc: a biblioteca C mínima do núcleo quando ele é compilado para WebAssembly.
 *
 * No navegador não existe glibc: o núcleo roda "em cima do metal" do WebAssembly, então as funções
 * que ele usa (memória, strings, snprintf, qsort...) estão todas aqui. Na compilação nativa
 * (terminal) este arquivo não entra: lá o núcleo usa a libc do próprio aparelho.
 *
 * Compile sempre com -fno-builtin, senão o compilador troca os laços abaixo por chamadas a
 * memcpy/memset e a função passa a chamar a si mesma.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* ------------------------------------------------------------------ memória e strings */

void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *a = d; const unsigned char *b = s;
    while (n--) *a++ = *b++;
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    unsigned char *a = d; const unsigned char *b = s;
    if (a == b || n == 0) return d;
    if (a < b) { while (n--) *a++ = *b++; }
    else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
void *memset(void *d, int c, size_t n) {
    unsigned char *a = d;
    while (n--) *a++ = (unsigned char)c;
    return d;
}
int memcmp(const void *x, const void *y, size_t n) {
    const unsigned char *a = x, *b = y;
    for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
    return 0;
}
void *memchr(const void *s, int c, size_t n) {
    const unsigned char *a = s;
    for (; n; n--, a++) if (*a == (unsigned char)c) return (void *)a;
    return NULL;
}
size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
    }
    return 0;
}
int strcasecmp(const char *a, const char *b) {
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}
int strncasecmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y) return x - y;
        if (!x) return 0;
    }
    return 0;
}
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) {} return r; }
char *strncpy(char *d, const char *s, size_t n) {
    char *r = d;
    while (n && *s) { *d++ = *s++; n--; }
    while (n--) *d++ = 0;
    return r;
}
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strchr(const char *s, int c) {
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}
char *strrchr(const char *s, int c) {
    const char *r = NULL;
    for (;; s++) {
        if (*s == (char)c) r = s;
        if (!*s) return (char *)r;
    }
}
char *strstr(const char *h, const char *n) {
    size_t ln = strlen(n);
    if (!ln) return (char *)h;
    for (; *h; h++) if (*h == *n && strncmp(h, n, ln) == 0) return (char *)h;
    return NULL;
}
char *strndup(const char *s, size_t n) {
    size_t l = 0;
    while (l < n && s[l]) l++;
    char *r = malloc(l + 1);
    if (!r) return NULL;
    memcpy(r, s, l); r[l] = 0;
    return r;
}
char *strdup(const char *s) { return strndup(s, strlen(s)); }
size_t strspn(const char *s, const char *a) {
    size_t n = 0;
    while (s[n] && strchr(a, s[n])) n++;
    return n;
}
size_t strcspn(const char *s, const char *r) {
    size_t n = 0;
    while (s[n] && !strchr(r, s[n])) n++;
    return n;
}

/* ------------------------------------------------------------------ ctype */

int isdigit(int c) { return c >= '0' && c <= '9'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isprint(int c) { return c >= 32 && c < 127; }
int ispunct(int c) { return isprint(c) && !isalnum(c) && c != ' '; }
int toupper(int c) { return islower(c) ? c - 32 : c; }
int tolower(int c) { return isupper(c) ? c + 32 : c; }

/* ------------------------------------------------------------------ malloc
 * Blocos com cabeçalho de 8 bytes (tamanho + bloco físico anterior) e lista dupla dos blocos
 * livres guardada dentro do próprio espaço livre. Junta blocos vizinhos ao liberar. A memória
 * cresce pedindo páginas de 64 KiB ao WebAssembly.
 */

typedef struct Blk {
    size_t tam;              /* tamanho total do bloco (com cabeçalho); bit 0 = em uso */
    struct Blk *ant;         /* bloco físico anterior */
    struct Blk *lprox, *lant; /* lista de livres (só vale quando livre) */
} Blk;

#define CAB 8u
#define MINBLK 16u
#define TAM(b) ((b)->tam & ~(size_t)1)
#define USADO(b) ((b)->tam & 1)

extern unsigned char __heap_base;
static char *heap_ini, *heap_fim;   /* fim = primeiro byte ainda não usado por bloco */
static Blk *ultimo, *livres;
static size_t em_uso;

static int crescer_ate(char *novo_fim) {
    size_t atual = (size_t)__builtin_wasm_memory_size(0) * 65536u;
    if ((size_t)novo_fim <= atual) return 1;
    size_t falta = (size_t)novo_fim - atual;
    size_t pags = (falta + 65535u) / 65536u;
    if (pags < 16) pags = 16;   /* cresce de 1 MiB em 1 MiB para chamar menos vezes */
    if (__builtin_wasm_memory_grow(0, pags) == (size_t)-1) {
        pags = (falta + 65535u) / 65536u;
        if (__builtin_wasm_memory_grow(0, pags) == (size_t)-1) return 0;
    }
    return 1;
}
static void tira_livre(Blk *b) {
    if (b->lant) b->lant->lprox = b->lprox; else livres = b->lprox;
    if (b->lprox) b->lprox->lant = b->lant;
}
static void poe_livre(Blk *b) {
    b->tam &= ~(size_t)1;
    b->lant = NULL; b->lprox = livres;
    if (livres) livres->lant = b;
    livres = b;
}
static Blk *prox_fisico(Blk *b) { return b == ultimo ? NULL : (Blk *)((char *)b + TAM(b)); }

/* corta o bloco b (em uso) para "precisa" bytes, devolvendo a sobra para a lista de livres */
static void cortar(Blk *b, size_t precisa) {
    size_t tam = TAM(b);
    if (tam < precisa + MINBLK) return;
    Blk *r = (Blk *)((char *)b + precisa);
    r->tam = tam - precisa; r->ant = b;
    Blk *p = prox_fisico(b);
    b->tam = precisa | 1;
    if (b == ultimo) ultimo = r; else p->ant = r;
    /* junta com o próximo, se livre */
    Blk *n = prox_fisico(r);
    if (n && !USADO(n)) {
        tira_livre(n);
        if (n == ultimo) ultimo = r; else prox_fisico(n)->ant = r;
        r->tam += TAM(n);
    }
    poe_livre(r);
}

void *malloc(size_t n) {
    if (!heap_ini) {
        heap_ini = (char *)(((size_t)&__heap_base + 15u) & ~(size_t)15u);
        heap_fim = heap_ini;
    }
    if (n > 0x7ff00000u) return NULL;
    size_t precisa = (n + CAB + 7u) & ~(size_t)7u;
    if (precisa < MINBLK) precisa = MINBLK;
    for (Blk *b = livres; b; b = b->lprox) {
        if (TAM(b) >= precisa) {
            tira_livre(b);
            b->tam |= 1;
            cortar(b, precisa);
            em_uso += TAM(b);
            return (char *)b + CAB;
        }
    }
    Blk *b;
    if (ultimo && !USADO(ultimo)) {       /* estica o último bloco livre */
        b = ultimo;
        size_t falta = precisa - TAM(b);
        if (!crescer_ate(heap_fim + falta)) return NULL;
        tira_livre(b);
        heap_fim += falta;
        b->tam = precisa | 1;
    } else {
        if (!crescer_ate(heap_fim + precisa)) return NULL;
        b = (Blk *)heap_fim;
        heap_fim += precisa;
        b->tam = precisa | 1;
        b->ant = ultimo;
        ultimo = b;
    }
    em_uso += precisa;
    return (char *)b + CAB;
}

void free(void *p) {
    if (!p) return;
    Blk *b = (Blk *)((char *)p - CAB);
    em_uso -= TAM(b);
    b->tam &= ~(size_t)1;
    Blk *n = prox_fisico(b);
    if (n && !USADO(n)) {
        tira_livre(n);
        if (n == ultimo) ultimo = b; else prox_fisico(n)->ant = b;
        b->tam += TAM(n);
    }
    Blk *a = b->ant;
    if (a && !USADO(a)) {
        tira_livre(a);
        if (b == ultimo) ultimo = a; else prox_fisico(b)->ant = a;
        a->tam += TAM(b);
        b = a;
    }
    poe_livre(b);
}

void *calloc(size_t a, size_t b) {
    size_t n = a * b;
    if (b && n / b != a) return NULL;
    void *p = malloc(n);
    if (p) memset(p, 0, n);
    return p;
}

void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (!n) { free(p); return NULL; }
    Blk *b = (Blk *)((char *)p - CAB);
    size_t tam = TAM(b);
    size_t precisa = (n + CAB + 7u) & ~(size_t)7u;
    if (precisa < MINBLK) precisa = MINBLK;
    if (precisa <= tam) {
        em_uso -= tam;
        cortar(b, precisa);
        em_uso += TAM(b);
        return p;
    }
    /* tenta crescer no lugar: próximo bloco livre, ou fim do heap */
    Blk *prox = prox_fisico(b);
    if (prox && !USADO(prox) && tam + TAM(prox) >= precisa) {
        tira_livre(prox);
        if (prox == ultimo) ultimo = b; else prox_fisico(prox)->ant = b;
        em_uso -= tam;
        b->tam = (tam + TAM(prox)) | 1;
        cortar(b, precisa);
        em_uso += TAM(b);
        return p;
    }
    if (b == ultimo) {
        size_t falta = precisa - tam;
        if (crescer_ate(heap_fim + falta)) {
            heap_fim += falta;
            b->tam = precisa | 1;
            em_uso += falta;
            return p;
        }
    }
    /* dobra a folga em blocos grandes para não copiar a cada acréscimo */
    void *q = malloc(n);
    if (!q) return NULL;
    memcpy(q, p, tam - CAB < n ? tam - CAB : n);
    free(p);
    return q;
}

size_t tnux_heap_usado(void) { return em_uso; }
size_t tnux_heap_total(void) { return (size_t)__builtin_wasm_memory_size(0) * 65536u; }

void abort(void) { __builtin_trap(); }

/* ------------------------------------------------------------------ números */

int abs(int v) { return v < 0 ? -v : v; }

unsigned long long tnux_strtoull(const char *s, char **fim, int base, int *neg) {
    const char *p = s;
    unsigned long long v = 0;
    *neg = 0;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '+' || *p == '-') { *neg = (*p == '-'); p++; }
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && isxdigit((unsigned char)p[2])) {
        p += 2; base = 16;
    } else if (base == 0) base = (p[0] == '0') ? 8 : 10;
    const char *ini = p;
    for (;; p++) {
        int d;
        if (isdigit((unsigned char)*p)) d = *p - '0';
        else if (isalpha((unsigned char)*p)) d = tolower((unsigned char)*p) - 'a' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned)base + (unsigned)d;
    }
    if (fim) *fim = (char *)(p == ini ? s : p);
    return v;
}
long long strtoll(const char *s, char **fim, int base) {
    int neg; unsigned long long v = tnux_strtoull(s, fim, base, &neg);
    return neg ? -(long long)v : (long long)v;
}
long strtol(const char *s, char **fim, int base) { return (long)strtoll(s, fim, base); }
unsigned long strtoul(const char *s, char **fim, int base) {
    int neg; unsigned long long v = tnux_strtoull(s, fim, base, &neg);
    return (unsigned long)(neg ? -(long long)v : (long long)v);
}
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }

double strtod(const char *s, char **fim) {
    const char *p = s;
    double v = 0, sinal = 1;
    int achou = 0;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '+' || *p == '-') { if (*p == '-') sinal = -1; p++; }
    while (isdigit((unsigned char)*p)) { v = v * 10 + (*p++ - '0'); achou = 1; }
    if (*p == '.') {
        double f = 0.1;
        p++;
        while (isdigit((unsigned char)*p)) { v += (*p++ - '0') * f; f /= 10; achou = 1; }
    }
    if (achou && (*p == 'e' || *p == 'E')) {
        const char *q = p + 1;
        int es = 1, e = 0;
        if (*q == '+' || *q == '-') { if (*q == '-') es = -1; q++; }
        if (isdigit((unsigned char)*q)) {
            while (isdigit((unsigned char)*q)) e = e * 10 + (*q++ - '0');
            while (e--) v = es > 0 ? v * 10 : v / 10;
            p = q;
        }
    }
    if (fim) *fim = (char *)(achou ? p : s);
    return v * sinal;
}

/* ------------------------------------------------------------------ qsort (heapsort: sem recursão) */

static void troca(char *a, char *b, size_t n) {
    while (n--) { char t = *a; *a++ = *b; *b++ = t; }
}
static void desce(char *base, size_t ini, size_t fim, size_t tam, int (*cmp)(const void *, const void *)) {
    size_t raiz = ini;
    while (raiz * 2 + 1 <= fim) {
        size_t f = raiz * 2 + 1, t = raiz;
        if (cmp(base + t * tam, base + f * tam) < 0) t = f;
        if (f + 1 <= fim && cmp(base + t * tam, base + (f + 1) * tam) < 0) t = f + 1;
        if (t == raiz) return;
        troca(base + raiz * tam, base + t * tam, tam);
        raiz = t;
    }
}
void qsort(void *b, size_t n, size_t tam, int (*cmp)(const void *, const void *)) {
    char *base = b;
    if (n < 2) return;
    for (size_t i = (n - 2) / 2 + 1; i-- > 0;) desce(base, i, n - 1, tam, cmp);
    for (size_t fim = n - 1; fim > 0; fim--) {
        troca(base, base + fim * tam, tam);
        desce(base, 0, fim - 1, tam, cmp);
    }
}

/* ------------------------------------------------------------------ snprintf */

typedef struct { char *d; size_t n, pos; } Saida;
static void sc(Saida *s, char c) {
    if (s->pos + 1 < s->n) s->d[s->pos] = c;
    s->pos++;
}
static void campo(Saida *s, const char *txt, size_t len, int larg, int esq, char enche) {
    int falta = larg > (int)len ? larg - (int)len : 0;
    if (!esq) while (falta-- > 0) sc(s, enche);
    for (size_t i = 0; i < len; i++) sc(s, txt[i]);
    if (esq) while (falta-- > 0) sc(s, ' ');
}
static int fmt_u(char *fim, unsigned long long v, unsigned base, int maiusc) {
    const char *dg = maiusc ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    do { *--fim = dg[v % base]; v /= base; n++; } while (v);
    return n;
}

int vsnprintf(char *d, size_t n, const char *fmt, va_list ap) {
    Saida s = { d, n, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') { sc(&s, *fmt); continue; }
        fmt++;
        int esq = 0, zero = 0, mais = 0, espaco = 0, larg = 0, prec = -1, comp = 0;
        for (;; fmt++) {
            if (*fmt == '-') esq = 1;
            else if (*fmt == '0') zero = 1;
            else if (*fmt == '+') mais = 1;
            else if (*fmt == ' ') espaco = 1;
            else if (*fmt == '#') {}
            else break;
        }
        if (*fmt == '*') { larg = va_arg(ap, int); if (larg < 0) { esq = 1; larg = -larg; } fmt++; }
        else while (isdigit((unsigned char)*fmt)) larg = larg * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++; prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (isdigit((unsigned char)*fmt)) prec = prec * 10 + (*fmt++ - '0');
        }
        while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h' || *fmt == 'j' || *fmt == 't') {
            if (*fmt == 'l') comp++;
            if (*fmt == 'j') comp = 2;
            fmt++;
        }
        char tmp[80];
        char *fim = tmp + sizeof tmp;
        switch (*fmt) {
        case 'd': case 'i': {
            long long v = comp >= 2 ? va_arg(ap, long long) : comp == 1 ? va_arg(ap, long) : va_arg(ap, int);
            unsigned long long u = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
            int len = fmt_u(fim, u, 10, 0);
            fim -= len;
            while (len < prec) { *--fim = '0'; len++; }
            char sinal = v < 0 ? '-' : mais ? '+' : espaco ? ' ' : 0;
            if (sinal && zero && !esq && prec < 0) { sc(&s, sinal); larg--; sinal = 0; }
            if (sinal) { *--fim = sinal; len++; }
            campo(&s, fim, (size_t)len, larg, esq, (zero && !esq && prec < 0) ? '0' : ' ');
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            unsigned long long v = comp >= 2 ? va_arg(ap, unsigned long long) : comp == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            unsigned base = *fmt == 'u' ? 10 : *fmt == 'o' ? 8 : 16;
            int len = fmt_u(fim, v, base, *fmt == 'X');
            fim -= len;
            while (len < prec) { *--fim = '0'; len++; }
            campo(&s, fim, (size_t)len, larg, esq, (zero && !esq && prec < 0) ? '0' : ' ');
            break;
        }
        case 'p': {
            unsigned long v = (unsigned long)(size_t)va_arg(ap, void *);
            int len = fmt_u(fim, v, 16, 0);
            fim -= len;
            *--fim = 'x'; *--fim = '0'; len += 2;
            campo(&s, fim, (size_t)len, larg, esq, ' ');
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            campo(&s, &c, 1, larg, esq, ' ');
            break;
        }
        case 's': {
            const char *t = va_arg(ap, const char *);
            if (!t) t = "(null)";
            size_t len = strlen(t);
            if (prec >= 0 && (size_t)prec < len) len = (size_t)prec;
            campo(&s, t, len, larg, esq, ' ');
            break;
        }
        case 'f': case 'g': case 'e': {
            double v = va_arg(ap, double);
            if (prec < 0) prec = 6;
            if (prec > 15) prec = 15;
            int neg = v < 0;
            if (neg) v = -v;
            double arred = 0.5;
            for (int i = 0; i < prec; i++) arred /= 10;
            v += arred;
            unsigned long long inteiro = (unsigned long long)v;
            double frac = v - (double)inteiro;
            char fb[24]; int fl = 0;
            for (int i = 0; i < prec; i++) { frac *= 10; int dg = (int)frac; fb[fl++] = (char)('0' + dg); frac -= dg; }
            if (*fmt == 'g') { while (fl > 0 && fb[fl - 1] == '0') fl--; }
            for (int i = fl - 1; i >= 0; i--) *--fim = fb[i];
            if (fl) *--fim = '.';
            int len = fl + (fl ? 1 : 0);
            len += fmt_u(fim, inteiro, 10, 0);
            fim = tmp + sizeof tmp - len;
            if (neg) { *--fim = '-'; len++; }
            else if (mais) { *--fim = '+'; len++; }
            campo(&s, fim, (size_t)len, larg, esq, (zero && !esq) ? '0' : ' ');
            break;
        }
        case '%': sc(&s, '%'); break;
        case 0: fmt--; break;
        default: sc(&s, '%'); sc(&s, *fmt); break;
        }
    }
    if (s.n) s.d[s.pos < s.n ? s.pos : s.n - 1] = 0;
    return (int)s.pos;
}

int snprintf(char *d, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(d, n, fmt, ap);
    va_end(ap);
    return r;
}
