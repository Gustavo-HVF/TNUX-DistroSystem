/*
 * TNUX: programas básicos da área de trabalho: Terminal, Arquivos, Editor de Texto, o diálogo
 * de escolher arquivo, as janelas de apps web e a abertura de programas nativos (modo ponte).
 */
'use strict';

/* ================================================================== Terminal */
const TEMA_TERMINAL = {
  background: '#1d2326', foreground: '#e4ebee', cursor: '#e4ebee', cursorAccent: '#1d2326', selectionBackground: '#4d6a78',
  black: '#2e3436', red: '#e5484d', green: '#5fd38a', yellow: '#e9c46a', blue: '#5ab0ff', magenta: '#c58af9', cyan: '#4dd0e1', white: '#d3d7cf',
  brightBlack: '#6b767b', brightRed: '#ff7b7b', brightGreen: '#8af0ad', brightYellow: '#ffe08a', brightBlue: '#8cc8ff', brightMagenta: '#ddb3ff', brightCyan: '#8be9f5', brightWhite: '#ffffff',
};

/* arg: { titulo, cmd (roda e espera Enter: instalações), digitar (texto digitado no prompt) } */
Apps.terminal = (arg) => {
  const op = arg && typeof arg === 'object' ? arg : {};
  const jan = WM.abrir({ titulo: op.titulo || 'Terminal', icone: Icones.app('terminal'), largura: 780, altura: 480 });
  const caixa = h('div.term'), teclas = h('div.teclas');
  jan.corpo.append(caixa, teclas);
  const term = new Terminal({
    fontFamily: getComputedStyle(document.documentElement).getPropertyValue('--mono'), fontSize: Cfg.dados.fonteTerm, cursorBlink: true,
    scrollback: 8000, theme: TEMA_TERMINAL, allowProposedApi: true, macOptionIsMeta: true,
  });
  const ajuste = new FitAddon.FitAddon();
  term.loadAddon(ajuste);
  term.open(caixa);
  try { ajuste.fit(); } catch (e) { /* janela ainda sem tamanho */ }

  let ctrl = false, alt = false, digitou = !op.digitar;
  const sessao = Sistema.terminal.abrir({
    cols: term.cols, rows: term.rows, cmd: op.cmd,
    aoDado: (d) => {
      term.write(d);
      if (!digitou) { digitou = true; setTimeout(() => sessao.escrever(op.digitar + '\r'), 250); }
    },
    aoFechar: () => jan.fechar(true),
  });
  const soltar = () => { ctrl = alt = false; for (const b of teclas.querySelectorAll('.preso')) b.classList.remove('preso'); };
  term.onData((d) => {
    if (ctrl && d.length === 1) { const c = d.toUpperCase().charCodeAt(0); if (c >= 64 && c <= 95) d = String.fromCharCode(c - 64); else if (d === ' ') d = '\0'; }
    if (alt && d.length === 1) d = '\x1b' + d;
    if (ctrl || alt) soltar();
    sessao.escrever(d);
  });
  term.onResize(({ cols, rows }) => sessao.redimensionar(cols, rows));
  term.onTitleChange((t) => { if (t && !op.titulo) jan.titulo(t.length > 60 ? '…' + t.slice(-58) : t); });
  term.attachCustomKeyEventHandler((e) => {
    if (e.type !== 'keydown' || !e.ctrlKey || !e.shiftKey) return true;
    if (e.code === 'KeyC') { const s = term.getSelection(); if (s && navigator.clipboard) navigator.clipboard.writeText(s).catch(() => {}); return false; }
    if (e.code === 'KeyV') { if (navigator.clipboard) navigator.clipboard.readText().then((t) => term.paste(t)).catch(() => {}); return false; }
    return true;
  });
  caixa.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    Ctx.abrir(e.clientX, e.clientY, [
      { rotulo: 'Copiar', icone: 'copiar', desabilitado: !term.hasSelection(), acao: () => navigator.clipboard && navigator.clipboard.writeText(term.getSelection()).catch(() => {}) },
      { rotulo: 'Colar', icone: 'colar', acao: () => navigator.clipboard && navigator.clipboard.readText().then((t) => term.paste(t)).catch(() => Avisos.mostrar('Colar', 'O navegador não deixou ler a área de transferência. Use Ctrl+Shift+V.', 'atencao')) },
      '-',
      { rotulo: 'Limpar a tela', icone: 'apagar', acao: () => { term.clear(); term.focus(); } },
      { rotulo: 'Novo terminal', icone: 'novo', acao: () => Apps.terminal() },
    ]);
  });

  /* teclas extras para telas de toque (como as do Termux) */
  const T = [['Esc', '\x1b'], ['Tab', '\t'], ['Ctrl', 'ctrl'], ['Alt', 'alt'], ['↑', '\x1b[A'], ['↓', '\x1b[B'], ['←', '\x1b[D'], ['→', '\x1b[C'],
    ['Home', '\x1b[H'], ['End', '\x1b[F'], ['/', '/'], ['-', '-'], ['|', '|'], ['~', '~']];
  for (const [rot, seq] of T) {
    const b = h('button', { type: 'button', tabIndex: -1 }, rot);
    b.addEventListener('pointerdown', (e) => {
      e.preventDefault();          /* não tira o foco (nem o teclado da tela) do terminal */
      if (seq === 'ctrl') { ctrl = !ctrl; b.classList.toggle('preso', ctrl); }
      else if (seq === 'alt') { alt = !alt; b.classList.toggle('preso', alt); }
      else { let d = seq; if (ctrl && d.length === 1) d = String.fromCharCode(d.toUpperCase().charCodeAt(0) & 31); sessao.escrever(d); soltar(); }
    });
    teclas.append(b);
  }

  const ro = new ResizeObserver(() => { if (caixa.clientWidth > 20 && caixa.clientHeight > 20) { try { ajuste.fit(); } catch (e) { /* */ } } });
  ro.observe(caixa);
  jan.aoFocar = () => term.focus();
  jan.aoFechou = () => { ro.disconnect(); sessao.fechar(); term.dispose(); };
  term.focus();
  return jan;
};

