/*
 * TNUX: gerenciador de pacotes (apt, apt-get, dpkg).
 *
 * O catálogo (/var/lib/apt/lists/catalogo.json) descreve os programas. Tipos de pacote:
 *   interno  programa da própria área de trabalho (terminal, arquivos, editor...)
 *   web      um site/app web que abre numa janela
 *   git      um repositório do GitHub baixado para /opt (apps web estáticos, scripts)
 *   wasm     um programa WebAssembly (WASI) baixado para /usr/bin
 *   script   um script de shell baixado para /usr/bin
 *   nativo   um programa Linux de verdade (Chrome, GIMP...): só no modo ponte, onde o aparelho
 *            instala e executa
 * O que está instalado fica em /var/lib/dpkg/status, no mesmo formato de texto do dpkg.
 */
#include "cmds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int gh_url(const char *url, char *dono, size_t nd, char *repo, size_t nr);
int gh_clonar(Ctx *c, const char *dono, const char *repo, const char *ramo, const char *destino, const char *prefixos, int quieto);

#define STATUS "/var/lib/dpkg/status"
#define CATALOGO "/var/lib/apt/lists/catalogo.json"
#define APPS "/usr/share/applications"

typedef struct { const char *id, *nome, *resumo, *icone, *categoria; int essencial; } Interno;
static const Interno INTERNOS[] = {
    { "terminal", "Terminal", "Use a linha de comando", "terminal", "Sistema", 1 },
    { "arquivos", "Arquivos", "Acesse e organize seus arquivos", "pasta", "Acessórios", 1 },
    { "editor", "Editor de Texto", "Edite arquivos de texto", "editor", "Acessórios", 0 },
    { "navegador", "Navegador Web", "Navegue na internet", "navegador", "Internet", 0 },
    { "loja", "Gerenciador de Aplicativos", "Instale e remova programas", "loja", "Sistema", 1 },
    { "config", "Configurações do Sistema", "Aparência, papel de parede e modo ponte", "config", "Preferências", 1 },
    { "monitor", "Monitor do Sistema", "Veja memória, disco e janelas abertas", "monitor", "Sistema", 0 },
    { "imagem", "Visualizador de Imagens", "Veja suas imagens", "imagem", "Gráficos", 0 },
    { "calculadora", "Calculadora", "Faça contas", "calculadora", "Acessórios", 0 },
    { "sobre", "Informações do Sistema", "Versão do TNUX e dados do aparelho", "info", "Preferências", 0 },
    { "processamento", "Processamento no computador", "O computador faz o serviço pesado para o aparelho", "monitor", "Sistema", 0 },
    { "boasvindas", "Bem-vindo", "Primeiros passos no TNUX", "tnux", "Sistema", 0 },
    { NULL, NULL, NULL, NULL, NULL, 0 }
};
static const char *const BASE_PKGS[][2] = {
    { "tnux-base", "Arquivos básicos do sistema TNUX" }, { "tnux-desktop", "Área de trabalho do TNUX (estilo Cinnamon)" },
    { "bash", "Interpretador de comandos" }, { "coreutils", "Utilitários básicos (ls, cp, mv, cat...)" },
    { "apt", "Gerenciador de pacotes" }, { "dpkg", "Sistema de pacotes" }, { "grep", "Procura de texto" },
    { "sed", "Editor de fluxo de texto" }, { "findutils", "find e xargs" }, { "curl", "Transferência de URLs" },
    { "wget", "Baixa arquivos da web" }, { "git", "Baixa repositórios do GitHub" }, { NULL, NULL }
};

/* ------------------------------------------------------------------ banco de pacotes instalados */

