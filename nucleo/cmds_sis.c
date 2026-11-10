/*
 * TNUX: comandos de sistema (uname, date, neofetch, free, ps, env...) e a ligação do terminal com
 * a área de trabalho (xdg-open, nano/xed, nemo, firefox...).
 */
#include "cmds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *const DIAS[7] = { "dom", "seg", "ter", "qua", "qui", "sex", "sáb" };
static const char *const DIAS_LONGO[7] = { "domingo", "segunda", "terça", "quarta", "quinta", "sexta", "sábado" };
static const char *const MESES_LONGO[12] = { "janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho", "agosto", "setembro", "outubro", "novembro", "dezembro" };

static const char *arq_maquina(void) { return sis.nativo ? "nativo" : "wasm32"; }

static int c_uname(Ctx *c, int argc, char **argv) {
    int s = 0, n = 0, r = 0, v = 0, m = 0, o = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') continue;
        for (const char *p = argv[i] + 1; *p; p++) {
            switch (*p) {
            case 'a': s = n = r = v = m = o = 1; break;
            case 's': s = 1; break; case 'n': n = 1; break; case 'r': r = 1; break;
            case 'v': v = 1; break; case 'm': case 'p': case 'i': m = 1; break; case 'o': o = 1; break;
            }
        }
    }
    if (!(s || n || r || v || m || o)) s = 1;
    Buf b; buf_init(&b);
#define P(x) do { if (b.n) buf_putc(&b, ' '); buf_puts(&b, x); } while (0)
    if (s) P("Linux");
    if (n) P(sis.host);
    if (r) P(TNUX_KERNEL);
    if (v) P("#1 SMP TNUX " TNUX_VERSAO);
    if (m) P(arq_maquina());
    if (o) P("TNUX/Linux");
#undef P
    OUT("%s\n", buf_str(&b));
    buf_free(&b);
    return 0;
}
static int c_whoami(Ctx *c, int argc, char **argv) { (void)argc; (void)argv; OUT("%s\n", vfs_euid == 0 ? "root" : TNUX_USUARIO); return 0; }
static int c_id(Ctx *c, int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "-u")) { OUT("%u\n", vfs_euid); return 0; }
    if (argc > 1 && !strcmp(argv[1], "-un")) { OUT("%s\n", vfs_euid == 0 ? "root" : TNUX_USUARIO); return 0; }
    if (vfs_euid == 0) OUT("uid=0(root) gid=0(root) grupos=0(root)\n");
    else OUT("uid=1000(%s) gid=1000(%s) grupos=1000(%s),27(sudo)\n", TNUX_USUARIO, TNUX_USUARIO, TNUX_USUARIO);
    return 0;
}
static int c_groups(Ctx *c, int argc, char **argv) { (void)argc; (void)argv; OUT(vfs_euid == 0 ? "root\n" : TNUX_USUARIO " sudo\n"); return 0; }
static int c_hostname(Ctx *c, int argc, char **argv) { (void)argc; (void)argv; OUT("%s\n", sis.host); return 0; }
static int c_nproc(Ctx *c, int argc, char **argv) { (void)argc; (void)argv; OUT("%d\n", sis.cpus); return 0; }
static int c_arch(Ctx *c, int argc, char **argv) { (void)argc; (void)argv; OUT("%s\n", arq_maquina()); return 0; }
static int c_tty(Ctx *c, int argc, char **argv) { (void)argc; (void)argv; OUT("/dev/pts/%d\n", c->sh->sid); return 0; }

