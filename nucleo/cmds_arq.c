/*
 * TNUX: comandos de arquivos (ls, cat, cp, mv, rm, mkdir, find, tree, du, df, stat...).
 */
#include "cmds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const char *const MESES[12] = { "jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez" };

int ler_arq(Ctx *c, const char *cmd, const char *arq, const char **d, size_t *n) {
    if (!arq || !strcmp(arq, "-")) {
        *d = c->in ? c->in : "";
        *n = c->in ? c->inlen : 0;
        return 0;
    }
    Inode *f = vfs_achar(c->sh->cwd, arq, 1);
    if (!f) { ERR("%s: %s: %s\n", cmd, arq, vfs_erro(vfs_err)); return 1; }
    if (EH_DIR(f)) { ERR("%s: %s: É um diretório\n", cmd, arq); return 1; }
    if (!vfs_pode_ler(f)) { ERR("%s: %s: Permissão negada\n", cmd, arq); return 1; }
    *d = f->dados ? f->dados : "";
    *n = f->tam;
    return 0;
}

void modo_txt(uint32_t m, char *d) {
    d[0] = (m & T_FMT) == T_DIR ? 'd' : (m & T_FMT) == T_LNK ? 'l' : '-';
    const char *rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; i++) d[1 + i] = (m & (0400u >> i)) ? rwx[i] : '-';
    if (m & 01000) d[9] = (m & 1) ? 't' : 'T';
    d[10] = 0;
}
const char *dono_nome(uint32_t uid) { return uid == 0 ? "root" : TNUX_USUARIO; }

const char *prox_linha(const char **p, const char *fim, size_t *tam) {
    if (*p >= fim) return NULL;
    const char *ini = *p;
    const char *nl = memchr(ini, '\n', (size_t)(fim - ini));
    if (nl) { *tam = (size_t)(nl - ini); *p = nl + 1; }
    else { *tam = (size_t)(fim - ini); *p = fim; }
    return ini;
}

/* separa as opções curtas juntas (-la) e devolve o índice do primeiro argumento que não é opção */
static int opcoes(int argc, char **argv, const char *validas, char *achadas, Ctx *c) {
    int i = 1;
    achadas[0] = 0;
    for (; i < argc; i++) {
        if (argv[i][0] != '-' || !argv[i][1]) break;
        if (!strcmp(argv[i], "--")) { i++; break; }
        if (argv[i][1] == '-') {          /* opções longas conhecidas viram letras */
            const char *l = argv[i] + 2;
            char ch = 0;
            if (!strcmp(l, "all")) ch = 'a';
            else if (!strcmp(l, "recursive")) ch = 'r';
            else if (!strcmp(l, "force")) ch = 'f';
            else if (!strcmp(l, "parents")) ch = 'p';
            else if (!strcmp(l, "human-readable")) ch = 'h';
            else if (!strcmp(l, "verbose")) ch = 'v';
            else if (!strcmp(l, "help")) ch = '?';
            else if (!strncmp(l, "color", 5)) continue;
            if (ch) { size_t n = strlen(achadas); achadas[n] = ch; achadas[n + 1] = 0; continue; }
            ERR("%s: opção não reconhecida '%s'\n", argv[0], argv[i]);
            return -1;
        }
        for (const char *p = argv[i] + 1; *p; p++) {
            if (!strchr(validas, *p)) { ERR("%s: opção inválida -- '%c'\n", argv[0], *p); return -1; }
            size_t n = strlen(achadas);
            if (n < 30) { achadas[n] = *p; achadas[n + 1] = 0; }
        }
    }
    return i;
}
#define TEM(ch) (strchr(op, ch) != NULL)

/* ------------------------------------------------------------------ ls */

typedef struct { int l, a, A, h, R, um, d, F, t, r, S; int cor; int cols; } LsOp;
static LsOp *ls_cmp_op;

static int ls_cmp(const void *x, const void *y) {
    Inode *a = *(Inode *const *)x, *b = *(Inode *const *)y;
    int v;
    if (ls_cmp_op->t) v = a->mtime < b->mtime ? 1 : a->mtime > b->mtime ? -1 : strcmp(a->nome, b->nome);
    else if (ls_cmp_op->S) v = a->tam < b->tam ? 1 : a->tam > b->tam ? -1 : strcmp(a->nome, b->nome);
    else v = strcasecmp(a->nome[0] == '.' ? a->nome + 1 : a->nome, b->nome[0] == '.' ? b->nome + 1 : b->nome);
    return ls_cmp_op->r ? -v : v;
}
static const char *ls_cor(Inode *n, Inode *real) {
    if (EH_LNK(n)) return real ? "\x1b[01;36m" : "\x1b[01;31m";
    if (EH_DIR(n)) return (n->mode & 01000) ? "\x1b[30;42m" : "\x1b[01;34m";
    if (n->mode & 0111) return "\x1b[01;32m";
    const char *e = strrchr(n->nome, '.');
    if (e) {
        if (!strcasecmp(e, ".png") || !strcasecmp(e, ".jpg") || !strcasecmp(e, ".jpeg") || !strcasecmp(e, ".gif") ||
            !strcasecmp(e, ".svg") || !strcasecmp(e, ".webp") || !strcasecmp(e, ".mp4") || !strcasecmp(e, ".webm")) return "\x1b[01;35m";
        if (!strcasecmp(e, ".zip") || !strcasecmp(e, ".tar") || !strcasecmp(e, ".gz") || !strcasecmp(e, ".deb") || !strcasecmp(e, ".xz")) return "\x1b[01;31m";
        if (!strcasecmp(e, ".mp3") || !strcasecmp(e, ".ogg") || !strcasecmp(e, ".wav") || !strcasecmp(e, ".flac")) return "\x1b[00;36m";
    }
    return "";
}
static int larg_utf8(const char *s) { int l = 0; for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) l++; return l; }

