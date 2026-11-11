/*
 * TNUX no terminal: a ponte de comandos.
 *
 * Este é o "tnux" que roda no Termux ou no terminal do Linux. Ele é um shell com a cara do TNUX
 * (prompt, histórico, Tab), mas quem executa os comandos é o aparelho: cada linha digitada vai
 * para o shell do sistema, e os programas instalados são programas Linux de verdade.
 *
 *   tnux                     abre o shell
 *   tnux -c 'comandos'       roda e sai
 *   tnux instalar NOME       instala um programa (apt/pkg/pacman/dnf/apk/zypper do aparelho)
 *   tnux apt ...             o apt do TNUX: entende os nomes do catálogo e o gerenciador do aparelho
 *   tnux desktop             liga a área de trabalho no navegador (tnuxd)
 *   tnux --sandbox           o sistema do navegador rodando no terminal (não mexe no aparelho)
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "../nucleo/tnux.h"
#include "comum.h"
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>

int sandbox_main(int argc, char **argv);

static char *RAIZ;

/* ------------------------------------------------------------------ rodar comandos do aparelho */

static int rodar_sh(const char *linha) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { perror("tnux: fork"); return 127; }
    if (pid == 0) {
        signal(SIGINT, SIG_DFL); signal(SIGQUIT, SIG_DFL); signal(SIGTSTP, SIG_DFL);
        const char *sh = tem_comando("bash") ? "bash" : "sh";
        execlp(sh, sh, "-c", linha, (char *)NULL);
        fprintf(stderr, "tnux: não foi possível iniciar o shell do aparelho (%s)\n", sh);
        _exit(127);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (WIFSIGNALED(st)) { if (WTERMSIG(st) != SIGINT) fprintf(stderr, "\n[terminado pelo sinal %d]\n", WTERMSIG(st)); else fputc('\n', stdout); return 128 + WTERMSIG(st); }
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

/* "sudo " quando precisa e existe */
static const char *sudo(void) {
    if (geteuid() == 0 || eh_termux()) return "";
    if (tem_comando("sudo")) return "sudo ";
    if (tem_comando("doas")) return "doas ";
    return "";
}

static int mostrar_e_rodar(const char *cmd) {
    printf("\x1b[1;32m→\x1b[0m \x1b[1m%s\x1b[0m\n", cmd);
    return rodar_sh(cmd);
}

/* ------------------------------------------------------------------ catálogo e pacotes */

static JVal *catalogo_ler(void) {
    char cam[4200];
    snprintf(cam, sizeof cam, "%s/web/catalogo.json", RAIZ);
    FILE *f = fopen(cam, "rb");
    if (!f) return NULL;
    Buf b; buf_init(&b);
    char tmp[8192]; size_t n;
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) buf_put(&b, tmp, n);
    fclose(f);
    JVal *j = json_ler(buf_str(&b), b.n);
    buf_free(&b);
    return j;
}
static JVal *pkg_achar(JVal *cat, const char *nome) {
    JVal *ps = json_obter(cat, "pacotes");
    if (!ps || ps->tipo != J_LISTA) return NULL;
    for (int i = 0; i < ps->n; i++) {
        JVal *p = ps->itens[i];
        if (!strcasecmp(json_texto(p, "id", ""), nome)) return p;
        JVal *ap = json_obter(p, "apelidos");
        for (int k = 0; ap && ap->tipo == J_LISTA && k < ap->n; k++)
            if (ap->itens[k]->tipo == J_STR && !strcasecmp(ap->itens[k]->str, nome)) return p;
    }
    return NULL;
}

