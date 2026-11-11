/*
 * TNUX: funções comuns aos programas nativos (tnux e tnuxd): só libc, sem depender do núcleo.
 */
#ifndef TNUX_PONTE_COMUM_H
#define TNUX_PONTE_COMUM_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* o programa "nome" existe no PATH? */
static inline int tem_comando(const char *nome) {
    if (strchr(nome, '/')) return access(nome, X_OK) == 0;
    const char *path = getenv("PATH");
    if (!path) return 0;
    char cam[4096];
    while (*path) {
        size_t n = strcspn(path, ":");
        if (n && n + strlen(nome) + 2 < sizeof cam) {
            snprintf(cam, sizeof cam, "%.*s/%s", (int)n, path, nome);
            struct stat st;
            if (stat(cam, &st) == 0 && S_ISREG(st.st_mode) && access(cam, X_OK) == 0) return 1;
        }
        path += n;
        if (*path == ':') path++;
    }
    return 0;
}

static inline int eh_termux(void) {
    const char *p = getenv("PREFIX");
    return p && strstr(p, "com.termux") != NULL;
}

/* qual gerenciador de pacotes o aparelho usa: "termux", "apt", "pacman", "dnf", "apk", "zypper" ou NULL */
static inline const char *gerenciador(void) {
    if (eh_termux()) return "termux";
    if (tem_comando("apt-get")) return "apt";
    if (tem_comando("pacman")) return "pacman";
    if (tem_comando("dnf")) return "dnf";
    if (tem_comando("apk")) return "apk";
    if (tem_comando("zypper")) return "zypper";
    return NULL;
}

/* pasta onde o TNUX está (a que tem web/index.html). Devolve um texto alocado. */
static inline char *raiz_tnux(void) {
    const char *e = getenv("TNUX_RAIZ");
    if (e && *e) return strdup(e);
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        char *b = strrchr(exe, '/');
        if (b) {
            *b = 0;
            char teste[4200];
            snprintf(teste, sizeof teste, "%s/../web/index.html", exe);
            if (access(teste, R_OK) == 0) { b = strrchr(exe, '/'); if (b) *b = 0; return strdup(exe); }
            snprintf(teste, sizeof teste, "%s/web/index.html", exe);
            if (access(teste, R_OK) == 0) return strdup(exe);
        }
    }
    return strdup(".");
}

/* ~/.tnux (cria se faltar). Devolve um texto alocado. */
static inline char *pasta_tnux(void) {
    const char *casa = getenv("HOME");
    char cam[4096];
    snprintf(cam, sizeof cam, "%s/.tnux", casa && *casa ? casa : "/tmp");
    mkdir(cam, 0700);
    return strdup(cam);
}

/* nome bonito do sistema do aparelho, lido de /etc/os-release */
static inline void nome_sistema(char *dst, size_t n) {
    snprintf(dst, n, "%s", eh_termux() ? "Termux (Android)" : "Linux");
    if (eh_termux()) return;
    FILE *f = fopen("/etc/os-release", "r");
    if (!f) return;
    char l[512];
    while (fgets(l, sizeof l, f)) {
        if (strncmp(l, "PRETTY_NAME=", 12)) continue;
        char *v = l + 12;
        size_t t = strlen(v);
        while (t && (v[t - 1] == '\n' || v[t - 1] == '"')) v[--t] = 0;
        if (*v == '"') v++;
        snprintf(dst, n, "%.200s", v);
        break;
    }
    fclose(f);
}
#endif
