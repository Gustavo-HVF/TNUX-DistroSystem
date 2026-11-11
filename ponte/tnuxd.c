/*
 * tnuxd: o servidor da ponte do TNUX.
 *
 * Roda no aparelho (Termux, Linux) e liga a área de trabalho do navegador ao sistema de verdade:
 *   - serve a página (pasta web/);
 *   - /ws/pty      um terminal real (PTY) por WebSocket: é o shell do aparelho;
 *   - /api/fs/...  arquivos e pastas do aparelho, para o gerenciador de arquivos e o editor;
 *   - /api/apps    programas instalados (os .desktop do sistema e os apps web do TNUX);
 *   - /api/lancar  abre um programa gráfico do aparelho;
 *   - /ws/tela     a tela (VNC) em que esses programas aparecem, vista de dentro do TNUX.
 *
 * É um servidor de um processo só, com poll(), sem bibliotecas além da libc. Só escuta em
 * 127.0.0.1 e tudo o que mexe no aparelho exige a chave (token) mostrada ao iniciar.
 */
#define _GNU_SOURCE
#include "comum.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <termios.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#define VERSAO "1.0"
#define MAX_CON 128
#define MAX_CORPO (512LL * 1048576)

/* ------------------------------------------------------------------ buffer */

typedef struct { char *d; size_t n, cap; } B;
static void b_res(B *b, size_t mais) {
    if (b->n + mais + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap * 2 : 256;
    while (cap < b->n + mais + 1) cap *= 2;
    b->d = realloc(b->d, cap);
    if (!b->d) { fprintf(stderr, "tnuxd: sem memória\n"); exit(1); }
    b->cap = cap;
}
static void b_put(B *b, const void *p, size_t n) { b_res(b, n); memcpy(b->d + b->n, p, n); b->n += n; b->d[b->n] = 0; }
static void b_puts(B *b, const char *s) { b_put(b, s, strlen(s)); }
static void b_printf(B *b, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n > 0) { b_res(b, (size_t)n + 1); vsnprintf(b->d + b->n, (size_t)n + 1, fmt, ap2); b->n += (size_t)n; }
    va_end(ap2);
}
static void b_tira(B *b, size_t n) { if (n >= b->n) b->n = 0; else { memmove(b->d, b->d + n, b->n - n); b->n -= n; } if (b->d) b->d[b->n] = 0; }
static void b_livra(B *b) { free(b->d); b->d = NULL; b->n = b->cap = 0; }
static void b_json(B *b, const char *s) {
    b_put(b, "\"", 1);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { b_put(b, "\\", 1); b_put(b, s, 1); }
        else if (c == '\n') b_puts(b, "\\n");
        else if (c == '\r') b_puts(b, "\\r");
        else if (c == '\t') b_puts(b, "\\t");
        else if (c < 32) b_printf(b, "\\u%04x", c);
        else b_put(b, s, 1);
    }
    b_put(b, "\"", 1);
}

/* ------------------------------------------------------------------ SHA-1 e base64 (para o WebSocket) */

static void sha1(const unsigned char *msg, size_t n, unsigned char out[20]) {
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    size_t total = ((n + 8) / 64 + 1) * 64;
    unsigned char *m = calloc(1, total);
    memcpy(m, msg, n);
    m[n] = 0x80;
    uint64_t bits = (uint64_t)n * 8;
    for (int i = 0; i < 8; i++) m[total - 1 - i] = (unsigned char)(bits >> (8 * i));
    for (size_t o = 0; o < total; o += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)m[o + i * 4] << 24 | (uint32_t)m[o + i * 4 + 1] << 16 | (uint32_t)m[o + i * 4 + 2] << 8 | m[o + i * 4 + 3];
        for (int i = 16; i < 80; i++) { uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]; w[i] = x << 1 | x >> 31; }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = (a << 5 | a >> 27) + f + e + k + w[i];
            e = d; d = c; c = b << 30 | b >> 2; b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    free(m);
    for (int i = 0; i < 5; i++) { out[i * 4] = (unsigned char)(h[i] >> 24); out[i * 4 + 1] = (unsigned char)(h[i] >> 16); out[i * 4 + 2] = (unsigned char)(h[i] >> 8); out[i * 4 + 3] = (unsigned char)h[i]; }
}
static void base64(const unsigned char *d, size_t n, char *out) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)d[i] << 16 | (i + 1 < n ? (unsigned)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        out[o++] = t[v >> 18]; out[o++] = t[(v >> 12) & 63];
        out[o++] = i + 1 < n ? t[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[o] = 0;
}

/* ------------------------------------------------------------------ estado global */

enum { C_HTTP, C_WS_PTY, C_WS_TELA };
typedef struct {
    int fd, tipo, fechar;
    B in, out;
    int arq; long long arq_resta;        /* arquivo sendo enviado aos poucos */
    int alvo; pid_t pid; B alvo_out;     /* PTY ou socket da tela, e o que falta escrever nele */
    B msg; int msg_op;                   /* mensagem WebSocket em pedaços */
    time_t nasceu;
} Con;

static Con cons[MAX_CON];
static char *RAIZ, *PASTA, TOKEN[80];
static int porta = 8096, rede = 0, verboso = 0;
static char hosts_extra[8][128];
static int n_hosts_extra;
static volatile sig_atomic_t parar;

static struct { pid_t pid, wm; int display, larg, alt; char sock[256]; int porta_tcp; } tela;

static void registrar(const char *fmt, ...) {
    if (!verboso) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void nao_bloquear(int fd) { int f = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, f | O_NONBLOCK); }

static Con *con_nova(int fd) {
    for (int i = 0; i < MAX_CON; i++) {
        if (cons[i].fd > 0) continue;
        memset(&cons[i], 0, sizeof cons[i]);
        cons[i].fd = fd; cons[i].arq = -1; cons[i].alvo = -1;
        cons[i].nasceu = time(NULL);
        return &cons[i];
    }
    return NULL;
}
static void con_fechar(Con *c) {
    if (c->fd <= 0) return;
    close(c->fd);
    if (c->arq >= 0) close(c->arq);
    if (c->alvo >= 0) close(c->alvo);
    if (c->pid > 0) { kill(-c->pid, SIGHUP); kill(c->pid, SIGHUP); }
    b_livra(&c->in); b_livra(&c->out); b_livra(&c->alvo_out); b_livra(&c->msg);
    c->fd = 0;
}

/* ------------------------------------------------------------------ pedido HTTP */

typedef struct {
    char metodo[12], caminho[4096], consulta[4096];
    const char *cab; size_t ncab;        /* bloco de cabeçalhos */
    const char *corpo; size_t ncorpo;
} Req;

/* valor de um cabeçalho (sem diferenciar maiúsculas). Devolve 1 se achou. */
static int cabecalho(const Req *r, const char *nome, char *dst, size_t n) {
    size_t ln = strlen(nome);
    const char *p = r->cab, *fim = r->cab + r->ncab;
    while (p < fim) {
        const char *nl = memchr(p, '\n', (size_t)(fim - p));
        size_t l = nl ? (size_t)(nl - p) : (size_t)(fim - p);
        if (l > ln && !strncasecmp(p, nome, ln) && p[ln] == ':') {
            const char *v = p + ln + 1;
            while (v < p + l && (*v == ' ' || *v == '\t')) v++;
            size_t lv = (size_t)(p + l - v);
            while (lv && (v[lv - 1] == '\r' || v[lv - 1] == ' ')) lv--;
            if (lv >= n) lv = n - 1;
            memcpy(dst, v, lv); dst[lv] = 0;
            return 1;
        }
        if (!nl) break;
        p = nl + 1;
    }
    dst[0] = 0;
    return 0;
}
static int hexv(int c) { return isdigit(c) ? c - '0' : isxdigit(c) ? tolower(c) - 'a' + 10 : -1; }
static void url_decodificar(const char *s, size_t n, char *dst, size_t cap, int mais_espaco) {
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < cap; i++) {
        if (s[i] == '%' && i + 2 < n && hexv((unsigned char)s[i + 1]) >= 0 && hexv((unsigned char)s[i + 2]) >= 0) {
            dst[o++] = (char)(hexv((unsigned char)s[i + 1]) * 16 + hexv((unsigned char)s[i + 2]));
            i += 2;
        } else if (s[i] == '+' && mais_espaco) dst[o++] = ' ';
        else dst[o++] = s[i];
    }
    dst[o] = 0;
}
static int parametro(const Req *r, const char *nome, char *dst, size_t cap) {
    size_t ln = strlen(nome);
    for (const char *p = r->consulta; *p;) {
        size_t l = strcspn(p, "&");
        if (l > ln && !strncmp(p, nome, ln) && p[ln] == '=') { url_decodificar(p + ln + 1, l - ln - 1, dst, cap, 1); return 1; }
        p += l;
        if (*p == '&') p++;
    }
    dst[0] = 0;
    return 0;
}

