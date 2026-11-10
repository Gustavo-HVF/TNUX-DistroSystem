/*
 * TNUX: plataforma WebAssembly: a fronteira entre o núcleo em C e o navegador.
 *
 * "Importadas" são as funções que o JavaScript fornece ao núcleo (terminal, relógio, rede...).
 * "Exportadas" são as que o JavaScript chama no núcleo (ligar, mandar teclas, ler arquivos...).
 * O núcleo roda num Web Worker, então pode ficar ocupado sem travar a tela.
 */
#include "../nucleo/tnux.h"
#include <stdlib.h>
#include <string.h>

#define IMPORTA(nome) __attribute__((import_module("env"), import_name(nome)))
#define EXPORTA(nome) __attribute__((export_name(nome)))

IMPORTA("js_write") void js_write(int sid, const char *p, int n);
IMPORTA("js_agora") double js_agora(void);
IMPORTA("js_http") int js_http(const char *metodo, const char *url, const char *cab, const char *corpo, int tam, int *saida);
IMPORTA("js_gui") void js_gui(const char *json, int n);
IMPORTA("js_aleatorio") void js_aleatorio(void *p, int n);
IMPORTA("js_dormir") void js_dormir(int ms);
IMPORTA("js_wasm") int js_wasm(const char *bytes, int n, const char *args, int nargs);
IMPORTA("js_interrompido") int js_interrompido(void);

void host_write(int sid, const char *p, size_t n) { js_write(sid, p, (int)n); }
double host_agora_ms(void) { return js_agora(); }
void host_gui(const char *json) { js_gui(json, (int)strlen(json)); }
void host_aleatorio(void *p, size_t n) { js_aleatorio(p, (int)n); }
void host_dormir(int ms) { js_dormir(ms); }
int host_interrompido(void) { return js_interrompido(); }
int host_wasm(const char *bytes, size_t n, const char *args_json) { return js_wasm(bytes, (int)n, args_json, (int)strlen(args_json)); }
int host_http(const char *metodo, const char *url, const char *cab, const char *corpo_req, size_t tam_req,
              char **corpo, size_t *tam, char **cab_resp) {
    int saida[3] = { 0, 0, 0 };      /* ponteiro do corpo, tamanho, ponteiro dos cabeçalhos (malloc pelo JS) */
    int st = js_http(metodo, url, cab ? cab : "", corpo_req ? corpo_req : "", (int)tam_req, saida);
    *corpo = (char *)(size_t)saida[0];
    *tam = (size_t)saida[1];
    if (cab_resp) *cab_resp = (char *)(size_t)saida[2]; else free((char *)(size_t)saida[2]);
    return st;
}

/* ------------------------------------------------------------------ exportadas */

static Buf res;      /* resultado da última chamada (texto, JSON ou bytes) */

EXPORTA("tnux_alloc") void *tnux_alloc(int n) { return malloc((size_t)(n > 0 ? n : 1)); }
EXPORTA("tnux_free") void tnux_free(void *p) { free(p); }
EXPORTA("tnux_res_ptr") const char *tnux_res_ptr(void) { return buf_str(&res); }
EXPORTA("tnux_res_len") int tnux_res_len(void) { return (int)res.n; }
EXPORTA("tnux_heap_usado") int exp_heap_usado(void) { return (int)tnux_heap_usado(); }
EXPORTA("tnux_heap_total") int exp_heap_total(void) { return (int)tnux_heap_total(); }

EXPORTA("tnux_boot") void tnux_boot(const char *cfg, int ncfg, const char *img, int nimg) { k_boot(cfg, (size_t)ncfg, img, (size_t)nimg); }
EXPORTA("tnux_sessao_abrir") int tnux_sessao_abrir(int sid, int cols, int rows) { return k_abrir_sessao(sid, cols, rows, 1) != NULL; }
EXPORTA("tnux_sessao_fechar") void tnux_sessao_fechar(int sid) { k_fechar_sessao(sid); }
EXPORTA("tnux_entrada") void tnux_entrada(int sid, const char *p, int n) { k_entrada(sid, p, (size_t)n); }
EXPORTA("tnux_redimensionar") void tnux_redimensionar(int sid, int cols, int rows) { k_redimensionar(sid, cols, rows); }
EXPORTA("tnux_sujo") int tnux_sujo(void) { return vfs_sujo; }

/* roda um comando sem terminal e devolve a saída (para os programas da área de trabalho) */
EXPORTA("tnux_rodar") int tnux_rodar(const char *cmd, int root) {
    static Shell *sh;
    if (!sh) sh = sh_novo(0);
    res.n = 0;
    Out o = { &res, 0 };
    uint32_t e = vfs_euid;
    vfs_euid = root ? 0 : 1000;
    sh->sair = sh->quebra = sh->continua = 0;
    int r = sh_rodar(sh, cmd, NULL, 0, &o, &o);
    vfs_euid = e;
    return r;
}