static const char *status_bloco(const char *id, size_t *tam) {
    Inode *st = vfs_achar("/", STATUS, 1);
    if (!st || !st->dados) return NULL;
    char *chave = xasprintf("Package: %s\n", id);
    const char *p = st->dados;
    const char *r = NULL;
    while ((p = strstr(p, chave))) {
        if (p == st->dados || p[-1] == '\n') { r = p; break; }
        p++;
    }
    free(chave);
    if (!r) return NULL;
    const char *fim = strstr(r, "\n\n");
    *tam = fim ? (size_t)(fim - r) + 2 : strlen(r);
    return r;
}
static int instalado(const char *id) { size_t t; return status_bloco(id, &t) != NULL; }
static char *status_campo(const char *id, const char *campo) {
    size_t t;
    const char *b = status_bloco(id, &t);
    if (!b) return NULL;
    char *chave = xasprintf("\n%s: ", campo);
    char *r = NULL;
    for (const char *p = b; p + strlen(chave) < b + t; p++) {
        if (!strncmp(p, chave, strlen(chave))) { p += strlen(chave); r = xasprintf("%.*s", (int)strcspn(p, "\n"), p); break; }
    }
    free(chave);
    return r;
}
static void status_tirar(const char *id) {
    size_t t;
    const char *b = status_bloco(id, &t);
    if (!b) return;
    Inode *st = vfs_achar("/", STATUS, 1);
    size_t ini = (size_t)(b - st->dados);
    Buf n; buf_init(&n);
    buf_put(&n, st->dados, ini);
    buf_put(&n, b + t, st->tam - ini - t);
    uint32_t e = vfs_euid; vfs_euid = 0;
    vfs_gravar_no(st, n.d ? n.d : "", n.n, 0);
    vfs_euid = e;
    buf_free(&n);
}
static void status_por(const char *id, const char *versao, const char *tipo, const char *resumo, const char *arquivos) {
    status_tirar(id);
    char *txt = xasprintf("Package: %s\nStatus: install ok installed\nVersion: %s\nTnux-Tipo: %s\nTnux-Arquivos: %s\nDescription: %s\n\n",
                          id, versao, tipo, arquivos ? arquivos : "", resumo);
    uint32_t e = vfs_euid; vfs_euid = 0;
    vfs_gravar("/", STATUS, txt, strlen(txt), 1);
    vfs_euid = e;
    free(txt);
}

static void desktop_criar(const char *id, const char *nome, const char *resumo, const char *icone, const char *categoria, const char *tipo, const char *alvo) {
    char *cam = xasprintf(APPS "/%s.desktop", id);
    char *txt = xasprintf("[Desktop Entry]\nType=Application\nName=%s\nComment=%s\nIcon=%s\nCategories=%s;\nExec=xdg-open %s\nX-TNUX-Tipo=%s\nX-TNUX-Alvo=%s\n",
                          nome, resumo, icone, categoria, cam, tipo, alvo);
    vfs_sis_arq(cam, txt, 0644, 0);
    free(cam); free(txt);
}

void apt_instalar_base(void) {
    if (!vfs_achar("/", STATUS, 1)) vfs_sis_arq(STATUS, "", 0644, 0);
    for (int i = 0; BASE_PKGS[i][0]; i++)
        if (!instalado(BASE_PKGS[i][0])) status_por(BASE_PKGS[i][0], TNUX_VERSAO, "sistema", BASE_PKGS[i][1], "");
    /* os programas da área de trabalho entram uma vez; se o usuário remover, não voltam sozinhos */
    Inode *ja = vfs_achar("/", "/var/lib/dpkg/tnux-base", 1);
    Buf lista; buf_init(&lista);
    if (ja && ja->dados) buf_put(&lista, ja->dados, ja->tam);
    int mudou = 0;
    for (const Interno *a = INTERNOS; a->id; a++) {
        char *marca = xasprintf("%s\n", a->id);
        const char *p = lista.n ? strstr(buf_str(&lista), marca) : NULL;
        int visto = p && (p == lista.d || p[-1] == '\n');
        if (!visto) {
            status_por(a->id, TNUX_VERSAO, "interno", a->resumo, "");
            buf_puts(&lista, marca);
            mudou = 1;
        }
        if (instalado(a->id)) desktop_criar(a->id, a->nome, a->resumo, a->icone, a->categoria, "interno", a->id);
        free(marca);
    }
    if (mudou) vfs_sis_arq("/var/lib/dpkg/tnux-base", buf_str(&lista), 0644, 0);
    buf_free(&lista);
}

