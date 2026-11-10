/*
 * TNUX: comandos de texto (echo, printf, grep, sed, sort, uniq, cut, tr, head, tail, wc, awk...).
 */
#include "cmds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* interpreta \n \t \e \xHH \0NNN; devolve o novo tamanho. Para em \c (devolve -1 em *parar). */
static size_t escapes(char *s, int *parar) {
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p != '\\' || !p[1]) { *o++ = *p; continue; }
        p++;
        switch (*p) {
        case 'n': *o++ = '\n'; break;
        case 't': *o++ = '\t'; break;
        case 'r': *o++ = '\r'; break;
        case 'a': *o++ = '\a'; break;
        case 'b': *o++ = '\b'; break;
        case 'e': case 'E': *o++ = 0x1b; break;
        case '\\': *o++ = '\\'; break;
        case 'c': if (parar) *parar = 1; *o = 0; return (size_t)(o - s);
        case 'x': {
            int v = 0, n = 0;
            while (n < 2 && isxdigit((unsigned char)p[1])) { int d = tolower((unsigned char)p[1]); v = v * 16 + (isdigit(d) ? d - '0' : d - 'a' + 10); p++; n++; }
            *o++ = (char)v;
            break;
        }
        case '0': case '1': case '2': case '3': {
            int v = 0, n = 0;
            if (*p == '0' && p[1] >= '0' && p[1] <= '7') p++;
            while (n < 3 && *p >= '0' && *p <= '7') { v = v * 8 + (*p - '0'); p++; n++; }
            p--;
            *o++ = (char)v;
            break;
        }
        default: *o++ = '\\'; *o++ = *p;
        }
    }
    *o = 0;
    return (size_t)(o - s);
}

static int c_echo(Ctx *c, int argc, char **argv) {
    int i = 1, nl = 1, esc = 0;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "-n")) nl = 0;
        else if (!strcmp(argv[i], "-e")) esc = 1;
        else if (!strcmp(argv[i], "-E")) esc = 0;
        else if (!strcmp(argv[i], "-ne") || !strcmp(argv[i], "-en")) { nl = 0; esc = 1; }
        else break;
    }
    Buf b; buf_init(&b);
    for (int k = i; k < argc; k++) {
        if (k > i) buf_putc(&b, ' ');
        if (esc) {
            char *t = xstrdup(argv[k]);
            int parar = 0;
            size_t n = escapes(t, &parar);
            buf_put(&b, t, n);
            free(t);
            if (parar) { nl = 0; break; }
        } else buf_puts(&b, argv[k]);
    }
    if (nl) buf_putc(&b, '\n');
    o_write(c->out, b.d, b.n);
    buf_free(&b);
    return 0;
}

static int c_printf(Ctx *c, int argc, char **argv) {
    if (argc < 2) { ERR("printf: uso: printf formato [argumentos]\n"); return 2; }
    int ia = 2;
    Buf b; buf_init(&b);
    do {
        int usou = 0;
        for (const char *p = argv[1]; *p; p++) {
            if (*p == '\\') {
                char t[8] = { '\\', p[1], 0, 0, 0, 0, 0, 0 };
                int k = 2;
                if (p[1] == 'x' || (p[1] >= '0' && p[1] <= '7')) { while (k < 5 && isxdigit((unsigned char)p[k])) { t[k] = p[k]; k++; } }
                if (!p[1]) { buf_putc(&b, '\\'); break; }
                size_t n = escapes(t, NULL);
                buf_put(&b, t, n);
                p += k - 1;
                continue;
            }
            if (*p != '%') { buf_putc(&b, *p); continue; }
            if (p[1] == '%') { buf_putc(&b, '%'); p++; continue; }
            char fmt[32]; size_t fn = 0;
            fmt[fn++] = '%';
            p++;
            while (*p && strchr("-+ 0#", *p) && fn < 20) fmt[fn++] = *p++;
            while ((isdigit((unsigned char)*p) || *p == '.') && fn < 24) fmt[fn++] = *p++;
            const char *arg = ia < argc ? argv[ia] : NULL;
            if (!*p) break;
            if (strchr("diouxXcsfgeb", *p)) { if (arg) { ia++; usou = 1; } }
            switch (*p) {
            case 'd': case 'i': fmt[fn++] = 'l'; fmt[fn++] = 'l'; fmt[fn++] = 'd'; fmt[fn] = 0; buf_printf(&b, fmt, arg ? strtoll(arg, NULL, 0) : 0LL); break;
            case 'u': case 'o': case 'x': case 'X': fmt[fn++] = 'l'; fmt[fn++] = 'l'; fmt[fn++] = *p; fmt[fn] = 0; buf_printf(&b, fmt, (unsigned long long)(arg ? strtoll(arg, NULL, 0) : 0)); break;
            case 'f': case 'g': case 'e': fmt[fn++] = *p; fmt[fn] = 0; buf_printf(&b, fmt, arg ? strtod(arg, NULL) : 0.0); break;
            case 'c': buf_putc(&b, arg && *arg ? *arg : 0); if (!(arg && *arg)) b.n--; break;
            case 's': fmt[fn++] = 's'; fmt[fn] = 0; buf_printf(&b, fmt, arg ? arg : ""); break;
            case 'b': { char *t = xstrdup(arg ? arg : ""); size_t n = escapes(t, NULL); buf_put(&b, t, n); free(t); break; }
            default: buf_putc(&b, '%'); buf_putc(&b, *p);
            }
        }
        if (!usou) break;
    } while (ia < argc);
    o_write(c->out, b.d, b.n);
    buf_free(&b);
    return 0;
}

/* ------------------------------------------------------------------ head, tail, wc, tac, rev, nl */