static void ls_nome(Ctx *c, LsOp *o, Inode *n, const char *nome, Inode *dir) {
    Inode *real = n;
    if (EH_LNK(n)) {
        char *base = dir ? vfs_caminho(dir) : xstrdup(c->sh->cwd);
        real = vfs_achar(base, n->dados ? n->dados : "", 1);
        free(base);
    }
    const char *cor = o->cor ? ls_cor(n, real) : "";
    OUT("%s%s%s", cor, nome, *cor ? "\x1b[0m" : "");
    if (o->F) {
        if (EH_DIR(n)) OUT("/");
        else if (EH_LNK(n) && !o->l) OUT("@");
        else if (n->mode & 0111) OUT("*");
    }
    if (o->l && EH_LNK(n)) OUT(" -> %s", n->dados ? n->dados : "");
}
static void ls_longo(Ctx *c, LsOp *o, Inode *n, const char *nome, Inode *dir) {
    char m[12], tam[24];
    modo_txt(n->mode, m);
    if (o->h) k_tamanho_humano((double)(EH_DIR(n) ? 4096 : n->tam), tam, sizeof tam);
    else snprintf(tam, sizeof tam, "%lu", (unsigned long)(EH_DIR(n) ? 4096 : n->tam));
    int ano, mes, dia, h, mi, s, ds, ano_agora, x;
    k_data(n->mtime, &ano, &mes, &dia, &h, &mi, &s, &ds);
    k_data(k_agora(), &ano_agora, &x, &x, &x, &x, &x, &x);
    char quando[24];
    if (ano == ano_agora) snprintf(quando, sizeof quando, "%s %2d %02d:%02d", MESES[mes - 1], dia, h, mi);
    else snprintf(quando, sizeof quando, "%s %2d  %d", MESES[mes - 1], dia, ano);
    OUT("%s %2d %-7s %-7s %7s %s ", m, EH_DIR(n) ? n->nfilhos + 2 : 1, dono_nome(n->uid), dono_nome(n->uid), tam, quando);
    ls_nome(c, o, n, nome, dir);
    OUT("\n");
}
static void ls_lista(Ctx *c, LsOp *o, Inode **v, int n, Inode *dir) {
    if (o->l) {
        if (dir) {
            size_t tot = 0;
            for (int i = 0; i < n; i++) tot += EH_DIR(v[i]) ? 4096 : (v[i]->tam + 4095) / 4096 * 4096;
            OUT("total %lu\n", (unsigned long)(tot / 1024));
        }
        for (int i = 0; i < n; i++) ls_longo(c, o, v[i], v[i]->nome, dir);
        return;
    }
    if (o->um || !o_tty(c->out)) {
        for (int i = 0; i < n; i++) { ls_nome(c, o, v[i], v[i]->nome, dir); OUT("\n"); }
        return;
    }
    /* em colunas, preenchendo de cima para baixo como o ls de verdade */
    int maior = 0;
    for (int i = 0; i < n; i++) { int l = larg_utf8(v[i]->nome) + (o->F ? 1 : 0); if (l > maior) maior = l; }
    int ncol = o->cols / (maior + 2);
    if (ncol < 1) ncol = 1;
    int nlin = (n + ncol - 1) / ncol;
    for (int lin = 0; lin < nlin; lin++) {
        for (int col = 0; col < ncol; col++) {
            int i = col * nlin + lin;
            if (i >= n) continue;
            ls_nome(c, o, v[i], v[i]->nome, dir);
            if ((col + 1) * nlin + lin < n) {
                int l = larg_utf8(v[i]->nome) + ((o->F && (EH_DIR(v[i]) || EH_LNK(v[i]) || (v[i]->mode & 0111))) ? 1 : 0);
                for (; l < maior + 2; l++) OUT(" ");
            }
        }
        OUT("\n");
    }
}
static void ls_dir(Ctx *c, LsOp *o, Inode *dir, const char *titulo, int mostra_titulo) {
    if (!vfs_pode_ler(dir)) { ERR("ls: não foi possível abrir o diretório '%s': Permissão negada\n", titulo); return; }
    if (mostra_titulo) OUT("%s:\n", titulo);
    int n = 0;
    Inode **v = xmalloc(sizeof(Inode *) * (size_t)(dir->nfilhos + 1));
    for (int i = 0; i < dir->nfilhos; i++)
        if (o->a || o->A || dir->filhos[i]->nome[0] != '.') v[n++] = dir->filhos[i];
    ls_cmp_op = o;
    if (n > 1) qsort(v, (size_t)n, sizeof(Inode *), ls_cmp);
    if (o->l && o->a) {
        OUT("total %d\n", n * 4);
        ls_longo(c, o, dir, ".", NULL);
        ls_longo(c, o, dir->pai ? dir->pai : dir, "..", NULL);
        for (int i = 0; i < n; i++) ls_longo(c, o, v[i], v[i]->nome, dir);
    } else ls_lista(c, o, v, n, dir);
    if (o->R) {
        for (int i = 0; i < n; i++) {
            if (!EH_DIR(v[i])) continue;
            char *sub = xasprintf("%s%s%s", titulo, titulo[strlen(titulo) - 1] == '/' ? "" : "/", v[i]->nome);
            OUT("\n");
            ls_dir(c, o, v[i], sub, 1);
            free(sub);
        }
    }
    free(v);
}
static int c_ls(Ctx *c, int argc, char **argv) {
    char op[32];
    int i = opcoes(argc, argv, "laAhR1dFtrSC", op, c);
    if (i < 0) return 2;
    LsOp o = { TEM('l'), TEM('a'), TEM('A'), TEM('h'), TEM('R'), TEM('1'), TEM('d'), TEM('F'), TEM('t'), TEM('r'), TEM('S'), o_tty(c->out), c->sh->cols };
    int r = 0, narg = argc - i;
    if (narg == 0) {
        Inode *d = vfs_achar(c->sh->cwd, ".", 1);
        if (!d) { ERR("ls: não foi possível acessar '.': %s\n", vfs_erro(vfs_err)); return 2; }
        if (o.d) { ls_cmp_op = &o; Inode *v[1] = { d }; if (o.l) ls_longo(c, &o, d, ".", NULL); else { OUT(".\n"); } (void)v; }
        else ls_dir(c, &o, d, ".", o.R);
        return 0;
    }
    /* primeiro os arquivos, depois os diretórios */
    int primeiro = 1;
    for (int k = i; k < argc; k++) {
        int segue = !o.l || argv[k][strlen(argv[k]) - 1] == '/';
        Inode *n = vfs_achar(c->sh->cwd, argv[k], segue);
        if (!n) { ERR("ls: não foi possível acessar '%s': %s\n", argv[k], vfs_erro(vfs_err)); r = 2; continue; }
        if (EH_DIR(n) && !o.d) continue;
        if (o.l) ls_longo(c, &o, n, argv[k], NULL);
        else { ls_nome(c, &o, n, argv[k], NULL); OUT("\n"); }
        primeiro = 0;
    }
    for (int k = i; k < argc; k++) {
        int segue = !o.l || (argv[k][0] && argv[k][strlen(argv[k]) - 1] == '/');
        Inode *n = vfs_achar(c->sh->cwd, argv[k], segue);
        if (!n || !EH_DIR(n) || o.d) continue;
        if (!primeiro) OUT("\n");
        ls_dir(c, &o, n, argv[k], narg > 1 || o.R);
        primeiro = 0;
    }
    return r;
}

