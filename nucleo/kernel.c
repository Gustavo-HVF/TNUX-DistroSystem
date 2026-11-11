/*
 * TNUX: kernel: boot, árvore do sistema, relógio e sessões de terminal.
 */
#include "cmds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

Sistema sis;

const Cmd *const cmds_todas[] = { cmds_shell, cmds_arq, cmds_txt, cmds_sis, cmds_rede, cmds_apt, NULL };

/* ------------------------------------------------------------------ relógio */

int64_t k_agora(void) { return (int64_t)(host_agora_ms() / 1000.0); }

/* converte segundos desde 1970 (UTC) na data local */
void k_data(int64_t t, int *ano, int *mes, int *dia, int *h, int *mi, int *s, int *dsem) {
    t += (int64_t)sis.fuso_min * 60;
    int64_t dias = t / 86400, resto = t % 86400;
    if (resto < 0) { resto += 86400; dias--; }
    *h = (int)(resto / 3600); *mi = (int)(resto % 3600 / 60); *s = (int)(resto % 60);
    *dsem = (int)(((dias % 7) + 11) % 7);          /* 0 = domingo */
    /* algoritmo de datas civis de Howard Hinnant */
    int64_t z = dias + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *dia = (int)(doy - (153 * mp + 2) / 5 + 1);
    *mes = (int)(mp < 10 ? mp + 3 : mp - 9);
    *ano = (int)(y + (*mes <= 2));
}

char *k_tamanho_humano(double b, char *dst, size_t n) {
    static const char *const u[] = { "", "K", "M", "G", "T" };
    int i = 0;
    while (b >= 1024 && i < 4) { b /= 1024; i++; }
    if (i == 0) snprintf(dst, n, "%d", (int)b);
    else if (b < 10) {
        int dec = (int)(b * 10 + 0.5);
        snprintf(dst, n, "%d,%d%s", dec / 10, dec % 10, u[i]);
    } else snprintf(dst, n, "%d%s", (int)(b + 0.5), u[i]);
    return dst;
}

void k_log(const char *fmt, ...) {
    double t = (host_agora_ms() - sis.boot_ms) / 1000.0;
    buf_printf(&sis.dmesg, "[%10.6f] ", t < 0 ? 0 : t);
    va_list ap;
    va_start(ap, fmt);
    buf_vprintf(&sis.dmesg, fmt, ap);
    va_end(ap);
    buf_putc(&sis.dmesg, '\n');
}

/* ------------------------------------------------------------------ árvore do sistema */

static const char BASHRC[] =
    "# ~/.bashrc: lido toda vez que um terminal abre.\n"
    "# Coloque aqui seus apelidos e variáveis.\n"
    "\n"
    "alias ls='ls --color=auto'\n"
    "alias grep='grep --color=auto'\n"
    "alias cls='clear'\n"
    "alias atualizar='sudo apt update && sudo apt upgrade'\n";

static const char LEIAME[] =
    "Bem-vindo ao TNUX!\n"
    "==================\n"
    "\n"
    "O TNUX é um sistema no estilo do Linux Mint que roda dentro do navegador\n"
    "ou no terminal do seu aparelho (Termux, Linux).\n"
    "\n"
    "Para começar, abra o Terminal e experimente:\n"
    "\n"
    "  help                      lista todos os comandos\n"
    "  neofetch                  mostra as informações do sistema\n"
    "  apt search jogo           procura programas para instalar\n"
    "  sudo apt install NOME     instala um programa\n"
    "  git clone https://github.com/USUARIO/REPOSITORIO\n"
    "                            baixa um repositório do GitHub\n"
    "  tnux ajuda                explica os dois modos (navegador e ponte)\n"
    "\n"
    "Tudo o que você grava fica guardado no navegador e volta quando você\n"
    "abrir o TNUX de novo.\n";

static const char EXEMPLO_SH[] =
    "#!/bin/bash\n"
    "# Um script de exemplo. Rode com:  ./exemplo.sh  ou  bash exemplo.sh\n"
    "\n"
    "nome=${1:-mundo}\n"
    "echo \"Olá, $nome! Este é o $(uname -o) $(uname -r).\"\n"
    "\n"
    "for i in 1 2 3; do\n"
    "    echo \"contando: $i\"\n"
    "done\n"
    "\n"
    "if [ -d \"$HOME/Downloads\" ]; then\n"
    "    echo \"Você tem $(ls \"$HOME/Downloads\" | wc -l) arquivo(s) em Downloads.\"\n"
    "fi\n";

