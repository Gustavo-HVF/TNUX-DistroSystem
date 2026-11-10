/* vaca: a vaquinha que fala (no estilo do cowsay).   uso: vaca texto...   ou   echo texto | vaca */
#include "tnuxsdk.h"

static int largura(const char *s, int n) { int l = 0; for (int i = 0; i < n; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) l++; return l; }

int main(int argc, char **argv) {
    static char txt[2048];
    int n = 0;
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (i > 1 && n < (int)sizeof txt - 1) txt[n++] = ' ';
            for (const char *p = argv[i]; *p && n < (int)sizeof txt - 1; p++) txt[n++] = *p;
        }
    } else {
        n = ler(0, txt, sizeof txt - 1);
        if (n < 0) n = 0;
        while (n > 0 && (txt[n - 1] == '\n' || txt[n - 1] == ' ')) n--;
        for (int i = 0; i < n; i++) if (txt[i] == '\n') txt[i] = ' ';
    }
    if (n == 0) { const char *m = "Muuu! Escreva algo: vaca Olá"; n = (int)strlen(m); memcpy(txt, m, (size_t)n); }
    txt[n] = 0;
    int l = largura(txt, n);
    printf(" ");
    for (int i = 0; i < l + 2; i++) printf("_");
    printf("\n< %s >\n ", txt);
    for (int i = 0; i < l + 2; i++) printf("-");
    printf("\n        \\   ^__^\n         \\  (oo)\\_______\n            (__)\\       )\\/\\\n                ||----w |\n                ||     ||\n");
    return 0;
}
