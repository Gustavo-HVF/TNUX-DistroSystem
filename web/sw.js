/*
 * TNUX: service worker.
 *
 * Faz os arquivos do disco do TNUX (que só existe na memória da página) aparecerem como endereços
 * de verdade, em ./vfs/CAMINHO. É o que permite abrir numa janela um site baixado do GitHub para
 * dentro do sistema, com suas imagens, estilos e scripts. O conteúdo é pedido à página aberta.
 */
'use strict';
const TIPOS = {
  html: 'text/html; charset=utf-8', htm: 'text/html; charset=utf-8', css: 'text/css; charset=utf-8', js: 'text/javascript; charset=utf-8',
  mjs: 'text/javascript; charset=utf-8', json: 'application/json; charset=utf-8', wasm: 'application/wasm', svg: 'image/svg+xml', png: 'image/png',
  jpg: 'image/jpeg', jpeg: 'image/jpeg', gif: 'image/gif', webp: 'image/webp', ico: 'image/x-icon', bmp: 'image/bmp', avif: 'image/avif',
  txt: 'text/plain; charset=utf-8', md: 'text/plain; charset=utf-8', xml: 'application/xml', pdf: 'application/pdf', mp3: 'audio/mpeg',
  ogg: 'audio/ogg', wav: 'audio/wav', flac: 'audio/flac', m4a: 'audio/mp4', mp4: 'video/mp4', webm: 'video/webm', woff: 'font/woff',
  woff2: 'font/woff2', ttf: 'font/ttf', otf: 'font/otf', vtt: 'text/vtt; charset=utf-8', map: 'application/json',
};

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (ev) => ev.waitUntil(self.clients.claim()));

function pedir(cliente, cam) {
  return new Promise((ok) => {
    const canal = new MessageChannel();
    const t = setTimeout(() => ok(null), 15000);
    canal.port1.onmessage = (e) => { clearTimeout(t); ok(e.data); };
    cliente.postMessage({ t: 'vfs-ler', cam }, [canal.port2]);
  });
}

async function servir(cam) {
  const todos = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
  const paginas = todos.filter((c) => c.frameType === 'top-level' && !new URL(c.url).pathname.includes('/vfs/'));
  for (const p of paginas) {
    let r = await pedir(p, cam);
    if (r && r.dir) { cam = cam.replace(/\/?$/, '/index.html'); r = await pedir(p, cam); }
    if (r && r.ok) {
      const ext = (cam.split('.').pop() || '').toLowerCase();
      return new Response(r.dados, { status: 200, headers: { 'Content-Type': TIPOS[ext] || 'application/octet-stream', 'Cache-Control': 'no-store' } });
    }
  }
  const msg = paginas.length ? 'Arquivo não encontrado no disco do TNUX: ' + cam : 'O TNUX precisa estar aberto em uma aba para mostrar este arquivo.';
  return new Response(msg, { status: 404, headers: { 'Content-Type': 'text/plain; charset=utf-8' } });
}

self.addEventListener('fetch', (ev) => {
  const url = new URL(ev.request.url);
  const escopo = new URL(self.registration.scope).pathname;
  if (url.origin !== self.location.origin || !url.pathname.startsWith(escopo + 'vfs/')) return;
  let cam;
  try { cam = decodeURIComponent(url.pathname.slice(escopo.length + 3)); } catch (e) { return; }
  ev.respondWith(servir(cam));
});