void k_montar_base(void) {
    static const char *const dirs[] = {
        "/usr/bin", "/usr/sbin", "/usr/lib", "/usr/local/bin", "/usr/games", "/usr/share/applications",
        "/usr/share/tnux", "/usr/share/doc", "/etc/apt", "/etc/tnux", "/var/lib/dpkg", "/var/lib/apt/lists",
        "/var/log", "/var/cache/apt/archives", "/opt", "/mnt", "/media", "/srv", "/run", "/proc", "/dev", "/sys", "/boot", NULL
    };
    for (int i = 0; dirs[i]; i++) vfs_sis_dir(dirs[i], 0755, 0);
    vfs_sis_dir("/tmp", 01777, 0);
    vfs_sis_dir("/root", 0700, 0);
    vfs_sis_dir("/home", 0755, 0);

    /* como no Mint atual: /bin, /sbin e /lib são links para /usr */
    uint32_t e = vfs_euid;
    vfs_euid = 0;
    if (!vfs_achar("/", "/bin", 0)) vfs_link("/", "usr/bin", "/bin");
    if (!vfs_achar("/", "/sbin", 0)) vfs_link("/", "usr/sbin", "/sbin");
    if (!vfs_achar("/", "/lib", 0)) vfs_link("/", "usr/lib", "/lib");
    vfs_euid = e;

    /* cada comando do sistema aparece em /usr/bin como um arquivo executável */
    for (int t = 0; cmds_todas[t]; t++) {
        for (const Cmd *c = cmds_todas[t]; c->nome; c++) {
            if (c->nome[0] == '.' || c->nome[0] == ':' || c->nome[0] == '-') continue;
            char cam[96], txt[96];
            snprintf(cam, sizeof cam, "/usr/bin/%s", c->nome);
            snprintf(txt, sizeof txt, "#!tnux %s\n", c->nome);
            vfs_sis_arq(cam, txt, 0755, 0);
        }
    }

    vfs_sis_arq("/etc/os-release",
        "NAME=\"" TNUX_NOME "\"\nVERSION=\"" TNUX_VERSAO " (" TNUX_CODINOME ")\"\nID=tnux\nID_LIKE=\"linuxmint ubuntu debian\"\n"
        "PRETTY_NAME=\"" TNUX_NOME " " TNUX_VERSAO " " TNUX_CODINOME "\"\nVERSION_ID=\"" TNUX_VERSAO "\"\n"
        "HOME_URL=\"https://github.com/Gustavo-HVF/TNUX-DistroSystem\"\nVERSION_CODENAME=aurora\n", 0644, 0);
    vfs_sis_arq("/etc/lsb-release",
        "DISTRIB_ID=" TNUX_NOME "\nDISTRIB_RELEASE=" TNUX_VERSAO "\nDISTRIB_CODENAME=aurora\nDISTRIB_DESCRIPTION=\"" TNUX_NOME " " TNUX_VERSAO " " TNUX_CODINOME "\"\n", 0644, 0);
    vfs_sis_arq("/etc/tnux/info", "RELEASE=" TNUX_VERSAO "\nCODENAME=aurora\nEDITION=\"Navegador\"\nDESCRIPTION=\"" TNUX_NOME " " TNUX_VERSAO " " TNUX_CODINOME "\"\nBASE=\"" TNUX_BASE "\"\n", 0644, 0);
    vfs_sis_arq("/etc/issue", TNUX_NOME " " TNUX_VERSAO " " TNUX_CODINOME " \\n \\l\n", 0644, 0);
    char *hn = xasprintf("%s\n", sis.host ? sis.host : "tnux");
    vfs_sis_arq("/etc/hostname", hn, 0644, 0);
    free(hn);
    vfs_sis_arq("/etc/hosts", "127.0.0.1\tlocalhost\n127.0.1.1\ttnux\n", 0644, 0);
    vfs_sis_arq("/etc/passwd", "root:x:0:0:root:/root:/bin/bash\n" TNUX_USUARIO ":x:1000:1000:Usuário,,,:" TNUX_CASA ":/bin/bash\n", 0644, 0);
    vfs_sis_arq("/etc/group", "root:x:0:\nsudo:x:27:" TNUX_USUARIO "\n" TNUX_USUARIO ":x:1000:\n", 0644, 0);
    vfs_sis_arq("/etc/shells", "/bin/sh\n/bin/bash\n", 0644, 0);
    vfs_sis_arq("/etc/apt/sources.list",
        "# Fontes de programas do TNUX\n"
        "# O catálogo vem do próprio TNUX; os repositórios do GitHub são baixados direto de lá.\n"
        "deb https://github.com/Gustavo-HVF/TNUX-DistroSystem aurora main\n", 0644, 0);
    vfs_sis_arq("/usr/share/doc/LEIA-ME.txt", LEIAME, 0644, 0);

    /* /proc: um retrato do aparelho na hora do boot */
    Buf b; buf_init(&b);
    buf_printf(&b, "Linux version %s (tnux@build) (clang, WebAssembly) #1 SMP %s\n", TNUX_KERNEL, sis.nativo ? "nativo" : "wasm32");
    vfs_sis_arq("/proc/version", buf_str(&b), 0444, 0);
    b.n = 0;
    for (int i = 0; i < (sis.cpus > 0 ? sis.cpus : 1); i++)
        buf_printf(&b, "processor\t: %d\nmodel name\t: %s\ncpu cores\t: %d\n\n", i, sis.nativo ? "CPU do aparelho" : "CPU virtual WebAssembly (wasm32)", sis.cpus);
    vfs_sis_arq("/proc/cpuinfo", buf_str(&b), 0444, 0);
    b.n = 0;
    buf_printf(&b, "MemTotal:       %ld kB\n", sis.mem_mb * 1024);
    vfs_sis_arq("/proc/meminfo", buf_str(&b), 0444, 0);
    buf_free(&b);

    /* a pasta pessoal só é criada na primeira vez: depois é do usuário */
    if (!vfs_achar("/", TNUX_CASA, 1)) {
        static const char *const pastas[] = { "Área de Trabalho", "Documentos", "Downloads", "Imagens", "Modelos", "Música", "Público", "Vídeos", ".config", ".local/bin", ".local/share", NULL };
        vfs_sis_dir(TNUX_CASA, 0755, 1000);
        for (int i = 0; pastas[i]; i++) {
            char cam[128];
            snprintf(cam, sizeof cam, TNUX_CASA "/%s", pastas[i]);
            vfs_sis_dir(cam, 0755, 1000);
        }
        vfs_sis_dir(TNUX_CASA "/.local", 0755, 1000);
        vfs_sis_arq(TNUX_CASA "/.bashrc", BASHRC, 0644, 1000);
        vfs_sis_arq(TNUX_CASA "/.profile", "# ~/.profile\n[ -f ~/.bashrc ] && . ~/.bashrc\n", 0644, 1000);
        vfs_sis_arq(TNUX_CASA "/Documentos/Leia-me.txt", LEIAME, 0644, 1000);
        vfs_sis_arq(TNUX_CASA "/Documentos/exemplo.sh", EXEMPLO_SH, 0755, 1000);
    }
    vfs_sis_arq("/etc/tnux/versao", TNUX_VERSAO "\n", 0644, 0);
    apt_instalar_base();
}

