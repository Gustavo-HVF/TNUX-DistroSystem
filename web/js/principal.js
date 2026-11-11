/*
 * TNUX: sequência de boot da área de trabalho.
 */
'use strict';

window.tnuxInicio = performance.now();

(async () => {
  const boot = $('#boot'), passo = (t) => { $('#boot-passo').textContent = t; };
  $('#boot-logo').innerHTML = Icones.app('tnux');
  Cfg.carregar();
  try {
    if (typeof WebAssembly !== 'object' || typeof Worker !== 'function') throw new Error('Este navegador não tem WebAssembly ou Web Workers. Use uma versão recente do Chrome, Firefox, Edge ou Safari.');
    await Sistema.iniciar(passo);
    passo('Abrindo a área de trabalho');
    $('#area').hidden = false;
    $('#painel').hidden = false;
    Cfg.aplicar();
    await Desktop.iniciar();
    Processador.ligar();        /* no modo ponte, este computador passa a atender os pedidos de processamento do aparelho */

    /* pedidos que vêm do terminal: xdg-open, nano, nemo, firefox, reboot... */
    Sistema.ouvir('gui', (m) => {
      if (m.acao === 'abrir') {
        if (m.app === 'web') Apps.web({ vfs: m.arg, nome: Sistema.fs.nomeDe(m.arg) });
        else if (m.app === 'desktop') Apps.abrirArquivo(m.arg);
        else Desktop.abrirPorId(m.app, m.arg);
      } else if (m.acao === 'energia') Desktop.energia(m.app);
      else if (m.acao === 'redefinir') Sistema.disco.redefinir().then(() => location.reload());
    });
    Sistema.ouvir('erro', (msg) => Avisos.mostrar('Erro', msg, 'erro'));
    let avisouDisco = false;
    Sistema.ouvir('erro_disco', (msg) => {
      if (avisouDisco) return;
      avisouDisco = true;
      Avisos.mostrar('O disco não pôde ser gravado', 'O navegador recusou guardar os arquivos (pouco espaço, ou janela anônima). O que você fizer pode se perder ao fechar. Detalhe: ' + msg, 'erro', 15000);
    });
    /* ao sair da aba, garante que a última alteração foi para o disco */
    document.addEventListener('visibilitychange', () => { if (document.hidden && Sistema.modo === 'navegador') Sistema.disco.salvar().catch(() => {}); });

    boot.classList.add('some');
    setTimeout(() => boot.remove(), 500);
    if (!Cfg.dados.visto) Apps.boasvindas();
    if (Sistema.modo === 'navegador' && !Sistema.disco.grava) Avisos.mostrar('TNUX já aberto em outra aba', 'Nesta aba o sistema funciona, mas nada do que você fizer será gravado.', 'atencao', 12000);
    if (Sistema.modo === 'navegador' && !Sistema.vfs) Avisos.mostrar('Apps baixados do GitHub', 'Este endereço não é seguro (https ou localhost), então páginas guardadas no disco do TNUX abrem de forma limitada.', 'atencao', 9000);
  } catch (e) {
    console.error(e);
    boot.querySelector('.boot-barra').hidden = true;
    passo('O TNUX não conseguiu ligar.');
    boot.append(h('p.boot-erro', null, String(e && e.message || e)), h('button.btn.pri', { onclick: () => location.reload() }, 'Tentar de novo'));
  }
})();