static int igual_seguro(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    unsigned char d = (unsigned char)(la ^ lb);
    for (size_t i = 0; i < la && i < lb; i++) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0 && la == lb;
}
/* de onde veio a chave: 1 cabeçalho, 2 endereço, 3 cookie; 0 = sem chave válida */
static int autenticado(const Req *r) {
    char v[512];
    if (cabecalho(r, "X-TNUX-Token", v, sizeof v) && igual_seguro(v, TOKEN)) return 1;
    if (parametro(r, "token", v, sizeof v) && igual_seguro(v, TOKEN)) return 2;
    if (cabecalho(r, "Cookie", v, sizeof v)) {
        const char *p = strstr(v, "tnux_token=");
        if (p) {
            p += 11;
            char t[128]; size_t l = strcspn(p, "; ");
            if (l < sizeof t) { memcpy(t, p, l); t[l] = 0; if (igual_seguro(t, TOKEN)) return 3; }
        }
    }
    return 0;
}
/* o pedido veio para um endereço nosso? (protege contra páginas de outros sites apontando para cá) */
static int host_ok(const Req *r) {
    char h[256];
    if (!cabecalho(r, "Host", h, sizeof h)) return 0;
    if (rede) return 1;
    char *dp = h[0] == '[' ? strchr(h, ']') : strrchr(h, ':');
    if (dp) { if (*dp == ']') dp++; *dp = 0; }
    if (!strcmp(h, "127.0.0.1") || !strcasecmp(h, "localhost") || !strcmp(h, "[::1]")) return 1;
    for (int i = 0; i < n_hosts_extra; i++) if (!strcasecmp(h, hosts_extra[i])) return 1;
    return 0;
}
static int origem_ok(const Req *r) {
    char o[512], h[256];
    if (!cabecalho(r, "Origin", o, sizeof o) || !*o) return 1;      /* pedidos sem Origin: mesma página, curl... */
    cabecalho(r, "Host", h, sizeof h);
    const char *p = strstr(o, "://");
    return p && !strcasecmp(p + 3, h);
}

/* ------------------------------------------------------------------ respostas */

static const char *texto_status(int s) {
    switch (s) {
    case 200: return "OK"; case 206: return "Partial Content"; case 101: return "Switching Protocols";
    case 400: return "Bad Request"; case 401: return "Unauthorized"; case 403: return "Forbidden"; case 404: return "Not Found";
    case 405: return "Method Not Allowed"; case 409: return "Conflict"; case 413: return "Payload Too Large"; case 416: return "Range Not Satisfiable";
    default: return "Error";
    }
}
static void resp_cab(Con *c, int status, const char *tipo, long long tam, const char *extra) {
    b_printf(&c->out, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lld\r\nConnection: close\r\n"
             "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n%s\r\n",
             status, texto_status(status), tipo, tam, extra ? extra : "");
    c->fechar = 1;
}
static void resp(Con *c, int status, const char *tipo, const char *corpo, size_t n) {
    resp_cab(c, status, tipo, (long long)n, NULL);
    b_put(&c->out, corpo, n);
}
static void resp_json(Con *c, int status, const char *json) { resp(c, status, "application/json; charset=utf-8", json, strlen(json)); }
static void resp_erro(Con *c, int status, const char *msg) {
    B b = { 0 };
    b_puts(&b, "{\"erro\":"); b_json(&b, msg); b_puts(&b, "}");
    resp_json(c, status, b.d);
    b_livra(&b);
}
static void resp_errno(Con *c) {
    int e = errno;
    resp_erro(c, e == ENOENT ? 404 : e == EACCES || e == EPERM ? 403 : e == EEXIST || e == ENOTEMPTY ? 409 : 400, strerror(e));
}

static const char *mime(const char *cam) {
    const char *e = strrchr(cam, '.');
    if (!e) return "application/octet-stream";
    static const char *const t[][2] = {
        { ".html", "text/html; charset=utf-8" }, { ".htm", "text/html; charset=utf-8" }, { ".css", "text/css; charset=utf-8" },
        { ".js", "text/javascript; charset=utf-8" }, { ".mjs", "text/javascript; charset=utf-8" }, { ".json", "application/json; charset=utf-8" },
        { ".wasm", "application/wasm" }, { ".svg", "image/svg+xml" }, { ".png", "image/png" }, { ".jpg", "image/jpeg" }, { ".jpeg", "image/jpeg" },
        { ".gif", "image/gif" }, { ".webp", "image/webp" }, { ".ico", "image/x-icon" }, { ".bmp", "image/bmp" }, { ".avif", "image/avif" },
        { ".txt", "text/plain; charset=utf-8" }, { ".md", "text/plain; charset=utf-8" }, { ".xml", "application/xml" }, { ".pdf", "application/pdf" },
        { ".mp3", "audio/mpeg" }, { ".ogg", "audio/ogg" }, { ".oga", "audio/ogg" }, { ".opus", "audio/ogg" }, { ".wav", "audio/wav" }, { ".flac", "audio/flac" },
        { ".m4a", "audio/mp4" }, { ".mp4", "video/mp4" }, { ".m4v", "video/mp4" }, { ".webm", "video/webm" }, { ".mkv", "video/x-matroska" }, { ".mov", "video/quicktime" },
        { ".woff", "font/woff" }, { ".woff2", "font/woff2" }, { ".ttf", "font/ttf" }, { ".otf", "font/otf" }, { ".zip", "application/zip" },
        { ".vtt", "text/vtt; charset=utf-8" }, { ".srt", "text/plain; charset=utf-8" }, { ".map", "application/json" }, { NULL, NULL }
    };
    for (int i = 0; t[i][0]; i++) if (!strcasecmp(e, t[i][0])) return t[i][1];
    return "application/octet-stream";
}

/* envia um arquivo do disco, aos poucos, com suporte a Range (para vídeo e áudio) */
static void enviar_arquivo(Con *c, const Req *r, const char *cam, const char *tipo, const char *extra) {
    int fd = open(cam, O_RDONLY | O_CLOEXEC);
    struct stat st;
    if (fd < 0) { resp_errno(c); return; }
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { close(fd); resp_erro(c, 404, "não é um arquivo"); return; }
    long long ini = 0, fim = st.st_size - 1;
    char rg[128];
    int parcial = 0;
    if (cabecalho(r, "Range", rg, sizeof rg) && !strncmp(rg, "bytes=", 6) && st.st_size > 0) {
        const char *p = rg + 6;
        if (*p == '-') { long long n = atoll(p + 1); ini = n < st.st_size ? st.st_size - n : 0; }
        else { ini = atoll(p); const char *t = strchr(p, '-'); if (t && isdigit((unsigned char)t[1])) fim = atoll(t + 1); }
        if (fim >= st.st_size) fim = st.st_size - 1;
        if (ini > fim || ini < 0) {
            close(fd);
            char ex[128];
            snprintf(ex, sizeof ex, "Content-Range: bytes */%lld\r\n", (long long)st.st_size);
            resp_cab(c, 416, "text/plain", 0, ex);
            return;
        }
        parcial = 1;
    }
    char ex[512];
    snprintf(ex, sizeof ex, "Accept-Ranges: bytes\r\n%s", extra ? extra : "");
    if (parcial) snprintf(ex + strlen(ex), sizeof ex - strlen(ex), "Content-Range: bytes %lld-%lld/%lld\r\n", ini, fim, (long long)st.st_size);
    long long tam = st.st_size ? fim - ini + 1 : 0;
    resp_cab(c, parcial ? 206 : 200, tipo ? tipo : mime(cam), tam, ex);
    if (!strcmp(r->metodo, "HEAD") || tam == 0) { close(fd); return; }
    lseek(fd, (off_t)ini, SEEK_SET);
    c->arq = fd;
    c->arq_resta = tam;
}

/* junta base + relativo sem deixar sair da base (sem "..") */
static int caminho_dentro(const char *base, const char *rel, char *dst, size_t cap) {
    if (strstr(rel, "..") || strchr(rel, '\\')) return 0;
    while (*rel == '/') rel++;
    return snprintf(dst, cap, "%s/%s", base, rel) < (int)cap;
}

/* ------------------------------------------------------------------ arquivos do aparelho */

