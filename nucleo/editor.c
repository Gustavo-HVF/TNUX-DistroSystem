/*
 * TNUX: editor de linha do terminal.
 *
 * Recebe as teclas byte a byte (como chegam de um terminal em modo cru) e cuida de tudo o que se
 * espera ao digitar um comando: setas, Home/End, apagar, histórico, Tab para completar, Ctrl+A/E/
 * K/U/W/L, acentos em UTF-8 e linhas que passam da largura da tela. É usado tanto pelo terminal
 * do navegador quanto pelo "tnux" no terminal do aparelho.
 */
#include "tnux.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Editor {
    EdEscreve esc; EdCompleta comp; void *quem;
    char *prompt; int plarg;          /* largura visível do prompt */
    char *buf; size_t len, cap, pos;  /* em bytes; pos sempre no começo de um caractere */
    int cols;
    int maxlin, poslarg_ant;          /* estado do último desenho */
    char **hist; int nhist, caphist, ihist;
    char *rascunho;                   /* linha em edição enquanto se anda no histórico */
    int est; char seq[24]; int nseq;  /* leitor de sequências de escape */
    char *linha;                      /* última linha confirmada */
};

static void w(Editor *e, const char *s) { e->esc(e->quem, s, strlen(s)); }
static void wn(Editor *e, const char *s, size_t n) { if (n) e->esc(e->quem, s, n); }

/* largura na tela: conta caracteres UTF-8 e pula sequências de cor */
static int largura(const char *s, size_t n) {
    int l = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == 0x1b && i + 1 < n && s[i + 1] == '[') {
            i += 2;
            while (i < n && !((unsigned char)s[i] >= 0x40 && (unsigned char)s[i] <= 0x7e)) i++;
            continue;
        }
        if (ch == 1 || ch == 2) continue;      /* marcadores \[ \] */
        if ((ch & 0xC0) != 0x80) l++;
    }
    return l;
}

Editor *ed_novo(EdEscreve escreve, EdCompleta completa, void *quem) {
    Editor *e = xmalloc(sizeof *e);
    memset(e, 0, sizeof *e);
    e->esc = escreve; e->comp = completa; e->quem = quem;
    e->cols = 80;
    e->cap = 128;
    e->buf = xmalloc(e->cap);
    e->buf[0] = 0;
    e->prompt = xstrdup("");
    return e;
}
void ed_livrar(Editor *e) {
    if (!e) return;
    for (int i = 0; i < e->nhist; i++) free(e->hist[i]);
    free(e->hist); free(e->prompt); free(e->buf); free(e->rascunho); free(e->linha); free(e);
}
void ed_colunas(Editor *e, int colunas) { e->cols = colunas > 8 ? colunas : 8; }

static void desenhar(Editor *e) {
    char t[32];
    int cols = e->cols;
    int ltxt = largura(e->buf, e->len), lpos = largura(e->buf, e->pos);
    int linhas = (e->plarg + ltxt + cols - 1) / cols;
    int lin_cur_ant = (e->plarg + e->poslarg_ant + cols) / cols;
    int antigas = e->maxlin;
    Buf b; buf_init(&b);
    if (antigas - lin_cur_ant > 0) { snprintf(t, sizeof t, "\x1b[%dB", antigas - lin_cur_ant); buf_puts(&b, t); }
    for (int j = 0; j < antigas - 1; j++) buf_puts(&b, "\r\x1b[0K\x1b[1A");
    buf_puts(&b, "\r\x1b[0K");
    buf_puts(&b, e->prompt);
    buf_put(&b, e->buf, e->len);
    if (e->pos == e->len && e->pos && (lpos + e->plarg) % cols == 0) {
        buf_puts(&b, "\n\r");
        linhas++;
        if (linhas > e->maxlin) e->maxlin = linhas;
    }
    int lin_cur = (e->plarg + lpos + cols) / cols;
    if (linhas - lin_cur > 0) { snprintf(t, sizeof t, "\x1b[%dA", linhas - lin_cur); buf_puts(&b, t); }
    int col = (e->plarg + lpos) % cols;
    if (col) { snprintf(t, sizeof t, "\r\x1b[%dC", col); buf_puts(&b, t); }
    else buf_puts(&b, "\r");
    if (linhas > e->maxlin) e->maxlin = linhas;
    e->poslarg_ant = lpos;
    wn(e, b.d, b.n);
    buf_free(&b);
}