/* monta o comando do gerenciador do aparelho para uma operação */
static char *cmd_gerenciador(const char *ger, const char *op, const char *nomes) {
    const char *s = sudo();
    if (!strcmp(ger, "termux")) {
        if (!strcmp(op, "install")) return xasprintf("pkg install -y %s", nomes);
        if (!strcmp(op, "remove")) return xasprintf("pkg uninstall -y %s", nomes);
        if (!strcmp(op, "update")) return xstrdup("pkg update -y");
        if (!strcmp(op, "upgrade")) return xstrdup("pkg upgrade -y");
        if (!strcmp(op, "search")) return xasprintf("pkg search %s", nomes);
        if (!strcmp(op, "list")) return xstrdup("pkg list-installed");
        if (!strcmp(op, "show")) return xasprintf("pkg show %s", nomes);
    } else if (!strcmp(ger, "apt")) {
        if (!strcmp(op, "install")) return xasprintf("%sapt-get install -y %s", s, nomes);
        if (!strcmp(op, "remove")) return xasprintf("%sapt-get remove -y %s", s, nomes);
        if (!strcmp(op, "update")) return xasprintf("%sapt-get update", s);
        if (!strcmp(op, "upgrade")) return xasprintf("%sapt-get upgrade -y", s);
        if (!strcmp(op, "search")) return xasprintf("apt-cache search %s", nomes);
        if (!strcmp(op, "list")) return xstrdup("dpkg -l");
        if (!strcmp(op, "show")) return xasprintf("apt-cache show %s", nomes);
    } else if (!strcmp(ger, "pacman")) {
        if (!strcmp(op, "install")) return xasprintf("%spacman -S --needed --noconfirm %s", s, nomes);
        if (!strcmp(op, "remove")) return xasprintf("%spacman -Rs --noconfirm %s", s, nomes);
        if (!strcmp(op, "update")) return xasprintf("%spacman -Sy", s);
        if (!strcmp(op, "upgrade")) return xasprintf("%spacman -Syu --noconfirm", s);
        if (!strcmp(op, "search")) return xasprintf("pacman -Ss %s", nomes);
        if (!strcmp(op, "list")) return xstrdup("pacman -Q");
        if (!strcmp(op, "show")) return xasprintf("pacman -Si %s", nomes);
    } else if (!strcmp(ger, "dnf")) {
        if (!strcmp(op, "install")) return xasprintf("%sdnf install -y %s", s, nomes);
        if (!strcmp(op, "remove")) return xasprintf("%sdnf remove -y %s", s, nomes);
        if (!strcmp(op, "update")) return xasprintf("%sdnf makecache", s);
        if (!strcmp(op, "upgrade")) return xasprintf("%sdnf upgrade -y", s);
        if (!strcmp(op, "search")) return xasprintf("dnf search %s", nomes);
        if (!strcmp(op, "list")) return xstrdup("dnf list --installed");
        if (!strcmp(op, "show")) return xasprintf("dnf info %s", nomes);
    } else if (!strcmp(ger, "apk")) {
        if (!strcmp(op, "install")) return xasprintf("%sapk add %s", s, nomes);
        if (!strcmp(op, "remove")) return xasprintf("%sapk del %s", s, nomes);
        if (!strcmp(op, "update")) return xasprintf("%sapk update", s);
        if (!strcmp(op, "upgrade")) return xasprintf("%sapk upgrade", s);
        if (!strcmp(op, "search")) return xasprintf("apk search %s", nomes);
        if (!strcmp(op, "list")) return xstrdup("apk list -I");
        if (!strcmp(op, "show")) return xasprintf("apk info %s", nomes);
    } else if (!strcmp(ger, "zypper")) {
        if (!strcmp(op, "install")) return xasprintf("%szypper --non-interactive install %s", s, nomes);
        if (!strcmp(op, "remove")) return xasprintf("%szypper --non-interactive remove %s", s, nomes);
        if (!strcmp(op, "update")) return xasprintf("%szypper refresh", s);
        if (!strcmp(op, "upgrade")) return xasprintf("%szypper --non-interactive update", s);
        if (!strcmp(op, "search")) return xasprintf("zypper search %s", nomes);
        if (!strcmp(op, "list")) return xstrdup("zypper search -i");
        if (!strcmp(op, "show")) return xasprintf("zypper info %s", nomes);
    }
    return NULL;
}

/* só letras, números e os sinais comuns em nomes de pacote: nada que o shell interprete */
static int nome_seguro(const char *s) {
    if (!*s || *s == '-') return 0;
    for (; *s; s++) if (!(isalnum((unsigned char)*s) || strchr("+-_.:@/=~", *s))) return 0;
    return 1;
}

/* grava ~/.tnux/apps/ID/.tnux-app.json: é o que faz o app aparecer no menu da área de trabalho */
static void manifesto(const char *dir, const char *id, const char *nome, const char *resumo, const char *icone, const char *categoria, const char *entrada, const char *url) {
    char cam[4300];
    snprintf(cam, sizeof cam, "%s/.tnux-app.json", dir);
    Buf b; buf_init(&b);
    buf_puts(&b, "{\"id\":"); buf_json_str(&b, id);
    buf_puts(&b, ",\"nome\":"); buf_json_str(&b, nome);
    buf_puts(&b, ",\"resumo\":"); buf_json_str(&b, resumo);
    buf_puts(&b, ",\"icone\":"); buf_json_str(&b, icone);
    buf_puts(&b, ",\"categoria\":"); buf_json_str(&b, categoria);
    if (entrada) { buf_puts(&b, ",\"entrada\":"); buf_json_str(&b, entrada); }
    if (url) { buf_puts(&b, ",\"url\":"); buf_json_str(&b, url); }
    buf_puts(&b, "}\n");
    FILE *f = fopen(cam, "w");
    if (f) { fwrite(b.d, 1, b.n, f); fclose(f); }
    buf_free(&b);
}

static int instalar_git(const char *id, const char *repo, const char *ramo, const char *nome, const char *resumo, const char *icone, const char *categoria, const char *entrada) {
    if (!tem_comando("git")) { fprintf(stderr, "tnux: o git não está instalado no aparelho. Instale com: tnux instalar git\n"); return 1; }
    char *base = pasta_tnux();
    char dir[4200], apps[4200];
    snprintf(apps, sizeof apps, "%s/apps", base);
    mkdir(apps, 0700);
    snprintf(dir, sizeof dir, "%s/apps/%s", base, id);
    free(base);
    char git[4300];
    snprintf(git, sizeof git, "%s/.git", dir);
    char *cmd;
    if (access(git, F_OK) == 0) cmd = xasprintf("git -C '%s' pull --ff-only", dir);
    else if (ramo && *ramo) cmd = xasprintf("git clone --depth 1 -b '%s' 'https://github.com/%s' '%s'", ramo, repo, dir);
    else cmd = xasprintf("git clone --depth 1 'https://github.com/%s' '%s'", repo, dir);
    int r = mostrar_e_rodar(cmd);
    free(cmd);
    if (r) return r;
    char idx[4400];
    snprintf(idx, sizeof idx, "%s/%s", dir, entrada ? entrada : "index.html");
    if (access(idx, R_OK) == 0) {
        manifesto(dir, id, nome, resumo, icone, categoria, entrada ? entrada : "index.html", NULL);
        printf("Pronto: \"%s\" aparece no menu da área de trabalho do TNUX (tnux desktop).\n", nome);
    } else printf("Pronto: os arquivos estão em %s\n(o repositório não tem uma página index.html, então não entra no menu).\n", dir);
    return 0;
}