static int c_date(Ctx *c, int argc, char **argv) {
    int a, me, d, h, mi, s, ds;
    int64_t t = k_agora();
    int salvo = sis.fuso_min, utc = 0;
    const char *fmt = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-u")) utc = 1;
        else if (argv[i][0] == '+') fmt = argv[i] + 1;
    }
    if (utc) sis.fuso_min = 0;
    k_data(t, &a, &me, &d, &h, &mi, &s, &ds);
    int fuso = sis.fuso_min;
    sis.fuso_min = salvo;
    char tz[16];
    snprintf(tz, sizeof tz, "%c%02d", fuso < 0 ? '-' : '+', abs(fuso) / 60);
    if (!fmt) {
        OUT("%s %02d %s %d %02d:%02d:%02d %s\n", DIAS[ds], d, MESES[me - 1], a, h, mi, s, utc ? "UTC" : tz);
        return 0;
    }
    Buf b; buf_init(&b);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%' || !p[1]) { buf_putc(&b, *p); continue; }
        p++;
        switch (*p) {
        case 'Y': buf_printf(&b, "%d", a); break;
        case 'y': buf_printf(&b, "%02d", a % 100); break;
        case 'm': buf_printf(&b, "%02d", me); break;
        case 'd': buf_printf(&b, "%02d", d); break;
        case 'e': buf_printf(&b, "%2d", d); break;
        case 'H': buf_printf(&b, "%02d", h); break;
        case 'I': buf_printf(&b, "%02d", h % 12 ? h % 12 : 12); break;
        case 'M': buf_printf(&b, "%02d", mi); break;
        case 'S': buf_printf(&b, "%02d", s); break;
        case 'p': buf_puts(&b, h < 12 ? "AM" : "PM"); break;
        case 's': buf_printf(&b, "%lld", (long long)t); break;
        case 'a': buf_puts(&b, DIAS[ds]); break;
        case 'A': buf_puts(&b, DIAS_LONGO[ds]); break;
        case 'b': case 'h': buf_puts(&b, MESES[me - 1]); break;
        case 'B': buf_puts(&b, MESES_LONGO[me - 1]); break;
        case 'u': buf_printf(&b, "%d", ds ? ds : 7); break;
        case 'w': buf_printf(&b, "%d", ds); break;
        case 'F': buf_printf(&b, "%d-%02d-%02d", a, me, d); break;
        case 'T': buf_printf(&b, "%02d:%02d:%02d", h, mi, s); break;
        case 'R': buf_printf(&b, "%02d:%02d", h, mi); break;
        case 'D': buf_printf(&b, "%02d/%02d/%02d", me, d, a % 100); break;
        case 'Z': buf_puts(&b, utc ? "UTC" : tz); break;
        case 'z': buf_printf(&b, "%c%02d%02d", fuso < 0 ? '-' : '+', abs(fuso) / 60, abs(fuso) % 60); break;
        case 'n': buf_putc(&b, '\n'); break;
        case 't': buf_putc(&b, '\t'); break;
        case '%': buf_putc(&b, '%'); break;
        default: buf_putc(&b, '%'); buf_putc(&b, *p);
        }
    }
    OUT("%s\n", buf_str(&b));
    buf_free(&b);
    return 0;
}

static int dias_no_mes(int a, int m) {
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((a % 4 == 0 && a % 100 != 0) || a % 400 == 0)) return 29;
    return d[m - 1];
}
static int c_cal(Ctx *c, int argc, char **argv) {
    int a, me, d, h, mi, s, ds;
    k_data(k_agora(), &a, &me, &d, &h, &mi, &s, &ds);
    int hoje = d, mes_hoje = me, ano_hoje = a;
    if (argc == 3) { me = atoi(argv[1]); a = atoi(argv[2]); }
    else if (argc == 2) a = atoi(argv[1]);
    if (me < 1 || me > 12 || a < 1) { ERR("cal: data inválida\n"); return 1; }
    /* dia da semana do dia 1 (congruência de Zeller, 0 = domingo) */
    int y = a, m = me;
    if (m < 3) { m += 12; y--; }
    int prim = (1 + 13 * (m + 1) / 5 + y % 100 + y % 100 / 4 + y / 100 / 4 + 5 * (y / 100)) % 7;
    prim = (prim + 6) % 7;
    char tit[48];
    snprintf(tit, sizeof tit, "%s %d", MESES_LONGO[me - 1], a);
    int lt = 0;
    for (const char *p = tit; *p; p++) if (((unsigned char)*p & 0xC0) != 0x80) lt++;
    OUT("%*s%s\n", (20 - lt) / 2, "", tit);
    OUT("do se te qu qu se sá\n");
    for (int i = 0; i < prim; i++) OUT("   ");
    int n = dias_no_mes(a, me);
    for (int dia = 1; dia <= n; dia++) {
        int marca = o_tty(c->out) && dia == hoje && me == mes_hoje && a == ano_hoje;
        OUT(marca ? "\x1b[7m%2d\x1b[0m" : "%2d", dia);
        OUT((prim + dia) % 7 == 0 || dia == n ? "\n" : " ");
    }
    return 0;
}