void ed_prompt(Editor *e, const char *prompt) {
    free(e->prompt);
    e->prompt = xstrdup(prompt);
    e->plarg = largura(prompt, strlen(prompt));
    /* só a última linha do prompt conta para a posição do cursor */
    const char *nl = strrchr(prompt, '\n');
    if (nl) e->plarg = largura(nl + 1, strlen(nl + 1));
    e->len = e->pos = 0;
    e->buf[0] = 0;
    e->maxlin = 0; e->poslarg_ant = 0;
    e->ihist = e->nhist;
    free(e->rascunho); e->rascunho = NULL;
    e->est = 0;
    desenhar(e);
}

static void garante(Editor *e, size_t mais) {
    if (e->len + mais + 1 > e->cap) {
        while (e->len + mais + 1 > e->cap) e->cap *= 2;
        e->buf = xrealloc(e->buf, e->cap);
    }
}
static void inserir(Editor *e, const char *s, size_t n) {
    garante(e, n);
    memmove(e->buf + e->pos + n, e->buf + e->pos, e->len - e->pos);
    memcpy(e->buf + e->pos, s, n);
    e->len += n; e->pos += n;
    e->buf[e->len] = 0;
}
static void definir(Editor *e, const char *s) {
    size_t n = strlen(s);
    e->len = e->pos = 0;
    garante(e, n);
    memcpy(e->buf, s, n + 1);
    e->len = e->pos = n;
}
static size_t ant(Editor *e, size_t p) {
    if (p == 0) return 0;
    p--;
    while (p > 0 && ((unsigned char)e->buf[p] & 0xC0) == 0x80) p--;
    return p;
}
static size_t prox(Editor *e, size_t p) {
    if (p >= e->len) return e->len;
    p++;
    while (p < e->len && ((unsigned char)e->buf[p] & 0xC0) == 0x80) p++;
    return p;
}
static void apagar(Editor *e, size_t de, size_t ate) {
    if (ate <= de) return;
    memmove(e->buf + de, e->buf + ate, e->len - ate);
    e->len -= ate - de;
    e->buf[e->len] = 0;
    e->pos = de;
}

void ed_hist_add(Editor *e, const char *linha) {
    if (!*linha) return;
    if (e->nhist && !strcmp(e->hist[e->nhist - 1], linha)) return;
    if (e->nhist == e->caphist) {
        e->caphist = e->caphist ? e->caphist * 2 : 64;
        e->hist = xrealloc(e->hist, sizeof(char *) * (size_t)e->caphist);
    }
    if (e->nhist >= 2000) {
        free(e->hist[0]);
        memmove(e->hist, e->hist + 1, sizeof(char *) * (size_t)(e->nhist - 1));
        e->nhist--;
    }
    e->hist[e->nhist++] = xstrdup(linha);
    e->ihist = e->nhist;
}
int ed_hist_n(Editor *e) { return e->nhist; }
const char *ed_hist(Editor *e, int i) { return i >= 0 && i < e->nhist ? e->hist[i] : NULL; }
const char *ed_linha(Editor *e) { return e->linha ? e->linha : ""; }

static void historico(Editor *e, int passo) {
    int novo = e->ihist + passo;
    if (novo < 0 || novo > e->nhist) return;
    if (e->ihist == e->nhist) { free(e->rascunho); e->rascunho = xstrdup(e->buf); }
    e->ihist = novo;
    definir(e, novo == e->nhist ? (e->rascunho ? e->rascunho : "") : e->hist[novo]);
    desenhar(e);
}