/* ================================================================== abrir um arquivo no programa certo */
const EXT = {
  imagem: ['png', 'jpg', 'jpeg', 'gif', 'webp', 'svg', 'bmp', 'ico', 'avif'],
  video: ['mp4', 'webm', 'mkv', 'mov', 'm4v', 'ogv'],
  audio: ['mp3', 'ogg', 'wav', 'flac', 'm4a', 'opus', 'oga'],
  pagina: ['html', 'htm'],
  executa: ['sh', 'bash', 'wasm'],
};
const extensao = (nome) => (nome.includes('.') ? nome.split('.').pop().toLowerCase() : '');

Apps.abrirArquivo = async (cam) => {
  const e = extensao(cam), nome = Sistema.fs.nomeDe(cam);
  try {
    if (EXT.imagem.includes(e)) return Apps.imagem(cam);
    if (EXT.video.includes(e) || EXT.audio.includes(e)) return Apps.midia(cam);
    if (EXT.pagina.includes(e)) return Apps.web({ vfs: cam, nome });
    if (e === 'desktop' && Sistema.modo === 'navegador') {
      const txt = await Sistema.fs.lerTexto(cam);
      const id = nome.replace(/\.desktop$/, '');
      const it = Desktop.apps.find((a) => a.id === id);
      if (it) return Desktop.abrir(it);
      if (/^X-TNUX-/m.test(txt)) { await Desktop.atualizarApps(); const it2 = Desktop.apps.find((a) => a.id === id); if (it2) return Desktop.abrir(it2); }
    }
    const inf = await Sistema.fs.info(cam);
    if (!inf) throw new Error('o arquivo não existe mais');
    if (inf.tam > 3 * 1048576) {
      const baixar = await Dlg.confirma(nome, `Este arquivo tem ${tamanhoHumano(inf.tam)} e o TNUX não tem um programa para abri-lo.\nQuer salvá-lo no seu aparelho?`, { sim: 'Salvar no aparelho' });
      if (baixar) await Apps.baixar(cam);
      return null;
    }
    return Apps.editor(cam);
  } catch (err) { Avisos.erro('Não foi possível abrir ' + nome, err); return null; }
};

/* salva um arquivo do TNUX no aparelho de quem está usando o navegador */
Apps.baixar = async (cam) => {
  const dados = await Sistema.fs.ler(cam);
  const url = URL.createObjectURL(new Blob([dados]));
  const a = h('a', { href: url, download: Sistema.fs.nomeDe(cam) });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 30000);
};

/* ================================================================== Arquivos */
const AreaTransf = { modo: null, itens: [] };     /* copiar/recortar entre janelas do gerenciador */

function lugares() {
  const casa = Sistema.fs.casa, j = Sistema.fs.juntar;
  const l = [
    { nome: 'Pasta pessoal', cam: casa, icone: 'casa' },
    { nome: 'Área de Trabalho', cam: j(casa, 'Área de Trabalho'), icone: 'pasta' }, { nome: 'Área de Trabalho', cam: j(casa, 'Desktop'), icone: 'pasta' },
    { nome: 'Documentos', cam: j(casa, 'Documentos'), icone: 'pasta' }, { nome: 'Documentos', cam: j(casa, 'Documents'), icone: 'pasta' },
    { nome: 'Downloads', cam: j(casa, 'Downloads'), icone: 'pasta' },
    { nome: 'Imagens', cam: j(casa, 'Imagens'), icone: 'pasta' }, { nome: 'Imagens', cam: j(casa, 'Pictures'), icone: 'pasta' },
    { nome: 'Música', cam: j(casa, 'Música'), icone: 'pasta' }, { nome: 'Música', cam: j(casa, 'Music'), icone: 'pasta' },
    { nome: 'Vídeos', cam: j(casa, 'Vídeos'), icone: 'pasta' }, { nome: 'Vídeos', cam: j(casa, 'Videos'), icone: 'pasta' },
  ];
  if (Sistema.modo === 'ponte') l.push({ nome: 'Armazenamento do celular', cam: j(casa, 'storage/shared'), icone: 'pasta' }, { nome: 'Apps do TNUX', cam: j(casa, '.tnux/apps'), icone: 'pasta' });
  else l.push({ nome: 'Programas (/opt)', cam: '/opt', icone: 'pasta' });
  return l;
}

