/* TNUX: funções de apoio compartilhadas pelos arquivos de comandos. */
#ifndef TNUX_CMDS_H
#define TNUX_CMDS_H
#include "tnux.h"

/* lê um arquivo inteiro (ou a entrada padrão, se arq for NULL ou "-"). 0 = ok; em erro já avisa. */
int ler_arq(Ctx *c, const char *cmd, const char *arq, const char **d, size_t *n);
void modo_txt(uint32_t modo, char *dst);          /* "drwxr-xr-x" (dst com 11 bytes) */
const char *dono_nome(uint32_t uid);
/* percorre as linhas de um texto: devolve o começo da linha e o tamanho (sem o \n); NULL no fim */
const char *prox_linha(const char **p, const char *fim, size_t *tam);
int copiar_no(Ctx *c, const char *cmd, Inode *origem, const char *destino, int recursivo);
extern const char *const MESES[12];
#endif