/* ------------------------------------------------------------------ cat, touch, mkdir, rmdir, rm */

static int c_cat(Ctx *c, int argc, char **argv) {
    char op[32];
    int i = opcoes(argc, argv, "nbsAET", op, c);
    if (i < 0) return 1;
    int r = 0, num = 1;
    int k = i;
    do {
        const char *d; size_t n;
        if (ler_arq(c, "cat", k < argc ? argv[k] : NULL, &d, &n)) { r = 1; continue; }
        if (!TEM('n')) { o_write(c->out, d, n); continue; }
        const char *p = d, *fim = d + n, *l; size_t tl;
        while ((l = prox_linha(&p, fim, &tl))) { OUT("%6d\t", num++); o_write(c->out, l, tl); OUT("\n"); }
    } while (++k < argc);
    return r;
}
static int c_touch(Ctx *c, int argc, char **argv) {
    int r = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;
        Inode *f = vfs_achar(c->sh->cwd, argv[i], 1);
        if (f) { f->mtime = k_agora(); vfs_sujo = 1; continue; }
        if (!vfs_criar(c->sh->cwd, argv[i], 0644)) { ERR("touch: não foi possível tocar '%s': %s\n", argv[i], vfs_erro(vfs_err)); r = 1; }
    }
    if (argc < 2) { ERR("touch: falta o operando arquivo\n"); return 1; }
    return r;
}
static int c_mkdir(Ctx *c, int argc, char **argv) {
    char op[32];
    int i = opcoes(argc, argv, "pvm", op, c);
    if (i < 0) return 1;
    if (i >= argc) { ERR("mkdir: falta operando\n"); return 1; }
    int r = 0;
    for (; i < argc; i++) {
        if (!vfs_mkdir(c->sh->cwd, argv[i], TEM('p'), 0755)) {
            ERR("mkdir: não foi possível criar o diretório “%s”: %s\n", argv[i], vfs_erro(vfs_err));
            r = 1;
        } else if (TEM('v')) OUT("mkdir: foi criado o diretório '%s'\n", argv[i]);
    }
    return r;
}
static int c_rmdir(Ctx *c, int argc, char **argv) {
    int r = 0;
    if (argc < 2) { ERR("rmdir: falta operando\n"); return 1; }
    for (int i = 1; i < argc; i++) {
        Inode *d = vfs_achar(c->sh->cwd, argv[i], 0);
        if (!d) { ERR("rmdir: falhou em remover '%s': %s\n", argv[i], vfs_erro(vfs_err)); r = 1; continue; }
        if (!EH_DIR(d)) { ERR("rmdir: falhou em remover '%s': Não é um diretório\n", argv[i]); r = 1; continue; }
        if (vfs_remover(d, 0)) { ERR("rmdir: falhou em remover '%s': %s\n", argv[i], vfs_erro(vfs_err)); r = 1; }
    }
    return r;
}
static int dentro_do_cwd(Shell *sh, Inode *n) {
    Inode *cw = vfs_achar("/", sh->cwd, 1);
    for (; cw; cw = cw->pai) if (cw == n) return 1;
    return 0;
}
static int c_rm(Ctx *c, int argc, char **argv) {
    char op[32];
    int i = opcoes(argc, argv, "rRfvid", op, c);
    if (i < 0) return 1;
    int rec = TEM('r') || TEM('R'), forca = TEM('f'), r = 0;
    if (i >= argc && !forca) { ERR("rm: falta operando\n"); return 1; }
    for (; i < argc; i++) {
        Inode *n = vfs_achar(c->sh->cwd, argv[i], 0);
        if (!n) { if (!forca) { ERR("rm: não foi possível remover '%s': %s\n", argv[i], vfs_erro(vfs_err)); r = 1; } continue; }
        if (n == vfs_raiz) { ERR("rm: é perigoso operar recursivamente em '/'\nrm: use --no-preserve-root para contornar esta proteção (o TNUX não deixa)\n"); r = 1; continue; }
        if (EH_DIR(n) && !rec) { ERR("rm: não foi possível remover '%s': É um diretório\n", argv[i]); r = 1; continue; }
        if (dentro_do_cwd(c->sh, n)) { ERR("rm: não foi possível remover '%s': você está dentro dele\n", argv[i]); r = 1; continue; }
        if (vfs_remover(n, 1)) { ERR("rm: não foi possível remover '%s': %s\n", argv[i], vfs_erro(vfs_err)); r = 1; }
        else if (TEM('v')) OUT("removido '%s'\n", argv[i]);
    }
    return r;
}

