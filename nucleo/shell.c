/*
 * TNUX: o shell.
 *
 * Um interpretador no estilo do sh/bash para o sistema que roda no navegador: aspas, variáveis,
 * $(substituição), $((contas)), pipes, redirecionamentos, && || ; , curingas (* ? []), apelidos,
 * if/elif/else, while/until, for, e scripts (.sh). Como no navegador não há processos, um pipe
 * roda os comandos um depois do outro, passando a saída de um como entrada do próximo.
 */
#include "tnux.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ saída */

void o_write(Out *o, const char *p, size_t n) {
    if (!o || !n) return;
    if (o->buf) { buf_put(o->buf, p, n); return; }
    /* terminal em modo cru: \n vira \r\n */
    size_t ini = 0;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '\n' && (i == 0 || p[i - 1] != '\r')) {
            if (i > ini) host_write(o->sid, p + ini, i - ini);
            host_write(o->sid, "\r\n", 2);
            ini = i + 1;
        }
    }
    if (n > ini) host_write(o->sid, p + ini, n - ini);
}
void o_puts(Out *o, const char *s) { o_write(o, s, strlen(s)); }
void o_printf(Out *o, const char *fmt, ...) {
    Buf b; buf_init(&b);
    va_list ap;
    va_start(ap, fmt);
    buf_vprintf(&b, fmt, ap);
    va_end(ap);
    o_write(o, b.d, b.n);
    buf_free(&b);
}
int o_tty(Out *o) { return o && !o->buf; }

/* ------------------------------------------------------------------ variáveis */

static Var *var_achar(Var *v, int n, const char *k) {
    for (int i = 0; i < n; i++) if (!strcmp(v[i].k, k)) return &v[i];
    return NULL;
}
const char *sh_get(Shell *sh, const char *k) {
    Var *v = var_achar(sh->vars, sh->nvars, k);
    return v ? v->v : NULL;
}
void sh_set(Shell *sh, const char *k, const char *val, int exportar) {
    Var *v = var_achar(sh->vars, sh->nvars, k);
    if (v) {
        if (val) { free(v->v); v->v = xstrdup(val); }
        if (exportar) v->exportada = 1;
        return;
    }
    if (sh->nvars == sh->capvars) {
        sh->capvars = sh->capvars ? sh->capvars * 2 : 32;
        sh->vars = xrealloc(sh->vars, sizeof(Var) * (size_t)sh->capvars);
    }
    sh->vars[sh->nvars].k = xstrdup(k);
    sh->vars[sh->nvars].v = xstrdup(val ? val : "");
    sh->vars[sh->nvars].exportada = exportar;
    sh->nvars++;
}
void sh_unset(Shell *sh, const char *k) {
    Var *v = var_achar(sh->vars, sh->nvars, k);
    if (!v) return;
    free(v->k); free(v->v);
    *v = sh->vars[--sh->nvars];
}
static void apelido_set(Shell *sh, const char *k, const char *val) {
    Var *v = var_achar(sh->apelidos, sh->napelidos, k);
    if (v) { free(v->v); v->v = xstrdup(val); return; }
    if (sh->napelidos == sh->capapelidos) {
        sh->capapelidos = sh->capapelidos ? sh->capapelidos * 2 : 16;
        sh->apelidos = xrealloc(sh->apelidos, sizeof(Var) * (size_t)sh->capapelidos);
    }
    sh->apelidos[sh->napelidos].k = xstrdup(k);
    sh->apelidos[sh->napelidos].v = xstrdup(val);
    sh->apelidos[sh->napelidos].exportada = 0;
    sh->napelidos++;
}

Shell *sh_novo(int sid) {
    Shell *sh = xmalloc(sizeof *sh);
    memset(sh, 0, sizeof *sh);
    sh->sid = sid;
    sh->cwd = xstrdup(TNUX_CASA);
    sh->nome0 = xstrdup("bash");
    sh->cols = 80; sh->rows = 24;
    buf_init(&sh->pendente);
    sh_set(sh, "HOME", TNUX_CASA, 1);
    sh_set(sh, "USER", TNUX_USUARIO, 1);
    sh_set(sh, "LOGNAME", TNUX_USUARIO, 1);
    sh_set(sh, "SHELL", "/bin/bash", 1);
    sh_set(sh, "PATH", "/usr/local/bin:/usr/bin:/bin:/usr/games:" TNUX_CASA "/.local/bin", 1);
    sh_set(sh, "PWD", TNUX_CASA, 1);
    sh_set(sh, "LANG", "pt_BR.UTF-8", 1);
    sh_set(sh, "TERM", "xterm-256color", 1);
    sh_set(sh, "HOSTNAME", sis.host ? sis.host : "tnux", 1);
    sh_set(sh, "XDG_CURRENT_DESKTOP", "X-Cinnamon", 1);
    sh_set(sh, "DESKTOP_SESSION", "tnux", 1);
    sh_set(sh, "EDITOR", "nano", 1);
    apelido_set(sh, "ll", "ls -alF");
    apelido_set(sh, "la", "ls -A");
    apelido_set(sh, "l", "ls -CF");
    return sh;
}
static Shell *sh_filho(Shell *pai) {
    Shell *sh = xmalloc(sizeof *sh);
    memset(sh, 0, sizeof *sh);
    sh->sid = pai->sid;
    sh->cwd = xstrdup(pai->cwd);
    sh->nome0 = xstrdup(pai->nome0);
    sh->cols = pai->cols; sh->rows = pai->rows;
    sh->nivel = pai->nivel + 1;
    sh->ultimo = pai->ultimo;
    buf_init(&sh->pendente);
    for (int i = 0; i < pai->nvars; i++)
        if (pai->vars[i].exportada) sh_set(sh, pai->vars[i].k, pai->vars[i].v, 1);
    return sh;
}
static void no_livrar_fwd(void *n);
void sh_livrar(Shell *sh) {
    if (!sh) return;
    for (int i = 0; i < sh->nfuncs; i++) { free(sh->fnomes[i]); no_livrar_fwd(sh->funcs[i]); }
    free(sh->fnomes); free(sh->funcs);
    for (int i = 0; i < sh->nlixo; i++) no_livrar_fwd(sh->lixo[i]);
    free(sh->lixo);
    for (int i = 0; i < sh->nvars; i++) { free(sh->vars[i].k); free(sh->vars[i].v); }
    for (int i = 0; i < sh->napelidos; i++) { free(sh->apelidos[i].k); free(sh->apelidos[i].v); }
    for (int i = 0; i < sh->npos; i++) free(sh->pos[i]);
    free(sh->vars); free(sh->apelidos); free(sh->pos); free(sh->cwd); free(sh->nome0);
    buf_free(&sh->pendente);
    ed_livrar(sh->ed);
    free(sh);
}

char *sh_prompt(Shell *sh) {
    const char *casa = sh_get(sh, "HOME");
    Buf d; buf_init(&d);
    size_t lc = casa ? strlen(casa) : 0;
    if (lc > 1 && !strncmp(sh->cwd, casa, lc) && (sh->cwd[lc] == 0 || sh->cwd[lc] == '/')) {
        buf_putc(&d, '~');
        buf_puts(&d, sh->cwd + lc);
    } else buf_puts(&d, sh->cwd);
    int root = vfs_euid == 0;
    char *p = xasprintf("\x1b[01;%dm%s@%s\x1b[00m:\x1b[01;34m%s\x1b[00m%s ", root ? 31 : 32,
                        root ? "root" : TNUX_USUARIO, sis.host ? sis.host : "tnux", buf_str(&d), root ? "#" : "$");
    buf_free(&d);
    return p;
}

/* ------------------------------------------------------------------ lista de strings */

typedef struct { char **v; int n, cap; } Lista;
static void li_add(Lista *l, char *s) {
    if (l->n + 1 >= l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->v = xrealloc(l->v, sizeof(char *) * (size_t)l->cap);
    }
    l->v[l->n++] = s;
    l->v[l->n] = NULL;
}
static void li_livrar(Lista *l) {
    for (int i = 0; i < l->n; i++) free(l->v[i]);
    free(l->v);
    l->v = NULL; l->n = l->cap = 0;
}

/* ------------------------------------------------------------------ analisador léxico */

typedef enum { K_PAL, K_OP, K_REDIR, K_NL } TokTipo;
typedef struct Tok { TokTipo tipo; char *txt; int fd, modo, dup; } Tok;
/* modo do redirecionamento: 0 '<'  1 '>'  2 '>>'  3 duplicar (>&N)  4 '&>'  5 '&>>' */
typedef struct Lex { Tok *t; int n, cap; int incompleto; } Lex;

static const char *pula_cmdsub(const char *p, int *inc);

/* devolve o fim da palavra que começa em p */
static const char *fim_palavra(const char *p, int *inc) {
    while (*p) {
        char ch = *p;
        if (isspace((unsigned char)ch) || strchr(";&|<>()", ch)) break;
        if (ch == '\\') {
            if (!p[1]) { *inc = 1; return p + 1; }
            p += 2;
            continue;
        }
        if (ch == '\'') {
            const char *q = strchr(p + 1, '\'');
            if (!q) { *inc = 1; return p + strlen(p); }
            p = q + 1;
            continue;
        }
        if (ch == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p += 2;
                else if (*p == '$' && p[1] == '(') p = pula_cmdsub(p + 1, inc);
                else p++;
            }
            if (!*p) { *inc = 1; return p; }
            p++;
            continue;
        }
        if (ch == '`') {
            const char *q = strchr(p + 1, '`');
            if (!q) { *inc = 1; return p + strlen(p); }
            p = q + 1;
            continue;
        }
        if (ch == '$' && p[1] == '(') { p = pula_cmdsub(p + 1, inc); continue; }
        if (ch == '$' && p[1] == '{') {
            const char *q = strchr(p + 2, '}');
            if (!q) { *inc = 1; return p + strlen(p); }
            p = q + 1;
            continue;
        }
        p++;
    }
    return p;
}
/* p aponta para '(' ; devolve depois do ')' que fecha */
static const char *pula_cmdsub(const char *p, int *inc) {
    int nivel = 0;
    while (*p) {
        if (*p == '\\' && p[1]) { p += 2; continue; }
        if (*p == '\'') {
            const char *q = strchr(p + 1, '\'');
            if (!q) break;
            p = q + 1;
            continue;
        }
        if (*p == '"') {
            p++;
            while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
            if (!*p) break;
            p++;
            continue;
        }
        if (*p == '(') nivel++;
        else if (*p == ')') { nivel--; if (nivel == 0) return p + 1; }
        p++;
    }
    *inc = 1;
    return p;
}