/* ------------------------------------------------------------------ boot */

void k_boot(const char *cfg, size_t ncfg, const char *imagem, size_t tam_imagem) {
    memset(&sis, 0, sizeof sis);
    buf_init(&sis.dmesg);
    sis.boot_ms = host_agora_ms();
    JVal *j = cfg && ncfg ? json_ler(cfg, ncfg) : NULL;
    sis.cpus = (int)json_numero(j, "cpus", 1);
    sis.mem_mb = (long)json_numero(j, "mem_mb", 1024);
    sis.agente = xstrdup(json_texto(j, "agente", "desconhecido"));
    sis.plataforma = xstrdup(json_texto(j, "plataforma", "desconhecida"));
    sis.idioma = xstrdup(json_texto(j, "idioma", "pt-BR"));
    sis.host = xstrdup(json_texto(j, "host", "tnux"));
    sis.fuso_min = (int)json_numero(j, "fuso_min", -180);
    sis.nativo = (int)json_numero(j, "nativo", 0);

    k_log("Linux version %s (%s)", TNUX_KERNEL, sis.nativo ? "nativo" : "wasm32");
    k_log("Command line: BOOT_IMAGE=/boot/tnux.wasm root=/dev/tnux0 ro quiet splash");
    k_log("smpboot: %d processador(es) disponível(is)", sis.cpus);
    k_log("Memory: %ldM disponível", sis.mem_mb);
    int carregou = 0;
    vfs_iniciar();
    if (imagem && tam_imagem) {
        carregou = vfs_imagem_carregar(imagem, tam_imagem);
        k_log("tnuxfs: imagem do disco (%lu bytes): %s", (unsigned long)tam_imagem, carregou ? "montada" : "inválida, criando um disco novo");
    }
    if (!carregou) k_log("tnuxfs: primeiro boot, criando o sistema de arquivos");
    k_log("VFS: Mounted root (tnuxfs filesystem) on device 0:1");
    k_montar_base();
    /* o catálogo que veio junto com a página é sempre o mais novo */
    const char *cat = json_texto(j, "catalogo", NULL);
    if (cat) vfs_sis_arq("/var/lib/apt/lists/catalogo.json", cat, 0644, 0);
    k_log("systemd[1]: Reached target Graphical Interface.");
    k_log("tnux: %s %s (%s) pronto", TNUX_NOME, TNUX_VERSAO, TNUX_CODINOME);
    vfs_sis_arq("/var/log/dmesg", buf_str(&sis.dmesg), 0644, 0);
    json_livrar(j);
    if (!carregou) vfs_sujo = 1;
}

