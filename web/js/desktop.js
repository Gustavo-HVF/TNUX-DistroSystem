/*
 * TNUX: a área de trabalho: janelas, painel, menu de aplicativos, avisos, diálogos e ícones.
 */
'use strict';

const $ = (sel, raiz = document) => raiz.querySelector(sel);
/* cria um elemento: h('button.btn.pri', {title: 'x', onclick: f}, 'Texto', outroElemento) */
function h(desc, props, ...filhos) {
  const [tag, ...classes] = desc.split('.');
  const el = document.createElement(tag || 'div');
  if (classes.length) el.className = classes.join(' ');
  for (const [k, v] of Object.entries(props || {})) {
    if (v === null || v === undefined || v === false) continue;
    if (k === 'html') el.innerHTML = v;
    else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else if (k === 'dataset') Object.assign(el.dataset, v);
    else if (k in el && k !== 'list' && k !== 'form') el[k] = v;
    else el.setAttribute(k, v);
  }
  for (const f of filhos.flat()) if (f !== null && f !== undefined && f !== false) el.append(f);
  return el;
}
const toque = () => matchMedia('(pointer: coarse)').matches;
const telaPequena = () => innerWidth < 700;
function tamanhoHumano(b) {
  if (b < 1024) return b + ' B';
  const u = ['KiB', 'MiB', 'GiB', 'TiB'];
  let i = -1;
  do { b /= 1024; i++; } while (b >= 1024 && i < u.length - 1);
  return (b < 10 ? b.toFixed(1) : Math.round(b)).toString().replace('.', ',') + ' ' + u[i];
}

/* ------------------------------------------------------------------ preferências */
const Cfg = {
  dados: { tema: 'claro', acento: '#35a854', fundo: 'aurora', fundoImg: null, favoritos: ['navegador', 'terminal', 'arquivos', 'loja', 'config'], visto: false, lista: false, ocultos: false, fonteTerm: 14 },
  carregar() {
    try { Object.assign(this.dados, JSON.parse(localStorage.getItem('tnux_cfg') || '{}')); } catch (e) { /* preferências corrompidas: usa as padrão */ }
    this.aplicar();
  },
  salvar() {
    try { localStorage.setItem('tnux_cfg', JSON.stringify(this.dados)); }
    catch (e) { if (this.dados.fundoImg) { this.dados.fundoImg = null; this.dados.fundo = 'aurora'; Avisos.mostrar('Imagem grande demais', 'O navegador não tem espaço para guardar esse papel de parede. Escolha uma imagem menor.', 'atencao'); this.aplicar(); } }
  },
  definir(k, v) { this.dados[k] = v; this.salvar(); this.aplicar(); },
  aplicar() {
    const d = this.dados, raiz = document.documentElement;
    raiz.dataset.tema = d.tema;
    raiz.style.setProperty('--acento', d.acento);
    /* texto claro ou escuro por cima da cor de destaque, conforme o brilho dela */
    const n = parseInt(d.acento.slice(1), 16);
    const brilho = (0.299 * (n >> 16) + 0.587 * ((n >> 8) & 255) + 0.114 * (n & 255)) / 255;
    raiz.style.setProperty('--acento-texto', brilho > 0.62 ? '#1d2326' : '#fff');
    const f = $('#fundo');
    if (f) {
      f.dataset.fundo = d.fundo === 'imagem' && d.fundoImg ? 'imagem' : d.fundo;
      f.style.backgroundImage = d.fundo === 'imagem' && d.fundoImg ? `url("${d.fundoImg}")` : '';
    }
  },
};

/* ------------------------------------------------------------------ avisos (notificações) */
const Avisos = {
  mostrar(titulo, texto, tipo, ms) {
    const el = h('div.aviso' + (tipo ? '.' + tipo : ''), { role: 'status' }, h('div', null, h('b', null, titulo), texto ? h('span', null, texto) : null));
    const tirar = () => el.remove();
    el.addEventListener('click', tirar);
    $('#avisos').append(el);
    setTimeout(tirar, ms || (tipo === 'erro' ? 9000 : 5500));
    while ($('#avisos').children.length > 4) $('#avisos').firstChild.remove();
  },
  erro(titulo, e) { this.mostrar(titulo, String(e && e.message || e || ''), 'erro'); },
};

