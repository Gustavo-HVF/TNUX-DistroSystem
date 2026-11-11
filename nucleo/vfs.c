/*
 * TNUX: sistema de arquivos em memória.
 *
 * É o "disco" do sistema quando ele roda no navegador: uma árvore de inodes na memória do
 * WebAssembly. Para não se perder ao fechar a aba, a árvore inteira vira uma imagem binária
 * (vfs_imagem_salvar) que a área de trabalho guarda no IndexedDB do navegador e devolve no
 * próximo boot (vfs_imagem_carregar).
 */
#include "tnux.h"
#include <stdlib.h>
#include <string.h>

Inode *vfs_raiz;
int vfs_sujo;
uint32_t vfs_euid = 1000;
int vfs_err;
static uint32_t prox_ino = 2;

const char *vfs_erro(int e) {
    switch (e) {
    case E_NOENT: return "Arquivo ou diretório inexistente";
    case E_EXIST: return "Arquivo existe";
    case E_NOTDIR: return "Não é um diretório";
    case E_ISDIR: return "É um diretório";
    case E_NOTEMPTY: return "Diretório não vazio";
    case E_ACCES: return "Permissão negada";
    case E_INVAL: return "Argumento inválido";
    case E_LOOP: return "Muitos níveis de links simbólicos";
    case E_BUSY: return "Dispositivo ou recurso está ocupado";
    case E_NOMEM: return "Não foi possível alocar memória";
    default: return "Erro desconhecido";
    }
}

static Inode *no_novo(const char *nome, uint32_t modo, uint32_t uid) {
    Inode *n = xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->ino = prox_ino++;
    n->mode = modo;
    n->uid = uid;
    n->mtime = k_agora();
    n->nome = xstrdup(nome);
    return n;
}
static void no_livrar(Inode *n) {
    for (int i = 0; i < n->nfilhos; i++) no_livrar(n->filhos[i]);
    free(n->filhos); free(n->dados); free(n->nome); free(n);
}
static void liga(Inode *dir, Inode *f) {
    if (dir->nfilhos == dir->capfilhos) {
        dir->capfilhos = dir->capfilhos ? dir->capfilhos * 2 : 8;
        dir->filhos = xrealloc(dir->filhos, sizeof(Inode *) * (size_t)dir->capfilhos);
    }
    dir->filhos[dir->nfilhos++] = f;
    f->pai = dir;
    dir->mtime = k_agora();
    vfs_sujo = 1;
}
static void desliga(Inode *f) {
    Inode *dir = f->pai;
    if (!dir) return;
    for (int i = 0; i < dir->nfilhos; i++) {
        if (dir->filhos[i] == f) {
            memmove(&dir->filhos[i], &dir->filhos[i + 1], sizeof(Inode *) * (size_t)(dir->nfilhos - i - 1));
            dir->nfilhos--;
            break;
        }
    }
    f->pai = NULL;
    dir->mtime = k_agora();
    vfs_sujo = 1;
}

void vfs_iniciar(void) {
    if (vfs_raiz) no_livrar(vfs_raiz);
    prox_ino = 2;
    vfs_raiz = no_novo("", T_DIR | 0755, 0);
    vfs_raiz->ino = 1;
}

Inode *vfs_filho(Inode *dir, const char *nome) {
    if (!dir || !EH_DIR(dir)) return NULL;
    for (int i = 0; i < dir->nfilhos; i++)
        if (!strcmp(dir->filhos[i]->nome, nome)) return dir->filhos[i];
    return NULL;
}