static Tok *tok_novo(Lex *l, TokTipo tipo, const char *txt, size_t n) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 32;
        l->t = xrealloc(l->t, sizeof(Tok) * (size_t)l->cap);
    }
    Tok *t = &l->t[l->n++];
    memset(t, 0, sizeof *t);
    t->tipo = tipo;
    t->txt = xmalloc(n + 1);
    memcpy(t->txt, txt, n);
    t->txt[n] = 0;
    return t;
}
static void lex_livrar(Lex *l) {
    for (int i = 0; i < l->n; i++) free(l->t[i].txt);
    free(l->t);
}

static void lexar(const char *s, Lex *l) {
    memset(l, 0, sizeof *l);
    const char *p = s;
    while (*p) {
        if (*p == '\n') { tok_novo(l, K_NL, "\n", 1); p++; continue; }
        if (isspace((unsigned char)*p)) { p++; continue; }
        if (*p == '\\' && p[1] == '\n') { p += 2; continue; }
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        /* redirecionamentos, com descritor opcional na frente */
        const char *q = p;
        int fd = -1;
        if (isdigit((unsigned char)*q)) {
            const char *d = q;
            while (isdigit((unsigned char)*d)) d++;
            if (*d == '>' || *d == '<') { fd = atoi(q); q = d; }
        }
        if (*q == '&' && q[1] == '>') {
            Tok *t = tok_novo(l, K_REDIR, "&>", 2);
            t->fd = 1; t->modo = 4;
            p = q + 2;
            if (*p == '>') { t->modo = 5; p++; }
            continue;
        }
        if (*q == '>' || *q == '<') {
            Tok *t = tok_novo(l, K_REDIR, q, 1);
            if (*q == '<') { t->fd = fd < 0 ? 0 : fd; t->modo = 0; p = q + 1; if (*p == '<') { l->incompleto = 0; } continue; }
            t->fd = fd < 0 ? 1 : fd;
            t->modo = 1;
            p = q + 1;
            if (*p == '>') { t->modo = 2; p++; }
            else if (*p == '&' && (isdigit((unsigned char)p[1]) || p[1] == '-')) {
                t->modo = 3;
                t->dup = p[1] == '-' ? -1 : p[1] - '0';
                p += 2;
            } else if (*p == '|') p++;
            continue;
        }
        if (*p == '&' && p[1] == '&') { tok_novo(l, K_OP, "&&", 2); p += 2; continue; }
        if (*p == '|' && p[1] == '|') { tok_novo(l, K_OP, "||", 2); p += 2; continue; }
        if (*p == ';' && p[1] == ';') { tok_novo(l, K_OP, ";;", 2); p += 2; continue; }
        if (strchr(";&|()", *p)) { tok_novo(l, K_OP, p, 1); p++; continue; }
        int inc = 0;
        const char *f = fim_palavra(p, &inc);
        if (inc) l->incompleto = 1;
        if (f == p) { p++; continue; }
        tok_novo(l, K_PAL, p, (size_t)(f - p));
        p = f;
    }
}

/* ------------------------------------------------------------------ árvore */

typedef enum { N_SIMPLES, N_PIPE, N_E, N_OU, N_SEQ, N_SE, N_ENQUANTO, N_PARA, N_NAO, N_CASO, N_FUNC } NoTipo;
typedef struct Redir { int fd, modo, dup; char *alvo; } Redir;
typedef struct No No;
struct No {
    NoTipo tipo;
    char **pals; int npals;
    Redir *reds; int nreds;
    No **f; int nf;
    char *var;
    int ate;          /* until em vez de while */
};

static No *no(NoTipo t) {
    No *n = xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->tipo = t;
    return n;
}
static void no_filho(No *n, No *f) {
    n->f = xrealloc(n->f, sizeof(No *) * (size_t)(n->nf + 1));
    n->f[n->nf++] = f;
}
static void no_pal(No *n, const char *s) {
    n->pals = xrealloc(n->pals, sizeof(char *) * (size_t)(n->npals + 2));
    n->pals[n->npals++] = xstrdup(s);
    n->pals[n->npals] = NULL;
}
static void no_livrar(No *n) {
    if (!n) return;
    for (int i = 0; i < n->npals; i++) free(n->pals[i]);
    for (int i = 0; i < n->nreds; i++) free(n->reds[i].alvo);
    for (int i = 0; i < n->nf; i++) no_livrar(n->f[i]);
    free(n->pals); free(n->reds); free(n->f); free(n->var); free(n);
}

static void no_livrar_fwd(void *n) { no_livrar(n); }

typedef struct P { Tok *t; int i, n; int erro, incompleto; char msg[160]; } P;

static Tok *pk(P *p) { return p->i < p->n ? &p->t[p->i] : NULL; }
static int eh_pal(P *p, const char *s) {
    Tok *t = pk(p);
    return t && t->tipo == K_PAL && !strcmp(t->txt, s);
}
static int eh_op(P *p, const char *s) {
    Tok *t = pk(p);
    return t && t->tipo == K_OP && !strcmp(t->txt, s);
}
static void erro_sintaxe(P *p) {
    if (p->erro) return;
    p->erro = 1;
    Tok *t = pk(p);
    if (!t) { p->incompleto = 1; snprintf(p->msg, sizeof p->msg, "erro de sintaxe: fim de arquivo inesperado"); }
    else snprintf(p->msg, sizeof p->msg, "erro de sintaxe próximo ao token inesperado `%s'", t->tipo == K_NL ? "newline" : t->txt);
}
static void espera(P *p, const char *pal) {
    if (eh_pal(p, pal)) { p->i++; return; }
    erro_sintaxe(p);
}
static void pula_nl(P *p) { while (pk(p) && pk(p)->tipo == K_NL) p->i++; }

static No *p_lista(P *p);

static int fecha_bloco(P *p) {
    static const char *const f[] = { "then", "else", "elif", "fi", "do", "done", "}", "esac", NULL };
    Tok *t = pk(p);
    if (!t) return 1;
    if (t->tipo == K_OP && (!strcmp(t->txt, ")") || !strcmp(t->txt, ";;"))) return 1;
    if (t->tipo != K_PAL) return 0;
    for (int i = 0; f[i]; i++) if (!strcmp(t->txt, f[i])) return 1;
    return 0;
}

static No *p_se(P *p) {
    No *n = no(N_SE);
    no_filho(n, p_lista(p));
    espera(p, "then");
    no_filho(n, p_lista(p));
    if (p->erro) return n;
    if (eh_pal(p, "elif")) { p->i++; no_filho(n, p_se(p)); return n; }
    if (eh_pal(p, "else")) { p->i++; no_filho(n, p_lista(p)); }
    espera(p, "fi");
    return n;
}

static No *p_simples(P *p) {
    No *n = no(N_SIMPLES);
    for (;;) {
        Tok *t = pk(p);
        if (!t) break;
        if (t->tipo == K_PAL) { no_pal(n, t->txt); p->i++; continue; }
        if (t->tipo == K_REDIR) {
            Redir r = { t->fd, t->modo, t->dup, NULL };
            p->i++;
            if (r.modo != 3) {
                Tok *a = pk(p);
                if (!a || a->tipo != K_PAL) { erro_sintaxe(p); break; }
                r.alvo = xstrdup(a->txt);
                p->i++;
            }
            n->reds = xrealloc(n->reds, sizeof(Redir) * (size_t)(n->nreds + 1));
            n->reds[n->nreds++] = r;
            continue;
        }
        break;
    }
    if (!n->npals && !n->nreds) erro_sintaxe(p);
    return n;
}

static No *p_cmd(P *p) {
    if (eh_pal(p, "if")) { p->i++; return p_se(p); }
    if (eh_pal(p, "while") || eh_pal(p, "until")) {
        No *n = no(N_ENQUANTO);
        n->ate = eh_pal(p, "until");
        p->i++;
        no_filho(n, p_lista(p));
        espera(p, "do");
        no_filho(n, p_lista(p));
        espera(p, "done");
        return n;
    }
    if (eh_pal(p, "for")) {
        No *n = no(N_PARA);
        p->i++;
        Tok *t = pk(p);
        if (!t || t->tipo != K_PAL) { erro_sintaxe(p); return n; }
        n->var = xstrdup(t->txt);
        p->i++;
        pula_nl(p);
        if (eh_pal(p, "in")) {
            p->i++;
            while (pk(p) && pk(p)->tipo == K_PAL) { no_pal(n, pk(p)->txt); p->i++; }
        } else no_pal(n, "\"$@\"");
        if (eh_op(p, ";")) p->i++;
        pula_nl(p);
        espera(p, "do");
        no_filho(n, p_lista(p));
        espera(p, "done");
        return n;
    }
    if (eh_pal(p, "case")) {
        /* case PALAVRA in padrao|padrao) lista ;; ... esac
         * filhos: uma lista por ramo; pals: [palavra, padroes do ramo 0 separados por \x01, ...] */
        No *n = no(N_CASO);
        p->i++;
        Tok *t = pk(p);
        if (!t || t->tipo != K_PAL) { erro_sintaxe(p); return n; }
        no_pal(n, t->txt);
        p->i++;
        pula_nl(p);
        espera(p, "in");
        pula_nl(p);
        while (!p->erro && pk(p) && !eh_pal(p, "esac")) {
            Buf pad; buf_init(&pad);
            if (eh_op(p, "(")) p->i++;
            while (pk(p) && pk(p)->tipo == K_PAL) {
                if (pad.n) buf_putc(&pad, 1);
                buf_puts(&pad, pk(p)->txt);
                p->i++;
                if (eh_op(p, "|")) p->i++; else break;
            }
            if (!eh_op(p, ")")) { erro_sintaxe(p); buf_free(&pad); break; }
            p->i++;
            no_pal(n, buf_str(&pad));
            buf_free(&pad);
            no_filho(n, p_lista(p));
            if (eh_op(p, ";;")) p->i++;
            pula_nl(p);
        }
        espera(p, "esac");
        return n;
    }
    if (eh_pal(p, "{")) {
        p->i++;
        No *n = p_lista(p);
        espera(p, "}");
        return n;
    }
    if (eh_op(p, "(")) {
        p->i++;
        No *n = p_lista(p);
        if (eh_op(p, ")")) p->i++; else erro_sintaxe(p);
        return n;
    }
    /* definição de função: nome() { ... }  ou  function nome { ... } */
    {
        Tok *t = pk(p);
        int com_palavra = eh_pal(p, "function") && p->i + 1 < p->n && p->t[p->i + 1].tipo == K_PAL;
        int com_parens = t && t->tipo == K_PAL && p->i + 2 < p->n && p->t[p->i + 1].tipo == K_OP && !strcmp(p->t[p->i + 1].txt, "(") &&
                         p->t[p->i + 2].tipo == K_OP && !strcmp(p->t[p->i + 2].txt, ")");
        if (com_palavra || com_parens) {
            No *n = no(N_FUNC);
            if (com_palavra) p->i++;
            n->var = xstrdup(pk(p)->txt);
            p->i++;
            if (eh_op(p, "(") && p->i + 1 < p->n && !strcmp(p->t[p->i + 1].txt, ")")) p->i += 2;
            pula_nl(p);
            if (!pk(p)) { erro_sintaxe(p); return n; }
            no_filho(n, p_cmd(p));
            return n;
        }
    }
    return p_simples(p);
}

