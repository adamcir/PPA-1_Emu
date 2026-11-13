# PPA-SOUND/1 examples

Install `modules/ppa-sound-1.ppamod` into SLOT 1 with CPU power OFF, click ACTIVATE, and open its VIEW window.

Programs:
- `sound_tone.asm` — single 440 Hz test note.
- `sound_chord.asm` — four-voice A minor chord.
- `sound_sweep.asm` — rising frequency sweep.
- `sound_waveforms.asm` — square, saw, triangle, sine.
- `moskau.asm` — four-voice reduction of the user-supplied Moskau MIDI, using shared driver routines.

All sound programs include `includes/ppa-sound.inc`, whose callable routines are placed behind the initial `JMP start`.

Build from repo root:

```sh
make asm
out/asm/ppa-1_asm examples/sound_tone.asm out/sound_tone.bin
out/asm/ppa-1_asm examples/sound_chord.asm out/sound_chord.bin
out/asm/ppa-1_asm examples/sound_sweep.asm out/sound_sweep.bin
out/asm/ppa-1_asm examples/sound_waveforms.asm out/sound_waveforms.bin
out/asm/ppa-1_asm examples/moskau.asm out/moskau.bin
```

Clock speed changes how quickly the CPU moves between notes. 100 kHz is only a starting point; the MIDI tempo is not calibrated precisely.