static int c_head(Ctx *c, int argc, char **argv) {
    long n = 10; int bytes = 0, i = 1, r = 0, tail = !strcmp(argv[0], "tail"), desde = 0;
    for (; i < argc; i++) {
        if (argv[i][0] != '-' || !argv[i][1]) break;
        if (!strcmp(argv[i], "-n") && i + 1 < argc) { i++; if (argv[i][0] == '+') desde = 1; n = atol(argv[i]); }
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) { bytes = 1; n = atol(argv[++i]); }
        else if (!strncmp(argv[i], "-n", 2) && argv[i][2]) { if (argv[i][2] == '+') desde = 1; n = atol(argv[i] + 2); }
        else if (isdigit((unsigned char)argv[i][1])) n = atol(argv[i] + 1);
        else if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "-q")) {}
        else { ERR("%s: opção inválida -- '%s'\n", argv[0], argv[i] + 1); return 1; }
    }
    if (n < 0) n = -n;
    int varios = argc - i > 1, k = i;
    do {
        const char *d; size_t tam;
        if (ler_arq(c, argv[0], k < argc ? argv[k] : NULL, &d, &tam)) { r = 1; continue; }
        if (varios) OUT("%s==> %s <==\n", k > i ? "\n" : "", argv[k]);
        if (bytes) {
            size_t q = (size_t)n < tam ? (size_t)n : tam;
            o_write(c->out, tail ? d + tam - q : d, q);
            continue;
        }
        if (!tail) {
            const char *p = d, *fim = d + tam;
            for (long l = 0; l < n && p < fim; l++) { const char *nl = memchr(p, '\n', (size_t)(fim - p)); p = nl ? nl + 1 : fim; }
            o_write(c->out, d, (size_t)(p - d));
        } else if (desde) {
            const char *p = d, *fim = d + tam;
            for (long l = 1; l < n && p < fim; l++) { const char *nl = memchr(p, '\n', (size_t)(fim - p)); p = nl ? nl + 1 : fim; }
            o_write(c->out, p, (size_t)(fim - p));
        } else {
            const char *p = d + tam;
            if (p > d && p[-1] == '\n') p--;
            long l = 0;
            while (p > d) { if (p[-1] == '\n' && ++l >= n) break; p--; }
            if (n > 0) o_write(c->out, p, (size_t)(d + tam - p));
        }
    } while (++k < argc);
    return r;
}
static int c_wc(Ctx *c, int argc, char **argv) {
    int ol = 0, ow = 0, oc = 0, i = 1, r = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++)
        for (const char *p = argv[i] + 1; *p; p++) { if (*p == 'l') ol = 1; else if (*p == 'w') ow = 1; else if (*p == 'c' || *p == 'm') oc = 1; }
    if (!ol && !ow && !oc) ol = ow = oc = 1;
    unsigned long tl = 0, tw = 0, tc = 0;
    int k = i, n = 0;
    do {
        const char *d; size_t tam;
        if (ler_arq(c, "wc", k < argc ? argv[k] : NULL, &d, &tam)) { r = 1; continue; }
        unsigned long l = 0, w = 0; int dentro = 0;
        for (size_t x = 0; x < tam; x++) {
            if (d[x] == '\n') l++;
            if (isspace((unsigned char)d[x])) dentro = 0; else if (!dentro) { dentro = 1; w++; }
        }
        int so_um = (ol + ow + oc) == 1;
        if (ol) OUT(so_um ? "%lu" : "%7lu", l);
        if (ow) OUT(so_um ? "%lu" : " %7lu", w);
        if (oc) OUT(so_um ? "%lu" : " %7lu", (unsigned long)tam);
        if (k < argc) OUT(" %s", argv[k]);
        OUT("\n");
        tl += l; tw += w; tc += tam; n++;
    } while (++k < argc);
    if (n > 1) {
        if (ol) OUT("%7lu", tl);
        if (ow) OUT(" %7lu", tw);
        if (oc) OUT(" %7lu", tc);
        OUT(" total\n");
    }
    return r;
}
static int c_tac(Ctx *c, int argc, char **argv) {
    const char *d; size_t n;
    int rev = !strcmp(argv[0], "rev");
    if (ler_arq(c, argv[0], argc > 1 ? argv[1] : NULL, &d, &n)) return 1;
    if (rev) {
        const char *p = d, *fim = d + n, *l; size_t tl;
        while ((l = prox_linha(&p, fim, &tl))) {
            /* inverte por caractere UTF-8 */
            Buf b; buf_init(&b);
            size_t i = tl;
            while (i > 0) {
                size_t j = i - 1;
                while (j > 0 && ((unsigned char)l[j] & 0xC0) == 0x80) j--;
                buf_put(&b, l + j, i - j);
                i = j;
            }
            buf_putc(&b, '\n');
            o_write(c->out, b.d, b.n);
            buf_free(&b);
        }
        return 0;
    }
    const char *fim = d + n;
    if (fim > d && fim[-1] == '\n') fim--;
    while (fim > d) {
        const char *p = fim;
        while (p > d && p[-1] != '\n') p--;
        o_write(c->out, p, (size_t)(fim - p));
        OUT("\n");
        fim = p > d ? p - 1 : d;
        if (p == d) break;
    }
    return 0;
}
static int c_nl(Ctx *c, int argc, char **argv) {
    const char *d; size_t n;
    if (ler_arq(c, "nl", argc > 1 ? argv[argc - 1] : NULL, &d, &n)) return 1;
    const char *p = d, *fim = d + n, *l; size_t tl; int num = 1;
    while ((l = prox_linha(&p, fim, &tl))) {
        if (tl) OUT("%6d\t", num++); else OUT("       ");
        o_write(c->out, l, tl);
        OUT("\n");
    }
    return 0;
}

/* ------------------------------------------------------------------ grep */

typedef struct { int i, v, n, cnt, l, q, o, F, w, cor, H; const char *re; int achou; } GrepOp;

