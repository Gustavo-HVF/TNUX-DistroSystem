/*
 * TNUX: comandos de rede (curl, wget, git, ping, ip).
 *
 * No navegador a rede passa pelo próprio navegador (host_http), então vale a regra dele: só dá
 * para baixar de sites que permitem acesso de outras páginas (CORS). O GitHub permite, e é por
 * isso que "git clone" funciona: a lista de arquivos vem da API do GitHub e cada arquivo vem do
 * raw.githubusercontent.com.
 */
#include "cmds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char AVISO_CORS[] =
    "      No navegador só é possível baixar de sites que permitem acesso direto (CORS), como o GitHub.\n"
    "      Para baixar de qualquer lugar, use o modo ponte (veja: tnux ajuda ponte).\n";

static const char *nome_da_url(const char *url) {
    const char *q = strchr(url, '?');
    size_t n = q ? (size_t)(q - url) : strlen(url);
    while (n > 0 && url[n - 1] == '/') n--;
    const char *b = url + n;
    while (b > url && b[-1] != '/') b--;
    static char nome[256];
    size_t l = (size_t)(url + n - b);
    if (l == 0 || l >= sizeof nome || strstr(b, "://") || (b >= url + 2 && b[-1] == '/' && b[-2] == '/')) return "index.html";
    memcpy(nome, b, l); nome[l] = 0;
    return nome;
}

/* ------------------------------------------------------------------ curl */

static int c_curl(Ctx *c, int argc, char **argv) {
    const char *url = NULL, *saida = NULL, *metodo = NULL, *dados = NULL;
    int quieto = 0, so_cab = 0, nome_remoto = 0, falha_http = 0, mostra_cab = 0;
    Buf cab; buf_init(&cab);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-') { url = a; continue; }
        if (!strcmp(a, "-o") && i + 1 < argc) saida = argv[++i];
        else if ((!strcmp(a, "-X") || !strcmp(a, "--request")) && i + 1 < argc) metodo = argv[++i];
        else if ((!strcmp(a, "-H") || !strcmp(a, "--header")) && i + 1 < argc) { buf_puts(&cab, argv[++i]); buf_putc(&cab, '\n'); }
        else if ((!strcmp(a, "-d") || !strcmp(a, "--data") || !strcmp(a, "--data-raw")) && i + 1 < argc) dados = argv[++i];
        else if ((!strcmp(a, "-A") || !strcmp(a, "-u") || !strcmp(a, "-e") || !strcmp(a, "-m") || !strcmp(a, "--max-time") || !strcmp(a, "--connect-timeout") || !strcmp(a, "--retry")) && i + 1 < argc) i++;
        else if (!strcmp(a, "--silent")) quieto = 1;
        else if (!strcmp(a, "--head")) so_cab = 1;
        else if (!strcmp(a, "--fail")) falha_http = 1;
        else if (!strcmp(a, "--remote-name")) nome_remoto = 1;
        else if (!strcmp(a, "--include")) mostra_cab = 1;
        else if (a[1] == '-') {}
        else for (const char *p = a + 1; *p; p++) {
            if (*p == 's') quieto = 1; else if (*p == 'I') so_cab = 1; else if (*p == 'O') nome_remoto = 1;
            else if (*p == 'f') falha_http = 1; else if (*p == 'i') mostra_cab = 1;
        }
    }
    if (!url) { ERR("curl: tente 'help curl' para ver o uso\n"); buf_free(&cab); return 2; }
    if (nome_remoto && !saida) saida = nome_da_url(url);
    if (!metodo) metodo = so_cab ? "HEAD" : dados ? "POST" : "GET";
    if (dados && !strstr(buf_str(&cab), "ontent-")) buf_puts(&cab, "Content-Type: application/x-www-form-urlencoded\n");
    char *corpo = NULL, *cabr = NULL; size_t tam = 0;
    int st = host_http(metodo, url, buf_str(&cab), dados, dados ? strlen(dados) : 0, &corpo, &tam, &cabr);
    buf_free(&cab);
    int r = 0;
    if (st == 0) {
        if (!quieto) { ERR("curl: (6) Não foi possível acessar %s\n      %s\n", url, corpo ? corpo : ""); if (!sis.nativo) ERR("%s", AVISO_CORS); }
        r = 6;
    } else if (falha_http && st >= 400) {
        if (!quieto) ERR("curl: (22) O servidor respondeu com o erro %d\n", st);
        r = 22;
    } else {
        if (so_cab || mostra_cab) { OUT("HTTP/1.1 %d\n", st); if (cabr) OUT("%s", cabr); OUT("\n"); }
        if (!so_cab) {
            if (saida) {
                if (vfs_gravar(c->sh->cwd, saida, corpo ? corpo : "", tam, 0)) { ERR("curl: (23) Falha ao gravar %s: %s\n", saida, vfs_erro(vfs_err)); r = 23; }
            } else o_write(c->out, corpo ? corpo : "", tam);
        }
    }
    free(corpo); free(cabr);
    return r;
}