/* ------------------------------------------------------------------ cp, mv, ln */

int copiar_no(Ctx *c, const char *cmd, Inode *origem, const char *destino, int recursivo) {
    Shell *sh = c->sh;
    if (EH_DIR(origem)) {
        if (!recursivo) { ERR("%s: -r não especificado; omitindo o diretório '%s'\n", cmd, origem->nome); return 1; }
        Inode *d = vfs_achar(sh->cwd, destino, 1);
        if (!d) d = vfs_mkdir(sh->cwd, destino, 0, origem->mode & 0777);
        if (!d || !EH_DIR(d)) { ERR("%s: não foi possível criar o diretório '%s': %s\n", cmd, destino, vfs_erro(d ? E_NOTDIR : vfs_err)); return 1; }
        for (Inode *p = d; p; p = p->pai) if (p == origem) { ERR("%s: não é possível copiar um diretório, '%s', para dentro de si mesmo\n", cmd, origem->nome); return 1; }
        int r = 0, n = origem->nfilhos;
        Inode **filhos = xmalloc(sizeof(Inode *) * (size_t)(n + 1));
        memcpy(filhos, origem->filhos, sizeof(Inode *) * (size_t)n);
        for (int i = 0; i < n; i++) {
            char *sub = xasprintf("%s/%s", destino, filhos[i]->nome);
            r |= copiar_no(c, cmd, filhos[i], sub, 1);
            free(sub);
        }
        free(filhos);
        return r;
    }
    if (EH_LNK(origem)) {
        Inode *velho = vfs_achar(sh->cwd, destino, 0);
        if (velho && !EH_DIR(velho)) vfs_remover(velho, 0);
        if (vfs_link(sh->cwd, origem->dados ? origem->dados : "", destino)) { ERR("%s: não foi possível criar o link '%s': %s\n", cmd, destino, vfs_erro(vfs_err)); return 1; }
        return 0;
    }
    if (!vfs_pode_ler(origem)) { ERR("%s: não foi possível abrir '%s' para leitura: Permissão negada\n", cmd, origem->nome); return 1; }
    Inode *alvo = vfs_criar(sh->cwd, destino, origem->mode & 0777);
    if (alvo == origem) { ERR("%s: '%s' e '%s' são o mesmo arquivo\n", cmd, origem->nome, destino); return 1; }
    if (!alvo || vfs_gravar_no(alvo, origem->dados ? origem->dados : "", origem->tam, 0)) {
        ERR("%s: não foi possível criar o arquivo comum '%s': %s\n", cmd, destino, vfs_erro(vfs_err));
        return 1;
    }
    return 0;
}
static const char *nome_base(const char *cam) {
    size_t n = strlen(cam);
    while (n > 1 && cam[n - 1] == '/') n--;
    const char *b = cam + n;
    while (b > cam && b[-1] != '/') b--;
    return b;
}
static char *sem_barra_final(const char *cam) {
    char *r = xstrdup(cam);
    size_t n = strlen(r);
    while (n > 1 && r[n - 1] == '/') r[--n] = 0;
    return r;
}
static int c_cp(Ctx *c, int argc, char **argv) {
    char op[32];
    int i = opcoes(argc, argv, "rRafvpiu", op, c);
    if (i < 0) return 1;
    int rec = TEM('r') || TEM('R') || TEM('a'), narg = argc - i, r = 0;
    if (narg < 2) { ERR("cp: falta o operando arquivo de destino\n"); return 1; }
    const char *dest = argv[argc - 1];
    Inode *d = vfs_achar(c->sh->cwd, dest, 1);
    int dest_dir = d && EH_DIR(d);
    if (narg > 2 && !dest_dir) { ERR("cp: o alvo '%s' não é um diretório\n", dest); return 1; }
    for (; i < argc - 1; i++) {
        Inode *o = vfs_achar(c->sh->cwd, argv[i], rec ? 0 : 1);
        if (!o) { ERR("cp: não foi possível obter estado de '%s': %s\n", argv[i], vfs_erro(vfs_err)); r = 1; continue; }
        char *limpo = sem_barra_final(argv[i]);
        char *alvo = dest_dir ? xasprintf("%s/%s", dest, nome_base(limpo)) : xstrdup(dest);
        r |= copiar_no(c, "cp", o, alvo, rec);
        if (TEM('v')) OUT("'%s' -> '%s'\n", argv[i], alvo);
        free(alvo); free(limpo);
    }
    return r;
}
static int c_mv(Ctx *c, int argc, char **argv) {
    char op[32];
    int i = opcoes(argc, argv, "fivnu", op, c);
    if (i < 0) return 1;
    int narg = argc - i, r = 0;
    if (narg < 2) { ERR("mv: falta o operando arquivo de destino\n"); return 1; }
    const char *dest = argv[argc - 1];
    Inode *d = vfs_achar(c->sh->cwd, dest, 1);
    if (narg > 2 && !(d && EH_DIR(d))) { ERR("mv: o alvo '%s' não é um diretório\n", dest); return 1; }
    for (; i < argc - 1; i++) {
        if (vfs_renomear(c->sh->cwd, argv[i], dest)) {
            ERR("mv: não foi possível mover '%s' para '%s': %s\n", argv[i], dest, vfs_erro(vfs_err));
            r = 1;
        } else if (TEM('v')) OUT("renomeado '%s' -> '%s'\n", argv[i], dest);
    }
    /* se o diretório atual foi movido, o shell continua valendo pelo caminho novo */
    if (!vfs_achar("/", c->sh->cwd, 1)) { free(c->sh->cwd); c->sh->cwd = xstrdup("/"); }
    return r;
}
static int c_ln(Ctx *c, int argc, char **argv) {
    char op[32];
    int i = opcoes(argc, argv, "sfnv", op, c);
    if (i < 0) return 1;
    if (argc - i < 1) { ERR("ln: falta o operando arquivo\n"); return 1; }
    const char *alvo = argv[i];
    char *nome = argc - i >= 2 ? xstrdup(argv[i + 1]) : xstrdup(nome_base(alvo));
    Inode *d = vfs_achar(c->sh->cwd, nome, 1);
    if (d && EH_DIR(d)) { char *n2 = xasprintf("%s/%s", nome, nome_base(alvo)); free(nome); nome = n2; }
    if (TEM('f')) { Inode *v = vfs_achar(c->sh->cwd, nome, 0); if (v && !EH_DIR(v)) vfs_remover(v, 0); }
    int r = 0;
    if (!TEM('s')) {
        /* sem links rígidos: copia o arquivo (para o usuário dá no mesmo na maioria dos casos) */
        Inode *o = vfs_achar(c->sh->cwd, alvo, 1);
        if (!o) { ERR("ln: falhou ao acessar '%s': %s\n", alvo, vfs_erro(vfs_err)); r = 1; }
        else if (EH_DIR(o)) { ERR("ln: %s: link físico não permitido para diretório\n", alvo); r = 1; }
        else r = copiar_no(c, "ln", o, nome, 0);
    } else if (vfs_link(c->sh->cwd, alvo, nome)) {
        ERR("ln: falhou ao criar link simbólico '%s': %s\n", nome, vfs_erro(vfs_err));
        r = 1;
    }
    free(nome);
    return r;
}

