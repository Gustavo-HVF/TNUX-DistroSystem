#!/usr/bin/env node
/*
 * Testa a área de trabalho num navegador de verdade (Chromium, pelo Playwright):
 * boot, menu, terminal, instalação de programas, arquivos, editor, páginas servidas do disco
 * virtual e a persistência do disco entre recarregamentos. Depois repete o essencial no modo
 * ponte (com o tnuxd), incluindo o terminal real e a tela VNC, se o Xvnc estiver instalado.
 *
 *     node tests/testar_navegador.js [--fotos PASTA]
 */
'use strict';
const http = require('http');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn, spawnSync } = require('child_process');
const { chromium } = require('playwright');

const RAIZ = path.join(__dirname, '..');
const WEB = path.join(RAIZ, 'web');
const iF = process.argv.indexOf('--fotos');
const FOTOS = iF > 0 ? process.argv[iF + 1] : null;
const TIPOS = { html: 'text/html; charset=utf-8', js: 'text/javascript', css: 'text/css', json: 'application/json', wasm: 'application/wasm', svg: 'image/svg+xml', webmanifest: 'application/manifest+json' };
let falhas = 0, feitos = 0;

function confere(nome, ok, detalhe) {
  feitos++;
  if (ok) return;
  falhas++;
  console.log(`FALHOU: ${nome}${detalhe !== undefined ? '\n        ' + String(detalhe).slice(0, 600).split('\n').join('\n        ') : ''}`);
}
function servidorEstatico() {
  return new Promise((ok) => {
    const s = http.createServer((req, res) => {
      const cam = path.normalize(decodeURIComponent(req.url.split('?')[0])).replace(/^(\.\.[/\\])+/, '');
      const arq = path.join(WEB, cam === '/' || cam === '\\' ? 'index.html' : cam);
      if (!arq.startsWith(WEB) || !fs.existsSync(arq) || fs.statSync(arq).isDirectory()) { res.writeHead(404); res.end('nao'); return; }
      res.writeHead(200, { 'Content-Type': TIPOS[arq.split('.').pop()] || 'application/octet-stream', 'Cache-Control': 'no-store' });
      fs.createReadStream(arq).pipe(res);
    });
    s.listen(0, '127.0.0.1', () => ok(s));
  });
}
const foto = async (pagina, nome) => { if (FOTOS) { fs.mkdirSync(FOTOS, { recursive: true }); await pagina.screenshot({ path: path.join(FOTOS, nome + '.png') }); } };

/* texto visível do terminal da janela ativa */
const telaTerminal = (pagina) => pagina.evaluate(() => {
  const j = [...document.querySelectorAll('.jan:not(.min)')].sort((a, b) => b.style.zIndex - a.style.zIndex).find((x) => x.querySelector('.xterm-rows'));
  return j ? [...j.querySelectorAll('.xterm-rows > div')].map((d) => d.textContent).join('\n') : '';
});
async function esperaTerminal(pagina, trecho, ms = 15000) {
  const fim = Date.now() + ms;
  let t = '';
  while (Date.now() < fim) {
    t = await telaTerminal(pagina);
    if (trecho instanceof RegExp ? trecho.test(t) : t.includes(trecho)) return t;
    await pagina.waitForTimeout(100);
  }
  return t;
}
async function digitar(pagina, texto) {
  await pagina.keyboard.type(texto, { delay: 2 });
  await pagina.keyboard.press('Enter');
}
async function abrirPeloMenu(pagina, nome) {
  await pagina.click('#btn-menu');
  await pagina.fill('#menu-busca', nome);
  await pagina.keyboard.press('Enter');
}
const titulos = (pagina) => pagina.evaluate(() => [...document.querySelectorAll('.jan .jan-titulo .nome')].map((e) => e.textContent));

