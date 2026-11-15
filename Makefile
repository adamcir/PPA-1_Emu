CC = gcc
PYTHON ?= python3
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
SRC = src/main.c src/logger.c src/gui.c src/config.c \
      modules/ppa_module.c modules/ppa_sound.c modules/ppa_light.c \
      modules/ppa_module_ui.c modules/ppa_module_draw.c \
      modules/ppa_sound_gui.c modules/ppa_light_gui.c
EMU_HEADERS = src/gui.h src/logger.h src/config.h \
              modules/ppa_module.h modules/ppa_sound.h modules/ppa_light.h \
              modules/ppa_module_ui.h modules/ppa_module_draw.h \
              modules/ppa_sound_gui.h modules/ppa_light_gui.h
ASM_SRC = tools/asm/ppa-1_asm.c
ASM = out/asm/ppa-1_asm

EXAMPLE_SRCS := $(wildcard examples/*.asm)
EXAMPLE_BINS := $(patsubst examples/%.asm,out/examples/%.bin,$(EXAMPLE_SRCS))
MODULE_DEMOS := $(wildcard modules/*_demo.asm)
MODULE_DEMO_BINS := $(patsubst modules/%.asm,out/examples/%.bin,$(MODULE_DEMOS))
MODULE_DEFS := $(wildcard modules/*.json)
MODULE_BINS := $(patsubst modules/%.json,out/modules/%.ppamod,$(MODULE_DEFS))

.PHONY: all clean run asm emu examples sound-examples modules test-modules
all: asm emu modules examples

out/asm out/examples out/modules:
	mkdir -p $@

$(EMU): $(SRC) $(EMU_HEADERS)
	mkdir -p out
	$(CC) $(SDL3_CFLAGS) $(SDL3_TTF_CFLAGS) $(SDL3_IMAGE_CFLAGS) $(SRC) -o $(EMU) $(SDL3_LIBS) $(SDL3_TTF_LIBS) $(SDL3_IMAGE_LIBS) -lm
	rm -rf out/assets
	cp -a assets out/assets

emu: $(EMU)

$(ASM): $(ASM_SRC) | out/asm
	$(CC) $(ASM_SRC) -o $(ASM)
asm: $(ASM)

out/modules/%.ppamod: modules/%.json modules/build_modules.py | out/modules
	$(PYTHON) modules/build_modules.py $< $@
modules: $(MODULE_BINS)

out/ppa_module_test: modules/ppa_module.c modules/ppa_sound.c modules/ppa_light.c modules/ppa_module.h modules/ppa_sound.h modules/ppa_light.h modules/ppa_module_test.c
	mkdir -p out
	$(CC) -Imodules modules/ppa_module.c modules/ppa_sound.c modules/ppa_light.c modules/ppa_module_test.c -lm -o $@
test-modules: modules out/ppa_module_test
	./out/ppa_module_test

out/examples/%.bin: examples/%.asm $(ASM) $(wildcard examples/includes/*.inc) | out/examples
	$(ASM) $< $@
out/examples/%_demo.bin: modules/%_demo.asm $(ASM) | out/examples
	$(ASM) $< $@
examples: $(EXAMPLE_BINS) $(MODULE_DEMO_BINS)
sound-examples: examples

# Keep runtime preferences and logs across clean rebuilds.
clean:
	rm -rf out/asm out/examples out/modules out/assets
	rm -f out/PPA-1_Emu out/ppa_module_test