static int grep_casa(GrepOp *g, const char *linha, int *ini, int *fim) {
    if (g->F) {
        size_t lr = strlen(g->re);
        for (const char *p = linha; *p || lr == 0; p++) {
            if ((g->i ? strncasecmp(p, g->re, lr) : strncmp(p, g->re, lr)) == 0) { *ini = (int)(p - linha); *fim = *ini + (int)lr; return 1; }
            if (!*p) break;
        }
        return 0;
    }
    int desloc = 0;
    while (re_buscar(g->re, linha + desloc, g->i, ini, fim)) {
        *ini += desloc; *fim += desloc;
        if (!g->w) return 1;
        int antes = *ini == 0 || !(isalnum((unsigned char)linha[*ini - 1]) || linha[*ini - 1] == '_');
        int depois = !(isalnum((unsigned char)linha[*fim]) || linha[*fim] == '_');
        if (antes && depois) return 1;
        if (!linha[*ini]) return 0;
        desloc = *ini + 1;
    }
    return 0;
}
static void grep_texto(Ctx *c, GrepOp *g, const char *d, size_t tam, const char *nome) {
    const char *p = d, *fim = d + tam, *l; size_t tl;
    int num = 0; long conta = 0;
    while ((l = prox_linha(&p, fim, &tl))) {
        num++;
        char *linha = xmalloc(tl + 1);
        memcpy(linha, l, tl); linha[tl] = 0;
        int ini = 0, f = 0;
        int casa = grep_casa(g, linha, &ini, &f);
        if (casa != g->v) {
            conta++; g->achou = 1;
            if (!g->cnt && !g->l && !g->q) {
                if (g->H) OUT(g->cor ? "\x1b[35m%s\x1b[36m:\x1b[0m" : "%s:", nome);
                if (g->n) OUT(g->cor ? "\x1b[32m%d\x1b[36m:\x1b[0m" : "%d:", num);
                if (g->o && !g->v) { o_write(c->out, linha + ini, (size_t)(f - ini)); OUT("\n"); }
                else if (g->cor && !g->v && f > ini) {
                    /* pinta todas as ocorrências da linha */
                    const char *resto = linha;
                    int i2 = ini, f2 = f;
                    for (;;) {
                        o_write(c->out, resto, (size_t)i2);
                        OUT("\x1b[01;31m%.*s\x1b[0m", f2 - i2, resto + i2);
                        resto += f2;
                        if (!*resto || f2 == 0 || !grep_casa(g, resto, &i2, &f2) || f2 <= i2) break;
                    }
                    OUT("%s\n", resto);
                } else { o_write(c->out, linha, tl); OUT("\n"); }
            }
        }
        free(linha);
        if (g->q && g->achou) break;
    }
    if (g->cnt) { if (g->H) OUT("%s:", nome); OUT("%ld\n", conta); }
    if (g->l && conta) OUT("%s\n", nome);
}
static void grep_rec(Ctx *c, GrepOp *g, Inode *n, const char *cam) {
    if (EH_DIR(n)) {
        vfs_ordenar(n);
        for (int i = 0; i < n->nfilhos; i++) {
            char *sub = xasprintf("%s%s%s", cam, cam[strlen(cam) - 1] == '/' ? "" : "/", n->filhos[i]->nome);
            grep_rec(c, g, n->filhos[i], sub);
            free(sub);
        }
    } else if (EH_REG(n) && n->dados && !memchr(n->dados, 0, n->tam < 1024 ? n->tam : 1024))
        grep_texto(c, g, n->dados, n->tam, cam);
}
static int c_grep(Ctx *c, int argc, char **argv) {
    GrepOp g; memset(&g, 0, sizeof g);
    int i = 1, rec = 0, r = 0, semH = 0;
    if (!strcmp(argv[0], "fgrep")) g.F = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-' || !argv[i][1]) break;
        if (!strcmp(argv[i], "--")) { i++; break; }
        if (!strcmp(argv[i], "-e") && i + 1 < argc) { g.re = argv[++i]; continue; }
        if (!strncmp(argv[i], "--color", 7)) { g.cor = 1; continue; }
        if (argv[i][1] == '-') continue;
        for (const char *p = argv[i] + 1; *p; p++) {
            switch (*p) {
            case 'i': g.i = 1; break; case 'v': g.v = 1; break; case 'n': g.n = 1; break; case 'c': g.cnt = 1; break;
            case 'l': g.l = 1; break; case 'q': g.q = 1; break; case 'o': g.o = 1; break; case 'F': g.F = 1; break;
            case 'w': g.w = 1; break; case 'r': case 'R': rec = 1; break; case 'H': g.H = 1; break; case 'h': semH = 1; break;
            case 'E': case 'P': case 's': case 'a': case 'I': break;
            default: ERR("grep: opção inválida -- '%c'\n", *p); return 2;
            }
        }
    }
    if (!g.re) {
        if (i >= argc) { ERR("Uso: grep [OPÇÃO]... PADRÃO [ARQUIVO]...\n"); return 2; }
        g.re = argv[i++];
    }
    if (o_tty(c->out)) g.cor = 1;
    if (argc - i > 1 || rec) g.H = !semH;
    if (i >= argc && !rec) {
        grep_texto(c, &g, c->in ? c->in : "", c->in ? c->inlen : 0, "(entrada padrão)");
    } else if (i >= argc) {
        Inode *n = vfs_achar(c->sh->cwd, ".", 1);
        if (n) grep_rec(c, &g, n, ".");
    } else for (; i < argc; i++) {
        Inode *n = vfs_achar(c->sh->cwd, argv[i], 1);
        if (!n) { ERR("grep: %s: %s\n", argv[i], vfs_erro(vfs_err)); r = 2; continue; }
        if (EH_DIR(n)) { if (rec) grep_rec(c, &g, n, argv[i]); else ERR("grep: %s: É um diretório\n", argv[i]); continue; }
        grep_texto(c, &g, n->dados ? n->dados : "", n->tam, argv[i]);
    }
    return r ? r : (g.achou ? 0 : 1);
}

/* ------------------------------------------------------------------ sort, uniq */

