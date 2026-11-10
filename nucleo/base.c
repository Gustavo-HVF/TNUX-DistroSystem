/*
 * TNUX: utilitários do núcleo: buffer que cresce, JSON, glob e expressões regulares simples.
 */
#include "tnux.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ memória */

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { host_write(0, "\r\ntnux: sem memoria\r\n", 21); abort(); }
    return p;
}
void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) { host_write(0, "\r\ntnux: sem memoria\r\n", 21); abort(); }
    return q;
}
char *xstrdup(const char *s) {
    size_t n = strlen(s);
    char *r = xmalloc(n + 1);
    memcpy(r, s, n + 1);
    return r;
}
char *xasprintf(const char *fmt, ...) {
    Buf b; buf_init(&b);
    va_list ap;
    va_start(ap, fmt);
    buf_vprintf(&b, fmt, ap);
    va_end(ap);
    return buf_str(&b);
}

/* ------------------------------------------------------------------ Buf */

void buf_init(Buf *b) { b->d = NULL; b->n = b->cap = 0; }
void buf_free(Buf *b) { free(b->d); b->d = NULL; b->n = b->cap = 0; }
void buf_reserve(Buf *b, size_t mais) {
    if (b->n + mais + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap * 2 : 64;
    while (cap < b->n + mais + 1) cap *= 2;
    b->d = xrealloc(b->d, cap);
    b->cap = cap;
}
void buf_put(Buf *b, const void *p, size_t n) {
    buf_reserve(b, n);
    memcpy(b->d + b->n, p, n);
    b->n += n;
    b->d[b->n] = 0;
}
void buf_puts(Buf *b, const char *s) { buf_put(b, s, strlen(s)); }
void buf_putc(Buf *b, int c) { char ch = (char)c; buf_put(b, &ch, 1); }
void buf_vprintf(Buf *b, const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    char tmp[256];
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    if (n < (int)sizeof tmp) buf_put(b, tmp, (size_t)n);
    else {
        buf_reserve(b, (size_t)n + 1);
        vsnprintf(b->d + b->n, (size_t)n + 1, fmt, ap2);
        b->n += (size_t)n;
    }
    va_end(ap2);
}
void buf_printf(Buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    buf_vprintf(b, fmt, ap);
    va_end(ap);
}
char *buf_str(Buf *b) {
    buf_reserve(b, 0);
    b->d[b->n] = 0;
    return b->d;
}
void buf_json_str(Buf *b, const char *s) {
    buf_putc(b, '"');
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') { buf_putc(b, '\\'); buf_putc(b, ch); }
        else if (ch == '\n') buf_puts(b, "\\n");
        else if (ch == '\r') buf_puts(b, "\\r");
        else if (ch == '\t') buf_puts(b, "\\t");
        else if (ch < 32) buf_printf(b, "\\u%04x", ch);
        else buf_putc(b, ch);
    }
    buf_putc(b, '"');
}

/* ------------------------------------------------------------------ JSON */

typedef struct { const char *p, *fim; int erro; int nivel; } JLe;

