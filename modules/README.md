## PPA-SOUND/1: per-channel ADSR envelopes

Each of the four SOUND/1 oscillators now contains an independent
sample-rate-based ADSR generator. It continues running in the host audio
mixer even when the main CPU panel or the module window is hidden.

For a channel base at 0020, 0030, 0040, or 0050, the register map is:

| Offset | Control | Values |
|---|---|---|
| +0 / +1 | Frequency | existing 16-bit value (Hz) |
| +2 | Waveform | 0 square, 1 saw, 2 triangle, 3 sine, 4 noise |
| +3 | Volume | 0..255 |
| +4 | Enable | Existing oscillator power control |
| **+5** | **Attack** | 0..255; time 0..2 seconds |
| **+6** | **Decay** | 0..255; time 0..2 seconds |
| **+7** | **Sustain** | 0..255; level 0..100% |
| **+8** | **Release** | 0..255; time 0..4 seconds |
| **+9** | **Gate** | 0 release, 1 keep held, 2 retrigger |

Times use a squared 8-bit value mapped over the given range, allowing
fine control of short attacks and decays. GATE=2 resets the envelope
without requiring a separate GATE=0 write. For release, the synth
retains the last note's volume/pitch/waveform even if the program sets
the normal frequency and volume to zero on note-off.

**Compatibility:** ADSR is bypassed until software first writes a GATE
register. Old programs using only ENABLE and VOLUME continue producing
the same steady tone as before. SOUND/1 registers remain discoverable
through the existing PPAMOD type-2 dynamic slot binding.

ASM includes now define `SOUND_CH1_ATTACK` through
`SOUND_CH4_GATE`, plus `sound_gate1..4` and
`sound_adsr_defaults`. The per-channel screen shows ADSR, gate state
and envelope level next to its waveform and frequency.

Native regression test:

```sh
make test-sound-adsr
```

# PPAMOD identity and dynamic discovery (v2)

Every card type (LIGHT/1, PPA-SOUND/1, PPA-VGA/1 and PPA-PS/2/1)
exposes a **common read-only identity ROM** in its first slot bank.
All addresses below are relative to each module slot, not absolute BR
values. A module in slot 3 uses bank `4000`, for example.

| Relative address | Read-only field | Encoding |
|---|---|---|
| `0000–0001` | Signature | ASCII `PP` |
| `0002` | Type ID low | 1 LIGHT, 2 SOUND, 3 VGA, 4 PS/2 |
| `0003` | Binary file format | 1 legacy, 2 identity-aware |
| `0004–0006` | Module version | Major, minor, patch (u8 each) |
| `0007` | Name length | ASCII byte count, 0–47 |
| `0008–0009` | Name offset | Little-endian `0x0080` |
| `000A` | Slot number | 1–8 |
| `000B–000C` | Base bank | High then low byte of the BR slot base |
| `000D` | Card flags | Bit 0 = active |
| `000E` | Type ID high | Supports future 16-bit device IDs |
| `000F` | Reserved | Zero |
| `0080–00AF` | Module name | NUL-terminated, max 47 ASCII characters |
| `00B0–00BF` | Reserved name window | Reads zero |

No writes to the identity windows reach device hardware. Device
registers stay **unchanged**, starting at `0010`; SOUND's upper
registers through `0054`, VGA register addresses, PS/2 FIFO, and VGA
bank+1 VRAM are unaffected.

The module file format v2 retains the existing 24-byte file header,
followed by a 64-byte identification record (before bytecode and data):

- `+00–+2F`: 48 bytes ASCII name (NUL-padded);
- `+30–+32`: module version major, minor, patch;
- `+33–+3F`: reserved zero bytes.

`version` in a module's JSON is the **PPAMOD file format version**.
`module_version: [major,minor,patch]` is the card's own software/hardware
revision. Every built-in JSON definition now uses file format 2 and
contains a distinct `name` and `module_version`.
Old v1 PPAMOD binaries are still loadable; the emulator supplies
the original legacy name and version 1.0.0 in their read-only ID window.

## Automatic discovery by type

The memory-mapped system controller at **BR=F000** provides:

