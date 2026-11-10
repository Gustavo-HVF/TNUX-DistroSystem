/*
 * TNUX: núcleo do sistema (cabeçalho comum).
 *
 * O núcleo é C puro e não fala com o mundo de fora: tudo o que depende de onde ele está rodando
 * (escrever no terminal, hora, rede, abrir janelas) passa pelas funções host_* no fim deste
 * arquivo. Há duas implementações delas:
 *   plataforma/wasm.c   -> navegador (as funções são importadas do JavaScript)
 *   plataforma/nativo.c -> terminal do aparelho (Termux, Linux)
 */
#ifndef TNUX_H
#define TNUX_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#define TNUX_NOME      "TNUX"
#define TNUX_VERSAO    "1.0"
#define TNUX_CODINOME  "Aurora"
#define TNUX_BASE      "Linux Mint"
#define TNUX_KERNEL    "6.8.0-tnux"
#define TNUX_USUARIO   "usuario"
#define TNUX_CASA      "/home/usuario"

/* ------------------------------------------------------------------ buffer que cresce */
typedef struct Buf { char *d; size_t n, cap; } Buf;
void buf_init(Buf *b);
void buf_free(Buf *b);
void buf_reserve(Buf *b, size_t mais);
void buf_put(Buf *b, const void *p, size_t n);
void buf_puts(Buf *b, const char *s);
void buf_putc(Buf *b, int c);
void buf_printf(Buf *b, const char *fmt, ...);
void buf_vprintf(Buf *b, const char *fmt, va_list ap);
char *buf_str(Buf *b);                 /* garante o \0 final e devolve b->d (nunca NULL) */
void buf_json_str(Buf *b, const char *s); /* escreve s como string JSON, com aspas */

char *xstrdup(const char *s);
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xasprintf(const char *fmt, ...);

/* ------------------------------------------------------------------ sistema de arquivos em memória */
#define T_FMT  0170000u
#define T_DIR  0040000u
#define T_REG  0100000u
#define T_LNK  0120000u
#define EH_DIR(n) (((n)->mode & T_FMT) == T_DIR)
#define EH_REG(n) (((n)->mode & T_FMT) == T_REG)
#define EH_LNK(n) (((n)->mode & T_FMT) == T_LNK)

enum { E_OK = 0, E_NOENT = 1, E_EXIST, E_NOTDIR, E_ISDIR, E_NOTEMPTY, E_ACCES, E_INVAL, E_LOOP, E_BUSY, E_NOMEM };
const char *vfs_erro(int e);

typedef struct Inode Inode;
struct Inode {
    uint32_t ino;
    uint32_t mode;       /* tipo (T_*) + permissões */
    uint32_t uid;
    int64_t mtime;       /* segundos desde 1970 */
    Inode *pai;
    char *nome;
    Inode **filhos; int nfilhos, capfilhos;   /* diretório */
    char *dados; size_t tam, cap;             /* arquivo, ou destino do link simbólico */
};

extern Inode *vfs_raiz;
extern int vfs_sujo;        /* mudou desde a última gravação da imagem */
extern uint32_t vfs_euid;   /* usuário efetivo: 1000 = usuario, 0 = root (sudo) */
extern int vfs_err;         /* motivo da última falha */

void vfs_iniciar(void);
char *vfs_abs(const char *cwd, const char *caminho);               /* caminho absoluto limpo (malloc) */
Inode *vfs_achar(const char *cwd, const char *caminho, int seguir); /* NULL + vfs_err */
Inode *vfs_filho(Inode *dir, const char *nome);
char *vfs_caminho(Inode *n);                                       /* caminho absoluto (malloc) */
Inode *vfs_mkdir(const char *cwd, const char *caminho, int pais, uint32_t modo);
Inode *vfs_criar(const char *cwd, const char *caminho, uint32_t modo); /* cria ou acha arquivo comum */
int vfs_gravar(const char *cwd, const char *caminho, const void *d, size_t n, int acrescentar);
int vfs_gravar_no(Inode *f, const void *d, size_t n, int acrescentar);
int vfs_remover(Inode *n, int recursivo);
int vfs_renomear(const char *cwd, const char *de, const char *para);
int vfs_link(const char *cwd, const char *destino, const char *caminho);
int vfs_pode_escrever(Inode *n);
int vfs_pode_ler(Inode *n);
void vfs_ordenar(Inode *dir);
size_t vfs_tamanho_total(Inode *n, size_t *arquivos);
void vfs_imagem_salvar(Buf *saida);
int vfs_imagem_carregar(const char *d, size_t n);
/* atalhos para o sistema montar a própria árvore (ignoram permissões) */
void vfs_sis_dir(const char *caminho, uint32_t modo, uint32_t uid);
void vfs_sis_arq(const char *caminho, const char *conteudo, uint32_t modo, uint32_t uid);