/* caminho absoluto, resolvendo "." e ".." só no texto (sem seguir links) */
char *vfs_abs(const char *cwd, const char *caminho) {
    Buf b; buf_init(&b);
    if (caminho[0] != '/') { buf_puts(&b, cwd && *cwd ? cwd : "/"); buf_putc(&b, '/'); }
    buf_puts(&b, caminho);
    char *src = buf_str(&b);
    size_t n = strlen(src);
    char *dst = xmalloc(n + 2);
    size_t o = 0;
    for (size_t i = 0; i < n;) {
        while (src[i] == '/') i++;
        size_t j = i;
        while (src[j] && src[j] != '/') j++;
        size_t len = j - i;
        if (len == 0) break;
        if (len == 1 && src[i] == '.') {}
        else if (len == 2 && src[i] == '.' && src[i + 1] == '.') {
            while (o > 0 && dst[o - 1] != '/') o--;
            if (o > 0) o--;
        } else {
            dst[o++] = '/';
            memcpy(dst + o, src + i, len);
            o += len;
        }
        i = j;
    }
    if (o == 0) dst[o++] = '/';
    dst[o] = 0;
    buf_free(&b);
    return dst;
}

int vfs_pode_ler(Inode *n) {
    if (vfs_euid == 0) return 1;
    if (n->uid == vfs_euid) return (n->mode & 0400) != 0;
    return (n->mode & 0004) != 0;
}
int vfs_pode_escrever(Inode *n) {
    if (vfs_euid == 0) return 1;
    if (n->uid == vfs_euid) return (n->mode & 0200) != 0;
    return (n->mode & 0002) != 0;
}

/* anda pela árvore seguindo links simbólicos. "seguir" vale para o último componente. */
static Inode *andar(const char *abs, int seguir, int nivel) {
    if (nivel > 16) { vfs_err = E_LOOP; return NULL; }
    Inode *n = vfs_raiz;
    const char *p = abs;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *q = p;
        while (*q && *q != '/') q++;
        if (!EH_DIR(n)) { vfs_err = E_NOTDIR; return NULL; }
        size_t len = (size_t)(q - p);
        Inode *f = NULL;
        if (len == 2 && p[0] == '.' && p[1] == '.') f = n->pai ? n->pai : n;
        else if (len == 1 && p[0] == '.') f = n;
        else {
            for (int i = 0; i < n->nfilhos; i++) {
                const char *nm = n->filhos[i]->nome;
                if (!strncmp(nm, p, len) && nm[len] == 0) { f = n->filhos[i]; break; }
            }
        }
        if (!f) { vfs_err = E_NOENT; return NULL; }
        const char *resto = q;
        while (*resto == '/') resto++;
        int ultimo = (*resto == 0);
        if (EH_LNK(f) && (!ultimo || seguir)) {
            char *base = vfs_caminho(n);
            char *alvo = vfs_abs(base, f->dados ? f->dados : "");
            Buf b; buf_init(&b);
            buf_puts(&b, alvo);
            if (*resto) { buf_putc(&b, '/'); buf_puts(&b, resto); }
            Inode *r = andar(buf_str(&b), seguir, nivel + 1);
            free(base); free(alvo); buf_free(&b);
            return r;
        }
        n = f;
        p = q;
    }
    return n;
}

Inode *vfs_achar(const char *cwd, const char *caminho, int seguir) {
    if (!caminho || !*caminho) { vfs_err = E_NOENT; return NULL; }
    char *abs = vfs_abs(cwd, caminho);
    Inode *n = andar(abs, seguir, 0);
    free(abs);
    return n;
}

char *vfs_caminho(Inode *n) {
    if (!n->pai) return xstrdup("/");
    int nivel = 0;
    Inode *pilha[128];
    for (Inode *p = n; p->pai && nivel < 128; p = p->pai) pilha[nivel++] = p;
    Buf b; buf_init(&b);
    while (nivel--) { buf_putc(&b, '/'); buf_puts(&b, pilha[nivel]->nome); }
    return buf_str(&b);
}

/* separa "caminho" em diretório pai (resolvido) e nome final. Devolve o pai ou NULL. */
static Inode *pai_e_nome(const char *cwd, const char *caminho, char **nome) {
    char *abs = vfs_abs(cwd, caminho);
    char *barra = strrchr(abs, '/');
    *nome = xstrdup(barra + 1);
    Inode *pai;
    if (barra == abs) pai = vfs_raiz;
    else { *barra = 0; pai = andar(abs, 1, 0); }
    free(abs);
    if (pai && !EH_DIR(pai)) { vfs_err = E_NOTDIR; pai = NULL; }
    if (!pai) { free(*nome); *nome = NULL; }
    else if (!**nome) { free(*nome); *nome = NULL; vfs_err = E_EXIST; return NULL; }
    return pai;
}