/* ------------------------------------------------------------------ menu de contexto */
const Ctx = {
  el: null,
  fechar() { if (this.el) { this.el.remove(); this.el = null; } },
  /* itens: { rotulo, icone, acao, desabilitado } ou '-' para uma linha */
  abrir(x, y, itens) {
    this.fechar();
    const el = h('div.ctx', { role: 'menu' });
    for (const it of itens) {
      if (!it) continue;
      if (it === '-') { el.append(h('hr')); continue; }
      el.append(h('button', { role: 'menuitem', disabled: !!it.desabilitado, onclick: () => { this.fechar(); it.acao(); } },
        h('span', { html: it.icone ? Icones.ui(it.icone) : '<svg viewBox="0 0 20 20"></svg>' }), it.rotulo));
    }
    document.body.append(el);
    const r = el.getBoundingClientRect();
    el.style.left = Math.max(4, Math.min(x, innerWidth - r.width - 4)) + 'px';
    el.style.top = Math.max(4, Math.min(y, innerHeight - r.height - 4)) + 'px';
    this.el = el;
  },
};
addEventListener('pointerdown', (e) => { if (Ctx.el && !Ctx.el.contains(e.target)) Ctx.fechar(); }, true);
addEventListener('blur', () => Ctx.fechar());

/* ------------------------------------------------------------------ diálogos */
const Dlg = {
  /* mostra um diálogo por cima de tudo; devolve { el, fechar } */
  abrir(conteudo, classe) {
    const veu = h('div.veu');
    const caixa = h('div.dlg' + (classe ? '.' + classe : ''), { role: 'dialog', 'aria-modal': 'true' }, conteudo);
    veu.append(caixa);
    $('#janelas').append(veu);
    return { el: caixa, fechar: () => veu.remove() };
  },
  _simples(titulo, texto, botoes, campo) {
    return new Promise((ok) => {
      let d;
      const fim = (v) => { d.fechar(); removeEventListener('keydown', tecla, true); ok(v); };
      const entrada = campo !== undefined ? h('input.campo', { type: 'text', value: campo, spellcheck: false, autocomplete: 'off' }) : null;
      const bts = botoes.map((b) => h('button.btn' + (b.classe ? '.' + b.classe : ''), { onclick: () => fim(b.valor === '__campo' ? entrada.value : b.valor) }, b.rotulo));
      const tecla = (e) => {
        if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); fim(botoes[0].valor === '__campo' ? null : botoes[0].valor); }
        else if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); e.stopPropagation(); bts[bts.length - 1].click(); }
      };
      d = this.abrir([h('h3', null, titulo), texto ? h('div.txt', null, texto) : null, entrada, h('div.pe', null, bts)]);
      addEventListener('keydown', tecla, true);
      (entrada || bts[bts.length - 1]).focus();
      if (entrada) { const p = campo.lastIndexOf('.'); entrada.setSelectionRange(0, p > 0 ? p : campo.length); }
    });
  },
  aviso(titulo, texto) { return this._simples(titulo, texto, [{ rotulo: 'OK', valor: true, classe: 'pri' }]); },
  confirma(titulo, texto, op = {}) {
    return this._simples(titulo, texto, [{ rotulo: op.nao || 'Cancelar', valor: false }, { rotulo: op.sim || 'OK', valor: true, classe: op.perigo ? 'perigo' : 'pri' }]);
  },
  pergunta(titulo, texto, valor = '', op = {}) {
    return this._simples(titulo, texto, [{ rotulo: 'Cancelar', valor: null }, { rotulo: op.sim || 'OK', valor: '__campo', classe: 'pri' }], valor);
  },
};

