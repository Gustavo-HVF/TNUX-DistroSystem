/*
 * TNUX: a "Tela do aparelho" (modo ponte).
 *
 * Programas Linux com janela (Chromium, Firefox, GIMP...) precisam de uma tela gráfica. A ponte
 * liga um servidor de tela (Xvnc) no aparelho e este arquivo mostra essa tela dentro de uma janela
 * do TNUX, mandando de volta o mouse e o teclado. É um cliente do protocolo RFB (VNC) escrito do
 * zero: codificações Raw e CopyRect, o bastante para uma conexão local.
 */
'use strict';

class ClienteRFB {
  /* canvas: onde desenhar; url: WebSocket da ponte; ao: { estado(texto), tamanho(l, a), fechou(motivo), copiou(texto) } */
  constructor(canvas, url, ao) {
    this.canvas = canvas; this.ctx = canvas.getContext('2d', { alpha: false }); this.ao = ao;
    this.partes = []; this.total = 0; this.fase = 'versao'; this.larg = 0; this.alt = 0; this.pedido = false; this.botoes = 0;
    this.teclas = new Map();
    this.ws = new WebSocket(url);
    this.ws.binaryType = 'arraybuffer';
    this.ws.onmessage = (e) => { const b = new Uint8Array(e.data); this.partes.push(b); this.total += b.length; try { this.processar(); } catch (er) { this.fechar('erro no protocolo da tela: ' + er.message); } };
    this.ws.onclose = () => { if (this.fase !== 'fim') { this.fase = 'fim'; ao.fechou('a conexão com a tela foi fechada'); } };
    this.ws.onerror = () => {};
  }
  fechar(motivo) { if (this.fase === 'fim') return; this.fase = 'fim'; try { this.ws.close(); } catch (e) { /* */ } this.ao.fechou(motivo || ''); }
  enviar(bytes) { if (this.ws.readyState === 1) this.ws.send(bytes); }

  /* tira n bytes da fila (ou null se ainda não chegaram todos) */
  ler(n) {
    if (this.total < n) return null;
    this.total -= n;
    const p = this.partes[0];
    if (p.length === n) { this.partes.shift(); return p; }
    if (p.length > n) { this.partes[0] = p.subarray(n); return p.subarray(0, n); }
    const r = new Uint8Array(n);
    let o = 0;
    while (o < n) {
      const q = this.partes[0], falta = n - o;
      if (q.length <= falta) { r.set(q, o); o += q.length; this.partes.shift(); }
      else { r.set(q.subarray(0, falta), o); this.partes[0] = q.subarray(falta); o = n; }
    }
    return r;
  }
  /* olha os primeiros n bytes sem tirar */
  espiar(n) {
    if (this.total < n) return null;
    if (this.partes[0].length >= n) return this.partes[0];
    const r = new Uint8Array(n);
    let o = 0;
    for (const q of this.partes) { const c = Math.min(q.length, n - o); r.set(q.subarray(0, c), o); o += c; if (o === n) break; }
    return r;
  }