static No *p_pipe(P *p) {
    int nao = 0;
    while (eh_pal(p, "!")) { nao = !nao; p->i++; }
    No *prim = p_cmd(p);
    No *r = prim;
    if (eh_op(p, "|")) {
        r = no(N_PIPE);
        no_filho(r, prim);
        while (!p->erro && eh_op(p, "|")) {
            p->i++;
            pula_nl(p);
            if (!pk(p)) { p->erro = p->incompleto = 1; snprintf(p->msg, sizeof p->msg, "erro de sintaxe: fim de arquivo inesperado"); break; }
            no_filho(r, p_cmd(p));
        }
    }
    if (nao) { No *n = no(N_NAO); no_filho(n, r); r = n; }
    return r;
}

static No *p_eou(P *p) {
    No *esq = p_pipe(p);
    while (!p->erro && (eh_op(p, "&&") || eh_op(p, "||"))) {
        No *n = no(eh_op(p, "&&") ? N_E : N_OU);
        p->i++;
        pula_nl(p);
        if (!pk(p)) { p->erro = p->incompleto = 1; snprintf(p->msg, sizeof p->msg, "erro de sintaxe: fim de arquivo inesperado"); no_filho(n, esq); return n; }
        no_filho(n, esq);
        no_filho(n, p_pipe(p));
        esq = n;
    }
    return esq;
}

static No *p_lista(P *p) {
    No *seq = no(N_SEQ);
    while (!p->erro) {
        while (pk(p) && (pk(p)->tipo == K_NL || eh_op(p, ";") || eh_op(p, "&"))) p->i++;
        if (fecha_bloco(p)) break;
        no_filho(seq, p_eou(p));
        Tok *t = pk(p);
        if (t && !(t->tipo == K_NL || eh_op(p, ";") || eh_op(p, "&")) && !fecha_bloco(p)) erro_sintaxe(p);
    }
    return seq;
}

int sh_incompleto(const char *texto) {
    Lex l;
    lexar(texto, &l);
    int inc = l.incompleto;
    if (!inc) {
        size_t n = strlen(texto);
        while (n && (texto[n - 1] == '\n' || texto[n - 1] == ' ')) n--;
        if (n && texto[n - 1] == '\\' && (n < 2 || texto[n - 2] != '\\')) inc = 1;
    }
    if (!inc) {
        P p = { l.t, 0, l.n, 0, 0, "" };
        No *n = p_lista(&p);
        if (p.erro && p.incompleto) inc = 1;
        no_livrar(n);
    }
    lex_livrar(&l);
    return inc;
}

/* ------------------------------------------------------------------ contas: $(( ... )) */

typedef struct { Shell *sh; const char *p; int erro; } Ar;
static long long ar_ou(Ar *a);
static void ar_esp(Ar *a) { while (isspace((unsigned char)*a->p)) a->p++; }
static long long ar_prim(Ar *a) {
    ar_esp(a);
    if (*a->p == '(') {
        a->p++;
        long long v = ar_ou(a);
        ar_esp(a);
        if (*a->p == ')') a->p++; else a->erro = 1;
        return v;
    }
    if (*a->p == '-') { a->p++; return -ar_prim(a); }
    if (*a->p == '+') { a->p++; return ar_prim(a); }
    if (*a->p == '!') { a->p++; return !ar_prim(a); }
    if (isdigit((unsigned char)*a->p)) {
        char *f;
        long long v = strtoll(a->p, &f, 0);
        a->p = f;
        return v;
    }
    if (*a->p == '$') a->p++;
    if (isalpha((unsigned char)*a->p) || *a->p == '_') {
        char nome[64]; size_t n = 0;
        while ((isalnum((unsigned char)*a->p) || *a->p == '_') && n < sizeof nome - 1) nome[n++] = *a->p++;
        nome[n] = 0;
        const char *v = sh_get(a->sh, nome);
        return v ? strtoll(v, NULL, 0) : 0;
    }
    a->erro = 1;
    return 0;
}
static long long ar_mul(Ar *a) {
    long long v = ar_prim(a);
    for (;;) {
        ar_esp(a);
        char o = *a->p;
        if (o != '*' && o != '/' && o != '%') return v;
        a->p++;
        long long d = ar_prim(a);
        if (o == '*') v *= d;
        else if (d == 0) { a->erro = 2; return 0; }
        else if (o == '/') v /= d;
        else v %= d;
    }
}
static long long ar_soma(Ar *a) {
    long long v = ar_mul(a);
    for (;;) {
        ar_esp(a);
        if (*a->p == '+') { a->p++; v += ar_mul(a); }
        else if (*a->p == '-') { a->p++; v -= ar_mul(a); }
        else return v;
    }
}
static long long ar_cmp(Ar *a) {
    long long v = ar_soma(a);
    for (;;) {
        ar_esp(a);
        if (a->p[0] == '=' && a->p[1] == '=') { a->p += 2; v = v == ar_soma(a); }
        else if (a->p[0] == '!' && a->p[1] == '=') { a->p += 2; v = v != ar_soma(a); }
        else if (a->p[0] == '<' && a->p[1] == '=') { a->p += 2; v = v <= ar_soma(a); }
        else if (a->p[0] == '>' && a->p[1] == '=') { a->p += 2; v = v >= ar_soma(a); }
        else if (a->p[0] == '<') { a->p++; v = v < ar_soma(a); }
        else if (a->p[0] == '>') { a->p++; v = v > ar_soma(a); }
        else return v;
    }
}
static long long ar_e(Ar *a) {
    long long v = ar_cmp(a);
    for (;;) {
        ar_esp(a);
        if (a->p[0] == '&' && a->p[1] == '&') { a->p += 2; long long d = ar_cmp(a); v = v && d; }
        else return v;
    }
}
static long long ar_ou(Ar *a) {
    long long v = ar_e(a);
    for (;;) {
        ar_esp(a);
        if (a->p[0] == '|' && a->p[1] == '|') { a->p += 2; long long d = ar_e(a); v = v || d; }
        else return v;
    }
}

/* ------------------------------------------------------------------ expansão de palavras */

#define F_CITADO 1     /* veio entre aspas: não divide nem vira curinga */
#define F_EXP    2     /* veio de uma expansão sem aspas: divide nos espaços */
#define F_QUEBRA 4     /* separa campos à força ("$@") */

typedef struct { char *c; unsigned char *f; size_t n, cap; int teve_aspas; } Exp;

static void ex_put(Exp *e, char ch, int flag) {
    if (e->n + 1 >= e->cap) {
        e->cap = e->cap ? e->cap * 2 : 64;
        e->c = xrealloc(e->c, e->cap);
        e->f = xrealloc(e->f, e->cap);
    }
    e->c[e->n] = ch; e->f[e->n] = (unsigned char)flag;
    e->n++;
}
static void ex_puts(Exp *e, const char *s, int flag) { for (; *s; s++) ex_put(e, *s, flag); }

typedef struct { Shell *sh; Out *err; } XCtx;

static char *expandir_texto(XCtx *x, const char *raw);
static void expandir_em(XCtx *x, const char *raw, Exp *e, int entre_aspas);

static char *cmdsub(XCtx *x, const char *cmd, size_t n) {
    char *txt = xmalloc(n + 1);
    memcpy(txt, cmd, n); txt[n] = 0;
    Buf b; buf_init(&b);
    Out o = { &b, x->sh->sid };
    if (x->sh->nivel < 40) {
        x->sh->nivel++;
        sh_rodar(x->sh, txt, NULL, 0, &o, x->err);
        x->sh->nivel--;
    }
    free(txt);
    while (b.n && b.d[b.n - 1] == '\n') b.n--;
    return buf_str(&b);
}