/* ------------------------------------------------------------------ wget */

static int c_wget(Ctx *c, int argc, char **argv) {
    const char *saida = NULL; int quieto = 0, r = 0, feitos = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-O") && i + 1 < argc) saida = argv[++i];
        else if (!strncmp(argv[i], "-O", 2) && argv[i][2]) saida = argv[i] + 2;
        else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet")) quieto = 1;
        else if (!strcmp(argv[i], "-qO-") || !strcmp(argv[i], "-qO")) { quieto = 1; saida = !strcmp(argv[i], "-qO-") ? "-" : (i + 1 < argc ? argv[++i] : "-"); }
        else if (!strcmp(argv[i], "-P") && i + 1 < argc) i++;
    }
    for (int i = 1; i < argc; i++) {
        const char *url = argv[i];
        if (url[0] == '-') { if ((!strcmp(url, "-O") || !strcmp(url, "-P") || !strcmp(url, "-qO")) && i + 1 < argc) i++; continue; }
        if (saida && !strcmp(url, saida) && i > 1 && (!strcmp(argv[i - 1], "-O") || !strcmp(argv[i - 1], "-qO"))) continue;
        feitos++;
        char *completa = strstr(url, "://") ? xstrdup(url) : xasprintf("https://%s", url);
        const char *nome = saida ? saida : nome_da_url(completa);
        if (!quieto) ERR("--%s--\nConectando... ", completa);
        char *corpo = NULL; size_t tam = 0;
        int st = host_http("GET", completa, "", NULL, 0, &corpo, &tam, NULL);
        if (st == 0) {
            ERR("falhou.\nwget: não foi possível acessar %s\n      %s\n", completa, corpo ? corpo : "");
            if (!sis.nativo) ERR("%s", AVISO_CORS);
            r = 4;
        } else if (st >= 400) {
            ERR("%s%d\nwget: ERRO %d\n", quieto ? "" : "resposta: ", st, st);
            r = 8;
        } else if (!strcmp(nome, "-")) o_write(c->out, corpo ? corpo : "", tam);
        else if (vfs_gravar(c->sh->cwd, nome, corpo ? corpo : "", tam, 0)) { ERR("\nwget: %s: %s\n", nome, vfs_erro(vfs_err)); r = 3; }
        else if (!quieto) {
            char h[24];
            ERR("resposta: %d OK\nTamanho: %lu (%s)\nSalvando em: “%s”\n\n“%s” salvo [%lu/%lu]\n", st, (unsigned long)tam,
                k_tamanho_humano((double)tam, h, sizeof h), nome, nome, (unsigned long)tam, (unsigned long)tam);
        }
        free(corpo); free(completa);
    }
    if (!feitos) { ERR("wget: falta a URL\nUso: wget [-q] [-O arquivo] URL\n"); return 1; }
    return r;
}

/* ------------------------------------------------------------------ GitHub */

