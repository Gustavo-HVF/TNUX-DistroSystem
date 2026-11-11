/*
 * TNUX: ícones (SVG desenhados para o projeto, no estilo plano e colorido do Mint-Y).
 * Icones.app(nome) devolve um ícone de programa; Icones.ui(nome) um glifo de uma cor só.
 */
'use strict';

const Icones = (() => {
  const svg = (dentro, vb = 48) => `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${vb} ${vb}" aria-hidden="true">${dentro}</svg>`;
  let seq = 0;
  /* quadrado arredondado com gradiente e um glifo por cima */
  const quad = (c1, c2, glifo) => {
    const id = 'ig' + (seq++);
    return svg(`<defs><linearGradient id="${id}" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="${c1}"/><stop offset="1" stop-color="${c2}"/></linearGradient></defs>` +
      `<rect x="4" y="4" width="40" height="40" rx="10" fill="url(#${id})"/>${glifo}`);
  };
  const T = 'stroke="#fff" stroke-width="3.2" fill="none" stroke-linecap="round" stroke-linejoin="round"';

  const apps = {
    tnux: () => quad('#5fd38a', '#1f8a4c', '<path d="M12 13h24v7h-8.5v16h-7V20H12z" fill="#fff"/>'),
    terminal: () => quad('#3d4a50', '#1c2327', `<path d="M13 17l8 7-8 7" ${T}/><path d="M25 32h10" stroke="#7bed9f" stroke-width="3.2" stroke-linecap="round"/>`),
    pasta: () => svg('<path d="M4 13a3 3 0 0 1 3-3h11.5l4 5H41a3 3 0 0 1 3 3v19a3 3 0 0 1-3 3H7a3 3 0 0 1-3-3z" fill="#4f9a45"/><path d="M4 20.5h40V37a3 3 0 0 1-3 3H7a3 3 0 0 1-3-3z" fill="#86cf6b"/><path d="M4 20.5h40v2H4z" fill="#a4e08c"/>'),
    casa: () => svg('<path d="M4 13a3 3 0 0 1 3-3h11.5l4 5H41a3 3 0 0 1 3 3v19a3 3 0 0 1-3 3H7a3 3 0 0 1-3-3z" fill="#4f9a45"/><path d="M4 20.5h40V37a3 3 0 0 1-3 3H7a3 3 0 0 1-3-3z" fill="#86cf6b"/><path d="M24 23.5l-8 6.5h2.5v6h4v-4h3v4h4v-6H32z" fill="#fff"/>'),
    computador: () => svg('<rect x="5" y="8" width="38" height="25" rx="3" fill="#4b5a61"/><rect x="8" y="11" width="32" height="19" rx="1" fill="#6fc3e8"/><path d="M8 26l9-8 7 6 6-4 10 8v2H8z" fill="#4aa3cf"/><path d="M19 33h10l1.5 5h-13z" fill="#7d8a90"/><rect x="13" y="38" width="22" height="3" rx="1.5" fill="#4b5a61"/>'),
    editor: () => quad('#5aa9f0', '#2567c4', '<rect x="13" y="10" width="22" height="28" rx="3" fill="#fff"/><path d="M17 17h14M17 22h14M17 27h9" stroke="#2567c4" stroke-width="2.4" stroke-linecap="round"/>'),
    navegador: () => quad('#ff9f43', '#e25822', `<circle cx="24" cy="24" r="11.5" ${T}/><path d="M12.5 24h23M24 12.5c-5.5 6-5.5 17 0 23M24 12.5c5.5 6 5.5 17 0 23" ${T} stroke-width="2.4"/>`),
    loja: () => quad('#6fd08c', '#2d9153', '<path d="M24 11l3.6 8 8.7.9-6.5 5.8 1.9 8.6L24 29.9l-7.7 4.4 1.9-8.6-6.5-5.8 8.7-.9z" fill="#fff"/>'),
    config: () => quad('#8e9ba1', '#56646b', '<path d="M24 16.5a7.5 7.5 0 1 0 0 15 7.5 7.5 0 0 0 0-15zm0 4.5a3 3 0 1 1 0 6 3 3 0 0 1 0-6z" fill="#fff"/><path d="M22 9h4l.8 4.2 3.2 1.4 3.6-2.4 2.8 2.8-2.4 3.6 1.4 3.2L39.6 22v4l-4.2.8-1.4 3.2 2.4 3.6-2.8 2.8-3.6-2.4-3.2 1.4L26 39h-4l-.8-4.2-3.2-1.4-3.6 2.4-2.8-2.8 2.4-3.6-1.4-3.2L8.4 26v-4l4.2-.8 1.4-3.2-2.4-3.6 2.8-2.8 3.6 2.4 3.2-1.4z" fill="#fff" fill-opacity=".55"/>'),
    monitor: () => quad('#3ecfc0', '#178f8a', `<path d="M10 28h6l3.5-10 5 16 4-11 2.5 5h7" ${T}/>`),
    imagem: () => quad('#b389f5', '#6b3fc4', '<circle cx="17" cy="18" r="3.6" fill="#ffe27a"/><path d="M9 35l9.5-11 6.5 7 4.5-5 9.5 9z" fill="#fff"/>'),
    calculadora: () => quad('#ffb454', '#e07b17', '<rect x="13" y="10" width="22" height="8" rx="2" fill="#fff"/><g fill="#fff"><rect x="13" y="21" width="6" height="6" rx="1.5"/><rect x="21" y="21" width="6" height="6" rx="1.5"/><rect x="29" y="21" width="6" height="6" rx="1.5"/><rect x="13" y="30" width="6" height="6" rx="1.5"/><rect x="21" y="30" width="6" height="6" rx="1.5"/><rect x="29" y="30" width="6" height="6" rx="1.5"/></g>'),
    info: () => quad('#5ab0ff', '#1f6fd0', '<circle cx="24" cy="15" r="3" fill="#fff"/><path d="M19.5 21.5h6v13h3.5" stroke="#fff" stroke-width="3.6" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'),
    web: () => quad('#7c8cf8', '#4350c9', '<rect x="10" y="13" width="28" height="22" rx="3" fill="#fff"/><path d="M10 19h28" stroke="#4350c9" stroke-width="2"/><circle cx="14" cy="16" r="1.2" fill="#4350c9"/><circle cx="18" cy="16" r="1.2" fill="#4350c9"/><path d="M15 25h18M15 30h11" stroke="#9aa5f5" stroke-width="2.4" stroke-linecap="round"/>'),
    app: () => quad('#9aa8ae', '#5d6b72', '<rect x="12" y="12" width="10" height="10" rx="2.5" fill="#fff"/><rect x="26" y="12" width="10" height="10" rx="2.5" fill="#fff" fill-opacity=".75"/><rect x="12" y="26" width="10" height="10" rx="2.5" fill="#fff" fill-opacity=".75"/><rect x="26" y="26" width="10" height="10" rx="2.5" fill="#fff" fill-opacity=".5"/>'),
    video: () => quad('#ff7a7a', '#d22f3d', '<path d="M19 15.5v17a1.5 1.5 0 0 0 2.3 1.3l13.4-8.5a1.5 1.5 0 0 0 0-2.6L21.3 14.2A1.5 1.5 0 0 0 19 15.5z" fill="#fff"/>'),
    som: () => quad('#ff8fc7', '#d43f8d', '<path d="M20 31V15l14-3v16" stroke="#fff" stroke-width="3" fill="none" stroke-linecap="round" stroke-linejoin="round"/><circle cx="16.5" cy="31.5" r="4.2" fill="#fff"/><circle cx="30.5" cy="28.5" r="4.2" fill="#fff"/>'),
    lnux: () => quad('#2b2f33', '#0f1214', '<path d="M12.5 14v20h9" stroke="#7bed9f" stroke-width="3.6" fill="none" stroke-linecap="round" stroke-linejoin="round"/><path d="M26 14v20M26 14l10 20M36 14v20" stroke="#fff" stroke-width="3.2" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'),
    compartilhar: () => quad('#f6a03c', '#e06a12', '<path d="M17 29.5a6.5 6.5 0 0 1 .6-13 8.5 8.5 0 0 1 16.3 2.2A5.4 5.4 0 0 1 33 29.5z" fill="#fff"/><path d="M24 36V24.5M20 28l4-4 4 4" stroke="#e06a12" stroke-width="2.8" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'),
    tela: () => quad('#54c2f0', '#1b7fbf', '<rect x="9" y="12" width="30" height="19" rx="2.5" fill="#fff"/><rect x="12" y="15" width="13" height="9" rx="1" fill="#1b7fbf"/><rect x="27" y="19" width="9" height="9" rx="1" fill="#7cc9ee"/><path d="M19 36h10M24 31v5" stroke="#fff" stroke-width="3" stroke-linecap="round"/>'),
    arquivo: () => svg('<path d="M11 5h17l9 9v27a3 3 0 0 1-3 3H11a3 3 0 0 1-3-3V8a3 3 0 0 1 3-3z" fill="#eef1f3"/><path d="M28 5l9 9h-6a3 3 0 0 1-3-3z" fill="#c3ccd1"/><path d="M14 22h17M14 27h17M14 32h11" stroke="#9aa6ac" stroke-width="2.2" stroke-linecap="round"/><path d="M11 5h17l9 9v27a3 3 0 0 1-3 3H11a3 3 0 0 1-3-3V8a3 3 0 0 1 3-3z" fill="none" stroke="#aab4ba" stroke-width="1"/>'),
    script: () => svg('<path d="M11 5h17l9 9v27a3 3 0 0 1-3 3H11a3 3 0 0 1-3-3V8a3 3 0 0 1 3-3z" fill="#2f383c"/><path d="M28 5l9 9h-6a3 3 0 0 1-3-3z" fill="#56646b"/><path d="M14 23l5 4.5-5 4.5" stroke="#7bed9f" stroke-width="2.6" fill="none" stroke-linecap="round" stroke-linejoin="round"/><path d="M22 33h8" stroke="#fff" stroke-width="2.6" stroke-linecap="round"/>'),
    figura: () => svg('<rect x="6" y="9" width="36" height="30" rx="3" fill="#eef1f3" stroke="#aab4ba"/><circle cx="16" cy="19" r="3.5" fill="#f6c443"/><path d="M9 35l9-10 6 6.5 5-5.5 10 9z" fill="#6fc08a"/>'),
    midia: () => svg('<rect x="6" y="9" width="36" height="30" rx="3" fill="#3a2f4a"/><path d="M20 17v14l12-7z" fill="#fff"/>'),
    pacote: () => svg('<path d="M24 5l17 8v22l-17 8-17-8V13z" fill="#d9a86c"/><path d="M24 21l17-8M24 21L7 13M24 21v22" stroke="#a9773d" stroke-width="2"/><path d="M15.5 9l17 8v7l-5-2.3V17" fill="none" stroke="#fff" stroke-opacity=".7" stroke-width="2"/>'),
    lixeira: () => svg('<path d="M12 14h24l-2 27a3 3 0 0 1-3 2.8H17a3 3 0 0 1-3-2.8z" fill="#8e9ba1"/><rect x="9" y="9" width="30" height="5" rx="2" fill="#6c7a80"/><rect x="19" y="5" width="10" height="5" rx="2" fill="#6c7a80"/><path d="M19 20v17M24 20v17M29 20v17" stroke="#fff" stroke-opacity=".6" stroke-width="2" stroke-linecap="round"/>'),
  };

  /* glifos de uma cor (seguem a cor do texto) */
  const L = 'fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"';
  const glifos = {
    fechar: `<path d="M5 5l10 10M15 5L5 15" ${L}/>`,
    min: `<path d="M5 14h10" ${L}/>`,
    max: `<rect x="5" y="5" width="10" height="10" rx="1.5" ${L}/>`,
    restaurar: `<rect x="4.5" y="7.5" width="8" height="8" rx="1.5" ${L}/><path d="M8 7.5V6a1.5 1.5 0 0 1 1.5-1.5H14A1.5 1.5 0 0 1 15.5 6v4.5A1.5 1.5 0 0 1 14 12h-1.5" ${L}/>`,
    voltar: `<path d="M12 4l-6 6 6 6" ${L}/>`,
    avancar: `<path d="M8 4l6 6-6 6" ${L}/>`,
    cima: `<path d="M4 12l6-6 6 6" ${L}/>`,
    recarregar: `<path d="M15.5 10a5.5 5.5 0 1 1-1.6-3.9M15.5 4v3.5H12" ${L}/>`,
    inicio: `<path d="M3.5 9.5L10 4l6.5 5.5M5.5 8.5V16h9V8.5" ${L}/>`,
    novo: `<path d="M10 4v12M4 10h12" ${L}/>`,
    pasta_nova: `<path d="M3 6.5A1.5 1.5 0 0 1 4.5 5H8l1.5 2h6A1.5 1.5 0 0 1 17 8.5v6a1.5 1.5 0 0 1-1.5 1.5h-11A1.5 1.5 0 0 1 3 14.5zM10 9.5v4M8 11.5h4" ${L}/>`,
    salvar: `<path d="M4 5.5A1.5 1.5 0 0 1 5.5 4h7.6L16 6.9v7.6a1.5 1.5 0 0 1-1.5 1.5h-9A1.5 1.5 0 0 1 4 14.5zM7 4v4h5V4M7 16v-4.5h6V16" ${L}/>`,
    abrir: `<path d="M3 15V6.5A1.5 1.5 0 0 1 4.5 5H8l1.5 2h5A1.5 1.5 0 0 1 16 8.5V9M3 15l2-5.5h12.5L15.5 15z" ${L}/>`,
    apagar: `<path d="M4.5 6h11M8 6V4.5h4V6M6 6l.7 9.5a1 1 0 0 0 1 .9h4.6a1 1 0 0 0 1-.9L14 6" ${L}/>`,
    copiar: `<rect x="7" y="7" width="9" height="9" rx="1.5" ${L}/><path d="M13 7V5.5A1.5 1.5 0 0 0 11.5 4h-6A1.5 1.5 0 0 0 4 5.5v6A1.5 1.5 0 0 0 5.5 13H7" ${L}/>`,
    recortar: `<circle cx="6" cy="14" r="2" ${L}/><circle cx="14" cy="14" r="2" ${L}/><path d="M7.4 12.6L15 4M12.6 12.6L5 4" ${L}/>`,
    colar: `<path d="M7 5H5.5A1.5 1.5 0 0 0 4 6.5v9A1.5 1.5 0 0 0 5.5 17h9a1.5 1.5 0 0 0 1.5-1.5v-9A1.5 1.5 0 0 0 14.5 5H13" ${L}/><rect x="7" y="3.5" width="6" height="3" rx="1" ${L}/>`,
    renomear: `<path d="M4 16h3l8.5-8.5-3-3L4 13zM11 6l3 3" ${L}/>`,
    baixar: `<path d="M10 4v9M6.5 9.5L10 13l3.5-3.5M4.5 16h11" ${L}/>`,
    enviar: `<path d="M10 13V4M6.5 7.5L10 4l3.5 3.5M4.5 16h11" ${L}/>`,
    busca: `<circle cx="9" cy="9" r="5" ${L}/><path d="M13 13l3.5 3.5" ${L}/>`,
    externo: `<path d="M11 4h5v5M16 4l-7 7M14 11.5v3A1.5 1.5 0 0 1 12.5 16h-7A1.5 1.5 0 0 1 4 14.5v-7A1.5 1.5 0 0 1 5.5 6h3" ${L}/>`,
    lista: `<path d="M7 5.5h9M7 10h9M7 14.5h9M4 5.5h.01M4 10h.01M4 14.5h.01" ${L}/>`,
    grade: `<rect x="4" y="4" width="5" height="5" rx="1" ${L}/><rect x="11" y="4" width="5" height="5" rx="1" ${L}/><rect x="4" y="11" width="5" height="5" rx="1" ${L}/><rect x="11" y="11" width="5" height="5" rx="1" ${L}/>`,
    olho: `<path d="M2.5 10s3-5 7.5-5 7.5 5 7.5 5-3 5-7.5 5-7.5-5-7.5-5z" ${L}/><circle cx="10" cy="10" r="2.2" ${L}/>`,
    energia: `<path d="M10 3.5v6M6 5.8a6 6 0 1 0 8 0" ${L}/>`,
    reiniciar: `<path d="M15.5 10a5.5 5.5 0 1 1-1.6-3.9M15.5 4v3.5H12" ${L}/>`,
    ponte: `<path d="M2.5 13.5c0-4 3.4-7 7.5-7s7.5 3 7.5 7M2.5 13.5h15M6 13.5V9.6M10 13.5V6.5M14 13.5V9.6" ${L}/>`,
    navegador: `<circle cx="10" cy="10" r="6.5" ${L}/><path d="M3.5 10h13M10 3.5c-3 3.5-3 9.5 0 13M10 3.5c3 3.5 3 9.5 0 13" ${L}/>`,
    aparelho: `<rect x="6" y="3" width="8" height="14" rx="1.8" ${L}/><path d="M9 14.5h2" ${L}/>`,
    teclado: `<rect x="2.5" y="5.5" width="15" height="9" rx="1.5" ${L}/><path d="M5.5 8.5h.01M8.5 8.5h.01M11.5 8.5h.01M14.5 8.5h.01M6.5 11.5h7" ${L}/>`,
    aviso: `<path d="M10 3.5l7 12.5H3zM10 8.5v3.5M10 14.2h.01" ${L}/>`,
    cadeado: `<rect x="5" y="9" width="10" height="7.5" rx="1.5" ${L}/><path d="M7 9V6.8a3 3 0 0 1 6 0V9" ${L}/>`,
    quebra: `<path d="M4 5.5h12M4 10h10a2.3 2.3 0 0 1 0 4.6h-3M4 14.5h3.5M12.3 12.8l-1.8 1.8 1.8 1.8" ${L}/>`,
    tela_cheia: `<path d="M4 8V4h4M16 8V4h-4M4 12v4h4M16 12v4h-4" ${L}/>`,
  };
  const categorias = {
    'Todos': 'app', 'Acessórios': 'calculadora', 'Escritório': 'editor', 'Gráficos': 'imagem', 'Internet': 'navegador', 'Jogos': 'loja',
    'Programação': 'terminal', 'Som e vídeo': 'video', 'Sistema': 'config', 'Preferências': 'config', 'Educação': 'info', 'Outros': 'app',
  };

  return {
    app(nome) { return (apps[nome] || apps.app)(); },
    tem(nome) { return !!apps[nome]; },
    ui(nome) { return svg(glifos[nome] || glifos.novo, 20); },
    categoria(nome) { return (apps[categorias[nome]] || apps.app)(); },
    /* ícone para um arquivo, pelo nome */
    arquivo(nome, tipo) {
      if (tipo === 'd') return apps.pasta();
      const e = (nome.split('.').pop() || '').toLowerCase();
      if (['png', 'jpg', 'jpeg', 'gif', 'webp', 'svg', 'bmp', 'ico', 'avif'].includes(e)) return apps.figura();
      if (['mp4', 'webm', 'mkv', 'mov', 'avi', 'mp3', 'ogg', 'wav', 'flac', 'm4a', 'opus'].includes(e)) return apps.midia();
      if (['sh', 'bash', 'py', 'js', 'c', 'h', 'cpp', 'rs', 'go', 'wasm', 'pl', 'rb', 'lua'].includes(e)) return apps.script();
      if (['zip', 'tar', 'gz', 'xz', 'bz2', '7z', 'deb', 'rpm', 'apk'].includes(e)) return apps.pacote();
      if (['html', 'htm'].includes(e)) return apps.web();
      return apps.arquivo();
    },
  };
})();
