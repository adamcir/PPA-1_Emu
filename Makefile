CC = gcc
PKG_CONFIG ?= pkg-config
SDL3_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl3 2>/dev/null)
SDL3_LIBS := $(shell $(PKG_CONFIG) --libs sdl3 2>/dev/null)
SDL3_TTF_PKG := $(shell if $(PKG_CONFIG) --exists sdl3-ttf; then echo sdl3-ttf; elif $(PKG_CONFIG) --exists SDL3_ttf; then echo SDL3_ttf; fi)
SDL3_TTF_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(SDL3_TTF_PKG) 2>/dev/null)
SDL3_TTF_LIBS := $(shell $(PKG_CONFIG) --libs $(SDL3_TTF_PKG) 2>/dev/null)

EMU = out/PPA-1_Emu
SRC = main.c logger.c gui.c
ASM_SRC = tools/asm/ppa-1_asm.c
ASM = out/asm/ppa-1_asm

.PHONY: all clean run

all: asm emu

out:
	mkdir -p out/asm/

emu: out
	$(CC) $(SDL3_CFLAGS) $(SDL3_TTF_CFLAGS) $(SRC) -o $(EMU) $(SDL3_LIBS) $(SDL3_TTF_LIBS) -lm

asm: out
	$(CC) $(ASM_SRC) -o $(ASM)

clean:
	rm -rf out/