/* ------------------------------------------------------------------ sessões (um terminal = um shell) */

#define MAX_SESSOES 32
static Shell *sessoes[MAX_SESSOES];

static void ed_escreve(void *quem, const char *p, size_t n) { host_write(((Shell *)quem)->sid, p, n); }

static void novo_prompt(Shell *sh) {
    if (sh->pendente.n) { ed_prompt(sh->ed, "> "); return; }
    char *p = sh_prompt(sh);
    ed_prompt(sh->ed, p);
    free(p);
}

Shell *k_sessao(int sid) {
    for (int i = 0; i < MAX_SESSOES; i++) if (sessoes[i] && sessoes[i]->sid == sid) return sessoes[i];
    return NULL;
}

Shell *k_abrir_sessao(int sid, int cols, int rows, int interativa) {
    Shell *sh = k_sessao(sid);
    if (sh) return sh;
    int vaga = -1;
    for (int i = 0; i < MAX_SESSOES; i++) if (!sessoes[i]) { vaga = i; break; }
    if (vaga < 0) return NULL;
    sh = sh_novo(sid);
    sessoes[vaga] = sh;
    sh->cols = cols > 0 ? cols : 80;
    sh->rows = rows > 0 ? rows : 24;
    sh->interativo = interativa;
    if (!interativa) return sh;
    sh->ed = ed_novo(ed_escreve, sh_completar, sh);
    ed_colunas(sh->ed, sh->cols);
    /* histórico e ~/.bashrc */
    Inode *h = vfs_achar("/", TNUX_CASA "/.bash_history", 1);
    if (h && h->dados) {
        const char *p = h->dados, *fim = h->dados + h->tam, *l; size_t tl;
        while ((l = prox_linha(&p, fim, &tl))) {
            char *linha = xasprintf("%.*s", (int)tl, l);
            ed_hist_add(sh->ed, linha);
            free(linha);
        }
    }
    Out o = { NULL, sid };
    if (vfs_achar("/", TNUX_CASA "/.bashrc", 1)) sh_rodar(sh, "source ~/.bashrc", NULL, 0, &o, &o);
    sh->ultimo = 0;
    novo_prompt(sh);
    return sh;
}

void k_fechar_sessao(int sid) {
    for (int i = 0; i < MAX_SESSOES; i++)
        if (sessoes[i] && sessoes[i]->sid == sid) { sh_livrar(sessoes[i]); sessoes[i] = NULL; }
}

void k_redimensionar(int sid, int cols, int rows) {
    Shell *sh = k_sessao(sid);
    if (!sh) return;
    sh->cols = cols > 0 ? cols : 80;
    sh->rows = rows > 0 ? rows : 24;
    if (sh->ed) ed_colunas(sh->ed, sh->cols);
}