static void tempo_ligado(Buf *b) {
    long seg = (long)((host_agora_ms() - sis.boot_ms) / 1000.0);
    long dias = seg / 86400, h = seg % 86400 / 3600, m = seg % 3600 / 60;
    if (dias) buf_printf(b, "%ld dia%s, ", dias, dias == 1 ? "" : "s");
    if (h) buf_printf(b, "%ld hora%s, ", h, h == 1 ? "" : "s");
    buf_printf(b, "%ld min", m);
}
static int c_uptime(Ctx *c, int argc, char **argv) {
    (void)argc; (void)argv;
    int a, me, d, h, mi, s, ds;
    k_data(k_agora(), &a, &me, &d, &h, &mi, &s, &ds);
    Buf b; buf_init(&b);
    tempo_ligado(&b);
    OUT(" %02d:%02d:%02d ligado há %s,  1 usuário,  carga média: 0,00, 0,00, 0,00\n", h, mi, s, buf_str(&b));
    buf_free(&b);
    return 0;
}

static void memoria(double *total, double *usada) {
    size_t arqs = 0;
    *usada = (double)vfs_tamanho_total(vfs_raiz, &arqs) + arqs * 128.0 + 4.0 * 1048576;
    *total = (double)sis.mem_mb * 1048576.0;
    if (*total < *usada) *total = *usada * 2;
}
static int c_free(Ctx *c, int argc, char **argv) {
    int humano = argc > 1 && strchr(argv[1], 'h'), mega = argc > 1 && strchr(argv[1], 'm');
    double tot, us;
    memoria(&tot, &us);
    OUT("               total       usada       livre    compart.  buff/cache  disponível\n");
    if (humano) {
        char a[16], b[16], d[16];
        k_tamanho_humano(tot, a, sizeof a); k_tamanho_humano(us, b, sizeof b); k_tamanho_humano(tot - us, d, sizeof d);
        OUT("Mem.:    %11s %11s %11s %11s %11s %11s\n", a, b, d, "0", "0", d);
        OUT("Swap:    %11s %11s %11s\n", "0", "0", "0");
    } else {
        double div = mega ? 1048576.0 : 1024.0;
        OUT("Mem.:    %11.0f %11.0f %11.0f %11d %11d %11.0f\n", tot / div, us / div, (tot - us) / div, 0, 0, (tot - us) / div);
        OUT("Swap:    %11d %11d %11d\n", 0, 0, 0);
    }
    return 0;
}
static int c_ps(Ctx *c, int argc, char **argv) {
    int todos = argc > 1;
    if (todos) {
        OUT("USER         PID %%CPU %%MEM    VSZ   RSS TTY      STAT START   TIME COMMAND\n");
        OUT("root           1  0.0  0.1  16000  4000 ?        Ss   boot   0:00 /sbin/init splash\n");
        OUT("root          42  0.0  0.1  12000  3000 ?        Ss   boot   0:00 tnux-kernel\n");
        OUT("%-10s   310  0.1  1.2 400000 90000 ?        Sl   boot   0:01 tnux-desktop\n", TNUX_USUARIO);
        for (int s = 1; s < 64; s++) {
            if (!k_sessao(s)) continue;
            OUT("%-10s  %4d  0.0  0.2  10000  5000 pts/%-4d Ss   boot   0:00 bash\n", TNUX_USUARIO, 1000 + s, s);
        }
        OUT("%-10s  %4d  0.0  0.0   9000  3000 pts/%-4d R+   agora  0:00 ps %s\n", TNUX_USUARIO, 2000 + c->sh->sid, c->sh->sid, argv[1]);
    } else {
        OUT("    PID TTY          TIME CMD\n");
        OUT("%7d pts/%-8d 00:00:00 bash\n", 1000 + c->sh->sid, c->sh->sid);
        OUT("%7d pts/%-8d 00:00:00 ps\n", 2000 + c->sh->sid, c->sh->sid);
    }
    return 0;
}
static int c_top(Ctx *c, int argc, char **argv) {
    (void)argc;
    double tot, us;
    memoria(&tot, &us);
    Buf b; buf_init(&b);
    tempo_ligado(&b);
    OUT("%s - ligado há %s, 1 usuário\n", argv[0], buf_str(&b));
    OUT("MiB Mem : %8.1f total, %8.1f livre, %8.1f usada\n\n", tot / 1048576, (tot - us) / 1048576, us / 1048576);
    buf_free(&b);
    char *a[] = { "ps", "aux", NULL };
    return c_ps(c, 2, a);
}
static int c_kill(Ctx *c, int argc, char **argv) {
    if (argc < 2) { ERR("kill: uso: kill [-s sinal] pid\n"); return 2; }
    ERR("%s: (%s) - Operação não permitida: os processos do TNUX no navegador são as janelas; feche a janela.\n", argv[0], argv[argc - 1]);
    return 1;
}
static int c_env(Ctx *c, int argc, char **argv) {
    Shell *sh = c->sh;
    if (argc > 1 && !strcmp(argv[0], "printenv")) {
        const char *v = sh_get(sh, argv[1]);
        if (!v) return 1;
        OUT("%s\n", v);
        return 0;
    }
    /* env VAR=valor comando: roda com a variável definida */
    int i = 1;
    while (i < argc && strchr(argv[i], '=') && argv[i][0] != '=') i++;
    if (i < argc) {
        Buf b; buf_init(&b);
        for (int k = 1; k < argc; k++) {
            if (k > 1) buf_putc(&b, ' ');
            if (k < i) { char *eq = strchr(argv[k], '='); buf_put(&b, argv[k], (size_t)(eq - argv[k] + 1)); buf_putc(&b, '\''); buf_puts(&b, eq + 1); buf_putc(&b, '\''); }
            else { buf_putc(&b, '\''); buf_puts(&b, argv[k]); buf_putc(&b, '\''); }
        }
        int r = sh_rodar(sh, buf_str(&b), c->in, c->inlen, c->out, c->err);
        buf_free(&b);
        return r;
    }
    for (int k = 0; k < sh->nvars; k++) if (sh->vars[k].exportada) OUT("%s=%s\n", sh->vars[k].k, sh->vars[k].v);
    for (int k = 1; k < argc; k++) OUT("%s\n", argv[k]);
    return 0;
}
static int c_sleep(Ctx *c, int argc, char **argv) {
    if (argc < 2) { ERR("sleep: falta operando\n"); return 1; }
    double s = strtod(argv[1], NULL);
    size_t l = strlen(argv[1]);
    if (l && argv[1][l - 1] == 'm') s *= 60;
    if (s > 3600) s = 3600;
    int ms = (int)(s * 1000);
    while (ms > 0 && !host_interrompido()) { int p = ms > 100 ? 100 : ms; host_dormir(p); ms -= p; }
    return 0;
}
static int c_clear(Ctx *c, int argc, char **argv) {
    (void)argc; (void)argv;
    if (o_tty(c->out)) OUT("\x1b[H\x1b[2J\x1b[3J");
    return 0;
}
static int c_time(Ctx *c, int argc, char **argv) {
    double t0 = host_agora_ms();
    int r = argc > 1 ? sh_exec_argv(c, argc - 1, argv + 1) : 0;
    double dt = (host_agora_ms() - t0) / 1000.0;
    ERR("\nreal\t%dm%.3fs\nuser\t0m%.3fs\nsys\t0m0,000s\n", (int)(dt / 60), dt - (int)(dt / 60) * 60, dt);
    return r;
}
static int c_expr(Ctx *c, int argc, char **argv) {
    Buf b; buf_init(&b);
    buf_puts(&b, "echo $((");
    for (int i = 1; i < argc; i++) { buf_putc(&b, ' '); buf_puts(&b, argv[i]); }
    buf_puts(&b, " ))");
    int r = sh_rodar(c->sh, buf_str(&b), NULL, 0, c->out, c->err);
    buf_free(&b);
    return r;
}