static int instalar_um(JVal *cat, const char *ger, const char *nome) {
    struct utsname un;
    uname(&un);
    JVal *p = pkg_achar(cat, nome);
    if (!p) {
        /* dono/repositório do GitHub */
        const char *barra = strchr(nome, '/');
        if (barra && barra != nome && barra[1] && !strchr(barra + 1, '/') && nome_seguro(nome) && !strstr(nome, "..")) {
            char *resumo = xasprintf("Instalado de github.com/%s", nome);
            int r = instalar_git(barra + 1, nome, NULL, barra + 1, resumo, "web", "Outros", NULL);
            free(resumo);
            return r;
        }
        if (!nome_seguro(nome)) { fprintf(stderr, "tnux: nome de pacote inválido: %s\n", nome); return 1; }
        if (!ger) { fprintf(stderr, "tnux: não reconheci o gerenciador de pacotes deste aparelho; instale \"%s\" pelo seu sistema.\n", nome); return 1; }
        char *cmd = cmd_gerenciador(ger, "install", nome);      /* fora do catálogo: vai direto para o aparelho */
        int r = mostrar_e_rodar(cmd);
        free(cmd);
        return r;
    }
    const char *id = json_texto(p, "id", nome), *tipo = json_texto(p, "tipo", "");
    const char *nm = json_texto(p, "nome", id), *resumo = json_texto(p, "resumo", "");
    if (!strcmp(tipo, "git"))
        return instalar_git(id, json_texto(p, "repo", ""), json_texto(p, "ramo", NULL), nm, resumo, json_texto(p, "icone", "web"), json_texto(p, "categoria", "Outros"), json_texto(p, "entrada", NULL));
    if (!strcmp(tipo, "web")) {
        char *base = pasta_tnux();
        char dir[4200];
        snprintf(dir, sizeof dir, "%s/apps", base); mkdir(dir, 0700);
        snprintf(dir, sizeof dir, "%s/apps/%s", base, id); mkdir(dir, 0700);
        free(base);
        manifesto(dir, id, nm, resumo, json_texto(p, "icone", "web"), json_texto(p, "categoria", "Outros"), NULL, json_texto(p, "url", ""));
        printf("Pronto: \"%s\" aparece no menu da área de trabalho do TNUX.\n", nm);
        return 0;
    }
    if (!strcmp(tipo, "wasm") || !strcmp(tipo, "script")) {
        printf("\"%s\" é um programa do TNUX do navegador (WebAssembly). Use-o lá, ou aqui com: tnux --sandbox\n", id);
        return 0;
    }
    if (strcmp(tipo, "nativo")) { fprintf(stderr, "tnux: o pacote %s não pode ser instalado aqui (tipo %s)\n", id, tipo); return 1; }

    JVal *n = json_obter(p, "nativo");
    const char *nota = json_texto(n, "nota", NULL), *alt = json_texto(n, "alternativa", NULL);
    if (!ger) { fprintf(stderr, "tnux: não reconheci o gerenciador de pacotes deste aparelho; instale \"%s\" pelo seu sistema.\n", nm); return 1; }
    JVal *cmds = json_obter(n, "comandos");
    const char *linha = json_texto(cmds, ger, NULL);
    if (!linha) linha = json_texto(cmds, "*", NULL);
    const char *pacotes = json_texto(n, ger, NULL);
    int so_x86 = json_numero(n, "so_x86_64", 0) != 0 && strcmp(un.machine, "x86_64");
    if (so_x86 || (!linha && !pacotes)) {
        if (nota) printf("\x1b[33m%s\x1b[0m\n", nota);
        else printf("\x1b[33m\"%s\" não está disponível para este sistema (%s, %s).\x1b[0m\n", nm, ger, un.machine);
        if (alt && strcasecmp(alt, id)) { printf("Instalando a alternativa: %s\n", alt); return instalar_um(cat, ger, alt); }
        return 1;
    }
    printf("Instalando %s (%s) neste aparelho...\n", nm, resumo);
    if (!strcmp(ger, "termux")) {
        const char *repos = json_texto(n, "termux_repos", NULL);
        if (repos) { char *c1 = cmd_gerenciador(ger, "install", repos); int r = mostrar_e_rodar(c1); free(c1); if (r) return r; }
    }
    int r;
    if (linha) {
        /* o "sudo" dos comandos prontos só vale se o aparelho tiver sudo e você não for root */
        Buf b; buf_init(&b);
        for (const char *s = linha; *s;) {
            if (!strncmp(s, "sudo ", 5) && (s == linha || s[-1] == ' ')) { buf_puts(&b, sudo()); s += 5; }
            else buf_putc(&b, *s++);
        }
        r = mostrar_e_rodar(buf_str(&b));
        buf_free(&b);
    } else {
        char *cmd = cmd_gerenciador(ger, "install", pacotes);
        r = mostrar_e_rodar(cmd);
        free(cmd);
    }
    if (r == 0) {
        const char *ex = json_texto(n, "exec", NULL);
        if (ex && tem_comando(ex)) printf("\x1b[1;32mPronto:\x1b[0m %s instalado. Para abrir: \x1b[1m%s\x1b[0m\n", nm, ex);
    } else if (alt && strcasecmp(alt, id)) {
        printf("\x1b[33mNão deu certo. Tentando a alternativa: %s\x1b[0m\n", alt);
        return instalar_um(cat, ger, alt);
    }
    return r;
}

