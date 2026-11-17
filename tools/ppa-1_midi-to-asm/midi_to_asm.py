#!/usr/bin/env python3
"""PPA-1_MIDI-to-ASM — Standard MIDI File to PPA-SOUND/1 ASM, Python 3."""
from __future__ import annotations
import argparse
from collections import defaultdict
from dataclasses import dataclass, asdict
from fractions import Fraction
import json
from pathlib import Path
import sys

WAVES = ("square", "saw", "triangle", "sine", "noise")
POLY = ("low", "high", "latest")
DATA_START = 0x1000
MIDI_FRAMES_PER_BANK = (65536 - 2) // 14

class MidiError(ValueError):
    pass

@dataclass
class Track:
    id: str
    name: str
    notes: int

@dataclass
class Settings:
    voice: int = 0
    waveform: str = "square"
    polyphony: str = "high"
    volume: int = 200
    transpose: int = 0
    enabled: bool = False
    optimize: bool = False

def vlq(data, p, end):
    v = 0
    for _ in range(4):
        if p >= end:
            raise MidiError("Truncated MIDI variable-length integer")
        b = data[p]
        p += 1
        v = (v << 7) | (b & 127)
        if b < 128:
            return v, p
    raise MidiError("Invalid MIDI variable-length integer")

def parse_midi(path):
    data = Path(path).read_bytes()
    if len(data) < 14 or data[:4] != b"MThd":
        raise MidiError("Not an SMF MIDI file")
    hlen = int.from_bytes(data[4:8], "big")
    if hlen < 6 or 8 + hlen > len(data):
        raise MidiError("Malformed header")
    fmt = int.from_bytes(data[8:10], "big")
    n = int.from_bytes(data[10:12], "big")
    ppqn = int.from_bytes(data[12:14], "big")
    if fmt not in (0, 1) or ppqn == 0 or ppqn & 0x8000 or n == 0:
        raise MidiError("Requires MIDI format 0/1 with PPQN timing")
    p = 8 + hlen
    events, tempos, names = [], [(0, 500000, -1)], {}
    counts = defaultdict(int)
    endtick = 0
    order = 0
    for track in range(n):
        if p + 8 > len(data) or data[p:p + 4] != b"MTrk":
            raise MidiError("Missing MIDI track chunk")
        end = p + 8 + int.from_bytes(data[p + 4:p + 8], "big")
        if end > len(data):
            raise MidiError("Truncated track")
        pos, tick, running = p + 8, 0, 0
        while pos < end:
            delta, pos = vlq(data, pos, end)
            tick += delta
            if pos >= end:
                raise MidiError("Truncated event")
            byte = data[pos]
            if byte & 128:
                pos += 1
                if byte == 0xFF:
                    running = 0
                    if pos >= end:
                        raise MidiError("Truncated meta event")
                    kind = data[pos]
                    pos += 1
                    size, pos = vlq(data, pos, end)
                    if pos + size > end:
                        raise MidiError("Truncated meta payload")
                    payload = data[pos:pos + size]
                    pos += size
                    if kind == 3:
                        names[track] = payload.decode("utf-8", "replace")
                    elif kind == 0x51 and size == 3:
                        tempo = int.from_bytes(payload, "big")
                        if tempo:
                            tempos.append((tick, tempo, order))
                    if kind == 0x2F:
                        break
                    order += 1
                    continue
                if byte in (0xF0, 0xF7):
                    running = 0
                    size, pos = vlq(data, pos, end)
                    if pos + size > end:
                        raise MidiError("Truncated SysEx")
                    pos += size
                    order += 1
                    continue
                if byte >= 0xF0:
                    raise MidiError("Unsupported system MIDI event")
                running = byte
            elif running == 0:
                raise MidiError("Invalid running status")
            command, channel = running & 0xF0, running & 0x0F
            size = 1 if command in (0xC0, 0xD0) else 2
            if pos + size > end:
                raise MidiError("Truncated channel message")
            payload = data[pos:pos + size]
            if any(b & 128 for b in payload):
                raise MidiError("Malformed MIDI data byte")
            pos += size
            key = "%d:%d" % (track, channel)
            if command in (0x80, 0x90):
                note, vel = payload
                typ = "on" if command == 0x90 and vel > 0 else "off"
                events.append((tick, order, key, typ, note, vel))
                if typ == "on":
                    counts[key] += 1
            elif command == 0xB0 and payload[0] in (7, 11, 64, 120, 123):
                events.append((tick, order, key, "cc", payload[0], payload[1]))
            order += 1
        endtick = max(endtick, tick)
        p = end
    if not counts:
        raise MidiError("No notes in MIDI")
    tracks = [Track(key, "%s / channel %d" %
                    (names.get(int(key.split(":")[0]), "Track " +
                               str(int(key.split(":")[0]) + 1)),
                     int(key.split(":")[1]) + 1), count)
              for key, count in sorted(counts.items(), key=lambda x: tuple(map(int, x[0].split(":"))))]
    return tracks, sorted(events), sorted(tempos), ppqn, endtick

