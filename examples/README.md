# PPA-SOUND/1 examples

Install `out/modules/ppa-sound-1.ppamod` into SLOT 1 with CPU power OFF, click ACTIVATE, and open its VIEW window.

Programs:
- `sound_tone.asm` — single 440 Hz test note.
- `sound_chord.asm` — four-voice A minor chord.
- `sound_sweep.asm` — rising frequency sweep.
- `sound_waveforms.asm` — square, saw, triangle, sine.
- `moskau.asm` — four-voice reduction of the user-supplied Moskau MIDI, using shared driver routines.

All sound programs include `includes/ppa-sound.inc`, whose callable routines are placed behind the initial `JMP start`.

Build from repo root:

```sh
make modules examples
# Programs: out/examples/*.bin
# Cards: out/modules/*.ppamod
```

Clock speed changes how quickly the CPU moves between notes. 100 kHz is only a starting point; the MIDI tempo is not calibrated precisely.