static int remover_rec(const char *cam, int nivel) {
    struct stat st;
    if (lstat(cam, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) return unlink(cam);
    if (nivel > 64) { errno = ELOOP; return -1; }
    DIR *d = opendir(cam);
    if (!d) return -1;
    struct dirent *e;
    int r = 0;
    while ((e = readdir(d)) && r == 0) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char sub[4600];
        if (snprintf(sub, sizeof sub, "%s/%s", cam, e->d_name) >= (int)sizeof sub) { errno = ENAMETOOLONG; r = -1; break; }
        r = remover_rec(sub, nivel + 1);
    }
    closedir(d);
    return r == 0 ? rmdir(cam) : r;
}
/* roda um programa e espera (para cp e mv entre discos). Devolve o código de saída. */
static int rodar_espera(char *const argv[]) {
    pid_t p = fork();
    if (p < 0) return -1;
    if (p == 0) { int n = open("/dev/null", O_RDWR); if (n >= 0) { dup2(n, 0); dup2(n, 1); dup2(n, 2); } execvp(argv[0], argv); _exit(127); }
    int st = 0;
    while (waitpid(p, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
static int caminho_protegido(const char *cam) {
    const char *casa = getenv("HOME");
    char limpo[4200];
    snprintf(limpo, sizeof limpo, "%s", cam);
    size_t n = strlen(limpo);
    while (n > 1 && limpo[n - 1] == '/') limpo[--n] = 0;
    return !strcmp(limpo, "/") || (casa && !strcmp(limpo, casa)) || !strcmp(limpo, "/home") || !strcmp(limpo, "/usr") || !strcmp(limpo, "/etc") ||
           (getenv("PREFIX") && !strcmp(limpo, getenv("PREFIX")));
}

static void api_fs(Con *c, const Req *r, const char *op) {
    char cam[4200], para[4200];
    int escreve = strcmp(op, "listar") && strcmp(op, "ler") && strcmp(op, "info");
    if (escreve && strcmp(r->metodo, "POST")) { resp_erro(c, 405, "use POST"); return; }
    if (!strcmp(op, "mover") || !strcmp(op, "copiar")) {
        if (!parametro(r, "de", cam, sizeof cam) || !parametro(r, "para", para, sizeof para) || cam[0] != '/' || para[0] != '/') { resp_erro(c, 400, "informe de= e para= com caminhos absolutos"); return; }
    } else if (!parametro(r, "cam", cam, sizeof cam) || cam[0] != '/') { resp_erro(c, 400, "informe cam= com um caminho absoluto"); return; }

    if (!strcmp(op, "listar")) {
        DIR *d = opendir(cam);
        if (!d) { resp_errno(c); return; }
        B b = { 0 };
        b_puts(&b, "[");
        struct dirent *e;
        int n = 0;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char sub[4700];
            snprintf(sub, sizeof sub, "%s/%s", cam, e->d_name);
            struct stat ls, st;
            if (lstat(sub, &ls) != 0) continue;
            int link = S_ISLNK(ls.st_mode);
            if (!link || stat(sub, &st) != 0) st = ls;
            if (n++) b_puts(&b, ",");
            b_puts(&b, "{\"nome\":"); b_json(&b, e->d_name);
            b_printf(&b, ",\"tipo\":\"%s\",\"link\":%s,\"tam\":%lld,\"mtime\":%lld,\"modo\":%u}", S_ISDIR(st.st_mode) ? "d" : "f", link ? "true" : "false",
                     (long long)st.st_size, (long long)st.st_mtime, (unsigned)(st.st_mode & 07777));
        }
        closedir(d);
        b_puts(&b, "]");
        resp_json(c, 200, b.d);
        b_livra(&b);
    } else if (!strcmp(op, "info")) {
        struct stat st;
        if (stat(cam, &st) != 0) { resp_errno(c); return; }
        char j[256];
        snprintf(j, sizeof j, "{\"tipo\":\"%s\",\"tam\":%lld,\"mtime\":%lld,\"modo\":%u,\"gravavel\":%s}", S_ISDIR(st.st_mode) ? "d" : "f",
                 (long long)st.st_size, (long long)st.st_mtime, (unsigned)(st.st_mode & 07777), access(cam, W_OK) == 0 ? "true" : "false");
        resp_json(c, 200, j);
    } else if (!strcmp(op, "ler")) {
        enviar_arquivo(c, r, cam, "application/octet-stream", NULL);
    } else if (!strcmp(op, "gravar")) {
        int fd = open(cam, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd < 0) { resp_errno(c); return; }
        size_t feito = 0;
        while (feito < r->ncorpo) {
            ssize_t w = write(fd, r->corpo + feito, r->ncorpo - feito);
            if (w < 0) { if (errno == EINTR) continue; int e = errno; close(fd); errno = e; resp_errno(c); return; }
            feito += (size_t)w;
        }
        close(fd);
        resp_json(c, 200, "{\"ok\":true}");
    } else if (!strcmp(op, "mkdir")) {
        if (mkdir(cam, 0755) != 0) { resp_errno(c); return; }
        resp_json(c, 200, "{\"ok\":true}");
    } else if (!strcmp(op, "remover")) {
        if (caminho_protegido(cam)) { resp_erro(c, 403, "esta pasta é protegida: o TNUX não a apaga"); return; }
        if (remover_rec(cam, 0) != 0) { resp_errno(c); return; }
        resp_json(c, 200, "{\"ok\":true}");
    } else if (!strcmp(op, "mover")) {
        if (caminho_protegido(cam)) { resp_erro(c, 403, "esta pasta é protegida: o TNUX não a move"); return; }
        struct stat st;
        if (lstat(para, &st) == 0) { errno = EEXIST; resp_errno(c); return; }
        if (rename(cam, para) != 0) {
            if (errno != EXDEV) { resp_errno(c); return; }
            char *av[] = { "mv", "--", cam, para, NULL };
            if (rodar_espera(av) != 0) { resp_erro(c, 400, "não foi possível mover"); return; }
        }
        resp_json(c, 200, "{\"ok\":true}");
    } else if (!strcmp(op, "copiar")) {
        struct stat st;
        if (lstat(para, &st) == 0) { errno = EEXIST; resp_errno(c); return; }
        char *av[] = { "cp", "-a", "--", cam, para, NULL };
        if (rodar_espera(av) != 0) { resp_erro(c, 400, "não foi possível copiar"); return; }
        resp_json(c, 200, "{\"ok\":true}");
    } else resp_erro(c, 404, "operação desconhecida");
}

/* ------------------------------------------------------------------ programas instalados */

static const char *dirs_desktop(int i, char *tmp, size_t n) {
    const char *casa = getenv("HOME"), *prefixo = getenv("PREFIX");
    switch (i) {
    case 0: snprintf(tmp, n, "%s/.local/share/applications", casa ? casa : ""); return tmp;
    case 1: return "/usr/local/share/applications";
    case 2: return "/usr/share/applications";
    case 3: if (!prefixo) return ""; snprintf(tmp, n, "%s/share/applications", prefixo); return tmp;
    case 4: return "/var/lib/flatpak/exports/share/applications";
    case 5: return "/var/lib/snapd/desktop/applications";
    case 6: snprintf(tmp, n, "%s/.local/share/flatpak/exports/share/applications", casa ? casa : ""); return tmp;
    }
    return NULL;
}

typedef struct { char nome[256], resumo[512], exec[1024], icone[256], cat[256]; int terminal, oculto, app; int pn, pr; } Desktop;

/* lê a seção [Desktop Entry] de um arquivo .desktop, preferindo os textos em português */
static int ler_desktop(const char *cam, Desktop *d) {
    FILE *f = fopen(cam, "r");
    if (!f) return 0;
    memset(d, 0, sizeof *d);
    char l[2048];
    int dentro = 0;
    while (fgets(l, sizeof l, f)) {
        size_t n = strlen(l);
        while (n && (l[n - 1] == '\n' || l[n - 1] == '\r')) l[--n] = 0;
        if (l[0] == '[') { dentro = !strcmp(l, "[Desktop Entry]"); continue; }
        if (!dentro || l[0] == '#') continue;
        char *eq = strchr(l, '=');
        if (!eq) continue;
        *eq = 0;
        const char *v = eq + 1;
        if (!strcmp(l, "Name") && d->pn < 1) { snprintf(d->nome, sizeof d->nome, "%s", v); d->pn = 1; }
        else if (!strcmp(l, "Name[pt]") && d->pn < 2) { snprintf(d->nome, sizeof d->nome, "%s", v); d->pn = 2; }
        else if (!strcmp(l, "Name[pt_BR]")) { snprintf(d->nome, sizeof d->nome, "%s", v); d->pn = 3; }
        else if (!strcmp(l, "Comment") && d->pr < 1) { snprintf(d->resumo, sizeof d->resumo, "%s", v); d->pr = 1; }
        else if (!strcmp(l, "Comment[pt]") && d->pr < 2) { snprintf(d->resumo, sizeof d->resumo, "%s", v); d->pr = 2; }
        else if (!strcmp(l, "Comment[pt_BR]")) { snprintf(d->resumo, sizeof d->resumo, "%s", v); d->pr = 3; }
        else if (!strcmp(l, "Exec")) snprintf(d->exec, sizeof d->exec, "%s", v);
        else if (!strcmp(l, "Icon")) snprintf(d->icone, sizeof d->icone, "%s", v);
        else if (!strcmp(l, "Categories")) snprintf(d->cat, sizeof d->cat, "%s", v);
        else if (!strcmp(l, "Terminal")) d->terminal = !strcmp(v, "true");
        else if (!strcmp(l, "NoDisplay") || !strcmp(l, "Hidden")) { if (!strcmp(v, "true")) d->oculto = 1; }
        else if (!strcmp(l, "Type")) d->app = !strcmp(v, "Application");
    }
    fclose(f);
    /* tira os códigos de campo do Exec (%U, %f...) */
    char *o = d->exec;
    for (char *s = d->exec; *s; s++) {
        if (*s == '%' && s[1]) { if (s[1] == '%') *o++ = '%'; s++; continue; }
        *o++ = *s;
    }
    *o = 0;
    return d->app && d->nome[0] && d->exec[0];
}

/* traduz as categorias do freedesktop para as do menu do TNUX */
static const char *categoria_menu(const char *cats) {
    static const char *const mapa[][2] = {
        { "Game", "Jogos" }, { "Development", "Programação" }, { "Graphics", "Gráficos" }, { "Network", "Internet" }, { "AudioVideo", "Som e vídeo" },
        { "Audio", "Som e vídeo" }, { "Video", "Som e vídeo" }, { "Office", "Escritório" }, { "Settings", "Preferências" }, { "System", "Sistema" },
        { "Education", "Educação" }, { "Science", "Educação" }, { "Utility", "Acessórios" }, { NULL, NULL }
    };
    for (int i = 0; mapa[i][0]; i++) {
        const char *p = strstr(cats, mapa[i][0]);
        size_t l = strlen(mapa[i][0]);
        if (p && (p == cats || p[-1] == ';') && (p[l] == ';' || p[l] == 0)) return mapa[i][1];
    }
    return "Outros";
}

static void api_apps(Con *c) {
    B b = { 0 };
    b_puts(&b, "{\"web\":[");
    /* apps web do TNUX: ~/.tnux/apps/ID/.tnux-app.json */
    char dir[4200];
    snprintf(dir, sizeof dir, "%s/apps", PASTA);
    DIR *d = opendir(dir);
    int n = 0;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            char man[4700];
            snprintf(man, sizeof man, "%s/%s/.tnux-app.json", dir, e->d_name);
            FILE *f = fopen(man, "r");
            if (!f) continue;
            char txt[4096];
            size_t l = fread(txt, 1, sizeof txt - 1, f);
            fclose(f);
            while (l && (txt[l - 1] == '\n' || txt[l - 1] == ' ')) l--;
            txt[l] = 0;
            if (l < 2 || txt[0] != '{' || txt[l - 1] != '}') continue;
            if (n++) b_puts(&b, ",");
            b_puts(&b, txt);
        }
        closedir(d);
    }
    b_puts(&b, "],\"nativos\":[");
    n = 0;
    char vistos[512][96];
    int nv = 0;
    for (int i = 0;; i++) {
        char tmp[4200];
        const char *dd = dirs_desktop(i, tmp, sizeof tmp);
        if (!dd) break;
        if (!*dd || !(d = opendir(dd))) continue;
        struct dirent *e;
        while ((e = readdir(d)) && n < 600) {
            size_t l = strlen(e->d_name);
            if (l < 9 || strcmp(e->d_name + l - 8, ".desktop")) continue;
            int rep = 0;
            for (int k = 0; k < nv; k++) if (!strcmp(vistos[k], e->d_name)) rep = 1;
            if (rep) continue;
            if (nv < 512) snprintf(vistos[nv++], 96, "%s", e->d_name);
            char cam[4700];
            snprintf(cam, sizeof cam, "%s/%s", dd, e->d_name);
            Desktop dk;
            if (!ler_desktop(cam, &dk) || dk.oculto) continue;
            if (n++) b_puts(&b, ",");
            char id[300];
            snprintf(id, sizeof id, "%.*s", (int)(l - 8), e->d_name);
            b_puts(&b, "{\"id\":"); b_json(&b, id);
            b_puts(&b, ",\"nome\":"); b_json(&b, dk.nome);
            b_puts(&b, ",\"resumo\":"); b_json(&b, dk.resumo);
            b_puts(&b, ",\"exec\":"); b_json(&b, dk.exec);
            b_puts(&b, ",\"icone_sis\":"); b_json(&b, dk.icone);
            b_puts(&b, ",\"categoria\":"); b_json(&b, categoria_menu(dk.cat));
            b_printf(&b, ",\"terminal\":%s,\"tipo\":\"nativo\"}", dk.terminal ? "true" : "false");
        }
        closedir(d);
    }
    b_puts(&b, "]}");
    resp_json(c, 200, b.d);
    b_livra(&b);
}