  processar() {
    for (;;) {
      if (this.fase === 'versao') {
        const b = this.ler(12);
        if (!b) return;
        if (String.fromCharCode(...b.subarray(0, 4)) !== 'RFB ') throw new Error('não é um servidor de tela');
        this.enviar(new TextEncoder().encode('RFB 003.008\n'));
        this.fase = 'seguranca';
      } else if (this.fase === 'seguranca') {
        const n = this.espiar(1);
        if (!n) return;
        if (n[0] === 0) { if (!this.ler(1)) return; this.fase = 'motivo'; continue; }
        const b = this.espiar(1 + n[0]);
        if (!b) return;
        this.ler(1 + n[0]);
        if (![...b.subarray(1, 1 + n[0])].includes(1)) throw new Error('o servidor de tela pede uma senha que o TNUX não usa');
        this.enviar(new Uint8Array([1]));
        this.fase = 'resultado';
      } else if (this.fase === 'motivo') {
        const c = this.espiar(4);
        if (!c) return;
        const tam = new DataView(c.buffer, c.byteOffset, 4).getUint32(0);
        const b = this.espiar(4 + tam);
        if (!b) return;
        this.ler(4 + tam);
        throw new Error(new TextDecoder().decode(b.subarray(4, 4 + tam)));
      } else if (this.fase === 'resultado') {
        const b = this.ler(4);
        if (!b) return;
        if (b[3] !== 0) throw new Error('o servidor de tela recusou a conexão');
        this.enviar(new Uint8Array([1]));       /* compartilhada: outras janelas podem ver a mesma tela */
        this.fase = 'inicio';
      } else if (this.fase === 'inicio') {
        const c = this.espiar(24);
        if (!c) return;
        const dv = new DataView(c.buffer, c.byteOffset, 24);
        const tamNome = dv.getUint32(20);
        if (!this.espiar(24 + tamNome)) return;
        this.redimensionar(dv.getUint16(0), dv.getUint16(2));
        this.ler(24 + tamNome);
        /* pede os pixels como R, G, B, X (4 bytes): é o formato do canvas, sem conversão */
        const pf = new Uint8Array(20);
        pf.set([0, 0, 0, 0, 32, 24, 0, 1, 0, 255, 0, 255, 0, 255, 0, 8, 16, 0, 0, 0]);
        this.enviar(pf);
        const enc = new DataView(new ArrayBuffer(4 + 3 * 4));
        enc.setUint8(0, 2); enc.setUint16(2, 3);
        enc.setInt32(4, 1); enc.setInt32(8, 0); enc.setInt32(12, -223);      /* CopyRect, Raw, mudança de tamanho */
        this.enviar(new Uint8Array(enc.buffer));
        this.fase = 'normal';
        this.ao.estado('');
        this.pedir(false);
      } else if (this.fase === 'normal') {
        const t = this.espiar(1);
        if (!t) return;
        if (t[0] === 0) {                        /* atualização da tela */
          const c = this.ler(4);
          if (!c) return;
          this.retangulos = (c[2] << 8) | c[3];
          this.fase = 'ret';
        } else if (t[0] === 1) {                 /* mapa de cores: não usado em cor verdadeira */
          const c = this.espiar(6);
          if (!c) return;
          const n = (c[4] << 8) | c[5];
          if (!this.ler(6 + n * 6)) return;
        } else if (t[0] === 2) this.ler(1);      /* campainha */
        else if (t[0] === 3) {                   /* texto copiado no aparelho */
          const c = this.espiar(8);
          if (!c) return;
          const tam = new DataView(c.buffer, c.byteOffset, 8).getUint32(4);
          const b = this.espiar(8 + tam);
          if (!b) return;
          this.ler(8 + tam);
          this.ao.copiou(new TextDecoder('latin1').decode(b.subarray(8, 8 + tam)));
        } else throw new Error('mensagem desconhecida (' + t[0] + ')');
      } else if (this.fase === 'ret') {
        if (this.retangulos === 0) { this.fase = 'normal'; this.pedir(true); continue; }
        const c = this.espiar(12);
        if (!c) return;
        const dv = new DataView(c.buffer, c.byteOffset, 12);
        const x = dv.getUint16(0), y = dv.getUint16(2), w = dv.getUint16(4), a = dv.getUint16(6), cod = dv.getInt32(8);
        if (cod === 0) {
          const tam = 12 + w * a * 4;
          if (this.total < tam) return;
          this.ler(12);
          const px = this.ler(w * a * 4);
          if (w && a) {
            const img = this.ctx.createImageData(w, a);
            img.data.set(px);
            const u32 = new Uint32Array(img.data.buffer);
            for (let i = 0; i < u32.length; i++) u32[i] |= 0xff000000;     /* o 4º byte vira opaco */
            this.ctx.putImageData(img, x, y);
          }
        } else if (cod === 1) {
          const b = this.espiar(16);
          if (!b) return;
          this.ler(16);
          const sx = (b[12] << 8) | b[13], sy = (b[14] << 8) | b[15];
          this.ctx.drawImage(this.canvas, sx, sy, w, a, x, y, w, a);
        } else if (cod === -223) { this.ler(12); this.redimensionar(w, a); }
        else throw new Error('codificação não suportada (' + cod + ')');
        this.retangulos--;
      } else return;
    }
  }
  redimensionar(l, a) {
    this.larg = l; this.alt = a;
    this.canvas.width = l; this.canvas.height = a;
    this.ctx.fillStyle = '#000';
    this.ctx.fillRect(0, 0, l, a);
    this.ao.tamanho(l, a);
  }
  pedir(incremental) {
    if (this.pedido || this.fase === 'fim') return;
    this.pedido = true;
    requestAnimationFrame(() => {
      this.pedido = false;
      const m = new DataView(new ArrayBuffer(10));
      m.setUint8(0, 3); m.setUint8(1, incremental ? 1 : 0); m.setUint16(6, this.larg); m.setUint16(8, this.alt);
      this.enviar(new Uint8Array(m.buffer));
    });
  }