/* ------------------------------------------------------------------ JSON */
typedef enum { J_NULO, J_BOOL, J_NUM, J_STR, J_LISTA, J_OBJ } JTipo;
typedef struct JVal JVal;
struct JVal {
    JTipo tipo;
    double num;
    char *str;
    JVal **itens; char **chaves; int n;   /* lista: itens; objeto: chaves + itens */
};
JVal *json_ler(const char *txt, size_t n);
void json_livrar(JVal *v);
JVal *json_obter(const JVal *obj, const char *chave);
const char *json_texto(const JVal *obj, const char *chave, const char *padrao);
double json_numero(const JVal *obj, const char *chave, double padrao);

/* ------------------------------------------------------------------ editor de linha (terminal) */
typedef struct Editor Editor;
typedef void (*EdEscreve)(void *quem, const char *p, size_t n);
/* devolve as opções de completar para a palavra em [ini, pos) da linha; *n recebe quantas */
typedef char **(*EdCompleta)(void *quem, const char *linha, size_t ini, size_t pos, int *n);
enum { ED_NADA = 0, ED_LINHA, ED_EOF, ED_INTERROMPE };
Editor *ed_novo(EdEscreve escreve, EdCompleta completa, void *quem);
void ed_livrar(Editor *e);
void ed_prompt(Editor *e, const char *prompt);   /* define e desenha o prompt, linha vazia */
void ed_colunas(Editor *e, int colunas);
/* alimenta um byte vindo do teclado; ED_LINHA quando o usuário apertou Enter (ver ed_linha) */
int ed_byte(Editor *e, unsigned char c);
const char *ed_linha(Editor *e);
void ed_hist_add(Editor *e, const char *linha);
int ed_hist_n(Editor *e);
const char *ed_hist(Editor *e, int i);

/* ------------------------------------------------------------------ shell */
typedef struct Var { char *k, *v; int exportada; } Var;

typedef struct Shell {
    int sid;                 /* sessão (um terminal aberto) */
    char *cwd;
    Var *vars; int nvars, capvars;
    Var *apelidos; int napelidos, capapelidos;
    char **pos; int npos;    /* $1 $2 ... */
    char *nome0;             /* $0 */
    int ultimo;              /* $? */
    int sair;                /* pediu exit */
    int nivel;               /* profundidade de scripts/substituições */
    int interativo;
    int quebra, continua;    /* break / continue em laços */
    int retorna, valor_retorno;   /* return dentro de função */
    char **fnomes; void **funcs; int nfuncs;   /* funções definidas (nome e corpo) */
    void **lixo; int nlixo;  /* corpos de funções redefinidas, a liberar */
    Editor *ed;
    Buf pendente;            /* linhas incompletas (aspas abertas, if sem fi...) */
    int cols, rows;
} Shell;

typedef struct Out { Buf *buf; int sid; } Out;   /* buf == NULL -> direto no terminal da sessão */

typedef struct Ctx {
    Shell *sh;
    const char *in; size_t inlen;   /* entrada padrão (vinda de um pipe ou de <) */
    Out *out, *err;
} Ctx;

typedef int (*CmdFn)(Ctx *c, int argc, char **argv);
typedef struct Cmd { const char *nome; CmdFn fn; const char *resumo; const char *uso; } Cmd;

void o_write(Out *o, const char *p, size_t n);
void o_puts(Out *o, const char *s);
void o_printf(Out *o, const char *fmt, ...);
int o_tty(Out *o);                    /* a saída é o terminal? (para decidir cores) */
#define OUT(...) o_printf(c->out, __VA_ARGS__)
#define ERR(...) o_printf(c->err, __VA_ARGS__)