/* acha o arquivo de um ícone do sistema pelo nome (como aparece em Icon= nos .desktop) */
static void api_icone(Con *c, const Req *r) {
    char nome[256];
    if (!parametro(r, "nome", nome, sizeof nome) || !*nome || strstr(nome, "..")) { resp_erro(c, 400, "informe nome="); return; }
    if (nome[0] == '/') {
        const char *e = strrchr(nome, '.');
        if (e && (!strcasecmp(e, ".png") || !strcasecmp(e, ".svg") || !strcasecmp(e, ".xpm") || !strcasecmp(e, ".jpg")) && access(nome, R_OK) == 0) { enviar_arquivo(c, r, nome, NULL, "Cache-Control: max-age=86400\r\n"); return; }
        resp_erro(c, 404, "ícone não encontrado");
        return;
    }
    if (strchr(nome, '/')) { resp_erro(c, 400, "nome inválido"); return; }
    const char *prefixo = getenv("PREFIX"), *casa = getenv("HOME");
    char bases[6][512];
    int nb = 0;
    snprintf(bases[nb++], 512, "%s/.local/share/icons", casa ? casa : "");
    snprintf(bases[nb++], 512, "/usr/share/icons");
    if (prefixo) snprintf(bases[nb++], 512, "%s/share/icons", prefixo);
    snprintf(bases[nb++], 512, "/var/lib/flatpak/exports/share/icons");
    static const char *const temas[] = { "hicolor", "Mint-Y", "Adwaita", "breeze", "Papirus", "gnome", NULL };
    static const char *const tams[] = { "scalable/apps", "64x64/apps", "48x48/apps", "128x128/apps", "256x256/apps", "apps/64", "apps/48", "apps/scalable", "96x96/apps", "32x32/apps", NULL };
    static const char *const exts[] = { "svg", "png", NULL };
    char cam[1200];
    for (int b = 0; b < nb; b++)
        for (int t = 0; temas[t]; t++)
            for (int s = 0; tams[s]; s++)
                for (int x = 0; exts[x]; x++) {
                    snprintf(cam, sizeof cam, "%s/%s/%s/%s.%s", bases[b], temas[t], tams[s], nome, exts[x]);
                    if (access(cam, R_OK) == 0) { enviar_arquivo(c, r, cam, NULL, "Cache-Control: max-age=86400\r\n"); return; }
                }
    const char *pix[] = { "/usr/share/pixmaps", prefixo, NULL };
    for (int p = 0; p < 2; p++) {
        if (!pix[p]) continue;
        for (int x = 0; x < 2; x++) {
            snprintf(cam, sizeof cam, p == 0 ? "%s/%s.%s" : "%s/share/pixmaps/%s.%s", pix[p], nome, x ? "png" : "svg");
            if (access(cam, R_OK) == 0) { enviar_arquivo(c, r, cam, NULL, "Cache-Control: max-age=86400\r\n"); return; }
        }
    }
    resp_erro(c, 404, "ícone não encontrado");
}

/* ------------------------------------------------------------------ tela (VNC) para programas gráficos */

static const char *achar_xvnc(void) {
    if (tem_comando("Xvnc")) return "Xvnc";
    if (tem_comando("Xtigervnc")) return "Xtigervnc";
    return NULL;
}
static int tela_ativa(void) { return tela.pid > 0 && kill(tela.pid, 0) == 0; }

static void tela_parar(void) {
    if (tela.wm > 0) { kill(tela.wm, SIGTERM); tela.wm = 0; }
    if (tela.pid > 0) { kill(tela.pid, SIGTERM); tela.pid = 0; }
    if (tela.sock[0]) unlink(tela.sock);
    tela.sock[0] = 0;
}

static pid_t soltar(char *const argv[], const char *display, const char *log) {
    pid_t p = fork();
    if (p != 0) return p;
    setsid();
    int n = open("/dev/null", O_RDONLY);
    int l = log ? open(log, O_WRONLY | O_CREAT | O_APPEND, 0600) : open("/dev/null", O_WRONLY);
    if (n >= 0) dup2(n, 0);
    if (l >= 0) { dup2(l, 1); dup2(l, 2); }
    for (int fd = 3; fd < 1024; fd++) close(fd);
    if (display) { setenv("DISPLAY", display, 1); unsetenv("WAYLAND_DISPLAY"); }
    signal(SIGPIPE, SIG_DFL); signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL);
    execvp(argv[0], argv);
    _exit(127);
}