/* lista os atalhos de /usr/share/applications em JSON, para o menu da área de trabalho */
int apt_json_instalados(Buf *o) {
    Inode *d = vfs_achar("/", APPS, 1);
    buf_putc(o, '[');
    int n = 0;
    if (d && EH_DIR(d)) {
        vfs_ordenar(d);
        for (int i = 0; i < d->nfilhos; i++) {
            Inode *f = d->filhos[i];
            size_t ln = strlen(f->nome);
            if (!EH_REG(f) || ln < 9 || strcmp(f->nome + ln - 8, ".desktop") || !f->dados) continue;
            if (n++) buf_putc(o, ',');
            buf_puts(o, "{\"id\":");
            char *id = xasprintf("%.*s", (int)(ln - 8), f->nome);
            buf_json_str(o, id);
            free(id);
            const char *p = f->dados, *fim = f->dados + f->tam, *l; size_t tl;
            while ((l = prox_linha(&p, fim, &tl))) {
                const char *eq = memchr(l, '=', tl);
                if (!eq || l[0] == '[' || l[0] == '#') continue;
                char *k = xasprintf("%.*s", (int)(eq - l), l), *v = xasprintf("%.*s", (int)(l + tl - eq - 1), eq + 1);
                const char *nome = !strcmp(k, "Name") ? "nome" : !strcmp(k, "Comment") ? "resumo" : !strcmp(k, "Icon") ? "icone" :
                                   !strcmp(k, "Categories") ? "categoria" : !strcmp(k, "X-TNUX-Tipo") ? "tipo" : !strcmp(k, "X-TNUX-Alvo") ? "alvo" : NULL;
                if (nome) {
                    if (!strcmp(nome, "categoria")) { char *pv = strchr(v, ';'); if (pv) *pv = 0; }
                    buf_printf(o, ",\"%s\":", nome);
                    buf_json_str(o, v);
                }
                free(k); free(v);
            }
            buf_putc(o, '}');
        }
    }
    buf_putc(o, ']');
    return n;
}

/* ------------------------------------------------------------------ catálogo */

static JVal *catalogo(void) {
    Inode *f = vfs_achar("/", CATALOGO, 1);
    if (!f || !f->dados) return NULL;
    return json_ler(f->dados, f->tam);
}
static JVal *pacotes(JVal *cat) {
    JVal *p = json_obter(cat, "pacotes");
    return p && p->tipo == J_LISTA ? p : NULL;
}
static JVal *pkg_achar(JVal *cat, const char *nome) {
    JVal *ps = pacotes(cat);
    if (!ps) return NULL;
    for (int i = 0; i < ps->n; i++) {
        JVal *p = ps->itens[i];
        if (!strcasecmp(json_texto(p, "id", ""), nome)) return p;
        if (!strcasecmp(json_texto(p, "comando", ""), nome)) return p;
        JVal *ap = json_obter(p, "apelidos");
        if (ap && ap->tipo == J_LISTA)
            for (int k = 0; k < ap->n; k++)
                if (ap->itens[k]->tipo == J_STR && !strcasecmp(ap->itens[k]->str, nome)) return p;
    }
    return NULL;
}
static const Interno *interno_achar(const char *id) {
    for (const Interno *a = INTERNOS; a->id; a++) if (!strcmp(a->id, id)) return a;
    return NULL;
}
static int contem_ic(const char *texto, const char *termo) {
    size_t lt = strlen(termo);
    for (; *texto; texto++) if (!strncasecmp(texto, termo, lt)) return 1;
    return 0;
}

static void avisa_desktop(void) { host_gui("{\"acao\":\"apps_mudou\"}"); }

static void explica_nativo(Ctx *c, JVal *p) {
    ERR("\x1b[1;33mO pacote '%s' (%s) é um programa Linux nativo.\x1b[0m\n", json_texto(p, "id", ""), json_texto(p, "nome", ""));
    ERR("Dentro do navegador, sozinho, o TNUX roda apps web, scripts e programas WebAssembly.\n"
        "Para instalar e usar programas Linux de verdade, rode o TNUX no modo ponte, em que o seu\n"
        "aparelho (Termux ou Linux) faz o trabalho:\n\n"
        "    tnux ajuda ponte\n\n");
}