static int so_num, so_rev, so_ic, so_campo; static char so_sep;
static const char *campo_de(const char *s, int k, char sep) {
    for (int i = 1; i < k && *s; i++) {
        if (sep) { const char *f = strchr(s, sep); if (!f) return ""; s = f + 1; }
        else { while (*s && !isspace((unsigned char)*s)) s++; while (isspace((unsigned char)*s)) s++; }
    }
    if (!sep) while (isspace((unsigned char)*s)) s++;
    return s;
}
static int sort_cmp(const void *x, const void *y) {
    const char *a = *(char *const *)x, *b = *(char *const *)y;
    if (so_campo > 0) { a = campo_de(a, so_campo, so_sep); b = campo_de(b, so_campo, so_sep); }
    int v;
    if (so_num) { double da = strtod(a, NULL), db = strtod(b, NULL); v = da < db ? -1 : da > db ? 1 : 0; }
    else v = so_ic ? strcasecmp(a, b) : strcmp(a, b);
    if (v == 0) v = strcmp(*(char *const *)x, *(char *const *)y);
    return so_rev ? -v : v;
}
static int c_sort(Ctx *c, int argc, char **argv) {
    int i = 1, unico = 0;
    so_num = so_rev = so_ic = so_campo = 0; so_sep = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "-k") && i + 1 < argc) { so_campo = atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "-t") && i + 1 < argc) { so_sep = argv[++i][0]; continue; }
        for (const char *p = argv[i] + 1; *p; p++) {
            if (*p == 'n' || *p == 'g' || *p == 'h') so_num = 1;
            else if (*p == 'r') so_rev = 1;
            else if (*p == 'u') unico = 1;
            else if (*p == 'f') so_ic = 1;
            else if (*p == 'k') { so_campo = atoi(p + 1); break; }
            else if (*p == 't') { so_sep = p[1]; break; }
        }
    }
    Buf tudo; buf_init(&tudo);
    int k = i;
    do {
        const char *d; size_t n;
        if (ler_arq(c, "sort", k < argc ? argv[k] : NULL, &d, &n)) { buf_free(&tudo); return 2; }
        buf_put(&tudo, d, n);
        if (n && d[n - 1] != '\n') buf_putc(&tudo, '\n');
    } while (++k < argc);
    int nl = 0, cap = 64;
    char **linhas = xmalloc(sizeof(char *) * (size_t)cap);
    char *p = buf_str(&tudo);
    while (*p) {
        char *f = strchr(p, '\n');
        if (f) *f = 0;
        if (nl == cap) { cap *= 2; linhas = xrealloc(linhas, sizeof(char *) * (size_t)cap); }
        linhas[nl++] = p;
        if (!f) break;
        p = f + 1;
    }
    qsort(linhas, (size_t)nl, sizeof(char *), sort_cmp);
    for (int x = 0; x < nl; x++) {
        if (unico && x > 0 && !strcmp(linhas[x], linhas[x - 1])) continue;
        OUT("%s\n", linhas[x]);
    }
    free(linhas);
    buf_free(&tudo);
    return 0;
}
static int c_uniq(Ctx *c, int argc, char **argv) {
    int conta = 0, so_dup = 0, so_uni = 0, i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++)
        for (const char *p = argv[i] + 1; *p; p++) { if (*p == 'c') conta = 1; else if (*p == 'd') so_dup = 1; else if (*p == 'u') so_uni = 1; }
    const char *d; size_t n;
    if (ler_arq(c, "uniq", i < argc ? argv[i] : NULL, &d, &n)) return 1;
    const char *p = d, *fim = d + n, *l, *ant = NULL; size_t tl, tant = 0; long rep = 0;
    for (;;) {
        l = prox_linha(&p, fim, &tl);
        if (l && ant && tl == tant && !memcmp(l, ant, tl)) { rep++; continue; }
        if (ant && !(so_dup && rep < 2) && !(so_uni && rep > 1)) {
            if (conta) OUT("%7ld ", rep);
            o_write(c->out, ant, tant);
            OUT("\n");
        }
        if (!l) break;
        ant = l; tant = tl; rep = 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ cut, tr */

/* lista do tipo 1,3-5,7- : devolve 1 se k está nela */
static int na_lista(const char *lista, long k) {
    for (const char *p = lista; *p;) {
        long a = 1, b;
        if (*p == '-') { b = strtol(p + 1, (char **)&p, 10); }
        else {
            a = strtol(p, (char **)&p, 10);
            if (*p == '-') { p++; b = isdigit((unsigned char)*p) ? strtol(p, (char **)&p, 10) : 0x7fffffff; }
            else b = a;
        }
        if (k >= a && k <= b) return 1;
        if (*p == ',') p++; else if (*p) break;
    }
    return 0;
}
static int c_cut(Ctx *c, int argc, char **argv) {
    const char *campos = NULL, *chars = NULL; char sep = '\t'; int i = 1, so_sep = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char o = argv[i][1];
        const char *val = argv[i][2] ? argv[i] + 2 : (i + 1 < argc ? argv[++i] : "");
        if (o == 'd') sep = val[0];
        else if (o == 'f') campos = val;
        else if (o == 'c' || o == 'b') chars = val;
        else if (o == 's') { so_sep = 1; if (!argv[i][2]) i--; }
    }
    if (!campos && !chars) { ERR("cut: você deve especificar uma lista de bytes, caracteres ou campos\n"); return 1; }
    const char *d; size_t n;
    if (ler_arq(c, "cut", i < argc ? argv[i] : NULL, &d, &n)) return 1;
    const char *p = d, *fim = d + n, *l; size_t tl;
    while ((l = prox_linha(&p, fim, &tl))) {
        Buf b; buf_init(&b);
        if (chars) {
            long k = 0;
            for (size_t x = 0; x < tl; x++) {
                if (((unsigned char)l[x] & 0xC0) != 0x80) k++;
                if (na_lista(chars, k)) buf_putc(&b, l[x]);
            }
        } else if (!memchr(l, sep, tl)) {
            if (so_sep) { buf_free(&b); continue; }
            buf_put(&b, l, tl);
        } else {
            long k = 1; int prim = 1;
            size_t ini = 0;
            for (size_t x = 0; x <= tl; x++) {
                if (x == tl || l[x] == sep) {
                    if (na_lista(campos, k)) { if (!prim) buf_putc(&b, sep); buf_put(&b, l + ini, x - ini); prim = 0; }
                    k++; ini = x + 1;
                }
            }
        }
        buf_putc(&b, '\n');
        o_write(c->out, b.d, b.n);
        buf_free(&b);
    }
    return 0;
}
/* expande um conjunto do tr (a-z, [:upper:], \n) em bytes */
static int tr_conj(const char *s, unsigned char *out) {
    int n = 0;
    char *t = xstrdup(s);
    escapes(t, NULL);
    for (const char *p = t; *p && n < 512; p++) {
        if (p[0] == '[' && p[1] == ':') {
            const char *f = strstr(p, ":]");
            if (f) {
                char cl[16]; size_t l = (size_t)(f - p - 2);
                if (l < sizeof cl) {
                    memcpy(cl, p + 2, l); cl[l] = 0;
                    for (int ch = 1; ch < 128 && n < 512; ch++) {
                        int ok = (!strcmp(cl, "upper") && isupper(ch)) || (!strcmp(cl, "lower") && islower(ch)) || (!strcmp(cl, "digit") && isdigit(ch)) ||
                                 (!strcmp(cl, "alpha") && isalpha(ch)) || (!strcmp(cl, "alnum") && isalnum(ch)) || (!strcmp(cl, "space") && isspace(ch)) ||
                                 (!strcmp(cl, "punct") && ispunct(ch));
                        if (ok) out[n++] = (unsigned char)ch;
                    }
                }
                p = f + 1;
                continue;
            }
        }
        if (p[1] == '-' && p[2]) {
            for (int ch = (unsigned char)p[0]; ch <= (unsigned char)p[2] && n < 512; ch++) out[n++] = (unsigned char)ch;
            p += 2;
            continue;
        }
        out[n++] = (unsigned char)*p;
    }
    free(t);
    return n;
}
static int c_tr(Ctx *c, int argc, char **argv) {
    int apaga = 0, espreme = 0, i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] && !argv[i][2 + (argv[i][2] != 0)]; i++) {
        for (const char *p = argv[i] + 1; *p; p++) { if (*p == 'd') apaga = 1; else if (*p == 's') espreme = 1; }
    }
    if (i >= argc) { ERR("tr: falta operando\n"); return 1; }
    unsigned char a[512], b[512];
    int na = tr_conj(argv[i], a), nb = i + 1 < argc ? tr_conj(argv[i + 1], b) : 0;
    int mapa[256], del[256], esp[256];
    for (int k = 0; k < 256; k++) { mapa[k] = k; del[k] = 0; esp[k] = 0; }
    if (apaga) for (int k = 0; k < na; k++) del[a[k]] = 1;
    else if (nb) for (int k = 0; k < na; k++) mapa[a[k]] = b[k < nb ? k : nb - 1];
    if (espreme) { if (nb && !apaga) for (int k = 0; k < nb; k++) esp[b[k]] = 1; else for (int k = 0; k < na; k++) esp[a[k]] = 1; }
    Buf o; buf_init(&o);
    int ult = -1;
    for (size_t k = 0; k < c->inlen; k++) {
        unsigned char ch = (unsigned char)c->in[k];
        if (del[ch]) continue;
        int m = mapa[ch];
        if (esp[m] && m == ult) continue;
        buf_putc(&o, m);
        ult = m;
    }
    o_write(c->out, o.d, o.n);
    buf_free(&o);
    return 0;
}

