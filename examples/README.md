# PPA-1 module examples

Build from the repository root:

```sh
make modules examples
# Programs: out/examples/*.bin
# Cards: out/modules/*.ppamod
```

## Metro driver — Ečs cab (PPA-1 ASM)

`metro_ecs.asm` is a playable VGA metro driving example. The display is drawn
entirely in PPA-1 assembly (no PNGs or host-side game rendering). The cab is
visually inspired by the older **Ečs** train.

With CPU power OFF, install and activate:

- **SLOT 1:** `out/modules/ppa-vga-1.ppamod` — VGA mode 13h (320×200, 8-bit).
- **SLOT 2:** `out/modules/ppa-ps2-1.ppamod` — PS/2 keyboard, scan-set 2.
- **SLOT 3:** `out/modules/ppa-sound-1.ppamod` — traction motor sound.

Build at repository root:

```sh
git pull origin main
make modules examples
```

Load `out/examples/metro_ecs.bin` as the EEPROM image. Start the CPU with
PWR ON + RUN, open VGA VIEW and click the keyboard module's VIEW to capture
physical keyboard input. Around 1 MHz CPU is a useful starting point.

### Controls and controller notches

**W** (press and release) — advance **one** KV notch.

**S** (press and release) — move back **one** KV notch.

The seven notches, following the Metrostroi **KV-35** controller logic:

```text
T2 <-> T1A <-> T1 <-> 0 <-> X1 <-> X2 <-> X3
```

- **X1 / X2 / X3:** progressively faster *gradual* acceleration.
- **0:** coasting with rolling resistance.
- **T1 / T1A / T2:** progressively stronger *gradual* braking.

Notches remain selected after releasing W/S. The physical handle responds with
a small delay to rapid notch changes. The screen shows the selected physical
notch, the real changing speed (0–80 km/h), and indicator lamps.

### Route, movement and signals

The same three fictional stations, **DEPO → CENTRUM → LUKA**, are on a repeating
track. The cab viewport is a first-person view of rails and tunnel. Sleepers and
tunnel support ribs shift with actual distance travelled; station platform
details also scroll. Everything stops moving when train speed reaches zero.

Signal heads change apparent size/position as the train approaches and passes
them. Demonstration ARS sections show **80/70/60/40/0 km/h** codes, corresponding
colored lamps, a red warning indicator, and a pulsed audio warning on the\nsecond SOUND channel when speed exceeds the current code.
The graphical cab includes analog instruments, a KV controller handle, buttons,
a speed display, and text drawn by the game's own tiny VGA pixel font.

The **train physics, scripted line layout, signal positions and ARS warnings are
simplified game mechanics**, not Metrostroi's actual electrical simulation,
pneumatic model, EPK enforcement, or real route/interlocking logic.

### Metrostroi references

The behavior is based on the control positions in
[Metrostroi KV-35](https://github.com/metrostroi-repo/MetrostroiAddon/blob/dev/lua/metrostroi/systems/sys_kv_35.lua)
and the idea of ARS codes/track signals from
[Metrostroi track signals](https://github.com/metrostroi-repo/MetrostroiAddon/blob/dev/lua/entities/gmod_track_signal/init.lua)
and
[rail network](https://github.com/metrostroi-repo/MetrostroiAddon/blob/dev/lua/metrostroi/sv_railnetwork.lua).
These are references for a new independent ASM implementation; original Lua
program code is not included.

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
