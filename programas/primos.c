/* primos: lista os números primos até N (crivo de Eratóstenes) e mede o tempo.   uso: primos [N] */
#include "tnuxsdk.h"

static unsigned char crivo[5000001];

int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 100;
    if (n < 2 || n > 5000000) { eprintf("primos: informe um número entre 2 e 5000000\n"); return 1; }
    uint64_t t0, t1;
    __wasi_clock_time_get(1, 1000, &t0);
    int total = 0;
    for (int i = 2; i <= n; i++) {
        if (crivo[i]) continue;
        total++;
        for (long long j = (long long)i * i; j <= n; j += i) crivo[j] = 1;
    }
    __wasi_clock_time_get(1, 1000, &t1);
    if (n <= 1000) {
        int col = 0;
        for (int i = 2; i <= n; i++) if (!crivo[i]) { printf("%5d", i); if (++col % 12 == 0) printf("\n"); }
        if (col % 12) printf("\n");
    }
    printf("Há %d números primos até %d (calculado em %d ms).\n", total, n, (int)((t1 - t0) / 1000000));
    return 0;
}
