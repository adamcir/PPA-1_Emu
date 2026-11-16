# PPA-1_MIDI-to-ASM

Portable **Python 3 (standard library only)** conversion utility for PPA-SOUND/1.
Includes a Tkinter graphical MIDI-track mixer and a headless JSON/CLI workflow.
No pip packages, pygame, mido, fluidsynth, or third-party dependencies.

## Open GUI

From repository root:

    make midi-gui

Alternatively:

    python3 tools/PPA-1_MIDI-to-ASM.py --gui
    python3 tools/PPA-1_MIDI-to-ASM.py --gui path/to/song.mid

If tkinter is missing on Debian/Ubuntu, install the distro package
`python3-tk`. The CLI works without Tkinter or a graphical display.

In the editor, open the MIDI file. Each MIDI file track/channel is
shown as a separate row. Set:

- **Voice**: 0 = omit, 1..4 = one of PPA-SOUND/1's four oscillators.
  Two tracks cannot drive the same oscillator.
- **Waveform**: square (0), saw (1), triangle (2), sine (3), noise (4).
- **Polyphony**: `low` = lowest active MIDI pitch, `high` = highest,
  `latest` = most recently started note. This is one note per track;
  the original chords cannot play fully on one oscillator.
- **Volume**: 0..255, scaled by MIDI note velocity, CC7 and CC11.
- **Transpose**: -48..48 semitones.

By default the four track/channel streams with the most note-on events
are assigned voices 1..4; all others are disabled. MIDI channel 10
(drum kit) is treated as pitched notes unless you disable it manually.
The tool supports overlapping notes of the same pitch and sustain
pedal CC64. Standard MIDI Format 0/1 and PPQN time division are
supported; SMPTE division and independent format 2 are rejected.

## Command line

    python3 tools/PPA-1_MIDI-to-ASM.py my-song.mid --list
    python3 tools/PPA-1_MIDI-to-ASM.py my-song.mid --write-config song-mix.json
    # Edit track mapping, waveforms, volumes, polyphony in song-mix.json:
    python3 tools/PPA-1_MIDI-to-ASM.py my-song.mid \
        --config song-mix.json -o examples/my-song.asm
    make out/examples/my-song.bin

You can also specify `--clock-hz 100000 --tick-ms 10`.
Clock is in **emulator microsteps per second** (not notes/s).
The timing is approximate: an ASM busy-wait generates delays using
`DEC D` and `JNZ`, while the note-update code introduces extra work.
Set the actual PPA-1 CPU clock to match `--clock-hz`; adjust the
setting until the tempo sounds correct. No polyphonic MIDI renderer,
DAC, MIDI synthesizer, or hardware MIDI interface is required.

## Long MIDI: one ASM, multi-bank PRG, ROMPACK

There is **no 180-second time limit**. The MIDI converter emits **one**
`.asm` source with `.bank 0001`, `.bank 0002`, etc. and embedded
`.byte` music frames. It does **not** write companion data files.

Pipeline:

```text
MIDI + per-track JSON
       |
       v
aria_math.asm          (one source containing code and .bank data)
       |
       v  PPA-1 assembler --program
aria_math.prg          (1 to 32 whole 64 KiB banks, contiguous)
       |
       v  ROMPACK
aria_math.bin          (2 MiB EEPROM; bank placement already correct)
```

Each music frame occupies 14 bytes: duration HI/LO plus four triples of
frequency LO/HI and volume. A music data bank contains at most 4681 frames,
followed by the 2-byte **FFFF** marker if another bank follows or **0000**
at the end of the song. The ASM code remains entirely in bank `0000`.
A data-reading routine uses `BR` to select the current ROM data bank,
increments `Y` after every byte, and advances the bank stored in
RAM `1000:0064` when `Y` rolls over from `FFFF` to `0000`.
On the next `FFFF` marker it loops to read the next frame. This is
not an instruction `JMP` across banks: PPA-1 can only fetch executable
instructions from ROM bank `0000`, while `BR` selects the data bank.
This is realizable on the existing PPA-1 hardware.

The present EEPROM has 32 banks total: one code bank and 31 data banks.
There is therefore a **physical storage capacity**, but no artificial
song-duration limit. Large files that exceed 31 data banks produce a
clear error instead of silently truncating music.

### Conversion

Run in the repository root:

```sh
python3 tools/PPA-1_MIDI-to-ASM.py /path/to/aria_math.mid \
  --config /path/to/aria_math.json -o examples/aria_math.asm
make out/examples/aria_math.bin
```

Expected intermediate files:

```text
examples/aria_math.asm
out/examples/aria_math.prg
out/examples/aria_math.bin
```

The generated ASM includes `includes/ppa-slots.inc` and
`includes/ppa-sound.inc`, so keep the ASM within `examples/` or
provide those includes relative to its location. Place
`ppa-sound-1.ppamod` in **any active module slot**. The player uses the
existing PPAMOD discovery library.

No `--put` arguments or bank-splitting scripts are needed for the music.
Existing single-bank ASM examples still build as a 64 KiB PRG.
The assembler and ROMPACK accept a multi-bank PRG automatically.

The musical timing remains approximate because it uses CPU busy loops.
Tune `--clock-hz` and `--tick-ms` for the intended emulation speed.

## Tests

    make test-midi

This runs MIDI parser/format/track selection, sustain, mono priority,
config roundtrip, long-song/bank-boundary and ASM generation tests. External MIDI files are
not required: tests construct self-contained SMF fixtures in memory.

## Hardware considerations

The converter runs **outside the PPA-1** on a normal PC. The PPA-1
only executes the compiled ASM and uses its existing byte-wide bus,
four discrete oscillator registers, ROM/stack and bank selector.
The MIDI file itself is not interpreted by the PPA-1 CPU.