def defaults(tracks):
    out = {t.id: Settings() for t in tracks}
    for i, t in enumerate(sorted(tracks, key=lambda t: -t.notes)[:4]):
        out[t.id] = Settings(i + 1, WAVES[i % 4], enabled=True)
    return out

def validate(tracks, config):
    used = set()
    for t in tracks:
        s = config[t.id]
        if type(s.voice) is not int or s.voice not in range(5):
            raise MidiError("Invalid oscillator number for " + t.name)
        if not s.optimize:
            if s.voice in used and s.voice:
                raise MidiError("Two tracks share oscillator %d" % s.voice)
            used.add(s.voice)
        if type(s.enabled) is not bool or type(s.optimize) is not bool:
            raise MidiError("Invalid enabled/optimize flags for " + t.name)
        if s.waveform not in WAVES or s.polyphony not in POLY:
            raise MidiError("Invalid waveform/polyphony for " + t.name)
        if type(s.volume) is not int or not 0 <= s.volume <= 255:
            raise MidiError("Volume must be 0..255")
        if type(s.transpose) is not int or not -48 <= s.transpose <= 48:
            raise MidiError("Transpose must be -48..48")

def save_config(path, tracks, config):
    Path(path).write_text(json.dumps({
        "format": "PPA-1_MIDI-to-ASM/1",
        "optimize": any(s.optimize for s in config.values()),
        "tracks": {t.id: {"name": t.name, **asdict(config[t.id])} for t in tracks}
    }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

def load_config(path, tracks):
    raw = json.loads(Path(path).read_text(encoding="utf-8"))
    if raw.get("format") != "PPA-1_MIDI-to-ASM/1":
        raise MidiError("Invalid track configuration")
    result = defaults(tracks)
    optimize = raw.get("optimize", False)
    if type(optimize) is not bool:
        raise MidiError("optimize must be a boolean")
    for t in tracks:
        obj = raw.get("tracks", {}).get(t.id)
        if obj is not None:
            result[t.id] = Settings(**{k: obj[k] for k in Settings.__dataclass_fields__ if k in obj})
        result[t.id].optimize = optimize
        if "enabled" not in (obj or {}):
            result[t.id].enabled = result[t.id].voice > 0
    validate(tracks, result)
    return result

def freq(note):
    if not 0 <= note <= 127:
        return 0
    return max(1, min(65535, round(440 * 2 ** ((note - 69) / 12))))

def frames_from_midi(parsed, config, unit_ms=10):
    tracks, events, tempos, ppqn, endtick = parsed
    validate(tracks, config)
    if not 1 <= unit_ms <= 250:
        raise MidiError("Time unit must be 1..250 milliseconds")
    tempo_map = [(0, 500000)]
    for tick, value, _ in tempos:
        if tick == tempo_map[-1][0]:
            tempo_map[-1] = (tick, value)
        else:
            tempo_map.append((tick, value))
    offset = [Fraction(0)]
    for i in range(1, len(tempo_map)):
        offset.append(offset[-1] + Fraction(
            (tempo_map[i][0] - tempo_map[i - 1][0]) * tempo_map[i - 1][1], ppqn))
    from bisect import bisect_right
    ticks = [a for a, _ in tempo_map]
    def quantize(t):
        i = bisect_right(ticks, t) - 1
        us = offset[i] + Fraction((t - ticks[i]) * tempo_map[i][1], ppqn)
        return int(us / (unit_ms * 1000) + Fraction(1, 2))
    actions = defaultdict(list)
    active = {t.id: config[t.id] for t in tracks if config[t.id].voice}
    if not active:
        raise MidiError("No tracks assigned to oscillators")
    for tick, order, key, kind, a, b in events:
        if key in active:
            actions[quantize(tick)].append((order, key, kind, a, b))
    endtime = quantize(endtick) + max(1, (200 + unit_ms - 1) // unit_ms)
    held = defaultdict(lambda: defaultdict(list))
    sustain = defaultdict(bool)
    released = defaultdict(lambda: defaultdict(list))
    cc7 = defaultdict(lambda: 127)
    cc11 = defaultdict(lambda: 127)
    def get_state():
        state = [(0, 0)] * 4
        for key, cfg in active.items():
            voices = [(note, velocity, sequence)
                      for group in (held[key], released[key])
                      for note, playing in group.items()
                      for velocity, sequence in playing]
            if not voices:
                continue
            if cfg.polyphony == "low":
                note, vel, _ = min(voices, key=lambda a: (a[0], -a[2]))
            elif cfg.polyphony == "high":
                note, vel, _ = max(voices, key=lambda a: (a[0], a[2]))
            else:
                note, vel, _ = max(voices, key=lambda a: a[2])
            amp = round(cfg.volume * vel * cc7[key] * cc11[key] / 127**3)
            state[cfg.voice - 1] = (freq(note + cfg.transpose), amp)
        return tuple(state)
    out = []
    prev = tuple([(0, 0)] * 4)
    last = 0
    for at in sorted(set(actions) | {endtime}):
        if at > last:
            if out and out[-1][1] == prev:
                out[-1] = (out[-1][0] + at - last, prev)
            else:
                out.append((at - last, prev))
        for order, key, kind, a, b in sorted(actions.get(at, [])):
            if kind == "on":
                held[key][a].append((b, order))
            elif kind == "off":
                if held[key][a]:
                    item = held[key][a].pop(0)
                    if sustain[key]:
                        released[key][a].append(item)
            elif a == 7:
                cc7[key] = b
            elif a == 11:
                cc11[key] = b
            elif a == 64:
                sustain[key] = b >= 64
                if not sustain[key]:
                    released[key].clear()
            elif a in (120, 123):
                held[key].clear()
                released[key].clear()
        prev = get_state()
        last = at
    split = []
    for duration, state in out:
        while duration > 0:
            take = min(65534, duration)
            split.append((take, state))
            duration -= take
    return split

def optimize_frames(parsed, config, unit_ms=10):
    """Four voice time-multiplexing: preserve ongoing assignments, then
    fill free oscillators with other selected track segments. Every frame
    carries frequency, volume AND waveform for each physical oscillator.
    Track polyphony reduction, note velocities and sustain are inherited
    from the standard per-track timeline.
    """
    tracks = parsed[0]
    selected = [t for t in tracks if config[t.id].enabled]
    if not selected:
        raise MidiError("No enabled MIDI tracks")
    endpoints = defaultdict(list)
    end_time = 0
    for order, track in enumerate(selected):
        # Convert each selected track separately into a monophonic event
        # stream; use voice 1 only in this temporary logical track.
        mapping = {t.id: Settings() for t in tracks}
        original = config[track.id]
        mapping[track.id] = Settings(
            1, original.waveform, original.polyphony, original.volume,
            original.transpose, True, False)
        frames = frames_from_midi(parsed, mapping, unit_ms)
        cursor = 0
        for duration, state in frames:
            endpoints[cursor].append((track.id, state[0], order))
            cursor += duration
        endpoints[cursor].append((track.id, (0, 0), order))
        end_time = max(end_time, cursor)
    active = {}
    assigned = [None] * 4
    timeline = []
    previous = tuple((0, 0, 0) for _ in range(4))
    last = 0
    for instant in sorted(set(endpoints) | {end_time}):
        if instant > last:
            duration = instant - last
            if timeline and timeline[-1][1] == previous:
                timeline[-1] = (timeline[-1][0] + duration, previous)
            else:
                timeline.append((duration, previous))
        for name, note, order in endpoints.get(instant, []):
            active[name] = note
        # Preserve tracks that continue sounding (no unnecessary stealing).
        for i, name in enumerate(assigned):
            if name is not None and active.get(name, (0, 0))[1] <= 0:
                assigned[i] = None
        occupied = set(a for a in assigned if a is not None)
        waiting = [(name, note, next(j for j, t in enumerate(selected)
                                     if t.id == name))
                   for name, note in active.items()
                   if note[1] > 0 and name not in occupied]
        waiting.sort(key=lambda entry: (-entry[1][1], entry[2]))
        for i in range(4):
            if assigned[i] is None and waiting:
                name, _, _ = waiting.pop(0)
                assigned[i] = name
        state = []
        for name in assigned:
            if name is None:
                state.append((0, 0, 0))
            else:
                hz, volume = active[name]
                state.append((hz, volume, WAVES.index(config[name].waveform)))
        previous = tuple(state)
        last = instant
    split = []
    for duration, state in timeline:
        while duration:
            take = min(duration, 65534)
            split.append((take, state))
            duration -= take
    return split


def render(parsed, config, name, unit_ms=10, clock_hz=100000, prepared_frames=None):
    tracks = parsed[0]
    optimized = any(setting.optimize for setting in config.values())
    frames = prepared_frames
    if frames is None:
        frames = (optimize_frames(parsed, config, unit_ms)
                  if optimized else frames_from_midi(parsed, config, unit_ms))
    if not 100 <= clock_hz <= 10000000:
        raise MidiError("Clock must be 100..10000000 microsteps/s")
    # DEC D + JNZ are ~8 CPU microsteps per decrement/branch pair.
    inner = round(clock_hz * unit_ms / 8000)
    if not 1 <= inner <= 255:
        raise MidiError("Clock/tick combination exceeds 8-bit delay loop (1..255)")
    waves = [0] * 4
    for t in tracks:
        cfg = config[t.id]
        if cfg.voice and not optimized:
            waves[cfg.voice - 1] = WAVES.index(cfg.waveform)
    lines = [
        "; Generated by PPA-1_MIDI-to-ASM",
        "; Source: " + name.replace(";", "_").replace("\n", " "),
        "; %d event frames; %dms tick; %d microsteps/s approximate clock" %
        (len(frames), unit_ms, clock_hz),
        "; Data banks are embedded with .bank directly in this ASM.",
        ".org $0000",
        "    JMP midi_start",
        '.include "includes/ppa-slots.inc"',
        '.include "includes/ppa-sound.inc"',
        "",
        "midi_start:",
        "    CALL sound_init",
        "    MOV BR-HI,#10",
        "    MOV BR-LO,#00",
        "    LD A,$0061",
        "    CMP A,#A0",
        "    JZ midi_no_sound",
        "    CMP A,#00",
        "    JZ midi_no_sound",
        "    CALL slots_use_sound",
        "    MOV BR-HI,#10",
        "    MOV BR-LO,#00",
        "    MOV A,#01",
        "    ST $0064,A       ; current music ROM bank",
        "    CALL slots_use_sound",
    ]
    for i, wave in enumerate(waves):
        lines += ["    MOV A,#%02X" % wave, "    ST $%04X,A" % (0x22 + i * 16)]
    lines += [
        "    MOV Y-HI,#00", "    MOV Y-LO,#00",
        "midi_frame:",
        "    CALL midi_next",
        "    MOV X-HI,A",
        "    CALL midi_next",
        "    MOV X-LO,A",
        "    MOV A,X-HI",
        "    CMP A,#FF",
        "    JNZ midi_duration",
        "    MOV A,X-LO",
        "    CMP A,#FF",
        "    JZ midi_advance_bank",
        "midi_duration:",
        "    MOV A,X-HI",
        "    OR A,X-LO",
        "    JZ midi_done",
    ]
    for i in range(4):
        lines += [
            "    CALL midi_next", "    MOV B,A",
            "    CALL midi_next", "    MOV C,A",
            "    CALL sound_voice%d" % (i + 1),
            "    CALL midi_next", "    CALL slots_use_sound",
            "    ST $%04X,A" % (0x23 + i * 16)
        ]
        if optimized:
            lines += [
                "    CALL midi_next",
                "    CALL slots_use_sound",
                "    ST $%04X,A" % (0x22 + i * 16)
            ]
    lines += [
        "midi_wait:",
        "    MOV D,#%02X" % inner,
        "midi_inner:",
        "    DEC D",
        "    JNZ midi_inner",
        "    CMP X-LO,#00",
        "    JNZ midi_low",
        "    DEC X-HI",
        "midi_low:",
        "    DEC X-LO",
        "    MOV A,X-HI",
        "    OR A,X-LO",
        "    JNZ midi_wait",
        "    JMP midi_frame",
        "midi_advance_bank:",
        "    MOV Y-HI,#00",
        "    MOV Y-LO,#00",
        "    MOV BR-HI,#10",
        "    MOV BR-LO,#00",
        "    LD A,$0064",
        "    INC A",
        "    ST $0064,A",
        "    JMP midi_frame",
        "midi_next:",
        "    PUSH B",
        "    MOV BR-HI,#10",
        "    MOV BR-LO,#00",
        "    LD B,$0064",
        "    MOV BR-HI,#00",
        "    MOV BR-LO,B",
        "    LD A,Y",
        "    INC Y-LO",
        "    JNZ midi_next_no_carry",
        "    INC Y-HI",
        "midi_next_no_carry:",
        "    POP B",
        "    RET",
        "midi_done:",
        "    CALL sound_stop",
        "midi_no_sound:",
        "    HLT",
        "",
        "; Music frame banks start at ROM 0001.",
    ]

    # Monophonic static mode: 14 bytes/frame; optimized time-sharing:
    # 18 bytes/frame, including four dynamic waveform registers.
    # The FFFF marker explicitly resets Y and advances the ROM bank,
    # so it need not be physically located at address FFFE.
    frame_bytes = 18 if optimized else 14
    frames_per_bank = (65536 - 2) // frame_bytes
    bank_count = max(1, (len(frames) + frames_per_bank - 1) // frames_per_bank)
    if bank_count > 31:
        raise MidiError(
            "Music exceeds 31 data banks (2 MiB physical EEPROM). "
            "Increase --tick-ms, omit tracks or expand PPA-1 ROM hardware.")
    for bank in range(1, bank_count + 1):
        lines.extend(["", ".bank %04X" % bank, ".org $0000",
                      "; MIDI data bank %04X of %04X" % (bank, bank_count)])
        segment = frames[(bank - 1) * frames_per_bank:bank * frames_per_bank]
        for duration, voices in segment:
            values = [duration >> 8, duration & 255]
            for voice in voices:
                hz, volume = voice[:2]
                values.extend((hz & 255, hz >> 8, volume))
                if optimized:
                    values.append(voice[2])
            for index in range(0, len(values), 16):
                piece = values[index:index + 16]
                lines.append("    .byte " + ",".join("$%02X" % n for n in piece))
        if bank < bank_count:
            lines.append("    .byte $FF,$FF ; switch to next ROM bank")
        else:
            lines.append("    .byte $00,$00 ; song finished")
    return "\n".join(lines) + "\n"


def export_song(parsed, config, name, output_path, unit_ms=10, clock_hz=100000):
    """Write one multi-bank ASM file; assembler/ROMPACK do bank placement."""
    optimized = any(setting.optimize for setting in config.values())
    frames = optimize_frames(parsed, config, unit_ms) if optimized else frames_from_midi(parsed, config, unit_ms)
    asm = render(parsed, config, name, unit_ms, clock_hz,
                 prepared_frames=frames)
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(asm, encoding="utf-8")
    frame_size = 18 if optimized else 14
    per_bank = (65536 - 2) // frame_size
    bank_count = max(1, (len(frames) + per_bank - 1) // per_bank)
    return len(frames), bank_count


def main(argv=None):
    ap = argparse.ArgumentParser(prog="PPA-1_MIDI-to-ASM")
    ap.add_argument("midi", nargs="?", type=Path)
    ap.add_argument("-o", "--output", type=Path)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--config", type=Path)
    ap.add_argument("--write-config", type=Path)
    ap.add_argument("--gui", action="store_true")
    ap.add_argument("--clock-hz", type=int, default=100000)
    ap.add_argument("--tick-ms", type=int, default=10)
    args = ap.parse_args(argv)
    try:
        if args.gui:
            from gui import launch
            launch(args.midi, args.clock_hz, args.tick_ms)
            return 0
        if not args.midi:
            ap.error("MIDI file required (or use --gui)")
        parsed = parse_midi(args.midi)
        config = load_config(args.config, parsed[0]) if args.config else defaults(parsed[0])
        if args.list:
            for t in parsed[0]:
                c = config[t.id]
                print("%s | %s | notes=%d | voice=%d | %s %s vol=%d transpose=%+d" %
                      (t.id, t.name, t.notes, c.voice, c.waveform, c.polyphony,
                       c.volume, c.transpose))
        if args.write_config:
            save_config(args.write_config, parsed[0], config)
            print("Saved config:", args.write_config)
        if args.output:
            frame_count, bank_count = export_song(
                parsed, config, args.midi.name, args.output,
                args.tick_ms, args.clock_hz)
            print("Generated:", args.output, frame_count, "frames,",
                  bank_count, "ROM music banks")
        elif not args.list and not args.write_config:
            ap.error("Specify -o, --list, --write-config, or --gui")
    except (MidiError, OSError, ValueError, TypeError) as exc:
        print("PPA-1_MIDI-to-ASM:", exc, file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
