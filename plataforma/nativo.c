/*
 * TNUX: plataforma nativa: o mesmo sistema do navegador rodando direto no terminal do aparelho
 * ("tnux --sandbox"). O disco continua sendo em memória (gravado em ~/.tnux/sandbox.img), então
 * os comandos NÃO mexem nos arquivos de verdade. Serve para experimentar e para os testes.
 *
 * (O modo normal do "tnux" no terminal é a ponte, em ponte/tnux.c: lá os comandos são do aparelho.)
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "../nucleo/tnux.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

static volatile sig_atomic_t interrompido;
static int quer_sair, codigo_saida;
static char *raiz_web;

void host_write(int sid, const char *p, size_t n) {
    (void)sid;
    while (n) {
        ssize_t w = write(1, p, n);
        if (w < 0) { if (errno == EINTR) continue; return; }
        p += w; n -= (size_t)w;
    }
}
double host_agora_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}
void host_aleatorio(void *p, size_t n) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) { if (read(fd, p, n) < 0) memset(p, 4, n); close(fd); }
}
void host_dormir(int ms) { usleep((useconds_t)ms * 1000); }
int host_interrompido(void) { if (interrompido) { interrompido = 0; return 1; } return 0; }
int host_wasm(const char *bytes, size_t n, const char *args_json) { (void)bytes; (void)n; (void)args_json; return -1; }
void host_gui(const char *json) {
    if (strstr(json, "\"energia\"") || strstr(json, "\"fechar_terminal\"")) quer_sair = 1;
}

static char *ler_arquivo(const char *cam, size_t *tam) {
    FILE *f = fopen(cam, "rb");
    if (!f) return NULL;
    Buf b; buf_init(&b);
    char tmp[65536]; size_t n;
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) buf_put(&b, tmp, n);
    fclose(f);
    *tam = b.n;
    return buf_str(&b);
}

/* a rede, no terminal, é o curl do aparelho */
int host_http(const char *metodo, const char *url, const char *cab, const char *corpo_req, size_t tam_req,
              char **corpo, size_t *tam, char **cab_resp) {
    *corpo = NULL; *tam = 0;
    if (cab_resp) *cab_resp = NULL;
    if (!strstr(url, "://")) {              /* caminho relativo: arquivo da pasta web/ */
        char cam[1024];
        snprintf(cam, sizeof cam, "%s/%s", raiz_web ? raiz_web : ".", url);
        *corpo = ler_arquivo(cam, tam);
        if (!*corpo) { *corpo = xstrdup("arquivo local não encontrado"); return 0; }
        return 200;
    }
    char arq_corpo[] = "/tmp/tnux-http-XXXXXX", arq_cab[] = "/tmp/tnux-cab-XXXXXX", arq_req[] = "/tmp/tnux-req-XXXXXX";
    const char *tmp = getenv("TMPDIR");
    char b1[512], b2[512], b3[512];
    if (tmp && *tmp) {
        snprintf(b1, sizeof b1, "%s/tnux-http-XXXXXX", tmp); snprintf(b2, sizeof b2, "%s/tnux-cab-XXXXXX", tmp); snprintf(b3, sizeof b3, "%s/tnux-req-XXXXXX", tmp);
    } else { strcpy(b1, arq_corpo); strcpy(b2, arq_cab); strcpy(b3, arq_req); }
    int f1 = mkstemp(b1), f2 = mkstemp(b2), f3 = -1;
    if (f1 < 0 || f2 < 0) { *corpo = xstrdup("não foi possível criar arquivo temporário"); return 0; }
    close(f1); close(f2);
    char *args[64]; int na = 0;
    args[na++] = "curl"; args[na++] = "-sS"; args[na++] = "-L"; args[na++] = "--max-time"; args[na++] = "120";
    args[na++] = "-X"; args[na++] = (char *)metodo;
    if (!strcmp(metodo, "HEAD")) args[na++] = "-I";
    args[na++] = "-o"; args[na++] = b1; args[na++] = "-D"; args[na++] = b2; args[na++] = "-w"; args[na++] = "%{http_code}";
    char *copia = xstrdup(cab ? cab : "");
    for (char *l = strtok(copia, "\n"); l && na < 56; l = strtok(NULL, "\n")) { args[na++] = "-H"; args[na++] = l; }
    if (corpo_req && tam_req) {
        f3 = mkstemp(b3);
        if (f3 >= 0) { if (write(f3, corpo_req, tam_req) < 0) {} close(f3); }
        char *ref = xasprintf("@%s", b3);
        args[na++] = "--data-binary"; args[na++] = ref;
    }
    args[na++] = (char *)url; args[na] = NULL;
    int cano[2], cano_err[2];
    if (pipe(cano) < 0 || pipe(cano_err) < 0) { free(copia); *corpo = xstrdup("pipe falhou"); return 0; }
    pid_t pid = fork();
    if (pid == 0) {
        dup2(cano[1], 1); dup2(cano_err[1], 2);
        close(cano[0]); close(cano[1]); close(cano_err[0]); close(cano_err[1]);
        execvp("curl", args);
        _exit(127);
    }
    close(cano[1]); close(cano_err[1]);
    char cod[32] = "", erro[512] = "";
    ssize_t r = read(cano[0], cod, sizeof cod - 1);
    if (r > 0) cod[r] = 0;
    r = read(cano_err[0], erro, sizeof erro - 1);
    if (r > 0) erro[r] = 0;
    close(cano[0]); close(cano_err[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    free(copia);
    int status = atoi(cod);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0 || status == 0) {
        size_t le = strlen(erro);
        while (le && (erro[le - 1] == '\n' || erro[le - 1] == '\r')) erro[--le] = 0;
        *corpo = xstrdup(WIFEXITED(st) && WEXITSTATUS(st) == 127 ? "o programa curl não está instalado no aparelho" : (*erro ? erro : "falha de rede"));
        *tam = strlen(*corpo);
        status = 0;
    } else {
        *corpo = ler_arquivo(b1, tam);
        if (!*corpo) { *corpo = xstrdup(""); *tam = 0; }
        if (cab_resp) { size_t tc; *cab_resp = ler_arquivo(b2, &tc); }
    }
    unlink(b1); unlink(b2);
    if (f3 >= 0) unlink(b3);
    return status;
}

/* ------------------------------------------------------------------ onde está a pasta do TNUX */

char *nativo_raiz(void) {
    const char *e = getenv("TNUX_RAIZ");
    if (e && *e) return xstrdup(e);
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        char *b = strrchr(exe, '/');
        if (b) {
            *b = 0;
            char teste[4200];
            snprintf(teste, sizeof teste, "%s/../web/index.html", exe);
            if (access(teste, R_OK) == 0) { b = strrchr(exe, '/'); if (b) *b = 0; return xstrdup(exe); }
            snprintf(teste, sizeof teste, "%s/web/index.html", exe);
            if (access(teste, R_OK) == 0) return xstrdup(exe);
        }
    }
    return xstrdup(".");
}