/* aceita https://github.com/dono/repo(.git), git@github.com:dono/repo.git, github:dono/repo e dono/repo */
int gh_url(const char *url, char *dono, size_t nd, char *repo, size_t nr) {
    const char *p = url;
    if (!strncmp(p, "github:", 7)) p += 7;
    else if (!strncmp(p, "git@github.com:", 15)) p += 15;
    else {
        const char *g = strstr(p, "github.com/");
        if (g) p = g + 11;
        else if (strstr(p, "://") || strchr(p, '@')) return 0;
    }
    const char *b = strchr(p, '/');
    if (!b || b == p) return 0;
    size_t ld = (size_t)(b - p);
    const char *r = b + 1;
    size_t lr = strcspn(r, "/?#");
    if (lr > 4 && !strncmp(r + lr - 4, ".git", 4)) lr -= 4;
    if (!lr || ld >= nd || lr >= nr) return 0;
    for (size_t i = 0; i < ld; i++) if (!(isalnum((unsigned char)p[i]) || p[i] == '-' || p[i] == '_' || p[i] == '.')) return 0;
    for (size_t i = 0; i < lr; i++) if (!(isalnum((unsigned char)r[i]) || r[i] == '-' || r[i] == '_' || r[i] == '.')) return 0;
    memcpy(dono, p, ld); dono[ld] = 0;
    memcpy(repo, r, lr); repo[lr] = 0;
    return 1;
}

static void url_caminho(Buf *b, const char *cam) {
    for (; *cam; cam++) {
        unsigned char ch = (unsigned char)*cam;
        if (isalnum(ch) || strchr("-_.~/", ch)) buf_putc(b, ch);
        else buf_printf(b, "%%%02X", ch);
    }
}

/* baixa um repositório do GitHub para "destino". "prefixos" (opcional) limita às pastas dadas,
 * separadas por vírgula. Devolve 0 se deu certo. */