/* ------------------------------------------------------------------ gerenciador de janelas */
const WM = (() => {
  const jans = [];
  let z = 10, conta = 0;
  const area = () => $('#janelas').getBoundingClientRect();

  function ativar(j) {
    for (const o of jans) {
      const at = o === j;
      o.el.classList.toggle('inativa', !at);
      o.botao.classList.toggle('ativa', at);
    }
    if (j) {
      j.el.style.zIndex = ++z;
      j.el.classList.remove('min');
      j.botao.classList.remove('minimizada');
      if (j.aoFocar) j.aoFocar();
    }
  }
  function ativa() { return jans.filter((j) => !j.el.classList.contains('min')).sort((a, b) => b.el.style.zIndex - a.el.style.zIndex)[0] || null; }

  function arrastavel(alca, aoIniciar, aoMover, aoTerminar) {
    alca.addEventListener('pointerdown', (e) => {
      if (e.button !== 0 || e.target.closest('button')) return;
      const dados = aoIniciar(e);
      if (!dados) return;
      e.preventDefault();
      alca.setPointerCapture(e.pointerId);
      document.body.classList.add('arrastando');
      const mover = (ev) => aoMover(ev, dados);
      const fim = () => {
        alca.removeEventListener('pointermove', mover);
        alca.removeEventListener('pointerup', fim);
        alca.removeEventListener('pointercancel', fim);
        document.body.classList.remove('arrastando');
        if (aoTerminar) aoTerminar(dados);
      };
      alca.addEventListener('pointermove', mover);
      alca.addEventListener('pointerup', fim);
      alca.addEventListener('pointercancel', fim);
    });
  }

  /**
   * Abre uma janela. op: { titulo, icone (svg), largura, altura, unica (chave: reaproveita a janela), max, aoFechar }
   * Devolve { el, corpo, titulo(t), focar(), fechar(), aoFocar, aoFechar }
   */
  function abrir(op) {
    if (op.unica) {
      const ja = jans.find((x) => x.unica === op.unica);
      if (ja) { ativar(ja); return ja; }
    }
    const a = area();
    const larg = Math.min(op.largura || 720, a.width), alt = Math.min(op.altura || 480, a.height);
    const passo = conta++ % 7;
    const nome = h('span.nome', null, op.titulo);
    const btMax = h('button', { title: 'Maximizar', html: Icones.ui('max') });
    const corpo = h('div.jan-corpo');
    const barra = h('div.jan-titulo', null, h('span.ico', { html: op.icone || Icones.app('app') }), nome,
      h('div.jan-bot', null,
        h('button', { title: 'Minimizar', html: Icones.ui('min'), onclick: () => j.minimizar() }),
        btMax,
        h('button.fechar', { title: 'Fechar', html: Icones.ui('fechar'), onclick: () => j.fechar() })));
    const el = h('div.jan', { role: 'group', 'aria-label': op.titulo }, barra, corpo);
    el.style.width = larg + 'px';
    el.style.height = alt + 'px';
    el.style.left = Math.max(0, Math.min(a.width - larg, Math.round((a.width - larg) / 2) + (passo - 3) * 26)) + 'px';
    el.style.top = Math.max(0, Math.min(a.height - alt, Math.round((a.height - alt) / 3) + (passo - 3) * 22)) + 'px';
    for (const d of ['n', 's', 'l', 'o', 'no', 'nl', 'so', 'sl']) el.append(h('div.rz.' + d, { dataset: { d } }));

    const botao = h('button.bt-jan', { title: op.titulo }, h('span', { html: op.icone || Icones.app('app') }), h('span', null, op.titulo));
    botao.firstChild.style.display = 'contents';
    const j = {
      el, corpo, botao, unica: op.unica || null, aoFechar: op.aoFechar || null, aoFocar: null, fechada: false,
      titulo(t) { nome.textContent = t; botao.lastChild.textContent = t; botao.title = t; el.setAttribute('aria-label', t); },
      focar() { ativar(j); },
      minimizar() { el.classList.add('min'); botao.classList.add('minimizada'); botao.classList.remove('ativa'); ativar(ativa()); },
      alternarMax() {
        const m = el.classList.toggle('max');
        btMax.innerHTML = Icones.ui(m ? 'restaurar' : 'max');
        btMax.title = m ? 'Restaurar' : 'Maximizar';
      },
      /* fecha a janela. aoFechar pode devolver false (ou uma promessa de false) para impedir. */
      async fechar(forcar) {
        if (j.fechada) return;
        if (!forcar && j.aoFechar) { const pode = await j.aoFechar(); if (pode === false) return; }
        j.fechada = true;
        el.remove(); botao.remove();
        jans.splice(jans.indexOf(j), 1);
        if (j.aoFechou) j.aoFechou();
        ativar(ativa());
      },
    };
    btMax.addEventListener('click', () => j.alternarMax());
    barra.addEventListener('dblclick', (e) => { if (!e.target.closest('button')) j.alternarMax(); });
    el.addEventListener('pointerdown', () => { if (!botao.classList.contains('ativa')) ativar(j); }, true);
    botao.addEventListener('click', () => { if (botao.classList.contains('ativa') && !el.classList.contains('min')) j.minimizar(); else ativar(j); });
    botao.addEventListener('contextmenu', (e) => {
      e.preventDefault();
      Ctx.abrir(e.clientX, e.clientY - 90, [
        { rotulo: el.classList.contains('max') ? 'Restaurar' : 'Maximizar', icone: 'max', acao: () => { ativar(j); j.alternarMax(); } },
        { rotulo: 'Minimizar', icone: 'min', acao: () => j.minimizar() },
        '-', { rotulo: 'Fechar', icone: 'fechar', acao: () => j.fechar() },
      ]);
    });

    /* mover pela barra de título */
    arrastavel(barra, (e) => {
      if (el.classList.contains('max')) return null;
      return { x: e.clientX - el.offsetLeft, y: e.clientY - el.offsetTop };
    }, (e, d) => {
      const r = area();
      el.style.left = Math.max(90 - el.offsetWidth, Math.min(r.width - 90, e.clientX - d.x)) + 'px';
      el.style.top = Math.max(0, Math.min(r.height - 38, e.clientY - d.y)) + 'px';
    });
    /* redimensionar pelas bordas */
    for (const alca of el.querySelectorAll('.rz')) {
      arrastavel(alca, (e) => ({ x: e.clientX, y: e.clientY, l: el.offsetLeft, t: el.offsetTop, w: el.offsetWidth, h: el.offsetHeight, d: alca.dataset.d }),
        (e, d) => {
          const dx = e.clientX - d.x, dy = e.clientY - d.y, r = area();
          let { l, t, w, h: alt2 } = d;
          if (d.d.includes('o')) w = d.w + dx;
          if (d.d.includes('l')) { w = d.w - dx; l = d.l + dx; }
          if (d.d.includes('s')) alt2 = d.h + dy;
          if (d.d.includes('n')) { alt2 = d.h - dy; t = d.t + dy; }
          if (w < 260) { if (d.d.includes('l')) l -= 260 - w; w = 260; }
          if (alt2 < 140) { if (d.d.includes('n')) t -= 140 - alt2; alt2 = 140; }
          if (t < 0) { alt2 += t; t = 0; }
          el.style.left = l + 'px'; el.style.top = t + 'px';
          el.style.width = Math.min(w, r.width + 200) + 'px'; el.style.height = Math.min(alt2, r.height - t) + 'px';
        });
    }

    $('#janelas').append(el);
    $('#lista-janelas').append(botao);
    jans.push(j);
    if (op.max || telaPequena()) j.alternarMax();
    ativar(j);
    return j;
  }

  /* quando a tela muda de tamanho (girar o celular, teclado na tela), traz as janelas de volta para dentro */
  addEventListener('resize', () => {
    const r = area();
    for (const j of jans) {
      if (j.el.classList.contains('max')) continue;
      if (telaPequena()) { j.alternarMax(); continue; }
      if (j.el.offsetWidth > r.width) j.el.style.width = r.width + 'px';
      if (j.el.offsetHeight > r.height) j.el.style.height = r.height + 'px';
      j.el.style.left = Math.max(0, Math.min(j.el.offsetLeft, r.width - 90)) + 'px';
      j.el.style.top = Math.max(0, Math.min(j.el.offsetTop, r.height - 38)) + 'px';
    }
  });

  return { abrir, lista: () => jans.slice(), ativa, fecharTodas: async () => { for (const j of jans.slice()) await j.fechar(true); } };
})();