static int remover_um(JVal *cat, const char *ger, const char *nome) {
    JVal *p = pkg_achar(cat, nome);
    const char *id = p ? json_texto(p, "id", nome) : nome;
    const char *tipo = p ? json_texto(p, "tipo", "") : "";
    char *base = pasta_tnux();
    char dir[4200];
    const char *barra = strrchr(id, '/');
    snprintf(dir, sizeof dir, "%s/apps/%s", base, barra ? barra + 1 : id);
    free(base);
    if (nome_seguro(id) && !strstr(id, "..") && access(dir, F_OK) == 0 && (!p || !strcmp(tipo, "git") || !strcmp(tipo, "web"))) {
        char *cmd = xasprintf("rm -rf '%s'", dir);
        int r = mostrar_e_rodar(cmd);
        free(cmd);
        return r;
    }
    if (!ger) { fprintf(stderr, "tnux: não reconheci o gerenciador de pacotes deste aparelho.\n"); return 1; }
    const char *pacotes = p ? json_texto(json_obter(p, "nativo"), ger, NULL) : nome;
    if (!pacotes) pacotes = p ? json_texto(json_obter(p, "nativo"), "exec", nome) : nome;
    if (!nome_seguro(pacotes) && !strchr(pacotes, ' ')) { fprintf(stderr, "tnux: nome de pacote inválido: %s\n", pacotes); return 1; }
    char *cmd = cmd_gerenciador(ger, "remove", pacotes);
    int r = mostrar_e_rodar(cmd);
    free(cmd);
    return r;
}

/* o apt do TNUX: mesmos subcomandos do apt, traduzidos para o gerenciador do aparelho */
static int apt_ponte(int argc, char **argv) {
    const char *ger = gerenciador();
    const char *sub = NULL;
    int i = 1;
    for (; i < argc; i++) if (argv[i][0] != '-') { sub = argv[i++]; break; }
    if (!sub) {
        printf("apt do TNUX (modo ponte). Gerenciador deste aparelho: %s\n"
               "Uso: apt update | upgrade | install NOME... | remove NOME... | search TERMO | show NOME | list\n"
               "Os nomes do catálogo do TNUX (chrome, firefox, vlc, gimp, lnux-video...) são traduzidos para o seu sistema;\n"
               "os outros vão direto para o gerenciador do aparelho. \"install USUARIO/REPOSITORIO\" baixa do GitHub.\n", ger ? ger : "não reconhecido");
        return 0;
    }
    JVal *cat = catalogo_ler();
    int r = 0;
    if (!strcmp(sub, "install") || !strcmp(sub, "instalar") || !strcmp(sub, "reinstall")) {
        int n = 0;
        for (; i < argc; i++) { if (argv[i][0] == '-') continue; n++; if (instalar_um(cat, ger, argv[i])) r = 100; }
        if (!n) { fprintf(stderr, "E: informe pelo menos um pacote: apt install NOME\n"); r = 100; }
    } else if (!strcmp(sub, "remove") || !strcmp(sub, "purge") || !strcmp(sub, "remover")) {
        for (; i < argc; i++) { if (argv[i][0] == '-') continue; if (remover_um(cat, ger, argv[i])) r = 100; }
    } else if (!strcmp(sub, "search") || !strcmp(sub, "show") || !strcmp(sub, "info")) {
        const char *termo = i < argc ? argv[i] : "";
        const char *op = !strcmp(sub, "search") ? "search" : "show";
        JVal *ps = json_obter(cat, "pacotes");
        if (!strcmp(op, "search")) {
            for (int k = 0; ps && ps->tipo == J_LISTA && k < ps->n; k++) {
                JVal *p = ps->itens[k];
                const char *id = json_texto(p, "id", ""), *nm = json_texto(p, "nome", ""), *rs = json_texto(p, "resumo", "");
                int achou = 0;
                for (const char *t = id; *t && !achou; t++) if (!strncasecmp(t, termo, strlen(termo))) achou = 1;
                for (const char *t = nm; *t && !achou; t++) if (!strncasecmp(t, termo, strlen(termo))) achou = 1;
                for (const char *t = rs; *t && !achou; t++) if (!strncasecmp(t, termo, strlen(termo))) achou = 1;
                if (achou && *termo) printf("\x1b[32m%s\x1b[0m (catálogo do TNUX, %s)\n  %s: %s\n\n", id, json_texto(p, "tipo", ""), nm, rs);
            }
        }
        if (ger && nome_seguro(termo)) { char *cmd = cmd_gerenciador(ger, op, termo); r = mostrar_e_rodar(cmd); free(cmd); }
    } else if (!strcmp(sub, "update") || !strcmp(sub, "upgrade") || !strcmp(sub, "list") || !strcmp(sub, "full-upgrade") || !strcmp(sub, "dist-upgrade")) {
        const char *op = !strcmp(sub, "update") ? "update" : !strcmp(sub, "list") ? "list" : "upgrade";
        if (!ger) { fprintf(stderr, "tnux: não reconheci o gerenciador de pacotes deste aparelho.\n"); r = 1; }
        else { char *cmd = cmd_gerenciador(ger, op, ""); r = mostrar_e_rodar(cmd); free(cmd); }
    } else if (ger && (!strcmp(ger, "apt") || !strcmp(ger, "termux"))) {
        /* outros subcomandos (autoremove, policy...): o apt de verdade resolve */
        Buf b; buf_init(&b);
        buf_printf(&b, "%sapt", sudo());
        for (int k = 1; k < argc; k++) { buf_puts(&b, " '"); for (const char *s = argv[k]; *s; s++) { if (*s == '\'') buf_puts(&b, "'\\''"); else buf_putc(&b, *s); } buf_putc(&b, '\''); }
        r = mostrar_e_rodar(buf_str(&b));
        buf_free(&b);
    } else { fprintf(stderr, "E: Operação inválida %s\n", sub); r = 100; }
    json_livrar(cat);
    return r;
}