static void completar(Editor *e) {
    if (!e->comp) return;
    size_t ini = e->pos;
    while (ini > 0 && e->buf[ini - 1] != ' ' && e->buf[ini - 1] != '\t' && e->buf[ini - 1] != ';' &&
           e->buf[ini - 1] != '|' && e->buf[ini - 1] != '&' && e->buf[ini - 1] != '>' && e->buf[ini - 1] != '<')
        ini--;
    int n = 0;
    char **ops = e->comp(e->quem, e->buf, ini, e->pos, &n);
    if (!ops || n == 0) { w(e, "\a"); free(ops); return; }
    /* prefixo comum */
    size_t comum = strlen(ops[0]);
    for (int i = 1; i < n; i++) {
        size_t j = 0;
        while (j < comum && ops[i][j] == ops[0][j]) j++;
        comum = j;
    }
    while (comum > 0 && ((unsigned char)ops[0][comum] & 0xC0) == 0x80) comum--;
    size_t atual = e->pos - ini;
    if (comum > atual || n == 1) {
        apagar(e, ini, e->pos);
        inserir(e, ops[0], comum);
        if (n == 1 && comum && ops[0][comum - 1] != '/') inserir(e, " ", 1);
        desenhar(e);
    } else {
        /* várias opções: lista em colunas e redesenha a linha */
        size_t salva = e->pos;
        e->pos = e->len;
        desenhar(e);
        int maior = 0;
        for (int i = 0; i < n; i++) {
            int l = largura(ops[i], strlen(ops[i]));
            if (l > maior) maior = l;
        }
        int porlin = e->cols / (maior + 2);
        if (porlin < 1) porlin = 1;
        Buf b; buf_init(&b);
        buf_puts(&b, "\r\n");
        int mostra = n > 200 ? 200 : n;
        for (int i = 0; i < mostra; i++) {
            buf_puts(&b, ops[i]);
            if ((i + 1) % porlin == 0 || i == mostra - 1) buf_puts(&b, "\r\n");
            else for (int k = largura(ops[i], strlen(ops[i])); k < maior + 2; k++) buf_putc(&b, ' ');
        }
        if (mostra < n) buf_printf(&b, "... e mais %d\r\n", n - mostra);
        wn(e, b.d, b.n);
        buf_free(&b);
        e->pos = salva;
        e->maxlin = 0; e->poslarg_ant = 0;
        desenhar(e);
    }
    for (int i = 0; i < n; i++) free(ops[i]);
    free(ops);
}

static void palavra_esq(Editor *e) {
    while (e->pos > 0 && e->buf[e->pos - 1] == ' ') e->pos--;
    while (e->pos > 0 && e->buf[e->pos - 1] != ' ') e->pos--;
}
static void palavra_dir(Editor *e) {
    while (e->pos < e->len && e->buf[e->pos] == ' ') e->pos++;
    while (e->pos < e->len && e->buf[e->pos] != ' ') e->pos++;
}

static void tecla_csi(Editor *e, const char *seq, int n) {
    char fim = seq[n - 1];
    int p1 = atoi(seq);
    int ctrl = strstr(seq, ";5") != NULL || strstr(seq, ";3") != NULL;
    switch (fim) {
    case 'A': historico(e, -1); return;
    case 'B': historico(e, +1); return;
    case 'C': if (ctrl) palavra_dir(e); else e->pos = prox(e, e->pos); break;
    case 'D': if (ctrl) palavra_esq(e); else e->pos = ant(e, e->pos); break;
    case 'H': e->pos = 0; break;
    case 'F': e->pos = e->len; break;
    case '~':
        if (p1 == 1 || p1 == 7) e->pos = 0;
        else if (p1 == 4 || p1 == 8) e->pos = e->len;
        else if (p1 == 3) apagar(e, e->pos, prox(e, e->pos));
        else return;
        break;
    default: return;
    }
    desenhar(e);
}