/* ------------------------------------------------------------------ chmod, chown, stat */

static void chmod_rec(Inode *n, const char *spec, int rec) {
    char *fim;
    long oct = strtol(spec, &fim, 8);
    if (*spec && !*fim) n->mode = (n->mode & T_FMT) | ((uint32_t)oct & 07777);
    else {
        /* formato simbólico: [ugoa][+-=][rwxX] separados por vírgula */
        const char *p = spec;
        while (*p) {
            uint32_t quem = 0;
            for (; *p && strchr("ugoa", *p); p++) {
                if (*p == 'u') quem |= 0700; else if (*p == 'g') quem |= 0070; else if (*p == 'o') quem |= 0007; else quem |= 0777;
            }
            if (!quem) quem = 0777;
            char sinal = *p ? *p++ : '+';
            uint32_t bits = 0;
            for (; *p && *p != ','; p++) {
                if (*p == 'r') bits |= 0444;
                else if (*p == 'w') bits |= 0222;
                else if (*p == 'x' || (*p == 'X' && (EH_DIR(n) || (n->mode & 0111)))) bits |= 0111;
            }
            bits &= quem;
            if (sinal == '+') n->mode |= bits;
            else if (sinal == '-') n->mode &= ~bits;
            else n->mode = (n->mode & ~quem) | bits;
            if (*p == ',') p++;
        }
    }
    vfs_sujo = 1;
    if (rec) for (int i = 0; i < n->nfilhos; i++) chmod_rec(n->filhos[i], spec, 1);
}
static int c_chmod(Ctx *c, int argc, char **argv) {
    int i = 1, rec = 0, r = 0;
    while (i < argc && argv[i][0] == '-' && argv[i][1] && !strchr("rwxX", argv[i][1])) { if (strchr(argv[i], 'R')) rec = 1; i++; }
    if (argc - i < 2) { ERR("chmod: falta operando\n"); return 1; }
    const char *spec = argv[i++];
    for (; i < argc; i++) {
        Inode *n = vfs_achar(c->sh->cwd, argv[i], 1);
        if (!n) { ERR("chmod: não foi possível acessar '%s': %s\n", argv[i], vfs_erro(vfs_err)); r = 1; continue; }
        if (vfs_euid != 0 && n->uid != vfs_euid) { ERR("chmod: alterando permissões de '%s': Operação não permitida\n", argv[i]); r = 1; continue; }
        chmod_rec(n, spec, rec);
    }
    return r;
}
static void chown_rec(Inode *n, uint32_t uid, int rec) {
    n->uid = uid;
    if (rec) for (int i = 0; i < n->nfilhos; i++) chown_rec(n->filhos[i], uid, 1);
}
static int c_chown(Ctx *c, int argc, char **argv) {
    int i = 1, rec = 0, r = 0;
    while (i < argc && argv[i][0] == '-') { if (strchr(argv[i], 'R')) rec = 1; i++; }
    if (argc - i < 2) { ERR("chown: falta operando\n"); return 1; }
    if (vfs_euid != 0) { ERR("chown: alterando o dono de '%s': Operação não permitida\n", argv[i + 1]); return 1; }
    uint32_t uid = (!strncmp(argv[i], "root", 4) || !strncmp(argv[i], "0", 1)) ? 0 : 1000;
    for (i++; i < argc; i++) {
        Inode *n = vfs_achar(c->sh->cwd, argv[i], 1);
        if (!n) { ERR("chown: não foi possível acessar '%s': %s\n", argv[i], vfs_erro(vfs_err)); r = 1; continue; }
        chown_rec(n, uid, rec);
        vfs_sujo = 1;
    }
    return r;
}
static int c_stat(Ctx *c, int argc, char **argv) {
    int r = 0;
    if (argc < 2) { ERR("stat: falta operando\n"); return 1; }
    for (int i = 1; i < argc; i++) {
        Inode *n = vfs_achar(c->sh->cwd, argv[i], 0);
        if (!n) { ERR("stat: não foi possível obter estado de '%s': %s\n", argv[i], vfs_erro(vfs_err)); r = 1; continue; }
        char m[12];
        modo_txt(n->mode, m);
        int a, me, d, h, mi, s, ds;
        k_data(n->mtime, &a, &me, &d, &h, &mi, &s, &ds);
        OUT("  Arquivo: %s%s%s\n", argv[i], EH_LNK(n) ? " -> " : "", EH_LNK(n) ? n->dados : "");
        OUT("  Tamanho: %-10lu Blocos: %-6lu %s\n", (unsigned long)n->tam, (unsigned long)((n->tam + 511) / 512),
            EH_DIR(n) ? "diretório" : EH_LNK(n) ? "link simbólico" : n->tam ? "arquivo comum" : "arquivo comum vazio");
        OUT("Inode: %-10u Links: %d\n", n->ino, EH_DIR(n) ? n->nfilhos + 2 : 1);
        OUT("Acesso: (%04o/%s)  Uid: (%5u/%8s)   Gid: (%5u/%8s)\n", n->mode & 07777, m, n->uid, dono_nome(n->uid), n->uid, dono_nome(n->uid));
        OUT("Modificação: %04d-%02d-%02d %02d:%02d:%02d\n", a, me, d, h, mi, s);
    }
    return r;
}

