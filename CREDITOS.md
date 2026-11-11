# Créditos

O TNUX é escrito do zero para este projeto (núcleo em C, ponte em C, área de trabalho em HTML, CSS e
JavaScript), com as exceções abaixo.

## Código de terceiros incluído

| O quê | Onde | Licença | Autores |
|---|---|---|---|
| [xterm.js](https://xtermjs.org/) 6.0.0 e addon-fit 0.11.0 | `web/vendor/xterm/` | MIT (`web/vendor/xterm/LICENSE`) | The xterm.js authors, SourceLair, Christopher Jeffrey |

É o emulador de terminal usado pela janela Terminal. Não foi modificado.

## Ideias e algoritmos

- **Linux Mint e Cinnamon**: a inspiração para a aparência e para a organização do sistema (menu,
  painel, nomes de programas, mensagens do `apt`). Nenhum código, ícone, papel de parede ou logotipo
  do Linux Mint foi usado; os ícones e o logotipo do TNUX foram desenhados para este projeto. O TNUX
  não é feito nem endossado pela equipe do Linux Mint. "Linux Mint" é marca de seus donos.
- **Editor de linha** (`nucleo/editor.c`): a técnica de redesenhar linhas que passam da largura do
  terminal segue a do [linenoise](https://github.com/antirez/linenoise), de Salvatore Sanfilippo
  (BSD-2). O código foi reescrito.
- **Expressões regulares** (`nucleo/base.c`): o casador com retrocesso parte do exemplo de Rob Pike
  e Brian Kernighan em *The Practice of Programming*, ampliado com classes e quantificadores.
- **QR code** (`ponte/qr.h`): escrito a partir da norma ISO/IEC 18004, seguindo o roteiro da
  implementação de referência de [Project Nayuki](https://www.nayuki.io/page/qr-code-generator-library) (MIT).
- **Datas** (`nucleo/kernel.c`): o cálculo de dia, mês e ano usa o algoritmo de datas civis de
  Howard Hinnant (domínio público).
- **Protocolos**: RFB/VNC (RFC 6143), WebSocket (RFC 6455), SHA-1 (RFC 3174), SHA-256 (FIPS 180-4),
  formato ZIP (APPNOTE da PKWARE) e WASI preview 1: implementações próprias, a partir das
  especificações.

## Programas que o TNUX instala

O catálogo (`web/catalogo.json`) aponta para programas de outros autores (Chromium, Firefox, VLC,
GIMP, LibreOffice, o `cloudflared` da Cloudflare...). O TNUX não os distribui: no modo ponte, quem os baixa e instala é o
gerenciador de pacotes do seu aparelho, e cada um tem a própria licença. "Google Chrome" é marca do
Google. Os apps `lnux-*` vêm do [LNUX-APPs](https://github.com/Gustavo-HVF/LNUX-APPs), do mesmo autor
do TNUX (créditos no `CREDITOS.md` de lá).
