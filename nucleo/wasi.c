/*
 * TNUX: programas WebAssembly (WASI).
 *
 * Um arquivo .wasm no disco do TNUX é um programa de verdade: o host cria uma instância dele e as
 * chamadas de sistema do WASI (ler, escrever, abrir arquivo...) chegam aqui, onde são atendidas
 * pelo sistema de arquivos e pelo terminal do TNUX. É assim que dá para baixar e executar
 * programas compilados (de C, C++, Rust, Go...) dentro do navegador.
 */
#include "cmds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* números de erro do WASI */
enum { W_ACCES = 2, W_BADF = 8, W_EXIST = 20, W_INVAL = 28, W_ISDIR = 31, W_NOENT = 44, W_NOTDIR = 54, W_NOTEMPTY = 55 };

typedef struct { Inode *no; size_t pos; int escrita, acrescenta, usado; } Desc;
#define MAX_DESC 64
static Desc descs[MAX_DESC];
static Ctx *wctx;
static size_t wpos_in;

static int w_erro(void) {
    switch (vfs_err) {
    case E_NOENT: return -W_NOENT; case E_EXIST: return -W_EXIST; case E_NOTDIR: return -W_NOTDIR;
    case E_ISDIR: return -W_ISDIR; case E_NOTEMPTY: return -W_NOTEMPTY; case E_ACCES: return -W_ACCES;
    default: return -W_INVAL;
    }
}

int wasi_rodar(Ctx *c, Inode *prog, int argc, char **argv) {
    Buf j; buf_init(&j);
    buf_puts(&j, "{\"args\":[");
    for (int i = 0; i < argc; i++) { if (i) buf_putc(&j, ','); buf_json_str(&j, argv[i]); }
    buf_puts(&j, "],\"env\":[");
    int n = 0;
    for (int i = 0; i < c->sh->nvars; i++) {
        if (!c->sh->vars[i].exportada) continue;
        char *kv = xasprintf("%s=%s", c->sh->vars[i].k, c->sh->vars[i].v);
        if (n++) buf_putc(&j, ',');
        buf_json_str(&j, kv);
        free(kv);
    }
    buf_puts(&j, "]}");
    Ctx *ant = wctx; size_t pos_ant = wpos_in;
    Desc salvos[MAX_DESC];
    memcpy(salvos, descs, sizeof descs);
    memset(descs, 0, sizeof descs);
    wctx = c; wpos_in = 0;
    /* copia o programa: o arquivo pode mudar (ou sumir) enquanto ele roda */
    char *copia = xmalloc(prog->tam ? prog->tam : 1);
    memcpy(copia, prog->dados, prog->tam);
    int r = host_wasm(copia, prog->tam, buf_str(&j));
    free(copia);
    wctx = ant; wpos_in = pos_ant;
    memcpy(descs, salvos, sizeof descs);
    buf_free(&j);
    if (r == -1) {
        ERR("bash: %s: programas WebAssembly rodam no TNUX do navegador (aqui no terminal, use o modo ponte para programas nativos)\n", argv[0]);
        return 126;
    }
    return r;
}