| Address | R/W | Purpose |
|---|---|---|
| `FF30` | R/W | Desired type ID low |
| `FF31` | R/W | Desired type ID high |
| `FF32` | R/W | Match occurrence (0 = first, 1 = second…) |
| `FF33` | R | Matched slot base bank HI (`FF` if absent) |
| `FF34` | R | Matched slot base bank LO (`FF` if absent) |
| `FF35` | R | Matched slot number 1–8 (`FF` if absent) |
| `FF36` | R | Count of installed and activated cards of this type |

Lookup inspects all eight slots in ascending slot order. Only
**installed AND active** cards are visible to the CPU. There may be
multiple cards of the same type: set `FF32` to 0, 1, 2, etc.
The discovery controller has two 8-bit type latches and an occurrence
latch, and can be built as an eight-slot sequential scan without
needing a general-purpose processor. It remains separate from the
IRQ controller and preserves the system I/O bank mapping.

A runnable demo is `examples/ppamod_discover.asm`: it finds any active
SOUND/1 module, reads its ID ROM and enables the sound master without
ever assuming a fixed bank. See `make test-modules` for tests.

---

# Layout of module sources and compiled cards

## Module windows and background audio

Module VIEW windows now reopen at native size (540×510 for LIGHT,
SOUND and PS/2; 1280×800 for VGA monitor), preserving their position.
The SDL logical coordinate system still scales smoothly, so all windows
can be resized smaller; minimums are 280×260 (module detail) and
480×300 (VGA). Reopening resets an oversized window to its native size.

PPA-SOUND/1 **does not depend on the panel or module window being visible**.
Host audio is queued from the continuously serviced emulator loop, which
is independent of panel presentation; rendering is capped at approximately
30 fps and minimized/hidden windows are not redrawn. The audio stream
is still cleared when processor power goes OFF. This preserves hardware
CPU/software-clock semantics while preventing blocked vsync or a hidden
SDL window from starving the audio stream.

## Independent module user interfaces

The emulator owns only SDL windows and dispatches each module's render
callback. All device-specific drawing lives inside `modules/`:

- `ppa_sound.c/.h`: sound registers, oscillators and 16-bit audio mixer.
- `ppa_sound_gui.c/.h`: PPA-SOUND/1 VST-like panel, oscilloscope,
  speaker and per-voice status.
- `ppa_light.c/.h`: LIGHT register and pulse implementation.
- `ppa_light_gui.c/.h`: LIGHT indicator face.
- `ppa_module_ui.c/.h`: thin device-type UI dispatcher.
- `ppa_module_draw.c/.h`: shared SDL drawing primitives.

No individual card UI is drawn inside the emulator's `src/gui.c`.
The module protocol remains the safe PPAMOD v1 device model, **not** a
native VST plug-in loader.

### Saved module rack

`out/config.ini` automatically stores slots 1-8, device paths, active
states, window visibility and each module window's x/y/width/height.
The main emulator window's position and size are also retained.
Installed cards and their windows are restored at the next startup,
provided their `.ppamod` files still exist. Missing files are logged
without stopping startup. `make clean` deliberately preserves
`out/config.ini` and `out/latest.log`.


The `modules/` directory contains device host code (`ppa_module.c`,
`ppa_module.h`), unit tests, device definitions (`light.json` and
`ppa-sound-1.json`), and the `build_modules.py` compiler.

Run `make modules` to generate `out/modules/*.ppamod`. The generated
binary cards live only under `out/modules/` and are ignored by Git.
`make` builds the emulator, module cards and assembly examples.
The emulator sources are in `src/`; the assembler is in `tools/asm/`.

# PPA-1 PPAMOD modules

`out/modules/light.ppamod` is the first binary PPA-BUS/1 card.
The first four **file** bytes are ASCII `PPAM` (50 50 41 4D); the **bus ID** begins with `PP` and includes the device type, name, and revision.

## Loading

In the emulator's MODULES menu, select a slot, LOAD a `.ppamod`
file and ACTIVATE it while the PPA-1 power switch is OFF.
Use VIEW to open the card's optional hardware window.