/* ------------------------------------------------------------------ sed (o essencial) */

typedef struct { int ini, fim; char *re_end; char cmd; char *re, *sub; int g, ic, p; } SedCmd;

/* lê um trecho até o delimitador, respeitando \delim */
static char *sed_trecho(const char **pp, char delim) {
    Buf b; buf_init(&b);
    const char *p = *pp;
    while (*p && *p != delim) {
        if (*p == '\\' && p[1] == delim) { buf_putc(&b, delim); p += 2; continue; }
        if (*p == '\\' && p[1] == 'n') { buf_putc(&b, '\n'); p += 2; continue; }
        if (*p == '\\' && p[1]) { buf_putc(&b, *p++); }
        buf_putc(&b, *p++);
    }
    if (*p == delim) p++;
    *pp = p;
    return buf_str(&b);
}
static int sed_parse(const char *s, SedCmd *k) {
    memset(k, 0, sizeof *k);
    k->ini = k->fim = -1;
    while (isspace((unsigned char)*s)) s++;
    if (isdigit((unsigned char)*s)) { k->ini = k->fim = (int)strtol(s, (char **)&s, 10); }
    else if (*s == '$') { k->ini = k->fim = 0x7fffffff; s++; }
    else if (*s == '/') { s++; k->re_end = sed_trecho(&s, '/'); }
    if (*s == ',') {
        s++;
        if (*s == '$') { k->fim = 0x7ffffffe; s++; }
        else k->fim = (int)strtol(s, (char **)&s, 10);
    }
    while (isspace((unsigned char)*s)) s++;
    k->cmd = *s;
    if (*s == 's') {
        char delim = s[1];
        if (!delim) return 0;
        s += 2;
        k->re = sed_trecho(&s, delim);
        k->sub = sed_trecho(&s, delim);
        for (; *s; s++) { if (*s == 'g') k->g = 1; else if (*s == 'i' || *s == 'I') k->ic = 1; else if (*s == 'p') k->p = 1; }
        return 1;
    }
    return *s == 'd' || *s == 'p' || *s == 'q';
}
static int c_sed(Ctx *c, int argc, char **argv) {
    SedCmd cmds[16]; int nc = 0, quieto = 0, no_lugar = 0, i = 1;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "-n")) quieto = 1;
        else if (!strcmp(argv[i], "-i") || !strncmp(argv[i], "-i", 2)) no_lugar = 1;
        else if (!strcmp(argv[i], "-E") || !strcmp(argv[i], "-r")) {}
        else if (!strcmp(argv[i], "-e") && i + 1 < argc) { if (nc < 16 && !sed_parse(argv[++i], &cmds[nc++])) { ERR("sed: comando não suportado: %s\n", argv[i]); return 1; } }
        else break;
    }
    if (nc == 0) {
        if (i >= argc) { ERR("Uso: sed [-n] [-i] 's/expressão/troca/[g]' [arquivo]\n"); return 1; }
        /* vários comandos separados por ; */
        char *txt = xstrdup(argv[i++]);
        for (char *p = txt; p && *p && nc < 16;) {
            char *f = NULL;
            if (*p != 's' && !strchr(p, 's')) f = strchr(p, ';');
            else {
                /* acha o ; fora da expressão: só quebra quando o comando não é s///, ou depois das barras */
                const char *q = p;
                SedCmd t;
                if (sed_parse(p, &t) && t.cmd == 's') {
                    const char *s2 = strchr(q, 's');
                    char dl = s2[1];
                    int barras = 0;
                    for (s2 += 2; *s2; s2++) { if (*s2 == '\\' && s2[1]) { s2++; continue; } if (*s2 == dl && ++barras == 2) break; }
                    f = *s2 ? strchr(s2, ';') : NULL;
                    free(t.re); free(t.sub); free(t.re_end);
                } else f = strchr(p, ';');
            }
            if (f) *f = 0;
            if (!sed_parse(p, &cmds[nc++])) { ERR("sed: comando não suportado: %s\n", p); free(txt); return 1; }
            p = f ? f + 1 : NULL;
        }
        free(txt);
    }
    int r = 0, k = i;
    do {
        const char *d; size_t n;
        const char *arq = k < argc ? argv[k] : NULL;
        if (ler_arq(c, "sed", arq, &d, &n)) { r = 1; continue; }
        long total = 0;
        for (size_t x = 0; x < n; x++) if (d[x] == '\n') total++;
        if (n && d[n - 1] != '\n') total++;
        Buf sai; buf_init(&sai);
        const char *p = d, *fim = d + n, *l; size_t tl; long num = 0; int parar = 0;
        while (!parar && (l = prox_linha(&p, fim, &tl))) {
            num++;
            char *linha = xmalloc(tl + 1);
            memcpy(linha, l, tl); linha[tl] = 0;
            int apagada = 0;
            for (int q = 0; q < nc && !apagada; q++) {
                SedCmd *s = &cmds[q];
                int vale = 1, a, b;
                long ini = s->ini == 0x7fffffff ? total : s->ini, fi = s->fim >= 0x7ffffffe ? total : s->fim;
                if (s->re_end) vale = re_buscar(s->re_end, linha, 0, &a, &b);
                else if (s->ini >= 0) vale = num >= ini && num <= fi;
                if (!vale) continue;
                if (s->cmd == 'd') apagada = 1;
                else if (s->cmd == 'p') { buf_puts(&sai, linha); buf_putc(&sai, '\n'); }
                else if (s->cmd == 'q') parar = 1;
                else if (s->cmd == 's') {
                    Buf nv; buf_init(&nv);
                    const char *resto = linha; int trocou = 0;
                    while (re_buscar(s->re, resto, s->ic, &a, &b)) {
                        buf_put(&nv, resto, (size_t)a);
                        for (const char *t = s->sub; *t; t++) {
                            if (*t == '&') buf_put(&nv, resto + a, (size_t)(b - a));
                            else if (*t == '\\' && t[1]) buf_putc(&nv, *++t);
                            else buf_putc(&nv, *t);
                        }
                        trocou = 1;
                        if (b == a) { if (!resto[a]) { resto += a; break; } buf_putc(&nv, resto[a]); resto += a + 1; }
                        else resto += b;
                        if (!s->g) break;
                    }
                    buf_puts(&nv, resto);
                    free(linha);
                    linha = buf_str(&nv);
                    if (trocou && s->p) { buf_puts(&sai, linha); buf_putc(&sai, '\n'); }
                }
            }
            if (!apagada && !quieto) { buf_puts(&sai, linha); buf_putc(&sai, '\n'); }
            free(linha);
        }
        if (no_lugar && arq) { if (vfs_gravar(c->sh->cwd, arq, sai.d ? sai.d : "", sai.n, 0)) { ERR("sed: não foi possível gravar %s: %s\n", arq, vfs_erro(vfs_err)); r = 1; } }
        else o_write(c->out, sai.d, sai.n);
        buf_free(&sai);
    } while (++k < argc);
    for (int q = 0; q < nc; q++) { free(cmds[q].re); free(cmds[q].sub); free(cmds[q].re_end); }
    return r;
}

