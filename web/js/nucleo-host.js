/*
 * TNUX: o "hardware" do núcleo no JavaScript.
 *
 * O núcleo (web/tnux.wasm, escrito em C) não sabe falar com o mundo: ele pede tudo por oito
 * funções importadas (escrever no terminal, hora, rede, janelas...). Este arquivo fornece essas
 * funções e embrulha as exportadas numa interface cômoda. Também traz o carregador de programas
 * WebAssembly (WASI): as chamadas de sistema deles são repassadas ao núcleo.
 *
 * Funciona igual no Web Worker do navegador e no Node (para os testes).
 */
(function (global) {
  'use strict';

  const dec = new TextDecoder();
  const enc = new TextEncoder();

  /**
   * op: {
   *   wasm: ArrayBuffer|Uint8Array   bytes do tnux.wasm
   *   cfg: objeto                    configuração do boot (cpus, mem_mb, catalogo...)
   *   imagem: Uint8Array|null        disco gravado
   *   escrever(sid, Uint8Array)      saída de um terminal
   *   gui(objeto)                    pedido para a área de trabalho
   *   http({metodo,url,cab,corpo}) -> {status, corpo:Uint8Array, cab:string}   (síncrono)
   *   dormir(ms), interrompido() -> 0|1
   * }
   */
  function criarNucleo(op) {
    let ex = null;
    const u8 = () => new Uint8Array(ex.memory.buffer);
    const texto = (p, n) => dec.decode(u8().subarray(p, p + n));
    const cstr = (p) => { const m = u8(); let f = p; while (m[f]) f++; return dec.decode(m.subarray(p, f)); };
    const por = (bytes) => {          // copia bytes para a memória do núcleo (com \0); quem chama libera
      const p = ex.tnux_alloc(bytes.length + 1);
      const m = u8();
      m.set(bytes, p);
      m[p + bytes.length] = 0;
      return p;
    };
    const porTexto = (s) => por(enc.encode(s));
    const resultado = () => u8().slice(ex.tnux_res_ptr(), ex.tnux_res_ptr() + ex.tnux_res_len());
    const resultadoTexto = () => texto(ex.tnux_res_ptr(), ex.tnux_res_len());
    const comCaminho = (cam, fn) => { const p = porTexto(cam); try { return fn(p); } finally { ex.tnux_free(p); } };

    const env = {
      js_write(sid, p, n) { op.escrever(sid, u8().slice(p, p + n)); },
      js_agora() { return Date.now(); },
      js_http(m, url, cab, corpo, tam, saida) {
        let r;
        try {
          r = op.http({ metodo: cstr(m), url: cstr(url), cab: cstr(cab), corpo: tam ? u8().slice(corpo, corpo + tam) : null });
        } catch (e) {
          r = { status: 0, corpo: enc.encode(String(e && e.message || e)), cab: '' };
        }
        const bytes = r.corpo || new Uint8Array(0);
        const pc = por(bytes), ph = porTexto(r.cab || '');
        const dv = new DataView(ex.memory.buffer);
        dv.setInt32(saida, pc, true);
        dv.setInt32(saida + 4, bytes.length, true);
        dv.setInt32(saida + 8, ph, true);
        return r.status | 0;
      },
      js_gui(p, n) { try { op.gui(JSON.parse(texto(p, n))); } catch (e) { /* pedido malformado: ignora */ } },
      js_aleatorio(p, n) {
        const tmp = new Uint8Array(n);
        for (let i = 0; i < n; i += 65536) global.crypto.getRandomValues(tmp.subarray(i, Math.min(n, i + 65536)));
        u8().set(tmp, p);
      },
      js_dormir(ms) { op.dormir(ms); },
      js_wasm(p, n, a, na) {
        const bytes = u8().slice(p, p + n);
        let args;
        try { args = JSON.parse(texto(a, na)); } catch (e) { args = { args: [], env: [] }; }
        return rodarWasi(bytes, args);
      },
      js_interrompido() { return op.interrompido ? (op.interrompido() ? 1 : 0) : 0; },
    };

    /* ---------------------------------------------------------------- programas WASI */

    class Saida { constructor(codigo) { this.codigo = codigo; } }
    const ENOSYS = 52, EBADF = 8, EINVAL = 28, ESPIPE = 70;

    function rodarWasi(bytes, cfg) {
      const errTxt = (s) => { const b = enc.encode(s); const p = por(b); ex.tnux_wasi_write(2, p, b.length); ex.tnux_free(p); };
      let mod;
      try { mod = new WebAssembly.Module(bytes); }
      catch (e) { errTxt('bash: programa WebAssembly inválido: ' + e.message + '\n'); return 126; }

      let pmem = null;
      const P8 = () => new Uint8Array(pmem.buffer);
      const PV = () => new DataView(pmem.buffer);
      const args = (cfg.args || []).map((s) => enc.encode(s));
      const amb = (cfg.env || []).map((s) => enc.encode(s));
      const dirs = { 3: '/', 4: '.' };       /* diretórios pré-abertos: a raiz e o diretório atual */

      const caminho = (dirfd, ptr, len) => {
        const rel = dec.decode(P8().subarray(ptr, ptr + len));
        const base = dirs[dirfd];
        if (base === undefined) return null;
        if (rel.startsWith('/')) return rel;
        if (base === '.') return rel || '.';
        return base.replace(/\/$/, '') + '/' + rel;
      };
      const kcam = (cam, fn) => { const p = porTexto(cam); try { return fn(p); } finally { ex.tnux_free(p); } };
      const listas = (lista, pQtd, pTam) => {
        PV().setUint32(pQtd, lista.length, true);
        PV().setUint32(pTam, lista.reduce((t, b) => t + b.length + 1, 0), true);
        return 0;
      };
      const copiaLista = (lista, pVet, pBuf) => {
        for (const b of lista) {
          PV().setUint32(pVet, pBuf, true);
          pVet += 4;
          P8().set(b, pBuf);
          P8()[pBuf + b.length] = 0;
          pBuf += b.length + 1;
        }
        return 0;
      };
      const estado = (fn) => {             /* chama fn(ptr de 4 inteiros) e devolve os 4, ou o erro */
        const p = ex.tnux_alloc(16);
        try {
          const r = fn(p);
          if (r < 0) return -r;
          const dv = new DataView(ex.memory.buffer);
          return [dv.getInt32(p, true), dv.getInt32(p + 4, true), dv.getInt32(p + 8, true), dv.getInt32(p + 12, true)];
        } finally { ex.tnux_free(p); }
      };
      const gravaStat = (out, st) => {     /* struct filestat do WASI (64 bytes) */
        const v = PV();
        const tipo = st[0] === 1 ? 3 : st[0] === 2 ? 4 : st[0] === 3 ? 7 : 2;
        const ns = BigInt(st[2]) * 1000000000n;
        v.setBigUint64(out, 1n, true);
        v.setBigUint64(out + 8, BigInt(st[3] >>> 0), true);
        v.setUint8(out + 16, tipo);
        v.setBigUint64(out + 24, 1n, true);
        v.setBigUint64(out + 32, BigInt(st[1] >>> 0), true);
        v.setBigUint64(out + 40, ns, true);
        v.setBigUint64(out + 48, ns, true);
        v.setBigUint64(out + 56, ns, true);
        return 0;
      };

      const wasi = {
        args_sizes_get: (a, b) => listas(args, a, b),
        args_get: (a, b) => copiaLista(args, a, b),
        environ_sizes_get: (a, b) => listas(amb, a, b),
        environ_get: (a, b) => copiaLista(amb, a, b),
        clock_res_get(id, out) { PV().setBigUint64(out, 1000000n, true); return 0; },
        clock_time_get(id, prec, out) {
          const ms = id === 0 ? Date.now() : (global.performance ? global.performance.now() : Date.now());
          PV().setBigUint64(out, BigInt(Math.round(ms * 1000)) * 1000n, true);
          return 0;
        },
        random_get(buf, len) {
          const tmp = new Uint8Array(len);
          for (let i = 0; i < len; i += 65536) global.crypto.getRandomValues(tmp.subarray(i, Math.min(len, i + 65536)));
          P8().set(tmp, buf);
          return 0;
        },
        sched_yield: () => 0,
        proc_exit(codigo) { throw new Saida(codigo); },
        fd_write(fd, iovs, n, pEscritos) {
          let total = 0;
          for (let i = 0; i < n; i++) {
            const ptr = PV().getUint32(iovs + i * 8, true), len = PV().getUint32(iovs + i * 8 + 4, true);
            if (!len) continue;
            const kp = por(P8().subarray(ptr, ptr + len));
            const r = ex.tnux_wasi_write(fd, kp, len);
            ex.tnux_free(kp);
            if (r < 0) return -r;
            total += r;
          }
          PV().setUint32(pEscritos, total, true);
          return 0;
        },
        fd_read(fd, iovs, n, pLidos) {
          let total = 0;
          for (let i = 0; i < n; i++) {
            const ptr = PV().getUint32(iovs + i * 8, true), len = PV().getUint32(iovs + i * 8 + 4, true);
            if (!len) continue;
            const kp = ex.tnux_alloc(len);
            const r = ex.tnux_wasi_read(fd, kp, len);
            if (r > 0) P8().set(u8().subarray(kp, kp + r), ptr);
            ex.tnux_free(kp);
            if (r < 0) return -r;
            total += r;
            if (r < len) break;
          }
          PV().setUint32(pLidos, total, true);
          return 0;
        },
        fd_close(fd) { delete dirs[fd]; const r = ex.tnux_wasi_close(fd); return r < 0 ? -r : 0; },
        fd_seek(fd, desloc, deOnde, out) {
          const r = ex.tnux_wasi_seek(fd, Number(desloc), deOnde);
          if (r < 0) return ESPIPE;
          PV().setBigUint64(out, BigInt(Math.trunc(r)), true);
          return 0;
        },
        fd_tell(fd, out) {
          const r = ex.tnux_wasi_seek(fd, 0, 1);
          if (r < 0) return ESPIPE;
          PV().setBigUint64(out, BigInt(Math.trunc(r)), true);
          return 0;
        },
        fd_fdstat_get(fd, out) {
          let tipo = 2;                     /* dispositivo de caracteres (terminal) */
          if (fd >= 3) {
            if (dirs[fd] !== undefined) tipo = 3;
            else {
              const st = estado((p) => ex.tnux_wasi_fstat(fd, p));
              if (typeof st === 'number') return st;
              tipo = st[0] === 1 ? 3 : 4;
            }
          }
          const v = PV();
          v.setUint8(out, tipo);
          v.setUint16(out + 2, 0, true);
          v.setBigUint64(out + 8, 0x1fffffffn, true);
          v.setBigUint64(out + 16, 0x1fffffffn, true);
          return 0;
        },
        fd_fdstat_set_flags: () => 0,
        fd_sync: () => 0, fd_datasync: () => 0, fd_advise: () => 0, fd_allocate: () => 0,
        fd_prestat_get(fd, out) {
          if (fd !== 3 && fd !== 4) return EBADF;
          PV().setUint8(out, 0);
          PV().setUint32(out + 4, dirs[fd].length, true);
          return 0;
        },
        fd_prestat_dir_name(fd, ptr, len) {
          if (fd !== 3 && fd !== 4) return EBADF;
          P8().set(enc.encode(dirs[fd]).subarray(0, len), ptr);
          return 0;
        },
        fd_filestat_get(fd, out) {
          let st;
          if (fd === 3 || fd === 4) st = kcam(dirs[fd], (p) => estado((o) => ex.tnux_wasi_stat(p, 1, o)));
          else st = estado((p) => ex.tnux_wasi_fstat(fd, p));
          return typeof st === 'number' ? st : gravaStat(out, st);
        },
        path_filestat_get(dirfd, flags, ptr, len, out) {
          const cam = caminho(dirfd, ptr, len);
          if (cam === null) return EBADF;
          const st = kcam(cam, (p) => estado((o) => ex.tnux_wasi_stat(p, flags & 1, o)));
          return typeof st === 'number' ? st : gravaStat(out, st);
        },
        path_open(dirfd, dirflags, ptr, len, oflags, direitos, herdados, fdflags, pFd) {
          const cam = caminho(dirfd, ptr, len);
          if (cam === null) return EBADF;
          let flags = oflags & 15;
          if (fdflags & 1) flags |= 16;
          if ((BigInt(direitos) & 64n) !== 0n && !(oflags & 2)) flags |= 32;
          const st = kcam(cam, (p) => estado((o) => ex.tnux_wasi_stat(p, 1, o)));
          const ehDir = Array.isArray(st) && st[0] === 1;
          if (ehDir) flags &= ~32;
          const fd = kcam(cam, (p) => ex.tnux_wasi_open(p, flags));
          if (fd < 0) return -fd;
          if (ehDir) dirs[fd] = cam;
          PV().setUint32(pFd, fd, true);
          return 0;
        },
        fd_readdir(fd, buf, tam, cookie, pUsado) {
          let i = Number(cookie), usado = 0;
          const nome = ex.tnux_alloc(260), tipo = ex.tnux_alloc(4);
          try {
            for (;;) {
              const l = ex.tnux_wasi_readdir(fd, i, nome, 260, tipo);
              if (l < 0) break;
              const t = new DataView(ex.memory.buffer).getInt32(tipo, true);
              const cab = new Uint8Array(24 + l);
              const dv = new DataView(cab.buffer);
              dv.setBigUint64(0, BigInt(i + 1), true);
              dv.setBigUint64(8, BigInt(i + 1), true);
              dv.setUint32(16, l, true);
              dv.setUint8(20, t === 1 ? 3 : t === 3 ? 7 : 4);
              cab.set(u8().subarray(nome, nome + l), 24);
              const cabe = Math.min(cab.length, tam - usado);
              P8().set(cab.subarray(0, cabe), buf + usado);
              usado += cabe;
              if (cabe < cab.length) break;
              i++;
            }
          } finally { ex.tnux_free(nome); ex.tnux_free(tipo); }
          PV().setUint32(pUsado, usado, true);
          return 0;
        },
        path_unlink_file(dirfd, ptr, len) {
          const cam = caminho(dirfd, ptr, len);
          if (cam === null) return EBADF;
          const r = kcam(cam, (p) => ex.tnux_wasi_unlink(p, 0));
          return r < 0 ? -r : 0;
        },
        path_remove_directory(dirfd, ptr, len) {
          const cam = caminho(dirfd, ptr, len);
          if (cam === null) return EBADF;
          const r = kcam(cam, (p) => ex.tnux_wasi_unlink(p, 1));
          return r < 0 ? -r : 0;
        },
        path_create_directory(dirfd, ptr, len) {
          const cam = caminho(dirfd, ptr, len);
          if (cam === null) return EBADF;
          const r = kcam(cam, (p) => ex.tnux_wasi_mkdir(p));
          return r < 0 ? -r : 0;
        },
        path_rename(dirfd, ptr, len, dirfd2, ptr2, len2) {
          const a = caminho(dirfd, ptr, len), b = caminho(dirfd2, ptr2, len2);
          if (a === null || b === null) return EBADF;
          const pa = porTexto(a), pb = porTexto(b);
          const r = ex.tnux_wasi_rename(pa, pb);
          ex.tnux_free(pa); ex.tnux_free(pb);
          return r < 0 ? -r : 0;
        },
        path_readlink: () => EINVAL,
        poll_oneoff: () => ENOSYS,
      };

      /* só entrega o que o programa pede; o que o TNUX não tem responde "não implementado" */
      const imports = {};
      for (const imp of WebAssembly.Module.imports(mod)) {
        if (imp.kind !== 'function') {
          errTxt(`bash: este programa precisa de "${imp.module}.${imp.name}" (${imp.kind}), que o TNUX não fornece\n`);
          return 126;
        }
        imports[imp.module] = imports[imp.module] || {};
        if (imp.module === 'wasi_snapshot_preview1' || imp.module === 'wasi_unstable') imports[imp.module][imp.name] = wasi[imp.name] || (() => ENOSYS);
        else {
          errTxt(`bash: este programa não é WASI puro: precisa de "${imp.module}.${imp.name}" (feito para outro ambiente, como o Emscripten)\n`);
          return 126;
        }
      }
      let inst;
      try { inst = new WebAssembly.Instance(mod, imports); }
      catch (e) { errTxt('bash: não foi possível carregar o programa: ' + e.message + '\n'); return 126; }
      pmem = inst.exports.memory;
      if (!pmem || typeof inst.exports._start !== 'function') {
        errTxt('bash: o arquivo .wasm não é um programa (falta a função _start ou a memória exportada)\n');
        return 126;
      }
      try {
        inst.exports._start();
        return 0;
      } catch (e) {
        if (e instanceof Saida) return e.codigo & 255;
        errTxt('O programa parou com um erro: ' + (e && e.message || e) + '\n');
        return 134;
      }
    }

    /* ---------------------------------------------------------------- liga o núcleo */

    const modulo = new WebAssembly.Module(op.wasm);
    const instancia = new WebAssembly.Instance(modulo, { env });
    ex = instancia.exports;
    {
      const pc = porTexto(JSON.stringify(op.cfg || {}));
      const lc = enc.encode(JSON.stringify(op.cfg || {})).length;
      let pi = 0, li = 0;
      if (op.imagem && op.imagem.length) { pi = por(op.imagem); li = op.imagem.length; }
      ex.tnux_boot(pc, lc, pi, li);
      ex.tnux_free(pc);
      if (pi) ex.tnux_free(pi);
    }

    const erroFs = (cod, cam) => { const e = new Error(`${cam}: ${cstr(ex.tnux_erro(cod))}`); e.codigo = cod; return e; };

    return {
      sessaoAbrir: (sid, cols, rows) => ex.tnux_sessao_abrir(sid, cols, rows),
      sessaoFechar: (sid) => ex.tnux_sessao_fechar(sid),
      redimensionar: (sid, cols, rows) => ex.tnux_redimensionar(sid, cols, rows),
      entrada(sid, dados) {
        const b = typeof dados === 'string' ? enc.encode(dados) : dados;
        const p = por(b);
        try { ex.tnux_entrada(sid, p, b.length); } finally { ex.tnux_free(p); }
      },
      /* roda um comando sem terminal: devolve { codigo, saida } */
      rodar(cmd, root) {
        const codigo = comCaminho(cmd, (p) => ex.tnux_rodar(p, root ? 1 : 0));
        return { codigo, saida: resultadoTexto() };
      },
      fsListar(cam) { const r = comCaminho(cam, (p) => ex.tnux_fs_listar(p)); if (r) throw erroFs(r, cam); return JSON.parse(resultadoTexto()); },
      fsInfo(cam) { const r = comCaminho(cam, (p) => ex.tnux_fs_info(p)); if (r) return null; return JSON.parse(resultadoTexto()); },
      fsLer(cam) { const r = comCaminho(cam, (p) => ex.tnux_fs_ler(p)); if (r) throw erroFs(r, cam); return resultado(); },
      fsGravar(cam, bytes, root) {
        const b = typeof bytes === 'string' ? enc.encode(bytes) : bytes;
        const pc = porTexto(cam), pd = por(b);
        const r = ex.tnux_fs_gravar(pc, pd, b.length, root ? 1 : 0);
        ex.tnux_free(pc); ex.tnux_free(pd);
        if (r) throw erroFs(r, cam);
      },
      apps() { ex.tnux_apps(); return JSON.parse(resultadoTexto()); },
      imagem() { ex.tnux_imagem(); return resultado(); },
      sujo: () => !!ex.tnux_sujo(),
      memoria: () => ({ usado: ex.tnux_heap_usado(), total: ex.tnux_heap_total() }),
    };
  }

  const api = { criarNucleo };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else global.TnuxNucleo = api;
})(typeof self !== 'undefined' ? self : globalThis);