static int tela_conectar(void) {
    if (tela.sock[0]) {
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        struct sockaddr_un a;
        memset(&a, 0, sizeof a);
        a.sun_family = AF_UNIX;
        snprintf(a.sun_path, sizeof a.sun_path, "%s", tela.sock);
        if (connect(fd, (struct sockaddr *)&a, sizeof a) == 0) return fd;
        close(fd);
        return -1;
    }
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons((uint16_t)tela.porta_tcp); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) == 0) return fd;
    close(fd);
    return -1;
}

/* liga o Xvnc. Devolve NULL se deu certo, ou a explicação do erro. */
static const char *tela_iniciar(int larg, int alt) {
    if (tela_ativa()) return NULL;
    const char *xvnc = achar_xvnc();
    if (!xvnc) return "o servidor de tela (Xvnc, do pacote tigervnc) não está instalado. No terminal: tnux instalar tela";
    if (larg < 320) larg = 1280;
    if (alt < 240) alt = 720;
    if (larg > 3840) larg = 3840;
    if (alt > 2160) alt = 2160;
    const char *tmp = getenv("TMPDIR");
    int disp = -1;
    for (int n = 70; n < 90; n++) {
        char lock[512], sk[512];
        snprintf(lock, sizeof lock, "%s/.X%d-lock", tmp && *tmp ? tmp : "/tmp", n);
        snprintf(sk, sizeof sk, "%s/.X11-unix/X%d", tmp && *tmp ? tmp : "/tmp", n);
        if (access(lock, F_OK) != 0 && access(sk, F_OK) != 0) { disp = n; break; }
    }
    if (disp < 0) return "não há número de tela livre";
    char d[16], geo[32], log[4300];
    snprintf(d, sizeof d, ":%d", disp);
    snprintf(geo, sizeof geo, "%dx%d", larg, alt);
    snprintf(log, sizeof log, "%s/tela.log", PASTA);
    /* primeiro com socket local (ninguém mais no aparelho alcança); se o Xvnc for antigo, cai para TCP em 127.0.0.1 */
    /* (o caminho de um socket local tem limite de tamanho: se não couber, vai direto para o TCP) */
    for (int tentativa = strlen(PASTA) > 90 ? 1 : 0; tentativa < 2; tentativa++) {
        char porta_txt[16];
        tela.porta_tcp = 5900 + disp;
        snprintf(porta_txt, sizeof porta_txt, "%d", tela.porta_tcp);
        if (tentativa == 0) {
            snprintf(tela.sock, sizeof tela.sock, "%s/tela.sock", PASTA);
            unlink(tela.sock);
            char *av[] = { (char *)xvnc, d, "-geometry", geo, "-depth", "24", "-SecurityTypes", "None", "-rfbunixpath", tela.sock, "-rfbunixmode", "0600",
                           "-AlwaysShared", "-desktop", "TNUX", "-nolisten", "tcp", NULL };
            tela.pid = soltar(av, NULL, log);
        } else {
            tela.sock[0] = 0;
            char *av[] = { (char *)xvnc, d, "-geometry", geo, "-depth", "24", "-SecurityTypes", "None", "-localhost", "-rfbport", porta_txt,
                           "-AlwaysShared", "-desktop", "TNUX", "-nolisten", "tcp", NULL };
            tela.pid = soltar(av, NULL, log);
        }
        int ok = 0;
        for (int i = 0; i < 60 && !ok; i++) {          /* até 6 segundos */
            usleep(100000);
            int st;
            if (waitpid(tela.pid, &st, WNOHANG) == tela.pid) { tela.pid = 0; break; }
            int fd = tela_conectar();
            if (fd >= 0) { close(fd); ok = 1; }
        }
        if (ok) break;
        if (tela.pid > 0) { kill(tela.pid, SIGTERM); waitpid(tela.pid, NULL, 0); tela.pid = 0; }
    }
    if (tela.pid <= 0) { tela.sock[0] = 0; return "o servidor de tela não ligou (veja ~/.tnux/tela.log)"; }
    tela.display = disp; tela.larg = larg; tela.alt = alt;
    /* um gerenciador de janelas, se houver: dá barra de título e deixa mover/redimensionar */
    static const char *const wms[] = { "openbox", "xfwm4", "fluxbox", "icewm", "marco", "metacity", "twm", NULL };
    for (int i = 0; wms[i]; i++) {
        if (!tem_comando(wms[i])) continue;
        char *av[] = { (char *)wms[i], NULL };
        tela.wm = soltar(av, d, log);
        break;
    }
    return NULL;
}

static void api_tela(Con *c, const Req *r, const char *op) {
    if (!strcmp(op, "iniciar")) {
        if (strcmp(r->metodo, "POST")) { resp_erro(c, 405, "use POST"); return; }
        char l[16], a[16];
        parametro(r, "l", l, sizeof l); parametro(r, "a", a, sizeof a);
        const char *erro = tela_iniciar(atoi(l), atoi(a));
        if (erro) { resp_erro(c, 409, erro); return; }
    } else if (!strcmp(op, "parar")) {
        if (strcmp(r->metodo, "POST")) { resp_erro(c, 405, "use POST"); return; }
        tela_parar();
    }
    char j[256];
    snprintf(j, sizeof j, "{\"ativa\":%s,\"disponivel\":%s,\"display\":%d,\"largura\":%d,\"altura\":%d,\"janelas\":%s}", tela_ativa() ? "true" : "false",
             achar_xvnc() ? "true" : "false", tela.display, tela.larg, tela.alt, tela.wm > 0 ? "true" : "false");
    resp_json(c, 200, j);
}

/* abre um programa gráfico do aparelho, escolhido pelo id do .desktop */
static void api_lancar(Con *c, const Req *r) {
    if (strcmp(r->metodo, "POST")) { resp_erro(c, 405, "use POST"); return; }
    char id[300], onde[16];
    if (!parametro(r, "id", id, sizeof id) || !*id || strchr(id, '/') || strstr(id, "..")) { resp_erro(c, 400, "informe id="); return; }
    parametro(r, "onde", onde, sizeof onde);
    Desktop dk;
    int achou = 0;
    for (int i = 0; !achou; i++) {
        char tmp[4200], cam[4700];
        const char *dd = dirs_desktop(i, tmp, sizeof tmp);
        if (!dd) break;
        if (!*dd) continue;
        snprintf(cam, sizeof cam, "%s/%s.desktop", dd, id);
        if (ler_desktop(cam, &dk)) achou = 1;
    }
    if (!achou) { resp_erro(c, 404, "programa não encontrado"); return; }
    B b = { 0 };
    if (dk.terminal) {          /* programa de terminal: a área de trabalho abre um Terminal com ele */
        b_puts(&b, "{\"terminal\":true,\"exec\":"); b_json(&b, dk.exec); b_puts(&b, "}");
        resp_json(c, 200, b.d);
        b_livra(&b);
        return;
    }
    char disp[16] = "";
    const char *usar = NULL, *lugar = "aparelho";
    int tem_tela_propria = (getenv("DISPLAY") && *getenv("DISPLAY")) || (getenv("WAYLAND_DISPLAY") && *getenv("WAYLAND_DISPLAY"));
    if (!strcmp(onde, "tela") || (!tem_tela_propria && strcmp(onde, "aparelho"))) {
        if (!tela_ativa()) {
            resp_erro(c, 409, tem_tela_propria ? "a tela do TNUX não está ligada" :
                      "este aparelho não tem uma tela gráfica ligada. Abra \"Tela do aparelho\" no menu do TNUX para ligar uma (precisa do pacote tigervnc: tnux instalar tela).");
            b_livra(&b);
            return;
        }
        snprintf(disp, sizeof disp, ":%d", tela.display);
        usar = disp; lugar = "tela";
    }
    char log[4300];
    snprintf(log, sizeof log, "%s/programas.log", PASTA);
    /* como root (comum em contêineres), o Chrome e parecidos só abrem sem a caixa de areia deles */
    char *linha = dk.exec;
    B lb = { 0 };
    if (geteuid() == 0 && (strstr(dk.exec, "chrom") || strstr(dk.exec, "brave") || strstr(dk.exec, "code")) && !strstr(dk.exec, "--no-sandbox")) {
        b_printf(&lb, "%s --no-sandbox", dk.exec);
        linha = lb.d;
    }
    char *av[] = { "sh", "-c", linha, NULL };
    pid_t p = soltar(av, usar, log);
    b_livra(&lb);
    if (p < 0) { resp_erro(c, 400, "não foi possível iniciar o programa"); b_livra(&b); return; }
    b_printf(&b, "{\"ok\":true,\"onde\":\"%s\",\"nome\":", lugar); b_json(&b, dk.nome); b_puts(&b, "}");
    resp_json(c, 200, b.d);
    b_livra(&b);
}