/* expande o que vem depois de um '$'. p aponta para o caractere depois do '$'. Devolve o novo p. */
static const char *expandir_dolar(XCtx *x, const char *p, Exp *e, int aspas) {
    Shell *sh = x->sh;
    int flag = aspas ? F_CITADO : F_EXP;
    char tmp[32];
    if (*p == '(' && p[1] == '(') {            /* $(( conta )) */
        const char *q = p + 2;
        int nivel = 0;
        while (*q && !(nivel == 0 && q[0] == ')' && q[1] == ')')) {
            if (*q == '(') nivel++;
            else if (*q == ')') nivel--;
            q++;
        }
        if (*q) {
            char *expr = xmalloc((size_t)(q - p));
            memcpy(expr, p + 2, (size_t)(q - p - 2)); expr[q - p - 2] = 0;
            char *ex2 = expandir_texto(x, expr);
            Ar a = { sh, ex2, 0 };
            long long v = ar_ou(&a);
            ar_esp(&a);
            if (a.erro == 2) o_printf(x->err, "bash: divisão por 0\n");
            else if (a.erro || *a.p) o_printf(x->err, "bash: %s: erro de sintaxe na expressão\n", expr);
            snprintf(tmp, sizeof tmp, "%lld", v);
            ex_puts(e, tmp, flag);
            free(expr); free(ex2);
            return q + 2;
        }
    }
    if (*p == '(') {                            /* $( comando ) */
        int inc = 0;
        const char *f = pula_cmdsub(p, &inc);
        if (!inc) {
            char *r = cmdsub(x, p + 1, (size_t)(f - p - 2));
            ex_puts(e, r, flag);
            free(r);
            return f;
        }
    }
    if (*p == '{') {
        const char *f = strchr(p, '}');
        if (f) {
            char *dentro = xmalloc((size_t)(f - p));
            memcpy(dentro, p + 1, (size_t)(f - p - 1)); dentro[f - p - 1] = 0;
            if (dentro[0] == '#' && dentro[1]) {            /* ${#var} */
                const char *v = sh_get(sh, dentro + 1);
                int l = 0;
                for (const char *s = v ? v : ""; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) l++;
                snprintf(tmp, sizeof tmp, "%d", l);
                ex_puts(e, tmp, flag);
            } else {
                size_t k = 0;
                while (isalnum((unsigned char)dentro[k]) || dentro[k] == '_') k++;
                char op[3] = { 0, 0, 0 };
                char *arg = dentro + k;
                if (*arg == ':') { op[0] = ':'; op[1] = arg[1]; arg += 2; }
                else if ((*arg == '#' || *arg == '%')) { op[0] = *arg; if (arg[1] == *arg) { op[1] = *arg; arg += 2; } else arg++; }
                else if (*arg == '-' || *arg == '=' || *arg == '+') { op[0] = ':'; op[1] = *arg; arg++; }
                char *arg_copia = xstrdup(arg);
                dentro[k] = 0;
                const char *v;
                char num[32];
                if (isdigit((unsigned char)dentro[0])) {
                    int i = atoi(dentro);
                    v = i == 0 ? sh->nome0 : (i <= sh->npos ? sh->pos[i - 1] : NULL);
                } else if (!strcmp(dentro, "?") || (k == 0 && dentro[0] == 0 && op[0] == 0)) { snprintf(num, sizeof num, "%d", sh->ultimo); v = num; }
                else v = sh_get(sh, dentro);
                if (op[0] == ':') {
                    int vazio = !v || !*v;
                    char *alt = expandir_texto(x, arg_copia);
                    if (op[1] == '-') ex_puts(e, vazio ? alt : v, flag);
                    else if (op[1] == '=') { if (vazio) { sh_set(sh, dentro, alt, 0); ex_puts(e, alt, flag); } else ex_puts(e, v, flag); }
                    else if (op[1] == '+') { if (!vazio) ex_puts(e, alt, flag); }
                    else if (v) ex_puts(e, v, flag);
                    free(alt);
                } else if (op[0] && v) {
                    /* tira prefixo (#) ou sufixo (%) que casa com o padrão */
                    char *pad = expandir_texto(x, arg_copia);
                    size_t lv = strlen(v);
                    char *copia = xstrdup(v);
                    size_t ini = 0, fim = lv;
                    if (op[0] == '#') {
                        for (size_t i = 0; i <= lv; i++) {
                            size_t t = op[1] ? lv - i : i;
                            char sv = copia[t]; copia[t] = 0;
                            int ok = casa_glob(pad, copia);
                            copia[t] = sv;
                            if (ok) { ini = t; break; }
                        }
                    } else {
                        for (size_t i = 0; i <= lv; i++) {
                            size_t t = op[1] ? i : lv - i;
                            if (casa_glob(pad, copia + t)) { fim = t; break; }
                        }
                    }
                    copia[fim] = 0;
                    ex_puts(e, copia + ini, flag);
                    free(copia); free(pad);
                } else if (v) ex_puts(e, v, flag);
                free(arg_copia);
            }
            free(dentro);
            return f + 1;
        }
    }
    if (*p == '?') { snprintf(tmp, sizeof tmp, "%d", sh->ultimo); ex_puts(e, tmp, flag); return p + 1; }
    if (*p == '#') { snprintf(tmp, sizeof tmp, "%d", sh->npos); ex_puts(e, tmp, flag); return p + 1; }
    if (*p == '$') { snprintf(tmp, sizeof tmp, "%d", 1000 + sh->sid); ex_puts(e, tmp, flag); return p + 1; }
    if (*p == '!') { ex_puts(e, "0", flag); return p + 1; }
    if (*p == '@' || *p == '*') {
        for (int i = 0; i < sh->npos; i++) {
            if (i) { if (*p == '@' && aspas) ex_put(e, ' ', F_QUEBRA); else ex_put(e, ' ', flag); }
            ex_puts(e, sh->pos[i], flag);
        }
        if (aspas && *p == '@' && sh->npos == 0) e->teve_aspas = -1;   /* "$@" vazio some */
        return p + 1;
    }
    if (isdigit((unsigned char)*p)) {
        int i = *p - '0';
        const char *v = i == 0 ? sh->nome0 : (i <= sh->npos ? sh->pos[i - 1] : NULL);
        if (v) ex_puts(e, v, flag);
        return p + 1;
    }
    if (isalpha((unsigned char)*p) || *p == '_') {
        char nome[128]; size_t n = 0;
        while ((isalnum((unsigned char)*p) || *p == '_') && n < sizeof nome - 1) nome[n++] = *p++;
        nome[n] = 0;
        const char *v = sh_get(sh, nome);
        if (!v && !strcmp(nome, "RANDOM")) {
            unsigned short r; host_aleatorio(&r, sizeof r);
            snprintf(tmp, sizeof tmp, "%d", r & 0x7fff);
            v = tmp;
        }
        if (v) ex_puts(e, v, flag);
        return p;
    }
    ex_put(e, '$', aspas ? F_CITADO : 0);
    return p;
}

static void expandir_em(XCtx *x, const char *p, Exp *e, int entre_aspas) {
    if (!entre_aspas && *p == '~' && (p[1] == 0 || p[1] == '/')) {
        const char *casa = sh_get(x->sh, "HOME");
        ex_puts(e, casa ? casa : "/", F_CITADO);
        p++;
    }
    while (*p) {
        char ch = *p;
        if (entre_aspas) {
            if (ch == '\\' && p[1] && strchr("\"$`\\\n", p[1])) { if (p[1] != '\n') ex_put(e, p[1], F_CITADO); p += 2; continue; }
            if (ch == '$') { p = expandir_dolar(x, p + 1, e, 1); continue; }
            if (ch == '`') {
                const char *q = strchr(p + 1, '`');
                if (q) { char *r = cmdsub(x, p + 1, (size_t)(q - p - 1)); ex_puts(e, r, F_CITADO); free(r); p = q + 1; continue; }
            }
            ex_put(e, ch, F_CITADO);
            p++;
            continue;
        }
        if (ch == '\\') {
            if (p[1] == '\n') { p += 2; continue; }
            if (p[1]) { ex_put(e, p[1], F_CITADO); p += 2; } else p++;
            continue;
        }
        if (ch == '\'') {
            const char *q = strchr(p + 1, '\'');
            if (!q) q = p + strlen(p);
            for (const char *s = p + 1; s < q; s++) ex_put(e, *s, F_CITADO);
            if (e->teve_aspas == 0) e->teve_aspas = 1;
            p = *q ? q + 1 : q;
            continue;
        }
        if (ch == '"') {
            /* acha o fim das aspas respeitando escapes e $( ) */
            const char *q = p + 1;
            int inc = 0;
            while (*q && *q != '"') {
                if (*q == '\\' && q[1]) q += 2;
                else if (*q == '$' && q[1] == '(') q = pula_cmdsub(q + 1, &inc);
                else q++;
            }
            char *dentro = xmalloc((size_t)(q - p));
            memcpy(dentro, p + 1, (size_t)(q - p - 1)); dentro[q - p - 1] = 0;
            if (e->teve_aspas == 0) e->teve_aspas = 1;
            expandir_em(x, dentro, e, 1);
            free(dentro);
            p = *q ? q + 1 : q;
            continue;
        }
        if (ch == '$') { p = expandir_dolar(x, p + 1, e, 0); continue; }
        if (ch == '`') {
            const char *q = strchr(p + 1, '`');
            if (q) { char *r = cmdsub(x, p + 1, (size_t)(q - p - 1)); ex_puts(e, r, F_EXP); free(r); p = q + 1; continue; }
        }
        ex_put(e, ch, 0);
        p++;
    }
}