/* ------------------------------------------------------------------ awk (só o mais usado: print de campos) */

static int c_awk(Ctx *c, int argc, char **argv) {
    char sep = 0; int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strncmp(argv[i], "-F", 2)) { const char *v = argv[i][2] ? argv[i] + 2 : (i + 1 < argc ? argv[++i] : " "); sep = v[0] == '\\' && v[1] == 't' ? '\t' : v[0]; }
    }
    if (i >= argc) { ERR("uso: awk [-F sep] '[/padrão/] {print $1, $2}' [arquivo]\n"); return 2; }
    const char *prog = argv[i++];
    char *padrao = NULL;
    while (isspace((unsigned char)*prog)) prog++;
    if (*prog == '/') { prog++; padrao = sed_trecho(&prog, '/'); }
    while (isspace((unsigned char)*prog)) prog++;
    const char *corpo = "print $0";
    char *corpo_m = NULL;
    if (*prog == '{') {
        const char *f = strrchr(prog, '}');
        if (!f) { ERR("awk: falta } no programa\n"); free(padrao); return 2; }
        corpo_m = xasprintf("%.*s", (int)(f - prog - 1), prog + 1);
        corpo = corpo_m;
    } else if (*prog) { ERR("awk: o awk do TNUX só entende: [/padrão/] { print ... }\n"); free(padrao); return 2; }
    while (isspace((unsigned char)*corpo)) corpo++;
    if (strncmp(corpo, "print", 5)) { ERR("awk: o awk do TNUX só entende o comando print\n"); free(padrao); free(corpo_m); return 2; }
    corpo += 5;
    const char *d; size_t n;
    if (ler_arq(c, "awk", i < argc ? argv[i] : NULL, &d, &n)) { free(padrao); free(corpo_m); return 2; }
    const char *p = d, *fim = d + n, *l; size_t tl; long nr = 0;
    while ((l = prox_linha(&p, fim, &tl))) {
        nr++;
        char *linha = xmalloc(tl + 1);
        memcpy(linha, l, tl); linha[tl] = 0;
        int a, b;
        if (padrao && !re_buscar(padrao, linha, 0, &a, &b)) { free(linha); continue; }
        /* separa os campos */
        char *copia = xstrdup(linha);
        char *campos[256]; int nf = 0;
        char *q = copia;
        if (sep && sep != ' ') {
            while (nf < 255) { campos[nf++] = q; char *f = strchr(q, sep); if (!f) break; *f = 0; q = f + 1; }
        } else {
            while (*q && nf < 255) {
                while (isspace((unsigned char)*q)) q++;
                if (!*q) break;
                campos[nf++] = q;
                while (*q && !isspace((unsigned char)*q)) q++;
                if (*q) *q++ = 0;
            }
        }
        Buf o; buf_init(&o);
        const char *e = corpo;
        int vazio = 1;
        for (;;) {
            while (isspace((unsigned char)*e)) e++;
            if (!*e || *e == ';') break;
            vazio = 0;
            if (*e == ',') { buf_putc(&o, ' '); e++; continue; }
            if (*e == '"') {
                e++;
                while (*e && *e != '"') {
                    if (*e == '\\' && e[1]) { e++; buf_putc(&o, *e == 'n' ? '\n' : *e == 't' ? '\t' : *e); e++; }
                    else buf_putc(&o, *e++);
                }
                if (*e) e++;
                continue;
            }
            if (*e == '$') {
                e++;
                long k;
                if (!strncmp(e, "NF", 2)) { k = nf; e += 2; }
                else k = strtol(e, (char **)&e, 10);
                if (k == 0) buf_puts(&o, linha);
                else if (k >= 1 && k <= nf) buf_puts(&o, campos[k - 1]);
                continue;
            }
            if (!strncmp(e, "NF", 2)) { buf_printf(&o, "%d", nf); e += 2; continue; }
            if (!strncmp(e, "NR", 2)) { buf_printf(&o, "%ld", nr); e += 2; continue; }
            if (isdigit((unsigned char)*e)) { while (isdigit((unsigned char)*e) || *e == '.') buf_putc(&o, *e++); continue; }
            e++;
        }
        if (vazio) buf_puts(&o, linha);
        buf_putc(&o, '\n');
        o_write(c->out, o.d, o.n);
        buf_free(&o);
        free(copia); free(linha);
    }
    free(padrao); free(corpo_m);
    return 0;
}