/* ------------------------------------------------------------------ informações */

static void api_info(Con *c) {
    char so[256], host[128] = "aparelho";
    nome_sistema(so, sizeof so);
    gethostname(host, sizeof host - 1);
    struct utsname un;
    uname(&un);
    struct passwd *pw = getpwuid(getuid());
    const char *casa = getenv("HOME"), *ger = gerenciador();
    long cpus = sysconf(_SC_NPROCESSORS_ONLN), pags = sysconf(_SC_PHYS_PAGES), tpag = sysconf(_SC_PAGE_SIZE);
    char tnux[4200];
    snprintf(tnux, sizeof tnux, "%s/build/tnux", RAIZ);
    B b = { 0 };
    b_puts(&b, "{\"tnux\":\"" VERSAO "\",\"modo\":\"ponte\",\"usuario\":"); b_json(&b, pw ? pw->pw_name : "usuario");
    b_puts(&b, ",\"host\":"); b_json(&b, host);
    b_puts(&b, ",\"casa\":"); b_json(&b, casa ? casa : "/");
    b_puts(&b, ",\"sistema\":"); b_json(&b, so);
    b_puts(&b, ",\"kernel\":"); b_json(&b, un.release);
    b_puts(&b, ",\"arquitetura\":"); b_json(&b, un.machine);
    b_puts(&b, ",\"gerenciador\":"); b_json(&b, ger ? ger : "");
    b_printf(&b, ",\"cpus\":%ld,\"mem_mb\":%ld,\"termux\":%s,\"root\":%s,\"tem_tnux\":%s", cpus, pags > 0 ? (long)((double)pags * (double)tpag / 1048576.0) : 0,
             eh_termux() ? "true" : "false", geteuid() == 0 ? "true" : "false", access(tnux, X_OK) == 0 ? "true" : "false");
    b_printf(&b, ",\"tela_disponivel\":%s,\"tela_ativa\":%s,\"tela_propria\":%s}", achar_xvnc() ? "true" : "false", tela_ativa() ? "true" : "false",
             ((getenv("DISPLAY") && *getenv("DISPLAY")) || (getenv("WAYLAND_DISPLAY") && *getenv("WAYLAND_DISPLAY"))) ? "true" : "false");
    resp_json(c, 200, b.d);
    b_livra(&b);
}
static void api_tem(Con *c, const Req *r) {
    char lista[4096];
    parametro(r, "cmd", lista, sizeof lista);
    B b = { 0 };
    b_puts(&b, "{");
    int n = 0;
    for (char *p = lista; *p;) {
        size_t l = strcspn(p, ",");
        char nome[256];
        if (l && l < sizeof nome) {
            memcpy(nome, p, l); nome[l] = 0;
            if (n++) b_puts(&b, ",");
            b_json(&b, nome);
            b_printf(&b, ":%s", !strchr(nome, '/') && tem_comando(nome) ? "true" : "false");
        }
        p += l;
        if (*p == ',') p++;
    }
    b_puts(&b, "}");
    resp_json(c, 200, b.d);
    b_livra(&b);
}

/* ------------------------------------------------------------------ WebSocket */

static void ws_enviar(Con *c, int op, const void *d, size_t n) {
    unsigned char h[10];
    size_t hl = 2;
    h[0] = (unsigned char)(0x80 | op);
    if (n < 126) h[1] = (unsigned char)n;
    else if (n < 65536) { h[1] = 126; h[2] = (unsigned char)(n >> 8); h[3] = (unsigned char)n; hl = 4; }
    else { h[1] = 127; for (int i = 0; i < 8; i++) h[2 + i] = (unsigned char)((uint64_t)n >> (8 * (7 - i))); hl = 10; }
    b_put(&c->out, h, hl);
    b_put(&c->out, d, n);
}
static void ws_fim(Con *c, int codigo) {
    unsigned char p[2] = { (unsigned char)(codigo >> 8), (unsigned char)codigo };
    ws_enviar(c, 8, p, 2);
    c->fechar = 1;
}

/* abre um terminal de verdade (PTY) rodando o shell do aparelho */
static int pty_abrir(Con *c, int cols, int rows, const char *cmd) {
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0) { if (m >= 0) close(m); return -1; }
    char *nome = ptsname(m);
    if (!nome) { close(m); return -1; }
    char escravo[256];
    snprintf(escravo, sizeof escravo, "%s", nome);
    struct winsize ws = { (unsigned short)rows, (unsigned short)cols, 0, 0 };
    ioctl(m, TIOCSWINSZ, &ws);
    pid_t p = fork();
    if (p < 0) { close(m); return -1; }
    if (p == 0) {
        setsid();
        int s = open(escravo, O_RDWR);
        if (s < 0) _exit(126);
        ioctl(s, TIOCSCTTY, 0);
        dup2(s, 0); dup2(s, 1); dup2(s, 2);
        for (int fd = 3; fd < 1024; fd++) close(fd);
        signal(SIGPIPE, SIG_DFL); signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL); signal(SIGCHLD, SIG_DFL); signal(SIGHUP, SIG_DFL);
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("TNUX_PONTE", "1", 1);
        if (tela_ativa()) { char d[16]; snprintf(d, sizeof d, ":%d", tela.display); if (!getenv("DISPLAY")) setenv("DISPLAY", d, 1); }
        const char *casa = getenv("HOME");
        if (casa && chdir(casa) != 0) {}
        char tnux[4200];
        snprintf(tnux, sizeof tnux, "%s/build/tnux", RAIZ);
        if (access(tnux, X_OK) == 0) {
            if (cmd && *cmd) execl(tnux, "tnux", "-c", cmd, "--pausa", (char *)NULL);
            else execl(tnux, "tnux", (char *)NULL);
        }
        const char *sh = getenv("SHELL");
        if (!sh || !*sh) sh = "/bin/sh";
        if (cmd && *cmd) execl(sh, sh, "-c", cmd, (char *)NULL);
        else execl(sh, sh, "-l", (char *)NULL);
        _exit(127);
    }
    nao_bloquear(m);
    fcntl(m, F_SETFD, FD_CLOEXEC);
    c->alvo = m;
    c->pid = p;
    return 0;
}

static void ws_mensagem(Con *c, int op, const char *d, size_t n) {
    if (c->tipo == C_WS_PTY) {
        if (op == 1) {                               /* texto: comandos de controle em JSON */
            const char *pc = strstr(d, "\"c\":"), *pl = strstr(d, "\"l\":");
            if (strstr(d, "\"t\":\"r\"") && pc && pl && c->alvo >= 0) {
                struct winsize ws = { (unsigned short)atoi(pl + 4), (unsigned short)atoi(pc + 4), 0, 0 };
                if (ws.ws_row > 0 && ws.ws_col > 0) ioctl(c->alvo, TIOCSWINSZ, &ws);
            }
        } else if (c->alvo >= 0) b_put(&c->alvo_out, d, n);
    } else if (c->tipo == C_WS_TELA) {
        if (op == 2 && c->alvo >= 0) b_put(&c->alvo_out, d, n);
    }
}

/* consome os quadros WebSocket que já chegaram inteiros */
static void ws_processar(Con *c) {
    for (;;) {
        if (c->in.n < 2) return;
        const unsigned char *p = (const unsigned char *)c->in.d;
        int fin = p[0] & 0x80, op = p[0] & 0x0f, mascara = p[1] & 0x80;
        uint64_t len = p[1] & 0x7f;
        size_t hl = 2;
        if (len == 126) { if (c->in.n < 4) return; len = (uint64_t)p[2] << 8 | p[3]; hl = 4; }
        else if (len == 127) { if (c->in.n < 10) return; len = 0; for (int i = 0; i < 8; i++) len = len << 8 | p[2 + i]; hl = 10; }
        if (!mascara || len > 32u * 1048576u) { ws_fim(c, 1002); c->in.n = 0; return; }
        if (c->in.n < hl + 4 + len) return;
        unsigned char *dados = (unsigned char *)c->in.d + hl + 4;
        const unsigned char *mk = p + hl;
        for (uint64_t i = 0; i < len; i++) dados[i] ^= mk[i & 3];
        if (op == 8) { ws_fim(c, 1000); c->in.n = 0; return; }
        else if (op == 9) ws_enviar(c, 10, dados, (size_t)len);
        else if (op == 10) {}
        else {
            if (op != 0) { c->msg.n = 0; c->msg_op = op; }
            b_put(&c->msg, dados, (size_t)len);
            if (fin) { b_res(&c->msg, 1); c->msg.d[c->msg.n] = 0; ws_mensagem(c, c->msg_op, c->msg.d, c->msg.n); c->msg.n = 0; }
        }
        b_tira(&c->in, hl + 4 + (size_t)len);
    }
}

