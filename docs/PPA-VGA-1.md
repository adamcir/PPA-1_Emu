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
| 01h | 40 × 25 characters | text, character + attribute | 2,000 B |
| 02h | 80 × 25 characters | text, character + attribute | 4,000 B |
| 03h | 80 × 25 characters | text, character + attribute | 4,000 B |
| 04h / 05h | 320 × 200 | 2-bit packed (4-color) | 16,000 B |
| 06h | 640 × 200 | 1-bit packed (2-color) | 16,000 B |
| 07h | 80 × 25 characters | text, character + attribute | 4,000 B |
| 0Dh | 320 × 200 | 4-bit planar (16-color) | 32,000 B |
| 0Eh | 640 × 200 | 4-bit planar (16-color) | 64,000 B |
| 0Fh | 640 × 350 | 1-bit packed (2-color) | 28,000 B |
| 10h | 640 × 350 | 4-bit planar (16-color) | 112,000 B |
| 11h | 640 × 480 | 1-bit packed (2-color) | 38,400 B |
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

### Planar modes 0Dh, 0Eh, 10h and 12h

Four consecutive planes hold `width*height/8` bytes each (38,400
bytes per plane for mode 12h).
In planar modes, PAGE is fixed to 0 and ignored. Select the plane with
PLANE_WRITE (0020h) / PLANE_READ (0021h), value 0–3, and use a byte
offset in the range 0000h–95FFh for mode 12h (other modes use the corresponding
smaller plane lengths) within bank base+1.

Each bit corresponds to one pixel; most significant bit is the leftmost.
The four planes' corresponding bits form the 4-bit palette index.

### Text modes 01h, 02h, 03h and 07h

40 columns in mode 01h, otherwise 80 columns; all use 25 rows.
Each cell is two bytes:
`VRAM[(row*columns+col)*2] = ASCII character`, then
`VRAM[(row*columns+col)*2+1] = attribute`.

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

Classic 1-bit modes store eight pixels per byte (MSB first);
2-bit modes store four pixels per byte (MSB pair first). CGA modes
04h/05h use a linear layout and a fixed four-color mapping; they do
not emulate original CGA interleaved scan-line addressing or palette
register behavior. Monochrome mode 07h is rendered using programmable
text colors, rather than precisely modeling an MDA card.

Modes 01h–12h use standard EGA-like 16-color defaults.
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

## Virtual physical monitor

Each PPA-VGA/1 slot has an independent **ADAVA DISPLAY** monitor simulation
integrated with its VIEW window. The panel is physically separate from the
emulated VGA card. Its power key, OSD, LCD/CRT selection and image processing
do not change VGA registers or VRAM.

### Front-panel controls

| Button | Operation |
|---|---|
| MENU | Open OSD; advance to next settings row; close at last row |
| - / + | Decrease/increase the selected setting, or activate a toggle/action |
| AUTO | Reset picture adjustments to nominal factory values |
| LCD/CRT | Switch display appearance, including CRT scanlines and soft scaling |
| POWER | Independently turn the virtual monitor off/on |

The power indicator is green while displaying video, blue when there is no
signal, orange while entering standby or asleep, and dark when powered off.

The OSD provides Brightness, Contrast, Saturation, Sharpness, Gamma, Display
Type, Auto Sleep, Information and Factory Reset. MENU cycles entries;
the +/- buttons adjust the highlighted entry. Selecting INFORMATION and
pressing +/- opens a live input-status screen (MENU closes it).

### Signal and standby

The monitor detects its input from the **active VGA card's video-enable
register and supported timing/mode metadata**, not from CPU execution state.
When video disappears, the monitor displays a blue **NO VIDEO SIGNAL**
message. After three seconds it shows **ENTERING SLEEP MODE...**, and after
eight seconds the display goes black and its power LED turns orange.
If the VGA card restores a signal, the monitor wakes and briefly shows
mode and resolution information.

Powering off the monitor does **not** stop the PPA-1 processor and does not
erase video memory. The module must remain installed in its slot to open VIEW.

LCD uses crisp nearest-neighbor pixels by default. CRT uses softer filtering
and a scanline overlay. Image adjustments apply to the monitor's scanout
rather than the hardware VRAM. Settings currently last for the running emulator
session, including closing and reopening the VIEW window.