/* ------------------------------------------------------------------ neofetch e companhia */

static int conta_pacotes(void) {
    Inode *st = vfs_achar("/", "/var/lib/dpkg/status", 1);
    int n = 0;
    if (st && st->dados) for (const char *p = st->dados; (p = strstr(p, "Package: ")); p += 9) n++;
    return n;
}
static int c_neofetch(Ctx *c, int argc, char **argv) {
    (void)argc; (void)argv;
    static const char *const logo[] = {
        "  \x1b[1;32m.:::::::::::::::::::::::.\x1b[0m  ",
        " \x1b[1;32m:::::::::::::::::::::::::::\x1b[0m ",
        " \x1b[1;32m:::\x1b[1;37m#####################\x1b[1;32m:::\x1b[0m ",
        " \x1b[1;32m:::\x1b[1;37m#####################\x1b[1;32m:::\x1b[0m ",
        " \x1b[1;32m::::::::::\x1b[1;37m#######\x1b[1;32m::::::::::\x1b[0m ",
        " \x1b[1;32m::::::::::\x1b[1;37m#######\x1b[1;32m::::::::::\x1b[0m ",
        " \x1b[1;32m::::::::::\x1b[1;37m#######\x1b[1;32m::::::::::\x1b[0m ",
        " \x1b[1;32m::::::::::\x1b[1;37m#######\x1b[1;32m::::::::::\x1b[0m ",
        " \x1b[1;32m::::::::::\x1b[1;37m#######\x1b[1;32m::::::::::\x1b[0m ",
        " \x1b[1;32m::::::::::\x1b[1;37m#######\x1b[1;32m::::::::::\x1b[0m ",
        " \x1b[1;32m::::::::::\x1b[1;37m#######\x1b[1;32m::::::::::\x1b[0m ",
        " \x1b[1;32m:::::::::::::::::::::::::::\x1b[0m ",
        "  \x1b[1;32m':::::::::::::::::::::::'\x1b[0m  ",
    };
    char *info[16]; int n = 0;
    double tot, us;
    memoria(&tot, &us);
    Buf up; buf_init(&up);
    tempo_ligado(&up);
    info[n++] = xasprintf("\x1b[1;32m%s\x1b[0m@\x1b[1;32m%s\x1b[0m", vfs_euid == 0 ? "root" : TNUX_USUARIO, sis.host);
    info[n++] = xstrdup("-------------");
    info[n++] = xasprintf("\x1b[1;32mSO\x1b[0m: %s %s %s (%s)", TNUX_NOME, TNUX_VERSAO, TNUX_CODINOME, arq_maquina());
    info[n++] = xasprintf("\x1b[1;32mBase\x1b[0m: %s", TNUX_BASE);
    info[n++] = xasprintf("\x1b[1;32mAparelho\x1b[0m: %s", sis.plataforma);
    info[n++] = xasprintf("\x1b[1;32mKernel\x1b[0m: %s", TNUX_KERNEL);
    info[n++] = xasprintf("\x1b[1;32mLigado há\x1b[0m: %s", buf_str(&up));
    info[n++] = xasprintf("\x1b[1;32mPacotes\x1b[0m: %d (dpkg)", conta_pacotes());
    info[n++] = xstrdup("\x1b[1;32mShell\x1b[0m: bash (tnux)");
    info[n++] = xstrdup(sis.nativo ? "\x1b[1;32mInterface\x1b[0m: terminal" : "\x1b[1;32mInterface\x1b[0m: TNUX Desktop (estilo Cinnamon)");
    info[n++] = xasprintf("\x1b[1;32mCPU\x1b[0m: %d núcleo%s", sis.cpus, sis.cpus == 1 ? "" : "s");
    info[n++] = xasprintf("\x1b[1;32mMemória\x1b[0m: %.0fMiB / %.0fMiB", us / 1048576, tot / 1048576);
    info[n++] = xstrdup("");
    info[n++] = xstrdup("\x1b[40m   \x1b[41m   \x1b[42m   \x1b[43m   \x1b[44m   \x1b[45m   \x1b[46m   \x1b[47m   \x1b[0m");
    buf_free(&up);
    int nl = (int)(sizeof logo / sizeof logo[0]);
    int estreito = c->sh->cols < 70;
    if (estreito) {
        for (int i = 0; i < n; i++) OUT("%s\n", info[i]);
    } else {
        for (int i = 0; i < nl || i < n; i++)
            OUT("%s  %s\n", i < nl ? logo[i] : "                             ", i < n ? info[i] : "");
    }
    for (int i = 0; i < n; i++) free(info[i]);
    return 0;
}
static int c_lsb(Ctx *c, int argc, char **argv) {
    int tudo = argc < 2 || strchr(argv[1], 'a');
    if (tudo) OUT("No LSB modules are available.\n");
    if (tudo || strchr(argv[1], 'i')) OUT("Distributor ID:\t%s\n", TNUX_NOME);
    if (tudo || strchr(argv[1], 'd')) OUT("Description:\t%s %s %s\n", TNUX_NOME, TNUX_VERSAO, TNUX_CODINOME);
    if (tudo || strchr(argv[1], 'r')) OUT("Release:\t%s\n", TNUX_VERSAO);
    if (tudo || strchr(argv[1], 'c')) OUT("Codename:\taurora\n");
    return 0;
}
static int c_lscpu(Ctx *c, int argc, char **argv) {
    (void)argc; (void)argv;
    OUT("Arquitetura:             %s\n", arq_maquina());
    OUT("Modo(s) operacional da CPU: 32-bit\n");
    OUT("Ordem dos bytes:         Little Endian\n");
    OUT("CPU(s):                  %d\n", sis.cpus);
    OUT("Nome do modelo:          %s\n", sis.nativo ? "CPU do aparelho" : "CPU virtual WebAssembly");
    OUT("Aparelho:                %s\n", sis.agente);
    return 0;
}
static int c_dmesg(Ctx *c, int argc, char **argv) { (void)argc; (void)argv; o_write(c->out, sis.dmesg.d ? sis.dmesg.d : "", sis.dmesg.n); return 0; }

