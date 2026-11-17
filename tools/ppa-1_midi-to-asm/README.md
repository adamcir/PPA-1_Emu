## ADSR envelopes in the MIDI editor and PPA-SOUND/1

Press **F4** on a source track or expanded PART to adjust its
independent **Attack**, **Decay**, **Sustain**, and **Release** (0..255).
An orange envelope graph previews the shape. A/D span 0..2 seconds,
R spans 0..4 seconds (curved 8-bit time mapping), and sustain is a
0..255 level. Default is `A=0 D=0 S=255 R=0`.

Each parent MIDI track has default ADSR values; a PART can override
them independently. Parent changes propagate to matching (non-custom)
PART values. PPASP projects now save these four registers per track and
per PART. Existing **PPASP version 1** projects are loaded with the
compatible full-sustain/zero-time envelope, and are saved as version 2.

The generated multi-bank ASM writes the ADSR hardware registers
`xx25..xx28` for each physical oscillator and `xx29` GATE with:

- 0 = note-off / begin release
- 1 = held note (do not retrigger)
- 2 = start or explicitly retrigger note

All MIDI notes now carry their actual start/release events to the
hardware envelope rather than faking decay by CPU changes to VOLUME.
A falling GATE preserves the last pitch/volume inside SOUND/1, so its
release stage is audible even if the new note-frame is silent. Voice
allocation respects the same four-oscillator limit as before.

Data frames now use **compressed changed-voice records**: two bytes
for duration (big endian), one byte of voice-enable change mask (bits
0..3), and the payloads for selected voices only. Each selected voice
contains frequency (low/high), volume, an optional waveform byte
(optimized mode), plus A/D/S/R/GATE registers. Records range from 3
bytes (no voice change) to 39 bytes (all four optimized voices).
Frames never cross ROM banks; re-export older songs to use the format.

**Real PPA-1 timing:** The generated ASM programs TIMER/1 (F000:FF20..FF27)
at the selected CPU microcycle clock and arms its WAIT counter before
transferring each frame to PPA-SOUND/1. The countdown runs in parallel
with CPU bus writes, unlike the former instruction-delay loop. This is
implementable using physical counters, latches and bus decode logic;
there is no hidden MIDI playback or host-timed instruction shortcut.
MIDI still needs a sufficiently fast real CPU: overloaded frames are
late rather than pretending the CPU can execute impossible instructions.
Use the same fixed emulated clock as the editor's C setting, not TURBO.

Normal emulator RUN no longer logs each CPU instruction to latest.log;
set PPA_TRACE_CPU=1 to restore full instruction tracing for debugging.

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

**F3 / O (Open)** now shows `.mid`, `.midi` and `.ppasp` files together in
one ncurses popup. Select a MIDI to start an imported song, or a PPASP
to restore its embedded MIDI, edited tracks and voice parts. **F8 / L**
is an alias for the same Open dialog. **F6** remains the ASM Save As dialog.
 The default ASM export directory is now
`~/PPA-Songs/`, next to the saved `.ppasp` projects (not `examples/`).
The ASM can still be saved into `examples/` when desired. When run
from the repository root, the PPA-1 assembler also resolves the standard
`examples/includes/` library for generated ASM files located in
`~/PPA-Songs`, so the new default export is directly assemblable.

## File browser: favorites and remembered folders

The popup follows a two-pane file-manager layout:

- **Left:** up to eight favorite filesystem directories;
  press **1–8** to jump directly to one of them.
- **Right:** Open displays MIDI and PPASP files together; ASM Save As
  displays `.asm` files. Folder entries are always shown.
- **B:** add/remove current directory from favorites.
- **H:** jump to `$HOME`; **P:** jump to `~/PPA-Songs`.
- **R:** return to the last folder remembered for this dialog type.
- **Backspace:** go to parent; **Enter:** enter a folder or open a file.
- ASM save dialog: **F2** changes the filename; **S** confirms export,
  asking first before overwriting an existing file.
- **Esc:** cancel and return to the mixer without replacing the selection.

The browser remembers three directories independently (last MIDI,
last PPASP, last ASM export), and stores the favorites between sessions
in:

```text
${XDG_CONFIG_HOME:-$HOME/.config}/ppa-1-midi-to-asm/browser.conf
```

Invalid or no-longer-present folders in this config are safely ignored.
Home and PPA-Songs are initial favorites. Opening a new dialog starts
in its own last visited directory, including after restarting the editor.
Folder changes are saved immediately so closing the dialog with Esc does
not lose the last location.

The project browser defaults to `~/PPA-Songs/`, and `F7` continues
to save PPASP files there. ASM export also defaults to that directory,
but can be pointed anywhere through the popup.

## Keyboard and mouse controls

| Key | Action |
|---|---|
| O / F3 | Open MIDI or PPASP in the same popup |
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
| F8 / L | Open the same MIDI/PPASP browser |
| F6 | Save optimized ASM with the file dialog |
| C | CPU microsteps per second |
| T | Time-unit resolution in ms |
| A | Open the graphical ncurses SAVE ASM AS dialog |
| Enter | Open SAVE ASM AS and export single multi-bank ASM |
| Esc / Q | Exit the editor (asks about unsaved PPASP changes) |

## Unsaved changes and Escape

**Esc** in the main mixer exits the application. **Esc inside a modal
dialog** cancels that dialog and returns to the editor.

The editor marks a newly imported MIDI as unsaved. Changes to track
settings, logical PART settings, inclusion/removal, waveform, volume,
polyphony, transpose, optimizer, clock/tick and project name also mark
the PPASP as modified. The project header displays a `*` when dirty.
Saving with **F7 / S** clears that marker only if the PPASP write succeeds.

When exiting with Esc (or Q), or opening another MIDI/PPASP while the
current project is modified, a green/orange ncurses dialog offers:

- **S — Save:** write the current project to `~/PPA-Songs/name.ppasp`,
  then continue exiting/opening only after successful saving.
- **D — Discard:** continue without saving the current modifications.
- **C / Esc — Cancel:** stay in the current project unchanged.

Cancelling the Open browser does not discard edits. Selecting an invalid
MIDI file is rejected before replacing the current project. **Exporting
ASM is not the same as saving the PPASP project**, so it does not clear
the unsaved-changes indicator.

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

## Track-list scrolling (compact and expanded)

The source-track viewport is a real scrolling list. Its height accounts
for the parent MIDI track, up to four expanded PART lanes and the extra
overflow information line. The selected track or PART stays visible when
navigating past the bottom of the screen; the footer and four-channel
output panel remain fixed.

- **Up/Down** navigate across tracks and their expanded PART lanes.
- **Page Up/Page Down** scroll a full viewport of source rows.
- **Home/End** jump to the first/last track.
- **Mouse wheel** scrolls the selected source rows (when over the
  source-track list).
- **Tab** expands/collapses PART rows; scrolling recalculates immediately
  after optimization and expansion changes.
- The green list header displays the currently selected track and the
  first visible track, so the scroll position is clear.

This fixes the previous behavior in which the cursor could move into
an invisible area below the source-track viewport.

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
