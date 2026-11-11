#!/usr/bin/env bash
# Testa o núcleo compilado para o terminal (tnux --sandbox): o mesmo sistema do navegador,
# com a libc do aparelho. Cada linha: comando e a saída esperada.
#
#     bash tests/testar_nucleo.sh
cd "$(dirname "$0")/.." || exit 1
T="./build/tnux --sandbox --sem-disco -c"
falhas=0; feitos=0

confere() {   # confere "comando" "saída esperada"
    feitos=$((feitos + 1))
    local saida
    saida="$($T "$1" 2>&1)"
    if [ "$saida" != "$2" ]; then
        falhas=$((falhas + 1))
        printf 'FALHOU: %s\n        saída:    %q\n        esperado: %q\n' "$1" "$saida" "$2"
    fi
}
contem() {    # contem "comando" "trecho que deve aparecer"
    feitos=$((feitos + 1))
    local saida
    saida="$($T "$1" 2>&1)"
    case "$saida" in
        *"$2"*) ;;
        *) falhas=$((falhas + 1)); printf 'FALHOU: %s\n        saída: %q\n        devia conter: %q\n' "$1" "$saida" "$2" ;;
    esac
}

# shell
confere 'echo oi $USER em $PWD' 'oi usuario em /home/usuario'
confere 'x="a b"; for w in $x; do echo "[$w]"; done; echo "${x#a }" ${#x} "${nada:-padrao}"' $'[a]\n[b]\nb 3 padrao'
confere 'echo $((2+3*4)) $(( (2+3)*4 )) $((10/3)) $((10%3))' '14 20 3 1'
confere 'i=0; while [ $i -lt 3 ]; do i=$((i+1)); done; echo $i' '3'
confere 'for i in 1 2 3; do if [ $i -eq 2 ]; then continue; fi; echo $i; done' $'1\n3'
confere 'case foo.txt in *.md) echo md;; *.txt|*.c) echo txt;; *) echo outro;; esac' 'txt'
confere 'soma() { echo $(( $1 + $2 )); return 3; }; soma 2 5; echo "rc=$?"' $'7\nrc=3'
confere 'true && echo sim || echo nao; false && echo sim || echo nao; ! false && echo negado' $'sim\nnao\nnegado'
confere 'echo "aspas $HOME" '"'"'simples $HOME'"'"' sem\ espaco' 'aspas /home/usuario simples $HOME sem espaco'
confere 'echo a > f; echo b >> f; cat f; cat < f | wc -l; cat nada 2>/dev/null; echo "rc=$?"' $'a\nb\n2\nrc=1'
confere 'echo $(echo dentro $(echo mais))' 'dentro mais'
confere 'A=1 sh -c "echo \$A"; echo "[$A]"' $'1\n[]'
confere 'echo *.nada /e*c ~/Doc*' '*.nada /etc /home/usuario/Documentos'
confere 'alias oi="echo ola"; oi voce' 'ola voce'
confere 'cd /tmp && pwd && cd - >/dev/null && pwd' $'/tmp\n/home/usuario'
confere 'if [ -d Documentos ] && [ ! -f nada ]; then echo ok; fi' 'ok'
contem  'if true; then echo x' 'erro de sintaxe'
contem  'naoexiste' 'comando não encontrado'

# arquivos
confere 'mkdir -p a/b/c && touch a/b/c/x.txt a/y.md && find a -name "*.txt"' 'a/b/c/x.txt'
confere 'mkdir d && echo 1 > d/um && cp -r d e && mv e/um e/dois && ls e && rm -r d e && ls | grep -c "^[de]$"' $'dois\n0'
confere 'echo x > t; chmod 600 t; ls -l t | cut -c1-10; chmod +x t; ls -l t | cut -c1-10' $'-rw-------\n-rwx--x--x'
confere 'ln -s Documentos doc; readlink doc; ls doc/ | head -n 1' $'Documentos\nexemplo.sh'
confere 'echo x > /etc/teste 2>&1; echo "rc=$?"; sudo touch /etc/teste && ls /etc/teste' $'bash: /etc/teste: Permissão negada\nrc=1\n/etc/teste'
confere 'cd /root 2>&1; sudo ls -d /root' $'bash: cd: /root: Permissão negada\n/root'
confere 'basename /a/b/c.txt .txt; dirname /a/b/c.txt' $'c\n/a/b'
confere 'cd Documentos && ./exemplo.sh Gustavo | head -n 2' $'Olá, Gustavo! Este é o TNUX/Linux 6.8.0-tnux.\ncontando: 1'
contem  'rm -rf /' 'perigoso'
contem  'tree /home' 'Documentos'
contem  'df -h' 'Montado em'

# texto
confere 'printf "%-5s|%3d|%.2f\n" ab 7 3.14159' 'ab   |  7|3.14'
confere 'echo -e "a\tb"; echo -n sem; echo " quebra"' $'a\tb\nsem quebra'
confere 'seq 1 5 | tac | tr "\n" ,; echo' '5,4,3,2,1,'
confere 'printf "b\na\nc\na\n" | sort | uniq -c | sort -rn | head -n 1' '      2 a'
confere 'echo "nome: Ana, idade: 30" | sed "s/[0-9]+/XX/" | cut -d, -f2' ' idade: XX'
confere 'printf "1 2 3\n4 5 6\n" | awk "{print \$3, \$1}"' $'3 1\n6 4'
confere 'echo "Olá Mundo" | tr a-z A-Z | rev' 'ODNUM áLO'
confere 'printf "um\ndois\ntres\n" | grep -n -i D' '2:dois'
confere 'seq 1 100 | grep -c 7' '19'
confere 'printf "a\nb\nc\nd\n" | sed -n "2,3p"' $'b\nc'
confere 'echo abc | base64 | base64 -d' 'abc'
confere 'echo um dois tres | xargs -n1 echo item | wc -l' '3'
confere 'printf "x\n" | wc -c; printf "a b\nc\n" | wc -w' $'2\n3'
confere 'seq 1 10 | head -n 3 | tail -n 1' '3'

# sistema e pacotes
confere 'whoami; sudo whoami; id -u' $'usuario\nroot\n1000'
confere 'uname -s; uname -o; cat /etc/os-release | grep ^ID=' $'Linux\nTNUX/Linux\nID=tnux'
confere 'date +%Y | grep -c "^20"' '1'
contem  'neofetch' 'TNUX 1.0 Aurora'
contem  'cal 11 2026' ' 1  2  3  4  5  6  7'
contem  'apt install vaca' 'você é root'
confere 'apt search vaquinha | grep -c "^vaca/"' '1'
contem  'sudo apt install chrome' 'modo ponte'
contem  'sudo apt install pacote-que-nao-existe' 'Impossível encontrar o pacote'
contem  'dpkg -l' 'coreutils'
confere 'type ls cd ll | wc -l; which apt git' $'3\n/usr/bin/apt\n/usr/bin/git'
contem  'git clone https://gitlab.com/a/b' 'só clona do GitHub'
contem  'tnux versao' 'TNUX 1.0'

echo "$((feitos - falhas))/$feitos testes do núcleo (terminal) passaram"
[ "$falhas" -eq 0 ]
