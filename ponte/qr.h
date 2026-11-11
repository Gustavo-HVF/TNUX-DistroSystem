/*
 * TNUX: gerador de QR code (modo byte, correção de erros nível M, versões 1 a 15: até 412 bytes).
 * Só a libc. Usado para mostrar o link do túnel no terminal e na área de trabalho.
 *
 * A montagem segue a norma ISO/IEC 18004 e o roteiro da implementação de referência de
 * Project Nayuki (MIT): padrões fixos, Reed-Solomon em GF(256), entrelaçamento dos blocos,
 * zigue-zague dos dados e escolha da máscara pela menor penalidade.
 */
#ifndef TNUX_QR_H
#define TNUX_QR_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define QR_MAX 77                     /* lado da versão 15 */

static int qr_mascara_fixa = -1;      /* só para os testes: força uma máscara */

typedef struct { int lado; uint8_t m[QR_MAX][QR_MAX], fixo[QR_MAX][QR_MAX]; } Qr;

/* nível M, versões 1..15: total de palavras, palavras de correção por bloco, número de blocos */
static const short QR_TOTAL[16] = { 0, 26, 44, 70, 100, 134, 172, 196, 242, 292, 346, 404, 466, 532, 581, 655 };
static const char QR_EC[16] = { 0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24 };
static const char QR_BLOCOS[16] = { 0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10 };
static const unsigned char QR_ALIN[16][5] = {
    { 0 }, { 0 }, { 6, 18 }, { 6, 22 }, { 6, 26 }, { 6, 30 }, { 6, 34 }, { 6, 22, 38 }, { 6, 24, 42 }, { 6, 26, 46 },
    { 6, 28, 50 }, { 6, 30, 54 }, { 6, 32, 58 }, { 6, 34, 62 }, { 6, 26, 46, 66 }, { 6, 26, 48, 70 },
};

static uint8_t qr_mul(uint8_t x, uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; i--) { z = (z << 1) ^ ((z >> 7) * 0x11D); z ^= ((y >> i) & 1) * x; }
    return (uint8_t)z;
}
static void qr_pos(Qr *q, int x, int y, int escuro) { q->m[y][x] = (uint8_t)escuro; q->fixo[y][x] = 1; }

static void qr_formato(Qr *q, int mascara) {
    int dados = (0 << 3) | mascara;               /* nível M = 00 */
    int resto = dados;
    for (int i = 0; i < 10; i++) resto = (resto << 1) ^ ((resto >> 9) * 0x537);
    int b = ((dados << 10) | resto) ^ 0x5412, n = q->lado;
#define BIT(i) ((b >> (i)) & 1)
    for (int i = 0; i <= 5; i++) qr_pos(q, 8, i, BIT(i));
    qr_pos(q, 8, 7, BIT(6)); qr_pos(q, 8, 8, BIT(7)); qr_pos(q, 7, 8, BIT(8));
    for (int i = 9; i < 15; i++) qr_pos(q, 14 - i, 8, BIT(i));
    for (int i = 0; i < 8; i++) qr_pos(q, n - 1 - i, 8, BIT(i));
    for (int i = 8; i < 15; i++) qr_pos(q, 8, n - 15 + i, BIT(i));
    qr_pos(q, 8, n - 8, 1);
#undef BIT
}

static int qr_mascara(int k, int x, int y) {
    switch (k) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (x / 3 + y / 2) % 2 == 0;
    case 5: return x * y % 2 + x * y % 3 == 0;
    case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
    default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}
static void qr_aplicar(Qr *q, int k) {
    for (int y = 0; y < q->lado; y++)
        for (int x = 0; x < q->lado; x++)
            if (!q->fixo[y][x] && qr_mascara(k, x, y)) q->m[y][x] ^= 1;
}