/* monta a lista de arquivos de uma pasta dentro de "caixa"; usado pelo gerenciador e pelo diálogo de escolher */
async function listarPasta(cam, ocultos) {
  const itens = await Sistema.fs.listar(cam);
  return itens.filter((i) => ocultos || !i.nome.startsWith('.'))
    .sort((a, b) => (a.tipo === b.tipo ? a.nome.localeCompare(b.nome, 'pt-BR', { numeric: true }) : a.tipo === 'd' ? -1 : 1));
}

Apps.arquivos = async (inicio) => {
  const fs = Sistema.fs;
  let cam = typeof inicio === 'string' && inicio ? inicio : fs.casa;
  const hist = [], futuro = [];
  let itens = [], sel = new Set();
  const jan = WM.abrir({ titulo: 'Arquivos', icone: Icones.app('pasta'), largura: 820, altura: 520 });

  const campo = h('input.campo.caminho', { type: 'text', spellcheck: false, autocomplete: 'off', 'aria-label': 'Caminho' });
  const btVoltar = h('button.btn.liso', { title: 'Voltar', html: Icones.ui('voltar'), onclick: () => { if (hist.length) { futuro.push(cam); ir(hist.pop(), true); } } });
  const btAvancar = h('button.btn.liso', { title: 'Avançar', html: Icones.ui('avancar'), onclick: () => { if (futuro.length) { hist.push(cam); ir(futuro.pop(), true); } } });
  const btModo = h('button.btn.liso', { title: 'Lista ou ícones', onclick: () => { Cfg.definir('lista', !Cfg.dados.lista); desenhar(); } });
  const btOcultos = h('button.btn.liso', { title: 'Mostrar arquivos ocultos', html: Icones.ui('olho'), onclick: () => { Cfg.definir('ocultos', !Cfg.dados.ocultos); recarregar(); } });
  const entrada = h('input', { type: 'file', multiple: true, hidden: true });
  const barra = h('div.barra', null, btVoltar, btAvancar,
    h('button.btn.liso', { title: 'Pasta acima', html: Icones.ui('cima'), onclick: () => ir(fs.pastaDe(cam)) }),
    h('button.btn.liso', { title: 'Pasta pessoal', html: Icones.ui('inicio'), onclick: () => ir(fs.casa) }),
    campo,
    h('button.btn.liso', { title: 'Atualizar', html: Icones.ui('recarregar'), onclick: () => recarregar() }),
    h('div.sep'),
    h('button.btn.liso', { title: 'Nova pasta', html: Icones.ui('pasta_nova'), onclick: () => novaPasta() }),
    h('button.btn.liso', { title: 'Enviar arquivos do seu aparelho para esta pasta', html: Icones.ui('enviar'), onclick: () => entrada.click() }),
    btModo, btOcultos, entrada);
  const lado = h('div.arq-lado');
  const listaEl = h('div.arq-lista', { tabIndex: 0 });
  const rodape = h('div.rodape');
  jan.corpo.append(barra, h('div.arq', null, lado, listaEl), rodape);

  campo.addEventListener('keydown', (e) => { if (e.key === 'Enter') ir(campo.value.trim() || '/'); else if (e.key === 'Escape') campo.value = cam; });
  entrada.addEventListener('change', () => { enviar([...entrada.files]); entrada.value = ''; });

  async function ir(novo, semHist) {
    novo = novo.replace(/\/+$/, '') || '/';
    if (novo.startsWith('~')) novo = fs.casa + novo.slice(1);
    try {
      const l = await listarPasta(novo, Cfg.dados.ocultos);
      if (!semHist && novo !== cam) { hist.push(cam); futuro.length = 0; }
      cam = novo; itens = l; sel = new Set();
      desenhar();
    } catch (e) {
      campo.value = cam;
      /* era um arquivo? então abre */
      const inf = await fs.info(novo);
      if (inf && inf.tipo === 'f') Apps.abrirArquivo(novo); else Avisos.erro('Não foi possível abrir a pasta', e);
    }
  }
  const recarregar = () => ir(cam, true);

  function desenhar() {
    campo.value = cam;
    jan.titulo((cam === fs.casa ? 'Pasta pessoal' : fs.nomeDe(cam)) + ' — Arquivos');
    btVoltar.disabled = !hist.length; btAvancar.disabled = !futuro.length;
    btModo.innerHTML = Icones.ui(Cfg.dados.lista ? 'grade' : 'lista');
    listaEl.classList.toggle('linhas', !!Cfg.dados.lista);
    const els = itens.map((it) => {
      const b = h('button.arq-item' + (it.nome.startsWith('.') ? '.oculto' : '') + (sel.has(it.nome) ? '.sel' : ''), { dataset: { nome: it.nome }, tabIndex: -1, html: Icones.arquivo(it.nome, it.tipo) },
        h('span', null, it.nome), h('small', null, it.tipo === 'd' ? 'pasta' : tamanhoHumano(it.tam)));
      b.addEventListener('click', (e) => {
        if (e.ctrlKey || e.metaKey) { if (sel.has(it.nome)) sel.delete(it.nome); else sel.add(it.nome); }
        else if (e.shiftKey && sel.size) {
          const nomes = itens.map((x) => x.nome), a = nomes.indexOf([...sel].pop()), z2 = nomes.indexOf(it.nome);
          for (let i = Math.min(a, z2); i <= Math.max(a, z2); i++) sel.add(nomes[i]);
        } else sel = new Set([it.nome]);
        marcar();
      });
      Desktop.ativavel(b, () => abrirItem(it));
      b.addEventListener('contextmenu', (e) => {
        e.preventDefault(); e.stopPropagation();
        if (!sel.has(it.nome)) { sel = new Set([it.nome]); marcar(); }
        menuItem(e.clientX, e.clientY, it);
      });
      return b;
    });
    listaEl.replaceChildren(...els);
    if (!els.length) listaEl.append(h('div.vazio', { html: Icones.app('pasta') }, h('div', null, 'Esta pasta está vazia.')));
    /* lugares: só os que existem */
    lado.replaceChildren(h('h4', null, 'Lugares'));
    const vistos = new Set();
    for (const l of lugaresOk) {
      if (vistos.has(l.nome)) continue;
      vistos.add(l.nome);
      lado.append(h('button' + (l.cam === cam ? '.sel' : ''), { title: l.cam, onclick: () => ir(l.cam) }, h('span', { html: Icones.app(l.icone), style: 'display:contents' }), h('span', null, l.nome)));
    }
    lado.append(h('h4', null, 'Sistema'), h('button' + (cam === '/' ? '.sel' : ''), { title: '/', onclick: () => ir('/') }, h('span', { html: Icones.app('computador'), style: 'display:contents' }), h('span', null, 'Computador')));
    marcar();
  }
  function marcar() {
    for (const b of listaEl.querySelectorAll('.arq-item')) b.classList.toggle('sel', sel.has(b.dataset.nome));
    const pastas = itens.filter((i) => i.tipo === 'd').length;
    if (sel.size === 1) { const it = itens.find((i) => i.nome === [...sel][0]); rodape.textContent = it ? `"${it.nome}" selecionado` + (it.tipo === 'f' ? ` (${tamanhoHumano(it.tam)})` : ' (pasta)') : ''; }
    else if (sel.size > 1) rodape.textContent = `${sel.size} itens selecionados`;
    else rodape.textContent = `${itens.length} ${itens.length === 1 ? 'item' : 'itens'}` + (pastas ? ` (${pastas} ${pastas === 1 ? 'pasta' : 'pastas'})` : '') + (Sistema.modo === 'ponte' ? '  •  arquivos do aparelho' : '  •  disco do TNUX no navegador');
  }
  const caminhoDe = (nome) => fs.juntar(cam, nome);
  const selecionados = () => [...sel].map(caminhoDe);

  function abrirItem(it) {
    const c = caminhoDe(it.nome);
    if (it.tipo === 'd') ir(c); else Apps.abrirArquivo(c);
  }
  async function novaPasta() {
    const nome = await Dlg.pergunta('Nova pasta', 'Nome da pasta:', 'Nova pasta', { sim: 'Criar' });
    if (!nome) return;
    try { await fs.mkdir(caminhoDe(nome.trim())); await recarregar(); sel = new Set([nome.trim()]); marcar(); } catch (e) { Avisos.erro('Não foi possível criar a pasta', e); }
  }
  async function novoArquivo() {
    const nome = await Dlg.pergunta('Novo arquivo', 'Nome do arquivo:', 'novo.txt', { sim: 'Criar' });
    if (!nome) return;
    try {
      if (await fs.info(caminhoDe(nome.trim()))) throw new Error('já existe um item com esse nome');
      await fs.gravar(caminhoDe(nome.trim()), '');
      await recarregar();
    } catch (e) { Avisos.erro('Não foi possível criar o arquivo', e); }
  }
  async function renomear(it) {
    const novo = await Dlg.pergunta('Renomear', `Novo nome para "${it.nome}":`, it.nome, { sim: 'Renomear' });
    if (!novo || novo === it.nome) return;
    if (novo.includes('/')) { Avisos.mostrar('Nome inválido', 'O nome não pode ter barra (/).', 'atencao'); return; }
    try { await fs.mover(caminhoDe(it.nome), caminhoDe(novo.trim())); await recarregar(); } catch (e) { Avisos.erro('Não foi possível renomear', e); }
  }
  async function apagar() {
    const alvos = selecionados();
    if (!alvos.length) return;
    const ok = await Dlg.confirma('Apagar', alvos.length === 1 ? `Apagar "${fs.nomeDe(alvos[0])}" para sempre?` : `Apagar ${alvos.length} itens para sempre?`, { sim: 'Apagar', perigo: true });
    if (!ok) return;
    for (const a of alvos) { try { await fs.remover(a); } catch (e) { Avisos.erro('Não foi possível apagar ' + fs.nomeDe(a), e); } }
    await recarregar();
    Desktop.montarIcones();
  }
  function guardar(modo) { AreaTransf.modo = modo; AreaTransf.itens = selecionados(); rodape.textContent = `${AreaTransf.itens.length} ${AreaTransf.itens.length === 1 ? 'item' : 'itens'} para ${modo === 'copiar' ? 'copiar' : 'mover'}: abra a pasta de destino e escolha Colar`; }
  async function colar() {
    if (!AreaTransf.itens.length) return;
    for (const de of AreaTransf.itens) {
      let nome = fs.nomeDe(de), para = caminhoDe(nome);
      /* colar na mesma pasta: faz uma cópia com outro nome */
      for (let n = 1; await fs.info(para); n++) {
        if (AreaTransf.modo === 'recortar' && fs.pastaDe(de) === cam) { para = null; break; }
        const p = nome.lastIndexOf('.');
        para = caminhoDe(p > 0 ? `${nome.slice(0, p)} (cópia${n > 1 ? ' ' + n : ''})${nome.slice(p)}` : `${nome} (cópia${n > 1 ? ' ' + n : ''})`);
      }
      if (!para) continue;
      try { if (AreaTransf.modo === 'copiar') await fs.copiar(de, para); else await fs.mover(de, para); }
      catch (e) { Avisos.erro('Não foi possível colar ' + nome, e); }
    }
    if (AreaTransf.modo === 'recortar') AreaTransf.itens = [];
    await recarregar();
    Desktop.montarIcones();
  }
  async function enviar(arquivos) {
    let n = 0;
    for (const a of arquivos) {
      try { await fs.gravar(caminhoDe(a.name), new Uint8Array(await a.arrayBuffer())); n++; }
      catch (e) { Avisos.erro('Não foi possível enviar ' + a.name, e); }
    }
    if (n) Avisos.mostrar('Arquivos enviados', `${n} ${n === 1 ? 'arquivo copiado' : 'arquivos copiados'} para ${cam}`);
    await recarregar();
  }
  function executar(it) {
    const direto = extensao(it.nome) === 'wasm' || (it.modo & 0o111);
    const comando = `cd ${Sistema.q(cam)} && ` + (direto ? './' + Sistema.q(it.nome) : 'bash ' + Sistema.q(it.nome));
    Apps.terminal({ digitar: comando });
  }
  function menuItem(x, y, it) {
    const um = sel.size === 1, c = caminhoDe(it.nome), e = extensao(it.nome);
    Ctx.abrir(x, y, [
      um && { rotulo: 'Abrir', icone: 'abrir', acao: () => abrirItem(it) },
      um && it.tipo === 'f' && { rotulo: 'Abrir no Editor de Texto', icone: 'renomear', acao: () => Apps.editor(c) },
      um && it.tipo === 'f' && EXT.executa.includes(e) && { rotulo: 'Executar no terminal', icone: 'novo', acao: () => executar(it) },
      um && it.tipo === 'd' && { rotulo: 'Abrir terminal aqui', icone: 'novo', acao: () => Apps.terminal({ digitar: 'cd ' + Sistema.q(c) }) },
      '-',
      { rotulo: 'Recortar', icone: 'recortar', acao: () => guardar('recortar') },
      { rotulo: 'Copiar', icone: 'copiar', acao: () => guardar('copiar') },
      um && { rotulo: 'Renomear', icone: 'renomear', acao: () => renomear(it) },
      um && it.tipo === 'f' && { rotulo: 'Salvar no meu aparelho', icone: 'baixar', acao: () => Apps.baixar(c).catch((er) => Avisos.erro('Não foi possível salvar', er)) },
      ...Processador.itensDoMenu(selecionados(), cam, recarregar),
      '-',
      { rotulo: 'Apagar', icone: 'apagar', acao: apagar },
    ]);
  }
  listaEl.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    sel = new Set(); marcar();
    Ctx.abrir(e.clientX, e.clientY, [
      { rotulo: 'Nova pasta', icone: 'pasta_nova', acao: novaPasta },
      { rotulo: 'Novo arquivo de texto', icone: 'novo', acao: novoArquivo },
      { rotulo: 'Colar', icone: 'colar', desabilitado: !AreaTransf.itens.length, acao: colar },
      '-',
      { rotulo: 'Enviar arquivos do meu aparelho', icone: 'enviar', acao: () => entrada.click() },
      { rotulo: 'Abrir terminal aqui', icone: 'novo', acao: () => Apps.terminal({ digitar: 'cd ' + Sistema.q(cam) }) },
      { rotulo: 'Atualizar', icone: 'recarregar', acao: recarregar },
    ]);
  });
  listaEl.addEventListener('click', (e) => { if (e.target === listaEl) { sel = new Set(); marcar(); } });
  listaEl.addEventListener('keydown', (e) => {
    const it = sel.size === 1 ? itens.find((i) => i.nome === [...sel][0]) : null;
    if (e.key === 'Delete') apagar();
    else if (e.key === 'F2' && it) renomear(it);
    else if (e.key === 'Enter' && it) abrirItem(it);
    else if (e.key === 'Backspace') ir(fs.pastaDe(cam));
    else if (e.key === 'F5') { e.preventDefault(); recarregar(); }
    else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'c') guardar('copiar');
    else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'x') guardar('recortar');
    else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'v') colar();
    else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'a') { e.preventDefault(); sel = new Set(itens.map((i) => i.nome)); marcar(); }
  });
  /* soltar arquivos do aparelho em cima da lista envia para a pasta */
  listaEl.addEventListener('dragover', (e) => { if (e.dataTransfer && [...e.dataTransfer.types].includes('Files')) e.preventDefault(); });
  listaEl.addEventListener('drop', (e) => { if (e.dataTransfer && e.dataTransfer.files.length) { e.preventDefault(); enviar([...e.dataTransfer.files]); } });

  /* descobre quais lugares existem (uma vez) */
  const lugaresOk = [];
  await Promise.all(lugares().map(async (l, i) => { const inf = await fs.info(l.cam); if (inf && inf.tipo === 'd') lugaresOk[i] = l; }));
  for (let i = lugaresOk.length - 1; i >= 0; i--) if (!lugaresOk[i]) lugaresOk.splice(i, 1);
  jan.aoFocar = () => listaEl.focus({ preventScroll: true });
  await ir(cam, true);
  return jan;
};