int gh_clonar(Ctx *c, const char *dono, const char *repo, const char *ramo, const char *destino, const char *prefixos, int quieto) {
    const char *ref = ramo && *ramo ? ramo : "HEAD";
    char *api = xasprintf("https://api.github.com/repos/%s/%s/git/trees/%s?recursive=1", dono, repo, ref);
    char *corpo = NULL; size_t tam = 0;
    int st = host_http("GET", api, "Accept: application/vnd.github+json\n", NULL, 0, &corpo, &tam, NULL);
    free(api);
    if (st != 200) {
        if (st == 404) ERR("fatal: repositório 'https://github.com/%s/%s' não encontrado%s\n", dono, repo, ramo && *ramo ? " (ou o ramo não existe)" : "");
        else if (st == 403 || st == 429) ERR("fatal: o GitHub limitou os acessos deste endereço por enquanto (erro %d). Tente de novo em alguns minutos.\n", st);
        else if (st == 0) ERR("fatal: não foi possível acessar o GitHub: %s\n", corpo ? corpo : "sem rede");
        else ERR("fatal: o GitHub respondeu com o erro %d\n", st);
        free(corpo);
        return 128;
    }
    JVal *j = json_ler(corpo, tam);
    free(corpo);
    JVal *arv = json_obter(j, "tree");
    if (!arv || arv->tipo != J_LISTA) { ERR("fatal: resposta inesperada do GitHub\n"); json_livrar(j); return 128; }
    /* conta o que vai baixar */
    int total = 0; double bytes = 0;
    for (int i = 0; i < arv->n; i++) {
        JVal *it = arv->itens[i];
        if (strcmp(json_texto(it, "type", ""), "blob")) continue;
        total++; bytes += json_numero(it, "size", 0);
    }
    if (!vfs_mkdir(c->sh->cwd, destino, 1, 0755)) { ERR("fatal: não foi possível criar '%s': %s\n", destino, vfs_erro(vfs_err)); json_livrar(j); return 128; }
    char h[24];
    if (!quieto) {
        ERR("remote: Enumerando objetos: %d, pronto.\n", arv->n);
        ERR("remote: Total %d arquivos (%s)\n", total, k_tamanho_humano(bytes, h, sizeof h));
        if (json_numero(j, "truncated", 0) != 0) ERR("aviso: o repositório é grande demais e a lista veio cortada pelo GitHub; alguns arquivos vão faltar.\n");
    }
    int feitos = 0, falhas = 0, pulados = 0, tty = o_tty(c->err);
    double recebido = 0;
    for (int i = 0; i < arv->n; i++) {
        JVal *it = arv->itens[i];
        const char *cam = json_texto(it, "path", NULL), *tipo = json_texto(it, "type", "");
        if (!cam || strstr(cam, "..")) continue;
        if (prefixos && *prefixos) {
            int dentro = 0;
            for (const char *p = prefixos; *p;) {
                size_t l = strcspn(p, ",");
                if (l && !strncmp(cam, p, l)) dentro = 1;
                /* as pastas acima do prefixo também precisam existir */
                if (l && !strcmp(tipo, "tree") && !strncmp(p, cam, strlen(cam))) dentro = 1;
                p += l;
                if (*p == ',') p++;
            }
            if (!dentro) { if (!strcmp(tipo, "blob")) pulados++; continue; }
        }
        char *alvo = xasprintf("%s/%s", destino, cam);
        if (!strcmp(tipo, "tree")) { vfs_mkdir(c->sh->cwd, alvo, 1, 0755); free(alvo); continue; }
        if (strcmp(tipo, "blob")) { free(alvo); continue; }         /* submódulos ficam de fora */
        double tm = json_numero(it, "size", 0);
        const char *modo = json_texto(it, "mode", "100644");
        if (tm > 40.0 * 1048576) {
            ERR("\naviso: pulando %s (%s): grande demais para o navegador\n", cam, k_tamanho_humano(tm, h, sizeof h));
            free(alvo); falhas++; continue;
        }
        /* garante a pasta do arquivo */
        char *barra = strrchr(alvo, '/');
        if (barra) { *barra = 0; vfs_mkdir(c->sh->cwd, alvo, 1, 0755); *barra = '/'; }
        Buf u; buf_init(&u);
        buf_printf(&u, "https://raw.githubusercontent.com/%s/%s/%s/", dono, repo, ref);
        url_caminho(&u, cam);
        char *dados = NULL; size_t n = 0;
        int s2 = host_http("GET", buf_str(&u), "", NULL, 0, &dados, &n, NULL);
        buf_free(&u);
        if (s2 == 200) {
            if (!strcmp(modo, "120000")) {       /* link simbólico: o conteúdo é o destino */
                char *dst = xasprintf("%.*s", (int)n, dados ? dados : "");
                Inode *v = vfs_achar(c->sh->cwd, alvo, 0);
                if (v) vfs_remover(v, 1);
                vfs_link(c->sh->cwd, dst, alvo);
                free(dst);
            } else {
                Inode *f = vfs_criar(c->sh->cwd, alvo, !strcmp(modo, "100755") ? 0755 : 0644);
                if (!f || vfs_gravar_no(f, dados ? dados : "", n, 0)) { falhas++; ERR("\nerro: não foi possível gravar %s: %s\n", alvo, vfs_erro(vfs_err)); }
                else if (!strcmp(modo, "100755")) f->mode = T_REG | 0755;
            }
            recebido += (double)n;
        } else { falhas++; if (!quieto) ERR("\nerro: não foi possível baixar %s (erro %d)\n", cam, s2); }
        free(dados); free(alvo);
        feitos++;
        if (!quieto && tty && (feitos % 3 == 0 || feitos == total - pulados))
            ERR("\rRecebendo objetos: %3d%% (%d/%d), %s", (total - pulados) ? feitos * 100 / (total - pulados) : 100, feitos, total - pulados, k_tamanho_humano(recebido, h, sizeof h));
        if (host_interrompido()) { ERR("\nInterrompido.\n"); json_livrar(j); return 130; }
    }
    if (!quieto) ERR("%sRecebendo objetos: 100%% (%d/%d), %s, pronto.\n", tty ? "\r" : "", feitos - falhas, total - pulados, k_tamanho_humano(recebido, h, sizeof h));
    /* um .git mínimo, para o "git remote -v" e o "git pull" saberem de onde veio */
    char *cfg = xasprintf("%s/.git", destino);
    vfs_mkdir(c->sh->cwd, cfg, 1, 0755);
    free(cfg);
    cfg = xasprintf("%s/.git/config", destino);
    char *txt = xasprintf("[remote \"origin\"]\n\turl = https://github.com/%s/%s\n\tfetch = +refs/heads/*:refs/remotes/origin/*\n[tnux]\n\tdono = %s\n\trepo = %s\n\tramo = %s\n", dono, repo, dono, repo, ramo && *ramo ? ramo : "");
    vfs_gravar(c->sh->cwd, cfg, txt, strlen(txt), 0);
    free(cfg); free(txt);
    cfg = xasprintf("%s/.git/HEAD", destino);
    txt = xasprintf("ref: refs/heads/%s\n", ramo && *ramo ? ramo : "main");
    vfs_gravar(c->sh->cwd, cfg, txt, strlen(txt), 0);
    free(cfg); free(txt);
    json_livrar(j);
    return falhas && feitos == falhas ? 128 : 0;
}