/* expande sem dividir em campos nem aplicar curingas (atribuições, redirecionamentos, case) */
static char *expandir_texto(XCtx *x, const char *raw) {
    Exp e; memset(&e, 0, sizeof e);
    expandir_em(x, raw, &e, 0);
    char *r = xmalloc(e.n + 1);
    if (e.n) memcpy(r, e.c, e.n);
    r[e.n] = 0;
    free(e.c); free(e.f);
    return r;
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* curingas: casa o padrão (com \ protegendo os caracteres citados) contra a árvore de arquivos */
static void glob_rec(Shell *sh, const char *base, const char *resto, Lista *out) {
    while (*resto == '/') resto++;
    if (!*resto) { li_add(out, xstrdup(*base ? base : "/")); return; }
    const char *barra = resto;
    while (*barra && *barra != '/') { if (*barra == '\\' && barra[1]) barra++; barra++; }
    char *comp = xmalloc((size_t)(barra - resto) + 1);
    memcpy(comp, resto, (size_t)(barra - resto)); comp[barra - resto] = 0;
    int tem_curinga = 0;
    for (const char *s = comp; *s; s++) { if (*s == '\\' && s[1]) { s++; continue; } if (strchr("*?[", *s)) tem_curinga = 1; }
    Inode *dir = vfs_achar(sh->cwd, *base ? base : ".", 1);
    if (dir && EH_DIR(dir) && vfs_pode_ler(dir)) {
        if (!tem_curinga) {
            /* tira as barras de proteção */
            char *lit = xmalloc(strlen(comp) + 1); size_t o = 0;
            for (const char *s = comp; *s; s++) { if (*s == '\\' && s[1]) s++; lit[o++] = *s; }
            lit[o] = 0;
            if (!strcmp(lit, ".") || !strcmp(lit, "..") || vfs_filho(dir, lit)) {
                char *novo = (!*base) ? xstrdup(lit) : xasprintf("%s%s%s", base, base[strlen(base) - 1] == '/' ? "" : "/", lit);
                glob_rec(sh, novo, barra, out);
                free(novo);
            }
            free(lit);
        } else {
            vfs_ordenar(dir);
            for (int i = 0; i < dir->nfilhos; i++) {
                const char *nm = dir->filhos[i]->nome;
                if (nm[0] == '.' && comp[0] != '.') continue;
                if (!casa_glob(comp, nm)) continue;
                if (*barra && !EH_DIR(dir->filhos[i]) && !EH_LNK(dir->filhos[i])) continue;
                char *novo = (!*base) ? xstrdup(nm) : xasprintf("%s%s%s", base, base[strlen(base) - 1] == '/' ? "" : "/", nm);
                glob_rec(sh, novo, barra, out);
                free(novo);
            }
        }
    }
    free(comp);
}

/* expande uma palavra em zero ou mais campos */
static void expandir_palavra(XCtx *x, const char *raw, Lista *out) {
    Exp e; memset(&e, 0, sizeof e);
    expandir_em(x, raw, &e, 0);
    size_t i = 0;
    int produziu = 0;
    while (i <= e.n) {
        /* pula separadores */
        while (i < e.n && (((e.f[i] & F_EXP) && isspace((unsigned char)e.c[i])) || (e.f[i] & F_QUEBRA))) i++;
        if (i >= e.n) break;
        size_t j = i;
        while (j < e.n && !(((e.f[j] & F_EXP) && isspace((unsigned char)e.c[j])) || (e.f[j] & F_QUEBRA))) j++;
        /* monta o campo e o padrão de curinga (citados ficam protegidos com \) */
        Buf campo, pad; buf_init(&campo); buf_init(&pad);
        int curinga = 0;
        for (size_t k = i; k < j; k++) {
            buf_putc(&campo, e.c[k]);
            if (e.f[k] & F_CITADO) { if (strchr("*?[]\\", e.c[k])) buf_putc(&pad, '\\'); }
            else if (strchr("*?[", e.c[k])) curinga = 1;
            buf_putc(&pad, e.c[k]);
        }
        int feito = 0;
        if (curinga) {
            Lista g = { 0 };
            char *padrao = buf_str(&pad);
            glob_rec(x->sh, padrao[0] == '/' ? "/" : "", padrao, &g);
            if (g.n) {
                qsort(g.v, (size_t)g.n, sizeof(char *), cmp_str);
                for (int k = 0; k < g.n; k++) li_add(out, g.v[k]);
                free(g.v);
                feito = 1;
            }
        }
        if (!feito) li_add(out, xstrdup(buf_str(&campo)));
        buf_free(&campo); buf_free(&pad);
        produziu = 1;
        i = j;
    }
    if (!produziu && e.teve_aspas == 1) li_add(out, xstrdup(""));
    free(e.c); free(e.f);
}

/* ------------------------------------------------------------------ comandos */

static int exec_no(Shell *sh, No *n, const char *in, size_t inlen, Out *out, Out *err);

const Cmd *cmd_achar(const char *nome) {
    for (int t = 0; cmds_todas[t]; t++)
        for (const Cmd *c = cmds_todas[t]; c->nome; c++)
            if (!strcmp(c->nome, nome)) return c;
    return NULL;
}
static const Cmd *embutido_achar(const char *nome) {
    for (const Cmd *c = cmds_shell; c->nome; c++) if (!strcmp(c->nome, nome)) return c;
    return NULL;
}

char *sh_resolver(Shell *sh, const char *nome) {
    const char *path = sh_get(sh, "PATH");
    if (!path) return NULL;
    while (*path) {
        const char *f = strchr(path, ':');
        size_t n = f ? (size_t)(f - path) : strlen(path);
        if (n) {
            char *cam = xasprintf("%.*s/%s", (int)n, path, nome);
            Inode *i = vfs_achar(sh->cwd, cam, 1);
            if (i && EH_REG(i) && (i->mode & 0111)) return cam;
            free(cam);
        }
        if (!f) break;
        path = f + 1;
    }
    return NULL;
}

static int rodar_arquivo(Ctx *c, const char *caminho, int argc, char **argv) {
    Shell *sh = c->sh;
    Inode *f = vfs_achar(sh->cwd, caminho, 1);
    if (!f) { ERR("bash: %s: %s\n", argv[0], vfs_erro(vfs_err)); return 127; }
    if (EH_DIR(f)) { ERR("bash: %s: É um diretório\n", argv[0]); return 126; }
    if (!(f->mode & 0111)) { ERR("bash: %s: Permissão negada\n", argv[0]); return 126; }
    if (f->tam >= 4 && !memcmp(f->dados, "\0asm", 4)) return wasi_rodar(c, f, argc, argv);
    if (f->tam >= 7 && !memcmp(f->dados, "#!tnux ", 7)) {
        char nome[64]; size_t n = 0;
        const char *p = f->dados + 7;
        while (*p && *p != '\n' && n < sizeof nome - 1) nome[n++] = *p++;
        nome[n] = 0;
        const Cmd *cmd = cmd_achar(nome);
        if (!cmd) { ERR("bash: %s: programa do sistema ausente\n", argv[0]); return 127; }
        return cmd->fn(c, argc, argv);
    }
    if (f->tam >= 4 && !memcmp(f->dados, "\x7f" "ELF", 4)) {
        ERR("bash: %s: não é possível executar o arquivo binário: é um programa Linux nativo (ELF).\n"
            "      No navegador o TNUX roda scripts e programas WebAssembly (.wasm).\n"
            "      Para rodar programas Linux de verdade, use o modo ponte (veja: tnux ajuda ponte).\n", argv[0]);
        return 126;
    }
    /* script de shell: roda num shell filho, como um processo separado */
    if (sh->nivel > 30) { ERR("bash: %s: muitos níveis de scripts\n", argv[0]); return 126; }
    Shell *filho = sh_filho(sh);
    free(filho->nome0);
    filho->nome0 = xstrdup(argv[0]);
    filho->npos = argc - 1;
    filho->pos = xmalloc(sizeof(char *) * (size_t)(argc > 1 ? argc - 1 : 1));
    for (int i = 1; i < argc; i++) filho->pos[i - 1] = xstrdup(argv[i]);
    char *texto = xmalloc(f->tam + 1);
    memcpy(texto, f->dados, f->tam); texto[f->tam] = 0;
    int r = sh_rodar(filho, texto, c->in, c->inlen, c->out, c->err);
    free(texto);
    sh_livrar(filho);
    return r;
}

static char *citar(const char *s) {
    Buf b; buf_init(&b);
    buf_putc(&b, '\'');
    for (; *s; s++) { if (*s == '\'') buf_puts(&b, "'\\''"); else buf_putc(&b, *s); }
    buf_putc(&b, '\'');
    return buf_str(&b);
}

int sh_exec_argv(Ctx *c, int argc, char **argv) {
    Shell *sh = c->sh;
    if (argc == 0) return 0;
    const char *nome = argv[0];
    /* apelidos (não reexpande o mesmo apelido dentro dele) */
    static int no_apelido;
    Var *ap = var_achar(sh->apelidos, sh->napelidos, nome);
    if (ap && no_apelido < 8) {
        Buf b; buf_init(&b);
        buf_puts(&b, ap->v);
        for (int i = 1; i < argc; i++) { char *q = citar(argv[i]); buf_putc(&b, ' '); buf_puts(&b, q); free(q); }
        /* se o apelido começa com o próprio nome (ls='ls --color'), não entra em laço */
        size_t ln = strlen(nome);
        int proprio = !strncmp(ap->v, nome, ln) && (ap->v[ln] == ' ' || ap->v[ln] == 0);
        char *salvo_k = NULL;
        if (proprio) { salvo_k = ap->k; ap->k = xstrdup("\x01"); }
        no_apelido++;
        int r = sh_rodar(sh, buf_str(&b), c->in, c->inlen, c->out, c->err);
        no_apelido--;
        if (proprio) {
            Var *v2 = var_achar(sh->apelidos, sh->napelidos, "\x01");
            if (v2) { free(v2->k); v2->k = salvo_k; } else free(salvo_k);
        }
        buf_free(&b);
        return r;
    }
    for (int i = 0; i < sh->nfuncs; i++) {
        if (strcmp(sh->fnomes[i], nome) || !sh->funcs[i]) continue;
        if (sh->nivel > 60) { ERR("bash: %s: funções aninhadas demais\n", nome); return 1; }
        char **pos_ant = sh->pos; int npos_ant = sh->npos;
        sh->npos = argc - 1;
        sh->pos = xmalloc(sizeof(char *) * (size_t)argc);
        for (int k = 1; k < argc; k++) sh->pos[k - 1] = xstrdup(argv[k]);
        sh->nivel++;
        int r = exec_no(sh, sh->funcs[i], c->in, c->inlen, c->out, c->err);
        sh->nivel--;
        if (sh->retorna) { sh->retorna = 0; r = sh->valor_retorno; }
        for (int k = 0; k < sh->npos; k++) free(sh->pos[k]);
        free(sh->pos);
        sh->pos = pos_ant; sh->npos = npos_ant;
        return r;
    }
    const Cmd *emb = embutido_achar(nome);
    if (emb) return emb->fn(c, argc, argv);
    if (strchr(nome, '/')) return rodar_arquivo(c, nome, argc, argv);
    char *cam = sh_resolver(sh, nome);
    if (cam) {
        int r = rodar_arquivo(c, cam, argc, argv);
        free(cam);
        return r;
    }
    /* como o command-not-found do Mint: sugere o pacote quando conhece */
    Buf b; buf_init(&b);
    Out o = { &b, sh->sid };
    Ctx c2 = { sh, NULL, 0, &o, &o };
    char *args[] = { "apt", "--sugerir", (char *)nome, NULL };
    const Cmd *apt = cmd_achar("apt");
    if (apt && apt->fn(&c2, 3, args) == 0 && b.n) o_write(c->err, b.d, b.n);
    else ERR("%s: comando não encontrado\n", nome);
    buf_free(&b);
    return 127;
}

/* ------------------------------------------------------------------ execução da árvore */

static int eh_atribuicao(const char *s) {
    if (!(isalpha((unsigned char)*s) || *s == '_')) return 0;
    while (isalnum((unsigned char)*s) || *s == '_') s++;
    return *s == '=';
}

static int exec_simples(Shell *sh, No *n, const char *in, size_t inlen, Out *out, Out *err) {
    XCtx x = { sh, err };
    Lista args = { 0 };
    int i = 0, r = 0;
    /* atribuições na frente: VAR=valor [comando] */
    int natr = 0;
    while (natr < n->npals && eh_atribuicao(n->pals[natr])) natr++;
    for (i = natr; i < n->npals; i++) expandir_palavra(&x, n->pals[i], &args);

    /* variáveis: sem comando, valem para o shell; com comando, só durante ele */
    Lista salvos = { 0 };
    for (i = 0; i < natr; i++) {
        char *eq = strchr(n->pals[i], '=');
        char *k = xmalloc((size_t)(eq - n->pals[i]) + 1);
        memcpy(k, n->pals[i], (size_t)(eq - n->pals[i])); k[eq - n->pals[i]] = 0;
        char *v = expandir_texto(&x, eq + 1);
        if (args.n) {
            const char *velho = sh_get(sh, k);
            Var *vv = var_achar(sh->vars, sh->nvars, k);
            li_add(&salvos, xstrdup(k));
            li_add(&salvos, velho ? xstrdup(velho) : NULL);
            li_add(&salvos, xstrdup(vv && vv->exportada ? "1" : "0"));
            sh_set(sh, k, v, 1);
        } else sh_set(sh, k, v, 0);
        free(k); free(v);
    }

    /* redirecionamentos */
    Buf bin, bout, berr; buf_init(&bin); buf_init(&bout); buf_init(&berr);
    Out oout = { &bout, sh->sid }, oerr = { &berr, sh->sid };
    char *arq_out = NULL, *arq_err = NULL;
    int acr_out = 0, acr_err = 0, falhou = 0;
    for (i = 0; i < n->nreds && !falhou; i++) {
        Redir *rd = &n->reds[i];
        if (rd->modo == 3) {
            if (rd->fd == 2 && rd->dup == 1) err = out;
            else if (rd->fd == 1 && rd->dup == 2) out = err;
            continue;
        }
        char *alvo = expandir_texto(&x, rd->alvo);
        if (rd->modo == 0) {
            Inode *f = vfs_achar(sh->cwd, alvo, 1);
            if (!strcmp(alvo, "/dev/null")) { in = ""; inlen = 0; }
            else if (!f) { o_printf(err, "bash: %s: %s\n", alvo, vfs_erro(vfs_err)); falhou = 1; }
            else if (EH_DIR(f)) { o_printf(err, "bash: %s: É um diretório\n", alvo); falhou = 1; }
            else {
                bin.n = 0;
                buf_put(&bin, f->dados ? f->dados : "", f->tam);
                in = bin.d; inlen = bin.n;
            }
            free(alvo);
            continue;
        }
        /* saída: cria o arquivo já, para acusar erro antes de rodar */
        int nulo = !strcmp(alvo, "/dev/null");
        if (!nulo) {
            Inode *f = vfs_criar(sh->cwd, alvo, 0644);
            if (!f || !vfs_pode_escrever(f)) {
                o_printf(err, "bash: %s: %s\n", alvo, vfs_erro(f ? E_ACCES : vfs_err));
                falhou = 1; free(alvo);
                continue;
            }
        }
        int acr = (rd->modo == 2 || rd->modo == 5);
        if (rd->fd == 2 && rd->modo < 4) {
            free(arq_err); arq_err = nulo ? NULL : xstrdup(alvo); acr_err = acr;
            berr.n = 0; err = &oerr;
        } else {
            if (arq_out && !acr_out) vfs_gravar(sh->cwd, arq_out, "", 0, 0);
            free(arq_out); arq_out = nulo ? NULL : xstrdup(alvo); acr_out = acr;
            bout.n = 0; out = &oout;
            if (rd->modo >= 4) err = out;
        }
        free(alvo);
    }

    if (falhou) r = 1;
    else if (args.n) {
        Ctx c = { sh, in, inlen, out, err };
        /* os comandos recebem uma cópia do vetor: podem trocar itens sem atrapalhar a liberação */
        char **av = xmalloc(sizeof(char *) * (size_t)(args.n + 1));
        memcpy(av, args.v, sizeof(char *) * (size_t)(args.n + 1));
        r = sh_exec_argv(&c, args.n, av);
        free(av);
    }

    if (arq_out) { if (vfs_gravar(sh->cwd, arq_out, bout.d ? bout.d : "", bout.n, acr_out)) { } }
    if (arq_err) vfs_gravar(sh->cwd, arq_err, berr.d ? berr.d : "", berr.n, acr_err);
    free(arq_out); free(arq_err);
    buf_free(&bin); buf_free(&bout); buf_free(&berr);

    for (i = 0; i + 2 < salvos.n + 1 && i < salvos.n; i += 3) {
        if (salvos.v[i + 1]) {
            sh_set(sh, salvos.v[i], salvos.v[i + 1], 0);
            Var *vv = var_achar(sh->vars, sh->nvars, salvos.v[i]);
            if (vv) vv->exportada = salvos.v[i + 2][0] == '1';
        } else sh_unset(sh, salvos.v[i]);
    }
    for (i = 0; i < salvos.n; i++) free(salvos.v[i]);
    free(salvos.v);
    li_livrar(&args);
    return r;
}

static int exec_no(Shell *sh, No *n, const char *in, size_t inlen, Out *out, Out *err) {
    int r = 0;
    if (!n) return 0;
    switch (n->tipo) {
    case N_SEQ:
        for (int i = 0; i < n->nf; i++) {
            r = exec_no(sh, n->f[i], in, inlen, out, err);
            sh->ultimo = r;
            if (sh->sair || sh->quebra || sh->continua || sh->retorna) break;
            if (host_interrompido()) { r = 130; sh->quebra = 99; break; }
        }
        return n->nf ? r : sh->ultimo;
    case N_E:
        r = exec_no(sh, n->f[0], in, inlen, out, err);
        sh->ultimo = r;
        if (r == 0 && !sh->sair && n->nf > 1) r = exec_no(sh, n->f[1], in, inlen, out, err);
        return r;
    case N_OU:
        r = exec_no(sh, n->f[0], in, inlen, out, err);
        sh->ultimo = r;
        if (r != 0 && !sh->sair && n->nf > 1) r = exec_no(sh, n->f[1], in, inlen, out, err);
        return r;
    case N_NAO:
        return !exec_no(sh, n->f[0], in, inlen, out, err);
    case N_PIPE: {
        Buf a, b; buf_init(&a); buf_init(&b);
        const char *ein = in; size_t einlen = inlen;
        for (int i = 0; i < n->nf; i++) {
            int ult = i == n->nf - 1;
            b.n = 0;
            Out ob = { &b, sh->sid };
            r = exec_no(sh, n->f[i], ein, einlen, ult ? out : &ob, err);
            Buf t = a; a = b; b = t;       /* a saída vira a entrada do próximo */
            ein = a.d ? a.d : ""; einlen = a.n;
            if (sh->sair) break;
        }
        buf_free(&a); buf_free(&b);
        return r;
    }
    case N_SE:
        r = exec_no(sh, n->f[0], in, inlen, out, err);
        if (sh->sair) return r;
        if (r == 0) return exec_no(sh, n->f[1], in, inlen, out, err);
        if (n->nf > 2) return exec_no(sh, n->f[2], in, inlen, out, err);
        return 0;
    case N_ENQUANTO: {
        int voltas = 0;
        r = 0;
        for (;;) {
            int cnd = exec_no(sh, n->f[0], in, inlen, out, err);
            if (sh->sair) break;
            if ((cnd == 0) == n->ate) break;
            r = exec_no(sh, n->f[1], in, inlen, out, err);
            if (sh->sair || sh->retorna) break;
            if (sh->quebra) { sh->quebra--; break; }
            if (sh->continua) sh->continua = 0;
            if (host_interrompido()) { r = 130; break; }
            if (++voltas > 5000000) { o_printf(err, "bash: laço interrompido depois de 5 milhões de voltas\n"); r = 1; break; }
        }
        return r;
    }
    case N_PARA: {
        XCtx x = { sh, err };
        Lista itens = { 0 };
        for (int i = 0; i < n->npals; i++) expandir_palavra(&x, n->pals[i], &itens);
        r = 0;
        for (int i = 0; i < itens.n; i++) {
            sh_set(sh, n->var, itens.v[i], 0);
            r = exec_no(sh, n->f[0], in, inlen, out, err);
            if (sh->sair || sh->retorna) break;
            if (sh->quebra) { sh->quebra--; break; }
            if (sh->continua) sh->continua = 0;
            if (host_interrompido()) { r = 130; break; }
        }
        li_livrar(&itens);
        return r;
    }
    case N_CASO: {
        XCtx x = { sh, err };
        char *valor = expandir_texto(&x, n->pals[0]);
        r = 0;
        for (int i = 0; i < n->nf; i++) {
            char *pads = xstrdup(n->pals[i + 1]);
            int casou = 0;
            for (char *p = pads; p && !casou;) {
                char *f = strchr(p, 1);
                if (f) *f = 0;
                char *pad = expandir_texto(&x, p);
                casou = casa_glob(pad, valor);
                free(pad);
                p = f ? f + 1 : NULL;
            }
            free(pads);
            if (casou) { r = exec_no(sh, n->f[i], in, inlen, out, err); break; }
        }
        free(valor);
        return r;
    }
    case N_FUNC: {
        if (!n->nf || !n->f[0]) return 0;       /* já foi definida por esta mesma árvore */
        int i;
        for (i = 0; i < sh->nfuncs; i++) if (!strcmp(sh->fnomes[i], n->var)) break;
        if (i == sh->nfuncs) {
            sh->fnomes = xrealloc(sh->fnomes, sizeof(char *) * (size_t)(sh->nfuncs + 1));
            sh->funcs = xrealloc(sh->funcs, sizeof(void *) * (size_t)(sh->nfuncs + 1));
            sh->fnomes[i] = xstrdup(n->var);
            sh->funcs[i] = NULL;
            sh->nfuncs++;
        }
        /* a função ativa não pode ser liberada enquanto roda: fica numa lista de descarte */
        if (sh->funcs[i]) {
            sh->lixo = xrealloc(sh->lixo, sizeof(void *) * (size_t)(sh->nlixo + 1));
            sh->lixo[sh->nlixo++] = sh->funcs[i];
        }
        sh->funcs[i] = n->f[0];                 /* o corpo passa a ser da tabela de funções */
        n->f[0] = NULL;
        return 0;
    }
    case N_SIMPLES:
        return exec_simples(sh, n, in, inlen, out, err);
    }
    return 0;
}

int sh_rodar(Shell *sh, const char *texto, const char *in, size_t inlen, Out *out, Out *err) {
    Lex l;
    lexar(texto, &l);
    P p = { l.t, 0, l.n, 0, 0, "" };
    No *n = p_lista(&p);
    int r;
    if (!p.erro && p.i < p.n) erro_sintaxe(&p);
    if (p.erro || l.incompleto) {
        o_printf(err, "bash: %s\n", p.msg[0] ? p.msg : "erro de sintaxe: aspas ou parênteses sem fechar");
        r = 2;
    } else r = exec_no(sh, n, in, inlen, out, err);
    sh->ultimo = r;
    no_livrar(n);
    lex_livrar(&l);
    return r;
}
void sh_limpar(Shell *sh) {
    for (int i = 0; i < sh->nlixo; i++) no_livrar(sh->lixo[i]);
    free(sh->lixo); sh->lixo = NULL; sh->nlixo = 0;
    sh->retorna = sh->quebra = sh->continua = 0;
}

/* ------------------------------------------------------------------ completar com Tab */

static void comp_add(Lista *l, const char *s) {
    for (int i = 0; i < l->n; i++) if (!strcmp(l->v[i], s)) return;
    li_add(l, xstrdup(s));
}
static char *escapar_nome(const char *s) {
    Buf b; buf_init(&b);
    for (; *s; s++) { if (strchr(" '\"\\$&;|<>()*?[]#`!", *s)) buf_putc(&b, '\\'); buf_putc(&b, *s); }
    return buf_str(&b);
}

char **sh_completar(void *quem, const char *linha, size_t ini, size_t pos, int *n) {
    Shell *sh = quem;
    Lista l = { 0 };
    char *pal = xmalloc(pos - ini + 1);
    size_t o = 0;
    for (size_t i = ini; i < pos; i++) { if (linha[i] == '\\' && i + 1 < pos) i++; pal[o++] = linha[i]; }
    pal[o] = 0;
    /* é a primeira palavra do comando? */
    size_t k = ini;
    while (k > 0 && linha[k - 1] == ' ') k--;
    int primeira = (k == 0) || strchr("|;&(", linha[k - 1]) != NULL;
    if (!primeira && k >= 4 && !strncmp(linha + k - 4, "sudo", 4) && (k == 4 || linha[k - 5] == ' ')) primeira = 1;
    size_t lp = strlen(pal);
    if (primeira && !strchr(pal, '/')) {
        for (const Cmd *c = cmds_shell; c->nome; c++) if (!strncmp(c->nome, pal, lp)) comp_add(&l, c->nome);
        for (int i = 0; i < sh->napelidos; i++) if (!strncmp(sh->apelidos[i].k, pal, lp)) comp_add(&l, sh->apelidos[i].k);
        const char *path = sh_get(sh, "PATH");
        while (path && *path) {
            const char *f = strchr(path, ':');
            size_t tn = f ? (size_t)(f - path) : strlen(path);
            char *d = xasprintf("%.*s", (int)tn, path);
            Inode *dir = vfs_achar(sh->cwd, d, 1);
            if (dir && EH_DIR(dir))
                for (int i = 0; i < dir->nfilhos; i++)
                    if (!strncmp(dir->filhos[i]->nome, pal, lp) && (dir->filhos[i]->mode & 0111)) comp_add(&l, dir->filhos[i]->nome);
            free(d);
            if (!f) break;
            path = f + 1;
        }
    } else {
        /* arquivos: separa diretório e começo do nome */
        char *barra = strrchr(pal, '/');
        char *dirtxt = barra ? xasprintf("%.*s", (int)(barra - pal + 1), pal) : xstrdup("");
        const char *prefixo = barra ? barra + 1 : pal;
        char *dirreal;
        if (dirtxt[0] == '~' && (dirtxt[1] == '/' || dirtxt[1] == 0)) dirreal = xasprintf("%s%s", sh_get(sh, "HOME") ? sh_get(sh, "HOME") : "/", dirtxt + 1);
        else dirreal = xstrdup(*dirtxt ? dirtxt : ".");
        Inode *dir = vfs_achar(sh->cwd, dirreal, 1);
        size_t lpre = strlen(prefixo);
        if (dir && EH_DIR(dir)) {
            for (int i = 0; i < dir->nfilhos; i++) {
                Inode *f = dir->filhos[i];
                if (strncmp(f->nome, prefixo, lpre)) continue;
                if (f->nome[0] == '.' && prefixo[0] != '.') continue;
                Inode *real = EH_LNK(f) ? vfs_achar(dirreal, f->nome, 1) : f;
                char *esc = escapar_nome(f->nome);
                char *cand = xasprintf("%s%s%s", dirtxt, esc, real && EH_DIR(real) ? "/" : "");
                comp_add(&l, cand);
                free(esc); free(cand);
            }
        }
        free(dirtxt); free(dirreal);
    }
    free(pal);
    if (l.n > 1) qsort(l.v, (size_t)l.n, sizeof(char *), cmp_str);
    *n = l.n;
    return l.v;
}

/* ------------------------------------------------------------------ comandos embutidos do shell */

static int c_cd(Ctx *c, int argc, char **argv) {
    Shell *sh = c->sh;
    const char *alvo = argc > 1 ? argv[1] : sh_get(sh, "HOME");
    if (argc > 2) { ERR("bash: cd: número excessivo de argumentos\n"); return 1; }
    if (!alvo) alvo = "/";
    int mostra = 0;
    if (!strcmp(alvo, "-")) {
        alvo = sh_get(sh, "OLDPWD");
        if (!alvo) { ERR("bash: cd: OLDPWD não definida\n"); return 1; }
        mostra = 1;
    }
    Inode *d = vfs_achar(sh->cwd, alvo, 1);
    if (!d) { ERR("bash: cd: %s: %s\n", alvo, vfs_erro(vfs_err)); return 1; }
    if (!EH_DIR(d)) { ERR("bash: cd: %s: Não é um diretório\n", alvo); return 1; }
    if (vfs_euid != 0 && !((d->uid == vfs_euid ? d->mode >> 6 : d->mode) & 1)) { ERR("bash: cd: %s: Permissão negada\n", alvo); return 1; }
    char *novo = vfs_abs(sh->cwd, alvo);
    sh_set(sh, "OLDPWD", sh->cwd, 1);
    free(sh->cwd);
    sh->cwd = novo;
    sh_set(sh, "PWD", novo, 1);
    if (mostra) OUT("%s\n", novo);
    return 0;
}
static int c_pwd(Ctx *c, int argc, char **argv) { (void)argc; (void)argv; OUT("%s\n", c->sh->cwd); return 0; }

static int c_export(Ctx *c, int argc, char **argv) {
    Shell *sh = c->sh;
    if (argc == 1 || !strcmp(argv[1], "-p")) {
        for (int i = 0; i < sh->nvars; i++) if (sh->vars[i].exportada) OUT("declare -x %s=\"%s\"\n", sh->vars[i].k, sh->vars[i].v);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) { *eq = 0; sh_set(sh, argv[i], eq + 1, 1); *eq = '='; }
        else sh_set(sh, argv[i], NULL, 1);
    }
    return 0;
}
static int c_unset(Ctx *c, int argc, char **argv) {
    for (int i = 1; i < argc; i++) sh_unset(c->sh, argv[i]);
    return 0;
}
static int c_set(Ctx *c, int argc, char **argv) {
    Shell *sh = c->sh;
    if (argc == 1) {
        for (int i = 0; i < sh->nvars; i++) OUT("%s=%s\n", sh->vars[i].k, sh->vars[i].v);
        return 0;
    }
    int i = 1;
    if (argv[1][0] == '-' || argv[1][0] == '+') {      /* set -e, set -x...: aceitos e ignorados */
        if (strcmp(argv[1], "--")) return 0;
        i = 2;
    }
    for (int k = 0; k < sh->npos; k++) free(sh->pos[k]);
    free(sh->pos);
    sh->npos = argc - i;
    sh->pos = xmalloc(sizeof(char *) * (size_t)(sh->npos + 1));
    for (int k = 0; k < sh->npos; k++) sh->pos[k] = xstrdup(argv[i + k]);
    return 0;
}
static int c_shift(Ctx *c, int argc, char **argv) {
    Shell *sh = c->sh;
    int n = argc > 1 ? atoi(argv[1]) : 1;
    if (n > sh->npos) return 1;
    for (int i = 0; i < n; i++) free(sh->pos[i]);
    memmove(sh->pos, sh->pos + n, sizeof(char *) * (size_t)(sh->npos - n));
    sh->npos -= n;
    return 0;
}
static int c_alias(Ctx *c, int argc, char **argv) {
    Shell *sh = c->sh;
    if (argc == 1) {
        for (int i = 0; i < sh->napelidos; i++) OUT("alias %s='%s'\n", sh->apelidos[i].k, sh->apelidos[i].v);
        return 0;
    }
    int r = 0;
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) { *eq = 0; apelido_set(sh, argv[i], eq + 1); *eq = '='; }
        else {
            Var *v = var_achar(sh->apelidos, sh->napelidos, argv[i]);
            if (v) OUT("alias %s='%s'\n", v->k, v->v);
            else { ERR("bash: alias: %s: não encontrado\n", argv[i]); r = 1; }
        }
    }
    return r;
}
static int c_unalias(Ctx *c, int argc, char **argv) {
    Shell *sh = c->sh;
    for (int i = 1; i < argc; i++) {
        Var *v = var_achar(sh->apelidos, sh->napelidos, argv[i]);
        if (!v) continue;
        free(v->k); free(v->v);
        *v = sh->apelidos[--sh->napelidos];
    }
    return 0;
}
static int c_exit(Ctx *c, int argc, char **argv) {
    c->sh->sair = 1;
    return argc > 1 ? atoi(argv[1]) & 255 : c->sh->ultimo;
}
static int c_break(Ctx *c, int argc, char **argv) { c->sh->quebra = argc > 1 ? atoi(argv[1]) : 1; return 0; }
static int c_continue(Ctx *c, int argc, char **argv) { (void)argc; (void)argv; c->sh->continua = 1; return 0; }
static int c_return(Ctx *c, int argc, char **argv) {
    c->sh->valor_retorno = argc > 1 ? atoi(argv[1]) & 255 : c->sh->ultimo;
    c->sh->retorna = 1;
    return c->sh->valor_retorno;
}
static int c_local(Ctx *c, int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (argv[i][0] == '-') continue;
        if (eq) { *eq = 0; sh_set(c->sh, argv[i], eq + 1, 0); *eq = '='; }
        else if (!sh_get(c->sh, argv[i])) sh_set(c->sh, argv[i], "", 0);
    }
    return 0;
}
static int c_true(Ctx *c, int argc, char **argv) { (void)c; (void)argc; (void)argv; return 0; }
static int c_false(Ctx *c, int argc, char **argv) { (void)c; (void)argc; (void)argv; return 1; }

