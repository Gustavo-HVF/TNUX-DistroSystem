#!/usr/bin/env node
/*
 * Testa o núcleo compilado para WebAssembly (web/tnux.wasm), do jeito que o navegador o usa:
 * boot, comandos, disco (salvar e recarregar a imagem), terminal interativo, apt, git clone
 * (do GitHub de verdade, se houver rede) e programas WASI.
 *
 *     node tests/testar_wasm.js [--sem-rede]
 */
'use strict';
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');
const { criarNucleo } = require('../web/js/nucleo-host.js');

const WEB = path.join(__dirname, '..', 'web');
const semRede = process.argv.includes('--sem-rede');
let falhas = 0, feitos = 0;
const telas = {};

function http(req) {
  if (!/^[a-z]+:\/\//.test(req.url)) {
    const arq = path.join(WEB, req.url);
    if (!fs.existsSync(arq)) return { status: 404, corpo: new Uint8Array(0), cab: '' };
    return { status: 200, corpo: new Uint8Array(fs.readFileSync(arq)), cab: '' };
  }
  if (semRede) return { status: 0, corpo: Buffer.from('sem rede (teste)'), cab: '' };
  const r = spawnSync('curl', ['-sS', '-L', '--max-time', '60', '-X', req.metodo, '-o', '-', '-w', '\n%{http_code}', req.url], { maxBuffer: 256 * 1048576 });
  if (r.status !== 0) return { status: 0, corpo: r.stderr, cab: '' };
  const fim = r.stdout.lastIndexOf(10);
  return { status: parseInt(r.stdout.subarray(fim + 1).toString(), 10) || 0, corpo: new Uint8Array(r.stdout.subarray(0, fim)), cab: '' };
}

function ligar(imagem) {
  return criarNucleo({
    wasm: fs.readFileSync(path.join(WEB, 'tnux.wasm')),
    cfg: { cpus: 4, mem_mb: 2048, host: 'tnux', plataforma: 'Node (teste)', agente: 'node', fuso_min: -180,
           catalogo: fs.readFileSync(path.join(WEB, 'catalogo.json'), 'utf8') },
    imagem,
    escrever(sid, bytes) { telas[sid] = (telas[sid] || '') + Buffer.from(bytes).toString('utf8'); },
    gui(m) { ligar.gui.push(m); },
    http,
    dormir() {},
  });
}
ligar.gui = [];

function confere(nome, ok, detalhe) {
  feitos++;
  if (ok) return;
  falhas++;
  console.log(`FALHOU: ${nome}\n        ${String(detalhe).split('\n').join('\n        ')}`);
}
function cmd(k, comando, esperado, root) {
  const r = k.rodar(comando, root);
  const ok = esperado instanceof RegExp ? esperado.test(r.saida) : r.saida === esperado;
  confere(comando, ok, `saída: ${JSON.stringify(r.saida)}\n esperado: ${esperado}`);
  return r;
}

let k = ligar(null);

/* shell e utilitários (a mesma lógica do teste nativo, agora com a libc própria do TNUX) */
cmd(k, 'echo oi $USER em $PWD', 'oi usuario em /home/usuario\n');
cmd(k, 'echo $((7*6)) $(( (2+3) * 4 )) ${HOME#/home/}', '42 20 usuario\n');
cmd(k, 'for i in 1 2 3; do printf "%02d-" $i; done; echo', '01-02-03-\n');
cmd(k, 'printf "%5.2f|%-4s|%x|%5d\\n" 3.14159 ab 255 -42', ' 3.14|ab  |ff|  -42\n');
cmd(k, 'f() { echo "arg=$1"; return 4; }; f x; echo "rc=$?"', 'arg=x\nrc=4\n');
cmd(k, 'echo "c\nb\na" > t.txt; sort t.txt | tr "\\n" " "; wc -l < t.txt', 'a b c 3\n');
cmd(k, 'seq 1 100 | grep -c 7', '19\n');
cmd(k, 'echo "nome: Ana, idade: 30" | sed "s/[0-9]+/XX/" | cut -d, -f2', ' idade: XX\n');
cmd(k, 'mkdir -p a/b && touch a/b/x.c a/y.c && find a -name "*.c" | sort | xargs', 'a/b/x.c a/y.c\n');
cmd(k, 'ls /usr/bin | wc -l', /^1\d\d\n$/);
cmd(k, 'cat /etc/os-release | grep ^ID=', 'ID=tnux\n');
cmd(k, 'echo x > /etc/proibido', /Permissão negada/);
cmd(k, 'sudo sh -c "echo x > /etc/permitido"; cat /etc/permitido', 'x\n');
cmd(k, 'date +%Y | grep -c "^20"', '1\n');
cmd(k, 'neofetch | grep -c TNUX', /^[1-9]/);
cmd(k, 'x=5; while [ $x -gt 0 ]; do x=$((x-2)); done; echo $x', '-1\n');
cmd(k, 'case arquivo.tar.gz in *.zip) echo zip;; *.tar.*) echo tar;; esac', 'tar\n');
cmd(k, 'echo "muito texto" | base64 | base64 -d', 'muito texto\n');
cmd(k, 'naoexiste', /comando não encontrado/);
cmd(k, 'chrome', /sudo apt install chrome/);