static void j_espaco(JLe *l) {
    while (l->p < l->fim && isspace((unsigned char)*l->p)) l->p++;
}
static JVal *j_novo(JTipo t) {
    JVal *v = xmalloc(sizeof *v);
    memset(v, 0, sizeof *v);
    v->tipo = t;
    return v;
}
static void utf8_put(Buf *b, unsigned cp) {
    if (cp < 0x80) buf_putc(b, (int)cp);
    else if (cp < 0x800) { buf_putc(b, 0xC0 | (cp >> 6)); buf_putc(b, 0x80 | (cp & 63)); }
    else if (cp < 0x10000) { buf_putc(b, 0xE0 | (cp >> 12)); buf_putc(b, 0x80 | ((cp >> 6) & 63)); buf_putc(b, 0x80 | (cp & 63)); }
    else { buf_putc(b, 0xF0 | (cp >> 18)); buf_putc(b, 0x80 | ((cp >> 12) & 63)); buf_putc(b, 0x80 | ((cp >> 6) & 63)); buf_putc(b, 0x80 | (cp & 63)); }
}
static unsigned j_hex4(JLe *l) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        if (l->p >= l->fim || !isxdigit((unsigned char)*l->p)) { l->erro = 1; return 0; }
        int ch = tolower((unsigned char)*l->p++);
        v = v * 16 + (unsigned)(isdigit(ch) ? ch - '0' : ch - 'a' + 10);
    }
    return v;
}
static char *j_string(JLe *l) {
    Buf b; buf_init(&b);
    l->p++;   /* aspas */
    while (l->p < l->fim && *l->p != '"') {
        char ch = *l->p++;
        if (ch != '\\') { buf_putc(&b, ch); continue; }
        if (l->p >= l->fim) break;
        ch = *l->p++;
        switch (ch) {
        case 'n': buf_putc(&b, '\n'); break;
        case 't': buf_putc(&b, '\t'); break;
        case 'r': buf_putc(&b, '\r'); break;
        case 'b': buf_putc(&b, '\b'); break;
        case 'f': buf_putc(&b, '\f'); break;
        case 'u': {
            unsigned cp = j_hex4(l);
            if (cp >= 0xD800 && cp < 0xDC00 && l->p + 1 < l->fim && l->p[0] == '\\' && l->p[1] == 'u') {
                l->p += 2;
                unsigned lo = j_hex4(l);
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
            utf8_put(&b, cp);
            break;
        }
        default: buf_putc(&b, ch);
        }
    }
    if (l->p >= l->fim) l->erro = 1; else l->p++;
    return buf_str(&b);
}
static void j_acrescenta(JVal *v, char *chave, JVal *item) {
    if (v->n == 0 || (v->n >= 4 && (v->n & (v->n - 1)) == 0)) {   /* capacidade: 4, 8, 16... */
        int cap = v->n ? v->n * 2 : 4;
        v->itens = xrealloc(v->itens, sizeof(JVal *) * (size_t)cap);
        if (v->tipo == J_OBJ) v->chaves = xrealloc(v->chaves, sizeof(char *) * (size_t)cap);
    }
    v->itens[v->n] = item;
    if (v->tipo == J_OBJ) v->chaves[v->n] = chave;
    v->n++;
}
static JVal *j_valor(JLe *l) {
    j_espaco(l);
    if (l->p >= l->fim || l->nivel > 200) { l->erro = 1; return j_novo(J_NULO); }
    char ch = *l->p;
    if (ch == '"') { JVal *v = j_novo(J_STR); v->str = j_string(l); return v; }
    if (ch == '{' || ch == '[') {
        JVal *v = j_novo(ch == '{' ? J_OBJ : J_LISTA);
        char fecha = ch == '{' ? '}' : ']';
        l->p++; l->nivel++;
        j_espaco(l);
        if (l->p < l->fim && *l->p == fecha) { l->p++; l->nivel--; return v; }
        while (!l->erro) {
            char *chave = NULL;
            j_espaco(l);
            if (v->tipo == J_OBJ) {
                if (l->p >= l->fim || *l->p != '"') { l->erro = 1; break; }
                chave = j_string(l);
                j_espaco(l);
                if (l->p >= l->fim || *l->p != ':') { l->erro = 1; free(chave); break; }
                l->p++;
            }
            j_acrescenta(v, chave, j_valor(l));
            j_espaco(l);
            if (l->p < l->fim && *l->p == ',') { l->p++; continue; }
            if (l->p < l->fim && *l->p == fecha) { l->p++; break; }
            l->erro = 1;
        }
        l->nivel--;
        return v;
    }
    if (ch == 't' && l->fim - l->p >= 4 && !memcmp(l->p, "true", 4)) { l->p += 4; JVal *v = j_novo(J_BOOL); v->num = 1; return v; }
    if (ch == 'f' && l->fim - l->p >= 5 && !memcmp(l->p, "false", 5)) { l->p += 5; return j_novo(J_BOOL); }
    if (ch == 'n' && l->fim - l->p >= 4 && !memcmp(l->p, "null", 4)) { l->p += 4; return j_novo(J_NULO); }
    if (ch == '-' || isdigit((unsigned char)ch)) {
        char tmp[48]; size_t n = 0;
        while (l->p < l->fim && n < sizeof tmp - 1 && (isdigit((unsigned char)*l->p) || strchr("+-.eE", *l->p))) tmp[n++] = *l->p++;
        tmp[n] = 0;
        JVal *v = j_novo(J_NUM);
        v->num = strtod(tmp, NULL);
        return v;
    }
    l->erro = 1;
    return j_novo(J_NULO);
}
JVal *json_ler(const char *txt, size_t n) {
    JLe l = { txt, txt + n, 0, 0 };
    JVal *v = j_valor(&l);
    if (l.erro) { json_livrar(v); return NULL; }
    return v;
}
void json_livrar(JVal *v) {
    if (!v) return;
    for (int i = 0; i < v->n; i++) {
        json_livrar(v->itens[i]);
        if (v->chaves) free(v->chaves[i]);
    }
    free(v->itens); free(v->chaves); free(v->str); free(v);
}
JVal *json_obter(const JVal *obj, const char *chave) {
    if (!obj || obj->tipo != J_OBJ) return NULL;
    for (int i = 0; i < obj->n; i++) if (!strcmp(obj->chaves[i], chave)) return obj->itens[i];
    return NULL;
}
const char *json_texto(const JVal *obj, const char *chave, const char *padrao) {
    JVal *v = json_obter(obj, chave);
    return v && v->tipo == J_STR ? v->str : padrao;
}
double json_numero(const JVal *obj, const char *chave, double padrao) {
    JVal *v = json_obter(obj, chave);
    return v && (v->tipo == J_NUM || v->tipo == J_BOOL) ? v->num : padrao;
}

/* ------------------------------------------------------------------ glob: * ? [abc] [a-z] [!abc] */

int casa_glob(const char *p, const char *t) {
    for (; *p; p++, t++) {
        if (*p == '*') {
            while (*p == '*') p++;
            if (!*p) return 1;
            for (; *t; t++) if (casa_glob(p, t)) return 1;
            return casa_glob(p, t);
        }
        if (!*t) return 0;
        if (*p == '?') continue;
        if (*p == '[') {
            const char *q = p + 1;
            int nega = (*q == '!' || *q == '^'), ok = 0;
            if (nega) q++;
            const char *ini = q;
            for (; *q && (*q != ']' || q == ini); q++) {
                if (q[1] == '-' && q[2] && q[2] != ']') {
                    if ((unsigned char)*t >= (unsigned char)q[0] && (unsigned char)*t <= (unsigned char)q[2]) ok = 1;
                    q += 2;
                } else if (*q == *t) ok = 1;
            }
            if (!*q) { if (*p != *t) return 0; continue; }   /* '[' sem ']': literal */
            if (ok == nega) return 0;
            p = q;
            continue;
        }
        if (*p == '\\' && p[1]) p++;
        if (*p != *t) return 0;
    }
    return !*t;
}

/* ------------------------------------------------------------------ expressões regulares simples
 * Casador com retrocesso, no estilo do de Kernighan e Pike, com classes e os quantificadores
 * * + ?. Alternância (|) só no nível de cima. É o bastante para grep e sed do dia a dia.
 */

typedef struct { int ic; const char *ini_txt; } ReCtx;

/* tamanho do átomo que começa em re (um caractere, um escape ou uma classe) */
static int re_atomo(const char *re) {
    if (*re == '\\' && re[1]) return 2;
    if (*re == '[') {
        const char *q = re + 1;
        if (*q == '^') q++;
        if (*q == ']') q++;
        while (*q && *q != ']') q++;
        return *q ? (int)(q - re) + 1 : 1;
    }
    return 1;
}
static int re_casa1(ReCtx *x, const char *re, int tam, int ch) {
    if (!ch) return 0;
    if (tam == 1) {
        if (*re == '.') return 1;
        return x->ic ? tolower((unsigned char)*re) == tolower(ch) : (unsigned char)*re == ch;
    }
    if (*re == '\\') {
        switch (re[1]) {
        case 'd': return isdigit(ch);
        case 'w': return isalnum(ch) || ch == '_';
        case 's': return isspace(ch);
        case 'D': return !isdigit(ch);
        case 'W': return !(isalnum(ch) || ch == '_');
        case 'S': return !isspace(ch);
        case 'n': return ch == '\n';
        case 't': return ch == '\t';
        default: return x->ic ? tolower((unsigned char)re[1]) == tolower(ch) : (unsigned char)re[1] == ch;
        }
    }
    /* classe */
    const char *q = re + 1, *fim = re + tam - 1;
    int nega = 0, ok = 0;
    if (*q == '^') { nega = 1; q++; }
    int c2 = x->ic ? tolower(ch) : ch;
    for (; q < fim; q++) {
        int a = (unsigned char)*q;
        if (q + 2 < fim && q[1] == '-') {
            int b = (unsigned char)q[2];
            if (x->ic) { a = tolower(a); b = tolower(b); }
            if (c2 >= a && c2 <= b) ok = 1;
            q += 2;
        } else {
            if (x->ic) a = tolower(a);
            if (c2 == a) ok = 1;
        }
    }
    return ok != nega;
}
static const char *re_aqui(ReCtx *x, const char *re, const char *fim_re, const char *t) {
    if (re >= fim_re) return t;
    if (*re == '$' && re + 1 == fim_re) return (*t == 0 || *t == '\n') ? t : NULL;
    int tam = re_atomo(re);
    char q = re + tam < fim_re ? re[tam] : 0;
    if (q == '*' || q == '+' || q == '?') {
        const char *resto = re + tam + 1;
        int min = q == '+' ? 1 : 0, max = q == '?' ? 1 : 0x7fffffff, n = 0;
        const char *p = t;
        while (n < max && re_casa1(x, re, tam, (unsigned char)*p)) { p++; n++; }
        for (; n >= min; n--, p--) {      /* guloso: tenta do maior para o menor */
            const char *r = re_aqui(x, resto, fim_re, p);
            if (r) return r;
        }
        return NULL;
    }
    if (re_casa1(x, re, tam, (unsigned char)*t)) return re_aqui(x, re + tam, fim_re, t + 1);
    return NULL;
}
static int re_ramo(ReCtx *x, const char *re, const char *fim_re, const char *texto, int *ini, int *fim) {
    if (re < fim_re && *re == '^') {
        const char *r = re_aqui(x, re + 1, fim_re, texto);
        if (!r) return 0;
        *ini = 0; *fim = (int)(r - texto);
        return 1;
    }
    for (const char *t = texto;; t++) {
        const char *r = re_aqui(x, re, fim_re, t);
        if (r) { *ini = (int)(t - texto); *fim = (int)(r - texto); return 1; }
        if (!*t) return 0;
    }
}
int re_buscar(const char *re, const char *texto, int ic, int *ini, int *fim) {
    ReCtx x = { ic, texto };
    int melhor = 0, bi = 0, bf = 0;
    const char *p = re;
    for (;;) {
        const char *q = p;
        while (*q && *q != '|') {
            if (*q == '\\' && q[1]) q += 2;
            else if (*q == '[') q += re_atomo(q);
            else q++;
        }
        int i, f;
        if (re_ramo(&x, p, q, texto, &i, &f) && (!melhor || i < bi)) { melhor = 1; bi = i; bf = f; }
        if (!*q) break;
        p = q + 1;
    }
    if (melhor) { if (ini) *ini = bi; if (fim) *fim = bf; }
    return melhor;
}