static int c_source(Ctx *c, int argc, char **argv) {
    if (argc < 2) { ERR("bash: %s: requer o nome de um arquivo\n", argv[0]); return 2; }
    Inode *f = vfs_achar(c->sh->cwd, argv[1], 1);
    if (!f || EH_DIR(f)) { ERR("bash: %s: %s\n", argv[1], f ? "É um diretório" : vfs_erro(vfs_err)); return 1; }
    char *texto = xmalloc(f->tam + 1);
    memcpy(texto, f->dados ? f->dados : "", f->tam); texto[f->tam] = 0;
    c->sh->nivel++;
    int r = c->sh->nivel > 30 ? 1 : sh_rodar(c->sh, texto, c->in, c->inlen, c->out, c->err);
    c->sh->nivel--;
    free(texto);
    return r;
}
static int c_eval(Ctx *c, int argc, char **argv) {
    Buf b; buf_init(&b);
    for (int i = 1; i < argc; i++) { if (i > 1) buf_putc(&b, ' '); buf_puts(&b, argv[i]); }
    int r = sh_rodar(c->sh, buf_str(&b), c->in, c->inlen, c->out, c->err);
    buf_free(&b);
    return r;
}
/* sh / bash: roda um script, ou "-c texto" */
static int c_sh(Ctx *c, int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "-c")) {
        Shell *f = sh_filho(c->sh);
        f->npos = argc > 4 ? argc - 4 : 0;
        f->pos = xmalloc(sizeof(char *) * (size_t)(f->npos + 1));
        for (int i = 0; i < f->npos; i++) f->pos[i] = xstrdup(argv[4 + i]);
        int r = sh_rodar(f, argv[2], c->in, c->inlen, c->out, c->err);
        sh_livrar(f);
        return r;
    }
    int i = 1;
    while (i < argc && argv[i][0] == '-') i++;
    if (i >= argc) {
        if (c->inlen) {          /* script vindo de um pipe: curl ... | bash */
            Shell *f = sh_filho(c->sh);
            char *texto = xmalloc(c->inlen + 1);
            memcpy(texto, c->in, c->inlen); texto[c->inlen] = 0;
            int r = sh_rodar(f, texto, NULL, 0, c->out, c->err);
            free(texto);
            sh_livrar(f);
            return r;
        }
        return 0;                /* um shell dentro do shell: já estamos nele */
    }
    Inode *f = vfs_achar(c->sh->cwd, argv[i], 1);
    if (!f || EH_DIR(f)) { ERR("%s: %s: %s\n", argv[0], argv[i], f ? "É um diretório" : vfs_erro(vfs_err)); return 127; }
    Shell *filho = sh_filho(c->sh);
    free(filho->nome0);
    filho->nome0 = xstrdup(argv[i]);
    filho->npos = argc - i - 1;
    filho->pos = xmalloc(sizeof(char *) * (size_t)(filho->npos + 1));
    for (int k = 0; k < filho->npos; k++) filho->pos[k] = xstrdup(argv[i + 1 + k]);
    char *texto = xmalloc(f->tam + 1);
    memcpy(texto, f->dados ? f->dados : "", f->tam); texto[f->tam] = 0;
    int r = sh_rodar(filho, texto, c->in, c->inlen, c->out, c->err);
    free(texto);
    sh_livrar(filho);
    return r;
}
static int c_read(Ctx *c, int argc, char **argv) {
    /* lê uma linha da entrada (pipe ou <). Sem entrada, devolve 1 (não há teclado no meio de um comando). */
    int i = 1;
    while (i < argc && argv[i][0] == '-') { if (!strcmp(argv[i], "-p")) i++; i++; }
    if (!c->in || !c->inlen) { for (; i < argc; i++) sh_set(c->sh, argv[i], "", 0); return 1; }
    const char *nl = memchr(c->in, '\n', c->inlen);
    size_t n = nl ? (size_t)(nl - c->in) : c->inlen;
    char *linha = xmalloc(n + 1);
    memcpy(linha, c->in, n); linha[n] = 0;
    if (i >= argc) sh_set(c->sh, "REPLY", linha, 0);
    char *p = linha;
    for (; i < argc; i++) {
        while (*p == ' ' || *p == '\t') p++;
        if (i == argc - 1) { sh_set(c->sh, argv[i], p, 0); break; }
        char *f = p;
        while (*f && *f != ' ' && *f != '\t') f++;
        char sv = *f; *f = 0;
        sh_set(c->sh, argv[i], p, 0);
        *f = sv;
        p = f;
    }
    free(linha);
    return 0;
}
static int c_history(Ctx *c, int argc, char **argv) {
    (void)argc; (void)argv;
    Shell *raiz = k_sessao(c->sh->sid);
    if (!raiz || !raiz->ed) return 0;
    for (int i = 0; i < ed_hist_n(raiz->ed); i++) OUT("%5d  %s\n", i + 1, ed_hist(raiz->ed, i));
    return 0;
}
static int c_type(Ctx *c, int argc, char **argv) {
    int r = 0, so_caminho = !strcmp(argv[0], "which");
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;
        Var *ap = var_achar(c->sh->apelidos, c->sh->napelidos, argv[i]);
        char *cam = sh_resolver(c->sh, argv[i]);
        if (so_caminho) {
            if (cam) OUT("%s\n", cam); else r = 1;
        } else if (ap) OUT("%s está apelidada para `%s'\n", argv[i], ap->v);
        else if (embutido_achar(argv[i])) OUT("%s é um comando interno do shell\n", argv[i]);
        else if (cam) OUT("%s é %s\n", argv[i], cam);
        else { ERR("bash: type: %s: não encontrado\n", argv[i]); r = 1; }
        free(cam);
    }
    return r;
}