/* muita memória: a libc própria precisa crescer o heap sem se perder */
cmd(k, 'seq 1 200000 > grande.txt; wc -l < grande.txt; tail -n 1 grande.txt; sort -rn grande.txt | head -n 1', '200000\n200000\n200000\n');
cmd(k, 'for i in $(seq 1 300); do echo "linha $i" >> muitos.txt; cp muitos.txt copia$i.txt; done; ls copia*.txt | wc -l; rm copia*.txt; ls | grep -c copia', '300\n0\n');

/* pacotes */
cmd(k, 'apt install vaca', /você é root/);
cmd(k, 'apt install vaca', /A configurar vaca/, true);
cmd(k, 'vaca Muuu | head -n 2 | tail -n 1', '< Muuu >\n');
cmd(k, 'echo do pipe | vaca | grep -c "do pipe"', '1\n');
cmd(k, 'apt install ola primos && cd /tmp && ola Gustavo && cat ola.txt', /Olá, Gustavo![\s\S]*Recebi 1 argumento[\s\S]*Li de volta do arquivo ola.txt: gravado pelo programa ola\ngravado pelo programa ola\n$/, true);
cmd(k, 'primos 100 | tail -n 1', /^Há 25 números primos até 100/);
cmd(k, 'primos 5000000 | tail -n 1', /^Há 348513 números primos/);
cmd(k, 'apt list --installed | grep -c "^vaca/"', '1\n');
cmd(k, 'apt install chrome', /programa Linux nativo[\s\S]*modo ponte/, true);
cmd(k, 'apt install photopea && cat /usr/share/applications/photopea.desktop | grep X-TNUX', /X-TNUX-Tipo=web\nX-TNUX-Alvo=https:\/\/www.photopea.com\//, true);
confere('apt avisa a área de trabalho', ligar.gui.some((m) => m.acao === 'apps_mudou'), JSON.stringify(ligar.gui));
confere('menu tem os apps internos e o instalado', (() => { const a = k.apps(); return a.some((x) => x.id === 'terminal' && x.tipo === 'interno') && a.some((x) => x.id === 'photopea'); })(), JSON.stringify(k.apps()));
cmd(k, 'apt remove vaca && which vaca; echo "rc=$?"', /A remover vaca[\s\S]*rc=1\n$/, true);
cmd(k, 'apt remove terminal', /essencial/, true);
cmd(k, 'apt search navegador | grep -c /aurora', /^[2-9]\n$/);

/* arquivos pela interface da área de trabalho */
k.fsGravar('/home/usuario/Documentos/nota.txt', 'conteúdo com acento\n');
confere('fsLer', Buffer.from(k.fsLer('/home/usuario/Documentos/nota.txt')).toString() === 'conteúdo com acento\n', '');
confere('fsListar', k.fsListar('/home/usuario/Documentos').some((e) => e.nome === 'nota.txt' && e.tipo === 'f' && e.tam === 21), JSON.stringify(k.fsListar('/home/usuario/Documentos')));
let negou = false;
try { k.fsGravar('/etc/x', 'a'); } catch (e) { negou = /Permissão negada/.test(e.message); }
confere('fsGravar respeita permissão', negou, '');

/* terminal interativo: prompt, edição, Tab, histórico, linha de continuação */
k.sessaoAbrir(1, 80, 24);
confere('prompt', /usuario@tnux\x1b\[00m:\x1b\[01;34m~\x1b\[00m\$ /.test(telas[1]), JSON.stringify(telas[1]));
telas[1] = '';
k.entrada(1, 'echo ola\x7fá mundo\r');
confere('edição com apagar e acento', telas[1].includes('\r\nolá mundo\r\n'), JSON.stringify(telas[1]));
telas[1] = '';
k.entrada(1, 'cd Doc\t\r');
k.entrada(1, 'pwd\r');
confere('Tab completa pastas', telas[1].includes('/home/usuario/Documentos\r\n'), JSON.stringify(telas[1]));
telas[1] = '';
k.entrada(1, 'for i in a b\r');
confere('linha de continuação', /\r\x1b\[0K> \r\x1b\[2C$/.test(telas[1]), JSON.stringify(telas[1]));
k.entrada(1, 'do echo item-$i; done\r');
confere('comando de várias linhas', telas[1].includes('item-a\r\nitem-b\r\n'), JSON.stringify(telas[1]));
telas[1] = '';
k.entrada(1, '\x1b[A\x1b[A\r');      /* duas vezes seta para cima: volta ao "pwd" */
confere('histórico com as setas', telas[1].includes('/home/usuario/Documentos\r\n') && telas[1].includes('for i in a b; do echo item-$i; done'), JSON.stringify(telas[1]));
telas[1] = '';
k.entrada(1, 'ls --color=auto /\r');
confere('ls colorido no terminal', telas[1].includes('\x1b[01;34mhome\x1b[0m'), JSON.stringify(telas[1]));
ligar.gui.length = 0;
k.entrada(1, 'xed nota.txt\r');
confere('xed abre o editor', ligar.gui.some((m) => m.acao === 'abrir' && m.app === 'editor' && m.arg === '/home/usuario/Documentos/nota.txt'), JSON.stringify(ligar.gui));
k.entrada(1, 'exit\r');
confere('exit fecha o terminal', ligar.gui.some((m) => m.acao === 'fechar_terminal' && m.sid === 1), JSON.stringify(ligar.gui));

/* o disco sobrevive a "fechar a aba": salva a imagem e liga de novo com ela */
confere('disco marcado como alterado', k.sujo(), '');
const img = k.imagem();
confere('imagem tem o formato do TNUX', Buffer.from(img.subarray(0, 8)).toString() === 'TNUXFS1\n', '');
k = ligar(img);
cmd(k, 'cat ~/Documentos/nota.txt; wc -l < ~/grande.txt; ola voltou | head -n 1; tail -n 1 ~/.bash_history', 'conteúdo com acento\n200000\nOlá, voltou! Sou um programa em C rodando dentro do TNUX (WebAssembly).\nexit\n');
cmd(k, 'apt list --installed | grep -c "^photopea/"', '1\n');

/* rede de verdade: GitHub */
if (!semRede) {
  const r = cmd(k, 'cd /tmp && git clone https://github.com/Gustavo-HVF/Termux-Toons lnux 2>&1 | tail -n 1; ls lnux | grep -c instalar.sh; head -c 19 lnux/instalar.sh; echo; cd lnux && git remote -v | head -n 1',
    /pronto\.\n1\n#!\/usr\/bin\/env bash\norigin\thttps:\/\/github.com\/Gustavo-HVF\/Termux-Toons \(fetch\)\n$/);
  if (/pronto/.test(r.saida)) {
    cmd(k, 'apt install lnux-som 2>&1 | tail -n 1; ls /opt/lnux-som/apps/som/index.html /opt/lnux-som/comum/lnux.js; ls /opt/lnux-som/apps | wc -l', /A configurar lnux-som[\s\S]*\/opt\/lnux-som\/apps\/som\/index.html\n\/opt\/lnux-som\/comum\/lnux.js\n1\n$/, true);
    confere('app do GitHub entra no menu', k.apps().some((x) => x.id === 'lnux-som' && x.tipo === 'arquivo' && x.alvo === '/opt/lnux-som/apps/som/index.html'), JSON.stringify(k.apps()));
    cmd(k, 'curl -s https://raw.githubusercontent.com/Gustavo-HVF/Termux-Toons/HEAD/.gitignore | head -n 1', /\S/);
  }
  /* (alguns proxies respondem 403 no lugar do 404 do GitHub) */
  cmd(k, 'git clone https://github.com/Gustavo-HVF/este-repositorio-nao-existe-123; echo "rc=$?"', /(não encontrado|erro 403)[\s\S]*rc=128\n$/);
}
cmd(k, 'git clone https://gitlab.com/a/b', /só clona do GitHub/);

const m = k.memoria();
console.log(`${feitos - falhas}/${feitos} testes do núcleo WebAssembly passaram (memória do núcleo: ${(m.usado / 1048576).toFixed(1)} de ${(m.total / 1048576).toFixed(0)} MiB)`);
process.exit(falhas ? 1 : 0);