int k_wasi_write(int fd, const char *p, int n) {
    if (!wctx || n < 0) return -W_BADF;
    if (fd == 1) { o_write(wctx->out, p, (size_t)n); return n; }
    if (fd == 2) { o_write(wctx->err, p, (size_t)n); return n; }
    if (fd < 3 || fd >= MAX_DESC || !descs[fd].usado || !descs[fd].escrita) return -W_BADF;
    Desc *d = &descs[fd];
    Inode *f = d->no;
    if (d->acrescenta) d->pos = f->tam;
    size_t fim = d->pos + (size_t)n;
    if (fim + 1 > f->cap) {
        size_t cap = (fim + 1) * 2;
        char *nv = realloc(f->dados, cap);
        if (!nv) return -W_INVAL;
        f->dados = nv; f->cap = cap;
    }
    if (d->pos > f->tam) memset(f->dados + f->tam, 0, d->pos - f->tam);
    memcpy(f->dados + d->pos, p, (size_t)n);
    if (fim > f->tam) f->tam = fim;
    f->dados[f->tam] = 0;
    f->mtime = k_agora();
    d->pos = fim;
    vfs_sujo = 1;
    return n;
}
int k_wasi_read(int fd, char *p, int n) {
    if (!wctx || n < 0) return -W_BADF;
    if (fd == 0) {
        size_t resta = wctx->in && wctx->inlen > wpos_in ? wctx->inlen - wpos_in : 0;
        size_t q = resta < (size_t)n ? resta : (size_t)n;
        if (q) memcpy(p, wctx->in + wpos_in, q);
        wpos_in += q;
        return (int)q;
    }
    if (fd < 3 || fd >= MAX_DESC || !descs[fd].usado) return -W_BADF;
    Desc *d = &descs[fd];
    if (EH_DIR(d->no)) return -W_ISDIR;
    size_t resta = d->no->tam > d->pos ? d->no->tam - d->pos : 0;
    size_t q = resta < (size_t)n ? resta : (size_t)n;
    if (q) memcpy(p, d->no->dados + d->pos, q);
    d->pos += q;
    return (int)q;
}
/* flags: 1 criar, 2 diretório, 4 exclusivo, 8 truncar, 16 acrescentar, 32 escrita */
int k_wasi_open(const char *cam, int flags) {
    if (!wctx) return -W_BADF;
    const char *cwd = wctx->sh->cwd;
    Inode *n = vfs_achar(cwd, cam, 1);
    if (n && (flags & 1) && (flags & 4)) return -W_EXIST;
    if (!n) {
        if (!(flags & 1)) return w_erro();
        n = vfs_criar(cwd, cam, 0644);
        if (!n) return w_erro();
    }
    if ((flags & 2) && !EH_DIR(n)) return -W_NOTDIR;
    if ((flags & 32) && EH_DIR(n)) return -W_ISDIR;
    if ((flags & 32) && !vfs_pode_escrever(n)) return -W_ACCES;
    if (!vfs_pode_ler(n)) return -W_ACCES;
    if ((flags & 8) && EH_REG(n)) vfs_gravar_no(n, "", 0, 0);
    for (int fd = 5; fd < MAX_DESC; fd++) {   /* 3 e 4 são os diretórios pré-abertos do WASI */
        if (descs[fd].usado) continue;
        descs[fd].usado = 1; descs[fd].no = n; descs[fd].pos = 0;
        descs[fd].escrita = (flags & 32) != 0; descs[fd].acrescenta = (flags & 16) != 0;
        return fd;
    }
    return -W_INVAL;
}
int k_wasi_close(int fd) {
    if (fd < 3 || fd >= MAX_DESC || !descs[fd].usado) return fd >= 0 && fd < 3 ? 0 : -W_BADF;
    descs[fd].usado = 0;
    return 0;
}
double k_wasi_seek(int fd, double desloc, int de_onde) {
    if (fd < 3 || fd >= MAX_DESC || !descs[fd].usado) return -1;
    Desc *d = &descs[fd];
    double base = de_onde == 0 ? 0 : de_onde == 1 ? (double)d->pos : (double)d->no->tam;
    double novo = base + desloc;
    if (novo < 0) return -1;
    d->pos = (size_t)novo;
    return novo;
}
static void preenche_stat(Inode *n, int *out) {
    out[0] = EH_DIR(n) ? 1 : EH_LNK(n) ? 3 : 2;
    out[1] = (int)n->tam;
    out[2] = (int)n->mtime;
    out[3] = (int)n->ino;
}
int k_wasi_stat(const char *cam, int seguir, int *out) {
    if (!wctx) return -W_BADF;
    Inode *n = vfs_achar(wctx->sh->cwd, cam, seguir);
    if (!n) return w_erro();
    preenche_stat(n, out);
    return 0;
}
int k_wasi_fstat(int fd, int *out) {
    if (fd >= 0 && fd < 3) { out[0] = 0; out[1] = 0; out[2] = 0; out[3] = fd; return 0; }
    if (fd >= MAX_DESC || !descs[fd].usado) return -W_BADF;
    preenche_stat(descs[fd].no, out);
    return 0;
}
/* nome do item "i" do diretório aberto em fd; -1 quando acabou */
int k_wasi_readdir(int fd, int i, char *nome, int cap, int *tipo) {
    if (fd < 3 || fd >= MAX_DESC || !descs[fd].usado || !EH_DIR(descs[fd].no)) return -1;
    Inode *d = descs[fd].no;
    if (i < 0 || i >= d->nfilhos) return -1;
    Inode *f = d->filhos[i];
    int l = (int)strlen(f->nome);
    if (l >= cap) l = cap - 1;
    memcpy(nome, f->nome, (size_t)l); nome[l] = 0;
    *tipo = EH_DIR(f) ? 1 : EH_LNK(f) ? 3 : 2;
    return l;
}
int k_wasi_unlink(const char *cam, int dir) {
    if (!wctx) return -W_BADF;
    Inode *n = vfs_achar(wctx->sh->cwd, cam, 0);
    if (!n) return w_erro();
    if (dir && !EH_DIR(n)) return -W_NOTDIR;
    if (!dir && EH_DIR(n)) return -W_ISDIR;
    for (int fd = 3; fd < MAX_DESC; fd++) if (descs[fd].usado && descs[fd].no == n) descs[fd].usado = 0;
    return vfs_remover(n, 0) ? w_erro() : 0;
}
int k_wasi_mkdir(const char *cam) {
    if (!wctx) return -W_BADF;
    return vfs_mkdir(wctx->sh->cwd, cam, 0, 0755) ? 0 : w_erro();
}
int k_wasi_rename(const char *de, const char *para) {
    if (!wctx) return -W_BADF;
    return vfs_renomear(wctx->sh->cwd, de, para) ? w_erro() : 0;
}
