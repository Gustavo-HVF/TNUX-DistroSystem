#!/usr/bin/env bash
# Instala o TNUX neste aparelho: compila o "tnux" e o "tnuxd" e cria o atalho "tnux".
# Funciona no Termux (Android) e nas distros com apt, pacman, dnf, apk ou zypper.
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

# sudo/doas só quando precisa (no Termux e para quem já é root, não)
if [ "$(id -u)" -eq 0 ]; then ROOT=""
elif command -v sudo >/dev/null 2>&1; then ROOT="sudo"
elif command -v doas >/dev/null 2>&1; then ROOT="doas"
else ROOT=""
fi

precisa=""
command -v cc >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || command -v clang >/dev/null 2>&1 || precisa="compilador"
command -v make >/dev/null 2>&1 || precisa="$precisa make"
command -v git >/dev/null 2>&1 || precisa="$precisa git"
command -v curl >/dev/null 2>&1 || precisa="$precisa curl"

if [ -n "$precisa" ]; then
    echo "Faltam ferramentas ($precisa). Instalando..."
    if [ -n "$PREFIX" ] && [[ "$PREFIX" == *com.termux* ]]; then
        pkg install -y clang make git curl
    elif command -v apt-get >/dev/null 2>&1; then
        $ROOT apt-get update
        $ROOT apt-get install -y gcc make git curl
    elif command -v pacman >/dev/null 2>&1; then
        $ROOT pacman -S --needed --noconfirm gcc make git curl
    elif command -v dnf >/dev/null 2>&1; then
        $ROOT dnf install -y gcc make git curl
    elif command -v apk >/dev/null 2>&1; then
        $ROOT apk add gcc musl-dev make git curl bash
    elif command -v zypper >/dev/null 2>&1; then
        $ROOT zypper --non-interactive install gcc make git curl
    else
        echo "Não reconheci o gerenciador de pacotes. Instale um compilador C, make, git e curl e rode de novo."
        exit 1
    fi
fi

echo "Compilando o TNUX..."
make

# atalho "tnux" no shell
for rc in "$HOME/.bashrc" "$HOME/.zshrc"; do
    if [ "$rc" = "$HOME/.zshrc" ] && [ ! -f "$rc" ]; then continue; fi
    touch "$rc"
    if grep -q "^alias tnux=" "$rc"; then
        sed -i "s#^alias tnux=.*#alias tnux='$DIR/build/tnux'#" "$rc"
    else
        echo "alias tnux='$DIR/build/tnux'" >> "$rc"
    fi
done

echo
echo "Pronto! Abra um novo terminal (ou rode: source ~/.bashrc) e use:"
echo
echo "  tnux                  o shell do TNUX (os comandos rodam neste aparelho)"
echo "  tnux desktop          a área de trabalho, no navegador deste aparelho"
echo "  tnux desktop --rede   idem, e um computador da rede pode abrir e emprestar o processador"
echo "  tnux instalar NOME    instala programas (chromium, firefox, vlc, gimp...)"
echo "  tnux ajuda            mais comandos"
if [ -n "$PREFIX" ] && [[ "$PREFIX" == *com.termux* ]]; then
    echo
    echo "Para programas com janela (Chromium, Firefox...) dentro do TNUX: tnux instalar tela"
fi
exit 0
