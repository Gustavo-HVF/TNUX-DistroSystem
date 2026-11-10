/* ola: o primeiro programa. Mostra argumentos, e grava e relê um arquivo.   uso: ola [nome] */
#include "tnuxsdk.h"

int main(int argc, char **argv) {
    const char *nome = argc > 1 ? argv[1] : "mundo";
    printf("Olá, %s! Sou um programa em C rodando dentro do TNUX (WebAssembly).\n", nome);
    printf("Recebi %d argumento(s).\n", argc - 1);

    int fd = abrir("ola.txt", "w");
    if (fd < 0) { eprintf("ola: não consegui criar ola.txt nesta pasta\n"); return 1; }
    tnux_iov v = { "gravado pelo programa ola\n", 26 };
    uint32_t w;
    __wasi_fd_write(fd, &v, 1, &w);
    fechar(fd);

    char buf[64];
    fd = abrir("ola.txt", "r");
    int n = fd >= 0 ? ler(fd, buf, sizeof buf - 1) : 0;
    if (fd >= 0) fechar(fd);
    if (n < 0) n = 0;
    buf[n] = 0;
    printf("Li de volta do arquivo ola.txt: %s", buf);
    return 0;
}
