# PPA-VGA/1

PPA-VGA/1 is a color framebuffer adapter for PPA-1. Software communicates
with it only through PPA-BUS/1 memory transactions; there is no host drawing
API exposed to PPA-1 programs.

## Slot mapping

PPA-VGA/1 uses two banks inside its slot window:

- slot base + 0: control registers
- slot base + 1: VRAM

For SLOT 1 these are banks 2000 and 2001. For SLOT 2 they are 3000 and 3001.

Mode 0 is 320x200 pixels, 8-bit indexed color. VRAM is row-major and exactly
64,000 bytes: `offset = y*320+x`. Valid VRAM addresses are 0000-F9FF.

## Registers

| Address | Access | Name | Meaning |
|---|---|---|---|
| 0000 | R | ID0 | ASCII P |
| 0001 | R | ID1 | ASCII P |
| 0002 | R | TYPE | 03 |
| 0003 | R | PPAM_VER | 01 |
| 0010 | R/W | CONTROL | bit0 = video enable |
| 0011 | R | STATUS | bit0 READY, bit1 display enabled |
| 0012 | R/W | MODE | 00 = 320x200x8 |
| 0013 | R/W | BORDER | border palette index |
| 0014 | R/W | DAC_INDEX | palette entry 0..255 |
| 0015 | R/W | DAC_R | red 0..255 |
| 0016 | R/W | DAC_G | green 0..255 |
| 0017 | R/W | DAC_B | blue 0..255; write auto-increments index |
| 0018 | R | WIDTH_LO | 40 |
| 0019 | R | WIDTH_HI | 01 |
| 001A | R | HEIGHT_LO | C8 |
| 001B | R | HEIGHT_HI | 00 |

The reset palette is RGB332, but all 256 entries hold full 24-bit RGB and can
be reprogrammed through the DAC registers.

A physical implementation can preserve this exact software interface using
64 KiB SRAM, register latches, palette RAM, a pixel timing generator and an
RGB DAC or digital video encoder.
