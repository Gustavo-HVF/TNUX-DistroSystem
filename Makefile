# TNUX: compilação.
#
#   make            compila o tnux e o tnuxd para o aparelho (Termux, Linux)
#   make wasm       recompila o núcleo para o navegador (web/tnux.wasm); precisa de clang e lld
#   make programas  compila os programas de exemplo em WebAssembly (programas/*.c)
#   make teste      roda os testes
#   make limpar
#
# O web/tnux.wasm já vem pronto no repositório: só é preciso "make wasm" se você mexer no núcleo.

CC      ?= cc
CFLAGS  ?= -O2
AVISOS   = -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation
NUCLEO   = $(wildcard nucleo/*.c)
CAB      = $(wildcard nucleo/*.h)

# clang que gera WebAssembly (no Termux: pkg install clang lld)
WASM_CC ?= clang
WASM_FLAGS = --target=wasm32 -O2 -nostdlib -ffreestanding -fno-exceptions -fvisibility=hidden -isystem libc/include $(AVISOS)
WASM_LD = -Wl,--no-entry -Wl,--export-dynamic -Wl,--allow-undefined-file=plataforma/importadas.txt \
          -Wl,-z,stack-size=4194304 -Wl,--stack-first -Wl,--strip-all -Wl,--initial-memory=16777216 -Wl,--max-memory=2147483648

todos: build/tnux build/tnuxd

build/tnux: ponte/tnux.c ponte/comum.h plataforma/nativo.c $(NUCLEO) $(CAB)
	@mkdir -p build
	$(CC) $(CFLAGS) $(AVISOS) -o $@ ponte/tnux.c plataforma/nativo.c $(NUCLEO)

build/tnuxd: ponte/tnuxd.c ponte/comum.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(AVISOS) -o $@ ponte/tnuxd.c

wasm: web/tnux.wasm
web/tnux.wasm: $(NUCLEO) $(CAB) plataforma/wasm.c libc/libc.c plataforma/importadas.txt
	@mkdir -p build/wasm
	$(WASM_CC) $(WASM_FLAGS) -fno-builtin -c libc/libc.c -o build/wasm/libc.o
	$(WASM_CC) $(WASM_FLAGS) $(WASM_LD) -o $@ $(NUCLEO) plataforma/wasm.c build/wasm/libc.o

PROGS = $(patsubst programas/%.c,web/programas/%.wasm,$(wildcard programas/*.c))
programas: $(PROGS)
web/programas/%.wasm: programas/%.c programas/tnuxsdk.h
	@mkdir -p web/programas
	$(WASM_CC) --target=wasm32 -O2 -nostdlib -ffreestanding -fno-builtin $(AVISOS) -Wl,--no-entry -Wl,--export=_start \
	    -Wl,--allow-undefined -Wl,--strip-all -Wl,-z,stack-size=262144 -o $@ $<

teste: build/tnux build/tnuxd
	bash tests/testar_nucleo.sh
	node tests/testar_wasm.js
	python3 tests/testar_ponte.py
	@if node -e "require('playwright')" 2>/dev/null; then node tests/testar_navegador.js; \
	else echo "(Playwright não instalado: pulei o teste da área de trabalho no navegador)"; fi

limpar:
	rm -rf build

.PHONY: todos wasm programas teste limpar
