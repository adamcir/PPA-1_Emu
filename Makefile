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
      modules/ppa_module.c modules/ppa_sound.c modules/ppa_light.c modules/ppa_vga.c modules/ppa_ps2.c \
      modules/ppa_module_ui.c modules/ppa_module_draw.c \
      modules/ppa_sound_gui.c modules/ppa_light_gui.c modules/ppa_vga_gui.c \
      modules/ppa_monitor.c modules/ppa_ps2_gui.c src/irq.c
EMU_HEADERS = src/gui.h src/logger.h src/config.h src/irq.h \
              modules/ppa_module.h modules/ppa_sound.h modules/ppa_light.h modules/ppa_vga.h modules/ppa_ps2.h \
              modules/ppa_module_ui.h modules/ppa_module_draw.h \
              modules/ppa_sound_gui.h modules/ppa_light_gui.h modules/ppa_vga_gui.h \
              modules/ppa_monitor.h modules/ppa_ps2_gui.h
ASM_SRC = tools/asm/ppa-1_asm.c
ASM = out/asm/ppa-1_asm
ROMPACK_SRC = tools/rompack/ppa_rompack.c
ROMPACK = out/tools/ppa_rompack

# Built-in examples are grouped by category. Custom programs are opt-in
# so unfinished user ASM files cannot break the standard 'make all'.
EXAMPLE_SRCS := $(filter-out examples/custom/%,$(wildcard examples/*/*.asm))
CUSTOM_EXAMPLE_SRCS := $(wildcard examples/custom/*.asm)
EXAMPLE_PROGRAMS := $(patsubst examples/%.asm,out/examples/%.prg,$(EXAMPLE_SRCS))
EXAMPLE_BINS := $(patsubst examples/%.asm,out/examples/%.bin,$(EXAMPLE_SRCS))
CUSTOM_EXAMPLE_PROGRAMS := $(patsubst examples/%.asm,out/examples/%.prg,$(CUSTOM_EXAMPLE_SRCS))
CUSTOM_EXAMPLE_BINS := $(patsubst examples/%.asm,out/examples/%.bin,$(CUSTOM_EXAMPLE_SRCS))
MODULE_DEMOS := $(wildcard modules/*_demo.asm)
MODULE_DEMO_PROGRAMS := $(patsubst modules/%.asm,out/examples/%.prg,$(MODULE_DEMOS))
MODULE_DEMO_BINS := $(patsubst modules/%.asm,out/examples/%.bin,$(MODULE_DEMOS))

# Keep assembler outputs as real, inspectable intermediate files.
.PRECIOUS: out/examples/%.prg
.SECONDARY: $(EXAMPLE_PROGRAMS) $(CUSTOM_EXAMPLE_PROGRAMS) $(MODULE_DEMO_PROGRAMS)
MODULE_DEFS := $(wildcard modules/*.json)
MODULE_BINS := $(patsubst modules/%.json,out/modules/%.ppamod,$(MODULE_DEFS))

.PHONY: all clean run asm emu examples custom-examples all-examples sound-examples modules test-modules test-irq
all: asm emu modules examples midi-gui

out/asm out/examples out/modules out/tools:
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

out/ppa_module_test: modules/ppa_module.c modules/ppa_sound.c modules/ppa_light.c modules/ppa_vga.c modules/ppa_ps2.c modules/ppa_module.h modules/ppa_sound.h modules/ppa_light.h modules/ppa_vga.h modules/ppa_ps2.h modules/ppa_module_test.c
	mkdir -p out
	$(CC) -Imodules modules/ppa_module.c modules/ppa_sound.c modules/ppa_light.c modules/ppa_vga.c modules/ppa_ps2.c modules/ppa_module_test.c -lm -o $@
test-modules: modules out/ppa_module_test
	./out/ppa_module_test

# PPA-SOUND/1 envelope tests (module code only, no SDL dependency).
.PHONY: test-sound-adsr
out/ppa_sound_adsr_test: modules/ppa_sound_adsr_test.c modules/ppa_sound.c modules/ppa_sound.h modules/ppa_module.h
	$(CC) -std=c11 -O2 -Wall -Wextra -Imodules \
	  modules/ppa_sound_adsr_test.c modules/ppa_sound.c -o $@ -lm

test-sound-adsr: out/ppa_sound_adsr_test
	./out/ppa_sound_adsr_test


out/irq_test: src/irq.c src/irq.h src/irq_test.c
	mkdir -p out
	$(CC) -Isrc src/irq.c src/irq_test.c -o $@

test-irq: out/irq_test
	./out/irq_test

# Category paths are preserved (e.g. examples/sound/tone.asm ->
# out/examples/sound/tone.prg -> out/examples/sound/tone.bin).
out/examples/%.prg: examples/%.asm $(ASM) $(wildcard examples/includes/*.inc)
	@mkdir -p $(dir $@)
	$(ASM) $< $@ --program
out/examples/%_demo.prg: modules/%_demo.asm $(ASM) $(wildcard examples/includes/*.inc)
	@mkdir -p $(dir $@)
	$(ASM) $< $@ --program

out/examples/%.bin: out/examples/%.prg $(ROMPACK)
	@mkdir -p $(dir $@)
	$(ROMPACK) $< $@

examples: $(EXAMPLE_BINS) $(MODULE_DEMO_BINS)
custom-examples: $(CUSTOM_EXAMPLE_BINS)
all-examples: examples custom-examples
sound-examples: $(filter out/examples/sound/%,$(EXAMPLE_BINS))

$(ROMPACK): $(ROMPACK_SRC) | out/tools
	$(CC) -O2 -std=gnu11 -Wall -Wextra $< -o $@
rompack: $(ROMPACK)

# Build demo from game/graphics ASM libraries, then insert any 320x200 BMP.
# e.g. make rom-graphics-demo BMP=~/Pictures/my_gimp_export.bmp
.PHONY: rom-graphics-demo rompack
rom-graphics-demo: out/examples/other/rom_gfx_demo.prg $(ROMPACK)
	@if [ -z "$(BMP)" ]; then \
	  echo "Usage: make rom-graphics-demo BMP=/path/to/gimp-export.bmp"; exit 1; \
	fi
	$(ROMPACK) out/examples/other/rom_gfx_demo.prg out/examples/other/rom_gfx_demo_packed.bin --bmp 0001:$(BMP)

# Keep runtime preferences and logs across clean rebuilds.
clean:
	rm -rf out/asm out/examples out/modules out/assets out/tools
	rm -f out/PPA-1_Emu out/ppa_module_test out/irq_test

.PHONY: test-rompack
test-rompack: $(ROMPACK)
	python3 tools/rompack/test_rompack.py $(ROMPACK)

# PPA-1_MIDI-to-ASM: entirely native C11 + ncursesw.
CURSES_CFLAGS := $(shell $(PKG_CONFIG) --cflags ncursesw 2>/dev/null)
CURSES_LIBS := $(shell $(PKG_CONFIG) --libs ncursesw 2>/dev/null || echo -lncursesw)
MIDI_TUI = out/tools/PPA-1_MIDI-to-ASM

$(MIDI_TUI): tools/ppa-1_midi-to-asm/main.c tools/ppa-1_midi-to-asm/midi_core.c tools/ppa-1_midi-to-asm/midi_core.h tools/ppa-1_midi-to-asm/project.c tools/ppa-1_midi-to-asm/project.h tools/ppa-1_midi-to-asm/browser.c tools/ppa-1_midi-to-asm/browser.h | out/tools
	$(CC) -std=c11 -O2 -Wall -Wextra $(CURSES_CFLAGS) \
	  tools/ppa-1_midi-to-asm/main.c tools/ppa-1_midi-to-asm/midi_core.c \
	  tools/ppa-1_midi-to-asm/project.c tools/ppa-1_midi-to-asm/browser.c \
	  -o $@ $(CURSES_LIBS) -lm

.PHONY: test-midi test-midi-native midi-gui midi-tui midi-run
out/tools/test_midi_core: tools/ppa-1_midi-to-asm/test_midi_core.c tools/ppa-1_midi-to-asm/midi_core.c tools/ppa-1_midi-to-asm/midi_core.h tools/ppa-1_midi-to-asm/project.c tools/ppa-1_midi-to-asm/project.h | out/tools
	$(CC) -std=c11 -O2 -Wall -Wextra \
	  tools/ppa-1_midi-to-asm/test_midi_core.c tools/ppa-1_midi-to-asm/midi_core.c \
	  tools/ppa-1_midi-to-asm/project.c -o $@ -lm

test-midi-native: asm rompack out/tools/test_midi_core
	./out/tools/test_midi_core

out/tools/test_midi_browser: tools/ppa-1_midi-to-asm/test_browser.c tools/ppa-1_midi-to-asm/browser.c tools/ppa-1_midi-to-asm/browser.h | out/tools
	$(CC) -std=c11 -O2 -Wall -Wextra $(CURSES_CFLAGS) \
	  tools/ppa-1_midi-to-asm/test_browser.c tools/ppa-1_midi-to-asm/browser.c \
	  -o $@ $(CURSES_LIBS)

.PHONY: test-midi-browser
test-midi-browser: out/tools/test_midi_browser
	./out/tools/test_midi_browser

test-midi: test-midi-native test-midi-browser test-sound-adsr

midi-gui midi-tui: $(MIDI_TUI)

# Explicit execution is separate from compilation.
midi-run: $(MIDI_TUI)
	./$(MIDI_TUI)