/* ------------------------------------------------------------------ tee, seq, xargs, base64, hexdump */

static int c_tee(Ctx *c, int argc, char **argv) {
    int acr = 0, r = 0;
    o_write(c->out, c->in ? c->in : "", c->in ? c->inlen : 0);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-a")) { acr = 1; continue; }
        if (vfs_gravar(c->sh->cwd, argv[i], c->in ? c->in : "", c->in ? c->inlen : 0, acr)) { ERR("tee: %s: %s\n", argv[i], vfs_erro(vfs_err)); r = 1; }
    }
    return r;
}
static int c_seq(Ctx *c, int argc, char **argv) {
    int i = 1; const char *sep = "\n";
    if (argc > 2 && !strcmp(argv[1], "-s")) { sep = argv[2]; i = 3; }
    long a = 1, passo = 1, b;
    if (argc - i == 1) b = atol(argv[i]);
    else if (argc - i == 2) { a = atol(argv[i]); b = atol(argv[i + 1]); }
    else if (argc - i == 3) { a = atol(argv[i]); passo = atol(argv[i + 1]); b = atol(argv[i + 2]); }
    else { ERR("seq: falta operando\n"); return 1; }
    if (passo == 0) return 1;
    Buf o; buf_init(&o);
    long conta = 0;
    for (long v = a; passo > 0 ? v <= b : v >= b; v += passo) {
        if (conta++) buf_puts(&o, sep);
        buf_printf(&o, "%ld", v);
        if (conta > 2000000) break;
    }
    if (conta) buf_putc(&o, '\n');
    o_write(c->out, o.d, o.n);
    buf_free(&o);
    return 0;
}
static int c_xargs(Ctx *c, int argc, char **argv) {
    int i = 1, por = 0; const char *troca = NULL;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc) por = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-I") && i + 1 < argc) troca = argv[++i];
        else if (!strncmp(argv[i], "-n", 2)) por = atoi(argv[i] + 2);
    }
    /* separa a entrada em itens (por linha com -I; senão por espaço) */
    char *txt = xmalloc(c->inlen + 1);
    memcpy(txt, c->in ? c->in : "", c->inlen); txt[c->inlen] = 0;
    char **itens = NULL; int ni = 0;
    for (char *p = txt; *p;) {
        while (*p && (troca ? *p == '\n' : isspace((unsigned char)*p))) p++;
        if (!*p) break;
        itens = xrealloc(itens, sizeof(char *) * (size_t)(ni + 1));
        itens[ni++] = p;
        while (*p && !(troca ? *p == '\n' : isspace((unsigned char)*p))) p++;
        if (*p) *p++ = 0;
    }
    int nbase = argc - i, r = 0;
    char *eco[] = { "echo" };
    char **base = nbase ? argv + i : eco;
    if (!nbase) nbase = 1;
    if (troca) por = 1;
    if (por <= 0) por = ni ? ni : 1;
    Ctx c2 = *c;
    c2.in = NULL; c2.inlen = 0;
    for (int k = 0; k < ni; k += por) {
        int n = 0;
        char **av = xmalloc(sizeof(char *) * (size_t)(nbase + por + 1));
        char **livrar = xmalloc(sizeof(char *) * (size_t)(nbase + 1)); int nl = 0;
        for (int b = 0; b < nbase; b++) {
            if (troca && strstr(base[b], troca)) {
                Buf t; buf_init(&t);
                for (const char *s = base[b]; *s;) {
                    if (!strncmp(s, troca, strlen(troca))) { buf_puts(&t, itens[k]); s += strlen(troca); }
                    else buf_putc(&t, *s++);
                }
                av[n++] = livrar[nl++] = buf_str(&t);
            } else av[n++] = base[b];
        }
        if (!troca) for (int q = k; q < k + por && q < ni; q++) av[n++] = itens[q];
        av[n] = NULL;
        int rr = sh_exec_argv(&c2, n, av);
        if (rr) r = 123;
        for (int q = 0; q < nl; q++) free(livrar[q]);
        free(livrar); free(av);
    }
    free(itens); free(txt);
    return r;
}
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int c_base64(Ctx *c, int argc, char **argv) {
    int dec = 0, i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) if (strchr(argv[i], 'd') || !strcmp(argv[i], "--decode")) dec = 1;
    const char *d; size_t n;
    if (ler_arq(c, "base64", i < argc ? argv[i] : NULL, &d, &n)) return 1;
    Buf o; buf_init(&o);
    if (dec) {
        unsigned acc = 0; int bits = 0;
        for (size_t k = 0; k < n; k++) {
            const char *p = d[k] ? strchr(B64, d[k]) : NULL;
            if (!p) continue;
            acc = (acc << 6) | (unsigned)(p - B64); bits += 6;
            if (bits >= 8) { bits -= 8; buf_putc(&o, (acc >> bits) & 0xff); }
        }
    } else {
        int col = 0;
        for (size_t k = 0; k < n; k += 3) {
            unsigned v = (unsigned char)d[k] << 16;
            if (k + 1 < n) v |= (unsigned char)d[k + 1] << 8;
            if (k + 2 < n) v |= (unsigned char)d[k + 2];
            buf_putc(&o, B64[v >> 18]); buf_putc(&o, B64[(v >> 12) & 63]);
            buf_putc(&o, k + 1 < n ? B64[(v >> 6) & 63] : '=');
            buf_putc(&o, k + 2 < n ? B64[v & 63] : '=');
            col += 4;
            if (col >= 76) { buf_putc(&o, '\n'); col = 0; }
        }
        if (col) buf_putc(&o, '\n');
    }
    o_write(c->out, o.d, o.n);
    buf_free(&o);
    return 0;
}
static int c_hexdump(Ctx *c, int argc, char **argv) {
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1]) i++;
    const char *d; size_t n;
    if (ler_arq(c, argv[0], i < argc ? argv[i] : NULL, &d, &n)) return 1;
    Buf o; buf_init(&o);
    for (size_t k = 0; k < n; k += 16) {
        buf_printf(&o, "%08lx  ", (unsigned long)k);
        for (size_t x = 0; x < 16; x++) {
            if (k + x < n) buf_printf(&o, "%02x ", (unsigned char)d[k + x]); else buf_puts(&o, "   ");
            if (x == 7) buf_putc(&o, ' ');
        }
        buf_puts(&o, " |");
        for (size_t x = 0; x < 16 && k + x < n; x++) buf_putc(&o, isprint((unsigned char)d[k + x]) ? d[k + x] : '.');
        buf_puts(&o, "|\n");
        if (o.n > 65536) { o_write(c->out, o.d, o.n); o.n = 0; }
    }
    buf_printf(&o, "%08lx\n", (unsigned long)n);
    o_write(c->out, o.d, o.n);
    buf_free(&o);
    return 0;
}

