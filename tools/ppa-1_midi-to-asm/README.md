# PPA-1_MIDI-to-ASM

Native **C11 + ncursesw** MIDI editor and converter for PPA-SOUND/1.
The interactive terminal application uses **no Python**: MIDI parsing,
tempo conversion, track editing, four-channel optimization and multi-bank
ASM generation are in midi_core.c and main.c.

The obsolete Python MIDI converter and Tkinter GUI have been removed.
The MIDI tool is now entirely implemented in C.

## Build and run

Debian/Ubuntu:

    sudo apt install build-essential libncurses-dev
    cd ~/Dokumenty/Projekty/C-C++/PPA-1_Emu
    make all       # builds emulator, modules, examples AND MIDI editor
    make midi-gui  # builds MIDI editor only, DOES NOT run it

Run separately:

    ./out/tools/PPA-1_MIDI-to-ASM
    ./out/tools/PPA-1_MIDI-to-ASM /path/to/song.mid

Or run using:

    make midi-run

The file browser navigates directories and shows .mid/.midi files.
By default the ASM output goes to the repository examples directory.

## Keyboard and mouse controls

| Key | Action |
|---|---|
| O | Browse MIDI files |
| Up / Down | Select track |
| Space | Remove/restore MIDI track |
| W | Cycle waveform |
| P | Polyphony policy: low / high / latest |
| + / - | Track volume 0..255 |
| F or click OPTIMIZE 4 CHANNELS | Enable dynamic four-voice allocation |
| C | CPU microsteps per second |
| T | Time-unit resolution in ms |
| A | Open the graphical ncurses SAVE ASM AS dialog |
| Enter | Open SAVE ASM AS and export single multi-bank ASM |
| Q | Quit |

The export dialog browses directories, shows existing ASM files, edits the
filename with F2, chooses the directory with Enter, and confirms the
destination with S. Existing files require confirmation before overwrite.
Esc cancels without writing. The dialog is entirely implemented in C,
using ncurses windows.

Every MIDI track/channel starts enabled. Remove unwanted tracks, then
activate OPTIMIZE when more than four remain. Without optimization, only
four enabled tracks are permitted.

## Waveforms and symbols

- Square: |- | _ | - | _ (actual compact display: |-|_|-|_)
- Sawtooth: /|/|/|/|
- Triangle: /\/\/\/\
- Sine: ~^~^~^~^
- Noise: .:*?^*.:

Waveform register codes are 0 square, 1 saw, 2 triangle, 3 sine, 4 noise.

## Four-channel optimizer

Each MIDI track uses a monophonic priority policy (lowest, highest or
newest note). Up to four streams play simultaneously. Ongoing notes keep
their assigned oscillator; idle oscillators are filled with waiting
tracks, ordered by effective volume and track position. When an
oscillator begins to play another track, its **waveform changes too**.
This is achieved by encoding an extra WAVE byte for every voice update.

The native MIDI parser supports SMF format 0/1 with PPQN, running status,
tempo changes, note-on/off, velocity, CC7/CC11 volume and expression,
CC64 sustain, and all-sound-off. MIDI tracks beyond four simultaneous
notes may be omitted while all oscillators are occupied.

## Output and bank layout

One input MIDI produces exactly one ASM file, such as:

    examples/aria_math.asm

The code section occupies ROM bank 0000; the music data follow in
.bank 0001, .bank 0002, etc. A duration marker FF FF switches the
data reader into the next bank; 00 00 stops playback. The CPU continues
fetching instructions from ROM bank 0000 and uses BR for music data.
The SOUND module is discovered by hardware type in any PPAMOD slot.

Without optimization frames take 14 bytes each. With optimization they
take 18 bytes each including four dynamic WAVE values. A large-duration
event is split into multiple frames. No artificial 180-second limit
exists; the real 2 MiB EEPROM capacity is enforced.

Build the result:

    make out/examples/aria_math.bin

This yields:

    ASM (multi-bank) -> PPA-1 assembler -> PRG (multi-bank)
    -> ROMPACK -> out/examples/aria_math.bin (2 MiB EEPROM)

There are no separate bank binary sidecar files.

## Tests

    make out/tools/PPA-1_MIDI-to-ASM
    make test-midi-native
    make test-midi

The native tests use C-generated SMF MIDI fixtures. They cover
multiple tracks, changing waveform, long songs, multi-bank output,
and the actual assembler/ROMPACK pipeline. No Python runtime is used
by the MIDI converter or its tests.

The rest of the PPA-1 repository may still contain Python build utilities
for unrelated components such as PPAMOD card image generation.