int ed_byte(Editor *e, unsigned char ch) {
    if (e->est == 1) {                       /* depois de ESC */
        if (ch == '[') { e->est = 2; e->nseq = 0; return ED_NADA; }
        if (ch == 'O') { e->est = 3; return ED_NADA; }
        e->est = 0;
        if (ch == 'b') { palavra_esq(e); desenhar(e); }
        else if (ch == 'f') { palavra_dir(e); desenhar(e); }
        else if (ch == 127 || ch == 8) { size_t f = e->pos; palavra_esq(e); size_t i = e->pos; e->pos = f; apagar(e, i, f); desenhar(e); }
        return ED_NADA;
    }
    if (e->est == 2) {                       /* ESC [ ... */
        if (e->nseq < (int)sizeof e->seq - 1) e->seq[e->nseq++] = (char)ch;
        if (ch >= 0x40 && ch <= 0x7e) {
            e->seq[e->nseq] = 0;
            e->est = 0;
            tecla_csi(e, e->seq, e->nseq);
        }
        return ED_NADA;
    }
    if (e->est == 3) {                       /* ESC O x */
        e->est = 0;
        char s[2] = { (char)ch, 0 };
        tecla_csi(e, s, 1);
        return ED_NADA;
    }
    switch (ch) {
    case 0x1b: e->est = 1; return ED_NADA;
    case '\r': case '\n': {
        e->pos = e->len;
        desenhar(e);
        w(e, "\r\n");
        free(e->linha);
        e->linha = xstrdup(e->buf);
        e->len = e->pos = 0; e->buf[0] = 0;
        e->maxlin = 0; e->poslarg_ant = 0;
        return ED_LINHA;
    }
    case 3:                                   /* Ctrl+C */
        e->pos = e->len;
        desenhar(e);
        w(e, "^C\r\n");
        e->len = e->pos = 0; e->buf[0] = 0;
        e->maxlin = 0; e->poslarg_ant = 0;
        return ED_INTERROMPE;
    case 4:                                   /* Ctrl+D */
        if (e->len == 0) return ED_EOF;
        apagar(e, e->pos, prox(e, e->pos));
        break;
    case 127: case 8:
        if (e->pos == 0) return ED_NADA;
        apagar(e, ant(e, e->pos), e->pos);
        break;
    case 1: e->pos = 0; break;                /* Ctrl+A */
    case 5: e->pos = e->len; break;           /* Ctrl+E */
    case 2: e->pos = ant(e, e->pos); break;   /* Ctrl+B */
    case 6: e->pos = prox(e, e->pos); break;  /* Ctrl+F */
    case 11: apagar(e, e->pos, e->len); break;            /* Ctrl+K */
    case 21: { size_t f = e->pos; apagar(e, 0, f); break; } /* Ctrl+U */
    case 23: { size_t f = e->pos; palavra_esq(e); size_t i = e->pos; e->pos = f; apagar(e, i, f); break; } /* Ctrl+W */
    case 12:                                  /* Ctrl+L */
        w(e, "\x1b[H\x1b[2J");
        e->maxlin = 0; e->poslarg_ant = 0;
        break;
    case 16: historico(e, -1); return ED_NADA; /* Ctrl+P */
    case 14: historico(e, +1); return ED_NADA; /* Ctrl+N */
    case '\t': completar(e); return ED_NADA;
    default:
        if (ch < 32) return ED_NADA;
        {
            char s = (char)ch;
            inserir(e, &s, 1);
            /* caminho rápido: digitando no fim de uma linha que cabe na tela */
            if ((ch & 0xC0) == 0x80 || ch < 0x80) {
                int tot = e->plarg + largura(e->buf, e->len);
                if (e->pos == e->len && tot < e->cols && e->maxlin <= 1) {
                    /* bytes de continuação UTF-8 saem junto com o primeiro: redesenhar é mais seguro */
                    if (ch < 0x80) { wn(e, &s, 1); e->poslarg_ant = largura(e->buf, e->pos); if (e->maxlin < 1) e->maxlin = 1; return ED_NADA; }
                }
            } else return ED_NADA;            /* primeiro byte de um caractere UTF-8: espera o resto */
            /* só redesenha quando o caractere UTF-8 está completo */
            {
                size_t i = e->pos;
                int cont = 0;
                while (i > 0 && ((unsigned char)e->buf[i - 1] & 0xC0) == 0x80) { i--; cont++; }
                if (i > 0) {
                    unsigned char l = (unsigned char)e->buf[i - 1];
                    int precisa = l >= 0xF0 ? 3 : l >= 0xE0 ? 2 : l >= 0xC0 ? 1 : 0;
                    if (cont < precisa) return ED_NADA;
                }
            }
        }
        break;
    }
    desenhar(e);
    return ED_NADA;
}
