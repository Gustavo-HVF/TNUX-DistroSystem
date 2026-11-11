/*
 * TNUX: a ligação da área de trabalho com o sistema.
 *
 * A área de trabalho não sabe (nem precisa saber) onde o sistema está rodando. Ela fala com este
 * objeto, que tem duas implementações por baixo:
 *
 *   modo "navegador"  o núcleo em C (WebAssembly) dentro de um Web Worker; disco no IndexedDB
 *   modo "ponte"      o aparelho de verdade, pelo servidor tnuxd (terminal real, arquivos reais)
 *
 * O modo ponte é escolhido sozinho quando a página é aberta pelo endereço que o tnuxd mostra
 * (com #token=...).
 */
'use strict';

const Sistema = (() => {
  const enc = new TextEncoder(), dec = new TextDecoder();
  const base = new URL('.', location.href).href;
  const ouvintes = new Map();
  let modo = 'navegador', info = {}, token = null, catalogo = null;

  /* os programas da própria área de trabalho (no modo navegador a lista vem do núcleo; aqui é a do modo ponte) */
  const INTERNOS = [
    { id: 'terminal', nome: 'Terminal', resumo: 'Use a linha de comando', icone: 'terminal', categoria: 'Sistema' },
    { id: 'arquivos', nome: 'Arquivos', resumo: 'Acesse e organize seus arquivos', icone: 'pasta', categoria: 'Acessórios' },
    { id: 'editor', nome: 'Editor de Texto', resumo: 'Edite arquivos de texto', icone: 'editor', categoria: 'Acessórios' },
    { id: 'navegador', nome: 'Navegador Web', resumo: 'Navegue na internet', icone: 'navegador', categoria: 'Internet' },
    { id: 'loja', nome: 'Gerenciador de Aplicativos', resumo: 'Instale e remova programas', icone: 'loja', categoria: 'Sistema' },
    { id: 'config', nome: 'Configurações do Sistema', resumo: 'Aparência, papel de parede e modo ponte', icone: 'config', categoria: 'Preferências' },
    { id: 'monitor', nome: 'Monitor do Sistema', resumo: 'Veja memória, disco e janelas abertas', icone: 'monitor', categoria: 'Sistema' },
    { id: 'imagem', nome: 'Visualizador de Imagens', resumo: 'Veja suas imagens', icone: 'imagem', categoria: 'Gráficos' },
    { id: 'calculadora', nome: 'Calculadora', resumo: 'Faça contas', icone: 'calculadora', categoria: 'Acessórios' },
    { id: 'sobre', nome: 'Informações do Sistema', resumo: 'Versão do TNUX e dados do aparelho', icone: 'info', categoria: 'Preferências' },
    { id: 'boasvindas', nome: 'Bem-vindo', resumo: 'Primeiros passos no TNUX', icone: 'tnux', categoria: 'Sistema' },
  ];

  function emitir(evento, dado) { for (const f of ouvintes.get(evento) || []) { try { f(dado); } catch (e) { console.error(e); } } }
  function ouvir(evento, f) { if (!ouvintes.has(evento)) ouvintes.set(evento, []); ouvintes.get(evento).push(f); }

  /* aspas simples para o shell: o texto vira um argumento só, sem nada interpretado */
  const q = (s) => "'" + String(s).replace(/'/g, "'\\''") + "'";
  const juntar = (dir, nome) => (dir === '/' ? '/' : dir.replace(/\/+$/, '') + '/') + nome;
  const pastaDe = (cam) => { const i = cam.replace(/\/+$/, '').lastIndexOf('/'); return i <= 0 ? '/' : cam.slice(0, i); };
  const nomeDe = (cam) => cam.replace(/\/+$/, '').split('/').pop() || '/';

  /* ================================================================ modo navegador: o núcleo */
  let worker = null, seq = 1, sidSeq = 1, gravaDisco = true, primeiroBoot = false;
  const pend = new Map();
  const ttys = new Map();

  function cfgBoot() {
    const ua = navigator.userAgent;
    const nav = /Firefox\//.test(ua) ? 'Firefox' : /Edg\//.test(ua) ? 'Edge' : /OPR\//.test(ua) ? 'Opera' : /Chrome\//.test(ua) ? 'Chrome' : /Safari\//.test(ua) ? 'Safari' : 'Navegador';
    const so = /Android/.test(ua) ? 'Android' : /iPhone|iPad/.test(ua) ? 'iOS' : /Windows/.test(ua) ? 'Windows' : /Mac OS X/.test(ua) ? 'macOS' : /Linux/.test(ua) ? 'Linux' : '';
    return {
      cpus: navigator.hardwareConcurrency || 1,
      mem_mb: Math.round((navigator.deviceMemory || 2) * 1024),
      agente: ua,
      plataforma: nav + (so ? ' no ' + so : ''),
      idioma: navigator.language || 'pt-BR',
      host: 'tnux',
      fuso_min: -new Date().getTimezoneOffset(),
      catalogo: catalogo ? JSON.stringify(catalogo) : undefined,
    };
  }

  function ligarNucleo() {
    return new Promise((ok, erro) => {
      worker = new Worker(new URL('js/nucleo-worker.js', base));
      worker.onerror = (e) => erro(new Error('o núcleo não carregou: ' + (e.message || 'erro no worker')));
      worker.onmessage = (ev) => {
        const m = ev.data;
        if (m.t === 'pronto') { primeiroBoot = m.primeiro; ok(); }
        else if (m.t === 'falhou') erro(new Error(m.msg));
        else if (m.t === 'tty') { const t = ttys.get(m.sid); if (t) t.aoDado(m.d); }
        else if (m.t === 'gui') aoGui(m.m);
        else if (m.t === 'salvo') emitir('salvo', m.tam);
        else if (m.t === 'erro_disco') emitir('erro_disco', m.msg);
        else if (m.t === 'resp') {
          const p = pend.get(m.id);
          if (!p) return;
          pend.delete(m.id);
          if (m.ok) p.ok(m.r); else p.erro(new Error(m.erro));
        }
      };
      worker.postMessage({ t: 'boot', cfg: cfgBoot(), base, gravar: gravaDisco });
    });
  }
  function chamar(fn, ...args) {
    return new Promise((ok, erro) => {
      if (!worker) { erro(new Error('o núcleo está desligado')); return; }
      const id = seq++;
      pend.set(id, { ok, erro, fn, desde: performance.now() });
      worker.postMessage({ t: 'chamar', id, fn, args });
    });
  }
  function aoGui(m) {
    if (m.acao === 'fechar_terminal') { const t = ttys.get(m.sid); if (t) { ttys.delete(m.sid); t.aoFechar(); } return; }
    if (m.acao === 'apps_mudou') { emitir('apps'); return; }
    emitir('gui', m);
  }
  /* há quanto tempo (ms) o núcleo está preso num comando; 0 se está livre */
  function ocupadoHa() {
    let mais = 0;
    for (const p of pend.values()) if (p.fn === 'entrada' || p.fn === 'rodar') mais = Math.max(mais, performance.now() - p.desde);
    return mais;
  }
  /* o núcleo não pode ser interrompido no meio de um comando: a saída é desligá-lo e ligá-lo de novo
   * (volta com o disco da última gravação) */
  async function reiniciarNucleo() {
    if (worker) worker.terminate();
    worker = null;
    for (const p of pend.values()) p.erro(new Error('o núcleo foi reiniciado'));
    pend.clear();
    await ligarNucleo();
    for (const [sid, t] of ttys) {
      t.aoDado(enc.encode('\r\n\x1b[33m^C  O comando foi interrompido e o núcleo reiniciou (o que ainda não tinha sido gravado se perdeu).\x1b[0m\r\n'));
      await chamar('sessaoAbrir', sid, t.cols, t.rows);
    }
    emitir('apps');
  }

  async function rodar(cmd, root) {
    if (modo !== 'navegador') throw new Error('só no modo navegador');
    return chamar('rodar', cmd, !!root);
  }
  async function rodarOk(cmd, root) {
    const r = await rodar(cmd, root);
    if (r.codigo !== 0) throw new Error(r.saida.trim().split('\n').pop() || 'o comando falhou');
    return r.saida;
  }

  /* ================================================================ modo ponte */
  async function api(metodo, cam, corpo) {
    const r = await fetch(new URL(cam, base), { method: metodo, headers: { 'X-TNUX-Token': token }, body: corpo });
    if (r.status === 401) throw new Error('a ponte não aceitou a chave: abra de novo o endereço que o "tnux desktop" mostrou');
    return r;
  }
  async function apiJson(metodo, cam, corpo) {
    const r = await api(metodo, cam, corpo);
    let j = null;
    try { j = await r.json(); } catch (e) { /* resposta sem JSON */ }
    if (!r.ok) throw new Error((j && j.erro) || 'erro ' + r.status);
    return j;
  }
  const P = (cam) => encodeURIComponent(cam);

  /* ================================================================ interface comum */
  const fs = {
    get casa() { return modo === 'ponte' ? info.casa : '/home/usuario'; },
    juntar, pastaDe, nomeDe,
    async listar(cam) {
      const lista = modo === 'ponte' ? await apiJson('GET', 'api/fs/listar?cam=' + P(cam)) : await chamar('fsListar', cam);
      return lista;
    },
    async info(cam) {
      if (modo === 'ponte') { try { return await apiJson('GET', 'api/fs/info?cam=' + P(cam)); } catch (e) { return null; } }
      return chamar('fsInfo', cam);
    },
    async ler(cam) {
      if (modo === 'ponte') {
        const r = await api('GET', 'api/fs/ler?cam=' + P(cam));
        if (!r.ok) { let j = null; try { j = await r.json(); } catch (e) { /* */ } throw new Error((j && j.erro) || 'erro ' + r.status); }
        return new Uint8Array(await r.arrayBuffer());
      }
      return chamar('fsLer', cam);
    },
    async lerTexto(cam) { return dec.decode(await fs.ler(cam)); },
    async gravar(cam, dados, root) {
      const bytes = typeof dados === 'string' ? enc.encode(dados) : dados;
      if (modo === 'ponte') { await apiJson('POST', 'api/fs/gravar?cam=' + P(cam), bytes); return; }
      await chamar('fsGravar', cam, bytes, !!root);
    },
    async mkdir(cam) {
      if (modo === 'ponte') { await apiJson('POST', 'api/fs/mkdir?cam=' + P(cam)); return; }
      await rodarOk('mkdir -- ' + q(cam));
    },
    async remover(cam) {
      if (modo === 'ponte') { await apiJson('POST', 'api/fs/remover?cam=' + P(cam)); return; }
      await rodarOk('rm -r -- ' + q(cam));
    },
    async mover(de, para) {
      if (modo === 'ponte') { await apiJson('POST', `api/fs/mover?de=${P(de)}&para=${P(para)}`); return; }
      if (await fs.info(para)) throw new Error('já existe um item com esse nome');
      await rodarOk(`mv -- ${q(de)} ${q(para)}`);
    },
    async copiar(de, para) {
      if (modo === 'ponte') { await apiJson('POST', `api/fs/copiar?de=${P(de)}&para=${P(para)}`); return; }
      if (await fs.info(para)) throw new Error('já existe um item com esse nome');
      await rodarOk(`cp -r -- ${q(de)} ${q(para)}`);
    },
    /* endereço para <img>, <video> e <audio>. null se não houver como (aí use fs.ler + Blob). */
    url(cam) {
      const partes = cam.split('/').map(encodeURIComponent).join('/');
      if (modo === 'ponte') return new URL('arq' + partes + '?token=' + encodeURIComponent(token), base).href;
      return vfsPronto ? new URL('vfs' + partes, base).href : null;
    },
  };

  const terminal = {
    /* abre um terminal: devolve { escrever, redimensionar, fechar }. cmd (opcional) roda logo de início. */
    abrir({ cols, rows, aoDado, aoFechar, cmd }) {
      if (modo === 'ponte') {
        const u = new URL('ws/pty', base);
        u.protocol = u.protocol === 'https:' ? 'wss:' : 'ws:';
        u.search = `?token=${encodeURIComponent(token)}&cols=${cols}&rows=${rows}` + (cmd ? '&cmd=' + encodeURIComponent(cmd) : '');
        const ws = new WebSocket(u);
        ws.binaryType = 'arraybuffer';
        const espera = [];
        let aberto = false, fechado = false;
        ws.onopen = () => { aberto = true; for (const d of espera) ws.send(d); espera.length = 0; };
        ws.onmessage = (e) => { if (typeof e.data !== 'string') aoDado(new Uint8Array(e.data)); };
        ws.onclose = () => { if (!fechado) { fechado = true; aoFechar(); } };
        ws.onerror = () => { if (!aberto) aoDado(enc.encode('\r\n\x1b[31mNão foi possível abrir um terminal no aparelho (a ponte está ligada?).\x1b[0m\r\n')); };
        const mandar = (d) => { if (aberto) { if (ws.readyState === 1) ws.send(d); } else espera.push(d); };
        return {
          escrever: (d) => mandar(typeof d === 'string' ? enc.encode(d) : d),
          redimensionar: (c, l) => mandar(JSON.stringify({ t: 'r', c, l })),
          fechar: () => { fechado = true; try { ws.close(); } catch (e) { /* já fechado */ } },
        };
      }
      const sid = sidSeq++;
      ttys.set(sid, { aoDado, aoFechar, cols, rows });
      chamar('sessaoAbrir', sid, cols, rows).then(() => { if (cmd) chamar('entrada', sid, cmd + '\r').catch(() => {}); }).catch(() => {});
      return {
        escrever: (d) => {
          /* Ctrl+C com o núcleo preso num comando há mais de 2 s: reinicia o núcleo */
          const s = typeof d === 'string' ? d : dec.decode(d);
          if (s === '\x03' && ocupadoHa() > 2000) { reiniciarNucleo().catch((e) => emitir('erro', e.message)); return; }
          chamar('entrada', sid, d).catch(() => {});
        },
        redimensionar: (c, l) => { const t = ttys.get(sid); if (t) { t.cols = c; t.rows = l; } chamar('redimensionar', sid, c, l).catch(() => {}); },
        fechar: () => { if (ttys.delete(sid)) chamar('sessaoFechar', sid).catch(() => {}); },
      };
    },
  };

  const apps = {
    /* tudo o que aparece no menu: { id, nome, resumo, icone, categoria, tipo, alvo } */
    async listar() {
      if (modo === 'navegador') return chamar('apps');
      const lista = INTERNOS.map((a) => ({ ...a, tipo: 'interno', alvo: a.id }));
      lista.push({ id: 'tela', nome: 'Tela do aparelho', resumo: 'Veja e use os programas gráficos do aparelho (Chromium, Firefox, GIMP...)', icone: 'tela', categoria: 'Sistema', tipo: 'interno', alvo: 'tela' });
      try {
        const j = await apiJson('GET', 'api/apps');
        for (const w of j.web) {
          if (w.url) lista.push({ id: w.id, nome: w.nome, resumo: w.resumo || '', icone: w.icone || 'web', categoria: w.categoria || 'Outros', tipo: 'web', alvo: w.url });
          else lista.push({ id: w.id, nome: w.nome, resumo: w.resumo || '', icone: w.icone || 'web', categoria: w.categoria || 'Outros', tipo: 'appponte', alvo: w.id + '/' + (w.entrada || 'index.html') });
        }
        for (const n of j.nativos) lista.push({ id: 'nativo:' + n.id, nome: n.nome, resumo: n.resumo || '', icone: 'app', icone_sis: n.icone_sis, categoria: n.categoria || 'Outros', tipo: 'nativo', alvo: n.id, terminal: n.terminal });
      } catch (e) { emitir('erro', 'não consegui listar os programas do aparelho: ' + e.message); }
      return lista;
    },
    /* endereço de um app web instalado no aparelho (modo ponte): outro host, para ficar isolado do TNUX */
    urlAppPonte(alvo) {
      const u = new URL(base);
      const outro = u.hostname === '127.0.0.1' ? 'localhost' : u.hostname === 'localhost' ? '127.0.0.1' : null;
      const isolado = !!outro;
      if (outro) u.hostname = outro;
      u.pathname = '/c/' + info.chave_apps + '/' + alvo.split('/').map(encodeURIComponent).join('/');
      u.search = ''; u.hash = '';
      return { url: u.href, isolado };
    },
    urlIcone(nome) { return new URL('api/icone?nome=' + encodeURIComponent(nome) + '&token=' + encodeURIComponent(token), base).href; },
    /* abre um programa nativo do aparelho (modo ponte) */
    async lancar(id, onde) { return apiJson('POST', 'api/lancar?id=' + encodeURIComponent(id) + (onde ? '&onde=' + onde : '')); },
  };

  const pacotes = {
    async catalogo() { return catalogo || { pacotes: [], categorias: [] }; },
    /* conjunto com os ids instalados */
    async instalados() {
      const s = new Set();
      if (modo === 'navegador') {
        const r = await rodar('apt list --installed');
        for (const l of r.saida.split('\n')) { const m = l.match(/^([^/\s]+)\/aurora /); if (m) s.add(m[1]); }
        return s;
      }
      const cat = (catalogo && catalogo.pacotes) || [];
      const execs = cat.filter((p) => p.tipo === 'nativo' && p.nativo && p.nativo.exec).map((p) => p.nativo.exec);
      try {
        const tem = execs.length ? await apiJson('GET', 'api/tem?cmd=' + encodeURIComponent(execs.join(','))) : {};
        for (const p of cat) if (p.tipo === 'nativo' && p.nativo && tem[p.nativo.exec]) s.add(p.id);
        const j = await apiJson('GET', 'api/apps');
        for (const w of j.web) s.add(w.id);
      } catch (e) { /* sem resposta da ponte: lista vazia */ }
      return s;
    },
    /* a linha de comando que instala ou remove (o Gerenciador de Aplicativos a roda num terminal) */
    comando(acao, id) {
      if (modo === 'ponte') return (acao === 'instalar' ? 'tnux instalar ' : 'tnux remover ') + id;
      return (acao === 'instalar' ? 'sudo apt install ' : 'sudo apt remove ') + id;
    },
    disponivel(p) {
      if (modo === 'ponte') return p.tipo !== 'wasm' && p.tipo !== 'script';
      return p.tipo !== 'nativo';
    },
  };

  const tela = {
    estado: () => apiJson('GET', 'api/tela/estado'),
    iniciar: (l, a) => apiJson('POST', `api/tela/iniciar?l=${l}&a=${a}`),
    parar: () => apiJson('POST', 'api/tela/parar'),
    urlWs() {
      const u = new URL('ws/tela', base);
      u.protocol = u.protocol === 'https:' ? 'wss:' : 'ws:';
      u.search = '?token=' + encodeURIComponent(token);
      return u.href;
    },
  };

  const disco = {
    get grava() { return modo === 'navegador' && gravaDisco; },
    salvar: () => chamar('salvar'),
    exportar: () => chamar('exportar'),
    async importar(bytes) { await chamar('importar', bytes); },
    async redefinir() { await chamar('redefinir'); },
    memoria: () => chamar('memoria'),
  };

  /* ---------------------------------------------------------------- arquivos do disco virtual como endereços (service worker) */
  let vfsPronto = false;
  async function ligarVfs() {
    if (!('serviceWorker' in navigator) || !window.isSecureContext) return;
    try {
      await navigator.serviceWorker.register(new URL('sw.js', base));
      navigator.serviceWorker.addEventListener('message', async (ev) => {
        const m = ev.data;
        if (!m || m.t !== 'vfs-ler' || !ev.ports[0]) return;
        try {
          const inf = await fs.info(m.cam);
          if (inf && inf.tipo === 'd') { ev.ports[0].postMessage({ ok: false, dir: true }); return; }
          const d = await fs.ler(m.cam);
          ev.ports[0].postMessage({ ok: true, dados: d.buffer }, [d.buffer]);
        } catch (e) { ev.ports[0].postMessage({ ok: false }); }
      });
      await navigator.serviceWorker.ready;
      vfsPronto = true;
    } catch (e) { console.warn('service worker indisponível:', e); }
  }

  /* ---------------------------------------------------------------- início */
  async function iniciar(passo) {
    const m = location.hash.match(/token=([A-Za-z0-9._~-]+)/);
    if (m) {
      sessionStorage.setItem('tnux_token', m[1]);
      history.replaceState(null, '', location.pathname + location.search);   /* tira a chave da barra de endereço */
    }
    token = sessionStorage.getItem('tnux_token');
    passo('Lendo o catálogo de programas');
    try { catalogo = await (await fetch(new URL('catalogo.json', base), { cache: 'no-cache' })).json(); } catch (e) { catalogo = { pacotes: [], categorias: [] }; }
    if (token) {
      passo('Conectando ao aparelho');
      try {
        const r = await fetch(new URL('api/info', base), { headers: { 'X-TNUX-Token': token } });
        if (r.ok) { info = await r.json(); modo = 'ponte'; }
      } catch (e) { /* não é o tnuxd: segue no modo navegador */ }
      if (modo !== 'ponte') { sessionStorage.removeItem('tnux_token'); token = null; }
    }
    if (modo === 'ponte') return;
    /* só uma aba grava o disco: duas abas gravando por cima uma da outra perderiam arquivos */
    if (navigator.locks) {
      gravaDisco = await new Promise((ok) => {
        navigator.locks.request('tnux-disco', { ifAvailable: true }, (trava) => {
          if (!trava) { ok(false); return undefined; }
          ok(true);
          return new Promise(() => {});      /* segura a trava enquanto a aba existir */
        }).catch(() => ok(true));
      });
    }
    passo('Ligando o núcleo');
    await ligarNucleo();
    passo('Montando o disco');
    await ligarVfs();
    if (navigator.storage && navigator.storage.persist) navigator.storage.persist().catch(() => {});
    info = { modo: 'navegador', usuario: 'usuario', host: 'tnux', casa: '/home/usuario', sistema: 'TNUX 1.0 Aurora', ...cfgBoot(), catalogo: undefined };
  }

  return {
    iniciar, ouvir, q,
    get modo() { return modo; },
    get info() { return info; },
    get primeiroBoot() { return primeiroBoot; },
    get vfs() { return vfsPronto; },
    fs, terminal, apps, pacotes, tela, disco, rodar, rodarOk, reiniciarNucleo, ocupadoHa,
    versao: '1.0', codinome: 'Aurora',
  };
})();