/* ------------------------------------------------------------------ du, df, find, tree */

static size_t du_rec(Ctx *c, Inode *n, const char *cam, int humano, int so_total, int nivel, int max) {
    size_t t = EH_DIR(n) ? 4096 : (n->tam + 4095) / 4096 * 4096;
    if (!EH_DIR(n)) return t;
    vfs_ordenar(n);
    for (int i = 0; i < n->nfilhos; i++) {
        char *sub = xasprintf("%s%s%s", cam, cam[strlen(cam) - 1] == '/' ? "" : "/", n->filhos[i]->nome);
        t += du_rec(c, n->filhos[i], sub, humano, so_total, nivel + 1, max);
        free(sub);
    }
    if (!so_total && (max < 0 || nivel <= max)) {
        char tam[24];
        if (humano) k_tamanho_humano((double)t, tam, sizeof tam); else snprintf(tam, sizeof tam, "%lu", (unsigned long)(t / 1024));
        OUT("%s\t%s\n", tam, cam);
    }
    return t;
}
static int c_du(Ctx *c, int argc, char **argv) {
    int humano = 0, so_total = 0, max = -1, r = 0, feitos = 0;
    for (int i = 1; i <= argc; i++) {
        const char *a = i < argc ? argv[i] : NULL;
        if (a && a[0] == '-' && a[1]) {
            if (!strncmp(a, "--max-depth=", 12)) max = atoi(a + 12);
            else if (!strcmp(a, "-d") && i + 1 < argc) max = atoi(argv[++i]);
            else { if (strchr(a, 'h')) humano = 1; if (strchr(a, 's')) so_total = 1; }
            continue;
        }
        if (!a && feitos) break;
        const char *cam = a ? a : ".";
        Inode *n = vfs_achar(c->sh->cwd, cam, 0);
        if (!n) { ERR("du: não foi possível acessar '%s': %s\n", cam, vfs_erro(vfs_err)); r = 1; feitos++; continue; }
        size_t t = du_rec(c, n, cam, humano, so_total || !EH_DIR(n), 0, max);
        if (so_total || !EH_DIR(n)) {
            char tam[24];
            if (humano) k_tamanho_humano((double)t, tam, sizeof tam); else snprintf(tam, sizeof tam, "%lu", (unsigned long)(t / 1024));
            OUT("%s\t%s\n", tam, cam);
        }
        feitos++;
    }
    return r;
}
static int c_df(Ctx *c, int argc, char **argv) {
    int humano = argc > 1 && strchr(argv[1], 'h');
    size_t arqs = 0;
    size_t usado = vfs_tamanho_total(vfs_raiz, &arqs) + arqs * 128;
    double total = (double)sis.mem_mb * 1024.0 * 1024.0 / 2;       /* metade da memória do aparelho */
    if (total < 256.0 * 1048576) total = 256.0 * 1048576;
    if (total < usado * 2.0) total = usado * 2.0;
    char a[24], b[24], d[24];
    if (humano) {
        k_tamanho_humano(total, a, sizeof a); k_tamanho_humano((double)usado, b, sizeof b); k_tamanho_humano(total - usado, d, sizeof d);
        OUT("Sist. Arq.      Tam. Usado Disp. Uso%% Montado em\n");
        OUT("%-14s %5s %5s %5s %3d%% /\n", sis.nativo ? "tnuxfs" : "/dev/tnux0", a, b, d, (int)(usado * 100.0 / total));
        OUT("%-14s %5s %5s %5s %3d%% /tmp\n", "tmpfs", a, "0", a, 0);
    } else {
        OUT("Sist. Arq.     Blocos de 1K     Usado Disponível Uso%% Montado em\n");
        OUT("%-14s %12.0f %9lu %10.0f %3d%% /\n", sis.nativo ? "tnuxfs" : "/dev/tnux0", total / 1024, (unsigned long)(usado / 1024), (total - usado) / 1024, (int)(usado * 100.0 / total));
    }
    return 0;
}

