/*
 * TNUX: o LNUX dentro da área de trabalho, e o compartilhamento pela internet (túnel da Cloudflare).
 */
'use strict';

/* ================================================================== LNUX */
/* cada opção: [nome, argumentos do "lnux", descrição, só no Termux?]. Um argumento pode ser
 * { pergunta }, { arquivo } ou { pasta }: a área de trabalho pede antes de rodar. */
const OPCOES_LNUX = [
  ['Geral', [
    ['Menu completo do LNUX', [], 'Todas as funções do LNUX, no menu dele'],
    ['Ficha do sistema', ['ficha'], 'Resumo do aparelho, no estilo do neofetch'],
    ['Diagnóstico', ['doctor'], 'Confere o que está instalado e o que falta'],
    ['Atualizar tudo', ['atualizar'], 'Termux, pacotes Python, distros e o próprio LNUX'],
    ['Limpeza rápida', ['limpar'], 'Limpa caches e temporários, com segurança'],
    ['Painel do sistema', ['dashboard'], 'Bateria, memória, armazenamento e rede, ao vivo'],
    ['Gerenciador de tarefas', ['tarefas'], 'Sua lista de tarefas no terminal'],
  ]],
  ['Música e vídeo', [
    ['Baixar música', ['baixar', { pergunta: 'Link do YouTube ou do Spotify, ou o nome da música:' }], 'Do YouTube ou do Spotify, ou por busca'],
    ['Baixar vídeo', ['video', { pergunta: 'Link do TikTok ou do YouTube, ou o que procurar:' }], 'Do TikTok, do YouTube e de outros sites'],
    ['Reduzir um vídeo', ['reduzir-video', { arquivo: 'Escolha o vídeo' }], 'Uma cópia numa resolução menor (bom para o WhatsApp)'],
    ['Compartilhar um vídeo por link', ['compartilhar', { arquivo: 'Escolha o vídeo' }], 'Um link para assistir no navegador'],
    ['Tocar a biblioteca', ['tocar'], 'As músicas baixadas'],
  ]],
  ['Rede e internet', [
    ['Compartilhar uma pasta', ['pasta', { pasta: 'Pasta a compartilhar no navegador:' }], 'Os outros baixam (e, se você deixar, mandam) arquivos'],
    ['Publicar uma pasta como site', ['site', { pasta: 'Pasta do site (com o index.html):' }], 'Um servidor de páginas para a pasta'],
    ['Proxy: a internet do celular no PC', ['proxy'], 'Proxy HTTP e SOCKS5'],
    ['Navegador pelo celular', ['navegador'], 'Abre sites pela conexão do aparelho'],
    ['Servidor de vídeos', ['midia'], 'Links de compartilhamento de vídeos'],
    ['Transmitir a tela do celular', ['tela'], 'A tela do celular num link secreto', true],
  ]],
  ['Celular (Termux)', [
    ['Controle da TV', ['tv', 'controle'], 'Pelo infravermelho ou pelo Wi-Fi', true],
    ['Desktop de uma distro', ['desktop'], 'Abre o desktop de uma distro no Termux:X11', true],
    ['Player de música', ['player', 'abrir'], 'O player na notificação', true],
  ]],
  ['Jogos e programação', [
    ['Cobrinha', ['snake'], 'O jogo da cobrinha'],
    ['Interpretador BASIC', ['basic'], 'Programe em BASIC'],
    ['Apps Web do LNUX', ['apps'], 'Os editores de vídeo, som e efeitos no navegador'],
  ]],
];