static long qr_penalidade(Qr *q) {
    long p = 0;
    int n = q->lado, escuros = 0;
    for (int eixo = 0; eixo < 2; eixo++) {
        for (int a = 0; a < n; a++) {
            int corrida = 0, cor = -1;
            for (int b = 0; b < n; b++) {
                int v = eixo ? q->m[b][a] : q->m[a][b];
                if (v == cor) { corrida++; if (corrida == 5) p += 3; else if (corrida > 5) p++; }
                else { cor = v; corrida = 1; }
                /* parecido com o padrão de localização: 1011101 com 4 claros de um lado */
                if (b >= 10) {
                    int s[11];
                    for (int k = 0; k < 11; k++) s[k] = eixo ? q->m[b - 10 + k][a] : q->m[a][b - 10 + k];
                    static const int P1[11] = { 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0 }, P2[11] = { 0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1 };
                    int ig1 = 1, ig2 = 1;
                    for (int k = 0; k < 11; k++) { if (s[k] != P1[k]) ig1 = 0; if (s[k] != P2[k]) ig2 = 0; }
                    if (ig1) p += 40;
                    if (ig2) p += 40;
                }
            }
        }
    }
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            escuros += q->m[y][x];
            if (x + 1 < n && y + 1 < n && q->m[y][x] == q->m[y][x + 1] && q->m[y][x] == q->m[y + 1][x] && q->m[y][x] == q->m[y + 1][x + 1]) p += 3;
        }
    int total = n * n;
    int k = (abs(escuros * 20 - total * 10) + total - 1) / total - 1;
    p += (long)k * 10;
    return p;
}

/* monta o QR code de "txt". Devolve 0 se coube. */
static int qr_gerar(Qr *q, const char *txt) {
    size_t n = strlen(txt);
    int v;
    for (v = 1; v <= 15; v++) {
        int dados = QR_TOTAL[v] - QR_EC[v] * QR_BLOCOS[v];
        int bits = 4 + (v < 10 ? 8 : 16) + 8 * (int)n;
        if (bits <= dados * 8) break;
    }
    if (v > 15) return -1;
    memset(q, 0, sizeof *q);
    int lado = q->lado = 17 + 4 * v;
    int ndados = QR_TOTAL[v] - QR_EC[v] * QR_BLOCOS[v];

    /* 1. os bits: modo byte, tamanho, texto, terminador e enchimento */
    uint8_t cw[700];
    memset(cw, 0, sizeof cw);
    int pos = 0;
#define PUT(val, nb) do { for (int i_ = (nb) - 1; i_ >= 0; i_--) { if (((val) >> i_) & 1) cw[pos >> 3] |= (uint8_t)(0x80 >> (pos & 7)); pos++; } } while (0)
    PUT(4, 4);
    PUT((int)n, v < 10 ? 8 : 16);
    for (size_t i = 0; i < n; i++) PUT((unsigned char)txt[i], 8);
    int term = ndados * 8 - pos;
    PUT(0, term < 4 ? term : 4);
    pos = (pos + 7) & ~7;
    for (int pad = 0xEC; pos < ndados * 8; pad ^= 0xEC ^ 0x11) PUT(pad, 8);
#undef PUT

    /* 2. Reed-Solomon por bloco e entrelaçamento */
    int ec = QR_EC[v], nb = QR_BLOCOS[v], curtos = nb - (QR_TOTAL[v] % nb), lcurto = QR_TOTAL[v] / nb - ec;
    uint8_t div[32] = { 0 }, ecs[10][32];
    div[ec - 1] = 1;
    uint8_t raiz = 1;
    for (int i = 0; i < ec; i++) {
        for (int j = 0; j < ec; j++) { div[j] = qr_mul(div[j], raiz); if (j + 1 < ec) div[j] ^= div[j + 1]; }
        raiz = qr_mul(raiz, 2);
    }
    int ini[10], tam[10], o = 0;
    for (int b = 0; b < nb; b++) {
        ini[b] = o; tam[b] = lcurto + (b >= curtos);
        uint8_t *r = ecs[b];
        memset(r, 0, 32);
        for (int i = 0; i < tam[b]; i++) {
            uint8_t f = cw[o + i] ^ r[0];
            memmove(r, r + 1, (size_t)ec - 1);
            r[ec - 1] = 0;
            for (int j = 0; j < ec; j++) r[j] ^= qr_mul(div[j], f);
        }
        o += tam[b];
    }
    uint8_t fim[700];
    int k = 0;
    for (int i = 0; i <= lcurto; i++)
        for (int b = 0; b < nb; b++) if (i < tam[b]) fim[k++] = cw[ini[b] + i];
    for (int i = 0; i < ec; i++)
        for (int b = 0; b < nb; b++) fim[k++] = ecs[b][i];

    /* 3. padrões fixos */
    for (int i = 0; i < lado; i++) { qr_pos(q, 6, i, i % 2 == 0); qr_pos(q, i, 6, i % 2 == 0); }
    int cx[3] = { 3, lado - 4, 3 }, cy[3] = { 3, 3, lado - 4 };
    for (int f = 0; f < 3; f++)
        for (int dy = -4; dy <= 4; dy++)
            for (int dx = -4; dx <= 4; dx++) {
                int x = cx[f] + dx, y = cy[f] + dy, d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
                if (x >= 0 && x < lado && y >= 0 && y < lado) qr_pos(q, x, y, d != 2 && d != 4);
            }
    int na = 0;
    while (na < 5 && QR_ALIN[v][na]) na++;
    for (int i = 0; i < na; i++)
        for (int j = 0; j < na; j++) {
            if ((i == 0 && j == 0) || (i == 0 && j == na - 1) || (i == na - 1 && j == 0)) continue;
            for (int dy = -2; dy <= 2; dy++)
                for (int dx = -2; dx <= 2; dx++) {
                    int d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
                    qr_pos(q, QR_ALIN[v][i] + dx, QR_ALIN[v][j] + dy, d != 1);
                }
        }
    qr_formato(q, 0);
    if (v >= 7) {
        int resto = v;
        for (int i = 0; i < 12; i++) resto = (resto << 1) ^ ((resto >> 11) * 0x1F25);
        long bits = ((long)v << 12) | resto;
        for (int i = 0; i < 18; i++) {
            int bit = (bits >> i) & 1, a = lado - 11 + i % 3, b = i / 3;
            qr_pos(q, a, b, bit); qr_pos(q, b, a, bit);
        }
    }

    /* 4. os dados, em zigue-zague de duas colunas, de baixo para cima e de volta */
    int i = 0, total = k * 8;
    for (int dir = lado - 1; dir >= 1; dir -= 2) {
        if (dir == 6) dir = 5;
        for (int vert = 0; vert < lado; vert++)
            for (int j = 0; j < 2; j++) {
                int x = dir - j, sobe = ((dir + 1) & 2) == 0, y = sobe ? lado - 1 - vert : vert;
                if (!q->fixo[y][x] && i < total) { q->m[y][x] = (fim[i >> 3] >> (7 - (i & 7))) & 1; i++; }
            }
    }

    /* 5. a máscara de menor penalidade */
    int melhor = 0;
    long menor = -1;
    for (int mk = 0; mk < 8; mk++) {
        qr_aplicar(q, mk);
        qr_formato(q, mk);
        long p = qr_penalidade(q);
        if (menor < 0 || p < menor) { menor = p; melhor = mk; }
        qr_aplicar(q, mk);
    }
    if (qr_mascara_fixa >= 0) melhor = qr_mascara_fixa;
    qr_aplicar(q, melhor);
    qr_formato(q, melhor);
    return 0;
}