typedef struct { const char *nome; int inome; char tipo; int maxprof, minprof; int apagar; long tam_min; } FindOp;
static void find_rec(Ctx *c, FindOp *o, Inode *n, const char *cam, int nivel) {
    int casa = 1;
    if (o->nome) {
        if (o->inome) {
            char *a = xstrdup(o->nome), *b = xstrdup(n->pai ? n->nome : cam);
            for (char *p = a; *p; p++) *p = (char)tolower((unsigned char)*p);
            for (char *p = b; *p; p++) *p = (char)tolower((unsigned char)*p);
            casa = casa_glob(a, b);
            free(a); free(b);
        } else casa = casa_glob(o->nome, nivel == 0 ? nome_base(cam) : n->nome);
    }
    if (casa && o->tipo) casa = (o->tipo == 'd' && EH_DIR(n)) || (o->tipo == 'f' && EH_REG(n)) || (o->tipo == 'l' && EH_LNK(n));
    if (casa && nivel < o->minprof) casa = 0;
    if (EH_DIR(n) && (o->maxprof < 0 || nivel < o->maxprof)) {
        vfs_ordenar(n);
        int nf = n->nfilhos;
        Inode **filhos = xmalloc(sizeof(Inode *) * (size_t)(nf + 1));
        memcpy(filhos, n->filhos, sizeof(Inode *) * (size_t)nf);
        for (int i = 0; i < nf; i++) {
            char *sub = xasprintf("%s%s%s", cam, cam[strlen(cam) - 1] == '/' ? "" : "/", filhos[i]->nome);
            if (!o->apagar && casa && i == 0) { OUT("%s\n", cam); casa = 0; }
            find_rec(c, o, filhos[i], sub, nivel + 1);
            free(sub);
        }
        free(filhos);
    }
    if (casa) {
        if (o->apagar) { if (n != vfs_raiz) vfs_remover(n, 1); }
        else OUT("%s\n", cam);
    }
}
static int c_find(Ctx *c, int argc, char **argv) {
    FindOp o = { NULL, 0, 0, -1, 0, 0, 0 };
    int i = 1, r = 0;
    while (i < argc && argv[i][0] != '-') i++;
    int fim_cam = i;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "-name") && i + 1 < argc) o.nome = argv[++i];
        else if (!strcmp(argv[i], "-iname") && i + 1 < argc) { o.nome = argv[++i]; o.inome = 1; }
        else if (!strcmp(argv[i], "-type") && i + 1 < argc) o.tipo = argv[++i][0];
        else if (!strcmp(argv[i], "-maxdepth") && i + 1 < argc) o.maxprof = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-mindepth") && i + 1 < argc) o.minprof = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-delete")) o.apagar = 1;
        else if (!strcmp(argv[i], "-print")) {}
        else { ERR("find: predicado desconhecido '%s'\n", argv[i]); return 1; }
    }
    for (int k = 1; k < fim_cam || (k == 1 && fim_cam == 1); k++) {
        const char *cam = k < fim_cam ? argv[k] : ".";
        Inode *n = vfs_achar(c->sh->cwd, cam, 0);
        if (!n) { ERR("find: ‘%s’: %s\n", cam, vfs_erro(vfs_err)); r = 1; continue; }
        find_rec(c, &o, n, cam, 0);
        if (fim_cam == 1) break;
    }
    return r;
}

typedef struct { int dirs, arqs, todos, so_dirs, max, cor; } TreeOp;
static void tree_rec(Ctx *c, TreeOp *o, Inode *n, const char *prefixo, int nivel) {
    if (o->max > 0 && nivel >= o->max) return;
    vfs_ordenar(n);
    int total = 0;
    for (int i = 0; i < n->nfilhos; i++) {
        Inode *f = n->filhos[i];
        if ((!o->todos && f->nome[0] == '.') || (o->so_dirs && !EH_DIR(f))) continue;
        total++;
    }
    int k = 0;
    for (int i = 0; i < n->nfilhos; i++) {
        Inode *f = n->filhos[i];
        if ((!o->todos && f->nome[0] == '.') || (o->so_dirs && !EH_DIR(f))) continue;
        int ult = ++k == total;
        const char *cor = o->cor ? ls_cor(f, f) : "";
        OUT("%s%s%s%s%s", prefixo, ult ? "└── " : "├── ", cor, f->nome, *cor ? "\x1b[0m" : "");
        if (EH_LNK(f)) OUT(" -> %s", f->dados ? f->dados : "");
        OUT("\n");
        if (EH_DIR(f)) {
            o->dirs++;
            char *novo = xasprintf("%s%s", prefixo, ult ? "    " : "│   ");
            tree_rec(c, o, f, novo, nivel + 1);
            free(novo);
        } else o->arqs++;
    }
}
static int c_tree(Ctx *c, int argc, char **argv) {
    TreeOp o = { 0, 0, 0, 0, 0, o_tty(c->out) };
    const char *cam = ".";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-L") && i + 1 < argc) o.max = atoi(argv[++i]);
        else if (argv[i][0] == '-') { if (strchr(argv[i], 'a')) o.todos = 1; if (strchr(argv[i], 'd')) o.so_dirs = 1; }
        else cam = argv[i];
    }
    Inode *n = vfs_achar(c->sh->cwd, cam, 1);
    if (!n || !EH_DIR(n)) { OUT("%s  [erro ao abrir o diretório]\n", cam); return 1; }
    OUT("%s%s%s\n", o.cor ? "\x1b[01;34m" : "", cam, o.cor ? "\x1b[0m" : "");
    tree_rec(c, &o, n, "", 0);
    OUT("\n%d diretório%s, %d arquivo%s\n", o.dirs, o.dirs == 1 ? "" : "s", o.arqs, o.arqs == 1 ? "" : "s");
    return 0;
}

/* ------------------------------------------------------------------ nomes e caminhos */