Inode *vfs_mkdir(const char *cwd, const char *caminho, int pais, uint32_t modo) {
    if (pais) {
        char *abs = vfs_abs(cwd, caminho);
        Inode *n = vfs_raiz;
        char *p = abs;
        while (*p) {
            while (*p == '/') p++;
            if (!*p) break;
            char *q = p;
            while (*q && *q != '/') q++;
            char salvo = *q;
            *q = 0;
            Inode *f = vfs_achar("/", abs, 1);
            if (!f) {
                if (!vfs_pode_escrever(n)) { vfs_err = E_ACCES; free(abs); return NULL; }
                f = no_novo(p, T_DIR | (modo & 07777), vfs_euid);
                liga(n, f);
            } else if (!EH_DIR(f)) { vfs_err = E_NOTDIR; free(abs); return NULL; }
            n = f;
            *q = salvo;
            p = q;
        }
        free(abs);
        return n;
    }
    char *nome;
    Inode *pai = pai_e_nome(cwd, caminho, &nome);
    if (!pai) return NULL;
    Inode *r = NULL;
    if (vfs_filho(pai, nome)) vfs_err = E_EXIST;
    else if (!vfs_pode_escrever(pai)) vfs_err = E_ACCES;
    else { r = no_novo(nome, T_DIR | (modo & 07777), vfs_euid); liga(pai, r); }
    free(nome);
    return r;
}

Inode *vfs_criar(const char *cwd, const char *caminho, uint32_t modo) {
    Inode *n = vfs_achar(cwd, caminho, 1);
    if (n) {
        if (EH_DIR(n)) { vfs_err = E_ISDIR; return NULL; }
        return n;
    }
    /* um link simbólico solto: cria o alvo dele */
    Inode *l = vfs_achar(cwd, caminho, 0);
    if (l && EH_LNK(l)) {
        char *base = vfs_caminho(l->pai);
        char *alvo = vfs_abs(base, l->dados);
        Inode *r = vfs_criar("/", alvo, modo);
        free(base); free(alvo);
        return r;
    }
    char *nome;
    Inode *pai = pai_e_nome(cwd, caminho, &nome);
    if (!pai) return NULL;
    Inode *r = NULL;
    if (!vfs_pode_escrever(pai)) vfs_err = E_ACCES;
    else { r = no_novo(nome, T_REG | (modo & 07777), vfs_euid); liga(pai, r); }
    free(nome);
    return r;
}

int vfs_gravar_no(Inode *f, const void *d, size_t n, int acrescentar) {
    if (EH_DIR(f)) return vfs_err = E_ISDIR;
    if (!vfs_pode_escrever(f)) return vfs_err = E_ACCES;
    size_t ini = acrescentar ? f->tam : 0;
    size_t novo = ini + n;
    if (novo + 1 > f->cap || (novo * 4 < f->cap && f->cap > 4096)) {
        size_t cap = acrescentar ? (novo + 1) * 2 : novo + 1;
        if (cap < 16) cap = 16;
        char *p = realloc(f->dados, cap);
        if (!p) return vfs_err = E_NOMEM;
        f->dados = p; f->cap = cap;
    }
    if (n) memcpy(f->dados + ini, d, n);
    f->tam = novo;
    f->dados[novo] = 0;     /* arquivos de texto podem ser lidos como string C */
    f->mtime = k_agora();
    vfs_sujo = 1;
    return E_OK;
}

int vfs_gravar(const char *cwd, const char *caminho, const void *d, size_t n, int acrescentar) {
    Inode *f = vfs_criar(cwd, caminho, 0644);
    if (!f) return vfs_err;
    return vfs_gravar_no(f, d, n, acrescentar);
}

int vfs_remover(Inode *n, int recursivo) {
    if (n == vfs_raiz) return vfs_err = E_BUSY;
    if (!vfs_pode_escrever(n->pai)) return vfs_err = E_ACCES;
    if (EH_DIR(n) && n->nfilhos && !recursivo) return vfs_err = E_NOTEMPTY;
    desliga(n);
    no_livrar(n);
    return E_OK;
}