/* ------------------------------------------------------------------ área de trabalho */

static void gui(const char *acao, const char *app, const char *arg) {
    Buf b; buf_init(&b);
    buf_puts(&b, "{\"acao\":");
    buf_json_str(&b, acao);
    if (app) { buf_puts(&b, ",\"app\":"); buf_json_str(&b, app); }
    if (arg) { buf_puts(&b, ",\"arg\":"); buf_json_str(&b, arg); }
    buf_putc(&b, '}');
    host_gui(buf_str(&b));
    buf_free(&b);
}
static int eh_url(const char *s) { return !strncmp(s, "http://", 7) || !strncmp(s, "https://", 8) || !strncmp(s, "about:", 6); }
static int termina(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !strcasecmp(s + a - b, suf);
}
/* nano, xed, nemo, firefox, xdg-open...: abrem a janela correspondente */
static int c_gui(Ctx *c, int argc, char **argv) {
    const char *nome = argv[0];
    const char *arg = NULL;
    for (int i = 1; i < argc; i++) if (argv[i][0] != '-') { arg = argv[i]; break; }
    const char *app;
    if (!strcmp(nome, "nano") || !strcmp(nome, "xed") || !strcmp(nome, "gedit") || !strcmp(nome, "vi") || !strcmp(nome, "vim") || !strcmp(nome, "editor") || !strcmp(nome, "pluma")) app = "editor";
    else if (!strcmp(nome, "nemo") || !strcmp(nome, "nautilus") || !strcmp(nome, "thunar")) app = "arquivos";
    else if (!strcmp(nome, "firefox") || !strcmp(nome, "navegador") || !strcmp(nome, "x-www-browser")) app = "navegador";
    else if (!strcmp(nome, "gnome-terminal") || !strcmp(nome, "x-terminal-emulator")) app = "terminal";
    else if (!strcmp(nome, "mintinstall") || !strcmp(nome, "loja")) app = "loja";
    else if (!strcmp(nome, "cinnamon-settings") || !strcmp(nome, "configuracoes")) app = "config";
    else if (!strcmp(nome, "gnome-system-monitor") || !strcmp(nome, "monitor")) app = "monitor";
    else if (!strcmp(nome, "xviewer") || !strcmp(nome, "eog")) app = "imagem";
    else if (!strcmp(nome, "gnome-calculator") || !strcmp(nome, "calculadora")) app = "calculadora";
    else {      /* xdg-open, open: decide pelo que foi pedido */
        if (!arg) { ERR("uso: %s arquivo|pasta|URL\n", nome); return 1; }
        if (eh_url(arg)) app = "navegador";
        else {
            Inode *n = vfs_achar(c->sh->cwd, arg, 1);
            if (!n) { ERR("%s: %s: %s\n", nome, arg, vfs_erro(vfs_err)); return 2; }
            if (EH_DIR(n)) app = "arquivos";
            else if (termina(arg, ".html") || termina(arg, ".htm")) app = "web";
            else if (termina(arg, ".png") || termina(arg, ".jpg") || termina(arg, ".jpeg") || termina(arg, ".gif") || termina(arg, ".svg") || termina(arg, ".webp")) app = "imagem";
            else if (termina(arg, ".desktop")) app = "desktop";
            else app = "editor";
        }
    }
    char *abs = NULL;
    if (arg && !eh_url(arg)) {
        abs = vfs_abs(c->sh->cwd, arg);
        if (!strcmp(app, "arquivos") || !strcmp(app, "imagem") || !strcmp(app, "web")) {
            Inode *n = vfs_achar("/", abs, 1);
            if (!n) { ERR("%s: %s: %s\n", nome, arg, vfs_erro(vfs_err)); free(abs); return 2; }
        }
    }
    if (sis.nativo) OUT("[área de trabalho] abrir %s%s%s\n", app, arg ? ": " : "", arg ? (abs ? abs : arg) : "");
    gui("abrir", app, abs ? abs : arg);
    free(abs);
    return 0;
}
static int c_energia(Ctx *c, int argc, char **argv) {
    (void)argc;
    const char *tipo = (!strcmp(argv[0], "reboot")) ? "reiniciar" : "desligar";
    if (argc > 1 && !strcmp(argv[1], "-r")) tipo = "reiniciar";
    OUT("%s o sistema...\n", !strcmp(tipo, "reiniciar") ? "Reiniciando" : "Desligando");
    gui("energia", tipo, NULL);
    return 0;
}