/* test / [ / [[ */
typedef struct { Ctx *c; char **a; int n, i; } Tst;
static int t_ou(Tst *t);
static int t_arq(Tst *t, const char *op, const char *cam) {
    Inode *f = vfs_achar(t->c->sh->cwd, cam, op[1] != 'L' && op[1] != 'h');
    if (!f) return 0;
    switch (op[1]) {
    case 'e': return 1;
    case 'f': return EH_REG(f);
    case 'd': return EH_DIR(f);
    case 'L': case 'h': return EH_LNK(f);
    case 's': return f->tam > 0;
    case 'r': return vfs_pode_ler(f);
    case 'w': return vfs_pode_escrever(f);
    case 'x': return (f->mode & 0111) != 0;
    }
    return 0;
}
static int t_prim(Tst *t) {
    if (t->i >= t->n) return 0;
    char *a = t->a[t->i];
    if (!strcmp(a, "!")) { t->i++; return !t_prim(t); }
    if (!strcmp(a, "(")) {
        t->i++;
        int v = t_ou(t);
        if (t->i < t->n && !strcmp(t->a[t->i], ")")) t->i++;
        return v;
    }
    if (a[0] == '-' && a[1] && !a[2] && t->i + 1 < t->n && strchr("efdLhsrwxzn", a[1])) {
        /* operador de um argumento, a não ser que o seguinte seja um operador de dois */
        int binario = t->i + 2 < t->n && (!strcmp(t->a[t->i + 1], "=") || !strcmp(t->a[t->i + 1], "!=") || !strcmp(t->a[t->i + 1], "=="));
        if (!binario) {
            char *v = t->a[t->i + 1];
            t->i += 2;
            if (a[1] == 'z') return !*v;
            if (a[1] == 'n') return *v != 0;
            return t_arq(t, a, v);
        }
    }
    if (t->i + 2 < t->n) {
        char *op = t->a[t->i + 1], *b = t->a[t->i + 2];
        int ok = 1, v = 0;
        if (!strcmp(op, "=") || !strcmp(op, "==")) v = !strcmp(a, b) || (!strcmp(t->a[0], "[[") && casa_glob(b, a));
        else if (!strcmp(op, "!=")) v = strcmp(a, b) != 0;
        else if (!strcmp(op, "-eq")) v = atol(a) == atol(b);
        else if (!strcmp(op, "-ne")) v = atol(a) != atol(b);
        else if (!strcmp(op, "-lt")) v = atol(a) < atol(b);
        else if (!strcmp(op, "-le")) v = atol(a) <= atol(b);
        else if (!strcmp(op, "-gt")) v = atol(a) > atol(b);
        else if (!strcmp(op, "-ge")) v = atol(a) >= atol(b);
        else if (!strcmp(op, "<")) v = strcmp(a, b) < 0;
        else if (!strcmp(op, ">")) v = strcmp(a, b) > 0;
        else ok = 0;
        if (ok) { t->i += 3; return v; }
    }
    t->i++;
    return *a != 0;
}
static int t_e(Tst *t) {
    int v = t_prim(t);
    while (t->i < t->n && (!strcmp(t->a[t->i], "-a") || !strcmp(t->a[t->i], "&&"))) { t->i++; int d = t_prim(t); v = v && d; }
    return v;
}
static int t_ou(Tst *t) {
    int v = t_e(t);
    while (t->i < t->n && (!strcmp(t->a[t->i], "-o") || !strcmp(t->a[t->i], "||"))) { t->i++; int d = t_e(t); v = v || d; }
    return v;
}
static int c_test(Ctx *c, int argc, char **argv) {
    int n = argc;
    if (!strcmp(argv[0], "[")) {
        if (strcmp(argv[argc - 1], "]")) { ERR("bash: [: faltando `]'\n"); return 2; }
        n--;
    } else if (!strcmp(argv[0], "[[")) {
        if (strcmp(argv[argc - 1], "]]")) { ERR("bash: [[: faltando `]]'\n"); return 2; }
        n--;
    }
    if (n <= 1) return 1;
    Tst t = { c, argv, n, 1 };
    return t_ou(&t) ? 0 : 1;
}