/* ------------------------------------------------------------------ programas */
const Apps = {};       /* preenchido por apps-base.js, apps-extra.js e tela.js: Apps.terminal(arg) etc. */

const Desktop = (() => {
  let lista = [];      /* tudo o que está instalado: { id, nome, resumo, icone, categoria, tipo, alvo } */

  function iconeDe(it) {
    if (it.icone_sis && Sistema.modo === 'ponte') return `<img alt="" loading="lazy" src="${Sistema.apps.urlIcone(it.icone_sis)}" onerror="this.outerHTML=decodeURIComponent('${encodeURIComponent(Icones.app('app'))}')">`;
    return Icones.app(it.icone);
  }

  /* abre um item do menu */
  async function abrir(it, arg) {
    try {
      if (it.tipo === 'interno') {
        const f = Apps[it.alvo];
        if (!f) throw new Error('este programa não existe nesta versão do TNUX');
        await f(arg);
      } else if (it.tipo === 'web') Apps.web({ url: it.alvo, nome: it.nome, icone: Icones.app(it.icone), externo: true });
      else if (it.tipo === 'arquivo') Apps.web({ vfs: it.alvo, nome: it.nome, icone: Icones.app(it.icone) });
      else if (it.tipo === 'appponte') { const u = Sistema.apps.urlAppPonte(it.alvo); Apps.web({ url: u.url, nome: it.nome, icone: Icones.app(it.icone), isolar: !u.isolado }); }
      else if (it.tipo === 'nativo') await Apps.nativo(it);
    } catch (e) { Avisos.erro('Não foi possível abrir ' + it.nome, e); }
  }
  function abrirPorId(id, arg) {
    const it = lista.find((x) => x.id === id) || (Apps[id] ? { id, nome: id, tipo: 'interno', alvo: id } : null);
    if (it) return abrir(it, arg);
    Avisos.mostrar('Programa não instalado', `"${id}" não está instalado. Procure no Gerenciador de Aplicativos.`, 'atencao');
    return null;
  }

  /* ---------------------------------------------------------------- menu */
  const menu = $('#menu'), busca = $('#menu-busca');
  let catSel = 'Todos';

  function botaoApp(it) {
    const b = h('button', { title: it.resumo || it.nome, dataset: { id: it.id } }, h('span', { html: iconeDe(it) }), h('span', null, it.nome));
    b.firstChild.style.display = 'contents';
    b.addEventListener('click', () => { fecharMenu(); abrir(it); });
    b.addEventListener('pointerenter', () => descrever(it));
    b.addEventListener('focus', () => descrever(it));
    b.addEventListener('contextmenu', (e) => {
      e.preventDefault();
      const fav = Cfg.dados.favoritos.includes(it.id);
      const itens = [
        { rotulo: 'Abrir', icone: 'abrir', acao: () => { fecharMenu(); abrir(it); } },
        { rotulo: fav ? 'Tirar dos favoritos' : 'Pôr nos favoritos', icone: fav ? 'fechar' : 'novo', acao: () => { Cfg.definir('favoritos', fav ? Cfg.dados.favoritos.filter((x) => x !== it.id) : [...Cfg.dados.favoritos, it.id]); montarLado(); } },
      ];
      if (it.tipo !== 'interno' && it.tipo !== 'nativo') itens.push('-', { rotulo: 'Desinstalar', icone: 'apagar', acao: () => { fecharMenu(); Apps.loja({ remover: it.id }); } });
      Ctx.abrir(e.clientX, e.clientY, itens);
    });
    return b;
  }
  function descrever(it) {
    $('#menu-desc').replaceChildren(h('b', null, it.nome), h('span', null, it.resumo || ''));
  }
  function montarApps() {
    const termo = busca.value.trim().toLowerCase();
    const semAcento = (s) => s.normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase();
    let itens = lista;
    if (termo) { const t = semAcento(termo); itens = lista.filter((a) => semAcento(a.nome).includes(t) || semAcento(a.resumo || '').includes(t) || a.id.includes(t)); }
    else if (catSel !== 'Todos') itens = lista.filter((a) => a.categoria === catSel);
    /* na busca, primeiro quem começa com o termo, depois quem o tem no nome, depois o resto */
    const t2 = termo ? semAcento(termo) : '';
    const nota = (x) => (!t2 ? 0 : semAcento(x.nome).startsWith(t2) ? 0 : semAcento(x.nome).includes(t2) ? 1 : 2);
    itens = itens.slice().sort((a, b) => nota(a) - nota(b) || a.nome.localeCompare(b.nome, 'pt-BR'));
    const caixa = $('#menu-apps');
    caixa.replaceChildren(...itens.map(botaoApp));
    if (!itens.length) caixa.append(h('div.vazio', null, termo ? `Nada encontrado para "${busca.value}".` : 'Nenhum programa nesta categoria.'));
    if (termo && itens.length) { caixa.firstChild.classList.add('sel'); descrever(itens[0]); }
    caixa.scrollTop = 0;
  }
  function montarCats() {
    const ordem = ['Acessórios', 'Educação', 'Escritório', 'Gráficos', 'Internet', 'Jogos', 'Programação', 'Som e vídeo', 'Sistema', 'Preferências', 'Outros'];
    const tem = new Set(lista.map((a) => a.categoria));
    const cats = ['Todos', ...ordem.filter((c) => tem.has(c)), ...[...tem].filter((c) => !ordem.includes(c)).sort()];
    if (!cats.includes(catSel)) catSel = 'Todos';
    $('#menu-cats').replaceChildren(...cats.map((c) => {
      const b = h('button' + (c === catSel ? '.sel' : ''), null, h('span', { html: Icones.categoria(c) }), h('span', null, c));
      b.firstChild.style.display = 'contents';
      const escolher = () => { catSel = c; busca.value = ''; montarCats(); montarApps(); };
      b.addEventListener('click', escolher);
      b.addEventListener('pointerenter', (e) => { if (e.pointerType === 'mouse' && c !== catSel) escolher(); });
      return b;
    }));
  }
  function montarLado() {
    const lado = $('#menu-lado');
    const favs = Cfg.dados.favoritos.map((id) => lista.find((a) => a.id === id)).filter(Boolean);
    lado.replaceChildren(...favs.map((it) => h('button', { title: it.nome, html: iconeDe(it), onclick: () => { fecharMenu(); abrir(it); }, onpointerenter: () => descrever(it) })),
      h('div.esp'),
      h('button.sessao', { title: 'Reiniciar o TNUX', html: Icones.ui('reiniciar'), onclick: () => { fecharMenu(); energia('reiniciar'); } }),
      h('button.sessao', { title: 'Desligar', html: Icones.ui('energia'), onclick: () => { fecharMenu(); energia('desligar'); } }));
  }
  function abrirMenu() {
    busca.value = '';
    catSel = 'Todos';
    montarLado(); montarCats(); montarApps();
    $('#menu-desc').replaceChildren(h('b', null, `${Sistema.info.usuario || 'usuario'}@${Sistema.info.host || 'tnux'}`),
      h('span', null, Sistema.modo === 'ponte' ? 'Modo ponte: ' + (Sistema.info.sistema || 'aparelho') : 'TNUX ' + Sistema.versao + ' ' + Sistema.codinome + ' no navegador'));
    menu.hidden = false;
    $('#btn-menu').classList.add('aberto');
    if (!toque()) busca.focus();
  }
  function fecharMenu() { menu.hidden = true; $('#btn-menu').classList.remove('aberto'); }
  $('#btn-menu').addEventListener('click', () => (menu.hidden ? abrirMenu() : fecharMenu()));
  busca.addEventListener('input', montarApps);
  busca.addEventListener('keydown', (e) => {
    const bts = [...$('#menu-apps').querySelectorAll('button')];
    const i = bts.findIndex((b) => b.classList.contains('sel'));
    if (e.key === 'Enter') { (bts[i] || bts[0])?.click(); e.preventDefault(); }
    else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault();
      if (!bts.length) return;
      const n = Math.max(0, Math.min(bts.length - 1, i + (e.key === 'ArrowDown' ? 1 : -1)));
      bts.forEach((b) => b.classList.remove('sel'));
      bts[n].classList.add('sel');
      bts[n].scrollIntoView({ block: 'nearest' });
      const it = lista.find((a) => a.id === bts[n].dataset.id);
      if (it) descrever(it);
    }
  });
  addEventListener('pointerdown', (e) => { if (!menu.hidden && !menu.contains(e.target) && !$('#btn-menu').contains(e.target) && !(Ctx.el && Ctx.el.contains(e.target))) fecharMenu(); }, true);
  addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && !menu.hidden) { fecharMenu(); e.preventDefault(); }
    if (e.key === 'Escape') Ctx.fechar();
    if (e.ctrlKey && e.altKey && e.key.toLowerCase() === 't') { e.preventDefault(); abrirPorId('terminal'); }
  });

  /* ---------------------------------------------------------------- painel */
  function montarAtalhos() {
    const ids = ['arquivos', 'navegador', 'terminal'];
    $('#atalhos').replaceChildren(...ids.map((id) => lista.find((a) => a.id === id)).filter(Boolean)
      .map((it) => h('button', { title: it.nome, html: Icones.app(it.icone), onclick: () => abrir(it) })));
  }
  function relogio() {
    const d = new Date();
    const dias = ['dom', 'seg', 'ter', 'qua', 'qui', 'sex', 'sáb'], meses = ['jan', 'fev', 'mar', 'abr', 'mai', 'jun', 'jul', 'ago', 'set', 'out', 'nov', 'dez'];
    $('#relogio').replaceChildren(h('b', null, d.toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit' })), `${dias[d.getDay()]} ${d.getDate()} ${meses[d.getMonth()]}`);
  }
  $('#relogio').addEventListener('click', () => {
    Avisos.mostrar(new Date().toLocaleDateString('pt-BR', { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric' }), new Date().toLocaleTimeString('pt-BR'));
  });
  function indicador() {
    const b = $('#ind-modo');
    const ponte = Sistema.modo === 'ponte';
    b.className = ponte ? 'ponte' : (Sistema.disco.grava ? '' : 'aviso');
    b.title = ponte ? 'Modo ponte: os comandos e arquivos são os do aparelho (' + (Sistema.info.sistema || '') + ')'
      : Sistema.disco.grava ? 'Modo navegador: o sistema roda dentro desta aba e o disco fica guardado no navegador'
        : 'Outra aba do TNUX está aberta: nesta, o que você fizer não será gravado';
    b.replaceChildren(h('i'), h('span', null, ponte ? 'Ponte' : 'Navegador'));
  }
  $('#ind-modo').addEventListener('click', () => abrirPorId('config', 'sistema'));

  /* ---------------------------------------------------------------- ícones da área de trabalho */
  function ativavel(el, f) {
    el.addEventListener('dblclick', f);
    el.addEventListener('pointerup', (e) => { if (e.pointerType !== 'mouse') f(); });
    el.addEventListener('keydown', (e) => { if (e.key === 'Enter') f(); });
  }
  async function montarIcones() {
    const fixos = [
      { nome: 'Computador', icone: Icones.app('computador'), f: () => abrirPorId('arquivos', '/') },
      { nome: 'Pasta pessoal', icone: Icones.app('casa'), f: () => abrirPorId('arquivos', Sistema.fs.casa) },
    ];
    const els = fixos.map((it) => { const b = h('button.icone-area', { html: it.icone }, h('span', null, it.nome)); ativavel(b, it.f); return b; });
    /* o que estiver na pasta "Área de Trabalho" também aparece */
    for (const nome of ['Área de Trabalho', 'Desktop']) {
      const pasta = Sistema.fs.juntar(Sistema.fs.casa, nome);
      try {
        const itens = await Sistema.fs.listar(pasta);
        for (const it of itens.filter((x) => !x.nome.startsWith('.')).slice(0, 24)) {
          const cam = Sistema.fs.juntar(pasta, it.nome);
          const b = h('button.icone-area', { html: Icones.arquivo(it.nome, it.tipo) }, h('span', null, it.nome));
          ativavel(b, () => (it.tipo === 'd' ? abrirPorId('arquivos', cam) : Apps.abrirArquivo(cam)));
          els.push(b);
        }
        break;
      } catch (e) { /* a pasta não existe: tudo bem */ }
    }
    $('#icones').replaceChildren(...els);
    for (const b of els) b.addEventListener('pointerdown', () => { els.forEach((x) => x.classList.remove('sel')); b.classList.add('sel'); });
  }
  $('#fundo').addEventListener('pointerdown', () => { for (const b of $('#icones').children) b.classList.remove('sel'); });
  $('#area').addEventListener('contextmenu', (e) => {
    if (e.target.closest('.jan, .veu, .aviso')) return;
    e.preventDefault();
    Ctx.abrir(e.clientX, e.clientY, [
      { rotulo: 'Abrir o terminal', icone: 'novo', acao: () => abrirPorId('terminal') },
      { rotulo: 'Abrir os arquivos', icone: 'abrir', acao: () => abrirPorId('arquivos') },
      '-',
      { rotulo: 'Mudar o papel de parede', icone: 'olho', acao: () => abrirPorId('config', 'aparencia') },
      { rotulo: 'Configurações do sistema', icone: 'lista', acao: () => abrirPorId('config') },
    ]);
  });

  /* ---------------------------------------------------------------- energia */
  async function energia(tipo) {
    if (Sistema.modo === 'navegador') { try { await Sistema.disco.salvar(); } catch (e) { /* segue mesmo assim */ } }
    if (tipo === 'reiniciar') { location.reload(); return; }
    await WM.fecharTodas();
    const tela = h('div', { id: 'boot' }, h('div.boot-logo', { html: Icones.app('tnux') }), h('div.boot-nome', null, 'TNUX'),
      h('div.boot-passo', null, Sistema.modo === 'ponte' ? 'A área de trabalho foi fechada. A ponte continua ligada no aparelho (Ctrl+C no terminal para desligá-la).' : 'O sistema foi desligado. Seus arquivos estão guardados.'),
      h('button.btn.pri', { onclick: () => location.reload() }, 'Ligar de novo'));
    document.body.append(tela);
    $('#painel').hidden = true;
    $('#area').hidden = true;
  }

  async function atualizarApps() {
    try { lista = await Sistema.apps.listar(); } catch (e) { Avisos.erro('Não consegui ler a lista de programas', e); }
    montarAtalhos();
    if (!menu.hidden) { montarLado(); montarCats(); montarApps(); }
  }

  async function iniciar() {
    if (toque()) document.body.classList.add('toque');
    $('#btn-menu').innerHTML = Icones.app('tnux');
    await atualizarApps();
    relogio();
    setInterval(relogio, 10000);
    indicador();
    await montarIcones();
    Sistema.ouvir('apps', atualizarApps);
  }

  return { iniciar, abrir, abrirPorId, atualizarApps, montarIcones, energia, iconeDe, ativavel, indicador, get apps() { return lista; } };
})();