/* instala um pacote do catálogo. 0 = ok */
static int instalar_pkg(Ctx *c, JVal *p, int n, int total) {
    const char *id = json_texto(p, "id", ""), *tipo = json_texto(p, "tipo", "web");
    const char *nome = json_texto(p, "nome", id), *resumo = json_texto(p, "resumo", nome);
    const char *versao = json_texto(p, "versao", "1.0"), *icone = json_texto(p, "icone", "app");
    const char *categoria = json_texto(p, "categoria", "Outros"), *comando = json_texto(p, "comando", NULL);
    Buf arqs; buf_init(&arqs);
    int r = 0;
    OUT("Obter:%d %s aurora/main %s %s\n", n, !strcmp(tipo, "git") ? "https://github.com" : "tnux", id, versao);
    if (!strcmp(tipo, "interno")) {
        const Interno *a = interno_achar(id);
        if (a) desktop_criar(a->id, a->nome, a->resumo, a->icone, a->categoria, "interno", a->id);
    } else if (!strcmp(tipo, "web")) {
        const char *url = json_texto(p, "url", "");
        desktop_criar(id, nome, resumo, icone, categoria, "web", url);
    } else if (!strcmp(tipo, "git")) {
        char dono[100], repo[100];
        const char *origem = json_texto(p, "repo", "");
        if (!gh_url(origem, dono, sizeof dono, repo, sizeof repo)) { ERR("E: o pacote %s tem um repositório inválido: %s\n", id, origem); buf_free(&arqs); return 1; }
        char *dest = xasprintf("/opt/%s", id);
        Inode *velho = vfs_achar("/", dest, 0);
        if (velho) vfs_remover(velho, 1);
        OUT("A descompactar %s (%s) de github.com/%s/%s ...\n", id, versao, dono, repo);
        r = gh_clonar(c, dono, repo, json_texto(p, "ramo", NULL), dest, json_texto(p, "pastas", NULL), 0);
        if (!r) {
            buf_printf(&arqs, "%s ", dest);
            const char *entrada = json_texto(p, "entrada", NULL);
            char *alvo = xasprintf("%s/%s", dest, entrada ? entrada : "index.html");
            if (vfs_achar("/", alvo, 1)) desktop_criar(id, nome, resumo, icone, categoria, "arquivo", alvo);
            else if (entrada) ERR("W: o arquivo de entrada %s não veio no repositório\n", alvo);
            free(alvo);
            const char *script = json_texto(p, "script", NULL);
            if (comando && script) {
                char *bin = xasprintf("/usr/bin/%s", comando), *txt = xasprintf("#!/bin/bash\nexec bash %s/%s \"$@\"\n", dest, script);
                vfs_sis_arq(bin, txt, 0755, 0);
                buf_printf(&arqs, "%s ", bin);
                free(bin); free(txt);
            }
        }
        free(dest);
    } else if (!strcmp(tipo, "wasm") || !strcmp(tipo, "script")) {
        const char *url = json_texto(p, "url", "");
        char *corpo = NULL; size_t tam = 0;
        int st = host_http("GET", url, "", NULL, 0, &corpo, &tam, NULL);
        if (st != 200) { ERR("E: Falhou ao obter %s  (%s%d)\n", url, st ? "erro " : "sem acesso, ", st); r = 1; }
        else {
            char *bin = xasprintf("/usr/bin/%s", comando ? comando : id);
            Inode *f = vfs_criar("/", bin, 0755);
            if (!f || vfs_gravar_no(f, corpo ? corpo : "", tam, 0)) { ERR("E: não foi possível gravar %s: %s\n", bin, vfs_erro(vfs_err)); r = 1; }
            else { f->mode = T_REG | 0755; f->uid = 0; buf_printf(&arqs, "%s ", bin); }
            free(bin);
        }
        free(corpo);
    } else { ERR("E: tipo de pacote desconhecido: %s\n", tipo); r = 1; }
    if (!r) {
        OUT("A configurar %s (%s) ...\n", id, versao);
        status_por(id, versao, tipo, resumo, buf_str(&arqs));
    }
    buf_free(&arqs);
    (void)total;
    return r;
}