/* ------------------------------------------------------------------ informações do sistema */

static void banner(void) {
    char so[256];
    nome_sistema(so, sizeof so);
    struct utsname un;
    uname(&un);
    struct passwd *pw = getpwuid(getuid());
    char host[128] = "aparelho";
    gethostname(host, sizeof host - 1);
    const char *ger = gerenciador();
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    long pags = sysconf(_SC_PHYS_PAGES), tpag = sysconf(_SC_PAGE_SIZE);
    static const char *const logo[] = {
        "  \x1b[1;32m.:::::::::::::::::::::::.\x1b[0m  ", " \x1b[1;32m:::::::::::::::::::::::::::\x1b[0m ",
        " \x1b[1;32m:::\x1b[1;37m#####################\x1b[1;32m:::\x1b[0m ", " \x1b[1;32m:::\x1b[1;37m#####################\x1b[1;32m:::\x1b[0m ",
        " \x1b[1;32m::::::::::\x1b[1;37m#######\x1b[1;32m::::::::::\x1b[0m ", " \x1b[1;32m::::::::::\x1b[1;37m#######\x1b[1;32m::::::::::\x1b[0m ",
        " \x1b[1;32m::::::::::\x1b[1;37m#######\x1b[1;32m::::::::::\x1b[0m ", " \x1b[1;32m::::::::::\x1b[1;37m#######\x1b[1;32m::::::::::\x1b[0m ",
        " \x1b[1;32m:::::::::::::::::::::::::::\x1b[0m ", "  \x1b[1;32m':::::::::::::::::::::::'\x1b[0m  ",
    };
    char info[10][300];
    int n = 0;
    snprintf(info[n++], 300, "\x1b[1;32m%s\x1b[0m@\x1b[1;32m%s\x1b[0m", pw ? pw->pw_name : "usuario", host);
    snprintf(info[n++], 300, "-------------");
    snprintf(info[n++], 300, "\x1b[1;32mSO\x1b[0m: %s %s %s (modo ponte)", TNUX_NOME, TNUX_VERSAO, TNUX_CODINOME);
    snprintf(info[n++], 300, "\x1b[1;32mAparelho\x1b[0m: %s", so);
    snprintf(info[n++], 300, "\x1b[1;32mKernel\x1b[0m: %s %s", un.sysname, un.release);
    snprintf(info[n++], 300, "\x1b[1;32mArquitetura\x1b[0m: %s", un.machine);
    snprintf(info[n++], 300, "\x1b[1;32mPacotes\x1b[0m: %s", ger ? ger : "gerenciador não reconhecido");
    snprintf(info[n++], 300, "\x1b[1;32mCPU\x1b[0m: %ld núcleo%s", cpus, cpus == 1 ? "" : "s");
    snprintf(info[n++], 300, "\x1b[1;32mMemória\x1b[0m: %.0f MiB", pags > 0 ? (double)pags * (double)tpag / 1048576.0 : 0.0);
    snprintf(info[n++], 300, "\x1b[40m   \x1b[41m   \x1b[42m   \x1b[43m   \x1b[44m   \x1b[45m   \x1b[46m   \x1b[47m   \x1b[0m");
    struct winsize ws;
    int estreito = ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_col < 72;
    for (int i = 0; i < 10; i++) {
        if (estreito) printf("%s\n", info[i]);
        else printf("%s  %s\n", logo[i], i < n ? info[i] : "");
    }
}

static void ajuda(void) {
    printf("\x1b[1;32m%s %s \"%s\"\x1b[0m no terminal: a ponte de comandos.\n\n"
           "Aqui os comandos são os do seu aparelho: tudo o que você digitar roda de verdade.\n\n"
           "\x1b[1mComandos do TNUX\x1b[0m\n"
           "  apt install NOME       instala um programa (chrome, firefox, vlc, gimp, python...)\n"
           "  apt install DONO/REPO  baixa um repositório do GitHub para ~/.tnux/apps\n"
           "  apt search TERMO       procura no catálogo do TNUX e no seu sistema\n"
           "  apt remove NOME        remove\n"
           "  tnux desktop           liga a área de trabalho (abre no navegador)\n"
           "  tnux fetch             informações do sistema\n"
           "  tnux --sandbox         o TNUX do navegador rodando aqui, isolado do aparelho\n"
           "  exit                   sai\n\n"
           "Dentro do shell: setas e Tab funcionam; Ctrl+C interrompe o comando; Ctrl+D sai.\n", TNUX_NOME, TNUX_VERSAO, TNUX_CODINOME);
}

static int desktop(int argc, char **argv) {
    char cam[4200];
    snprintf(cam, sizeof cam, "%s/build/tnuxd", RAIZ);
    if (access(cam, X_OK) != 0) { fprintf(stderr, "tnux: não achei o servidor da área de trabalho (%s). Rode: bash instalar.sh\n", cam); return 1; }
    char **av = xmalloc(sizeof(char *) * (size_t)(argc + 3));
    int n = 0;
    av[n++] = cam;
    int tem_abrir = 0;
    for (int i = 0; i < argc; i++) { av[n++] = argv[i]; if (!strcmp(argv[i], "--nao-abrir") || !strcmp(argv[i], "--abrir")) tem_abrir = 1; }
    if (!tem_abrir) av[n++] = "--abrir";
    av[n] = NULL;
    execv(cam, av);
    perror("tnux: tnuxd");
    return 1;
}

/* ------------------------------------------------------------------ o shell */

