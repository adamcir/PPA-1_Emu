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

## Output ASM

The output is ordinary PPA-1 assembly:

- Loads the existing `includes/ppa-slots.inc` and
  `includes/ppa-sound.inc`.
- `sound_init` **finds the first active SOUND card regardless of slot**.
  If no SOUND card is found, it halts without attempting to play.
- Configures four oscillator waveforms and plays frequency/volume
  changes by writing the PPA-SOUND/1 registers.
- Uses a 14-byte event frame in ROM bank `0000` at `1000`:
  16-bit big-endian duration; 4 × (frequency low/high, volume).
- Ends on a zero-duration marker, stops the master and halts.
- Uses RAM `1000:0061` reserved by the common slot library.
- Only supports one 64KiB program bank (no cross-bank frame streaming).
  Rejects input requiring more than the available 4388 frames.
  A denser timeline may be obtained by increasing `--tick-ms` or
  disabling tracks; very large/long pieces must be shortened.

**Copy generated ASM into `examples/`**, or keep your source there.
Assembler include paths are resolved relative to the ASM file; an
arbitrary ASM file generated elsewhere should be moved to `examples/`
before building. ROMPACK builds the final EEPROM image:

    out/asm/ppa-1_asm examples/my-song.asm out/examples/my-song.prg --program
    out/tools/ppa_rompack out/examples/my-song.prg out/examples/my-song.bin

Load the image in the PPA-1 emulator. Insert and activate
`out/modules/ppa-sound-1.ppamod` in **any** PPAMOD slot, POWER ON,
RUN. The generated player automatically locates the sound card.

## Tests

    make test-midi

This runs MIDI parser/format/track selection, sustain, mono priority,
config roundtrip and ASM generation tests. External MIDI files are
not required: tests construct self-contained SMF fixtures in memory.

## Hardware considerations

The converter runs **outside the PPA-1** on a normal PC. The PPA-1
only executes the compiled ASM and uses its existing byte-wide bus,
four discrete oscillator registers, ROM/stack and bank selector.
The MIDI file itself is not interpreted by the PPA-1 CPU.