/* ------------------------------------------------------------------ terminal */

static struct termios termios_salvo;
static int cru;
static void restaurar(void) { if (cru) { tcsetattr(0, TCSAFLUSH, &termios_salvo); cru = 0; } }
static void ao_sinal(int s) { if (s == SIGINT) interrompido = 1; else { restaurar(); _exit(128 + s); } }

static void salvar_imagem(const char *cam) {
    if (!cam || !vfs_sujo) return;
    Buf b; buf_init(&b);
    vfs_imagem_salvar(&b);
    char *tmp = xasprintf("%s.tmp", cam);
    FILE *f = fopen(tmp, "wb");
    if (f) {
        fwrite(b.d, 1, b.n, f);
        fclose(f);
        rename(tmp, cam);
    }
    free(tmp);
    buf_free(&b);
}

int sandbox_main(int argc, char **argv) {
    const char *cmd = NULL, *imagem = NULL;
    int sem_disco = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) cmd = argv[++i];
        else if (!strcmp(argv[i], "--imagem") && i + 1 < argc) imagem = argv[++i];
        else if (!strcmp(argv[i], "--sem-disco")) sem_disco = 1;
    }
    char *raiz = nativo_raiz();
    raiz_web = xasprintf("%s/web", raiz);
    char *img_padrao = NULL;
    if (!imagem && !sem_disco) {
        const char *casa = getenv("HOME");
        if (casa) {
            char *dir = xasprintf("%s/.tnux", casa);
            mkdir(dir, 0700);
            img_padrao = xasprintf("%s/sandbox.img", dir);
            imagem = img_padrao;
            free(dir);
        }
    }
    /* configuração do boot */
    size_t tcat = 0, timg = 0;
    char *cam_cat = xasprintf("%s/catalogo.json", raiz_web);
    char *cat = ler_arquivo(cam_cat, &tcat);
    char *img = imagem ? ler_arquivo(imagem, &timg) : NULL;
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    long pags = sysconf(_SC_PHYS_PAGES), tpag = sysconf(_SC_PAGE_SIZE);
    time_t agora = time(NULL);
    struct tm lt;
    localtime_r(&agora, &lt);
    char host[128] = "tnux";
    gethostname(host, sizeof host - 1);
    Buf cfg; buf_init(&cfg);
    buf_printf(&cfg, "{\"nativo\":1,\"cpus\":%ld,\"mem_mb\":%ld,\"fuso_min\":%ld,\"host\":", cpus > 0 ? cpus : 1,
               pags > 0 && tpag > 0 ? (long)((double)pags * (double)tpag / 1048576.0) : 1024, (long)(lt.tm_gmtoff / 60));
    buf_json_str(&cfg, host);
    buf_puts(&cfg, ",\"plataforma\":");
    buf_json_str(&cfg, getenv("PREFIX") && strstr(getenv("PREFIX"), "com.termux") ? "Termux (Android)" : "terminal Linux");
    buf_puts(&cfg, ",\"agente\":\"terminal\"");
    if (cat) { buf_puts(&cfg, ",\"catalogo\":"); buf_json_str(&cfg, cat); }
    buf_putc(&cfg, '}');
    k_boot(cfg.d, cfg.n, img, timg);
    buf_free(&cfg);
    free(cat); free(img); free(cam_cat);

    struct winsize ws;
    int cols = 80, rows = 24;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col) { cols = ws.ws_col; rows = ws.ws_row; }

    if (cmd) {
        Shell *sh = k_abrir_sessao(1, cols, rows, 0);
        int r;
        if (isatty(1)) {
            Out o = { NULL, 1 };
            r = sh_rodar(sh, cmd, NULL, 0, &o, &o);
        } else {                       /* saída para arquivo ou pipe: texto puro, sem \r nem cores */
            Buf saida, erros; buf_init(&saida); buf_init(&erros);
            Out o = { &saida, 1 }, e = { &erros, 1 };
            r = sh_rodar(sh, cmd, NULL, 0, &o, &e);
            if (saida.n) fwrite(saida.d, 1, saida.n, stdout);
            if (erros.n) fwrite(erros.d, 1, erros.n, stderr);
            buf_free(&saida); buf_free(&erros);
        }
        salvar_imagem(imagem);
        free(img_padrao);
        return r;
    }
    if (!isatty(0)) { fprintf(stderr, "tnux: o modo sandbox interativo precisa de um terminal (ou use -c 'comando')\n"); return 1; }
    tcgetattr(0, &termios_salvo);
    struct termios t = termios_salvo;
    t.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    t.c_oflag &= ~(tcflag_t)OPOST;
    t.c_cflag |= CS8;
    t.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &t);
    cru = 1;
    atexit(restaurar);
    signal(SIGINT, ao_sinal); signal(SIGTERM, ao_sinal); signal(SIGHUP, ao_sinal);
    host_write(1, "\x1b[1;32mTNUX " TNUX_VERSAO " (sandbox)\x1b[0m: sistema em memória, não mexe nos arquivos do aparelho. \"exit\" para sair.\r\n", 115);
    k_abrir_sessao(1, cols, rows, 1);
    char buf[1024];
    while (!quer_sair) {
        ssize_t n = read(0, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col) k_redimensionar(1, ws.ws_col, ws.ws_row);
        k_entrada(1, buf, (size_t)n);
        salvar_imagem(imagem);
    }
    restaurar();
    salvar_imagem(imagem);
    free(img_padrao);
    return codigo_saida;
}
