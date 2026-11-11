# TNUX

Um sistema operacional no estilo do **Linux Mint** que roda **dentro do navegador** ou **no terminal do
aparelho** (Termux, Linux). O núcleo é escrito em **C**; a área de trabalho, em HTML, CSS e JavaScript.

```
  .:::::::::::::::::::::::.     usuario@tnux
 :::::::::::::::::::::::::::    -------------
 :::#####################:::    SO: TNUX 1.0 Aurora
 :::#####################:::    Base: Linux Mint
 ::::::::::#######::::::::::    Kernel: 6.8.0-tnux
 ::::::::::#######::::::::::    Shell: bash (tnux)
 ::::::::::#######::::::::::    Interface: TNUX Desktop (estilo Cinnamon)
 :::::::::::::::::::::::::::
  ':::::::::::::::::::::::'
```

O TNUX tem **dois modos**, e é importante entender a diferença:

| | **Modo navegador** | **Modo ponte** |
|---|---|---|
| Onde roda | Inteiro dentro da aba (WebAssembly) | No seu aparelho; o navegador é só a tela |
| Terminal | Shell próprio do TNUX, com mais de 130 comandos | O shell de verdade do aparelho |
| Arquivos | Disco virtual, guardado no navegador | As pastas do aparelho |
| Programas | Apps web, scripts, programas WebAssembly, repositórios do GitHub | **Todos os programas Linux** do aparelho: Chromium, Firefox, GIMP, python, gcc... |
| Precisa instalar? | Não: é só abrir a página | Sim: `bash instalar.sh` no Termux ou no Linux |

> **Sobre "rodar qualquer programa Linux":** um navegador não deixa uma página executar programas
> nativos. Por isso, dentro da aba (modo navegador) o TNUX roda o que foi feito para ele: apps web,
> scripts de shell e programas compilados para WebAssembly. Para programas Linux de verdade, como o
> Chrome, existe o modo ponte: o aparelho instala e executa, e o TNUX é a área de trabalho deles.

## Modo navegador

Sirva a pasta `web/` com qualquer servidor de páginas e abra no navegador. Por exemplo:

```bash
git clone https://github.com/Gustavo-HVF/TNUX-DistroSystem
```

```bash
cd TNUX-DistroSystem/web
```

```bash
python3 -m http.server 8000
```

Abra `http://localhost:8000`. Também funciona publicado no GitHub Pages (basta apontar para a pasta
`web/`). Não é preciso compilar nada: o núcleo já vem pronto em `web/tnux.wasm`.

O que você encontra:

- **Área de trabalho** no jeito do Cinnamon: painel, menu com categorias e busca, janelas que movem,
  redimensionam, minimizam e maximizam, ícones, temas claro e escuro, cores e papéis de parede. No
  celular as janelas abrem em tela cheia.
- **Terminal** com prompt, histórico, Tab para completar, pipes, redirecionamentos, variáveis,
  `if`/`for`/`while`/`case`, funções e scripts `.sh`. Comandos: `ls`, `cp`, `mv`, `rm`, `find`, `tree`,
  `cat`, `grep`, `sed`, `awk`, `sort`, `uniq`, `cut`, `tr`, `head`, `tail`, `wc`, `curl`, `wget`, `git`,
  `apt`, `dpkg`, `sudo`, `neofetch`, `date`, `cal`, `free`, `df`, `du`... Digite `help`.
- **Arquivos**, **Editor de Texto**, **Gerenciador de Aplicativos**, **Navegador Web**, **Visualizador
  de Imagens**, player de vídeo e áudio, **Calculadora**, **Monitor do Sistema** e **Configurações**.
- **O disco não se perde:** tudo o que você grava fica no navegador (IndexedDB) e volta na próxima
  vez. Em Configurações dá para salvar uma cópia do disco num arquivo e restaurar.

### Instalar programas (modo navegador)

```bash
apt search jogo
```

```bash
sudo apt install vaca
```

```bash
sudo apt install lnux-som
```

```bash
sudo apt install USUARIO/REPOSITORIO
```

```bash
git clone https://github.com/USUARIO/REPOSITORIO
```