/* instala direto de um repositório do GitHub que não está no catálogo: dono/repo */
static int instalar_github(Ctx *c, const char *spec) {
    char dono[100], repo[100];
    if (!gh_url(spec, dono, sizeof dono, repo, sizeof repo)) return -1;
    char id[128];
    size_t k = 0;
    for (const char *s = repo; *s && k < sizeof id - 1; s++) id[k++] = (char)tolower((unsigned char)*s);
    id[k] = 0;
    char *dest = xasprintf("/opt/%s", id);
    Inode *velho = vfs_achar("/", dest, 0);
    if (velho) vfs_remover(velho, 1);
    OUT("Obter:1 https://github.com/%s/%s HEAD\n", dono, repo);
    int r = gh_clonar(c, dono, repo, NULL, dest, NULL, 0);
    if (!r) {
        char *idx = xasprintf("%s/index.html", dest);
        char *resumo = xasprintf("Instalado de github.com/%s/%s", dono, repo);
        int tem_pagina = vfs_achar("/", idx, 1) != NULL;
        if (tem_pagina) desktop_criar(id, repo, resumo, "web", "Outros", "arquivo", idx);
        status_por(id, "git", "git", resumo, dest);
        OUT("A configurar %s (git) ...\n", id);
        if (tem_pagina) OUT("O app \"%s\" apareceu no menu (categoria Outros).\n", repo);
        else OUT("Os arquivos estão em %s (o repositório não tem index.html, então não entrou no menu).\n", dest);
        free(idx); free(resumo);
    }
    free(dest);
    return r ? 1 : 0;
}

static int remover_pkg(Ctx *c, const char *id) {
    const Interno *a = interno_achar(id);
    char *tipo = status_campo(id, "Tnux-Tipo");
    if ((a && a->essencial) || (tipo && !strcmp(tipo, "sistema"))) {
        ERR("E: o pacote %s é essencial para o sistema e não pode ser removido.\n", id);
        free(tipo);
        return 1;
    }
    char *arqs = status_campo(id, "Tnux-Arquivos");
    OUT("A remover %s ...\n", id);
    for (char *p = arqs; p && *p;) {
        while (*p == ' ') p++;
        char *f = strchr(p, ' ');
        if (f) *f = 0;
        if (*p == '/' && strlen(p) > 5) { Inode *n = vfs_achar("/", p, 0); if (n) vfs_remover(n, 1); }
        if (!f) break;
        p = f + 1;
    }
    char *desk = xasprintf(APPS "/%s.desktop", id);
    Inode *d = vfs_achar("/", desk, 0);
    if (d) vfs_remover(d, 0);
    free(desk); free(arqs); free(tipo);
    status_tirar(id);
    return 0;
}

static void cabecalho(Ctx *c) {
    OUT("Lendo listas de pacotes... Pronto\nConstruindo árvore de dependências... Pronto\nLendo informação de estado... Pronto\n");
}
static int precisa_root(Ctx *c) {
    if (vfs_euid == 0) return 0;
    ERR("E: Não foi possível abrir arquivo de trava /var/lib/dpkg/lock-frontend - open (13: Permissão negada)\n"
        "E: Impossível adquirir a trava do frontend dpkg (/var/lib/dpkg/lock-frontend), você é root?\n");
    return 1;
}
static void linha_lista(Ctx *c, const char *id, const char *versao, const char *tipo, int inst) {
    OUT(o_tty(c->out) ? "\x1b[32m%s\x1b[0m/aurora %s %s%s\n" : "%s/aurora %s %s%s\n", id, versao, tipo, inst ? " [instalado]" : "");
}