static void ws_aceitar(Con *c, const Req *r) {
    char chave[256], up[64];
    cabecalho(r, "Upgrade", up, sizeof up);
    if (strcasecmp(up, "websocket") || !cabecalho(r, "Sec-WebSocket-Key", chave, sizeof chave)) { resp_erro(c, 400, "esperava um WebSocket"); return; }
    if (!origem_ok(r)) { resp_erro(c, 403, "origem não permitida"); return; }
    if (!autenticado(r)) { resp_erro(c, 401, "chave inválida"); return; }
    int tipo = !strcmp(r->caminho, "/ws/pty") ? C_WS_PTY : C_WS_TELA;
    if (tipo == C_WS_PTY) {
        char cs[16], ls[16], cmd[2048];
        parametro(r, "cols", cs, sizeof cs); parametro(r, "rows", ls, sizeof ls); parametro(r, "cmd", cmd, sizeof cmd);
        int cols = atoi(cs), rows = atoi(ls);
        if (pty_abrir(c, cols > 0 ? cols : 80, rows > 0 ? rows : 24, cmd) != 0) { resp_erro(c, 400, "não foi possível abrir um terminal no aparelho"); return; }
    } else {
        if (!tela_ativa()) { resp_erro(c, 409, "a tela não está ligada"); return; }
        int fd = tela_conectar();
        if (fd < 0) { resp_erro(c, 409, "não foi possível conectar à tela"); return; }
        nao_bloquear(fd);
        c->alvo = fd;
    }
    char junto[400];
    unsigned char dg[20];
    char aceite[64];
    snprintf(junto, sizeof junto, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", chave);
    sha1((const unsigned char *)junto, strlen(junto), dg);
    base64(dg, 20, aceite);
    b_printf(&c->out, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", aceite);
    c->tipo = tipo;
}

/* ------------------------------------------------------------------ roteador */

static void atender(Con *c, const Req *r) {
    registrar("%s %s", r->metodo, r->caminho);
    if (!host_ok(r)) { resp_erro(c, 403, "endereço não permitido (use 127.0.0.1 ou --permitir-host)"); return; }
    const char *cam = r->caminho;
    if (!strncmp(cam, "/ws/", 4)) {
        if (!strcmp(cam, "/ws/pty") || !strcmp(cam, "/ws/tela")) ws_aceitar(c, r);
        else resp_erro(c, 404, "não existe");
        return;
    }
    int aut = autenticado(r);
    /* quem chega com a chave ganha um cookie: é o que deixa imagens e páginas (/arq, /apps) carregarem */
    char cookie[256] = "";
    if (aut == 1 || aut == 2) snprintf(cookie, sizeof cookie, "Set-Cookie: tnux_token=%s; Path=/; HttpOnly; SameSite=Strict\r\n", TOKEN);

    if (!strncmp(cam, "/api/", 5)) {
        if (!aut) { resp_erro(c, 401, "chave (token) ausente ou inválida"); return; }
        if (!origem_ok(r)) { resp_erro(c, 403, "origem não permitida"); return; }
        size_t antes = c->out.n;
        if (!strcmp(cam, "/api/info")) api_info(c);
        else if (!strncmp(cam, "/api/fs/", 8)) api_fs(c, r, cam + 8);
        else if (!strcmp(cam, "/api/tem")) api_tem(c, r);
        else if (!strcmp(cam, "/api/apps")) api_apps(c);
        else if (!strcmp(cam, "/api/icone")) api_icone(c, r);
        else if (!strcmp(cam, "/api/lancar")) api_lancar(c, r);
        else if (!strncmp(cam, "/api/tela/", 10)) api_tela(c, r, cam + 10);
        else resp_erro(c, 404, "não existe");
        /* encaixa o cookie logo depois da linha de status */
        if (cookie[0] && c->out.n > antes) {
            char *nl = strstr(c->out.d + antes, "\r\n");
            if (nl) {
                size_t pos = (size_t)(nl - c->out.d) + 2, lc = strlen(cookie);
                b_res(&c->out, lc);
                memmove(c->out.d + pos + lc, c->out.d + pos, c->out.n - pos);
                memcpy(c->out.d + pos, cookie, lc);
                c->out.n += lc;
                c->out.d[c->out.n] = 0;
            }
        }
        return;
    }
    if (strcmp(r->metodo, "GET") && strcmp(r->metodo, "HEAD")) { resp_erro(c, 405, "método não permitido"); return; }
    char arq[8400];
    if (!strncmp(cam, "/arq/", 5)) {                 /* um arquivo qualquer do aparelho (para ver imagens e páginas) */
        if (!aut) { resp_erro(c, 401, "chave (token) ausente ou inválida"); return; }
        if (strstr(cam, "/../") || strlen(cam) < 6) { resp_erro(c, 400, "caminho inválido"); return; }
        enviar_arquivo(c, r, cam + 4, NULL, cookie);
        return;
    }
    if (!strncmp(cam, "/apps/", 6)) {                /* apps web instalados em ~/.tnux/apps */
        if (!aut) { resp_erro(c, 401, "chave (token) ausente ou inválida"); return; }
        char base[4300];
        snprintf(base, sizeof base, "%s/apps", PASTA);
        if (!caminho_dentro(base, cam + 6, arq, sizeof arq)) { resp_erro(c, 400, "caminho inválido"); return; }
        struct stat st;
        if (stat(arq, &st) == 0 && S_ISDIR(st.st_mode)) strncat(arq, "/index.html", sizeof arq - strlen(arq) - 1);
        enviar_arquivo(c, r, arq, NULL, cookie);
        return;
    }
    /* a página do TNUX (pasta web/): pública, é só o código da área de trabalho */
    char web[4300];
    snprintf(web, sizeof web, "%s/web", RAIZ);
    if (!caminho_dentro(web, !strcmp(cam, "/") ? "index.html" : cam, arq, sizeof arq)) { resp_erro(c, 400, "caminho inválido"); return; }
    struct stat st;
    if (stat(arq, &st) != 0 || !S_ISREG(st.st_mode)) { resp(c, 404, "text/plain; charset=utf-8", "não encontrado\n", 16); return; }
    enviar_arquivo(c, r, arq, NULL, "Cross-Origin-Resource-Policy: same-origin\r\n");
}

/* tenta montar um pedido com o que já chegou. 1 = atendeu, 0 = falta chegar mais. */
static int http_processar(Con *c) {
    char *fim_cab = c->in.d ? strstr(c->in.d, "\r\n\r\n") : NULL;
    if (!fim_cab) {
        if (c->in.n > 65536) { resp_erro(c, 400, "cabeçalhos grandes demais"); return 1; }
        return 0;
    }
    Req r;
    memset(&r, 0, sizeof r);
    char *linha_fim = strstr(c->in.d, "\r\n");
    char alvo[8192];
    if (sscanf(c->in.d, "%11s %8191s", r.metodo, alvo) != 2) { resp_erro(c, 400, "pedido inválido"); return 1; }
    r.cab = linha_fim + 2;
    r.ncab = (size_t)(fim_cab - r.cab) + 2;
    char cl[32];
    long long tam = cabecalho(&r, "Content-Length", cl, sizeof cl) ? atoll(cl) : 0;
    if (tam < 0 || tam > MAX_CORPO) { resp_erro(c, 413, "arquivo grande demais para enviar pela ponte"); return 1; }
    size_t ini_corpo = (size_t)(fim_cab - c->in.d) + 4;
    if (c->in.n < ini_corpo + (size_t)tam) return 0;
    r.corpo = c->in.d + ini_corpo;
    r.ncorpo = (size_t)tam;
    char *q = strchr(alvo, '?');
    if (q) { *q = 0; snprintf(r.consulta, sizeof r.consulta, "%s", q + 1); }
    url_decodificar(alvo, strlen(alvo), r.caminho, sizeof r.caminho, 0);
    if (r.caminho[0] != '/') { resp_erro(c, 400, "pedido inválido"); return 1; }
    atender(c, &r);
    if (c->tipo == C_HTTP) c->in.n = 0;
    else b_tira(&c->in, ini_corpo + (size_t)tam);       /* virou WebSocket: o resto já são quadros */
    return 1;
}

/* ------------------------------------------------------------------ laço principal */

static void ao_sinal(int s) { (void)s; parar = 1; }

static void token_gerar(void) {
    unsigned char r[24];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0 || read(fd, r, sizeof r) != (ssize_t)sizeof r) { fprintf(stderr, "tnuxd: não consegui gerar a chave (sem /dev/urandom)\n"); exit(1); }
    close(fd);
    for (size_t i = 0; i < sizeof r; i++) snprintf(TOKEN + i * 2, 3, "%02x", r[i]);
}

static void abrir_navegador(const char *url) {
    const char *prog = eh_termux() && tem_comando("termux-open-url") ? "termux-open-url" : tem_comando("xdg-open") ? "xdg-open" : NULL;
    if (!prog) return;
    char *av[] = { (char *)prog, (char *)url, NULL };
    soltar(av, NULL, NULL);
}

static void uso(void) {
    printf("tnuxd " VERSAO ": servidor da ponte do TNUX (liga a área de trabalho do navegador a este aparelho)\n\n"
           "uso: tnuxd [opções]      (ou: tnux desktop [opções])\n\n"
           "  --porta N            porta (padrão 8096; se ocupada, tenta as seguintes)\n"
           "  --abrir              abre o navegador ao iniciar\n"
           "  --token CHAVE        usa esta chave em vez de sortear uma\n"
           "  --permitir-host H    aceita pedidos para este nome (para usar atrás de um túnel https)\n"
           "  --rede               aceita conexões de outros aparelhos da rede (cuidado: quem tiver a chave controla este aparelho)\n"
           "  -v                   mostra os pedidos\n");
}

int main(int argc, char **argv) {
    int abrir = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--porta") && i + 1 < argc) porta = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--abrir")) abrir = 1;
        else if (!strcmp(argv[i], "--nao-abrir")) abrir = 0;
        else if (!strcmp(argv[i], "--rede")) rede = 1;
        else if (!strcmp(argv[i], "-v")) verboso = 1;
        else if (!strcmp(argv[i], "--token") && i + 1 < argc) snprintf(TOKEN, sizeof TOKEN, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--permitir-host") && i + 1 < argc) { if (n_hosts_extra < 8) snprintf(hosts_extra[n_hosts_extra++], 128, "%s", argv[++i]); else i++; }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { uso(); return 0; }
        else { fprintf(stderr, "tnuxd: opção desconhecida: %s\n\n", argv[i]); uso(); return 2; }
    }
    RAIZ = raiz_tnux();
    PASTA = pasta_tnux();
    char idx[4300];
    snprintf(idx, sizeof idx, "%s/web/index.html", RAIZ);
    if (access(idx, R_OK) != 0) { fprintf(stderr, "tnuxd: não achei a pasta web/ do TNUX (procurei em %s). Rode de dentro da pasta do TNUX ou defina TNUX_RAIZ.\n", RAIZ); return 1; }
    if (!TOKEN[0]) token_gerar();
    if (strlen(TOKEN) < 16) { fprintf(stderr, "tnuxd: a chave precisa ter pelo menos 16 caracteres\n"); return 2; }

    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = ao_sinal;
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL); sigaction(SIGHUP, &sa, NULL);

    int srv = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int um = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &um, sizeof um);
    struct sockaddr_in a;
    int ligou = 0;
    for (int t = 0; t < 12 && !ligou; t++) {
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons((uint16_t)(porta + t));
        a.sin_addr.s_addr = htonl(rede ? INADDR_ANY : INADDR_LOOPBACK);
        if (bind(srv, (struct sockaddr *)&a, sizeof a) == 0) { porta += t; ligou = 1; }
    }
    if (!ligou || listen(srv, 32) != 0) { fprintf(stderr, "tnuxd: não consegui abrir a porta %d: %s\n", porta, strerror(errno)); return 1; }
    nao_bloquear(srv);

    char url[256], so[256];
    snprintf(url, sizeof url, "http://127.0.0.1:%d/#token=%s", porta, TOKEN);
    nome_sistema(so, sizeof so);
    printf("\n\x1b[1;32m  TNUX " VERSAO " \"Aurora\"\x1b[0m: área de trabalho no modo ponte\n\n"
           "  Abra no navegador deste aparelho:\n\n    \x1b[1;36m%s\x1b[0m\n\n"
           "  Aparelho: %s%s\n  A parte depois de #token= é a chave: quem a tiver controla este aparelho. Não compartilhe.\n"
           "  Ctrl+C desliga.\n\n", url, so, rede ? "  |  \x1b[1;33maceitando conexões da rede\x1b[0m" : "");
    fflush(stdout);
    if (abrir) abrir_navegador(url);

    while (!parar) {
        struct pollfd pf[1 + MAX_CON * 2];
        Con *dono[1 + MAX_CON * 2];
        int papel[1 + MAX_CON * 2], n = 0;
        pf[n].fd = srv; pf[n].events = POLLIN; dono[n] = NULL; papel[n++] = 0;
        for (int i = 0; i < MAX_CON; i++) {
            Con *c = &cons[i];
            if (c->fd <= 0) continue;
            short ev = 0;
            if (!c->fechar && c->in.n < 64u * 1048576u && c->alvo_out.n < 8u * 1048576u) ev |= POLLIN;
            if (c->out.n || c->arq >= 0) ev |= POLLOUT;
            pf[n].fd = c->fd; pf[n].events = ev; dono[n] = c; papel[n++] = 1;
            if (c->alvo >= 0) {
                short ea = 0;
                if (c->out.n < 4u * 1048576u && !c->fechar) ea |= POLLIN;
                if (c->alvo_out.n) ea |= POLLOUT;
                pf[n].fd = c->alvo; pf[n].events = ea; dono[n] = c; papel[n++] = 2;
            }
        }
        int pr = poll(pf, (nfds_t)n, 1000);
        /* recolhe os processos que terminaram */
        int st;
        pid_t morto;
        while ((morto = waitpid(-1, &st, WNOHANG)) > 0) {
            if (morto == tela.pid) { tela.pid = 0; if (tela.wm > 0) kill(tela.wm, SIGTERM); tela.wm = 0; }
            else if (morto == tela.wm) tela.wm = 0;
            for (int i = 0; i < MAX_CON; i++) if (cons[i].fd > 0 && cons[i].pid == morto) cons[i].pid = 0;
        }
        if (pr <= 0) {
            time_t agora = time(NULL);       /* conexões HTTP paradas há muito tempo */
            for (int i = 0; i < MAX_CON; i++) if (cons[i].fd > 0 && cons[i].tipo == C_HTTP && cons[i].arq < 0 && agora - cons[i].nasceu > 120) con_fechar(&cons[i]);
            continue;
        }
        for (int k = 0; k < n; k++) {
            if (!pf[k].revents) continue;
            Con *c = dono[k];
            if (papel[k] == 0) {
                for (;;) {
                    int fd = accept(srv, NULL, NULL);
                    if (fd < 0) break;
                    nao_bloquear(fd);
                    fcntl(fd, F_SETFD, FD_CLOEXEC);
                    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &um, sizeof um);
                    if (!con_nova(fd)) close(fd);
                }
                continue;
            }
            if (c->fd <= 0) continue;
            char buf[65536];
            if (papel[k] == 2) {                     /* PTY ou tela */
                if (pf[k].revents & POLLOUT && c->alvo_out.n) {
                    ssize_t w = write(c->alvo, c->alvo_out.d, c->alvo_out.n);
                    if (w > 0) b_tira(&c->alvo_out, (size_t)w);
                }
                if (pf[k].revents & (POLLIN | POLLHUP | POLLERR)) {
                    ssize_t r = read(c->alvo, buf, sizeof buf);
                    if (r > 0) ws_enviar(c, 2, buf, (size_t)r);
                    else if (r == 0 || (errno != EAGAIN && errno != EINTR)) {
                        close(c->alvo); c->alvo = -1;      /* o shell saiu, ou a tela fechou */
                        ws_fim(c, 1000);
                    }
                }
                continue;
            }
            if (pf[k].revents & POLLIN) {
                ssize_t r = read(c->fd, buf, sizeof buf);
                if (r > 0) {
                    b_put(&c->in, buf, (size_t)r);
                    if (c->tipo == C_HTTP) { if (!c->fechar) http_processar(c); if (c->tipo != C_HTTP) ws_processar(c); }
                    else ws_processar(c);
                } else if (r == 0 || (errno != EAGAIN && errno != EINTR)) { con_fechar(c); continue; }
            } else if (pf[k].revents & (POLLHUP | POLLERR)) { con_fechar(c); continue; }
            if (pf[k].revents & POLLOUT) {
                if (!c->out.n && c->arq >= 0) {       /* próxima fatia do arquivo */
                    size_t q = c->arq_resta < (long long)sizeof buf ? (size_t)c->arq_resta : sizeof buf;
                    ssize_t r = read(c->arq, buf, q);
                    if (r > 0) { b_put(&c->out, buf, (size_t)r); c->arq_resta -= r; }
                    if (r <= 0 || c->arq_resta <= 0) { close(c->arq); c->arq = -1; }
                }
                if (c->out.n) {
                    ssize_t w = write(c->fd, c->out.d, c->out.n);
                    if (w > 0) b_tira(&c->out, (size_t)w);
                    else if (w < 0 && errno != EAGAIN && errno != EINTR) { con_fechar(c); continue; }
                }
                if (!c->out.n && c->arq < 0 && c->fechar) con_fechar(c);
            }
        }
    }
    printf("\nDesligando a ponte...\n");
    for (int i = 0; i < MAX_CON; i++) con_fechar(&cons[i]);
    tela_parar();
    close(srv);
    return 0;
}