/* põe um argumento entre aspas para o shell do tnux */
const aspas = (s) => (!/[\s'"\\$`|&;<>(){}*?#~!]/.test(s) && s ? s : !s.includes("'") ? `'${s}'` : !s.includes('"') ? `"${s}"` : `'${s.replace(/'/g, '')}'`);

Apps.lnux = () => {
  const jan = WM.abrir({ titulo: 'LNUX', icone: Icones.app('lnux'), largura: 760, altura: 560, unica: 'lnux' });
  if (jan.corpo.childElementCount) return jan;
  const pag = h('div.pag');
  jan.corpo.append(pag);
  const ponte = Sistema.modo === 'ponte', termux = !!Sistema.info.termux;
  const topo = h('div.bv-topo', null, h('span', { html: Icones.app('lnux'), style: 'display:contents' }),
    h('div', null, h('h2', null, 'LNUX'), h('p.sub', null, 'O canivete suíço para o terminal, do mesmo autor do TNUX: downloads de música e vídeo, servidores, rede, arquivos, jogos e muito mais.')));

  if (!ponte) {
    pag.append(topo, h('div.caixa.dica', null, h('b', null, 'O LNUX roda no aparelho (modo ponte)'),
      h('p', { style: 'margin:6px 0 0' }, 'Ele é um programa em Python que usa o Termux e as ferramentas do Linux, então não roda dentro do navegador. No terminal do aparelho:'),
      h('pre', null, TEXTO_PONTE.join('\n') + '\ntnux instalar lnux')));
    return jan;
  }
  async function rodar(nome, args) {
    const final = [];
    for (const a of args) {
      if (typeof a === 'string') { final.push(a); continue; }
      let v = null;
      if (a.pergunta) v = await Dlg.pergunta(nome, a.pergunta, '', { sim: 'Continuar' });
      else if (a.arquivo) v = await Apps.escolher({ titulo: a.arquivo, modo: 'abrir', inicio: Sistema.fs.casa });
      else if (a.pasta) v = await Dlg.pergunta(nome, a.pasta, Sistema.fs.casa, { sim: 'Continuar' });
      if (!v || !v.trim()) return;
      final.push(v.trim());
    }
    Apps.terminal({ titulo: 'LNUX: ' + nome, cmd: ['tnux', 'lnux', ...final].map(aspas).join(' ') });
  }
  function desenhar() {
    const itens = [topo];
    if (!Sistema.info.lnux) {
      itens.push(h('div.caixa.dica', null, h('b', null, 'O LNUX ainda não está instalado neste aparelho.'),
        h('p', { style: 'margin:6px 0 10px' }, 'O TNUX baixa do GitHub e roda o instalador dele (precisa do git e do Python).'),
        h('button.btn.pri', { onclick: () => {
          const t = Apps.terminal({ titulo: 'Instalando o LNUX', cmd: 'tnux instalar lnux' });
          const antes = t.aoFechou;
          t.aoFechou = async () => { if (antes) antes(); try { Object.assign(Sistema.info, await Sistema.ponte.json('GET', 'api/info')); } catch (e) { /* */ } if (!jan.fechada) desenhar(); };
        } }, 'Instalar o LNUX')));
    } else itens.push(h('p.sub', null, 'Instalado em ', h('code', null, Sistema.info.lnux), '. Cada opção abre num terminal. No terminal, o mesmo: ', h('code', null, 'tnux lnux ficha'), '.'));
    for (const [cat, ops] of OPCOES_LNUX) {
      itens.push(h('h3', null, cat));
      itens.push(h('div.bv-cartoes', null, ops.map(([nome, args, desc, soTermux]) => {
        const off = !Sistema.info.lnux || (soTermux && !termux);
        const b = h('button', { disabled: off, title: soTermux && !termux ? 'Só no Termux (Android)' : desc, onclick: () => rodar(nome, args) },
          h('div', null, h('b', null, nome), h('small', null, soTermux && !termux ? 'só no Termux (Android)' : desc)));
        if (off) b.style.opacity = '.5';
        return b;
      })));
    }
    const rolagem = pag.scrollTop;
    pag.replaceChildren(...itens);
    pag.scrollTop = rolagem;
  }
  desenhar();
  return jan;
};

/* ================================================================== Compartilhar pela internet */
Apps.compartilhar = () => {
  const jan = WM.abrir({ titulo: 'Compartilhar pela internet', icone: Icones.app('compartilhar'), largura: 620, altura: 620, unica: 'compartilhar' });
  if (jan.corpo.childElementCount) return jan;
  const pag = h('div.pag');
  jan.corpo.append(pag);
  const topo = h('div.bv-topo', null, h('span', { html: Icones.app('compartilhar'), style: 'display:contents' }),
    h('div', null, h('h2', null, 'Compartilhar pela internet'),
      h('p.sub', null, 'Um link https, feito pela Cloudflare (de graça e sem conta), que abre esta área de trabalho de qualquer lugar: de outra casa, do 4G, de outro computador.')));
  if (Sistema.modo !== 'ponte') {
    pag.append(topo, h('div.caixa.dica', null, h('b', null, 'Só no modo ponte'),
      h('p', { style: 'margin:6px 0 0' }, 'No modo navegador o TNUX roda inteiro nesta aba, e não há o que compartilhar. No terminal do aparelho:'),
      h('pre', null, TEXTO_PONTE.slice(0, 3).join('\n') + '\ntnux instalar cloudflared\ntnux desktop --tunel')));
    return jan;
  }
  const pelaInternet = /\.trycloudflare\.com$/.test(location.hostname);
  let ultimo = '', qrDe = '';
  const qr = h('img', { alt: 'QR code do link', width: 220, height: 220, style: 'display:block;margin:6px auto;background:#fff;border-radius:8px;image-rendering:pixelated' });
  async function atualizar() {
    if (jan.fechada) return;
    let e;
    try { e = await Sistema.ponte.json('GET', 'api/tunel/estado'); } catch (er) { pag.replaceChildren(topo, h('p.sub', null, 'A ponte não respondeu: ' + er.message)); return; }
    const chave = JSON.stringify(e);
    if (chave === ultimo) return;
    ultimo = chave;
    const itens = [topo];
    if (!e.disponivel) {
      itens.push(h('div.caixa.dica', null, h('b', null, 'Falta o cloudflared neste aparelho'),
        h('p', { style: 'margin:6px 0 10px' }, 'É o programa da Cloudflare que cria o túnel.'),
        h('button.btn.pri', { onclick: () => {
          const t = Apps.terminal({ titulo: 'Instalando o cloudflared', cmd: 'tnux instalar cloudflared' });
          const antes = t.aoFechou;
          t.aoFechou = () => { if (antes) antes(); ultimo = ''; atualizar(); };
        } }, 'Instalar o cloudflared')));
    } else if (!e.ativo) {
      itens.push(h('div.caixa', null,
        h('p', { style: 'margin:0 0 10px' }, e.erro ? 'O último túnel não ligou: ' + e.erro : 'O túnel está desligado.'),
        h('button.btn.pri', { onclick: async (ev) => {
          ev.target.disabled = true;
          try { await Sistema.ponte.json('POST', 'api/tunel/iniciar'); } catch (er) { Avisos.erro('Não foi possível ligar o túnel', er); }
          ultimo = ''; atualizar();
        } }, 'Criar o link')));
    } else if (!e.pronto) {
      itens.push(h('div.caixa', null, h('p', { style: 'margin:0' }, 'Ligando o túnel da Cloudflare... (alguns segundos)')));
    } else {
      const campo = h('input.campo', { type: 'text', readOnly: true, value: e.link, style: 'flex:1;font:12.5px var(--mono)', onfocus: (ev) => ev.target.select() });
      if (qrDe !== e.link) { qrDe = e.link; qr.src = new URL('api/tunel/qr.svg?token=' + encodeURIComponent(sessionStorage.getItem('tnux_token') || '') + '&v=' + encodeURIComponent(e.link.slice(-12)), location.href).href; }
      itens.push(
        h('h3', null, 'O link'),
        h('div', { style: 'display:flex;gap:6px' }, campo, h('button.btn.pri', { onclick: () => {
          const ok = () => Avisos.mostrar('Link copiado', 'Mande só para quem deve usar este aparelho.');
          if (navigator.clipboard) navigator.clipboard.writeText(e.link).then(ok).catch(() => { campo.select(); document.execCommand('copy'); ok(); });
          else { campo.select(); document.execCommand('copy'); ok(); }
        } }, 'Copiar')),
        qr,
        h('p.sub', { style: 'text-align:center;margin:0' }, 'Aponte a câmera de outro celular para abrir.'),
        h('div.caixa', { style: 'border-color:#e5484d55' }, h('b', null, 'Cuidado: '), 'quem tiver este link controla este aparelho como você (terminal, arquivos, programas). Mande só para quem você confia. O link tem uma chave própria e deixa de funcionar quando você desliga o túnel; ao ligar de novo, sai um link novo.'),
        h('div', { style: 'display:flex;gap:8px;justify-content:flex-end' },
          h('button.btn.perigo', { onclick: async () => {
            if (pelaInternet && !(await Dlg.confirma('Desligar o túnel?', 'Você está usando o TNUX por este túnel: ao desligar, esta página perde a conexão.', { sim: 'Desligar', perigo: true }))) return;
            await Sistema.ponte.json('POST', 'api/tunel/parar').catch(() => {});
            ultimo = ''; qrDe = ''; atualizar();
          } }, 'Desligar o link')));
    }
    itens.push(h('p.sub', null, 'No terminal do aparelho: ', h('code', null, 'tnux compartilhar'), ' mostra o link com o QR code, e ', h('code', null, 'tnux compartilhar --parar'), ' desliga.'));
    const rolagem = pag.scrollTop;
    pag.replaceChildren(...itens);
    pag.scrollTop = rolagem;
  }
  atualizar();
  const t = setInterval(atualizar, 1000);
  jan.aoFechou = () => clearInterval(t);
  return jan;
};