/* escreve o QR no terminal com meios-blocos (duas linhas de módulos por linha de texto), com borda clara */
__attribute__((unused)) static void qr_terminal(const Qr *q, FILE *f) {
    int borda = 2, n = q->lado;
    for (int y = -borda; y < n + borda; y += 2) {
        fputs("  \x1b[30;107m", f);
        for (int x = -borda; x < n + borda; x++) {
            int a = x >= 0 && x < n && y >= 0 && y < n && q->m[y][x];
            int b = x >= 0 && x < n && y + 1 >= 0 && y + 1 < n && q->m[y + 1][x];
            fputs(a && b ? "\xe2\x96\x88" : a ? "\xe2\x96\x80" : b ? "\xe2\x96\x84" : " ", f);
        }
        fputs("\x1b[0m\n", f);
    }
}

/* SVG do QR (um retângulo por sequência de módulos escuros numa linha) */
__attribute__((unused)) static char *qr_svg(const Qr *q, size_t *tam) {
    int borda = 4, n = q->lado, lado = n + 2 * borda;
    size_t cap = 512 + (size_t)n * n * 40, o = 0;
    char *s = malloc(cap);
    if (!s) return NULL;
    o += (size_t)snprintf(s + o, cap - o, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" shape-rendering=\"crispEdges\">"
                          "<rect width=\"%d\" height=\"%d\" fill=\"#fff\"/><path fill=\"#000\" d=\"", lado, lado, lado, lado);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n;) {
            if (!q->m[y][x]) { x++; continue; }
            int x0 = x;
            while (x < n && q->m[y][x]) x++;
            o += (size_t)snprintf(s + o, cap - o, "M%d %dh%dv1h-%dz", x0 + borda, y + borda, x - x0, x - x0);
        }
    o += (size_t)snprintf(s + o, cap - o, "\"/></svg>");
    *tam = o;
    return s;
}
#endif