/* ================================================================== diálogo de escolher arquivo */
/* op: { titulo, modo: 'abrir' | 'salvar', inicio (pasta), nome (sugestão) }. Devolve o caminho ou null. */
Apps.escolher = (op) => new Promise((resolver) => {
  const fs = Sistema.fs;
  let cam = op.inicio || fs.casa, itens = [];
  const campoCam = h('input.campo.caminho', { type: 'text', spellcheck: false });
  const campoNome = h('input.campo', { type: 'text', value: op.nome || '', spellcheck: false, placeholder: 'Nome do arquivo', style: 'flex:1' });
  const lista = h('div.arq-lista.linhas', { style: 'flex:1' });
  let d;
  const fim = (v) => { d.fechar(); resolver(v); };
  const confirmar = async () => {
    const nome = campoNome.value.trim();
    if (!nome) return;
    const alvo = nome.startsWith('/') ? nome : fs.juntar(cam, nome);
    const inf = await fs.info(alvo);
    if (inf && inf.tipo === 'd') { ir(alvo); campoNome.value = ''; return; }
    if (op.modo === 'salvar' && inf && !(await Dlg.confirma('Substituir o arquivo?', `"${fs.nomeDe(alvo)}" já existe nesta pasta.`, { sim: 'Substituir', perigo: true }))) return;
    if (op.modo === 'abrir' && !inf) { Avisos.mostrar('Arquivo não encontrado', alvo, 'atencao'); return; }
    fim(alvo);
  };
  async function ir(novo) {
    try { itens = await listarPasta(novo, Cfg.dados.ocultos); cam = novo; } catch (e) { Avisos.erro('Não foi possível abrir a pasta', e); }
    campoCam.value = cam;
    lista.replaceChildren(...itens.map((it) => {
      const b = h('button.arq-item', { html: Icones.arquivo(it.nome, it.tipo) }, h('span', null, it.nome), h('small', null, it.tipo === 'd' ? 'pasta' : tamanhoHumano(it.tam)));
      b.addEventListener('click', () => {
        if (it.tipo === 'd') ir(fs.juntar(cam, it.nome));
        else { campoNome.value = it.nome; for (const x of lista.children) x.classList.remove('sel'); b.classList.add('sel'); }
      });
      b.addEventListener('dblclick', () => { if (it.tipo === 'f') { campoNome.value = it.nome; confirmar(); } });
      return b;
    }));
    if (!itens.length) lista.append(h('div.vazio', null, 'Pasta vazia.'));
  }
  campoCam.addEventListener('keydown', (e) => { if (e.key === 'Enter') ir(campoCam.value.trim() || '/'); });
  campoNome.addEventListener('keydown', (e) => { if (e.key === 'Enter') confirmar(); });
  d = Dlg.abrir([
    h('h3', null, op.titulo || (op.modo === 'salvar' ? 'Salvar como' : 'Abrir')),
    h('div.barra', { style: 'margin-top:8px' },
      h('button.btn.liso', { title: 'Pasta acima', html: Icones.ui('cima'), onclick: () => ir(fs.pastaDe(cam)) }),
      h('button.btn.liso', { title: 'Pasta pessoal', html: Icones.ui('inicio'), onclick: () => ir(fs.casa) }), campoCam),
    lista,
    h('div.pe', null, campoNome, h('button.btn', { onclick: () => fim(null) }, 'Cancelar'), h('button.btn.pri', { onclick: confirmar }, op.modo === 'salvar' ? 'Salvar' : 'Abrir')),
  ], 'largo');
  ir(cam).then(() => { if (op.modo === 'salvar') { campoNome.focus(); const p = campoNome.value.lastIndexOf('.'); campoNome.setSelectionRange(0, p > 0 ? p : campoNome.value.length); } });
});