/* ------------------------------------------------------------------ o comando tnux */

static int c_tnux(Ctx *c, int argc, char **argv) {
    const char *sub = argc > 1 ? argv[1] : "ajuda";
    if (!strcmp(sub, "versao") || !strcmp(sub, "--version") || !strcmp(sub, "-v")) {
        OUT("%s %s (%s), base %s, núcleo em C para %s\n", TNUX_NOME, TNUX_VERSAO, TNUX_CODINOME, TNUX_BASE, arq_maquina());
        return 0;
    }
    if (!strcmp(sub, "modo")) {
        OUT(sis.nativo ? "sandbox no terminal (sistema em memória; os comandos NÃO mexem no aparelho)\n"
                       : "navegador (sistema dentro da aba; os arquivos ficam guardados no navegador)\n");
        return 0;
    }
    if (!strcmp(sub, "salvar")) { gui("salvar", NULL, NULL); OUT("Disco gravado.\n"); return 0; }
    if (!strcmp(sub, "redefinir")) {
        if (argc < 3 || strcmp(argv[2], "--sim")) {
            OUT("Isto apaga TUDO (arquivos, programas instalados, configurações) e volta o TNUX ao estado de fábrica.\n"
                "Para confirmar: tnux redefinir --sim\n");
            return 1;
        }
        gui("redefinir", NULL, NULL);
        return 0;
    }
    if (!strcmp(sub, "instalar") || !strcmp(sub, "install")) {
        const Cmd *apt = cmd_achar("apt");
        argv[1] = "install";
        uint32_t e = vfs_euid;
        vfs_euid = 0;
        int r = apt->fn(c, argc, argv);
        vfs_euid = e;
        return r;
    }
    if (!strcmp(sub, "ajuda") || !strcmp(sub, "help") || !strcmp(sub, "--help")) {
        const char *tema = argc > 2 ? argv[2] : "";
        if (!strcmp(tema, "ponte")) {
            OUT("\x1b[1mModo ponte: o TNUX usando o seu aparelho de verdade\x1b[0m\n\n"
                "No navegador, sozinho, o TNUX roda numa caixa fechada: scripts, programas WebAssembly e apps web.\n"
                "Programas Linux nativos (Chrome, Firefox, GIMP, python, gcc, tudo do apt/pkg) precisam de um\n"
                "aparelho para rodar. É para isso que serve a ponte:\n\n"
                "  1. No Termux (Android) ou no terminal do Linux:\n"
                "       git clone https://github.com/Gustavo-HVF/TNUX-DistroSystem\n"
                "       cd TNUX-DistroSystem && bash instalar.sh\n"
                "  2. Rode:  tnux desktop\n"
                "  3. Abra no navegador o endereço que ele mostrar.\n\n"
                "Com a ponte, o Terminal desta área de trabalho é um terminal de verdade do aparelho, o apt\n"
                "instala os programas reais e o Gerenciador de Aplicativos passa a oferecer todos eles.\n");
            return 0;
        }
        OUT("\x1b[1;32m%s %s \"%s\"\x1b[0m: um sistema no estilo do %s.\n\n", TNUX_NOME, TNUX_VERSAO, TNUX_CODINOME, TNUX_BASE);
        OUT("\x1b[1mComandos do tnux\x1b[0m\n"
            "  tnux versao              versão do sistema\n"
            "  tnux modo                diz onde o sistema está rodando\n"
            "  tnux instalar NOME       instala um programa (igual a: sudo apt install NOME)\n"
            "  tnux salvar              grava o disco agora\n"
            "  tnux redefinir           volta ao estado de fábrica\n"
            "  tnux ajuda ponte         como rodar programas Linux de verdade\n\n"
            "\x1b[1mPara instalar programas\x1b[0m\n"
            "  apt search TERMO         procura no catálogo\n"
            "  sudo apt install NOME    instala\n"
            "  sudo apt install USUARIO/REPOSITORIO   instala direto do GitHub\n"
            "  git clone URL            baixa um repositório do GitHub\n\n"
            "Digite \x1b[1mhelp\x1b[0m para ver todos os comandos.\n");
        return 0;
    }
    ERR("tnux: subcomando desconhecido: %s (veja: tnux ajuda)\n", sub);
    return 1;
}
static int c_su(Ctx *c, int argc, char **argv) {
    (void)argc;
    ERR("%s: neste sistema o root só existe por comando: use \"sudo comando\".\n", argv[0]);
    return 1;
}