static void rodar_linha(Shell *sh, const char *linha) {
    Out o = { NULL, sh->sid };
    /* !! repete o último comando */
    char *texto = NULL;
    const char *bb = strstr(linha, "!!");
    if (bb && ed_hist_n(sh->ed) > 0) {
        const char *ult = ed_hist(sh->ed, ed_hist_n(sh->ed) - 1);
        Buf b; buf_init(&b);
        for (const char *p = linha; *p;) {
            if (p[0] == '!' && p[1] == '!') { buf_puts(&b, ult); p += 2; }
            else buf_putc(&b, *p++);
        }
        texto = buf_str(&b);
        o_printf(&o, "%s\n", texto);
    }
    const char *cmd = texto ? texto : linha;
    /* guarda no histórico (uma linha só) */
    int so_espaco = 1;
    for (const char *p = cmd; *p; p++) if (*p != ' ' && *p != '\n' && *p != '\t') so_espaco = 0;
    if (!so_espaco) {
        char *h = xstrdup(cmd);
        size_t n = strlen(h);
        while (n && h[n - 1] == '\n') h[--n] = 0;
        /* comando de várias linhas vira uma linha só no histórico: "for i in a b; do ...; done" */
        if (strchr(h, '\n')) {
            Buf j; buf_init(&j);
            for (char *p = h; *p;) {
                char *f = strchr(p, '\n');
                size_t l = f ? (size_t)(f - p) : strlen(p);
                while (l && (p[l - 1] == ' ' || p[l - 1] == '\t')) l--;
                buf_put(&j, p, l);
                if (!f) break;
                static const char *const sem_pv[] = { "do", "then", "else", "{", "(", "|", "&&", "||", ";", "in", NULL };
                int precisa = l > 0;
                for (int q = 0; sem_pv[q] && precisa; q++) {
                    size_t ls = strlen(sem_pv[q]);
                    if (l >= ls && !strncmp(p + l - ls, sem_pv[q], ls) && (l == ls || !isalnum((unsigned char)p[l - ls - 1]) || !isalpha((unsigned char)sem_pv[q][0]))) precisa = 0;
                }
                buf_puts(&j, precisa ? "; " : " ");
                p = f + 1;
            }
            free(h);
            h = buf_str(&j);
            n = strlen(h);
        }
        ed_hist_add(sh->ed, h);
        uint32_t e = vfs_euid;
        vfs_euid = 1000;
        Inode *hist = vfs_criar("/", TNUX_CASA "/.bash_history", 0600);
        if (hist) {
            if (hist->tam > 200000) vfs_gravar_no(hist, "", 0, 0);
            vfs_gravar_no(hist, h, n, 1);
            vfs_gravar_no(hist, "\n", 1, 1);
        }
        vfs_euid = e;
        free(h);
        sh_limpar(sh);
        sh_rodar(sh, cmd, NULL, 0, &o, &o);
    }
    free(texto);
}

void k_entrada(int sid, const char *p, size_t n) {
    Shell *sh = k_sessao(sid);
    if (!sh || !sh->ed) return;
    for (size_t i = 0; i < n; i++) {
        int ev = ed_byte(sh->ed, (unsigned char)p[i]);
        if (ev == ED_NADA) continue;
        if (ev == ED_INTERROMPE) { sh->pendente.n = 0; sh->ultimo = 130; novo_prompt(sh); continue; }
        if (ev == ED_EOF) {
            host_write(sid, "exit\r\n", 6);
            sh->sair = 1;
        } else {
            buf_puts(&sh->pendente, ed_linha(sh->ed));
            buf_putc(&sh->pendente, '\n');
            if (sh_incompleto(buf_str(&sh->pendente))) { novo_prompt(sh); continue; }
            char *texto = xstrdup(buf_str(&sh->pendente));
            sh->pendente.n = 0;
            rodar_linha(sh, texto);
            free(texto);
        }
        if (sh->sair) {
            char msg[64];
            snprintf(msg, sizeof msg, "{\"acao\":\"fechar_terminal\",\"sid\":%d}", sid);
            k_fechar_sessao(sid);
            host_gui(msg);
            return;
        }
        novo_prompt(sh);
    }
}