Slot N occupies banks `(0x2000 + (N-1)*0x1000)` through
`(base + 0x0FFF)`. For LIGHT the register map, relative to slot base:

- `0000:0000`: ASCII 'P', read only
- `0000:0001`: ASCII 'P', read only
- `0000:0002`: type ID = 1 (LIGHT), read only
- `0000:0003`: PPAM version = 1
- `0000:0010`: LED state, 0/1, read only
- `0000:0011-0012`: remaining emulated cycles (low 16 bits)
- `0000:0010`: WRITE nonzero = light up for the configured duration;
  WRITE zero = turn off immediately.

Slot 1 example in PPA-1 ASM:

```asm
MOV BR-HI,#20
MOV BR-LO,#00
MOV A,#01
ST $0010,A
```

## PPAMOD legacy file format version 1

All integers little-endian. Fixed 24-byte header:

| Offset | Bytes | Meaning |
|---|---:|---|
| 0 | 4 | `PPAM` magic |
| 4 | 2 | version (1) |
| 6 | 2 | type (1 = LIGHT) |
| 8 | 4 | bytecode length, multiple of 8, <=65536 |
| 12 | 4 | card data length (may exceed 2 MiB) |
| 16 | 4 | default LED pulse duration, emulated CPU microcycles |
| 20 | 4 | reserved, must be zero |

After the header: program bytecode, then optional card data (up to
256 MiB minus 64 KiB). Data are mapped read-only to slot bank + 1
onwards. A module file is NOT restricted by the 2 MiB RAM/EEPROM
limits. Data and bytecode are bounded by the slot's 256 MiB window.

Bytecode is a list of eight-byte rules:
`event:u8, action:u8, offset:u32, duration:u16`.
For v1: `event=1` means bus WRITE, `action=1` means pulse LIGHT
when a nonzero byte is written to the rule's relative register
offset; zero clears it. Duration 0 uses the header default. This
logic is interpreted by the emulator, NOT loaded as host machine code.
Additional module types and opcodes can be added with versioned rules.

Eject / activation requires POWER OFF; RESET clears LED state while
keeping cards attached. Modules work with GUI CPU and console CPU.

## Command-line use

```sh
./out/PPA-1_Emu demo.bin --module 1:out/modules/light.ppamod
```

The module is activated on startup. GUI loads are initially inactive,
and must be activated using the MODULES window while power is off.
The unit test is `make test-modules`.

## Demo

`modules/light_demo.asm` sets BR to the base bank for SLOT 1,
writes 01 to register 0010, waits and repeats. Assemble with
`make examples` (output: `out/examples/light_demo.bin`).

## PPA-SOUND/1

`out/modules/ppa-sound-1.ppamod` has a PPAM header with device type 2.
It uses the PPAMOD host's safe built-in synthesizer (not native VST
or arbitrary executable code). Loading it into SLOT 1 maps:

| Offset | Meaning |
|---|---|
| `0010` | MASTER enable (bit 0) |
| `0011` | MASTER volume 0..255 |
| `0020` | Voice 1 frequency low byte |
| `0021` | Voice 1 frequency high byte |
| `0022` | Voice 1 waveform: 0 square, 1 saw, 2 triangle, 3 sine, 4 noise |
| `0023` | Voice 1 volume 0..255 |
| `0024` | Voice 1 enable (bit 0) |

Voice 2 is at 0030-0034, voice 3 at 0040-0044 and
voice 4 at 0050-0054. All offsets are relative to the slot base
bank (2000 for SLOT 1, 3000 for SLOT 2, etc).

PPA-SOUND/1 has a separate VST-like SDL window with a virtual
speaker, 64x24 pixel waveform monitor, and per-channel readouts.
A single SDL audio stream mixes all active SOUND cards to 44.1 kHz
16-bit mono PCM. The sound output depends on the CPU-written
registers; audio sample generation is independent of host-frame
rendering and from the emulated CPU clock.

This is an internal PPAM host UI/audio API, not a real VST
plugin or a binary VST loader. LIGHT uses the same separate
module-host window mechanism. Module insert/eject/activation,
EEPROM reload and memory sizing are disabled visibly when
processor POWER is ON.