static int c_apt(Ctx *c, int argc, char **argv) {
    const char *sub = NULL;
    int i = 1;
    for (; i < argc; i++) { if (argv[i][0] != '-' || !strcmp(argv[i], "--sugerir")) { sub = argv[i++]; break; } }
    if (!sub) {
        OUT("apt %s (%s)\nUso: apt comando [opções]\n\nComandos mais usados:\n"
            "  update      atualiza o catálogo de programas\n  search      procura um programa\n  show        mostra os detalhes de um pacote\n"
            "  install     instala programas (do catálogo, ou USUARIO/REPOSITORIO do GitHub)\n  remove      remove programas\n"
            "  list        lista os pacotes (--installed para só os instalados)\n  upgrade     atualiza o que está instalado\n", TNUX_VERSAO, sis.nativo ? "nativo" : "wasm32");
        return 0;
    }
    JVal *cat = catalogo();
    int r = 0;
    if (!strcmp(sub, "--sugerir")) {
        JVal *p = i < argc ? pkg_achar(cat, argv[i]) : NULL;
        if (p && !instalado(json_texto(p, "id", ""))) {
            OUT("O comando '%s' não foi encontrado, mas pode ser instalado com:\nsudo apt install %s\n", argv[i], json_texto(p, "id", ""));
            if (!strcmp(json_texto(p, "tipo", ""), "nativo")) OUT("(é um programa Linux nativo: precisa do modo ponte; veja: tnux ajuda ponte)\n");
        } else r = 1;
    } else if (!strcmp(sub, "update")) {
        if (precisa_root(c)) r = 100;
        else {
            OUT("Atingido:1 https://github.com/Gustavo-HVF/TNUX-DistroSystem aurora InRelease\n");
            char *corpo = NULL; size_t tam = 0;
            int st = host_http("GET", "catalogo.json", "", NULL, 0, &corpo, &tam, NULL);
            JVal *novo = st == 200 ? json_ler(corpo, tam) : NULL;
            if (novo && pacotes(novo)) {
                vfs_sis_arq(CATALOGO, "", 0644, 0);
                Inode *f = vfs_achar("/", CATALOGO, 1);
                if (f) vfs_gravar_no(f, corpo, tam, 0);
                OUT("Obter:2 catálogo do TNUX [%lu B]\n", (unsigned long)tam);
                cabecalho(c);
                OUT("%d pacotes no catálogo. Todos os pacotes estão atualizados.\n", pacotes(novo)->n);
            } else {
                cabecalho(c);
                OUT("W: não foi possível baixar o catálogo novo agora (erro %d); continuo com o que já tenho.\n", st);
            }
            json_livrar(novo);
            free(corpo);
        }
    } else if (!strcmp(sub, "install") || !strcmp(sub, "reinstall")) {
        int re = !strcmp(sub, "reinstall");
        if (precisa_root(c)) r = 100;
        else if (i >= argc) { ERR("E: informe pelo menos um pacote: apt install NOME\n"); r = 100; }
        else {
            cabecalho(c);
            JVal *lista[64]; const char *gh[64]; int nl = 0, ng = 0;
            for (; i < argc && nl < 64 && ng < 64; i++) {
                if (argv[i][0] == '-') continue;
                JVal *p = pkg_achar(cat, argv[i]);
                char d[100], rp[100];
                if (!p && interno_achar(argv[i])) {
                    const Interno *a = interno_achar(argv[i]);
                    if (instalado(a->id) && !re) OUT("%s já é a versão mais nova (%s).\n", a->id, TNUX_VERSAO);
                    else { desktop_criar(a->id, a->nome, a->resumo, a->icone, a->categoria, "interno", a->id); status_por(a->id, TNUX_VERSAO, "interno", a->resumo, ""); OUT("A configurar %s (%s) ...\n", a->id, TNUX_VERSAO); }
                } else if (!p && instalado(argv[i])) OUT("%s já é a versão mais nova.\n", argv[i]);
                else if (!p && strchr(argv[i], '/') && gh_url(argv[i], d, sizeof d, rp, sizeof rp)) gh[ng++] = argv[i];
                else if (!p) { ERR("E: Impossível encontrar o pacote %s\n", argv[i]); r = 100; }
                else if (!strcmp(json_texto(p, "tipo", ""), "nativo")) { explica_nativo(c, p); r = 100; }
                else if (instalado(json_texto(p, "id", "")) && !re) OUT("%s já é a versão mais nova (%s).\n", json_texto(p, "id", ""), json_texto(p, "versao", "1.0"));
                else lista[nl++] = p;
            }
            if (nl + ng) {
                OUT("Os NOVOS pacotes a seguir serão instalados:\n ");
                for (int k = 0; k < nl; k++) OUT(" %s", json_texto(lista[k], "id", ""));
                for (int k = 0; k < ng; k++) OUT(" %s", gh[k]);
                OUT("\n0 pacotes atualizados, %d pacotes novos instalados, 0 a serem removidos e 0 não atualizados.\n", nl + ng);
                int falhas = 0;
                for (int k = 0; k < nl; k++) if (instalar_pkg(c, lista[k], k + 1, nl)) falhas++;
                for (int k = 0; k < ng; k++) if (instalar_github(c, gh[k])) falhas++;
                if (falhas) { ERR("E: %d pacote(s) não puderam ser instalados.\n", falhas); r = 100; }
                avisa_desktop();
            } else if (!r) OUT("0 pacotes atualizados, 0 pacotes novos instalados, 0 a serem removidos e 0 não atualizados.\n");
        }
    } else if (!strcmp(sub, "remove") || !strcmp(sub, "purge") || !strcmp(sub, "autoremove")) {
        if (precisa_root(c)) r = 100;
        else {
            cabecalho(c);
            int n = 0;
            for (; i < argc; i++) {
                if (argv[i][0] == '-') continue;
                JVal *p = pkg_achar(cat, argv[i]);
                const char *id = p ? json_texto(p, "id", argv[i]) : argv[i];
                char minus[128]; size_t k = 0;
                for (const char *s = id; *s && k < sizeof minus - 1; s++) minus[k++] = *s;
                minus[k] = 0;
                if (!instalado(minus)) { OUT("O pacote '%s' não está instalado, por isso não foi removido\n", argv[i]); continue; }
                if (remover_pkg(c, minus)) r = 100; else n++;
            }
            OUT("0 pacotes atualizados, 0 pacotes novos instalados, %d a serem removidos e 0 não atualizados.\n", n);
            if (n) avisa_desktop();
        }
    } else if (!strcmp(sub, "upgrade") || !strcmp(sub, "full-upgrade") || !strcmp(sub, "dist-upgrade")) {
        if (precisa_root(c)) r = 100;
        else {
            cabecalho(c);
            OUT("Calculando atualização... Pronto\n");
            int n = 0;
            JVal *ps = pacotes(cat);
            for (int k = 0; ps && k < ps->n; k++) {
                JVal *p = ps->itens[k];
                const char *id = json_texto(p, "id", "");
                if (!instalado(id)) continue;
                char *v = status_campo(id, "Version");
                if (v && strcmp(v, json_texto(p, "versao", "1.0")) && strcmp(json_texto(p, "tipo", ""), "nativo")) {
                    OUT("Atualizando %s (%s -> %s)\n", id, v, json_texto(p, "versao", "1.0"));
                    if (!instalar_pkg(c, p, ++n, 0)) {}
                }
                free(v);
            }
            OUT("%d pacotes atualizados, 0 pacotes novos instalados, 0 a serem removidos e 0 não atualizados.\n", n);
            if (n) avisa_desktop();
        }
    } else if (!strcmp(sub, "list")) {
        int so_inst = 0; const char *filtro = NULL;
        for (; i < argc; i++) { if (!strcmp(argv[i], "--installed")) so_inst = 1; else if (argv[i][0] != '-') filtro = argv[i]; }
        OUT("Listando... Pronto\n");
        JVal *ps = pacotes(cat);
        for (int k = 0; ps && k < ps->n; k++) {
            JVal *p = ps->itens[k];
            const char *id = json_texto(p, "id", "");
            int inst = instalado(id);
            if ((so_inst && !inst) || (filtro && !casa_glob(filtro, id))) continue;
            linha_lista(c, id, json_texto(p, "versao", "1.0"), json_texto(p, "tipo", ""), inst);
        }
        /* instalados que não estão no catálogo (base do sistema, repositórios avulsos) */
        Inode *st = vfs_achar("/", STATUS, 1);
        for (const char *p = st && st->dados ? st->dados : ""; (p = strstr(p, "Package: ")); p += 9) {
            if (p != st->dados && p[-1] != '\n') continue;
            char *id = xasprintf("%.*s", (int)strcspn(p + 9, "\n"), p + 9);
            if (!pkg_achar(cat, id) && !(filtro && !casa_glob(filtro, id))) {
                char *v = status_campo(id, "Version"), *t = status_campo(id, "Tnux-Tipo");
                linha_lista(c, id, v ? v : "?", t ? t : "", 1);
                free(v); free(t);
            }
            free(id);
        }
    } else if (!strcmp(sub, "search")) {
        if (i >= argc) { ERR("E: informe o que procurar: apt search TERMO\n"); r = 100; }
        else {
            OUT("Ordenando... Pronto\nPesquisa em todo o texto... Pronto\n");
            JVal *ps = pacotes(cat);
            int n = 0;
            for (int k = 0; ps && k < ps->n; k++) {
                JVal *p = ps->itens[k];
                const char *id = json_texto(p, "id", ""), *nome = json_texto(p, "nome", ""), *res = json_texto(p, "resumo", ""), *ct = json_texto(p, "categoria", "");
                if (!contem_ic(id, argv[i]) && !contem_ic(nome, argv[i]) && !contem_ic(res, argv[i]) && !contem_ic(ct, argv[i])) continue;
                linha_lista(c, id, json_texto(p, "versao", "1.0"), json_texto(p, "tipo", ""), instalado(id));
                OUT("  %s: %s\n\n", nome, res);
                n++;
            }
            if (!n) OUT("Nada encontrado para \"%s\". Dica: \"apt install USUARIO/REPOSITORIO\" instala direto do GitHub.\n", argv[i]);
        }
    } else if (!strcmp(sub, "show") || !strcmp(sub, "info")) {
        JVal *p = i < argc ? pkg_achar(cat, argv[i]) : NULL;
        if (!p) { ERR("N: Impossível encontrar o pacote %s\nE: Nenhum pacote encontrado\n", i < argc ? argv[i] : ""); r = 100; }
        else {
            const char *id = json_texto(p, "id", "");
            OUT("Package: %s\nNome: %s\nVersion: %s\nTipo: %s\nSection: %s\nInstalado: %s\n", id, json_texto(p, "nome", id), json_texto(p, "versao", "1.0"),
                json_texto(p, "tipo", ""), json_texto(p, "categoria", ""), instalado(id) ? "sim" : "não");
            if (json_texto(p, "repo", NULL)) OUT("Origem: https://github.com/%s\n", json_texto(p, "repo", ""));
            if (json_texto(p, "url", NULL)) OUT("Origem: %s\n", json_texto(p, "url", ""));
            if (json_texto(p, "licenca", NULL)) OUT("Licença: %s\n", json_texto(p, "licenca", ""));
            OUT("Description: %s\n", json_texto(p, "resumo", ""));
            if (json_texto(p, "descricao", NULL)) OUT(" %s\n", json_texto(p, "descricao", ""));
        }
    } else if (!strcmp(sub, "clean") || !strcmp(sub, "autoclean")) {
    } else { ERR("E: Operação inválida %s\n", sub); r = 100; }
    json_livrar(cat);
    return r;
}