- `apt install NOME` instala do catálogo (`web/catalogo.json`): programas de terminal em WebAssembly,
  sites que abrem numa janela e apps web baixados do GitHub (como os do
  [LNUX-APPs](https://github.com/Gustavo-HVF/LNUX-APPs)).
- `apt install USUARIO/REPOSITORIO` baixa qualquer repositório do GitHub para `/opt`; se ele tiver um
  `index.html`, vira um programa no menu e abre numa janela.
- `git clone` baixa um repositório para a pasta atual. `xdg-open pasta/index.html` abre a página.
- Um arquivo `.wasm` (padrão WASI) é um programa: `chmod +x programa.wasm && ./programa.wasm`.

O Gerenciador de Aplicativos faz o mesmo com cliques.

**Limites do modo navegador:** só baixa de sites que permitem acesso de outras páginas (o GitHub
permite); `git` baixa e atualiza, mas não faz commit nem push; programas `.wasm` não leem do teclado
enquanto rodam (recebem dados por argumentos, arquivos e pipes); um comando que travar é interrompido
com Ctrl+C reiniciando o núcleo. O Navegador Web abre os sites dentro de uma janela, e muitos sites
grandes (Google, YouTube, bancos) não aceitam isso: para eles há o botão de abrir em uma aba.

## Modo ponte

No Termux (Android) ou no terminal de uma distro Linux:

```bash
git clone https://github.com/Gustavo-HVF/TNUX-DistroSystem
```

```bash
cd TNUX-DistroSystem
```

```bash
bash instalar.sh
```

Abra um terminal novo e use:

```bash
tnux
```

É o **shell do TNUX no terminal**: prompt, histórico e Tab, mas cada comando é executado pelo
aparelho. O `apt` do TNUX entende os nomes do catálogo e traduz para o gerenciador do seu sistema
(`pkg`, `apt`, `pacman`, `dnf`, `apk` ou `zypper`):

```bash
tnux instalar chromium
```

```bash
tnux instalar firefox vlc gimp
```

```bash
tnux instalar Gustavo-HVF/LNUX-APPs
```

O que não está no catálogo vai direto para o gerenciador do aparelho (`tnux instalar htop`). O
Google só publica o Chrome para computadores x86 de 64 bits; em celular e ARM, `tnux instalar chrome`
instala o Chromium, que é o Chrome sem as partes do Google.

### A área de trabalho no modo ponte

```bash
tnux desktop
```

Ele mostra um endereço como `http://127.0.0.1:8096/#token=...`. Abra-o no navegador do aparelho. A
área de trabalho é a mesma, mas agora:

- o **Terminal** é um terminal de verdade (PTY) do aparelho: `vim`, `htop`, `python`, tudo funciona;
- **Arquivos** e **Editor** mexem nas pastas do aparelho;
- o **Gerenciador de Aplicativos** instala programas Linux reais;
- o menu mostra os programas instalados no aparelho.

### Programas com janela (Chromium, Firefox, GIMP...)

Programas gráficos precisam de uma tela. Instale o servidor de tela e abra **Tela do aparelho** no
menu:

```bash
tnux instalar tela
```

A janela "Tela do aparelho" mostra a tela gráfica do aparelho dentro do TNUX, com mouse e teclado. Os
programas gráficos que você abrir pelo menu aparecem ali. (Por baixo é um servidor VNC local, o
`Xvnc`, ligado por um socket que só este usuário alcança.) Se o aparelho já tem uma tela gráfica
própria, os programas abrem nela.

### Compartilhar pela internet (Cloudflare)

Para abrir o TNUX do aparelho de qualquer lugar (de outra casa, pelo 4G, no computador do trabalho),
o TNUX cria um link `https://….trycloudflare.com` pelo túnel gratuito da Cloudflare, sem conta e sem
abrir portas no roteador. Primeiro instale o `cloudflared`:

```bash
tnux instalar cloudflared
```

Depois, ou ligue a área de trabalho já com o link:

```bash
tnux desktop --tunel
```

ou ligue o link numa ponte que já está rodando:

```bash
tnux compartilhar
```

O link aparece no terminal com um **QR code** (aponte a câmera de outro celular). Na área de
trabalho, o programa **Compartilhar pela internet** faz o mesmo, com o botão de copiar o link e o QR
code. Para desligar:

```bash
tnux compartilhar --parar
```

- O link tem uma **chave própria**, diferente da chave do aparelho, e ela **deixa de valer quando o
  túnel é desligado**. Ao ligar de novo, sai um link novo.
- **Quem tiver o link controla o aparelho como você** (terminal, arquivos, programas). Mande só
  para quem você confia.
- O endereço `trycloudflare.com` muda toda vez que o túnel liga. Isso é do serviço gratuito da
  Cloudflare.

### O LNUX dentro do TNUX

O [LNUX](https://github.com/Gustavo-HVF/Termux-Toons), do mesmo autor, se instala com
`tnux instalar lnux` e passa a fazer parte do sistema:

- **na área de trabalho**, o programa **LNUX** mostra as opções dele por categoria: geral, música e
  vídeo, rede, celular, e jogos e programação. Cada opção abre num terminal; as que precisam de um
  link, um arquivo ou uma pasta perguntam antes. As que só funcionam no Termux aparecem apagadas nas
  distros;
- **no terminal**, `tnux lnux` abre o menu completo do LNUX, e `tnux lnux OPÇÃO` roda uma opção
  direto. Exemplos: `tnux lnux ficha`, `tnux lnux baixar "nome da música"`, `tnux lnux pasta ~/Fotos`.

O TNUX acha o LNUX em `~/LNUX`, `~/Termux-Toons`, pelo apelido `lnux` do `~/.bashrc`, ou onde a
variável `LNUX_PY` disser.

### O computador fazendo o serviço pesado do celular

Um celular tem pouco processador e pouca memória. Se você abrir o TNUX do celular **no navegador de um
computador**, o computador empresta o processador dele: o celular manda os arquivos, o computador
processa e devolve o resultado.

No Termux do celular:

```bash
tnux desktop --rede
```

Ele mostra também um endereço com o IP do celular (`http://192.168.x.x:8096/#token=...`). Abra esse
endereço no navegador do computador, na mesma rede Wi-Fi, e deixe a aba aberta. Depois, no celular
(em outra sessão do Termux, ou no Terminal da própria área de trabalho):

```bash
tnux pc
```

```bash
tnux pc imagem --para webp --largura 1600 *.jpg
```

```bash
tnux pc zip fotos.zip Fotos/
```

```bash
tnux pc unzip fotos.zip
```

```bash
tnux pc hash video.mp4
```

```bash
tnux pc rodar 'sort -n dados.txt | uniq -c | sort -rn > contagem.txt' dados.txt
```

| Comando | O que o computador faz |
|---|---|
| `tnux pc` | Mostra os computadores conectados e os trabalhos |
| `tnux pc imagem` | Converte e reduz imagens (WebP, JPG, PNG), várias ao mesmo tempo |
| `tnux pc zip` / `unzip` | Compacta e extrai |
| `tnux pc hash` | Calcula o SHA-256 |
| `tnux pc rodar 'COMANDOS' ARQUIVOS...` | Roda comandos do shell do TNUX (e programas `.wasm`) sobre cópias dos arquivos; o que for criado volta para a pasta atual do celular |

O computador usa um Web Worker por núcleo. As mesmas operações aparecem no menu do Gerenciador de
Arquivos (botão direito: Converter imagens, Compactar em .zip, Extrair aqui, SHA-256), e a janela
**Processamento no computador** mostra a fila.

**Limite:** o computador processa com o que um navegador sabe fazer. Converter vídeo (ffmpeg) ainda
não está entre os trabalhos.

### Segurança da ponte

- O `tnuxd` só escuta em `127.0.0.1`, a não ser que você peça `--rede`.
- Tudo o que mexe no aparelho exige a **chave** (a parte depois de `#token=`). Quem tem a chave
  controla o aparelho como você: não compartilhe o endereço.
- A chave não fica em cookie, e os apps web instalados são servidos por outro nome de host e por uma
  chave separada, que só dá acesso à pasta deles: um app baixado não alcança o terminal nem os
  arquivos.
- Com `--rede` a conexão **não é cifrada**: use só numa rede de confiança (a de casa). Fora de
  casa, use o link da Cloudflare (`--tunel`), que é https.
- O link da Cloudflare usa uma chave separada, que deixa de valer quando o túnel desliga.

### Experimentar o sistema do navegador no terminal

```bash
tnux --sandbox
```

Roda no terminal o mesmo sistema da aba do navegador (disco em memória, gravado em
`~/.tnux/sandbox.img`). Os comandos **não** mexem no aparelho.

## Escrever programas para o TNUX

Programas do modo navegador são arquivos WebAssembly no padrão WASI. O `programas/tnuxsdk.h` tem o
mínimo para escrever em C só com o clang:

```c
#include "tnuxsdk.h"

int main(int argc, char **argv) {
    printf("Olá, %s!\n", argc > 1 ? argv[1] : "mundo");
    return 0;
}
```

```bash
make programas
```

O resultado sai em `web/programas/`. Programas feitos com wasi-sdk, Rust (`wasm32-wasip1`), TinyGo ou
Zig também rodam: copie o `.wasm` para dentro do TNUX (Arquivos → enviar) e dê `chmod +x`.

Para pôr um programa ou app no catálogo, acrescente uma entrada em `web/catalogo.json`:

| `tipo` | O que é | Campos |
|---|---|---|
| `wasm` | Programa de terminal | `url` (do `.wasm`), `comando` |
| `git` | App web num repositório do GitHub | `repo`, `pastas` (opcional), `entrada` (o `.html`) |
| `web` | Site que abre numa janela | `url` |
| `nativo` | Programa Linux (modo ponte) | `nativo`: nome do pacote em cada gerenciador, `exec` |

## Como é por dentro

```
nucleo/       o núcleo, em C: sistema de arquivos em memória, shell, comandos, apt, git, WASI
libc/         a libc mínima do núcleo no navegador (malloc, strings, snprintf, qsort)
plataforma/   a fronteira do núcleo: wasm.c (navegador) e nativo.c (terminal)
ponte/        tnux.c (o shell, "tnux pc", "tnux lnux", "tnux compartilhar"), tnuxd.c (servidor: HTTP,
              WebSocket, PTY, tela, fila, túnel) e qr.h (gerador de QR code)
programas/    SDK e programas de exemplo em C para o modo navegador
web/          a área de trabalho: index.html, css/, js/, o núcleo compilado (tnux.wasm) e o catálogo
tests/        testes do núcleo, da ponte e da área de trabalho
```

- **O núcleo não fala com o mundo.** Tudo o que depende do ambiente (escrever no terminal, hora, rede,
  janelas) passa por oito funções `host_*`. No navegador elas são JavaScript (`web/js/nucleo-host.js`);
  no terminal, C (`plataforma/nativo.c`). O mesmo código do núcleo é compilado para os dois.
- **No navegador** o núcleo roda num Web Worker. O disco é uma árvore em memória, gravada inteira no
  IndexedDB depois de cada alteração. Um service worker faz os arquivos desse disco aparecerem como
  endereços (`vfs/...`), e é assim que um site baixado do GitHub abre numa janela.
- **A ponte** (`tnuxd`) é um servidor de um processo só, com `poll()`, sem bibliotecas além da libc:
  HTTP, WebSocket (com SHA-1 e base64 próprios), terminal por PTY, API de arquivos, proxy da tela e a
  fila de trabalhos.

## Compilar e testar

```bash
make
```

```bash
make wasm
```

```bash
make teste
```

- `make` compila `build/tnux` e `build/tnuxd` (precisa de um compilador C e `make`).
- `make wasm` recompila `web/tnux.wasm` (precisa de `clang` e `lld`; só é necessário se você mexer no
  núcleo).
- `make teste` roda os testes do núcleo no terminal, do núcleo em WebAssembly (Node), da ponte
  (Python) e, se o Playwright estiver instalado, da área de trabalho num navegador de verdade.

## O que foi e o que não foi testado

Testado automaticamente neste repositório: o núcleo (terminal e WebAssembly), `git clone` e `apt`
contra o GitHub de verdade, a ponte (chave, arquivos, terminal, fila, tela VNC, túnel), a área de
trabalho no Chromium (modo navegador, celular e ponte), um programa gráfico real aparecendo na tela
do TNUX, o `tnux pc` de ponta a ponta, as opções do LNUX pela área de trabalho, e o gerador de QR code
(660 QR codes lidos de volta pelo zxing-cpp, versões 1 a 15). Tudo em Linux x86-64.

**Não testado:** a execução dentro do Termux num celular de verdade; **um túnel real da
Cloudflare**: o ambiente de testes não alcança a Cloudflare, então o túnel foi testado com um
`cloudflared` simulado, que escreve as mesmas mensagens do verdadeiro; as opções do LNUX de verdade,
além da ficha do sistema (os testes usam um LNUX simulado); a instalação dos pacotes nativos
do catálogo em cada distribuição (os nomes de pacote para `pkg`, `pacman`, `dnf`, `apk` e `zypper`
foram escritos de memória e podem precisar de ajuste); os sites externos do catálogo (Photopea,
diagrams.net) e do Navegador, que dependem de cada site aceitar ser aberto dentro de uma janela; e
outros navegadores além do Chromium.

## Licença e créditos

MIT (veja [LICENSE](LICENSE)). O TNUX é inspirado no Linux Mint, mas é um projeto independente: não
é feito nem endossado pela equipe do Linux Mint e não usa código nem arte deles. Créditos de
terceiros em [CREDITOS.md](CREDITOS.md).

Do mesmo autor: [LNUX](https://github.com/Gustavo-HVF/Termux-Toons) (canivete suíço para o terminal) e
[LNUX-APPs](https://github.com/Gustavo-HVF/LNUX-APPs) (apps web de criação), que o TNUX instala com
`tnux instalar lnux` e `apt install lnux-video`, `lnux-som`, `lnux-efeitos`, `lnux-vlc`, `lnux-companhia`.