/* ================================================================== Editor de Texto */
Apps.editor = async (arg) => {
  const fs = Sistema.fs;
  let cam = typeof arg === 'string' && arg ? arg : null;
  let original = '', comoRoot = false;
  const jan = WM.abrir({ titulo: 'Editor de Texto', icone: Icones.app('editor'), largura: 760, altura: 520 });
  const area = h('textarea.ed-area', { spellcheck: false, autocapitalize: 'off', autocomplete: 'off', wrap: 'off', 'aria-label': 'Texto' });
  const rodape = h('div.rodape');
  const btQuebra = h('button.btn.liso', { title: 'Quebrar linhas longas', html: Icones.ui('quebra'), onclick: () => { area.classList.toggle('quebra'); area.wrap = area.classList.contains('quebra') ? 'soft' : 'off'; } });
  jan.corpo.append(h('div.barra', null,
    h('button.btn.liso', { title: 'Novo', html: Icones.ui('novo'), onclick: () => Apps.editor() }),
    h('button.btn.liso', { title: 'Abrir', html: Icones.ui('abrir'), onclick: abrir }),
    h('button.btn.liso', { title: 'Salvar (Ctrl+S)', html: Icones.ui('salvar'), onclick: () => salvar(false) }),
    h('button.btn.liso', { onclick: () => salvar(true) }, 'Salvar como'),
    h('div.sep'), btQuebra), area, rodape);

  const mudou = () => area.value !== original;
  function estado() {
    jan.titulo((mudou() ? '• ' : '') + (cam ? fs.nomeDe(cam) : 'Documento novo') + ' — Editor de Texto');
    const ate = area.value.slice(0, area.selectionStart).split('\n');
    rodape.textContent = `${cam || 'ainda não salvo'}    Lin ${ate.length}, Col ${ate[ate.length - 1].length + 1}` + (comoRoot ? '    (salvando como administrador)' : '');
  }
  async function carregar(c) {
    try {
      const inf = await fs.info(c);
      if (inf && inf.tipo === 'd') throw new Error('é uma pasta');
      if (inf && inf.tam > 3 * 1048576) throw new Error(`o arquivo tem ${tamanhoHumano(inf.tam)}: grande demais para o editor`);
      let texto = '';
      if (inf) {
        const bytes = await fs.ler(c);
        if (bytes.subarray(0, 4096).includes(0)) { if (!(await Dlg.confirma('Arquivo binário', `"${fs.nomeDe(c)}" não parece ser texto. Abrir mesmo assim? (salvar pode estragar o arquivo)`, { sim: 'Abrir assim mesmo' }))) { if (!cam) jan.fechar(true); return; } }
        texto = new TextDecoder().decode(bytes);
      }
      cam = c; original = texto; area.value = texto; comoRoot = false;
      area.setSelectionRange(0, 0);
      estado();
    } catch (e) { Avisos.erro('Não foi possível abrir ' + fs.nomeDe(c), e); if (!cam) estado(); }
  }
  async function abrir() {
    if (mudou() && !(await Dlg.confirma('Descartar as alterações?', 'O texto atual tem alterações que não foram salvas.', { sim: 'Descartar', perigo: true }))) return;
    const c = await Apps.escolher({ modo: 'abrir', inicio: cam ? fs.pastaDe(cam) : fs.casa });
    if (c) carregar(c);
  }
  async function salvar(como) {
    let alvo = cam;
    if (como || !alvo) {
      alvo = await Apps.escolher({ modo: 'salvar', inicio: cam ? fs.pastaDe(cam) : fs.juntar(fs.casa, 'Documentos'), nome: cam ? fs.nomeDe(cam) : 'documento.txt' });
      if (!alvo) return false;
      comoRoot = false;
    }
    try { await fs.gravar(alvo, area.value, comoRoot); }
    catch (e) {
      if (Sistema.modo === 'navegador' && /Permissão negada/.test(e.message) && await Dlg.confirma('Arquivo do sistema', `Você não tem permissão para gravar em\n${alvo}\n\nSalvar como administrador (root)?`, { sim: 'Salvar como administrador' })) {
        try { await fs.gravar(alvo, area.value, true); comoRoot = true; } catch (e2) { Avisos.erro('Não foi possível salvar', e2); return false; }
      } else { Avisos.erro('Não foi possível salvar', e); return false; }
    }
    cam = alvo; original = area.value;
    estado();
    Desktop.montarIcones();
    return true;
  }
  area.addEventListener('input', estado);
  area.addEventListener('keyup', estado);
  area.addEventListener('click', estado);
  area.addEventListener('keydown', (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') { e.preventDefault(); salvar(e.shiftKey); }
    else if (e.key === 'Tab' && !e.ctrlKey && !e.altKey) { e.preventDefault(); area.setRangeText('\t', area.selectionStart, area.selectionEnd, 'end'); estado(); }
  });
  jan.aoFocar = () => area.focus({ preventScroll: true });
  jan.aoFechar = async () => {
    if (!mudou()) return true;
    const r = await Dlg._simples('Salvar as alterações?', `"${cam ? fs.nomeDe(cam) : 'Documento novo'}" tem alterações que não foram salvas.`,
      [{ rotulo: 'Cancelar', valor: 'c' }, { rotulo: 'Fechar sem salvar', valor: 'n', classe: 'perigo' }, { rotulo: 'Salvar', valor: 's', classe: 'pri' }]);
    if (r === 's') return salvar(false);
    return r === 'n';
  };
  estado();
  if (cam) await carregar(cam);
  area.focus();
  return jan;
};

