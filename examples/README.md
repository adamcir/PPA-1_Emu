# PPA-1 module examples

Build from the repository root:

```sh
make modules examples
# Programs: out/examples/*.bin
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
  `game_delay`, `game_random8`. Keyboard uses PS/2 module in slot 2.

The corresponding resource packer is written in **C**:
`tools/rompack/ppa_rompack.c` → `out/tools/ppa_rompack`.

Compile and pack a **320×200 uncompressed BMP exported from GIMP**:

```sh
make rompack
make rom-graphics-demo BMP=/path/to/your-art.bmp
```

Load `out/examples/rom_gfx_demo_packed.bin` in the emulator, with VGA
in **slot 1** and PS/2 keyboard in **slot 2**; open the VGA VIEW.
Press **W** to reload the bitmap. Without an image, `rom_gfx_demo.bin`
displays a diagnostic solid background. The imported image lives in
**ROM bank 0001**, the CPU code in ROM bank 0000, and visible graphics
in PPA-VGA's VRAM bank 2001.

For arbitrary binaries or several independent images:

```sh
out/tools/ppa_rompack out/examples/rom_gfx_demo.bin out/custom.bin \
  --bmp 0001:/path/to/background.bmp \
  --put 0002:0100:/path/to/data.bin --info
```

The format is documented in `ppa-gfx.inc`: per-bank PIMG resource,
16-byte header, 768-byte RGB palette, pixel bytes at offset 0400h.
The converter supports 8-, 24- and uncompressed 32-bit Windows BMP;
it maps BMP color values to one shared 256-color VGA palette.

## PPA-SOUND/1 examples

For the standalone sound examples, install `out/modules/ppa-sound-1.ppamod` into SLOT 1 with CPU power OFF, click ACTIVATE, and open its VIEW window.

Programs:

- `sound_tone.asm` — single 440 Hz test note.
- `sound_chord.asm` — four-voice A minor chord.
- `sound_sweep.asm` — rising frequency sweep.
- `sound_waveforms.asm` — square, saw, triangle, sine.
- `moskau.asm` — four-voice reduction of the user-supplied Moskau MIDI, using shared driver routines.

All standalone sound programs include `includes/ppa-sound.inc`, whose callable routines are placed behind the initial `JMP start`.

Clock speed changes how quickly the CPU moves between notes. 100 kHz is only a starting point; the MIDI tempo is not calibrated precisely.