int vfs_renomear(const char *cwd, const char *de, const char *para) {
    Inode *n = vfs_achar(cwd, de, 0);
    if (!n) return vfs_err;
    if (n == vfs_raiz) return vfs_err = E_BUSY;
    Inode *alvo = vfs_achar(cwd, para, 1);
    char *nome;
    Inode *pai;
    if (alvo && EH_DIR(alvo) && !(EH_DIR(n) && alvo == n)) {
        /* mover para dentro de um diretório que já existe */
        pai = alvo;
        nome = xstrdup(n->nome);
    } else {
        pai = pai_e_nome(cwd, para, &nome);
        if (!pai) return vfs_err;
    }
    for (Inode *p = pai; p; p = p->pai)
        if (p == n) { free(nome); return vfs_err = E_INVAL; }   /* mover para dentro de si mesmo */
    if (!vfs_pode_escrever(pai) || !vfs_pode_escrever(n->pai)) { free(nome); return vfs_err = E_ACCES; }
    Inode *velho = vfs_filho(pai, nome);
    if (velho == n) { free(nome); return E_OK; }
    if (velho) {
        if (EH_DIR(velho) && velho->nfilhos) { free(nome); return vfs_err = E_NOTEMPTY; }
        if (EH_DIR(velho) != EH_DIR(n)) { free(nome); return vfs_err = EH_DIR(velho) ? E_ISDIR : E_NOTDIR; }
        desliga(velho);
        no_livrar(velho);
    }
    desliga(n);
    free(n->nome);
    n->nome = nome;
    liga(pai, n);
    return E_OK;
}

int vfs_link(const char *cwd, const char *destino, const char *caminho) {
    char *nome;
    Inode *pai = pai_e_nome(cwd, caminho, &nome);
    if (!pai) return vfs_err;
    int r = E_OK;
    if (vfs_filho(pai, nome)) r = vfs_err = E_EXIST;
    else if (!vfs_pode_escrever(pai)) r = vfs_err = E_ACCES;
    else {
        Inode *l = no_novo(nome, T_LNK | 0777, vfs_euid);
        l->dados = xstrdup(destino);
        l->tam = strlen(destino);
        l->cap = l->tam + 1;
        liga(pai, l);
    }
    free(nome);
    return r;
}

static int cmp_nome(const void *a, const void *b) {
    return strcmp((*(Inode *const *)a)->nome, (*(Inode *const *)b)->nome);
}
void vfs_ordenar(Inode *dir) {
    if (dir->nfilhos > 1) qsort(dir->filhos, (size_t)dir->nfilhos, sizeof(Inode *), cmp_nome);
}

size_t vfs_tamanho_total(Inode *n, size_t *arquivos) {
    size_t t = n->tam;
    if (arquivos) (*arquivos)++;
    for (int i = 0; i < n->nfilhos; i++) t += vfs_tamanho_total(n->filhos[i], arquivos);
    return t;
}

/* atalhos do sistema: criam como root, sem checar permissão */
void vfs_sis_dir(const char *caminho, uint32_t modo, uint32_t uid) {
    uint32_t e = vfs_euid;
    vfs_euid = 0;
    Inode *d = vfs_mkdir("/", caminho, 1, modo);
    if (d) { d->mode = T_DIR | modo; d->uid = uid; }
    vfs_euid = e;
}
void vfs_sis_arq(const char *caminho, const char *conteudo, uint32_t modo, uint32_t uid) {
    uint32_t e = vfs_euid;
    vfs_euid = 0;
    Inode *f = vfs_criar("/", caminho, modo);
    if (f) {
        vfs_gravar_no(f, conteudo, strlen(conteudo), 0);
        f->mode = T_REG | modo;
        f->uid = uid;
    }
    vfs_euid = e;
}

/* ------------------------------------------------------------------ imagem do disco
 * Formato: "TNUXFS1\n" e depois cada inode, em profundidade:
 *   modo(u32) uid(u32) mtime(i64) tam_nome(u16) nome tam_dados(u32) dados n_filhos(u32) filhos...
 * Tudo em little-endian.
 */

