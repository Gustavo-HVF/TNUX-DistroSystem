/*
 * TNUX: o Web Worker em que o núcleo roda.
 *
 * O núcleo (C compilado para WebAssembly) trabalha de forma síncrona: um comando demorado ocupa
 * o processador até terminar. Por isso ele fica aqui, fora da página: a área de trabalho continua
 * respondendo. É também aqui que o disco é guardado no IndexedDB e que a rede é feita (pedidos
 * síncronos, que só são permitidos dentro de um Worker).
 */
'use strict';
importScripts('nucleo-host.js');

let nucleo = null;
let base = '';
let gravar = true;
const enc = new TextEncoder();

/* ---------------------------------------------------------------- saída dos terminais, em lotes */
const fila = new Map();
let ultimoDespejo = 0;
function despejar() {
  for (const [sid, partes] of fila) {
    let total = 0;
    for (const p of partes) total += p.length;
    const junto = new Uint8Array(total);
    let o = 0;
    for (const p of partes) { junto.set(p, o); o += p.length; }
    postMessage({ t: 'tty', sid, d: junto }, [junto.buffer]);
  }
  fila.clear();
  ultimoDespejo = performance.now();
}
function escrever(sid, bytes) {
  if (!fila.has(sid)) fila.set(sid, []);
  fila.get(sid).push(bytes);
  if (performance.now() - ultimoDespejo > 40) despejar();
}

/* ---------------------------------------------------------------- rede */
function http(req) {
  let url = req.url;
  if (!/^[a-z][a-z0-9+.-]*:/i.test(url)) url = new URL(url, base).href;
  const x = new XMLHttpRequest();
  try {
    x.open(req.metodo, url, false);
    x.responseType = 'arraybuffer';
    for (const linha of req.cab.split('\n')) {
      const i = linha.indexOf(':');
      if (i > 0) { try { x.setRequestHeader(linha.slice(0, i).trim(), linha.slice(i + 1).trim()); } catch (e) { /* cabeçalho proibido pelo navegador */ } }
    }
    despejar();
    x.send(req.corpo || null);
  } catch (e) {
    return { status: 0, corpo: enc.encode('o navegador não conseguiu acessar este endereço: sem rede, ou o site não permite acesso direto de outras páginas'), cab: '' };
  }
  return { status: x.status, corpo: new Uint8Array(x.response || new ArrayBuffer(0)), cab: x.getAllResponseHeaders() || '' };
}
function dormir(ms) {
  despejar();
  const fim = performance.now() + ms;
  while (performance.now() < fim) { /* espera ocupada: um Worker não tem outro jeito de pausar sem SharedArrayBuffer */ }
}

/* ---------------------------------------------------------------- disco no IndexedDB */
function abrirBanco() {
  return new Promise((ok, erro) => {
    const r = indexedDB.open('tnux', 1);
    r.onupgradeneeded = () => r.result.createObjectStore('disco');
    r.onsuccess = () => ok(r.result);
    r.onerror = () => erro(r.error);
  });
}
async function discoLer() {
  try {
    const db = await abrirBanco();
    return await new Promise((ok) => {
      const g = db.transaction('disco').objectStore('disco').get('imagem');
      g.onsuccess = () => ok(g.result || null);
      g.onerror = () => ok(null);
    });
  } catch (e) { return null; }
}
async function discoGravar(bytes) {
  const db = await abrirBanco();
  await new Promise((ok, erro) => {
    const tx = db.transaction('disco', 'readwrite');
    if (bytes) tx.objectStore('disco').put(bytes, 'imagem'); else tx.objectStore('disco').delete('imagem');
    tx.oncomplete = ok;
    tx.onerror = () => erro(tx.error);
  });
}
let timer = null, salvando = false;
async function salvar() {
  timer = null;
  if (!nucleo || !gravar || salvando) return;
  if (!nucleo.sujo()) return;
  salvando = true;
  try {
    const img = nucleo.imagem();
    await discoGravar(img);
    postMessage({ t: 'salvo', tam: img.length });
  } catch (e) {
    postMessage({ t: 'erro_disco', msg: String(e && e.message || e) });
  } finally { salvando = false; }
}
function agendarSalvar(logo) {
  if (!gravar) return;
  if (timer) clearTimeout(timer);
  timer = setTimeout(salvar, logo ? 0 : 700);
}

/* ---------------------------------------------------------------- mensagens da página */
const funcoes = {
  sessaoAbrir: (sid, cols, rows) => nucleo.sessaoAbrir(sid, cols, rows),
  sessaoFechar: (sid) => nucleo.sessaoFechar(sid),
  redimensionar: (sid, cols, rows) => nucleo.redimensionar(sid, cols, rows),
  entrada: (sid, dados) => nucleo.entrada(sid, dados),
  rodar: (cmd, root) => nucleo.rodar(cmd, root),
  fsListar: (cam) => nucleo.fsListar(cam),
  fsInfo: (cam) => nucleo.fsInfo(cam),
  fsLer: (cam) => nucleo.fsLer(cam),
  fsGravar: (cam, bytes, root) => nucleo.fsGravar(cam, bytes, root),
  apps: () => nucleo.apps(),
  memoria: () => nucleo.memoria(),
  salvar: async () => { if (timer) clearTimeout(timer); await salvar(); return true; },
  exportar: () => nucleo.imagem(),
  importar: async (bytes) => { gravar = false; await discoGravar(bytes); return true; },
  redefinir: async () => { gravar = false; await discoGravar(null); return true; },
};

onmessage = async (ev) => {
  const m = ev.data;
  if (m.t === 'boot') {
    try {
      base = m.base;
      gravar = m.gravar !== false;
      const [wasm, imagem] = await Promise.all([
        fetch(new URL('tnux.wasm', base)).then((r) => { if (!r.ok) throw new Error('não achei o tnux.wasm (erro ' + r.status + ')'); return r.arrayBuffer(); }),
        discoLer(),
      ]);
      nucleo = TnuxNucleo.criarNucleo({
        wasm, cfg: m.cfg, imagem,
        escrever,
        gui(msg) { despejar(); if (msg.acao === 'salvar') agendarSalvar(true); postMessage({ t: 'gui', m: msg }); },
        http, dormir,
      });
      agendarSalvar(true);
      postMessage({ t: 'pronto', primeiro: !imagem });
    } catch (e) {
      postMessage({ t: 'falhou', msg: String(e && e.message || e) });
    }
    return;
  }
  if (m.t === 'chamar') {
    try {
      const r = await funcoes[m.fn](...m.args);
      despejar();
      const transf = r instanceof Uint8Array ? [r.buffer] : [];
      postMessage({ t: 'resp', id: m.id, ok: true, r }, transf);
    } catch (e) {
      despejar();
      postMessage({ t: 'resp', id: m.id, ok: false, erro: String(e && e.message || e) });
    }
    if (nucleo && nucleo.sujo()) agendarSalvar(false);
  }
};