static int c_sudo(Ctx *c, int argc, char **argv) {
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "-u") || !strcmp(argv[i], "-g")) i++;
        else if (!strcmp(argv[i], "-i") || !strcmp(argv[i], "-s") || !strcmp(argv[i], "su")) {
            ERR("sudo: neste sistema o root só existe por comando: use \"sudo comando\".\n");
            return 1;
        }
        i++;
    }
    if (i >= argc) { ERR("uso: sudo comando [argumentos]\n"); return 1; }
    uint32_t e = vfs_euid;
    vfs_euid = 0;
    int r = sh_exec_argv(c, argc - i, argv + i);
    vfs_euid = e;
    return r;
}

static int c_help(Ctx *c, int argc, char **argv) {
    if (argc > 1) {
        const Cmd *cmd = cmd_achar(argv[1]);
        if (!cmd) { ERR("%s: não há ajuda para `%s'\n", argv[0], argv[1]); return 1; }
        OUT("\x1b[1mNOME\x1b[0m\n    %s - %s\n\n\x1b[1mUSO\x1b[0m\n    %s\n", cmd->nome, cmd->resumo, cmd->uso ? cmd->uso : cmd->nome);
        return 0;
    }
    OUT("%s %s (%s): comandos disponíveis. Use \"help comando\" para ver o uso.\n\n", TNUX_NOME, TNUX_VERSAO, TNUX_CODINOME);
    static const char *const tit[] = { "Shell", "Arquivos", "Texto", "Sistema", "Rede", "Pacotes" };
    for (int t = 0; cmds_todas[t]; t++) {
        OUT("\x1b[1;32m%s\x1b[0m\n", tit[t]);
        for (const Cmd *cmd = cmds_todas[t]; cmd->nome; cmd++)
            if (cmd->resumo) OUT("  \x1b[1m%-12s\x1b[0m %s\n", cmd->nome, cmd->resumo);
    }
    return 0;
}

const Cmd cmds_shell[] = {
    { "cd", c_cd, "muda de diretório", "cd [diretório | -]" },
    { "pwd", c_pwd, "mostra o diretório atual", "pwd" },
    { "export", c_export, "define variáveis de ambiente", "export NOME=valor" },
    { "unset", c_unset, "apaga variáveis", "unset NOME..." },
    { "set", c_set, "lista as variáveis do shell", "set" },
    { "shift", c_shift, NULL, NULL },
    { "alias", c_alias, "cria apelidos de comandos", "alias nome='comando'" },
    { "unalias", c_unalias, "remove apelidos", "unalias nome" },
    { "source", c_source, "roda um script no shell atual", "source arquivo" },
    { ".", c_source, NULL, NULL },
    { "eval", c_eval, NULL, NULL },
    { "sh", c_sh, "roda um script de shell", "sh script.sh [argumentos] | sh -c 'comandos'" },
    { "bash", c_sh, "roda um script de shell", "bash script.sh [argumentos] | bash -c 'comandos'" },
    { "read", c_read, NULL, NULL },
    { "exit", c_exit, "fecha o terminal", "exit [código]" },
    { "logout", c_exit, NULL, NULL },
    { "return", c_return, NULL, NULL },
    { "local", c_local, NULL, NULL },
    { "declare", c_local, NULL, NULL },
    { "readonly", c_local, NULL, NULL },
    { "break", c_break, NULL, NULL },
    { "continue", c_continue, NULL, NULL },
    { "true", c_true, NULL, NULL },
    { "false", c_false, NULL, NULL },
    { ":", c_true, NULL, NULL },
    { "test", c_test, "avalia uma condição", "test EXPRESSÃO  |  [ EXPRESSÃO ]" },
    { "[", c_test, NULL, NULL },
    { "[[", c_test, NULL, NULL },
    { "history", c_history, "mostra os comandos já digitados", "history" },
    { "type", c_type, "diz o que é um comando", "type nome..." },
    { "which", c_type, "mostra o caminho de um programa", "which nome..." },
    { "sudo", c_sudo, "roda um comando como administrador (root)", "sudo comando [argumentos]" },
    { "help", c_help, "lista os comandos e mostra a ajuda de cada um", "help [comando]" },
    { "man", c_help, "mostra a ajuda de um comando", "man comando" },
    { NULL, NULL, NULL, NULL }
};