static void p32(Buf *b, uint32_t v) {
    unsigned char t[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
    buf_put(b, t, 4);
}
static void salvar_no(Buf *b, Inode *n) {
    p32(b, n->mode);
    p32(b, n->uid);
    p32(b, (uint32_t)n->mtime);
    p32(b, (uint32_t)((uint64_t)n->mtime >> 32));
    size_t ln = strlen(n->nome);
    unsigned char t[2] = { (unsigned char)ln, (unsigned char)(ln >> 8) };
    buf_put(b, t, 2);
    buf_put(b, n->nome, ln);
    p32(b, (uint32_t)n->tam);
    if (n->tam) buf_put(b, n->dados, n->tam);
    p32(b, (uint32_t)n->nfilhos);
    for (int i = 0; i < n->nfilhos; i++) salvar_no(b, n->filhos[i]);
}
void vfs_imagem_salvar(Buf *saida) {
    buf_puts(saida, "TNUXFS1\n");
    salvar_no(saida, vfs_raiz);
    vfs_sujo = 0;
}

typedef struct { const unsigned char *p, *fim; int erro; } Le;
static uint32_t l32(Le *l) {
    if (l->fim - l->p < 4) { l->erro = 1; return 0; }
    uint32_t v = (uint32_t)l->p[0] | ((uint32_t)l->p[1] << 8) | ((uint32_t)l->p[2] << 16) | ((uint32_t)l->p[3] << 24);
    l->p += 4;
    return v;
}
static Inode *carregar_no(Le *l, int nivel) {
    if (nivel > 200) { l->erro = 1; return NULL; }
    uint32_t modo = l32(l), uid = l32(l), t0 = l32(l), t1 = l32(l);
    if (l->erro || l->fim - l->p < 2) { l->erro = 1; return NULL; }
    size_t ln = (size_t)l->p[0] | ((size_t)l->p[1] << 8);
    l->p += 2;
    if ((size_t)(l->fim - l->p) < ln) { l->erro = 1; return NULL; }
    Inode *n = xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->ino = prox_ino++;
    n->mode = modo; n->uid = uid;
    n->mtime = (int64_t)(((uint64_t)t1 << 32) | t0);
    n->nome = xmalloc(ln + 1);
    memcpy(n->nome, l->p, ln); n->nome[ln] = 0;
    l->p += ln;
    uint32_t tam = l32(l);
    if (l->erro || (size_t)(l->fim - l->p) < tam) { l->erro = 1; no_livrar(n); return NULL; }
    if (tam || EH_LNK(n)) {
        n->dados = xmalloc(tam + 1);
        memcpy(n->dados, l->p, tam); n->dados[tam] = 0;
        n->tam = tam; n->cap = tam + 1;
        l->p += tam;
    }
    uint32_t nf = l32(l);
    if (l->erro) { no_livrar(n); return NULL; }
    for (uint32_t i = 0; i < nf; i++) {
        Inode *f = carregar_no(l, nivel + 1);
        if (!f) { no_livrar(n); return NULL; }
        if (n->nfilhos == n->capfilhos) {
            n->capfilhos = n->capfilhos ? n->capfilhos * 2 : 8;
            n->filhos = xrealloc(n->filhos, sizeof(Inode *) * (size_t)n->capfilhos);
        }
        n->filhos[n->nfilhos++] = f;
        f->pai = n;
    }
    return n;
}
int vfs_imagem_carregar(const char *d, size_t n) {
    if (n < 8 || memcmp(d, "TNUXFS1\n", 8)) return 0;
    Le l = { (const unsigned char *)d + 8, (const unsigned char *)d + n, 0 };
    uint32_t salvo = prox_ino;
    prox_ino = 1;
    Inode *r = carregar_no(&l, 0);
    if (!r || !EH_DIR(r)) { if (r) no_livrar(r); prox_ino = salvo; return 0; }
    if (vfs_raiz) no_livrar(vfs_raiz);
    vfs_raiz = r;
    vfs_sujo = 0;
    return 1;
}
