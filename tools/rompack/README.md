# PPA-1 ROM packer (C)

`ppa_rompack.c` is the final stage that turns a **64 KiB program file**
produced by `ppa-1_asm --program` into a **complete 2 MiB PPA-1 EEPROM image**.
Optional assets can be inserted in other ROM banks. ROMPACK must run
even when no assets are present. The output is a file to load into the
emulator; ROMPACK does not update emulated memory while the CPU runs.

Build: `make rompack`.

## BMP exported from GIMP

Create a 320×200 RGB or indexed image in GIMP. Use **Export As… → .bmp**,
select uncompressed Windows BMP (8-bit indexed or 24-bit RGB; 32-bit BI_RGB
also accepted). RLE and bitfield-compressed BMP variants are not supported.

```sh
out/tools/ppa_rompack out/main.prg out/main.bin \
  --bmp 0001:assets/cab_ecs.bmp --info
```

The example paths are for MetroDriver; the emulator example instead uses
`out/examples/rom_gfx_demo.prg` as its input **program** file.

`--bmp HEXBANK:PATH` converts a BMP up to 320×200 pixels into the **PIMG**
resource format and places it at the start of ROM bank HEXBANK. One PIMG
image uses one ROM bank (no compression).

```sh
out/tools/ppa_rompack input.prg output.bin \
  --put 0002:0100:path/to/custom.dat \
  --put 0003:0000:path/to/other.bin
```

`--put HEXBANK:HEXOFFSET:PATH` places an arbitrary file byte-for-byte into
a bank. Files **must not cross a 64 KiB bank boundary**. Overlapping
resources and writes over pre-existing non-`FF` bytes are refused. Bank
`0000` is reserved for the executable and always protected.

The recommended input is a **64 KiB program file**: bank 0000 is copied
into the final EEPROM; banks 0001–001F start as `FF` and receive optional
assets. For backwards compatibility ROMPACK also accepts a clean 2 MiB
assembler-produced ROM. The output is **always a 2 MiB image**, never a
program-only file. Do not use a previously packed image as the input.

```text
ASM source -> ppa-1_asm --program -> game.prg -> ppa_rompack -> game.bin
```

Without assets, the command is simply
`out/tools/ppa_rompack game.prg game.bin`.

## PIMG v1 bank layout

| Offset (hex) | Contents |
|---|---|
| 0000–0003 | ASCII `PIMG` |
| 0004 | Format version = 1 |
| 0005 | Pixel format = 1 (8-bit palette indices) |
| 0006–0007 | Width, little endian |
| 0008–0009 | Height, little endian |
| 000A–000B | Pixel count, little endian |
| 000C–000D | Pixel offset = 0400 |
| 000E–000F | Palette offset = 0010 |
| 0010–030F | 256 VGA RGB triples, 8 bits/channel |
| 0310–03FF | Reserved `FF` padding |
| 0400 onward | Row-major, top-to-bottom pixel data |

The palette reserves indices **0–15** for the emulator's EGA colors and
uses a 6×8×5 RGB color cube at indices **16–255**. Source images (including
8-bit indexed BMPs with their own palettes) are converted to this shared
palette. There is no transparent-color key in PIMG v1.

For a fullscreen 320×200 image, pixel data occupies 64000 bytes from
0400h through FDFFh; the bank has 512 free bytes at FE00h–FFFFh.

## ASM library

Include `ppa-gfx.inc` from the repo's `includes/` (MetroDriver) or
`examples/includes/` (emulator) directory.

- `gfx_show_screen`: A = ROM bank; verifies 320×200 PIMG, loads palette
  and image into PPA-VGA/1 (slot 1), returns A = 1 on success / 0 if absent.
- `gfx_apply_palette`: A = ROM bank; load 256 DAC entries.
- `gfx_copy_screen`: A = ROM bank; copies 64000 pixels from PIMG offset 0400.
- `gfx_draw_sprite`: A = ROM bank; Y = VGA destination linear offset;
  uses header width/height, limited to width 1..255 and height 1..200.
  **No clipping**; the caller must avoid reaching beyond the VGA frame.
  Its palette is shared with full-screen backgrounds.

A game reads pixel bytes from EEPROM banks using `BR`, then writes them to
VGA VRAM bank `2001`. Source BMP files themselves never need to be decoded
inside the emulated CPU.