static int c_basename(Ctx *c, int argc, char **argv) {
    if (argc < 2) { ERR("basename: falta operando\n"); return 1; }
    char *limpo = sem_barra_final(argv[1]);
    char *b = xstrdup(nome_base(limpo));
    if (argc > 2) {
        size_t lb = strlen(b), ls = strlen(argv[2]);
        if (lb > ls && !strcmp(b + lb - ls, argv[2])) b[lb - ls] = 0;
    }
    OUT("%s\n", b);
    free(b); free(limpo);
    return 0;
}
static int c_dirname(Ctx *c, int argc, char **argv) {
    if (argc < 2) { ERR("dirname: falta operando\n"); return 1; }
    char *d = sem_barra_final(argv[1]);
    char *b = strrchr(d, '/');
    if (!b) OUT(".\n");
    else if (b == d) OUT("/\n");
    else { *b = 0; OUT("%s\n", d); }
    free(d);
    return 0;
}
static int c_realpath(Ctx *c, int argc, char **argv) {
    int r = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;
        Inode *n = vfs_achar(c->sh->cwd, argv[i], 1);
        if (!n) { ERR("realpath: %s: %s\n", argv[i], vfs_erro(vfs_err)); r = 1; continue; }
        char *p = vfs_caminho(n);
        OUT("%s\n", p);
        free(p);
    }
    return r;
}
static int c_readlink(Ctx *c, int argc, char **argv) {
    if (argc < 2) return 1;
    const char *a = argv[argc - 1];
    Inode *n = vfs_achar(c->sh->cwd, a, 0);
    if (!n) return 1;
    if (argc > 2 && strchr(argv[1], 'f')) { Inode *r = vfs_achar(c->sh->cwd, a, 1); if (!r) return 1; char *p = vfs_caminho(r); OUT("%s\n", p); free(p); return 0; }
    if (!EH_LNK(n)) return 1;
    OUT("%s\n", n->dados ? n->dados : "");
    return 0;
}
static int c_file(Ctx *c, int argc, char **argv) {
    int r = 0;
    for (int i = 1; i < argc; i++) {
        Inode *n = vfs_achar(c->sh->cwd, argv[i], 0);
        if (!n) { OUT("%s: não foi possível abrir (%s)\n", argv[i], vfs_erro(vfs_err)); r = 1; continue; }
        const char *t = "dados";
        const unsigned char *d = (const unsigned char *)(n->dados ? n->dados : "");
        if (EH_DIR(n)) t = "diretório";
        else if (EH_LNK(n)) t = "link simbólico";
        else if (n->tam == 0) t = "vazio";
        else if (n->tam >= 4 && !memcmp(d, "\0asm", 4)) t = "programa WebAssembly (wasm), executável no TNUX";
        else if (n->tam >= 4 && !memcmp(d, "\x7f" "ELF", 4)) t = "executável ELF (programa Linux nativo)";
        else if (n->tam >= 7 && !memcmp(d, "#!tnux ", 7)) t = "programa do sistema TNUX";
        else if (n->tam >= 2 && d[0] == '#' && d[1] == '!') t = "script, texto executável";
        else if (n->tam >= 8 && !memcmp(d, "\x89PNG", 4)) t = "imagem PNG";
        else if (n->tam >= 3 && !memcmp(d, "\xff\xd8\xff", 3)) t = "imagem JPEG";
        else if (n->tam >= 4 && !memcmp(d, "GIF8", 4)) t = "imagem GIF";
        else if (n->tam >= 4 && !memcmp(d, "PK\x03\x04", 4)) t = "arquivo Zip";
        else if (n->tam >= 2 && d[0] == 0x1f && d[1] == 0x8b) t = "dados compactados com gzip";
        else if (n->tam >= 4 && !memcmp(d, "%PDF", 4)) t = "documento PDF";
        else {
            int texto = 1;
            size_t lim = n->tam < 2048 ? n->tam : 2048;
            for (size_t k = 0; k < lim; k++) if (d[k] < 9 || (d[k] > 13 && d[k] < 32 && d[k] != 27)) { texto = 0; break; }
            if (texto) {
                const char *e = strrchr(n->nome, '.');
                if (e && (!strcmp(e, ".html") || !strcmp(e, ".htm"))) t = "documento HTML, texto UTF-8";
                else if (e && !strcmp(e, ".json")) t = "dados JSON";
                else if (e && !strcmp(e, ".c")) t = "código-fonte C, texto UTF-8";
                else if (e && !strcmp(e, ".js")) t = "código-fonte JavaScript, texto UTF-8";
                else t = "texto UTF-8";
            }
        }
        OUT("%s: %s\n", argv[i], t);
    }
    return r;
}
static int c_mktemp(Ctx *c, int argc, char **argv) {
    int dir = argc > 1 && !strcmp(argv[1], "-d");
    unsigned v; host_aleatorio(&v, sizeof v);
    char *cam = xasprintf("/tmp/tmp.%08x", v);
    if (dir) vfs_mkdir("/", cam, 0, 0700); else vfs_gravar("/", cam, "", 0, 0);
    OUT("%s\n", cam);
    free(cam);
    return 0;
}

const Cmd cmds_arq[] = {
    { "ls", c_ls, "lista arquivos e pastas", "ls [-l] [-a] [-h] [-R] [-t] [-S] [-r] [-1] [-F] [caminho...]" },
    { "cat", c_cat, "mostra o conteúdo de arquivos", "cat [-n] arquivo..." },
    { "touch", c_touch, "cria um arquivo vazio ou atualiza a data", "touch arquivo..." },
    { "mkdir", c_mkdir, "cria pastas", "mkdir [-p] pasta..." },
    { "rmdir", c_rmdir, "remove pastas vazias", "rmdir pasta..." },
    { "rm", c_rm, "apaga arquivos e pastas", "rm [-r] [-f] caminho..." },
    { "cp", c_cp, "copia arquivos e pastas", "cp [-r] origem... destino" },
    { "mv", c_mv, "move ou renomeia", "mv origem... destino" },
    { "ln", c_ln, "cria links", "ln -s alvo nome" },
    { "chmod", c_chmod, "muda as permissões", "chmod [-R] 755|u+x arquivo..." },
    { "chown", c_chown, "muda o dono", "sudo chown [-R] usuario|root arquivo..." },
    { "stat", c_stat, "mostra os detalhes de um arquivo", "stat arquivo..." },
    { "du", c_du, "tamanho ocupado por pastas", "du [-h] [-s] [-d N] [caminho...]" },
    { "df", c_df, "espaço do disco", "df [-h]" },
    { "find", c_find, "procura arquivos", "find [caminho] [-name padrão] [-type f|d] [-maxdepth N] [-delete]" },
    { "tree", c_tree, "mostra as pastas em árvore", "tree [-a] [-d] [-L N] [caminho]" },
    { "basename", c_basename, NULL, NULL },
    { "dirname", c_dirname, NULL, NULL },
    { "realpath", c_realpath, NULL, NULL },
    { "readlink", c_readlink, NULL, NULL },
    { "file", c_file, "diz o tipo de um arquivo", "file arquivo..." },
    { "mktemp", c_mktemp, NULL, NULL },
    { NULL, NULL, NULL, NULL }
};