async function modoNavegador(navegador) {
  const srv = await servidorEstatico();
  const url = `http://localhost:${srv.address().port}/`;
  const ctx = await navegador.newContext({ viewport: { width: 1280, height: 800 } });
  const pagina = await ctx.newPage();
  const erros = [];
  pagina.on('pageerror', (e) => erros.push('erro na página: ' + e.message));
  pagina.on('console', (m) => { if (m.type() === 'error' && !/favicon|Failed to load resource/.test(m.text())) erros.push('console: ' + m.text()); });

  await pagina.goto(url);
  await pagina.waitForSelector('#boot', { state: 'detached', timeout: 30000 });
  confere('boot: área de trabalho e painel aparecem', await pagina.isVisible('#painel') && await pagina.isVisible('#btn-menu'));
  confere('boot: modo navegador', await pagina.evaluate(() => Sistema.modo) === 'navegador');
  confere('boas-vindas abre na primeira vez', (await titulos(pagina)).includes('Bem-vindo ao TNUX'), await titulos(pagina));
  confere('ícones na área de trabalho', await pagina.locator('.icone-area').count() >= 2);
  await foto(pagina, '01-boas-vindas');
  await pagina.click('.jan .fechar');

  /* menu */
  await pagina.click('#btn-menu');
  confere('menu abre com categorias e programas', await pagina.locator('#menu-cats button').count() >= 5 && await pagina.locator('#menu-apps button').count() >= 10);
  await foto(pagina, '02-menu');
  await pagina.fill('#menu-busca', 'calc');
  confere('busca do menu', (await pagina.locator('#menu-apps button').allTextContents()).join('|') === 'Calculadora', await pagina.locator('#menu-apps button').allTextContents());
  await pagina.keyboard.press('Enter');
  confere('Enter abre o programa achado', (await titulos(pagina)).includes('Calculadora'), await titulos(pagina));
  await pagina.keyboard.type('12,5*(3+1)%');
  await pagina.keyboard.press('Enter');
  confere('calculadora', (await pagina.textContent('.calc-visor span')) === '0,5', await pagina.textContent('.calc-visor span'));
  await pagina.click('.jan:not(.inativa) .fechar');

  /* terminal */
  await abrirPeloMenu(pagina, 'Terminal');
  let t = await esperaTerminal(pagina, 'usuario@tnux:~$');
  confere('terminal: prompt', t.includes('usuario@tnux:~$'), t);
  await digitar(pagina, 'echo $((6*7)) && uname -o && ls /');
  t = await esperaTerminal(pagina, 'home');
  confere('terminal: comandos', t.includes('42') && t.includes('TNUX/Linux') && /\bhome\b/.test(t), t);
  await digitar(pagina, 'sudo apt install vaca && vaca Muuu do navegador');
  t = await esperaTerminal(pagina, '< Muuu do navegador >');
  confere('apt instala um programa WebAssembly e ele roda', t.includes('A configurar vaca') && t.includes('< Muuu do navegador >'), t);
  await foto(pagina, '03-terminal');
  await digitar(pagina, 'clear; mkdir -p ~/site && echo "<h1 id=t>Pagina do disco</h1><script src=a.js></script>" > ~/site/index.html && echo "document.title=\'ok-\'+document.getElementById(\'t\').textContent" > ~/site/a.js && echo feito');
  await esperaTerminal(pagina, 'feito');
  await digitar(pagina, 'xdg-open ~/site/index.html');
  await pagina.waitForSelector('iframe.nav-quadro', { timeout: 10000 });
  const quadro = pagina.frameLocator('iframe.nav-quadro');
  await quadro.locator('#t').waitFor({ timeout: 10000 });
  confere('página do disco virtual abre numa janela, com o script dela', await quadro.locator('#t').textContent() === 'Pagina do disco' &&
    await pagina.evaluate(() => document.querySelector('iframe.nav-quadro').contentDocument.title) === 'ok-Pagina do disco');
  await pagina.click('.jan:not(.inativa) .fechar');

  /* editor pelo terminal, salvando */
  await pagina.click('.term');
  await digitar(pagina, 'xed ~/Documentos/nota.txt');
  await pagina.waitForSelector('.ed-area');
  await pagina.fill('.ed-area', 'escrito no editor\n');
  await pagina.keyboard.press('Control+s');
  await pagina.waitForFunction(() => [...document.querySelectorAll('.jan .nome')].some((e) => e.textContent.startsWith('nota.txt')), null, { timeout: 8000 });
  await pagina.click('.jan:not(.inativa) .fechar');
  await pagina.click('.term');
  await digitar(pagina, 'clear; cat ~/Documentos/nota.txt');
  t = await esperaTerminal(pagina, 'escrito no editor');
  confere('editor grava o arquivo', t.includes('escrito no editor'), t);

  /* arquivos */
  await abrirPeloMenu(pagina, 'Arquivos');
  await pagina.waitForSelector('.arq-item');
  let nomes = await pagina.locator('.arq-lista .arq-item span').allTextContents();
  confere('arquivos: pasta pessoal', nomes.includes('Documentos') && nomes.includes('Downloads') && nomes.includes('site'), nomes);
  await pagina.dblclick('.arq-item[data-nome="Documentos"]');
  await pagina.waitForSelector('.arq-item[data-nome="nota.txt"]');
  confere('arquivos: entra na pasta', (await pagina.inputValue('.caminho')) === '/home/usuario/Documentos');
  await pagina.click('.arq-item[data-nome="nota.txt"]', { button: 'right' });
  await pagina.click('.ctx button:has-text("Renomear")');
  await pagina.fill('.dlg .campo', 'renomeada.txt');
  await pagina.keyboard.press('Enter');
  await pagina.waitForSelector('.arq-item[data-nome="renomeada.txt"]');
  confere('arquivos: renomear', await pagina.locator('.arq-item[data-nome="nota.txt"]').count() === 0);
  await foto(pagina, '04-arquivos');
  await pagina.click('.jan:not(.inativa) .fechar');

  /* gerenciador de aplicativos */
  await abrirPeloMenu(pagina, 'Gerenciador');
  await pagina.waitForSelector('.pacote');
  const cartoes = await pagina.locator('.pacote b').allTextContents();
  confere('loja: lista o catálogo', cartoes.includes('Vaca') && cartoes.includes('Google Chrome'), cartoes);
  confere('loja: marca o que está instalado', await pagina.locator('.pacote:has(b:text-is("Vaca")) .etq.ok').count() === 1);
  confere('loja: programa nativo explica o modo ponte', await pagina.locator('.pacote:has(b:text-is("Google Chrome")) button:has-text("Como instalar")').count() === 1);
  await foto(pagina, '05-loja');
  await pagina.click('.jan:not(.inativa) .fechar');

  /* configurações: tema escuro */
  await abrirPeloMenu(pagina, 'Configura');
  await pagina.click('.pag button:has-text("Escuro")');
  confere('tema escuro', await pagina.evaluate(() => document.documentElement.dataset.tema) === 'escuro');
  await foto(pagina, '06-config-escuro');
  await pagina.click('.pag button:has-text("Claro")');
  await pagina.click('.jan:not(.inativa) .fechar');

  /* o disco sobrevive a recarregar a página */
  await pagina.evaluate(() => Sistema.disco.salvar());
  await pagina.reload();
  await pagina.waitForSelector('#boot', { state: 'detached', timeout: 30000 });
  const r = await pagina.evaluate(() => Sistema.rodar('cat ~/Documentos/renomeada.txt; which vaca; tail -n 1 ~/.bash_history'));
  confere('disco persiste entre recarregamentos', r.saida === 'escrito no editor\n/usr/bin/vaca\nclear; cat ~/Documentos/nota.txt\n', JSON.stringify(r.saida));
  confere('sem erros no console (modo navegador)', erros.length === 0, erros.join('\n'));

  /* celular: janelas maximizadas e teclas extras */
  const cel = await navegador.newContext({ viewport: { width: 390, height: 780 }, hasTouch: true, isMobile: true });
  const p2 = await cel.newPage();
  await p2.goto(url);
  await p2.waitForSelector('#boot', { state: 'detached', timeout: 30000 });
  await p2.tap('.jan .fechar');
  await p2.tap('#btn-menu');
  await p2.fill('#menu-busca', 'Terminal');
  await p2.keyboard.press('Enter');
  await p2.waitForSelector('.term .xterm-rows');
  confere('celular: janela maximizada', await p2.evaluate(() => document.querySelector('.jan').classList.contains('max')));
  confere('celular: teclas extras do terminal', await p2.isVisible('.teclas button'));
  await foto(p2, '07-celular');
  await cel.close();
  await ctx.close();
  srv.close();
}

