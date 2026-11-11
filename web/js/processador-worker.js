/*
 * TNUX: o trabalhador do "processar no computador".
 *
 * Cada instância deste Worker usa um núcleo do processador do computador que está com o TNUX
 * aberto. Recebe os bytes de um arquivo (vindos do aparelho), faz a conta pesada e devolve o
 * resultado. Operações: converter imagem, compactar/expandir (deflate), CRC-32, SHA-256 e rodar
 * comandos do shell do TNUX (o núcleo em WebAssembly) sobre um conjunto de arquivos.
 */
'use strict';

/* ---------------------------------------------------------------- CRC-32 (do formato zip) */
const TAB_CRC = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return t;
})();
function crc32(b) {
  let c = 0xffffffff;
  for (let i = 0; i < b.length; i++) c = TAB_CRC[(c ^ b[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

/* ---------------------------------------------------------------- SHA-256 (para quando o navegador não oferece crypto.subtle) */
const K256 = new Uint32Array([
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2]);
function sha256js(dados) {
  const h = new Uint32Array([0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]);
  const total = (Math.floor((dados.length + 8) / 64) + 1) * 64;
  const m = new Uint8Array(total);
  m.set(dados);
  m[dados.length] = 0x80;
  const dv = new DataView(m.buffer);
  dv.setUint32(total - 8, Math.floor(dados.length / 0x20000000));
  dv.setUint32(total - 4, (dados.length << 3) >>> 0);
  const w = new Uint32Array(64);
  for (let o = 0; o < total; o += 64) {
    for (let i = 0; i < 16; i++) w[i] = dv.getUint32(o + i * 4);
    for (let i = 16; i < 64; i++) {
      const a = w[i - 15], b = w[i - 2];
      w[i] = (w[i - 16] + (((a >>> 7) | (a << 25)) ^ ((a >>> 18) | (a << 14)) ^ (a >>> 3)) + w[i - 7] + (((b >>> 17) | (b << 15)) ^ ((b >>> 19) | (b << 13)) ^ (b >>> 10))) >>> 0;
    }
    let [a, b, c, d, e, f, g, hh] = h;
    for (let i = 0; i < 64; i++) {
      const s1 = ((e >>> 6) | (e << 26)) ^ ((e >>> 11) | (e << 21)) ^ ((e >>> 25) | (e << 7));
      const t1 = (hh + s1 + ((e & f) ^ (~e & g)) + K256[i] + w[i]) >>> 0;
      const s0 = ((a >>> 2) | (a << 30)) ^ ((a >>> 13) | (a << 19)) ^ ((a >>> 22) | (a << 10));
      const t2 = (s0 + ((a & b) ^ (a & c) ^ (b & c))) >>> 0;
      hh = g; g = f; f = e; e = (d + t1) >>> 0; d = c; c = b; b = a; a = (t1 + t2) >>> 0;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  return [...h].map((v) => v.toString(16).padStart(8, '0')).join('');
}
async function sha256(dados) {
  if (self.crypto && self.crypto.subtle) {
    const d = new Uint8Array(await self.crypto.subtle.digest('SHA-256', dados));
    return [...d].map((v) => v.toString(16).padStart(2, '0')).join('');
  }
  return sha256js(dados);
}

/* ---------------------------------------------------------------- deflate */
async function fluxo(dados, transformador) {
  const r = new Response(new Blob([dados]).stream().pipeThrough(transformador));
  return new Uint8Array(await r.arrayBuffer());
}

/* ---------------------------------------------------------------- imagens */
async function imagem(m) {
  const bmp = await createImageBitmap(new Blob([m.dados]));
  const deL = bmp.width, deA = bmp.height;
  let l = deL, a = deA;
  if (m.largura && l > m.largura) { a = Math.max(1, Math.round(a * m.largura / l)); l = m.largura; }
  const c = new OffscreenCanvas(l, a);
  const ctx = c.getContext('2d');
  if (m.para === 'jpg') { ctx.fillStyle = '#fff'; ctx.fillRect(0, 0, l, a); }       /* JPEG não tem transparência */
  ctx.imageSmoothingQuality = 'high';
  ctx.drawImage(bmp, 0, 0, l, a);
  bmp.close();
  const tipo = m.para === 'jpg' ? 'image/jpeg' : m.para === 'png' ? 'image/png' : 'image/webp';
  const blob = await c.convertToBlob({ type: tipo, quality: (m.qualidade || 82) / 100 });
  if (blob.type !== tipo) throw new Error('este navegador não sabe gravar ' + m.para);
  return { dados: new Uint8Array(await blob.arrayBuffer()), largura: l, altura: a, deL, deA };
}

/* ---------------------------------------------------------------- comandos do TNUX rodando no computador */
let Nucleo = null;
async function comando(m) {
  if (!Nucleo) { importScripts('nucleo-host.js'); Nucleo = self.TnuxNucleo; }
  const enc = new TextEncoder();
  const wasm = await (await fetch(new URL('../tnux.wasm', self.location.href))).arrayBuffer();
  const k = Nucleo.criarNucleo({
    wasm, imagem: null,
    cfg: { cpus: navigator.hardwareConcurrency || 1, mem_mb: 4096, host: 'pc', plataforma: 'computador emprestado', agente: 'processador', fuso_min: -new Date().getTimezoneOffset(), catalogo: m.catalogo },
    escrever() {}, gui() {}, dormir() {},
    http(req) {                       /* a rede é a do computador */
      const x = new XMLHttpRequest();
      try {
        x.open(req.metodo, /^[a-z][a-z0-9+.-]*:/i.test(req.url) ? req.url : new URL('../' + req.url, self.location.href).href, false);
        x.responseType = 'arraybuffer';
        x.send(req.corpo || null);
        return { status: x.status, corpo: new Uint8Array(x.response || new ArrayBuffer(0)), cab: x.getAllResponseHeaders() || '' };
      } catch (e) { return { status: 0, corpo: enc.encode('sem acesso a este endereço'), cab: '' }; }
    },
  });
  /* os arquivos do aparelho entram numa pasta de trabalho; o que mudar ou for criado nela volta */
  k.rodar('mkdir -p /trabalho', true);
  k.rodar('chown usuario /trabalho', true);
  const antes = new Map();
  for (const a of m.arquivos) { k.fsGravar('/trabalho/' + a.nome, a.dados); antes.set(a.nome, a.dados.length + ':' + crc32(a.dados)); }
  const r = k.rodar('cd /trabalho && ' + m.cmd, false);
  const saidas = [];
  const andar = (dir, prefixo) => {
    for (const it of k.fsListar(dir)) {
      const cam = dir + '/' + it.nome, rel = prefixo + it.nome;
      if (it.tipo === 'd') { andar(cam, rel + '/'); continue; }
      const dados = k.fsLer(cam);
      if (antes.get(rel) !== dados.length + ':' + crc32(dados)) saidas.push({ nome: rel, dados });
    }
  };
  andar('/trabalho', '');
  return { codigo: r.codigo, saida: r.saida, saidas };
}

onmessage = async (ev) => {
  const m = ev.data;
  try {
    let r, transf = [];
    if (m.op === 'imagem') { r = await imagem(m); transf = [r.dados.buffer]; }
    else if (m.op === 'compactar') {
      const comp = await fluxo(m.dados, new CompressionStream('deflate-raw'));
      const menor = comp.length < m.dados.length;       /* se não diminuiu, guarda sem compactar */
      r = { crc: crc32(m.dados), tam: m.dados.length, comp: menor ? comp : null, dados: menor ? null : m.dados };
      transf = [menor ? comp.buffer : m.dados.buffer];
    } else if (m.op === 'expandir') {
      const dados = m.metodo === 8 ? await fluxo(m.dados, new DecompressionStream('deflate-raw')) : m.dados;
      if (crc32(dados) !== m.crc) throw new Error('arquivo corrompido dentro do zip (CRC não confere)');
      r = { dados }; transf = [dados.buffer];
    } else if (m.op === 'sha256') r = { hash: await sha256(m.dados) };
    else if (m.op === 'comando') { r = await comando(m); transf = r.saidas.map((s) => s.dados.buffer); }
    else throw new Error('operação desconhecida: ' + m.op);
    postMessage({ id: m.id, ok: true, r }, transf);
  } catch (e) {
    postMessage({ id: m.id, ok: false, erro: String(e && e.message || e) });
  }
};
