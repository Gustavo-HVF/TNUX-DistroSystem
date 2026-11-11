/*
 * TNUX: Gerenciador de Aplicativos, Navegador, Configurações, Monitor, Visualizador de Imagens,
 * player de mídia, Calculadora, Informações do Sistema e a tela de boas-vindas.
 */
'use strict';

const REPO = 'https://github.com/Gustavo-HVF/TNUX-DistroSystem';
const TEXTO_PONTE = [
  'git clone ' + REPO,
  'cd TNUX-DistroSystem',
  'bash instalar.sh',
  'tnux desktop',
];

/* ================================================================== Gerenciador de Aplicativos */
Apps.loja = async (arg) => {
  const jan = WM.abrir({ titulo: 'Gerenciador de Aplicativos', icone: Icones.app('loja'), largura: 900, altura: 580, unica: 'loja' });
  if (jan.corpo.childElementCount) { if (arg && arg.remover) jan.acao('remover', arg.remover); return jan; }
  const ponte = Sistema.modo === 'ponte';
  const cat = await Sistema.pacotes.catalogo();
  let instalados = new Set(), filtro = 'Todos', termo = '';
  const busca = h('input.campo', { type: 'search', placeholder: 'Procurar programas...', 'aria-label': 'Procurar programas' });
  const gh = h('input.campo', { type: 'text', placeholder: 'usuario/repositorio', spellcheck: false, style: 'flex:0 1 210px', 'aria-label': 'Repositório do GitHub' });
  const cats = h('div.loja-cats'), lista = h('div.loja-lista');
  jan.corpo.append(h('div.barra', null, busca, h('div.sep'), gh,
    h('button.btn', { title: 'Baixa um repositório do GitHub e, se for um site, põe no menu', onclick: () => { const v = gh.value.trim().replace(/^https?:\/\/github\.com\//, '').replace(/\.git$/, ''); if (/^[\w.-]+\/[\w.-]+$/.test(v)) acao('instalar', v, v); else Avisos.mostrar('Repositório do GitHub', 'Escreva no formato usuario/repositorio (por exemplo: Gustavo-HVF/LNUX-APPs).', 'atencao'); } }, 'Instalar do GitHub')),
  h('div.loja', null, cats, lista));

  const ROTULO = { wasm: 'programa de terminal', git: 'app web', web: 'site', nativo: 'programa Linux', script: 'script', interno: 'do sistema' };

  function acao(tipo, id, nome) {
    const cmd = Sistema.pacotes.comando(tipo, id);
    const t = Apps.terminal(ponte ? { titulo: (tipo === 'instalar' ? 'Instalando ' : 'Removendo ') + (nome || id), cmd } : { titulo: (tipo === 'instalar' ? 'Instalando ' : 'Removendo ') + (nome || id), digitar: cmd });
    const antes = t.aoFechou;
    t.aoFechou = () => { if (antes) antes(); atualizar(); Desktop.atualizarApps(); };
    if (!ponte) Avisos.mostrar(tipo === 'instalar' ? 'Instalando' : 'Removendo', 'Acompanhe no terminal. Quando terminar, pode fechá-lo.');
  }
  jan.acao = acao;

  function explicarPonte(p) {
    Dlg.abrir([
      h('h3', null, p.nome + ' é um programa Linux nativo'),
      h('div.txt', null,
        h('p', null, 'Dentro do navegador, sozinho, o TNUX roda apps web, scripts e programas WebAssembly. Programas Linux de verdade (como este) precisam de um aparelho para rodar: é o modo ponte.'),
        h('p', null, 'No Termux (Android) ou no terminal do Linux:'),
        h('div.caixa', null, h('pre', null, TEXTO_PONTE.join('\n'))),
        h('p', null, 'Abra o endereço que aparecer. Aí este Gerenciador instala o ' + p.nome + ' no aparelho (pelo apt, pkg, pacman...) e você o usa por aqui.'),
        p.nativo && p.nativo.nota ? h('p.sub', null, p.nativo.nota) : null),
      h('div.pe', null, h('button.btn.pri', { onclick: (e) => e.target.closest('.veu').remove() }, 'Entendi')),
    ]);
  }

  function cartao(p) {
    const inst = instalados.has(p.id), disp = Sistema.pacotes.disponivel(p);
    const item = Desktop.apps.find((a) => a.id === p.id);
    const el = h('div.pacote', null);
    el.innerHTML = Icones.app(p.icone).replace('<svg', '<svg class="ic"');
    const pe = h('div.pe', null, h('span.etq' + (p.tipo === 'nativo' ? '.ponte' : ''), null, ROTULO[p.tipo] || p.tipo), inst ? h('span.etq.ok', null, 'instalado') : null, h('span', { style: 'flex:1' }));
    if (!disp) pe.append(h('button.btn', { onclick: () => explicarPonte(p) }, 'Como instalar'));
    else if (inst) {
      if (item) pe.append(h('button.btn', { onclick: () => Desktop.abrir(item) }, 'Abrir'));
      else if (p.comando && !ponte) pe.append(h('button.btn', { onclick: () => Apps.terminal({ digitar: p.comando }) }, 'Abrir'));
      pe.append(h('button.btn', { onclick: () => acao('remover', p.id, p.nome) }, 'Remover'));
    } else pe.append(h('button.btn.pri', { onclick: () => acao('instalar', p.id, p.nome) }, 'Instalar'));
    el.append(h('div.inf', null, h('b', { title: p.id }, p.nome), h('p', { title: p.resumo }, p.resumo), pe));
    return el;
  }
  function desenhar() {
    const sa = (s) => s.normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase();
    const t = sa(termo);
    let ps = cat.pacotes.filter((p) => ponte ? p.tipo !== 'wasm' && p.tipo !== 'script' : true);
    if (t) ps = ps.filter((p) => sa(p.nome).includes(t) || sa(p.resumo).includes(t) || p.id.includes(t) || (p.apelidos || []).some((a) => a.includes(t)));
    else if (filtro === 'Instalados') ps = ps.filter((p) => instalados.has(p.id));
    else if (filtro !== 'Todos') ps = ps.filter((p) => p.categoria === filtro);
    /* primeiro o que dá para instalar agora */
    ps.sort((a, b) => (Sistema.pacotes.disponivel(b) - Sistema.pacotes.disponivel(a)) || a.nome.localeCompare(b.nome, 'pt-BR'));
    lista.replaceChildren();
    if (!ponte && !t && (filtro === 'Todos' || ps.some((p) => p.tipo === 'nativo'))) {
      lista.append(h('div.loja-aviso', null, h('b', null, 'Modo navegador. '), 'Aqui você instala apps web, programas WebAssembly e qualquer repositório do GitHub. Os programas Linux nativos (Chrome, Firefox, GIMP...) aparecem marcados e são instalados no modo ponte. ',
        h('a', { href: '#', onclick: (e) => { e.preventDefault(); Desktop.abrirPorId('config', 'sistema'); } }, 'Como ligar a ponte')));
    }
    if (ponte && !t && filtro === 'Todos') lista.append(h('div.loja-aviso', null, h('b', null, 'Modo ponte. '), `Os programas são instalados neste aparelho (${Sistema.info.sistema}) pelo gerenciador "${Sistema.info.gerenciador || 'do sistema'}". O que não estiver no catálogo instala-se pelo Terminal: `, h('code', null, 'apt install NOME')));
    lista.append(...ps.map(cartao));
    if (!ps.length) lista.append(h('div.vazio', { style: 'grid-column:1/-1' }, t ? `Nada encontrado para "${termo}". Se for um repositório do GitHub, use o campo "usuario/repositorio" acima.` : 'Nenhum programa aqui.'));
    const nomes = ['Todos', 'Instalados', ...(cat.categorias || []).filter((c) => cat.pacotes.some((p) => p.categoria === c))];
    cats.replaceChildren(...nomes.map((c) => h('button' + (c === filtro && !t ? '.sel' : ''), { onclick: () => { filtro = c; termo = ''; busca.value = ''; desenhar(); } }, c)));
  }
  async function atualizar() {
    try { instalados = await Sistema.pacotes.instalados(); } catch (e) { /* mantém o que tinha */ }
    if (!jan.fechada) desenhar();
  }
  busca.addEventListener('input', () => { termo = busca.value.trim(); desenhar(); });
  gh.addEventListener('keydown', (e) => { if (e.key === 'Enter') gh.nextElementSibling.click(); });
  Sistema.ouvir('apps', () => { if (!jan.fechada) atualizar(); });
  desenhar();
  await atualizar();
  if (arg && arg.remover) acao('remover', arg.remover);
  return jan;
};

/* ================================================================== Navegador Web */
Apps.navegador = (arg) => {
  const jan = WM.abrir({ titulo: 'Navegador Web', icone: Icones.app('navegador'), largura: 980, altura: 640 });
  const campo = h('input.campo', { type: 'text', placeholder: 'Digite um endereço ou uma busca', spellcheck: false, autocomplete: 'off', inputMode: 'url', 'aria-label': 'Endereço' });
  const quadro = h('iframe.nav-quadro', { title: 'Página', hidden: true, allow: 'fullscreen; autoplay; clipboard-write', referrerPolicy: 'no-referrer' });
  const hist = [];
  let pos = -1;
  const SITES = [
    ['Wikipédia', 'https://pt.wikipedia.org/'], ['OpenStreetMap', 'https://www.openstreetmap.org/export/embed.html?bbox=-74,-34,-34,6&layer=mapnik'],
    ['Internet Archive', 'https://archive.org/'], ['Photopea', 'https://www.photopea.com/'], ['Excalidraw', 'https://excalidraw.com/'], ['Diagramas', 'https://embed.diagrams.net/?ui=min&lang=pt-br'],
  ];
  const inicio = h('div.nav-inicio', null,
    h('h2', null, 'Navegador do TNUX'),
    h('p.sub', null, 'Este navegador abre os sites dentro de uma janela do TNUX. Muitos sites grandes (Google, YouTube, GitHub, bancos) não aceitam ser abertos dentro de outra página: para esses, use o botão ',
      h('span', { html: Icones.ui('externo'), style: 'display:inline-block;width:16px;vertical-align:-3px' }), ' (abrir em uma aba do seu navegador).',
      Sistema.modo === 'ponte' ? ' No modo ponte você também pode instalar o Chromium ou o Firefox de verdade pelo Gerenciador de Aplicativos.' : ' Para um navegador completo dentro do TNUX (Chromium, Firefox), use o modo ponte.'),
    h('div.nav-atalhos', null, SITES.map(([n, u]) => h('button', { onclick: () => ir(u) }, h('b', null, n), h('small', null, new URL(u).hostname)))));
  const aviso = h('div.nav-aviso', { hidden: true }, h('span', null, 'Página em branco? O site não aceita ser aberto aqui dentro.'), h('button.btn', { onclick: () => window.open(hist[pos], '_blank', 'noopener') }, 'Abrir em nova aba'));
  const btV = h('button.btn.liso', { title: 'Voltar', html: Icones.ui('voltar'), onclick: () => { if (pos > 0) { pos--; mostrar(); } else if (pos === 0) { pos = -1; mostrar(); } } });
  const btA = h('button.btn.liso', { title: 'Avançar', html: Icones.ui('avancar'), onclick: () => { if (pos < hist.length - 1) { pos++; mostrar(); } } });
  jan.corpo.append(h('div.barra', null, btV, btA,
    h('button.btn.liso', { title: 'Recarregar', html: Icones.ui('recarregar'), onclick: () => { if (pos >= 0) quadro.src = hist[pos]; } }),
    h('button.btn.liso', { title: 'Página inicial', html: Icones.ui('inicio'), onclick: () => { pos = -1; mostrar(); } }),
    campo,
    h('button.btn.liso', { title: 'Abrir em uma aba do navegador do aparelho', html: Icones.ui('externo'), onclick: () => { if (pos >= 0) window.open(hist[pos], '_blank', 'noopener'); } })),
  aviso, inicio, quadro);

  function mostrar() {
    const tem = pos >= 0;
    quadro.hidden = !tem; aviso.hidden = !tem; inicio.hidden = tem;
    btV.disabled = pos < 0; btA.disabled = pos >= hist.length - 1;
    if (tem) { campo.value = hist[pos]; if (quadro.src !== hist[pos]) quadro.src = hist[pos]; jan.titulo(new URL(hist[pos]).hostname + ' — Navegador Web'); }
    else { campo.value = ''; quadro.removeAttribute('src'); jan.titulo('Navegador Web'); }
  }
  function ir(texto) {
    let u = texto.trim();
    if (!u) return;
    if (/^about:/.test(u)) { pos = -1; mostrar(); return; }
    if (!/^[a-z][a-z0-9+.-]*:\/\//i.test(u)) {
      if (/^(localhost|[\w-]+(\.[\w-]+)+)(:\d+)?(\/.*)?$/i.test(u)) u = (/^(localhost|127\.|192\.168\.|10\.)/.test(u) ? 'http://' : 'https://') + u;
      else u = 'https://pt.wikipedia.org/w/index.php?search=' + encodeURIComponent(u);      /* busca: na Wikipédia, que aceita abrir aqui */
    }
    try { new URL(u); } catch (e) { Avisos.mostrar('Endereço inválido', u, 'atencao'); return; }
    if (location.protocol === 'https:' && u.startsWith('http://')) { Avisos.mostrar('Página sem https', 'O navegador do aparelho não deixa abrir páginas http dentro de uma página https. Abri em uma nova aba.', 'atencao'); window.open(u, '_blank', 'noopener'); return; }
    hist.splice(pos + 1);
    hist.push(u);
    pos = hist.length - 1;
    mostrar();
  }
  campo.addEventListener('keydown', (e) => { if (e.key === 'Enter') ir(campo.value); });
  campo.addEventListener('focus', () => campo.select());
  mostrar();
  if (typeof arg === 'string' && arg) ir(arg);
  jan.aoFocar = () => { if (pos < 0 && !toque()) campo.focus(); };
  return jan;
};

/* ================================================================== Configurações do Sistema */
const ACENTOS = [['Verde Mint', '#35a854'], ['Água', '#1f9ede'], ['Azul', '#3584e4'], ['Roxo', '#9141ac'], ['Rosa', '#e54980'], ['Vermelho', '#e01b24'], ['Laranja', '#e66100'], ['Amarelo', '#e5a50a'], ['Grafite', '#6c7a80']];
const FUNDOS = [['aurora', 'Aurora'], ['menta', 'Menta'], ['noite', 'Noite'], ['por-do-sol', 'Pôr do sol'], ['grafite', 'Grafite']];

Apps.config = (aba) => {
  const jan = WM.abrir({ titulo: 'Configurações do Sistema', icone: Icones.app('config'), largura: 720, altura: 560, unica: 'config' });
  if (jan.corpo.childElementCount) { if (aba) jan.ir(aba); return jan; }
  const abas = h('div.abas'), pag = h('div.pag');
  jan.corpo.append(abas, pag);
  const ponte = Sistema.modo === 'ponte';
  const linha = (rot, sub, ...val) => h('div.linha', null, h('div.rot', null, rot, sub ? h('small', null, sub) : null), ...val);

  const paginas = {
    aparencia: ['Aparência', () => {
      const entrada = h('input', { type: 'file', accept: 'image/*', hidden: true });
      entrada.addEventListener('change', () => {
        const arq = entrada.files[0];
        if (!arq) return;
        /* reduz a imagem para caber no armazenamento do navegador */
        const img = new Image();
        img.onload = () => {
          const esc = Math.min(1, 1920 / Math.max(img.width, img.height));
          const c = h('canvas', { width: Math.round(img.width * esc), height: Math.round(img.height * esc) });
          c.getContext('2d').drawImage(img, 0, 0, c.width, c.height);
          Cfg.dados.fundoImg = c.toDataURL('image/jpeg', 0.82);
          Cfg.definir('fundo', 'imagem');
          URL.revokeObjectURL(img.src);
          jan.ir('aparencia');
        };
        img.onerror = () => Avisos.mostrar('Imagem inválida', 'Não consegui abrir essa imagem.', 'atencao');
        img.src = URL.createObjectURL(arq);
      });
      const fundos = h('div.fundos', null, FUNDOS.map(([id, nome]) => {
        const b = h('button' + (Cfg.dados.fundo === id ? '.sel' : ''), { title: nome, onclick: () => { Cfg.definir('fundo', id); jan.ir('aparencia'); } }, h('span', null, nome));
        b.prepend(h('div', { dataset: { fundo: id }, style: 'position:absolute;inset:0' }));      /* os mesmos gradientes do fundo de verdade */
        return b;
      }));
      const propria = h('button' + (Cfg.dados.fundo === 'imagem' ? '.sel' : ''), { title: 'Escolher uma imagem do seu aparelho', onclick: () => entrada.click() }, h('span', null, 'Sua imagem...'));
      if (Cfg.dados.fundoImg) propria.style.backgroundImage = `url("${Cfg.dados.fundoImg}")`; else propria.style.background = 'var(--barra)';
      fundos.append(propria);
      return [
        h('h2', null, 'Aparência'),
        h('h3', null, 'Tema'),
        h('div.grupo', null,
          linha('Janelas', 'Claro ou escuro', h('button.btn' + (Cfg.dados.tema === 'claro' ? '.pri' : ''), { onclick: () => { Cfg.definir('tema', 'claro'); jan.ir('aparencia'); } }, 'Claro'),
            h('button.btn' + (Cfg.dados.tema === 'escuro' ? '.pri' : ''), { onclick: () => { Cfg.definir('tema', 'escuro'); jan.ir('aparencia'); } }, 'Escuro')),
          linha('Cor de destaque', 'Botões, seleção e detalhes', h('div.cores', null, ACENTOS.map(([nome, cor]) => h('button' + (Cfg.dados.acento === cor ? '.sel' : ''), { title: nome, style: 'background:' + cor, onclick: () => { Cfg.definir('acento', cor); jan.ir('aparencia'); } })))),
          linha('Letra do terminal', 'Tamanho, em pontos (vale para os próximos terminais)',
            h('button.btn', { onclick: () => { Cfg.definir('fonteTerm', Math.max(9, Cfg.dados.fonteTerm - 1)); jan.ir('aparencia'); } }, '−'), h('b', { style: 'min-width:26px;text-align:center' }, String(Cfg.dados.fonteTerm)),
            h('button.btn', { onclick: () => { Cfg.definir('fonteTerm', Math.min(28, Cfg.dados.fonteTerm + 1)); jan.ir('aparencia'); } }, '+'))),
        h('h3', null, 'Papel de parede'), fundos, entrada,
      ];
    }],
    sistema: ['Sistema e ponte', () => {
      const itens = [h('h2', null, 'Sistema'),
        h('p.sub', null, ponte ? 'O TNUX está no modo ponte: o Terminal, os arquivos e os programas são os deste aparelho.' : 'O TNUX está no modo navegador: o sistema inteiro roda dentro desta aba, isolado do seu aparelho.')];
      if (ponte) {
        const i = Sistema.info;
        itens.push(h('div.grupo', null,
          linha('Aparelho', null, h('span.val', null, i.sistema)), linha('Usuário', null, h('span.val', null, `${i.usuario}@${i.host}`)),
          linha('Pasta pessoal', null, h('span.val', null, i.casa)), linha('Gerenciador de pacotes', null, h('span.val', null, i.gerenciador || 'não reconhecido')),
          linha('Tela para programas gráficos', i.tela_disponivel ? 'Servidor de tela instalado' : 'Instale com: tnux instalar tela', h('button.btn', { onclick: () => Apps.tela() }, 'Abrir a tela')),
          linha('Compartilhar pela internet', i.tunel_ativo ? 'O link da Cloudflare está ligado' : 'Um link https da Cloudflare para abrir de qualquer lugar', h('button.btn', { onclick: () => Apps.compartilhar() }, i.tunel_ativo ? 'Ver o link' : 'Criar o link')),
          linha('LNUX', i.lnux ? 'Instalado em ' + i.lnux : 'Não instalado', h('button.btn', { onclick: () => Apps.lnux() }, 'Abrir')),
          linha('Sair do modo ponte', 'Esquece a chave nesta aba e volta ao TNUX do navegador', h('button.btn', { onclick: () => { sessionStorage.removeItem('tnux_token'); location.reload(); } }, 'Sair'))));
      } else {
        const uso = h('span.val', null, 'calculando...');
        Sistema.rodar('du -sh / | cut -f1; find / -type f | wc -l').then((r) => { const [t, n] = r.saida.trim().split('\n'); uso.textContent = `${t}B em ${n} arquivos`; }).catch(() => { uso.textContent = '?'; });
        const entrada = h('input', { type: 'file', accept: '.img,application/octet-stream', hidden: true });
        entrada.addEventListener('change', async () => {
          const arq = entrada.files[0];
          if (!arq) return;
          const bytes = new Uint8Array(await arq.arrayBuffer());
          if (new TextDecoder().decode(bytes.subarray(0, 8)) !== 'TNUXFS1\n') { Avisos.mostrar('Arquivo inválido', 'Esse arquivo não é uma cópia do disco do TNUX.', 'erro'); return; }
          if (!(await Dlg.confirma('Restaurar o disco?', 'Tudo o que está no TNUX agora será trocado pelo conteúdo da cópia.', { sim: 'Restaurar', perigo: true }))) return;
          await Sistema.disco.importar(bytes);
          location.reload();
        });
        itens.push(h('h3', null, 'Disco'), h('div.grupo', null,
          linha('Espaço usado', 'O disco do TNUX fica guardado neste navegador (IndexedDB)', uso),
          Sistema.disco.grava ? null : linha('Atenção', 'Outra aba do TNUX já está aberta: nesta aba nada é gravado', h('span.etq', null, 'somente leitura')),
          linha('Cópia de segurança', 'Salva o disco inteiro num arquivo .img no seu aparelho', h('button.btn', { onclick: async () => {
            try {
              const img = await Sistema.disco.exportar();
              const url = URL.createObjectURL(new Blob([img]));
              const a = h('a', { href: url, download: `tnux-disco-${new Date().toISOString().slice(0, 10)}.img` });
              document.body.append(a); a.click(); a.remove();
              setTimeout(() => URL.revokeObjectURL(url), 30000);
            } catch (e) { Avisos.erro('Não foi possível gerar a cópia', e); }
          } }, 'Salvar cópia'), h('button.btn', { onclick: () => entrada.click() }, 'Restaurar'), entrada),
          linha('Voltar ao estado de fábrica', 'Apaga todos os arquivos e programas instalados', h('button.btn.perigo', { onclick: async () => {
            if (!(await Dlg.confirma('Apagar tudo?', 'Todos os seus arquivos, programas instalados e o histórico do terminal serão apagados. Não dá para desfazer.', { sim: 'Apagar tudo', perigo: true }))) return;
            await Sistema.disco.redefinir();
            location.reload();
          } }, 'Redefinir'))),
        h('h3', null, 'Modo ponte: usar o seu aparelho de verdade'),
        h('p', null, 'Com a ponte, o TNUX deixa de ser uma caixa fechada: o Terminal vira o terminal do aparelho, o apt instala programas Linux reais (Chrome/Chromium, Firefox, GIMP, python, gcc...) e o Gerenciador de Arquivos mostra as suas pastas.'),
        h('div.caixa.dica', null, h('b', null, 'No Termux (Android) ou no terminal do Linux:'), h('pre', null, TEXTO_PONTE.join('\n')),
          h('p.sub', { style: 'margin:8px 0 0' }, 'O último comando mostra um endereço (http://127.0.0.1:8096/#token=...). Abra-o no navegador do mesmo aparelho.')));
      }
      return itens;
    }],
    atalhos: ['Atalhos e dicas', () => [
      h('h2', null, 'Atalhos e dicas'),
      h('div.grupo', null,
        linha('Abrir um terminal', null, h('span.val', null, h('kbd', null, 'Ctrl'), ' + ', h('kbd', null, 'Alt'), ' + ', h('kbd', null, 'T'))),
        linha('Copiar e colar no terminal', null, h('span.val', null, h('kbd', null, 'Ctrl'), ' + ', h('kbd', null, 'Shift'), ' + ', h('kbd', null, 'C'), ' / ', h('kbd', null, 'V'))),
        linha('Interromper um comando', 'No modo navegador, um comando que travou é interrompido reiniciando o núcleo', h('span.val', null, h('kbd', null, 'Ctrl'), ' + ', h('kbd', null, 'C'))),
        linha('Menu de um arquivo ou programa', null, h('span.val', null, 'botão direito, ou toque longo')),
        linha('Maximizar uma janela', null, h('span.val', null, 'dois cliques na barra de título')),
        linha('Salvar no editor', null, h('span.val', null, h('kbd', null, 'Ctrl'), ' + ', h('kbd', null, 'S')))),
      h('h3', null, 'No terminal'),
      h('div.caixa', null, h('pre', null, 'help                       lista todos os comandos\napt search TERMO           procura programas\nsudo apt install NOME      instala\nsudo apt install USUARIO/REPOSITORIO   instala do GitHub\ngit clone URL              baixa um repositório\nxdg-open ARQUIVO           abre no programa certo\ntnux ajuda                 ajuda do TNUX')),
    ]],
  };
  jan.ir = (id) => {
    if (!paginas[id]) id = 'aparencia';
    abas.replaceChildren(...Object.entries(paginas).map(([k, [nome]]) => h('button' + (k === id ? '.sel' : ''), { onclick: () => jan.ir(k) }, nome)),
      h('button', { onclick: () => Apps.sobre() }, 'Sobre'));
    pag.replaceChildren(...paginas[id][1].call(null).filter(Boolean));
    pag.scrollTop = 0;
  };
  jan.ir(aba || 'aparencia');
  return jan;
};

/* ================================================================== Monitor do Sistema */
Apps.monitor = () => {
  const jan = WM.abrir({ titulo: 'Monitor do Sistema', icone: Icones.app('monitor'), largura: 560, altura: 500, unica: 'monitor' });
  if (jan.corpo.childElementCount) return jan;
  const pag = h('div.pag');
  jan.corpo.append(pag);
  const inicio = performance.now();
  async function atualizar() {
    if (jan.fechada) return;
    const itens = [h('h2', null, 'Monitor do Sistema')];
    const linha = (rot, val) => h('div.linha', null, h('div.rot', null, rot), h('span.val', null, val));
    if (Sistema.modo === 'navegador') {
      try {
        const m = await Sistema.disco.memoria();
        const ocupado = Sistema.ocupadoHa();
        itens.push(h('h3', null, 'Núcleo'), h('div.grupo', null,
          h('div.linha', null, h('div.rot', null, 'Memória do núcleo', h('small', null, `${tamanhoHumano(m.usado)} em uso de ${tamanhoHumano(m.total)} reservados`), h('div.medidor', null, h('i', { style: `width:${Math.min(100, m.usado * 100 / m.total)}%` })))),
          linha('Estado', ocupado ? `ocupado com um comando há ${(ocupado / 1000).toFixed(0)} s` : 'livre'),
          linha('Processadores do aparelho', String(Sistema.info.cpus)),
          linha('Disco', Sistema.disco.grava ? 'gravando no navegador' : 'somente leitura (outra aba aberta)')));
      } catch (e) { itens.push(h('p.sub', null, 'O núcleo está ocupado.')); }
    } else {
      const i = Sistema.info;
      itens.push(h('h3', null, 'Aparelho'), h('div.grupo', null, linha('Sistema', i.sistema), linha('Kernel', i.kernel), linha('Arquitetura', i.arquitetura),
        linha('Processadores', String(i.cpus)), linha('Memória', tamanhoHumano(i.mem_mb * 1048576))),
      h('p.sub', null, 'Para ver os processos do aparelho, use o Terminal: ', h('code', null, 'top'), ' ou ', h('code', null, 'htop'), '.'));
    }
    const js = WM.lista();
    itens.push(h('h3', null, `Janelas abertas (${js.length})`), h('div.grupo', null, js.map((j) => h('div.linha', null, h('div.rot', null, j.el.getAttribute('aria-label')),
      j === jan ? h('span.val', null, 'esta janela') : h('button.btn', { onclick: () => { j.fechar(); setTimeout(atualizar, 100); } }, 'Fechar')))),
    h('p.sub', null, `Área de trabalho ligada há ${Math.round((performance.now() - inicio + (window.tnuxInicio ? performance.now() - window.tnuxInicio : 0)) / 60000)} min.`));
    const rolagem = pag.scrollTop;
    pag.replaceChildren(...itens);
    pag.scrollTop = rolagem;
  }
  atualizar();
  const t = setInterval(atualizar, 3000);
  jan.aoFechou = () => clearInterval(t);
  return jan;
};

/* ================================================================== imagens e mídia */
async function urlDoArquivo(cam, tipo) {
  const direta = Sistema.fs.url(cam);
  if (direta) return { url: direta, liberar: () => {} };
  const dados = await Sistema.fs.ler(cam);
  const url = URL.createObjectURL(new Blob([dados], tipo ? { type: tipo } : undefined));
  return { url, liberar: () => URL.revokeObjectURL(url) };
}
const TIPO_MIDIA = { svg: 'image/svg+xml', png: 'image/png', jpg: 'image/jpeg', jpeg: 'image/jpeg', gif: 'image/gif', webp: 'image/webp', bmp: 'image/bmp', ico: 'image/x-icon', avif: 'image/avif',
  mp4: 'video/mp4', webm: 'video/webm', mkv: 'video/x-matroska', mov: 'video/quicktime', m4v: 'video/mp4', ogv: 'video/ogg',
  mp3: 'audio/mpeg', ogg: 'audio/ogg', oga: 'audio/ogg', opus: 'audio/ogg', wav: 'audio/wav', flac: 'audio/flac', m4a: 'audio/mp4' };

Apps.imagem = async (cam) => {
  const fs = Sistema.fs;
  if (typeof cam !== 'string' || !cam) {
    cam = await Apps.escolher({ titulo: 'Abrir imagem', modo: 'abrir', inicio: fs.casa });
    if (!cam) return null;
  }
  const jan = WM.abrir({ titulo: 'Visualizador de Imagens', icone: Icones.app('imagem'), largura: 780, altura: 560 });
  const img = h('img', { alt: '' }), palco = h('div.img-palco', null, img), rodape = h('div.rodape');
  let atual = null, vizinhas = [];
  async function mostrar(c) {
    try {
      const novo = await urlDoArquivo(c, TIPO_MIDIA[extensao(c)]);
      if (atual) atual.liberar();
      atual = novo; cam = c;
      img.src = novo.url;
      jan.titulo(fs.nomeDe(c) + ' — Visualizador de Imagens');
      rodape.textContent = c;
    } catch (e) { Avisos.erro('Não foi possível abrir a imagem', e); }
  }
  img.addEventListener('load', () => { rodape.textContent = `${cam}    ${img.naturalWidth} × ${img.naturalHeight}`; });
  const passo = (d) => { if (vizinhas.length < 2) return; const i = vizinhas.indexOf(cam); mostrar(vizinhas[(i + d + vizinhas.length) % vizinhas.length]); };
  jan.corpo.append(h('div.barra', null,
    h('button.btn.liso', { title: 'Anterior', html: Icones.ui('voltar'), onclick: () => passo(-1) }),
    h('button.btn.liso', { title: 'Próxima', html: Icones.ui('avancar'), onclick: () => passo(1) }),
    h('div.sep'),
    h('button.btn.liso', { title: 'Tamanho real ou ajustado à janela', html: Icones.ui('busca'), onclick: () => { const real = img.style.maxWidth === 'none'; img.style.maxWidth = real ? '' : 'none'; img.style.maxHeight = real ? '' : 'none'; } }),
    h('button.btn.liso', { title: 'Salvar no meu aparelho', html: Icones.ui('baixar'), onclick: () => Apps.baixar(cam).catch((e) => Avisos.erro('Não foi possível salvar', e)) })), palco, rodape);
  jan.aoFechou = () => { if (atual) atual.liberar(); };
  jan.corpo.tabIndex = 0;
  jan.corpo.addEventListener('keydown', (e) => { if (e.key === 'ArrowRight') passo(1); else if (e.key === 'ArrowLeft') passo(-1); });
  await mostrar(cam);
  fs.listar(fs.pastaDe(cam)).then((l) => { vizinhas = l.filter((i) => i.tipo === 'f' && EXT.imagem.includes(extensao(i.nome))).map((i) => fs.juntar(fs.pastaDe(cam), i.nome)).sort((a, b) => a.localeCompare(b, 'pt-BR', { numeric: true })); }).catch(() => {});
  return jan;
};

Apps.midia = async (cam) => {
  const e = extensao(cam), video = EXT.video.includes(e);
  const jan = WM.abrir({ titulo: Sistema.fs.nomeDe(cam), icone: Icones.app(video ? 'video' : 'som'), largura: video ? 760 : 460, altura: video ? 500 : 190 });
  try {
    const fonte = await urlDoArquivo(cam, TIPO_MIDIA[e]);
    const el = h(video ? 'video' : 'audio', { controls: true, autoplay: true, src: fonte.url, style: video ? 'flex:1;min-height:0;width:100%;background:#000' : 'width:100%;margin:auto;padding:0 14px' });
    el.addEventListener('error', () => Avisos.mostrar('Formato não suportado', 'O navegador não consegue tocar este arquivo.' + (Sistema.modo === 'ponte' ? ' Instale o VLC pelo Gerenciador de Aplicativos.' : ''), 'atencao'));
    jan.corpo.style.background = video ? '#000' : '';
    jan.corpo.append(el);
    jan.aoFechou = () => { el.pause(); el.removeAttribute('src'); fonte.liberar(); };
  } catch (err) { Avisos.erro('Não foi possível abrir o arquivo', err); jan.fechar(true); }
  return jan;
};

/* ================================================================== Calculadora */
/* calcula uma expressão com + - × ÷, parênteses e porcentagem, sem usar eval */
function calcular(expr) {
  const toks = expr.replace(/\s+/g, '').replace(/,/g, '.').match(/\d+\.?\d*|\.\d+|[-+×÷*/()%]/g) || [];
  let i = 0;
  const espia = () => toks[i];
  function fator() {
    const t = toks[i++];
    if (t === '(') { const v = soma(); if (toks[i++] !== ')') throw new Error('falta )'); return pos(v); }
    if (t === '-') return -fator();
    if (t === '+') return fator();
    if (t === undefined || !/^[\d.]/.test(t)) throw new Error('expressão incompleta');
    return pos(parseFloat(t));
  }
  function pos(v) { while (espia() === '%') { i++; v /= 100; } return v; }
  function termo() {
    let v = fator();
    for (;;) {
      const t = espia();
      if (t === '×' || t === '*') { i++; v *= fator(); }
      else if (t === '÷' || t === '/') { i++; const d = fator(); if (d === 0) throw new Error('divisão por zero'); v /= d; }
      else if (t === '(') v *= fator();
      else return v;
    }
  }
  function soma() {
    let v = termo();
    for (;;) {
      const t = espia();
      if (t === '+') { i++; v += termo(); }
      else if (t === '-') { i++; v -= termo(); }
      else return v;
    }
  }
  const r = soma();
  if (i < toks.length) throw new Error('expressão inválida');
  return r;
}
Apps.calculadora = () => {
  const jan = WM.abrir({ titulo: 'Calculadora', icone: Icones.app('calculadora'), largura: 320, altura: 440 });
  let expr = '', anterior = '';
  const visor = h('div.calc-visor', null, h('small'), h('span', null, '0'));
  const teclas = h('div.calc-teclas');
  const fmt = (v) => { const s = Number.isInteger(v) ? String(v) : String(Math.round(v * 1e10) / 1e10); return s.replace('.', ','); };
  function mostra() { visor.firstChild.textContent = anterior; visor.lastChild.textContent = expr || '0'; }
  function tecla(t) {
    if (t === 'C') { expr = ''; anterior = ''; }
    else if (t === '⌫') expr = expr.slice(0, -1);
    else if (t === '=') {
      if (!expr) return;
      try { const r = calcular(expr); anterior = expr + ' ='; expr = fmt(r); }
      catch (e) { anterior = e.message; }
    } else expr += t;
    mostra();
  }
  for (const t of ['C', '(', ')', '⌫', '7', '8', '9', '÷', '4', '5', '6', '×', '1', '2', '3', '-', '0', ',', '%', '+']) {
    teclas.append(h('button' + (/[\d,]/.test(t) ? '' : '.op'), { onclick: () => tecla(t) }, t));
  }
  const ig = h('button.ig', { onclick: () => tecla('='), style: 'grid-column:1 / -1' }, '=');
  teclas.append(ig);
  const caixa = h('div.calc', { tabIndex: 0 }, visor, teclas);
  caixa.addEventListener('keydown', (e) => {
    const k = e.key;
    if (/^[\d()%+\-]$/.test(k)) tecla(k);
    else if (k === '*' || k === 'x') tecla('×');
    else if (k === '/') { e.preventDefault(); tecla('÷'); }
    else if (k === ',' || k === '.') tecla(',');
    else if (k === 'Enter' || k === '=') { e.preventDefault(); tecla('='); }
    else if (k === 'Backspace') tecla('⌫');
    else if (k === 'Escape' || k === 'Delete') tecla('C');
  });
  jan.corpo.append(caixa);
  jan.aoFocar = () => caixa.focus({ preventScroll: true });
  caixa.focus();
  return jan;
};

/* ================================================================== Informações do Sistema */
Apps.sobre = () => {
  const jan = WM.abrir({ titulo: 'Informações do Sistema', icone: Icones.app('info'), largura: 560, altura: 520, unica: 'sobre' });
  if (jan.corpo.childElementCount) return jan;
  const i = Sistema.info, ponte = Sistema.modo === 'ponte';
  const linha = (rot, val) => h('div.linha', null, h('div.rot', null, rot), h('span.val', null, val));
  jan.corpo.append(h('div.pag', null,
    h('div.bv-topo', null, h('span', { html: Icones.app('tnux'), style: 'display:contents' }),
      h('div', null, h('h2', null, `TNUX ${Sistema.versao} “${Sistema.codinome}”`), h('p.sub', null, 'Um sistema no estilo do Linux Mint, com núcleo em C, que roda no navegador ou no terminal do aparelho.'))),
    h('h3', null, 'Este sistema'),
    h('div.grupo', null,
      linha('Modo', ponte ? 'ponte (usando o aparelho)' : 'navegador (dentro desta aba)'),
      linha('Núcleo', ponte ? `${i.kernel} (${i.arquitetura})` : '6.8.0-tnux (WebAssembly, wasm32)'),
      linha('Área de trabalho', 'TNUX Desktop, inspirada no Cinnamon'),
      linha(ponte ? 'Aparelho' : 'Navegador', ponte ? i.sistema : i.plataforma),
      linha('Processadores', String(i.cpus)),
      linha('Memória', ponte ? tamanhoHumano(i.mem_mb * 1048576) : `${tamanhoHumano(i.mem_mb * 1048576)} (informada pelo navegador)`),
      linha('Usuário', `${i.usuario}@${i.host}`)),
    h('h3', null, 'Sobre o projeto'),
    h('p', null, 'O TNUX é software livre (licença MIT). Ele é inspirado no Linux Mint, mas é um projeto independente: não é feito nem endossado pela equipe do Linux Mint, e não usa código nem arte deles.'),
    h('p', null, 'O terminal usa o ', h('a', { href: 'https://xtermjs.org/', target: '_blank', rel: 'noopener' }, 'xterm.js'), ' (MIT).'),
    h('p', null, h('a', { href: REPO, target: '_blank', rel: 'noopener' }, REPO))));
  return jan;
};

/* ================================================================== Bem-vindo */
Apps.boasvindas = () => {
  const jan = WM.abrir({ titulo: 'Bem-vindo ao TNUX', icone: Icones.app('tnux'), largura: 700, altura: 560, unica: 'boasvindas' });
  if (jan.corpo.childElementCount) return jan;
  const ponte = Sistema.modo === 'ponte';
  const cartao = (icone, titulo, texto, f) => h('button', { onclick: f }, h('span', { html: Icones.app(icone), style: 'display:contents' }), h('div', null, h('b', null, titulo), h('small', null, texto)));
  const caixa = h('input', { type: 'checkbox', checked: !Cfg.dados.visto });
  caixa.addEventListener('change', () => Cfg.definir('visto', !caixa.checked));
  jan.corpo.append(h('div.pag', null,
    h('div.bv-topo', null, h('span', { html: Icones.app('tnux'), style: 'display:contents' }),
      h('div', null, h('h2', null, 'Bem-vindo ao TNUX'), h('p.sub', null, `Versão ${Sistema.versao} “${Sistema.codinome}”. ` + (ponte ? `Modo ponte: você está usando o aparelho (${Sistema.info.sistema}).` : 'Modo navegador: o sistema roda inteiro dentro desta aba.')))),
    h('div.bv-cartoes', null,
      cartao('terminal', 'Terminal', ponte ? 'O shell do seu aparelho, com o apt do TNUX' : 'Mais de 130 comandos: ls, grep, sed, apt, git...', () => Desktop.abrirPorId('terminal')),
      cartao('loja', 'Instalar programas', ponte ? 'Chromium, Firefox, VLC, GIMP e o que mais o aparelho tiver' : 'Apps web, programas e repositórios do GitHub', () => Desktop.abrirPorId('loja')),
      cartao('pasta', 'Arquivos', ponte ? 'As pastas do aparelho' : 'Seus arquivos, guardados no navegador', () => Desktop.abrirPorId('arquivos')),
      cartao('config', 'Aparência', 'Tema, cores e papel de parede', () => Desktop.abrirPorId('config', 'aparencia'))),
    ponte ? h('div.caixa.dica', null, h('b', null, 'Programas com janela (Chromium, Firefox, GIMP...)'),
      h('p', { style: 'margin:6px 0 0' }, 'Eles abrem na "Tela do aparelho", uma janela do TNUX que mostra a tela gráfica do aparelho. Se ainda não tiver o servidor de tela, instale pelo Terminal: ', h('code', null, 'tnux instalar tela')))
      : h('div.caixa.dica', null, h('b', null, 'Quer rodar programas Linux de verdade (Chrome, Firefox, python, gcc)?'),
        h('p', { style: 'margin:6px 0 0' }, 'Dentro do navegador o TNUX é uma caixa fechada. No modo ponte, o seu aparelho (Termux ou Linux) executa os programas e o TNUX vira a área de trabalho deles. No terminal do aparelho:'),
        h('pre', null, TEXTO_PONTE.join('\n'))),
    h('h3', null, 'Para experimentar no Terminal'),
    h('div.caixa', null, h('pre', null, ponte
      ? 'tnux fetch                  # informações do sistema\napt search navegador        # procura no catálogo e no aparelho\napt install chromium        # instala no aparelho\napt install Gustavo-HVF/LNUX-APPs   # baixa do GitHub'
      : 'neofetch                    # informações do sistema\nsudo apt install vaca && vaca Olá\nsudo apt install lnux-som   # um app web, baixado do GitHub\ngit clone https://github.com/Gustavo-HVF/Termux-Toons\ncd Documentos && ./exemplo.sh')),
    h('label', { style: 'display:flex;gap:8px;align-items:center;margin-top:14px' }, caixa, 'Mostrar esta janela ao iniciar')));
  return jan;
};
