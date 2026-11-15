# PPA-1 module examples

Build from the repository root:

```sh
make modules examples
# Programs: out/examples/*.bin
# Cards: out/modules/*.ppamod
```

## Metro driver — Ezh3-style

`metro_ezh3.asm` is a small VGA metro-driving game/example.

Install and activate these cards with CPU power OFF:

- SLOT 1: `out/modules/ppa-vga-1.ppamod`
- SLOT 2: `out/modules/ppa-ps2-1.ppamod`
- SLOT 3: `out/modules/ppa-sound-1.ppamod`

Load `out/examples/metro_ezh3.bin`, power the CPU on and RUN it. Open the VGA monitor VIEW and the PS/2 keyboard VIEW; click the keyboard window so the real keyboard is captured.

Controls:

- `W` — traction / accelerate
- `S` — brake

The demo uses VGA mode 13h (320x200x8), an old brown/black Ezh3-inspired cab desk, a speed bar and controller indicator, moving sleepers in the tunnel, and a looping route through three simple stations: DEPO, CENTRUM and LUKA. Station signs use D/C/L block letters and different stripe colors. PPA-SOUND/1 in SLOT 3 generates a traction tone whose frequency follows train speed.

A CPU clock around 1 MHz is a good starting point.

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