EXPORTA("tnux_fs_listar") int tnux_fs_listar(const char *cam) {
    res.n = 0;
    Inode *d = vfs_achar("/", cam, 1);
    if (!d) return vfs_err;
    if (!EH_DIR(d)) return E_NOTDIR;
    vfs_ordenar(d);
    buf_putc(&res, '[');
    for (int i = 0; i < d->nfilhos; i++) {
        Inode *f = d->filhos[i];
        Inode *real = f;
        if (EH_LNK(f)) { real = vfs_achar(cam, f->dados ? f->dados : "", 1); if (!real) real = f; }
        if (i) buf_putc(&res, ',');
        buf_puts(&res, "{\"nome\":");
        buf_json_str(&res, f->nome);
        buf_printf(&res, ",\"tipo\":\"%s\",\"link\":%s,\"tam\":%lu,\"mtime\":%.0f,\"modo\":%u,\"uid\":%u}",
                   EH_DIR(real) ? "d" : "f", EH_LNK(f) ? "true" : "false", (unsigned long)real->tam, (double)f->mtime, real->mode & 07777, f->uid);
    }
    buf_putc(&res, ']');
    return 0;
}
EXPORTA("tnux_fs_info") int tnux_fs_info(const char *cam) {
    res.n = 0;
    Inode *f = vfs_achar("/", cam, 1);
    if (!f) return vfs_err;
    buf_printf(&res, "{\"tipo\":\"%s\",\"tam\":%lu,\"mtime\":%.0f,\"modo\":%u,\"uid\":%u,\"gravavel\":%s}", EH_DIR(f) ? "d" : "f",
               (unsigned long)f->tam, (double)f->mtime, f->mode & 07777, f->uid, vfs_pode_escrever(f) ? "true" : "false");
    return 0;
}
EXPORTA("tnux_fs_ler") int tnux_fs_ler(const char *cam) {
    res.n = 0;
    Inode *f = vfs_achar("/", cam, 1);
    if (!f) return vfs_err;
    if (EH_DIR(f)) return E_ISDIR;
    buf_put(&res, f->dados ? f->dados : "", f->tam);
    return 0;
}
EXPORTA("tnux_fs_gravar") int tnux_fs_gravar(const char *cam, const char *p, int n, int root) {
    uint32_t e = vfs_euid;
    vfs_euid = root ? 0 : 1000;
    int r = vfs_gravar("/", cam, p, (size_t)n, 0);
    vfs_euid = e;
    return r;
}
EXPORTA("tnux_apps") void tnux_apps(void) { res.n = 0; apt_json_instalados(&res); }
EXPORTA("tnux_imagem") void tnux_imagem(void) { res.n = 0; vfs_imagem_salvar(&res); }
EXPORTA("tnux_erro") const char *tnux_erro(int e) { return vfs_erro(e); }

EXPORTA("tnux_wasi_write") int e_wasi_write(int fd, const char *p, int n) { return k_wasi_write(fd, p, n); }
EXPORTA("tnux_wasi_read") int e_wasi_read(int fd, char *p, int n) { return k_wasi_read(fd, p, n); }
EXPORTA("tnux_wasi_open") int e_wasi_open(const char *cam, int flags) { return k_wasi_open(cam, flags); }
EXPORTA("tnux_wasi_close") int e_wasi_close(int fd) { return k_wasi_close(fd); }
EXPORTA("tnux_wasi_seek") double e_wasi_seek(int fd, double d, int o) { return k_wasi_seek(fd, d, o); }
EXPORTA("tnux_wasi_stat") int e_wasi_stat(const char *cam, int seguir, int *out) { return k_wasi_stat(cam, seguir, out); }
EXPORTA("tnux_wasi_fstat") int e_wasi_fstat(int fd, int *out) { return k_wasi_fstat(fd, out); }
EXPORTA("tnux_wasi_readdir") int e_wasi_readdir(int fd, int i, char *nome, int cap, int *tipo) { return k_wasi_readdir(fd, i, nome, cap, tipo); }
EXPORTA("tnux_wasi_unlink") int e_wasi_unlink(const char *cam, int dir) { return k_wasi_unlink(cam, dir); }
EXPORTA("tnux_wasi_mkdir") int e_wasi_mkdir(const char *cam) { return k_wasi_mkdir(cam); }
EXPORTA("tnux_wasi_rename") int e_wasi_rename(const char *de, const char *para) { return k_wasi_rename(de, para); }
