# PPA-1 module examples

Build from the repository root:

```sh
make modules examples
# Programs: out/examples/*.bin
# Cards: out/modules/*.ppamod
```

## Metro driver — Ečs cab (PPA-1 ASM)

`metro_ecs.asm` is a playable metro-driving example with a pale, instrument-rich
Ečs-inspired control desk. Everything is drawn into PPA-VGA VRAM by the PPA-1
CPU; no host-rendered images or external bitmaps are used.

Install and activate these cards with CPU power OFF:

- **SLOT 1:** `out/modules/ppa-vga-1.ppamod` — VGA graphics in mode 13h.
- **SLOT 2:** `out/modules/ppa-ps2-1.ppamod` — PS/2 Set-2 keyboard input.
- **SLOT 3:** `out/modules/ppa-sound-1.ppamod` — traction motor tone.

After `make modules examples`, load `out/examples/metro_ecs.bin` as the CPU's
EEPROM image, power on, and RUN. Open the VGA monitor's VIEW and click the
keyboard module's VIEW so it captures physical keyboard events.

Controls:
- **W (hold)** — traction / acceleration.
- **S (hold)** — brake / deceleration.
- **Neither** — coast with gentle rolling resistance.

The 320x200x8-bit display includes a twin-tiered gray Ečs-like desk with analog
voltage/current/speed/pressure instruments, ARS lamps, brake-valve area, colored
lighting/doors/signal buttons, wood trim, a large moving brown controller handle,
a live speed bar, and VGA pixel-font labels. The outside view has track and tunnel
perspective, moving sleepers and three simple stations, **DEPO**, **CENTRUM** and
**LUKA**, on a repeating route.

Start at around **1 MHz CPU** and adjust for your emulator's rendering speed.

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
