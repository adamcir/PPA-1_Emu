# PPA-1 module examples

Build from the repository root:

```sh
make modules examples
# Stage 1: assembler generates out/examples/*.prg (64 KiB program)
# Stage 2: ROMPACK generates out/examples/*.bin (2 MiB EEPROM)
# Cards: out/modules/*.ppamod
```

## MetroDriver moved to its own repository

The Ečs-inspired metro driving game formerly at `examples/metro_ecs.asm`
now lives in **[PPA-1_MetroDriver](https://github.com/adamcir/PPA-1_MetroDriver)**.
Its entry point is `main.asm` and it has a standalone `Makefile`,
PPA-VGA / PS/2 includes, and an assembler.

Build the game with `make` **in the MetroDriver repository**, then load
`out/main.bin` into this emulator. The metro game is no longer built by
`make examples` in PPA-1_Emu.

## PPA-1 ROM graphics/game libraries

New assembler includes under `examples/includes/`:

- `ppa-gfx.inc` — load **PIMG** VGA images from EEPROM ROM banks 0001–001F;
  `gfx_show_screen` copies a complete 320×200 image to VGA VRAM,
  `gfx_apply_palette` uploads its 256-color VGA palette,
  `gfx_draw_sprite` draws small images (up to 255px wide, no clipping).
- `ppa-game.inc` — `game_keyboard_init`, `game_poll_key`,
  `game_delay`, `game_random8`. Keyboard discovers the first active PS/2 module in any slot.

The corresponding resource packer is written in **C**:
`tools/rompack/ppa_rompack.c` → `out/tools/ppa_rompack`.

All examples now follow the same path:
```text
example.asm -> assembler --program -> example.prg -> ROMPACK -> example.bin
```
Even an example without additional resources passes through ROMPACK.

Compile and pack a **320×200 uncompressed BMP exported from GIMP**:

```sh
make rompack
make rom-graphics-demo BMP=/path/to/your-art.bmp
```

Load `out/examples/rom_gfx_demo_packed.bin` in the emulator, with VGA and PS/2 installed and active in **any slots**; open the VGA VIEW.
Press **W** to reload the bitmap. Without an image, `rom_gfx_demo.bin`
displays a diagnostic solid background. The imported image lives in
**ROM bank 0001**, the CPU code in ROM bank 0000, and visible graphics
in the VGA card's discovered VRAM bank (base bank + 1).

For arbitrary binaries or several independent images:

```sh
out/tools/ppa_rompack out/examples/rom_gfx_demo.prg out/custom.bin \
  --bmp 0001:/path/to/background.bmp \
  --put 0002:0100:/path/to/data.bin --info
```

The format is documented in `ppa-gfx.inc`: per-bank PIMG resource,
16-byte header, 768-byte RGB palette, pixel bytes at offset 0400h.
The converter supports 8-, 24- and uncompressed 32-bit Windows BMP;
it maps BMP color values to one shared 256-color VGA palette.

## PPA-SOUND/1 examples

For standalone sound examples, install `out/modules/ppa-sound-1.ppamod` into **any PPAMOD slot** with CPU power OFF, click ACTIVATE, and open VIEW. The library scans by device type instead of assuming bank `2000`.

Programs:

- `sound_tone.asm` — single 440 Hz test note.
- `sound_chord.asm` — four-voice A minor chord.
- `sound_sweep.asm` — rising frequency sweep.
- `sound_waveforms.asm` — square, saw, triangle, sine.
- `moskau.asm` — four-voice reduction of the user-supplied Moskau MIDI, using shared driver routines.

All standalone sound programs include `includes/ppa-slots.inc` and `includes/ppa-sound.inc`, whose callable routines are placed behind the initial `JMP start`. `sound_init` scans the bus once and saves the SOUND bank in RAM.

Clock speed changes how quickly the CPU moves between notes. 100 kHz is only a starting point; the MIDI tempo is not calibrated precisely.

## Common PPAMOD slot-scanning ASM library

`includes/ppa-slots.inc` works with active LIGHT (type 1), SOUND (2),
VGA (3) and PS/2 (4), regardless of their inserted slot.

Include **once**, directly after `JMP start` and before any device
driver includes. It provides:

- `slots_find`: A=type, B=occurrence (0=first). Returns A=slot 1–8
  or FF if absent, B=base bank HI, C=base bank LO.
  Preserves D, X, Y and the original BR.
- `slots_find16`: as above, with D=high byte of type.
- `slots_bind_sound`, `slots_bind_vga`, `slots_bind_ps2`,
  `slots_bind_light`: find occurrence zero and cache its BR high byte.
  Return A=slot or FF if missing.
- `slots_use_sound`, `slots_use_vga`, `slots_use_ps2`,
  `slots_use_light`: select the cached device's register bank.
  They preserve A/B/C/D/X/Y, modify BR and status flags.
- `slots_use_vga_vram`: select the matched VGA's VRAM bank
  (its base bank + 1).

The library uses RAM `1000:0060–0063` as four cached bank bytes:
LIGHT, SOUND, VGA, PS/2. Do not reuse these addresses in applications.
Missing or not-yet-bound cards use unmapped bank A000 to avoid writes
into EEPROM. If a card is ejected/rearranged while CPU power is off,
rerun the program's `slots_bind_*` initialization.

For example, Moskau does not care whether SOUND is in slot 1 or 8;
its existing music table stays unchanged:

```asm
.org $0000
    JMP start
.include "includes/ppa-slots.inc"
.include "includes/ppa-sound.inc"

start:
    CALL sound_init   ; discovers SOUND and caches its slot
    ; ... music plays through sound_voice1..4 ...
```

`ppamod_discover.asm` illustrates generic lookup;
`light_demo.asm`, `vga_demo.asm`, `vga_text_demo.asm`,
`ps2_demo.asm`, `rom_gfx_demo.asm` and the SOUND examples all
use the same slot discovery protocol.

IRQ-only examples do not use PPAMOD and therefore need no slot scan.

Check assembly and module tests with:

```sh
make clean
make
make test-modules
```