static struct termios termios_normal;
static int tem_termios;
static void modo_cru(void) {
    if (!tem_termios) return;
    struct termios t = termios_normal;
    t.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    t.c_oflag &= ~(tcflag_t)OPOST;
    t.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSADRAIN, &t);
}
static void modo_normal(void) { if (tem_termios) tcsetattr(0, TCSADRAIN, &termios_normal); }

static void ed_out(void *quem, const char *p, size_t n) {
    (void)quem;
    while (n) { ssize_t w = write(1, p, n); if (w <= 0) { if (errno == EINTR) continue; return; } p += w; n -= (size_t)w; }
}

static int cmp_txt(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* Tab: programas do PATH (primeira palavra) ou arquivos do aparelho */
static char **completar(void *quem, const char *linha, size_t ini, size_t pos, int *n) {
    (void)quem;
    char **v = NULL; int qt = 0, cap = 0;
#define ADD(s) do { int dup_ = 0; for (int q_ = 0; q_ < qt; q_++) if (!strcmp(v[q_], (s))) dup_ = 1; \
        if (!dup_) { if (qt == cap) { cap = cap ? cap * 2 : 32; v = xrealloc(v, sizeof(char *) * (size_t)cap); } v[qt++] = xstrdup(s); } } while (0)
    char *pal = xmalloc(pos - ini + 1);
    size_t o = 0;
    for (size_t i = ini; i < pos; i++) { if (linha[i] == '\\' && i + 1 < pos) i++; pal[o++] = linha[i]; }
    pal[o] = 0;
    size_t k = ini;
    while (k > 0 && linha[k - 1] == ' ') k--;
    int primeira = k == 0 || strchr("|;&(", linha[k - 1]) != NULL || (k >= 4 && !strncmp(linha + k - 4, "sudo", 4));
    size_t lp = strlen(pal);
    if (primeira && !strchr(pal, '/') && lp > 0) {
        static const char *const internos[] = { "apt", "tnux", "cd", "exit", "help", "ajuda", "export", "history", NULL };
        for (int i = 0; internos[i]; i++) if (!strncmp(internos[i], pal, lp)) ADD(internos[i]);
        const char *path = getenv("PATH");
        while (path && *path && qt < 2000) {
            size_t l = strcspn(path, ":");
            char *d = xasprintf("%.*s", (int)l, path);
            DIR *dir = opendir(d);
            if (dir) {
                struct dirent *e;
                while ((e = readdir(dir))) if (!strncmp(e->d_name, pal, lp) && e->d_name[0] != '.') ADD(e->d_name);
                closedir(dir);
            }
            free(d);
            path += l;
            if (*path == ':') path++;
        }
    } else {
        char *barra = strrchr(pal, '/');
        char *dirtxt = barra ? xasprintf("%.*s", (int)(barra - pal + 1), pal) : xstrdup("");
        const char *prefixo = barra ? barra + 1 : pal;
        const char *casa = getenv("HOME");
        char *real = (dirtxt[0] == '~' && (dirtxt[1] == '/' || !dirtxt[1]) && casa) ? xasprintf("%s%s", casa, dirtxt + 1) : xstrdup(*dirtxt ? dirtxt : ".");
        DIR *dir = opendir(real);
        size_t lpre = strlen(prefixo);
        if (dir) {
            struct dirent *e;
            while ((e = readdir(dir)) && qt < 2000) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                if (strncmp(e->d_name, prefixo, lpre) || (e->d_name[0] == '.' && prefixo[0] != '.')) continue;
                char *cheio = xasprintf("%s/%s", real, e->d_name);
                struct stat st;
                int eh_dir = stat(cheio, &st) == 0 && S_ISDIR(st.st_mode);
                Buf b; buf_init(&b);
                buf_puts(&b, dirtxt);
                for (const char *s = e->d_name; *s; s++) { if (strchr(" '\"\\$&;|<>()*?[]#`!", *s)) buf_putc(&b, '\\'); buf_putc(&b, *s); }
                if (eh_dir) buf_putc(&b, '/');
                ADD(buf_str(&b));
                buf_free(&b);
                free(cheio);
            }
            closedir(dir);
        }
        free(dirtxt); free(real);
    }
    free(pal);
    if (qt > 1) qsort(v, (size_t)qt, sizeof(char *), cmp_txt);
    *n = qt;
    return v;
#undef ADD
}

static char *prompt(int ultimo) {
    char cwd[4096] = "?", host[128] = "aparelho";
    if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "?");
    gethostname(host, sizeof host - 1);
    char *ponto = strchr(host, '.');
    if (ponto) *ponto = 0;
    struct passwd *pw = getpwuid(getuid());
    const char *casa = getenv("HOME");
    size_t lc = casa ? strlen(casa) : 0;
    char mostra[4200];
    if (lc > 1 && !strncmp(cwd, casa, lc) && (cwd[lc] == 0 || cwd[lc] == '/')) snprintf(mostra, sizeof mostra, "~%s", cwd + lc);
    else snprintf(mostra, sizeof mostra, "%s", cwd);
    int root = geteuid() == 0;
    return xasprintf("%s\x1b[01;%dm%s@%s\x1b[00m:\x1b[01;34m%s\x1b[00m%s ", ultimo ? "\x1b[31m✗\x1b[0m " : "", root ? 31 : 32,
                     pw ? pw->pw_name : "usuario", host, mostra, root ? "#" : "$");
}

