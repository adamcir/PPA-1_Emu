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
| Delete / D | Remove source track (nondestructive) |
| R | Restore removed source track |
| Space | Toggle source track inclusion |
| W | Cycle waveform |
| P | Polyphony policy: low / high / latest |
| + / - | Track volume 0..255 |
| F / F5 or click OPTIMIZE | Allocate polyphonic notes to four channels and expand logical lanes |
| Tab | Expand / collapse the logical source-note lanes |
| E / F4 or double-click a track | Open the track configuration dialog |
| F3 | Browse for a MIDI file |
| F6 | Save optimized ASM with the file dialog |
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

## Green/orange two-panel mixer (Midnight Commander / htop style)

The interface uses green headers and controls, orange selection and
active sound, and subdued silence markers. On a wide terminal the left
side contains source MIDI tracks and the right side shows a live inspector
for the selected track. On a narrower terminal the source panel uses
the full width.

**F5** enables optimizer mode and expands each enabled source track into
logical `NOTE LANE 1` through `NOTE LANE 4`, according to its observed
maximum chord polyphony. A track with a five-note chord indicates
additional logical pitches but still uses at most four physical voices.
The lower panel displays exactly **four physical PPA-SOUND/1 channels**
over the same 64-step time preview. This makes the *expansion from source
notes* and *reduction onto the four hardware channels* visible at once.
The timeline is based on actual voice assignments from the native C
allocator, not arbitrary decorative bars.

Use **Tab** to collapse or re-expand the detailed note lanes.
Use **F4** or **E** to configure the selected MIDI source track in a
modal panel: enabled, waveform (with ASCII wave preview), volume 0-255
(with meter), note priority LOW/HIGH/LATEST and transpose -48..+48.
Change a field with arrow keys, press Enter to apply, Esc to cancel.
The output preview recomputes when a setting changes, a track is
enabled/disabled or optimizer mode is switched.

Peak chord size is measured on *all MIDI event boundaries*, including
short chords not covered by the 64-column display. The bottom channel
strips and the sampled dropped-note count summarize the actual allocation.
The sampled preview is not an audio waveform or a lossless event log;
the exported ASM still uses the full event timeline.

## Editable optimized parts and PPASP projects

When **F5 OPTIMIZE** is on, a source track can split into up to four
separate logical **PART 1–4** rows. Use arrow keys or the mouse to select
one of these rows and press **F4 / E** to configure that part independently:

- Enabled (mute/unmute the logical lane without removing its source track)
- Waveform (square, sawtooth, triangle, sine, noise)
- Volume (0–255), scaled together with the source track volume
- Transpose (-48 to +48 semitones)

The original source-track settings remain separately editable by selecting
the parent row. A part corresponds to a priority-ranked note in a chord
(LOW, HIGH, LATEST from the source track), not to a fixed hardware slot:
it follows the voice allocation when it moves to another physical channel.
The resulting ASM uses the individual part's waveform, volume and pitch,
not merely a cosmetic preview.

**Delete / D** removes the selected source track from the mix (nondestructive).
**R** restores it; **Space** toggles its inclusion. Removing a track does not
erase its original MIDI notes or its part settings.

**S / F7 = SAVE PROJECT**. This creates or updates:

```text
~/PPA-Songs/<project-name>.ppasp
```

The PPASP1 binary project file embeds the original MIDI data and retains
all source track settings, four part settings per track, optimizer state,
CPU clock and tick length. It can be copied to another computer and opened
without access to the original MIDI. The save operation writes to a
temporary file and renames it on success to avoid partially saved files.

**L / F8 = OPEN PPASP**, using a file selection window rooted at
`~/PPA-Songs`. **N** renames the project (its filename is sanitized).
**F6** is separate: export a full multi-bank `.asm` program through the
existing Save ASM As dialog. Saving a PPASP never overwrites the ASM.
The project folder is created automatically if missing.

The PPASP format is versioned with a `PPASP1\r\n` magic header, bounded
MIDI payload, recorded track/channel IDs and little-endian settings.
It is not an assembler file and should not be passed to ROMPACK.

## Four-channel optimizer

With optimization disabled, each MIDI track is monophonic:
the selected low/high/latest note is played by its dedicated oscillator.

With optimization enabled, **individual pitches inside one MIDI track
are spread across available oscillators**. A chord (C, E, G, B) in a
single track can occupy all four physical voices, rather than collapsing
to one pitch. Track polyphony preference determines the order in which
a chord's additional pitches are chosen:

- LOW favors lower pitches first;
- HIGH favors higher pitches first;
- LATEST favors the newest-started pitches first.

Assignments are tracked per (track, note), so notes keep their own
sounding intervals and remain on the same oscillator until released
(or sustain ends). To retain the character of multitrack arrangements,
the allocator first gives eligible unrepresented tracks one voice when
free slots are available, then fills remaining free slots with other
notes of the same tracks. When more than four pitches are sounding,
excess notes wait; freed oscillators are reassigned according to track
priority and audible volume.

**Every allocated oscillator also receives that note's track waveform**.
Moving a note from a piano track to an available voice can change the
waveform from a bass track's square to piano's triangle; the output
ASM contains dynamic WAVE bytes for all four voices.

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
multiple tracks, single-track chord splitting, preview channel mapping, changing waveform, long songs, multi-bank output,
and the actual assembler/ROMPACK pipeline. No Python runtime is used
by the MIDI converter or its tests.

The rest of the PPA-1 repository may still contain Python build utilities
for unrelated components such as PPAMOD card image generation.