  /* ---------------------------------------------------------------- mouse e teclado */
  ponteiro(x, y, botoes) {
    if (this.fase !== 'normal' && this.fase !== 'ret') return;
    const m = new DataView(new ArrayBuffer(6));
    m.setUint8(0, 5); m.setUint8(1, botoes);
    m.setUint16(2, Math.max(0, Math.min(this.larg - 1, Math.round(x)))); m.setUint16(4, Math.max(0, Math.min(this.alt - 1, Math.round(y))));
    this.enviar(new Uint8Array(m.buffer));
  }
  tecla(keysym, baixo) {
    if (this.fase !== 'normal' && this.fase !== 'ret') return;
    const m = new DataView(new ArrayBuffer(8));
    m.setUint8(0, 4); m.setUint8(1, baixo ? 1 : 0); m.setUint32(4, keysym);
    this.enviar(new Uint8Array(m.buffer));
  }
  /* manda um texto como se fosse digitado */
  digitar(texto) { for (const ch of texto) { const k = ClienteRFB.keysymDoCaractere(ch); if (k) { this.tecla(k, true); this.tecla(k, false); } } }
  colar(texto) {
    const b = new TextEncoder().encode(texto);
    const m = new Uint8Array(8 + b.length);
    m[0] = 6;
    new DataView(m.buffer).setUint32(4, b.length);
    m.set(b, 8);
    this.enviar(m);
  }
  soltarTudo() { for (const k of this.teclas.values()) this.tecla(k, false); this.teclas.clear(); }

  static keysymDoCaractere(ch) {
    const cp = ch.codePointAt(0);
    if (ch === '\n' || ch === '\r') return 0xff0d;
    if (ch === '\t') return 0xff09;
    if (cp >= 0x20 && cp <= 0xff) return cp;
    if (cp > 0xff) return 0x01000000 + cp;
    return 0;
  }
  static keysym(e) {
    const E = {
      Backspace: 0xff08, Tab: 0xff09, Enter: 0xff0d, Escape: 0xff1b, Delete: 0xffff, Home: 0xff50, ArrowLeft: 0xff51, ArrowUp: 0xff52, ArrowRight: 0xff53,
      ArrowDown: 0xff54, PageUp: 0xff55, PageDown: 0xff56, End: 0xff57, Insert: 0xff63, CapsLock: 0xffe5, AltGraph: 0xfe03, ContextMenu: 0xff67,
      PrintScreen: 0xff61, ScrollLock: 0xff14, Pause: 0xff13, NumLock: 0xff7f,
    };
    if (E[e.key]) return E[e.key];
    if (e.key === 'Shift') return e.location === 2 ? 0xffe2 : 0xffe1;
    if (e.key === 'Control') return e.location === 2 ? 0xffe4 : 0xffe3;
    if (e.key === 'Alt') return e.location === 2 ? 0xffea : 0xffe9;
    if (e.key === 'Meta' || e.key === 'OS') return e.location === 2 ? 0xffec : 0xffeb;
    const f = /^F(\d{1,2})$/.exec(e.key);
    if (f) return 0xffbe + parseInt(f[1], 10) - 1;
    if ([...e.key].length === 1) return ClienteRFB.keysymDoCaractere(e.key);
    return 0;
  }
}

