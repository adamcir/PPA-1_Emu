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