Shell *sh_novo(int sid);
void sh_livrar(Shell *sh);
const char *sh_get(Shell *sh, const char *k);
void sh_set(Shell *sh, const char *k, const char *v, int exportar);
void sh_unset(Shell *sh, const char *k);
/* roda um texto de shell (uma linha ou um script inteiro). Devolve o código de saída. */
int sh_rodar(Shell *sh, const char *texto, const char *in, size_t inlen, Out *out, Out *err);
/* 1 se o texto está incompleto (aspas abertas, "if" sem "fi"...) */
int sh_incompleto(const char *texto);
void sh_limpar(Shell *sh);   /* entre um comando e outro do terminal */
int sh_exec_argv(Ctx *c, int argc, char **argv);   /* roda um comando já separado em argumentos */
char *sh_prompt(Shell *sh);
const Cmd *cmd_achar(const char *nome);
char *sh_resolver(Shell *sh, const char *nome);    /* acha o programa no PATH (malloc) ou NULL */
char **sh_completar(void *quem, const char *linha, size_t ini, size_t pos, int *n);
int casa_glob(const char *padrao, const char *texto);

/* expressões regulares simples (grep, sed): . * + ? ^ $ [abc] [^abc] \d \w \s e | no topo */
int re_buscar(const char *re, const char *texto, int ignora_caixa, int *ini, int *fim);

/* tabelas de comandos, uma por arquivo */
extern const Cmd cmds_shell[], cmds_arq[], cmds_txt[], cmds_sis[], cmds_rede[], cmds_apt[];
extern const Cmd *const cmds_todas[];

/* ------------------------------------------------------------------ pacotes */
void apt_instalar_base(void);                 /* registra os programas que já vêm no sistema */
int apt_json_instalados(Buf *saida);          /* lista de apps do menu (JSON) */
int wasi_rodar(Ctx *c, Inode *prog, int argc, char **argv);
/* chamadas de sistema dos programas WebAssembly (o host as repassa para cá) */
int k_wasi_write(int fd, const char *p, int n);
int k_wasi_read(int fd, char *p, int n);
int k_wasi_open(const char *cam, int flags);
int k_wasi_close(int fd);
double k_wasi_seek(int fd, double desloc, int de_onde);
int k_wasi_stat(const char *cam, int seguir, int *out);
int k_wasi_fstat(int fd, int *out);
int k_wasi_readdir(int fd, int i, char *nome, int cap, int *tipo);
int k_wasi_unlink(const char *cam, int dir);
int k_wasi_mkdir(const char *cam);
int k_wasi_rename(const char *de, const char *para);

/* ------------------------------------------------------------------ kernel */
typedef struct Sistema {
    double boot_ms;          /* quando ligou */
    int cpus;
    long mem_mb;
    char *agente;            /* navegador / aparelho */
    char *plataforma;
    char *idioma;
    char *host;              /* nome da máquina */
    int fuso_min;            /* minutos a somar ao UTC para a hora local */
    int nativo;              /* 1 = rodando no terminal, 0 = navegador */
    Buf dmesg;
} Sistema;
extern Sistema sis;

void k_log(const char *fmt, ...);             /* mensagem de boot (dmesg) */
void k_montar_base(void);                     /* cria/atualiza a árvore do sistema */
Shell *k_sessao(int sid);                     /* acha a sessão (NULL se não existe) */
Shell *k_abrir_sessao(int sid, int cols, int rows, int interativa);
void k_fechar_sessao(int sid);
void k_boot(const char *cfg_json, size_t n, const char *imagem, size_t tam_imagem);
void k_entrada(int sid, const char *p, size_t n);   /* teclas vindas do terminal */
void k_redimensionar(int sid, int cols, int rows);
void k_data(int64_t t, int *ano, int *mes, int *dia, int *h, int *mi, int *s, int *dsem);
int64_t k_agora(void);                        /* segundos desde 1970 (UTC) */
char *k_tamanho_humano(double bytes, char *dst, size_t n);

/* ------------------------------------------------------------------ plataforma (host) */
void host_write(int sid, const char *p, size_t n);
double host_agora_ms(void);
/* requisição HTTP síncrona. Devolve o status (0 = falhou; *corpo recebe a mensagem de erro).
 * *corpo é malloc'ado pelo host e deve ser liberado com free(). cab_resp (opcional) recebe os
 * cabeçalhos da resposta. */
int host_http(const char *metodo, const char *url, const char *cabecalhos,
              const char *corpo_req, size_t tam_req, char **corpo, size_t *tam, char **cab_resp);
void host_gui(const char *json);              /* pede algo à área de trabalho (abrir janela...) */
void host_aleatorio(void *p, size_t n);
void host_dormir(int ms);
/* roda um programa WebAssembly (WASI). Devolve o código de saída, ou -1 se a plataforma não roda. */
int host_wasm(const char *bytes, size_t n, const char *args_json);
int host_interrompido(void);                  /* o usuário apertou Ctrl+C? */

#endif