/* ================================================================== janelas de apps web */
/* op: { url | vfs (caminho de um .html no disco), nome, icone, externo (site de fora), isolar } */
Apps.web = async (op) => {
  const jan = WM.abrir({ titulo: op.nome || 'Aplicativo web', icone: op.icone || Icones.app('web'), largura: 960, altura: 620 });
  const quadro = h('iframe.nav-quadro', { title: op.nome || 'Aplicativo web', allow: 'fullscreen; autoplay; clipboard-read; clipboard-write; microphone; camera; display-capture', referrerPolicy: 'no-referrer' });
  let url = op.url, blob = null;
  if (op.vfs) {
    url = Sistema.modo === 'navegador' ? Sistema.fs.url(op.vfs) : null;
    if (!url) {
      /* sem endereço próprio (modo ponte, ou navegador sem service worker): mostra só o arquivo, isolado */
      try {
        const dados = await Sistema.fs.ler(op.vfs);
        blob = URL.createObjectURL(new Blob([dados], { type: 'text/html; charset=utf-8' }));
        url = blob;
        op.isolar = true;
        jan.corpo.append(h('div.nav-aviso', null, h('span', null, 'Mostrando só esta página: imagens e scripts em arquivos separados podem não carregar.')));
      } catch (e) { Avisos.erro('Não foi possível abrir a página', e); jan.fechar(true); return null; }
    }
  }
  if (op.isolar) quadro.sandbox = 'allow-scripts allow-forms allow-popups allow-modals allow-downloads allow-pointer-lock';
  const barra = h('div.barra', null,
    h('button.btn.liso', { title: 'Recarregar', html: Icones.ui('recarregar'), onclick: () => { quadro.src = url; } }),
    h('span.sel-texto', { style: 'flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:12.5px;color:var(--texto-fraco);padding:0 6px' }, op.vfs || op.url),
    !blob && h('button.btn.liso', { title: 'Abrir em uma aba do navegador', html: Icones.ui('externo'), onclick: () => window.open(url, '_blank', 'noopener') }),
    h('button.btn.liso', { title: 'Tela cheia', html: Icones.ui('tela_cheia'), onclick: () => quadro.requestFullscreen && quadro.requestFullscreen().catch(() => {}) }));
  jan.corpo.prepend(barra);
  if (op.externo) jan.corpo.append(h('div.nav-aviso', null, h('span', null, 'Se a página ficar em branco, o site não aceita ser aberto dentro de outra página.'),
    h('button.btn', { onclick: () => window.open(url, '_blank', 'noopener') }, 'Abrir em nova aba')));
  jan.corpo.append(quadro);
  quadro.src = url;
  jan.aoFechou = () => { if (blob) URL.revokeObjectURL(blob); };
  return jan;
};