/* comandos que precisam ser do próprio tnux (mudam o estado do shell, ou são do TNUX) */
static int interno(char *linha, int *codigo, int *sair, Editor *ed) {
    char *p = linha;
    while (*p == ' ') p++;
    size_t n = strcspn(p, " \t");
    char *resto = p + n;
    while (*resto == ' ' || *resto == '\t') resto++;
    /* só intercepta comandos simples: com pipes, ; ou redirecionamento, vai para o shell do aparelho */
    int simples = !strpbrk(p, "|;&<>`$(){}\n");
#define EH(s) (n == strlen(s) && !strncmp(p, s, n))
    if (EH("exit") || EH("logout") || EH("sair")) { *sair = 1; *codigo = atoi(resto); return 1; }
    if (EH("cd") && simples) {
        char alvo[4096];
        const char *casa = getenv("HOME");
        size_t l = strlen(resto);
        while (l && resto[l - 1] == ' ') resto[--l] = 0;
        if ((resto[0] == '"' || resto[0] == '\'') && l >= 2 && resto[l - 1] == resto[0]) { resto[l - 1] = 0; resto++; }
        if (!*resto) snprintf(alvo, sizeof alvo, "%s", casa ? casa : "/");
        else if (!strcmp(resto, "-")) { const char *o = getenv("OLDPWD"); snprintf(alvo, sizeof alvo, "%s", o ? o : "."); printf("%s\n", alvo); }
        else if (resto[0] == '~' && (resto[1] == '/' || !resto[1])) snprintf(alvo, sizeof alvo, "%s%s", casa ? casa : "", resto + 1);
        else {
            size_t o = 0;
            for (const char *s = resto; *s && o < sizeof alvo - 1; s++) { if (*s == '\\' && s[1]) s++; alvo[o++] = *s; }
            alvo[o] = 0;
        }
        char antes[4096] = "";
        if (!getcwd(antes, sizeof antes)) antes[0] = 0;
        if (chdir(alvo) != 0) { fprintf(stderr, "tnux: cd: %s: %s\n", alvo, strerror(errno)); *codigo = 1; }
        else {
            char agora[4096];
            setenv("OLDPWD", antes, 1);
            if (getcwd(agora, sizeof agora)) setenv("PWD", agora, 1);
            *codigo = 0;
        }
        return 1;
    }
    if (EH("export") && simples && strchr(resto, '=')) {
        char *eq = strchr(resto, '=');
        *eq = 0;
        char *v = eq + 1;
        size_t l = strlen(v);
        while (l && v[l - 1] == ' ') v[--l] = 0;
        if ((v[0] == '"' || v[0] == '\'') && l >= 2 && v[l - 1] == v[0]) { v[l - 1] = 0; v++; }
        setenv(resto, v, 1);
        *eq = '=';
        *codigo = 0;
        return 1;
    }
    if (EH("unset") && simples && *resto) { unsetenv(resto); *codigo = 0; return 1; }
    if ((EH("help") || EH("ajuda")) && !*resto) { ajuda(); *codigo = 0; return 1; }
    if (EH("history") && !*resto) { for (int i = 0; i < ed_hist_n(ed); i++) printf("%5d  %s\n", i + 1, ed_hist(ed, i)); *codigo = 0; return 1; }
    if ((EH("apt") || EH("apt-get") || EH("tnux")) && simples) {
        /* separa os argumentos (aspas simples e duplas) e chama o próprio tnux */
        char *copia = xstrdup(p);
        char *av[128]; int ac = 0;
        for (char *s = copia; *s && ac < 127;) {
            while (*s == ' ' || *s == '\t') s++;
            if (!*s) break;
            char *o = s;
            av[ac++] = o;
            while (*s && *s != ' ' && *s != '\t') {
                if (*s == '"' || *s == '\'') { char q = *s++; while (*s && *s != q) *o++ = *s++; if (*s) s++; }
                else *o++ = *s++;
            }
            if (*s) s++;
            *o = 0;
        }
        av[ac] = NULL;
        if (EH("tnux")) {
            if (ac >= 2 && (!strcmp(av[1], "instalar") || !strcmp(av[1], "install"))) { av[1] = "install"; *codigo = apt_ponte(ac, av); }
            else if (ac >= 2 && (!strcmp(av[1], "remover") || !strcmp(av[1], "remove"))) { av[1] = "remove"; *codigo = apt_ponte(ac, av); }
            else if (ac >= 2 && !strcmp(av[1], "apt")) *codigo = apt_ponte(ac - 1, av + 1);
            else if (ac >= 2 && (!strcmp(av[1], "fetch") || !strcmp(av[1], "neofetch"))) { banner(); *codigo = 0; }
            else if (ac == 1 || !strcmp(av[1], "ajuda") || !strcmp(av[1], "help")) { ajuda(); *codigo = 0; }
            else if (!strcmp(av[1], "versao") || !strcmp(av[1], "--version")) { printf("%s %s (%s), modo ponte\n", TNUX_NOME, TNUX_VERSAO, TNUX_CODINOME); *codigo = 0; }
            else if (!strcmp(av[1], "modo")) { printf("ponte (os comandos rodam no aparelho)\n"); *codigo = 0; }
            else { free(copia); return 0; }       /* desktop, --sandbox...: roda o binário */
        } else *codigo = apt_ponte(ac, av);
        free(copia);
        return 1;
    }
#undef EH
    return 0;
}

static volatile sig_atomic_t redimensionou;
static void ao_winch(int s) { (void)s; redimensionou = 1; }