const Cmd cmds_txt[] = {
    { "echo", c_echo, "escreve um texto", "echo [-n] [-e] texto..." },
    { "printf", c_printf, "escreve com formato", "printf 'formato\\n' argumentos..." },
    { "head", c_head, "mostra o começo de um arquivo", "head [-n N] [arquivo...]" },
    { "tail", c_head, "mostra o fim de um arquivo", "tail [-n N | -n +N] [arquivo...]" },
    { "wc", c_wc, "conta linhas, palavras e bytes", "wc [-l] [-w] [-c] [arquivo...]" },
    { "grep", c_grep, "procura texto em arquivos", "grep [-i] [-v] [-n] [-c] [-r] [-l] [-o] [-w] [-F] padrão [arquivo...]" },
    { "egrep", c_grep, NULL, NULL },
    { "fgrep", c_grep, NULL, NULL },
    { "sed", c_sed, "troca e filtra texto", "sed [-n] [-i] 's/de/para/g' | 'Nd' | '/re/d' | 'N,Mp' [arquivo]" },
    { "awk", c_awk, "mostra campos de cada linha", "awk [-F sep] '[/padrão/] {print $1, $NF}' [arquivo]" },
    { "sort", c_sort, "ordena linhas", "sort [-n] [-r] [-u] [-f] [-k N] [-t sep] [arquivo...]" },
    { "uniq", c_uniq, "tira linhas repetidas seguidas", "uniq [-c] [-d] [-u] [arquivo]" },
    { "cut", c_cut, "recorta colunas", "cut -d sep -f lista | -c lista [arquivo]" },
    { "tr", c_tr, "troca ou apaga caracteres", "tr [-d] [-s] conjunto1 [conjunto2]" },
    { "tee", c_tee, "copia a entrada para arquivos e para a saída", "comando | tee [-a] arquivo" },
    { "tac", c_tac, "mostra as linhas de trás para frente", "tac [arquivo]" },
    { "rev", c_tac, "inverte cada linha", "rev [arquivo]" },
    { "nl", c_nl, "numera as linhas", "nl [arquivo]" },
    { "seq", c_seq, "gera uma sequência de números", "seq [início [passo]] fim" },
    { "xargs", c_xargs, "monta comandos com a entrada", "comando | xargs [-n N] [-I {}] comando" },
    { "base64", c_base64, "codifica ou decodifica em base64", "base64 [-d] [arquivo]" },
    { "hexdump", c_hexdump, "mostra os bytes em hexadecimal", "hexdump -C arquivo" },
    { "xxd", c_hexdump, NULL, NULL },
    { NULL, NULL, NULL, NULL }
};