/* acha a pasta do repositório (a que tem .git/config) subindo a partir do diretório atual */
static Inode *git_raiz(Shell *sh, char **cam) {
    for (Inode *d = vfs_achar("/", sh->cwd, 1); d; d = d->pai) {
        Inode *g = vfs_filho(d, ".git");
        if (g && vfs_filho(g, "config")) { *cam = vfs_caminho(d); return vfs_filho(g, "config"); }
    }
    return NULL;
}
static char *cfg_valor(Inode *cfg, const char *chave) {
    char *alvo = xasprintf("\t%s = ", chave);
    const char *p = cfg->dados ? strstr(cfg->dados, alvo) : NULL;
    char *r = NULL;
    if (p) { p += strlen(alvo); r = xasprintf("%.*s", (int)strcspn(p, "\n"), p); }
    free(alvo);
    return r;
}

static int c_git(Ctx *c, int argc, char **argv) {
    const char *sub = argc > 1 ? argv[1] : "";
    if (!strcmp(sub, "--version") || !strcmp(sub, "version")) { OUT("git version 2.43.0 (tnux)\n"); return 0; }
    if (!strcmp(sub, "clone")) {
        const char *url = NULL, *dir = NULL, *ramo = NULL; int quieto = 0;
        for (int i = 2; i < argc; i++) {
            if ((!strcmp(argv[i], "-b") || !strcmp(argv[i], "--branch")) && i + 1 < argc) ramo = argv[++i];
            else if (!strcmp(argv[i], "--depth") && i + 1 < argc) i++;
            else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet")) quieto = 1;
            else if (argv[i][0] == '-') {}
            else if (!url) url = argv[i];
            else dir = argv[i];
        }
        if (!url) { ERR("fatal: Você deve especificar um repositório para clonar.\n\nuso: git clone [-b ramo] <repositório> [<pasta>]\n"); return 129; }
        char dono[100], repo[100];
        if (!gh_url(url, dono, sizeof dono, repo, sizeof repo)) {
            ERR("fatal: '%s': no navegador o git do TNUX só clona do GitHub.\n"
                "       Para outros servidores (GitLab, Codeberg...) use o modo ponte (veja: tnux ajuda ponte).\n", url);
            return 128;
        }
        if (!dir) dir = repo;
        Inode *ja = vfs_achar(c->sh->cwd, dir, 1);
        if (ja && (!EH_DIR(ja) || ja->nfilhos)) { ERR("fatal: o caminho de destino '%s' já existe e não é um diretório vazio.\n", dir); return 128; }
        if (!quieto) ERR("Clonando em '%s'...\n", dir);
        return gh_clonar(c, dono, repo, ramo, dir, NULL, quieto);
    }
    char *raiz = NULL;
    Inode *cfg = git_raiz(c->sh, &raiz);
    if (!strcmp(sub, "pull") || !strcmp(sub, "remote") || !strcmp(sub, "status") || !strcmp(sub, "branch") || !strcmp(sub, "log")) {
        if (!cfg) { ERR("fatal: não é um repositório git (ou algum dos diretórios pai): .git\n"); return 128; }
        char *dono = cfg_valor(cfg, "dono"), *repo = cfg_valor(cfg, "repo"), *ramo = cfg_valor(cfg, "ramo");
        int r = 0;
        if (!dono || !repo) { ERR("fatal: este repositório não foi clonado pelo TNUX\n"); r = 128; }
        else if (!strcmp(sub, "remote")) {
            if (argc > 2 && !strcmp(argv[2], "-v")) OUT("origin\thttps://github.com/%s/%s (fetch)\norigin\thttps://github.com/%s/%s (push)\n", dono, repo, dono, repo);
            else OUT("origin\n");
        } else if (!strcmp(sub, "branch")) OUT("* %s\n", ramo && *ramo ? ramo : "main");
        else if (!strcmp(sub, "status")) OUT("No ramo %s\nSua cópia veio de 'origin/%s'.\n\n(o git do TNUX no navegador baixa e atualiza; para commits, use o modo ponte)\n", ramo && *ramo ? ramo : "main", ramo && *ramo ? ramo : "main");
        else if (!strcmp(sub, "log")) OUT("(o histórico de commits não é baixado no navegador; veja em https://github.com/%s/%s/commits)\n", dono, repo);
        else {
            ERR("Atualizando a partir de https://github.com/%s/%s...\n", dono, repo);
            r = gh_clonar(c, dono, repo, ramo, raiz, NULL, 0);
            if (!r) OUT("Já está atualizado com o GitHub.\n");
        }
        free(dono); free(repo); free(ramo); free(raiz);
        return r;
    }
    free(raiz);
    if (!*sub || !strcmp(sub, "--help") || !strcmp(sub, "help")) {
        OUT("uso: git clone [-b ramo] <repositório do GitHub> [<pasta>]\n"
            "     git pull | git status | git remote -v | git branch | git log\n\n"
            "No navegador, o git do TNUX baixa e atualiza repositórios do GitHub.\n"
            "Para o git completo (commit, push, outros servidores), use o modo ponte: tnux ajuda ponte\n");
        return *sub ? 0 : 1;
    }
    ERR("git: '%s' só funciona no modo ponte (o git completo roda no aparelho). Veja: tnux ajuda ponte\n", sub);
    return 1;
}

