CC = gcc
PKG_CONFIG ?= pkg-config
SDL3_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl3 2>/dev/null)
SDL3_LIBS := $(shell $(PKG_CONFIG) --libs sdl3 2>/dev/null)
SDL3_TTF_PKG := $(shell if $(PKG_CONFIG) --exists sdl3-ttf; then echo sdl3-ttf; elif $(PKG_CONFIG) --exists SDL3_ttf; then echo SDL3_ttf; fi)
SDL3_TTF_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(SDL3_TTF_PKG) 2>/dev/null)
SDL3_TTF_LIBS := $(shell $(PKG_CONFIG) --libs $(SDL3_TTF_PKG) 2>/dev/null)
SDL3_IMAGE_PKG := $(shell if $(PKG_CONFIG) --exists sdl3-image; then echo sdl3-image; elif $(PKG_CONFIG) --exists SDL3_image; then echo SDL3_image; fi)
SDL3_IMAGE_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(SDL3_IMAGE_PKG) 2>/dev/null)
SDL3_IMAGE_LIBS := $(shell $(PKG_CONFIG) --libs $(SDL3_IMAGE_PKG) 2>/dev/null)

EMU = out/PPA-1_Emu
SRC = main.c logger.c gui.c src/ppa_module.c
ASM_SRC = tools/asm/ppa-1_asm.c
ASM = out/asm/ppa-1_asm

EXAMPLE_SRCS := $(wildcard examples/*.asm)
EXAMPLE_BINS := $(patsubst examples/%.asm,out/examples/%.bin,$(EXAMPLE_SRCS))
MODULE_DEMOS := $(wildcard modules/*_demo.asm)
MODULE_DEMO_BINS := $(patsubst modules/%.asm,out/examples/%.bin,$(MODULE_DEMOS))

.PHONY: all clean run asm emu examples sound-examples test-modules

all: asm emu examples

out:
	mkdir -p out/asm/

emu: out
	$(CC) $(SDL3_CFLAGS) $(SDL3_TTF_CFLAGS) $(SDL3_IMAGE_CFLAGS) $(SRC) -o $(EMU) $(SDL3_LIBS) $(SDL3_TTF_LIBS) $(SDL3_IMAGE_LIBS) -lm
	rm -rf out/assets
	cp -a assets out/assets

asm: $(ASM)

$(ASM): $(ASM_SRC) | out
	$(CC) $(ASM_SRC) -o $(ASM)

clean:
	rm -rf out/

# Minimal regression test for binary PPAM loader and LIGHT card.
test-modules: out
	$(CC) -Isrc src/ppa_module.c src/ppa_module_test.c -lm -o out/ppa_module_test
	./out/ppa_module_test

# Build every assembly example and module demo into out/examples/.
# GNU Make rebuilds only outputs whose sources (or assembler) changed.
out/examples:
	mkdir -p $@

out/examples/%.bin: examples/%.asm $(ASM) | out/examples
	$(ASM) $< $@

out/examples/%_demo.bin: modules/%_demo.asm $(ASM) | out/examples
	$(ASM) $< $@

examples: $(EXAMPLE_BINS) $(MODULE_DEMO_BINS)

# Compatibility alias for existing sound-example workflow.
sound-examples: examples