async function modoPonte(navegador) {
  const casa = fs.mkdtempSync(path.join(os.tmpdir(), 'tnux-ponte-'));
  fs.mkdirSync(path.join(casa, 'Documentos'));
  fs.writeFileSync(path.join(casa, 'Documentos', 'do-aparelho.txt'), 'arquivo de verdade\n');
  const livre = await new Promise((ok) => { const s = http.createServer().listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => ok(p)); }); });
  const token = 'chave-do-teste-de-navegador-0123456789';
  const amb = { ...process.env, HOME: casa, TNUX_RAIZ: RAIZ };
  delete amb.DISPLAY; delete amb.WAYLAND_DISPLAY;
  const ponte = spawn(path.join(RAIZ, 'build', 'tnuxd'), ['--porta', String(livre), '--token', token], { env: amb, stdio: 'ignore' });
  await new Promise((ok) => setTimeout(ok, 500));
  const ctx = await navegador.newContext({ viewport: { width: 1280, height: 800 } });
  const pagina = await ctx.newPage();
  const erros = [];
  pagina.on('pageerror', (e) => erros.push('erro na página: ' + e.message));
  try {
    await pagina.goto(`http://127.0.0.1:${livre}/#token=${token}`);
    await pagina.waitForSelector('#boot', { state: 'detached', timeout: 30000 });
    confere('ponte: modo ponte reconhecido e chave tirada do endereço', await pagina.evaluate(() => Sistema.modo) === 'ponte' && !pagina.url().includes('token'), pagina.url());
    await pagina.click('.jan .fechar');
    await abrirPeloMenu(pagina, 'Terminal');
    let t = await esperaTerminal(pagina, 'modo ponte');
    confere('ponte: terminal é o shell do aparelho', t.includes('modo ponte'), t);
    await digitar(pagina, 'cat ~/Documentos/do-aparelho.txt && echo "do navegador" > ~/criado.txt && uname -s');
    t = await esperaTerminal(pagina, 'Linux');
    confere('ponte: comandos rodam no aparelho', t.includes('arquivo de verdade') && fs.existsSync(path.join(casa, 'criado.txt')), t);
    await foto(pagina, '08-ponte-terminal');
    await abrirPeloMenu(pagina, 'Arquivos');
    await pagina.waitForSelector('.arq-item[data-nome="criado.txt"]');
    confere('ponte: arquivos do aparelho', await pagina.locator('.arq-item[data-nome="Documentos"]').count() === 1);
    await pagina.dblclick('.arq-item[data-nome="criado.txt"]');
    await pagina.waitForSelector('.ed-area');
    await pagina.waitForFunction(() => document.querySelector('.ed-area').value.includes('do navegador'), null, { timeout: 8000 });
    await pagina.fill('.ed-area', 'editado pela area de trabalho\n');
    await pagina.keyboard.press('Control+s');
    await pagina.waitForFunction(() => [...document.querySelectorAll('.jan .nome')].some((e) => e.textContent.startsWith('criado.txt')), null, { timeout: 8000 });
    confere('ponte: editor grava no aparelho', fs.readFileSync(path.join(casa, 'criado.txt'), 'utf8') === 'editado pela area de trabalho\n');
    await pagina.click('.jan:not(.inativa) .fechar');
    await pagina.click('.jan:not(.inativa) .fechar');

    if (spawnSync('sh', ['-c', 'command -v Xvnc || command -v Xtigervnc']).status === 0 && spawnSync('sh', ['-c', 'command -v xterm']).status === 0) {
      await abrirPeloMenu(pagina, 'Tela do aparelho');
      await pagina.click('.tela-palco button:has-text("Ligar a tela")');
      await pagina.waitForSelector('canvas.tela-vnc:not([hidden])', { timeout: 20000 });
      const tam = await pagina.evaluate(() => { const c = document.querySelector('canvas.tela-vnc'); return [c.width, c.height]; });
      confere('ponte: a tela do aparelho aparece', tam[0] >= 800 && tam[1] >= 560, tam);
      /* abre um programa gráfico de verdade (xterm) na tela e confere que ele desenhou algo */
      const antes = await pagina.evaluate(() => { const c = document.querySelector('canvas.tela-vnc'); const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let s = 0; for (let i = 0; i < d.length; i += 97) s += d[i]; return s; });
      await pagina.evaluate(async () => { const it = Desktop.apps.find((a) => a.tipo === 'nativo' && /xterm/i.test(a.alvo) && !/uxterm/i.test(a.alvo)); await Sistema.apps.lancar(it.alvo, 'tela'); });
      let mudou = false;
      for (let i = 0; i < 60 && !mudou; i++) {
        await pagina.waitForTimeout(250);
        const agora = await pagina.evaluate(() => { const c = document.querySelector('canvas.tela-vnc'); const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let s = 0; for (let i = 0; i < d.length; i += 97) s += d[i]; return s; });
        mudou = agora !== antes;
      }
      confere('ponte: programa gráfico do aparelho aparece dentro do TNUX', mudou);
      /* digita no programa pela tela e confere o efeito no aparelho */
      await pagina.waitForTimeout(1500);
      const c = await pagina.locator('canvas.tela-vnc').boundingBox();
      await pagina.mouse.click(c.x + c.width / 2, c.y + c.height / 2);
      await pagina.keyboard.type('echo pela-tela > ~/da-tela.txt', { delay: 15 });
      await pagina.keyboard.press('Enter');
      let ok = false;
      for (let i = 0; i < 40 && !ok; i++) { await pagina.waitForTimeout(250); ok = fs.existsSync(path.join(casa, 'da-tela.txt')); }
      confere('ponte: teclado e mouse chegam ao programa gráfico', ok && fs.readFileSync(path.join(casa, 'da-tela.txt'), 'utf8').trim() === 'pela-tela', ok ? fs.readFileSync(path.join(casa, 'da-tela.txt'), 'utf8') : 'arquivo não criado');
      await foto(pagina, '09-ponte-tela');
    } else console.log('(Xvnc ou xterm ausentes: pulei o teste da tela)');
    confere('sem erros na página (modo ponte)', erros.length === 0, erros.join('\n'));
  } finally {
    await ctx.close();
    ponte.kill('SIGTERM');
    await new Promise((ok) => setTimeout(ok, 300));
    fs.rmSync(casa, { recursive: true, force: true });
  }
}

(async () => {
  const navegador = await chromium.launch();
  try {
    await modoNavegador(navegador);
    if (fs.existsSync(path.join(RAIZ, 'build', 'tnuxd'))) await modoPonte(navegador);
    else console.log('(build/tnuxd não existe: pulei o modo ponte; rode "make")');
  } catch (e) {
    falhas++;
    console.log('FALHOU com exceção: ' + (e && e.stack || e));
  } finally { await navegador.close(); }
  console.log(`${feitos - falhas}/${feitos} testes da área de trabalho passaram`);
  process.exit(falhas ? 1 : 0);
})();