static int shell(void) {
    char *base = pasta_tnux();
    char *arq_hist = xasprintf("%s/historico", base);
    free(base);
    tem_termios = isatty(0) && tcgetattr(0, &termios_normal) == 0;
    if (!tem_termios) {            /* entrada vinda de um pipe: lê linha a linha */
        char *linha = NULL; size_t cap = 0;
        int cod = 0, sair = 0;
        while (!sair && getline(&linha, &cap, stdin) > 0) {
            size_t l = strlen(linha);
            while (l && (linha[l - 1] == '\n' || linha[l - 1] == '\r')) linha[--l] = 0;
            if (!*linha) continue;
            char *copia = xstrdup(linha);
            if (!interno(copia, &cod, &sair, NULL)) cod = rodar_sh(linha);
            free(copia);
        }
        free(linha);
        return cod;
    }
    signal(SIGINT, SIG_IGN); signal(SIGQUIT, SIG_IGN); signal(SIGTSTP, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = ao_winch;
    sigaction(SIGWINCH, &sa, NULL);
    Editor *ed = ed_novo(ed_out, completar, NULL);
    FILE *f = fopen(arq_hist, "r");
    if (f) {
        char *l = NULL; size_t cap = 0;
        while (getline(&l, &cap, f) > 0) { size_t n = strlen(l); while (n && l[n - 1] == '\n') l[--n] = 0; ed_hist_add(ed, l); }
        free(l);
        fclose(f);
    }
    setenv("TNUX", TNUX_VERSAO, 1);
    printf("\x1b[1;32m%s %s \"%s\"\x1b[0m: modo ponte. Os comandos rodam neste aparelho. Digite \x1b[1mhelp\x1b[0m para ajuda.\n", TNUX_NOME, TNUX_VERSAO, TNUX_CODINOME);
    int ultimo = 0, sair = 0;
    Buf pend; buf_init(&pend);
    while (!sair) {
        struct winsize ws;
        if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col) ed_colunas(ed, ws.ws_col);
        modo_cru();
        char *p = pend.n ? xstrdup("> ") : prompt(ultimo);
        ed_prompt(ed, p);
        free(p);
        int ev = ED_NADA;
        while (ev == ED_NADA) {
            unsigned char ch;
            ssize_t r = read(0, &ch, 1);
            if (r < 0 && errno == EINTR) {
                if (redimensionou) { redimensionou = 0; if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col) ed_colunas(ed, ws.ws_col); }
                continue;
            }
            if (r <= 0) { ev = ED_EOF; break; }
            ev = ed_byte(ed, ch);
        }
        modo_normal();
        if (ev == ED_EOF) { printf("exit\n"); break; }
        if (ev == ED_INTERROMPE) { pend.n = 0; ultimo = 130; continue; }
        buf_puts(&pend, ed_linha(ed));
        buf_putc(&pend, '\n');
        if (sh_incompleto(buf_str(&pend))) continue;
        char *linha = xstrdup(buf_str(&pend));
        pend.n = 0;
        size_t l = strlen(linha);
        while (l && linha[l - 1] == '\n') linha[--l] = 0;
        int vazio = 1;
        for (const char *s = linha; *s; s++) if (!isspace((unsigned char)*s)) vazio = 0;
        if (!vazio) {
            char *uma = xstrdup(linha);
            for (char *s = uma; *s; s++) if (*s == '\n') *s = ' ';
            ed_hist_add(ed, uma);
            FILE *h = fopen(arq_hist, "a");
            if (h) { fprintf(h, "%s\n", uma); fclose(h); }
            free(uma);
            char *copia = xstrdup(linha);
            if (!interno(copia, &ultimo, &sair, ed)) ultimo = rodar_sh(linha);
            free(copia);
        }
        free(linha);
    }
    modo_normal();
    ed_livrar(ed);
    free(arq_hist);
    return ultimo;
}

int main(int argc, char **argv) {
    RAIZ = raiz_tnux();
    if (argc >= 2) {
        const char *a = argv[1];
        if (!strcmp(a, "--sandbox") || !strcmp(a, "sandbox")) return sandbox_main(argc - 1, argv + 1);
        if (!strcmp(a, "desktop") || !strcmp(a, "area-de-trabalho")) return desktop(argc - 2, argv + 2);
        if (!strcmp(a, "apt") || !strcmp(a, "apt-get")) return apt_ponte(argc - 1, argv + 1);
        if (!strcmp(a, "instalar") || !strcmp(a, "install")) { argv[1] = "install"; return apt_ponte(argc, argv); }
        if (!strcmp(a, "remover") || !strcmp(a, "remove")) { argv[1] = "remove"; return apt_ponte(argc, argv); }
        if (!strcmp(a, "procurar") || !strcmp(a, "search")) { argv[1] = "search"; return apt_ponte(argc, argv); }
        if (!strcmp(a, "fetch") || !strcmp(a, "neofetch")) { banner(); return 0; }
        if (!strcmp(a, "versao") || !strcmp(a, "--version") || !strcmp(a, "-v")) { printf("%s %s (%s)\n", TNUX_NOME, TNUX_VERSAO, TNUX_CODINOME); return 0; }
        if (!strcmp(a, "ajuda") || !strcmp(a, "help") || !strcmp(a, "--help") || !strcmp(a, "-h")) { ajuda(); return 0; }
        if (!strcmp(a, "-c") && argc >= 3) {
            int cod = 0, sair = 0, pausa = argc >= 4 && !strcmp(argv[3], "--pausa");
            char *copia = xstrdup(argv[2]);
            if (!interno(copia, &cod, &sair, NULL)) cod = rodar_sh(argv[2]);
            free(copia);
            if (pausa) {
                printf("\n\x1b[1m[%s] Pressione Enter para fechar.\x1b[0m", cod ? "terminou com erro" : "concluído");
                fflush(stdout);
                int ch;
                while ((ch = getchar()) != EOF && ch != '\n') {}
            }
            return cod;
        }
        fprintf(stderr, "tnux: opção desconhecida: %s (veja: tnux ajuda)\n", a);
        return 2;
    }
    return shell();
}