static int c_dpkg(Ctx *c, int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "-l") || !strcmp(argv[1], "--list"))) {
        OUT("Desejado=Desconhecido/Instalar/Remover/Expurgar/Reter\n||/ Nome                 Versão       Arquitetura  Descrição\n+++-====================-============-============-=================================\n");
        Inode *st = vfs_achar("/", STATUS, 1);
        for (const char *p = st && st->dados ? st->dados : ""; (p = strstr(p, "Package: ")); p += 9) {
            if (p != st->dados && p[-1] != '\n') continue;
            char *id = xasprintf("%.*s", (int)strcspn(p + 9, "\n"), p + 9);
            if (argc > 2 && !casa_glob(argv[2], id)) { free(id); continue; }
            char *v = status_campo(id, "Version"), *d = status_campo(id, "Description");
            OUT("ii  %-20s %-12s %-12s %s\n", id, v ? v : "", "all", d ? d : "");
            free(id); free(v); free(d);
        }
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "-L")) {
        char *a = status_campo(argv[2], "Tnux-Arquivos");
        if (!a) { ERR("dpkg-query: o pacote '%s' não está instalado\n", argv[2]); return 1; }
        for (char *p = a; *p;) { while (*p == ' ') p++; size_t l = strcspn(p, " "); if (l) OUT("%.*s\n", (int)l, p); p += l; }
        char *desk = xasprintf(APPS "/%s.desktop", argv[2]);
        if (vfs_achar("/", desk, 1)) OUT("%s\n", desk);
        free(desk); free(a);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "-s")) {
        size_t t;
        const char *b = status_bloco(argv[2], &t);
        if (!b) { ERR("dpkg-query: o pacote '%s' não está instalado e não há informações disponíveis\n", argv[2]); return 1; }
        o_write(c->out, b, t);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "-i")) {
        ERR("dpkg: pacotes .deb são programas Linux nativos: só no modo ponte (veja: tnux ajuda ponte)\n");
        return 1;
    }
    ERR("uso: dpkg -l [padrão] | dpkg -L pacote | dpkg -s pacote\n");
    return 2;
}

const Cmd cmds_apt[] = {
    { "apt", c_apt, "instala, remove e procura programas", "apt update | search TERMO | show NOME | list [--installed]\n    sudo apt install NOME... | USUARIO/REPOSITORIO\n    sudo apt remove NOME..." },
    { "apt-get", c_apt, NULL, NULL },
    { "dpkg", c_dpkg, "lista os pacotes instalados", "dpkg -l | dpkg -L pacote | dpkg -s pacote" },
    { NULL, NULL, NULL, NULL }
};