/* ------------------------------------------------------------------ ping, ip */

static int c_ping(Ctx *c, int argc, char **argv) {
    const char *host = NULL; int n = 4;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) n = atoi(argv[++i]);
        else if (argv[i][0] != '-') host = argv[i];
    }
    if (!host) { ERR("ping: uso: ping [-c N] endereço\n"); return 2; }
    if (n < 1) n = 1;
    if (n > 20) n = 20;
    char *url = strstr(host, "://") ? xstrdup(host) : xasprintf("https://%s/", host);
    OUT("PING %s: o navegador não envia ICMP; medindo o tempo de resposta por HTTPS.\n", host);
    int ok = 0; double soma = 0, min = 1e12, max = 0;
    for (int i = 0; i < n && !host_interrompido(); i++) {
        char *corpo = NULL; size_t tam = 0;
        double t0 = host_agora_ms();
        int st = host_http("HEAD", url, "", NULL, 0, &corpo, &tam, NULL);
        double dt = host_agora_ms() - t0;
        free(corpo);
        if (st) { OUT("resposta de %s: seq=%d status=%d tempo=%.0f ms\n", host, i + 1, st, dt); ok++; soma += dt; if (dt < min) min = dt; if (dt > max) max = dt; }
        else OUT("sem resposta de %s: seq=%d (o site não respondeu ou não aceita acesso direto do navegador)\n", host, i + 1);
        if (i + 1 < n) host_dormir(300);
    }
    OUT("\n--- %s: estatísticas ---\n%d enviados, %d recebidos, %d%% de perda\n", host, n, ok, (n - ok) * 100 / n);
    if (ok) OUT("tempo mín/méd/máx = %.0f/%.0f/%.0f ms\n", min, soma / ok, max);
    free(url);
    return ok ? 0 : 1;
}
static int c_ip(Ctx *c, int argc, char **argv) {
    (void)argc; (void)argv;
    OUT("1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 state UNKNOWN\n    inet 127.0.0.1/8 scope host lo\n");
    OUT("2: web0: <UP,LOWER_UP> mtu 1500 state UP\n    rede fornecida pelo %s (HTTP/HTTPS)\n", sis.nativo ? "aparelho" : "navegador");
    return 0;
}

const Cmd cmds_rede[] = {
    { "curl", c_curl, "busca uma URL", "curl [-s] [-L] [-I] [-o arquivo | -O] [-X método] [-H cabeçalho] [-d dados] URL" },
    { "wget", c_wget, "baixa um arquivo", "wget [-q] [-O arquivo] URL" },
    { "git", c_git, "baixa e atualiza repositórios do GitHub", "git clone [-b ramo] URL [pasta]  |  git pull  |  git remote -v" },
    { "ping", c_ping, "testa se um site responde", "ping [-c N] endereço" },
    { "ip", c_ip, "interfaces de rede", "ip a" },
    { "ifconfig", c_ip, NULL, NULL },
    { NULL, NULL, NULL, NULL }
};
