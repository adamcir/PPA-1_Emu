# PPA-VGA/1 — high-resolution VGA/SVGA-style adapter

PPA-VGA/1 is a **memory-mapped** graphics adapter for PPA-1. It implements
selected IBM VGA mode numbers and extended true-color modes while preserving
PPA-1's own bus. It is **not IBM PC BIOS or I/O-port VGA**: an x86 `INT 10h`,
IBM VGA CRTC/Sequencer/GC/Attribute Controller registers, and the analog
VGA signal timings are **not** emulated.

The virtual monitor is independently resizable and opens through MODULES ->
SLOT -> VIEW, even with the processor powered off. No signal is displayed
until the software enables video (register 0010h = 01h).

## Modes

Write one of these values to MODE (register 0012h) to switch modes.
This resets VRAM to zero (text modes initialize to spaces) and resets the
aperture page and planar selectors. Unsupported mode numbers are ignored.

| Mode | Resolution | Pixel/character format | VRAM |
|---|---|---|---|
| 00h (legacy PPA) | 320 × 200 | 8-bit indexed | 64,000 B |
| 02h | 80 × 25 characters | text, character + attribute | 4,000 B |
| 03h | 80 × 25 characters | text, character + attribute | 4,000 B |
| 12h | 640 × 480 | 4-bit (16-color) **planar** | 153,600 B |
| 13h | 320 × 200 | 8-bit (256-color) indexed | 64,000 B |
| 80h (PPA extension) | 800 × 600 | 24-bit RGB888 | 1,440,000 B |
| 81h (PPA extension) | 1024 × 768 | 24-bit RGB888 | 2,359,296 B |
| 82h (PPA extension) | 1280 × 720 | 24-bit RGB888 | 2,764,800 B |
| 83h (PPA extension) | 1920 × 1080 | 24-bit RGB888 | 6,220,800 B |

24-bit modes support 2^24 = 16,777,216 distinct pixel colors. Bytes are
stored in **R,G,B** order (no alpha byte).

## Banks and paging

PPA-VGA/1 uses two banks in the selected PPAM slot:

- **slot base + 0**: 16-bit-addressed registers
- **slot base + 1**: a 65,536-byte window onto adapter VRAM

For SLOT 1: register bank **2000h**, VRAM bank **2001h**.
For SLOT 3: register bank **4000h**, VRAM bank **4001h**.

On the PPA-BUS/1, **PAGE** (001Ch/001Dh) chooses which 64 KiB physical
VRAM segment is visible through slot base+1. For packed 8-bit and RGB888
modes, a byte at physical offset `n` is addressed as:

- `page = n / 65536`
- `address = n % 65536`
- write page low/high to 001Ch/001Dh, then read/write address in VRAM bank

Invalid physical addresses read as FFh and ignore writes.

### Planar mode 12h

Four consecutive planes hold 38,400 bytes each (640*480/8).
In mode 12h, PAGE is fixed to 0 and ignored. Select the plane with
PLANE_WRITE (0020h) / PLANE_READ (0021h), value 0–3, and use a byte
offset in the range 0000h–95FFh within bank base+1.

Each bit corresponds to one pixel; most significant bit is the leftmost.
The four planes' corresponding bits form the 4-bit palette index.

### Text modes 02h / 03h

80 columns, 25 rows. Each cell is two bytes:
`VRAM[(row*80+col)*2] = ASCII character`, then
`VRAM[(row*80+col)*2+1] = attribute`.

Attribute: low nibble = foreground EGA color 0–15;
high nibble = background EGA color 0–15.
The emulator's built-in 8×8 ASCII font renders printable characters.
This is not a full IBM VGA character-ROM/code-page emulation.

## Registers (slot base + 0)

| Address | R/W | Register | Description |
|---|---|---|---|
| 0000–0003 | R | PPAM ID | 'P','P',03h,01h |
| 0010 | R/W | CONTROL | Bit 0: video enabled |
| 0011 | R | STATUS | Bit 0: ready; bit 1: video enabled |
| 0012 | R/W | MODE | Mode number, see table |
| 0013 | R/W | BORDER | Palette index for overscan border |
| 0014 | R/W | DAC_INDEX | 0–255 palette entry |
| 0015 | R/W | DAC_R | 8-bit red of selected entry |
| 0016 | R/W | DAC_G | 8-bit green |
| 0017 | R/W | DAC_B | 8-bit blue; advances DAC_INDEX |
| 0018/0019 | R | WIDTH LO/HI | Pixel width, little-endian |
| 001A/001B | R | HEIGHT LO/HI | Pixel height, little-endian |
| 001C/001D | R/W | PAGE LO/HI | 64 KiB VRAM window page, little-endian |
| 001E | R | BPP | Bits per pixel (4 in text mode) |
| 0020 | R/W | PLANE_WRITE | 12h plane write selector (0–3) |
| 0021 | R/W | PLANE_READ | 12h plane read selector (0–3) |
| 0022–0024 | R | VRAM_SIZE | Allocated video RAM bytes, low/mid/high |

Modes 02h, 03h and 12h use standard EGA-like 16-color defaults.
Modes 00h and 13h use a 256-entry programmable RGB DAC. Every entry
contains three 8-bit color channels.

## Assembly example — VGA 13h

```asm
    MOV BR-HI,#20
    MOV BR-LO,#00
    MOV A,#13
    ST $0012,A   ; MODE 13h, 320x200x256
    MOV A,#01
    ST $0010,A   ; enable display

    MOV BR-LO,#01
    MOV A,#E0
    ST $0000,A   ; write palette-index pixel in VRAM
```

**True-color mode**: set MODE=83h in the register bank; then use PAGE to
reach the packed RGB bytes. Pixel offset is `(y*1920+x)*3`.

## Implementation and hardware notes

A physical adapter would need **at least 6,220,800 bytes of VRAM** for
Full HD RGB888, video timing/scanout logic, DAC or video transmitter and
a dedicated VRAM access arbitration mechanism. A classic analog VGA
connector and an LCD monitor do not imply that a 1987 IBM VGA card can
produce these extended resolutions or 24-bit pixel formats.

At 100 kHz CPU clock, filling 6.2 MB byte by byte is extremely slow;
use TURBO for demos or add future video DMA/blitter support.
