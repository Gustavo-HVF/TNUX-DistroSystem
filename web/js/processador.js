/*
 * TNUX: processar no computador.
 *
 * Quando a área de trabalho de um celular (Termux) é aberta no navegador de um computador, o
 * computador tem muito mais processador e memória do que o aparelho. Este arquivo põe essa força
 * para trabalhar:
 *
 *   1. Fica de olho na fila de trabalhos da ponte (o que o aparelho pede com "tnux pc ...").
 *   2. Para cada trabalho, lê os arquivos do aparelho pela ponte, processa aqui (em Web Workers,
 *      um por núcleo) e grava o resultado de volta no aparelho.
 *
 * Trabalhos: imagem (converter/reduzir), zip, unzip, hash (SHA-256) e comando (o shell do TNUX,
 * inclusive programas .wasm, rodando no computador sobre os arquivos enviados).
 * As mesmas operações aparecem no menu do Gerenciador de Arquivos.
 */
'use strict';

const Processador = (() => {
  const fs = Sistema.fs;
  const NUCLEOS = navigator.hardwareConcurrency || 2;
  const N = Math.max(1, Math.min(8, NUCLEOS - 1));
  const CAPS = ['imagem', 'zip', 'unzip', 'hash', 'comando'];

  /* ---------------------------------------------------------------- trabalhadores (um por núcleo) */
  const pool = [];
  const pend = new Map();
  let seq = 1;
  function novoWorker() {
    const w = new Worker(new URL('js/processador-worker.js', new URL('.', location.href)));
    w.carga = 0;
    w.onmessage = (ev) => {
      const p = pend.get(ev.data.id);
      if (!p) return;
      pend.delete(ev.data.id);
      p.w.carga--;
      if (ev.data.ok) p.ok(ev.data.r); else p.erro(new Error(ev.data.erro));
    };
    w.onerror = (e) => { for (const [id, p] of pend) if (p.w === w) { pend.delete(id); p.erro(new Error('o trabalhador parou: ' + (e.message || 'erro'))); } };
    return w;
  }
  /* manda uma operação para o trabalhador menos ocupado */
  function pedir(msg, transf, exclusivo) {
    return new Promise((ok, erro) => {
      let w;
      if (exclusivo) w = novoWorker();
      else {
        if (pool.length < N && pool.every((x) => x.carga > 0)) pool.push(novoWorker());
        if (!pool.length) pool.push(novoWorker());
        w = pool.reduce((a, b) => (b.carga < a.carga ? b : a));
      }
      const id = seq++;
      w.carga++;
      pend.set(id, { w, ok: (r) => { if (exclusivo) w.terminate(); ok(r); }, erro: (e) => { if (exclusivo) w.terminate(); erro(e); } });
      w.postMessage({ ...msg, id }, transf || []);
    });
  }
  /* roda f(item) para todos, no máximo N ao mesmo tempo */
  async function emParalelo(itens, f) {
    let prox = 0;
    const res = new Array(itens.length);
    await Promise.all(Array.from({ length: Math.min(N, itens.length) }, async () => {
      while (prox < itens.length) { const i = prox++; res[i] = await f(itens[i], i); }
    }));
    return res;
  }

  async function garantirPasta(cam) {
    if (cam === '/' || !cam) return;
    const inf = await fs.info(cam);
    if (inf) { if (inf.tipo !== 'd') throw new Error(cam + ' não é uma pasta'); return; }
    await garantirPasta(fs.pastaDe(cam));
    try { await fs.mkdir(cam); } catch (e) { if (!(await fs.info(cam))) throw e; }      /* outra tarefa pode ter criado ao mesmo tempo */
  }
  const semExt = (nome) => { const p = nome.lastIndexOf('.'); return p > 0 ? nome.slice(0, p) : nome; };
  const ext = (nome) => { const p = nome.lastIndexOf('.'); return p > 0 ? nome.slice(p + 1).toLowerCase() : ''; };

  /* imagem pelo canvas da página, para navegadores sem OffscreenCanvas nos Workers */
  async function imagemNaPagina(dados, t) {
    const bmp = await createImageBitmap(new Blob([dados]));
    const deL = bmp.width, deA = bmp.height;
    let l = deL, a = deA;
    if (t.largura && l > t.largura) { a = Math.max(1, Math.round(a * t.largura / l)); l = t.largura; }
    const c = h('canvas', { width: l, height: a });
    const ctx = c.getContext('2d');
    if (t.para === 'jpg') { ctx.fillStyle = '#fff'; ctx.fillRect(0, 0, l, a); }
    ctx.drawImage(bmp, 0, 0, l, a);
    const tipo = t.para === 'jpg' ? 'image/jpeg' : t.para === 'png' ? 'image/png' : 'image/webp';
    const blob = await new Promise((ok) => c.toBlob(ok, tipo, (t.qualidade || 82) / 100));
    if (!blob || blob.type !== tipo) throw new Error('este navegador não sabe gravar ' + t.para);
    return { dados: new Uint8Array(await blob.arrayBuffer()), largura: l, altura: a, deL, deA };
  }

  /* ---------------------------------------------------------------- os trabalhos */
  const fazer = {
    async imagem(t, prog) {
      const para = t.para === 'jpeg' ? 'jpg' : (t.para || 'webp');
      let feitos = 0, antes = 0, depois = 0;
      const linhas = [], falhas = [];
      if (t.saida) await garantirPasta(t.saida);
      await emParalelo(t.arquivos, async (cam) => {
        const nome = fs.nomeDe(cam);
        try {
          const dados = await fs.ler(cam);
          const tam = dados.length;
          let r;
          try { r = await pedir({ op: 'imagem', dados, para, largura: t.largura || 0, qualidade: t.qualidade || 82 }, [dados.buffer]); }
          catch (e) { if (!/OffscreenCanvas|convertToBlob/.test(e.message)) throw e; r = await imagemNaPagina(await fs.ler(cam), { ...t, para }); }
          /* nunca grava por cima do original */
          let novo = semExt(nome) + '.' + para;
          const pasta = t.saida || fs.pastaDe(cam);
          if (fs.juntar(pasta, novo) === cam) novo = `${semExt(nome)}-${r.largura}.${para}`;
          await fs.gravar(fs.juntar(pasta, novo), r.dados);
          antes += tam; depois += r.dados.length;
          linhas.push(`${nome} (${r.deL}×${r.deA}, ${tamanhoHumano(tam)}) → ${novo} (${r.largura}×${r.altura}, ${tamanhoHumano(r.dados.length)})`);
        } catch (e) { falhas.push(`${nome}: ${e.message}`); }
        prog(Math.round(++feitos * 100 / t.arquivos.length), `${feitos} de ${t.arquivos.length}: ${nome}`);
      });
      if (!linhas.length) throw new Error('nenhuma imagem pôde ser convertida\n' + falhas.join('\n'));
      return `${linhas.length} ${linhas.length === 1 ? 'imagem convertida' : 'imagens convertidas'} para ${para}: ${tamanhoHumano(antes)} → ${tamanhoHumano(depois)}\n` +
        linhas.sort().join('\n') + (falhas.length ? '\nNão convertidas:\n' + falhas.join('\n') : '');
    },

    async zip(t, prog) {
      /* junta a lista de arquivos (as pastas entram inteiras) */
      const entradas = [];
      const base = (t.base || '/').replace(/\/+$/, '') + '/';
      const nomeNoZip = (cam) => (cam.startsWith(base) ? cam.slice(base.length) : fs.nomeDe(cam));
      async function andar(cam, nome) {
        const inf = await fs.info(cam);
        if (!inf) throw new Error(cam + ': não existe');
        if (inf.tipo === 'f') { if (cam !== t.saida) entradas.push({ cam, nome, mtime: inf.mtime }); return; }
        const filhos = await fs.listar(cam);
        if (!filhos.length) entradas.push({ nome: nome + '/', pasta: true, mtime: inf.mtime });
        for (const f of filhos) await andar(fs.juntar(cam, f.nome), nome + '/' + f.nome);
      }
      for (const it of t.itens) await andar(it, nomeNoZip(it));
      if (!entradas.length) throw new Error('nada para compactar');
      let feitos = 0, bruto = 0;
      const prontas = await emParalelo(entradas, async (e) => {
        let r = { crc: 0, tam: 0, comp: null, dados: new Uint8Array(0) };
        if (!e.pasta) { const dados = await fs.ler(e.cam); r = await pedir({ op: 'compactar', dados }, [dados.buffer]); }
        bruto += r.tam;
        prog(Math.round(++feitos * 95 / entradas.length), `${feitos} de ${entradas.length}: ${e.nome}`);
        return r;
      });
      /* monta o arquivo zip */
      const enc = new TextEncoder(), partes = [], central = [];
      let pos = 0;
      entradas.forEach((e, i) => {
        const r = prontas[i], nome = enc.encode(e.nome), corpo = r.comp || r.dados, metodo = r.comp ? 8 : 0;
        const d = new Date((e.mtime || Date.now() / 1000) * 1000);
        const hora = (d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1);
        const data = (Math.max(0, d.getFullYear() - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate();
        const loc = new DataView(new ArrayBuffer(30));
        loc.setUint32(0, 0x04034b50, true); loc.setUint16(4, 20, true); loc.setUint16(6, 0x0800, true); loc.setUint16(8, metodo, true);
        loc.setUint16(10, hora, true); loc.setUint16(12, data, true); loc.setUint32(14, r.crc, true); loc.setUint32(18, corpo.length, true);
        loc.setUint32(22, r.tam, true); loc.setUint16(26, nome.length, true);
        const cen = new DataView(new ArrayBuffer(46));
        cen.setUint32(0, 0x02014b50, true); cen.setUint16(4, 0x031e, true); cen.setUint16(6, 20, true); cen.setUint16(8, 0x0800, true); cen.setUint16(10, metodo, true);
        cen.setUint16(12, hora, true); cen.setUint16(14, data, true); cen.setUint32(16, r.crc, true); cen.setUint32(20, corpo.length, true); cen.setUint32(24, r.tam, true);
        cen.setUint16(28, nome.length, true); cen.setUint32(38, ((e.pasta ? 0o40755 : 0o100644) << 16) >>> 0, true); cen.setUint32(42, pos, true);
        partes.push(loc.buffer, nome, corpo);
        central.push(cen.buffer, nome);
        pos += 30 + nome.length + corpo.length;
        if (pos > 0xfffffff0) throw new Error('o zip passaria de 4 GiB, que este compactador não faz');
      });
      const tamCentral = central.reduce((s, b) => s + b.byteLength, 0);
      const fim = new DataView(new ArrayBuffer(22));
      fim.setUint32(0, 0x06054b50, true); fim.setUint16(8, entradas.length, true); fim.setUint16(10, entradas.length, true);
      fim.setUint32(12, tamCentral, true); fim.setUint32(16, pos, true);
      const tudo = new Uint8Array(await new Blob([...partes, ...central, fim.buffer]).arrayBuffer());
      prog(97, 'gravando no aparelho');
      await garantirPasta(fs.pastaDe(t.saida));
      await fs.gravar(t.saida, tudo);
      return `${fs.nomeDe(t.saida)}: ${entradas.filter((e) => !e.pasta).length} arquivos, ${tamanhoHumano(bruto)} → ${tamanhoHumano(tudo.length)}`;
    },

    async unzip(t, prog) {
      const z = await fs.ler(t.arquivo);
      const dv = new DataView(z.buffer, z.byteOffset, z.byteLength);
      let f = -1;
      for (let i = z.length - 22; i >= 0 && i >= z.length - 66000; i--) if (dv.getUint32(i, true) === 0x06054b50) { f = i; break; }
      if (f < 0) throw new Error('não é um arquivo zip (ou está incompleto)');
      const qtd = dv.getUint16(f + 10, true);
      let p = dv.getUint32(f + 16, true);
      if (p === 0xffffffff) throw new Error('zip de formato grande (zip64): este extrator não abre');
      const dec = new TextDecoder();
      const itens = [];
      for (let i = 0; i < qtd; i++) {
        if (dv.getUint32(p, true) !== 0x02014b50) throw new Error('zip corrompido');
        const flags = dv.getUint16(p + 8, true), metodo = dv.getUint16(p + 10, true), crc = dv.getUint32(p + 16, true), comp = dv.getUint32(p + 20, true);
        const ln = dv.getUint16(p + 28, true), le = dv.getUint16(p + 30, true), lc = dv.getUint16(p + 32, true), local = dv.getUint32(p + 42, true);
        itens.push({ nome: dec.decode(z.subarray(p + 46, p + 46 + ln)), flags, metodo, crc, comp, local });
        p += 46 + ln + le + lc;
      }
      await garantirPasta(t.saida);
      let feitos = 0, bytes = 0;
      const pulados = [];
      /* primeiro as pastas, em ordem; depois os arquivos, em paralelo */
      const seguro = (n) => n && !n.startsWith('/') && !n.split('/').includes('..') && !n.includes('\\');
      const pastas = new Set();
      for (const it of itens) {
        if (!seguro(it.nome)) continue;
        const d = it.nome.endsWith('/') ? it.nome.replace(/\/+$/, '') : it.nome.includes('/') ? it.nome.slice(0, it.nome.lastIndexOf('/')) : '';
        if (d) pastas.add(d);
      }
      for (const d of [...pastas].sort()) await garantirPasta(fs.juntar(t.saida, d));
      const arquivos = itens.filter((it) => !it.nome.endsWith('/'));
      await emParalelo(arquivos, async (it) => {
        try {
          if (!seguro(it.nome)) throw new Error('nome perigoso, ignorado');
          if (it.flags & 1) throw new Error('protegido por senha');
          if (it.metodo !== 0 && it.metodo !== 8) throw new Error('método de compactação não suportado');
          const ini = it.local + 30 + dv.getUint16(it.local + 26, true) + dv.getUint16(it.local + 28, true);
          const pedaco = z.slice(ini, ini + it.comp);
          const r = await pedir({ op: 'expandir', dados: pedaco, metodo: it.metodo, crc: it.crc }, [pedaco.buffer]);
          await fs.gravar(fs.juntar(t.saida, it.nome), r.dados);
          bytes += r.dados.length;
        } catch (e) { pulados.push(`${it.nome}: ${e.message}`); }
        prog(Math.round(++feitos * 100 / arquivos.length), `${feitos} de ${arquivos.length}: ${it.nome}`);
      });
      if (arquivos.length && pulados.length === arquivos.length) throw new Error('nenhum arquivo pôde ser extraído\n' + pulados.slice(0, 20).join('\n'));
      return `${arquivos.length - pulados.length} arquivos extraídos em ${t.saida} (${tamanhoHumano(bytes)})` + (pulados.length ? '\nNão extraídos:\n' + pulados.slice(0, 30).join('\n') : '');
    },

    async hash(t, prog) {
      let feitos = 0;
      const linhas = await emParalelo(t.arquivos, async (cam) => {
        const dados = await fs.ler(cam);
        const r = await pedir({ op: 'sha256', dados }, [dados.buffer]);
        prog(Math.round(++feitos * 100 / t.arquivos.length), fs.nomeDe(cam));
        return `${r.hash}  ${fs.nomeDe(cam)}`;
      });
      return linhas.join('\n');
    },

    async comando(t, prog) {
      prog(5, 'copiando os arquivos para o computador');
      const arquivos = [];
      for (const cam of t.arquivos || []) arquivos.push({ nome: fs.nomeDe(cam), dados: await fs.ler(cam) });
      prog(25, 'rodando no computador');
      const r = await pedir({ op: 'comando', cmd: t.cmd, arquivos }, arquivos.map((a) => a.dados.buffer), true);
      prog(80, 'gravando o resultado no aparelho');
      for (const s of r.saidas) {
        const dest = fs.juntar(t.pasta, s.nome);
        await garantirPasta(fs.pastaDe(dest));
        await fs.gravar(dest, s.dados);
      }
      const resumo = r.saidas.length ? `\n(${r.saidas.length === 1 ? 'arquivo devolvido' : 'arquivos devolvidos'} ao aparelho: ${r.saidas.map((s) => s.nome).join(', ')})` : '';
      if (r.codigo !== 0) throw new Error((r.saida || '').trimEnd() + `\n(o comando terminou com o código ${r.codigo})` + resumo);
      return (r.saida || '').trimEnd() + resumo;
    },
  };

  async function executar(t, prog) {
    const f = fazer[t.tipo];
    if (!f) throw new Error('este computador não sabe fazer o trabalho "' + t.tipo + '"');
    return f(t, prog || (() => {}));
  }

  /* ---------------------------------------------------------------- atendendo a fila do aparelho (modo ponte) */
  const nome = `${Sistema.descricao()} #${Math.random().toString(36).slice(2, 6)}`;
  let ocupado = false, atual = 0, feitosAqui = 0, timer = null;
  const ativo = () => Sistema.modo === 'ponte' && Cfg.dados.emprestar !== false;

  async function ciclo() {
    if (!ativo()) return;
    if (ocupado) {            /* no meio de um trabalho: só avisa que este computador continua aqui */
      Sistema.ponte.json('GET', `api/trabalhos/presente?quem=${encodeURIComponent(nome)}&caps=${CAPS.join(',')}&nucleos=${NUCLEOS}&id=${atual}`).catch(() => {});
      return;
    }
    let r;
    try { r = await Sistema.ponte.json('GET', `api/trabalhos/pegar?quem=${encodeURIComponent(nome)}&caps=${CAPS.join(',')}&nucleos=${NUCLEOS}`); } catch (e) { return; }
    if (!r || !r.id) return;
    ocupado = true;
    const id = atual = r.id;
    let ultimo = 0;
    const prog = (p, msg) => {
      const agora = performance.now();
      if (agora - ultimo < 400 && p < 100) return;       /* não inunda a ponte de avisos */
      ultimo = agora;
      Sistema.ponte.pedir('POST', `api/trabalhos/andamento?id=${id}&p=${p}`, msg || '').catch(() => {});
    };
    try {
      const msg = await executar(r.trabalho, prog);
      await Sistema.ponte.pedir('POST', `api/trabalhos/fim?id=${id}&ok=1`, msg || '');
      feitosAqui++;
      Avisos.mostrar('Processado neste computador', (msg || '').split('\n')[0]);
    } catch (e) {
      await Sistema.ponte.pedir('POST', `api/trabalhos/fim?id=${id}&ok=0`, String(e && e.message || e)).catch(() => {});
      Avisos.mostrar('Um trabalho do aparelho falhou', String(e && e.message || e).split('\n')[0], 'erro');
    } finally { ocupado = false; setTimeout(ciclo, 50); }
  }
  function ligar() {
    if (timer || Sistema.modo !== 'ponte') return;
    timer = setInterval(ciclo, 1500);
    ciclo();
  }

  /* ---------------------------------------------------------------- uso direto pela área de trabalho */
  /* roda um trabalho aqui mesmo, mostrando o andamento num aviso; devolve o texto do resultado */
  async function local(t, titulo) {
    const barra = h('i', { style: 'width:0%' });
    const linha = h('span', null, 'começando...');
    const el = h('div.aviso', null, h('div', { style: 'flex:1;min-width:0' }, h('b', null, titulo), linha, h('div.medidor', null, barra)));
    $('#avisos').append(el);
    try {
      const msg = await executar(t, (p, m) => { barra.style.width = p + '%'; if (m) linha.textContent = m; });
      el.remove();
      return msg;
    } catch (e) { el.remove(); throw e; }
  }

  /* itens extras do menu do Gerenciador de Arquivos, conforme o que está selecionado */
  function itensDoMenu(caminhos, pasta, depois) {
    const IMG = ['png', 'jpg', 'jpeg', 'webp', 'gif', 'bmp', 'avif'];
    const onde = Sistema.modo === 'ponte' ? ' (processa neste computador)' : '';
    const rodar = async (t, titulo, mostrar) => {
      try {
        const msg = await local(t, titulo);
        await depois();
        if (mostrar) await Dlg.aviso(titulo, msg); else Avisos.mostrar(titulo, msg.split('\n')[0]);
      } catch (e) { Avisos.erro(titulo + ': não deu certo', e); await depois(); }
    };
    const itens = ['-'];
    if (caminhos.length && caminhos.every((c) => IMG.includes(ext(c)))) {
      itens.push({ rotulo: 'Converter imagens...' + onde, icone: 'recarregar', acao: async () => {
        const para = await Dlg._simples('Converter imagens', `Formato de saída para ${caminhos.length === 1 ? 'a imagem' : 'as ' + caminhos.length + ' imagens'}:`,
          [{ rotulo: 'Cancelar', valor: null }, { rotulo: 'PNG', valor: 'png' }, { rotulo: 'JPG', valor: 'jpg' }, { rotulo: 'WebP (menor)', valor: 'webp', classe: 'pri' }]);
        if (!para) return;
        const larg = await Dlg.pergunta('Converter imagens', 'Largura máxima em pontos (deixe 0 para não reduzir):', '0', { sim: 'Converter' });
        if (larg === null) return;
        rodar({ tipo: 'imagem', arquivos: caminhos, para, largura: parseInt(larg, 10) || 0, qualidade: 82 }, 'Converter imagens', true);
      } });
    }
    if (caminhos.length) {
      itens.push({ rotulo: 'Compactar em .zip' + onde, icone: 'baixar', acao: async () => {
        const sug = (caminhos.length === 1 ? semExt(fs.nomeDe(caminhos[0])) : fs.nomeDe(pasta) || 'arquivos') + '.zip';
        const nomeZip = await Dlg.pergunta('Compactar', 'Nome do arquivo zip:', sug, { sim: 'Compactar' });
        if (!nomeZip) return;
        rodar({ tipo: 'zip', saida: fs.juntar(pasta, nomeZip.trim()), base: pasta, itens: caminhos }, 'Compactar');
      } });
    }
    if (caminhos.length === 1 && ext(caminhos[0]) === 'zip') {
      itens.push({ rotulo: 'Extrair aqui' + onde, icone: 'enviar', acao: () => rodar({ tipo: 'unzip', arquivo: caminhos[0], saida: fs.juntar(pasta, semExt(fs.nomeDe(caminhos[0]))) }, 'Extrair') });
    }
    if (caminhos.length) itens.push({ rotulo: 'Calcular SHA-256' + onde, icone: 'cadeado', acao: () => rodar({ tipo: 'hash', arquivos: caminhos }, 'SHA-256', true) });
    return itens;
  }

  return { executar, local, itensDoMenu, ligar, get nome() { return nome; }, get nucleos() { return NUCLEOS; }, get feitos() { return feitosAqui; }, get ocupado() { return ocupado; }, ativo, caps: CAPS };
})();

/* ================================================================== janela "Processamento" */
Apps.processamento = () => {
  const jan = WM.abrir({ titulo: 'Processamento no computador', icone: Icones.app('monitor'), largura: 640, altura: 540, unica: 'processamento' });
  if (jan.corpo.childElementCount) return jan;
  const pag = h('div.pag');
  jan.corpo.append(pag);
  const linha = (rot, sub, ...val) => h('div.linha', null, h('div.rot', null, rot, sub ? h('small', null, sub) : null), ...val);
  async function atualizar() {
    if (jan.fechada) return;
    const itens = [h('h2', null, 'Processamento no computador')];
    if (Sistema.modo !== 'ponte') {
      itens.push(h('p', null, 'No modo ponte, o computador que abre o TNUX de um celular empresta o processador dele: o aparelho manda os arquivos, o computador processa e devolve o resultado.'),
        h('div.caixa.dica', null, h('b', null, 'Como usar'), h('pre', null, '# no Termux do celular:\ntnux desktop --rede\n\n# abra no navegador do computador o endereço http://IP-DO-CELULAR:8096/... que aparecer\n\n# depois, no celular:\ntnux pc imagem --para webp --largura 1600 *.jpg\ntnux pc zip fotos.zip Fotos/\ntnux pc rodar \'sort -n dados.txt | uniq -c > contagem.txt\' dados.txt')),
        h('p.sub', null, 'Neste modo (navegador), as mesmas operações estão no menu do Gerenciador de Arquivos: converter imagens, compactar, extrair e SHA-256.'));
    } else {
      const caixa = h('input', { type: 'checkbox', checked: Processador.ativo() });
      caixa.addEventListener('change', () => { Cfg.definir('emprestar', caixa.checked); atualizar(); });
      itens.push(h('p.sub', null, `O aparelho (${Sistema.info.sistema}) pode mandar serviço pesado para os computadores que estão com o TNUX dele aberto. No terminal do aparelho: `, h('code', null, 'tnux pc ajuda')),
        h('h3', null, 'Este computador'),
        h('div.grupo', null,
          linha('Emprestar o processador ao aparelho', `${Processador.nucleos} núcleos; sabe fazer: ${Processador.caps.join(', ')}`, caixa),
          linha('Identificação', null, h('span.val', null, Processador.nome)),
          linha('Estado', null, h('span.val', null, !Processador.ativo() ? 'desligado' : Processador.ocupado ? 'processando um trabalho' : 'esperando trabalhos')),
          linha('Trabalhos feitos nesta aba', null, h('span.val', null, String(Processador.feitos)))));
      try {
        const l = await Sistema.ponte.json('GET', 'api/trabalhos/lista');
        itens.push(h('h3', null, `Computadores conectados (${l.ajudantes.length})`),
          l.ajudantes.length ? h('div.grupo', null, l.ajudantes.map((a) => linha(a.nome + (a.nome === Processador.nome ? ' (este)' : ''), null, h('span.val', null, a.nucleos + ' núcleos'))))
            : h('p.sub', null, 'Nenhum (marque a caixa acima).'),
          h('h3', null, 'Trabalhos'),
          l.trabalhos.length ? h('div.grupo', null, l.trabalhos.map((t) => h('div.linha', null,
            h('div.rot', null, `#${t.id} ${t.tipo}`, h('small', null, (t.msg || '').split('\n')[0].slice(0, 110)), t.estado === 'rodando' ? h('div.medidor', null, h('i', { style: `width:${t.p}%` })) : null),
            h('span.etq' + (t.estado === 'feito' ? '.ok' : ''), null, t.estado === 'fila' ? 'na fila' : t.estado === 'rodando' ? t.p + '%' : t.estado))))
            : h('p.sub', null, 'Nenhum trabalho ainda. No terminal do aparelho, experimente: tnux pc hash ~/.bashrc'));
      } catch (e) { itens.push(h('p.sub', null, 'A ponte não respondeu: ' + e.message)); }
    }
    const rolagem = pag.scrollTop;
    pag.replaceChildren(...itens);
    pag.scrollTop = rolagem;
  }
  atualizar();
  const t = setInterval(atualizar, 1500);
  jan.aoFechou = () => clearInterval(t);
  return jan;
};