const Cmd cmds_sis[] = {
    { "uname", c_uname, "informações do sistema", "uname [-a] [-s] [-n] [-r] [-m] [-o]" },
    { "neofetch", c_neofetch, "resumo do sistema com o logotipo", "neofetch" },
    { "tnux", c_tnux, "comandos do próprio TNUX (tnux ajuda)", "tnux versao|modo|instalar|salvar|redefinir|ajuda [ponte]" },
    { "whoami", c_whoami, "mostra o usuário atual", "whoami" },
    { "id", c_id, "mostra usuário e grupos", "id" },
    { "groups", c_groups, NULL, NULL },
    { "hostname", c_hostname, "nome da máquina", "hostname" },
    { "date", c_date, "data e hora", "date [-u] [+formato]   ex.: date '+%d/%m/%Y %H:%M'" },
    { "cal", c_cal, "calendário do mês", "cal [mês ano]" },
    { "uptime", c_uptime, "há quanto tempo o sistema está ligado", "uptime" },
    { "free", c_free, "uso de memória", "free [-h] [-m]" },
    { "ps", c_ps, "lista os processos", "ps [aux]" },
    { "top", c_top, "processos e memória", "top" },
    { "htop", c_top, NULL, NULL },
    { "kill", c_kill, NULL, NULL },
    { "killall", c_kill, NULL, NULL },
    { "env", c_env, "mostra as variáveis de ambiente", "env" },
    { "printenv", c_env, NULL, NULL },
    { "sleep", c_sleep, "espera alguns segundos", "sleep segundos" },
    { "clear", c_clear, "limpa a tela", "clear" },
    { "reset", c_clear, NULL, NULL },
    { "time", c_time, "mede o tempo de um comando", "time comando" },
    { "expr", c_expr, NULL, NULL },
    { "lsb_release", c_lsb, "versão da distribuição", "lsb_release -a" },
    { "lscpu", c_lscpu, "informações do processador", "lscpu" },
    { "nproc", c_nproc, NULL, NULL },
    { "arch", c_arch, NULL, NULL },
    { "tty", c_tty, NULL, NULL },
    { "dmesg", c_dmesg, "mensagens do boot", "dmesg" },
    { "su", c_su, NULL, NULL },
    { "passwd", c_su, NULL, NULL },
    { "reboot", c_energia, "reinicia o sistema", "reboot" },
    { "poweroff", c_energia, "desliga o sistema", "poweroff" },
    { "shutdown", c_energia, NULL, NULL },
    { "xdg-open", c_gui, "abre um arquivo, pasta ou site no programa certo", "xdg-open arquivo|pasta|URL" },
    { "open", c_gui, NULL, NULL },
    { "nano", c_gui, "abre o editor de texto", "nano [arquivo]" },
    { "xed", c_gui, "abre o editor de texto", "xed [arquivo]" },
    { "gedit", c_gui, NULL, NULL },
    { "vi", c_gui, NULL, NULL },
    { "vim", c_gui, NULL, NULL },
    { "nemo", c_gui, "abre o gerenciador de arquivos", "nemo [pasta]" },
    { "firefox", c_gui, "abre o navegador", "firefox [URL]" },
    { "navegador", c_gui, NULL, NULL },
    { "x-www-browser", c_gui, NULL, NULL },
    { "gnome-terminal", c_gui, NULL, NULL },
    { "mintinstall", c_gui, "abre o Gerenciador de Aplicativos", "mintinstall" },
    { "cinnamon-settings", c_gui, "abre as Configurações", "cinnamon-settings" },
    { "gnome-system-monitor", c_gui, NULL, NULL },
    { "xviewer", c_gui, "abre o visualizador de imagens", "xviewer imagem" },
    { "gnome-calculator", c_gui, NULL, NULL },
    { NULL, NULL, NULL, NULL }
};