Apps.tela = async (op = {}) => {
  if (Sistema.modo !== 'ponte') { Avisos.mostrar('Tela do aparelho', 'Só existe no modo ponte.', 'atencao'); return null; }
  const jan = WM.abrir({ titulo: 'Tela do aparelho', icone: Icones.app('tela'), largura: 1000, altura: 660, unica: 'tela' });
  if (jan.corpo.childElementCount) { if (op.ligar && jan.ligar) await jan.ligar(); return jan; }
  const palco = h('div.tela-palco');
  const canvas = h('canvas.tela-vnc', { tabIndex: 0, width: 16, height: 16, hidden: true, 'aria-label': 'Tela gráfica do aparelho' });
  const entrada = h('input.tela-entrada', { type: 'text', autocapitalize: 'off', autocomplete: 'off', spellcheck: false, 'aria-label': 'Teclado' });
  const estado = h('div.vazio', { style: 'color:#cfd8dc' });
  const rodape = h('div.rodape');
  let rfb = null;

  const barra = h('div.barra', null,
    h('button.btn', { onclick: () => abrirPrograma() }, 'Abrir programa'),
    h('div.sep'),
    h('button.btn.liso', { title: 'Mostrar o teclado (telas de toque)', html: Icones.ui('teclado'), onclick: () => entrada.focus() }),
    h('button.btn.liso', { title: 'Mandar o texto copiado para o aparelho (depois use Ctrl+V lá)', html: Icones.ui('colar'), onclick: async () => {
      try { const t = await navigator.clipboard.readText(); if (rfb && t) { rfb.colar(t); Avisos.mostrar('Texto enviado', 'Agora use Ctrl+V no programa do aparelho.'); } }
      catch (e) { Avisos.mostrar('Colar', 'O navegador não deixou ler a área de transferência.', 'atencao'); }
    } }),
    h('button.btn.liso', { title: 'Tela cheia', html: Icones.ui('tela_cheia'), onclick: () => palco.requestFullscreen && palco.requestFullscreen().catch(() => {}) }),
    h('span', { style: 'flex:1' }),
    h('button.btn', { onclick: async () => { if (await Dlg.confirma('Desligar a tela?', 'Os programas gráficos abertos nela serão fechados.', { sim: 'Desligar', perigo: true })) { await Sistema.tela.parar().catch(() => {}); mostrarDesligada(); } } }, 'Desligar a tela'));
  palco.append(estado, canvas, entrada);
  jan.corpo.append(barra, palco, rodape);

  function mostrarDesligada(msg) {
    if (rfb) { rfb.fechar(); rfb = null; }
    canvas.hidden = true; estado.hidden = false;
    rodape.textContent = 'tela desligada';
    const tamanhos = [['Do tamanho desta janela', 0, 0], ['1280 × 720', 1280, 720], ['1024 × 768', 1024, 768], ['800 × 600', 800, 600]];
    estado.replaceChildren(h('span', { html: Icones.app('tela'), style: 'display:contents' }),
      h('p', null, msg || 'A tela do aparelho está desligada. Ela é onde aparecem os programas Linux com janela (Chromium, Firefox, GIMP...).'),
      Sistema.info.tela_disponivel
        ? h('div', { style: 'display:flex;gap:8px;flex-wrap:wrap;justify-content:center;margin-top:10px' }, tamanhos.map(([n, l, a], i) => h('button.btn' + (i === 0 ? '.pri' : ''), { onclick: () => ligar(l, a) }, i === 0 ? 'Ligar a tela' : n)))
        : h('p', null, 'Falta o servidor de tela neste aparelho. Instale pelo Terminal: ', h('code', null, 'tnux instalar tela'), ' e reinicie a ponte.'));
  }
  async function ligar(l, a) {
    if (!l) { const r = palco.getBoundingClientRect(); l = Math.max(800, Math.min(1920, Math.round(r.width / 8) * 8)); a = Math.max(560, Math.min(1200, Math.round(r.height / 8) * 8)); }
    estado.replaceChildren(h('p', null, 'Ligando a tela...'));
    try {
      const e = await Sistema.tela.iniciar(l, a);
      Sistema.info.tela_ativa = true;
      if (!e.janelas) Avisos.mostrar('Sem gerenciador de janelas', 'Os programas vão aparecer sem barra de título. Instale o openbox no aparelho para poder mover as janelas.', 'atencao', 9000);
      conectar();
    } catch (e) { mostrarDesligada('Não foi possível ligar a tela: ' + e.message); }
  }
  jan.ligar = async () => { const e = await Sistema.tela.estado(); if (!e.ativa) await ligar(0, 0); };

  function conectar() {
    if (rfb) rfb.fechar();
    estado.hidden = false; estado.replaceChildren(h('p', null, 'Conectando à tela...'));
    rfb = new ClienteRFB(canvas, Sistema.tela.urlWs(), {
      estado: () => { estado.hidden = true; canvas.hidden = false; canvas.focus({ preventScroll: true }); },
      tamanho: (l, a) => { rodape.textContent = `tela ${l} × ${a}  •  os programas gráficos do aparelho aparecem aqui`; },
      fechou: (motivo) => { if (!jan.fechada) mostrarDesligada(motivo ? 'A tela foi desconectada: ' + motivo : null); },
      copiou: (t) => { if (navigator.clipboard && t) navigator.clipboard.writeText(t).catch(() => {}); },
    });
  }

  /* mouse e toque */
  const pos = (e) => { const r = canvas.getBoundingClientRect(); return [(e.clientX - r.left) * canvas.width / r.width, (e.clientY - r.top) * canvas.height / r.height]; };
  const mascara = (b) => (b & 1) | ((b & 4) >> 1) | ((b & 2) << 1);
  canvas.addEventListener('pointerdown', (e) => { if (!rfb) return; canvas.setPointerCapture(e.pointerId); canvas.focus({ preventScroll: true }); rfb.botoes = e.pointerType === 'mouse' ? mascara(e.buttons) : 1; rfb.ponteiro(...pos(e), rfb.botoes); e.preventDefault(); });
  canvas.addEventListener('pointermove', (e) => { if (rfb) rfb.ponteiro(...pos(e), rfb.botoes); });
  const soltar = (e) => { if (!rfb) return; rfb.botoes = e.pointerType === 'mouse' ? mascara(e.buttons) : 0; rfb.ponteiro(...pos(e), rfb.botoes); };
  canvas.addEventListener('pointerup', soltar);
  canvas.addEventListener('pointercancel', soltar);
  canvas.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    if (rfb && e.pointerType !== 'mouse' && !(e.buttons & 2)) { const p = pos(e); rfb.ponteiro(...p, 4); rfb.ponteiro(...p, 0); }      /* toque longo = botão direito */
  });
  canvas.addEventListener('wheel', (e) => {
    if (!rfb) return;
    e.preventDefault();
    const p = pos(e), b = e.deltaY < 0 ? 8 : 16;
    const n = Math.max(1, Math.min(5, Math.round(Math.abs(e.deltaY) / 60)));
    for (let i = 0; i < n; i++) { rfb.ponteiro(...p, rfb.botoes | b); rfb.ponteiro(...p, rfb.botoes); }
  }, { passive: false });
  /* teclado físico */
  canvas.addEventListener('keydown', (e) => {
    if (!rfb) return;
    const k = ClienteRFB.keysym(e);
    if (!k) return;
    e.preventDefault(); e.stopPropagation();
    rfb.teclas.set(e.code || e.key, k);
    rfb.tecla(k, true);
  });
  canvas.addEventListener('keyup', (e) => {
    if (!rfb) return;
    const k = rfb.teclas.get(e.code || e.key);
    if (!k) return;
    e.preventDefault();
    rfb.teclas.delete(e.code || e.key);
    rfb.tecla(k, false);
  });
  canvas.addEventListener('blur', () => { if (rfb) rfb.soltarTudo(); });
  /* teclado da tela (celular): um campo invisível recebe o que é digitado */
  entrada.addEventListener('beforeinput', (e) => {
    if (!rfb) return;
    if (e.inputType === 'deleteContentBackward') { rfb.tecla(0xff08, true); rfb.tecla(0xff08, false); }
    else if (e.inputType === 'insertLineBreak' || e.inputType === 'insertParagraph') { rfb.tecla(0xff0d, true); rfb.tecla(0xff0d, false); }
    else if (e.data) rfb.digitar(e.data);
    e.preventDefault();
    entrada.value = '';
  });
  entrada.addEventListener('keydown', (e) => { if (rfb && e.key === 'Enter') { rfb.tecla(0xff0d, true); rfb.tecla(0xff0d, false); e.preventDefault(); } });

  async function abrirPrograma() {
    const nativos = Desktop.apps.filter((a) => a.tipo === 'nativo' && !a.terminal).sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'));
    let d;
    const lista = h('div.arq-lista.linhas', { style: 'flex:1' }, nativos.map((it) => {
      const b = h('button.arq-item', { html: Desktop.iconeDe(it) }, h('span', null, it.nome), h('small', null, it.categoria));
      b.addEventListener('click', async () => {
        d.fechar();
        try { const e = await Sistema.tela.estado(); if (!e.ativa) await ligar(0, 0); await Sistema.apps.lancar(it.alvo, 'tela'); canvas.focus({ preventScroll: true }); }
        catch (e) { Avisos.erro('Não foi possível abrir ' + it.nome, e); }
      });
      return b;
    }));
    if (!nativos.length) lista.append(h('div.vazio', null, 'Nenhum programa gráfico instalado neste aparelho ainda. Instale pelo Gerenciador de Aplicativos (por exemplo: Chromium, Firefox, GIMP).'));
    d = Dlg.abrir([h('h3', null, 'Abrir na tela do aparelho'), lista, h('div.pe', null, h('button.btn', { onclick: () => d.fechar() }, 'Fechar'))], 'largo');
  }

  jan.aoFocar = () => { if (!canvas.hidden) canvas.focus({ preventScroll: true }); };
  jan.aoFechou = () => { if (rfb) rfb.fechar(); };
  try {
    const e = await Sistema.tela.estado();
    Sistema.info.tela_disponivel = e.disponivel;
    if (e.ativa) conectar(); else if (op.ligar) await ligar(0, 0); else mostrarDesligada();
  } catch (e) { mostrarDesligada('Não consegui falar com a ponte: ' + e.message); }
  return jan;
};