/* ================================================================== programas nativos do aparelho (modo ponte) */
Apps.nativo = async (it) => {
  if (it.terminal) {
    const r = await Sistema.apps.lancar(it.alvo);
    return Apps.terminal({ titulo: it.nome, cmd: r.exec });
  }
  const tentar = async (onde) => {
    const r = await Sistema.apps.lancar(it.alvo, onde);
    if (r.onde === 'tela') { Apps.tela(); Avisos.mostrar(it.nome, 'Aberto na tela do aparelho (janela "Tela do aparelho").'); }
    else Avisos.mostrar(it.nome, 'Aberto na tela do próprio aparelho.');
  };
  try { await tentar(Sistema.info.tela_propria ? undefined : 'tela'); }
  catch (e) {
    /* sem tela ligada: oferece ligar a do TNUX */
    if (!/tela/.test(e.message)) throw e;
    if (!Sistema.info.tela_disponivel) {
      await Dlg.aviso('Falta uma tela para programas gráficos',
        `"${it.nome}" é um programa com janela, e este aparelho não tem uma tela gráfica ligada.\n\nPara ver esses programas dentro do TNUX, instale o servidor de tela. No Terminal:\n\n    tnux instalar tela\n\nDepois abra "Tela do aparelho" no menu.`);
      return;
    }
    if (!(await Dlg.confirma('Ligar a tela do TNUX?', `"${it.nome}" precisa de uma tela gráfica. Quer ligar a tela do TNUX e abrir o programa nela?`, { sim: 'Ligar e abrir' }))) return;
    await Apps.tela({ ligar: true });
    await tentar('tela');
  }
};
