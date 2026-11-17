"""Dependency-free regression tests for PPA-1_MIDI-to-ASM."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

HERE = Path(__file__).parent
spec = importlib.util.spec_from_file_location("midi_to_asm", HERE / "midi_to_asm.py")
mod = importlib.util.module_from_spec(spec)
import sys
sys.modules[spec.name] = mod
spec.loader.exec_module(mod)

def vlq(value):
    chunks = [value & 0x7f]
    value >>= 7
    while value:
        chunks.insert(0, (value & 0x7f) | 0x80)
        value >>= 7
    return bytes(chunks)

def mid(chunks):
    header = b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), 480)
    return header + b"".join(b"MTrk" + struct.pack(">I", len(c)) + c for c in chunks)

def chunk(name, messages):
    data = b"\0\xff\x03" + vlq(len(name)) + name
    for delta, message in messages:
        data += vlq(delta) + message
    return data + b"\0\xff\x2f\0"

class ConverterTests(unittest.TestCase):
    def sample(self, filename):
        tempo = chunk(b"tempo", [
            (0, b"\xff\x51\x03\x07\xa1\x20")])
        poly = chunk(b"piano", [
            (0, b"\x90\x3c\x7f"),  # C4
            (0, b"\x90\x40\x64"),  # E4 simultaneously
            (480, b"\x80\x40\x00"),
            (0, b"\x80\x3c\x00")])
        data = mid([tempo, poly])
        filename.write_bytes(data)
        return mod.parse_midi(filename)

    def test_parser_and_voices(self):
        with tempfile.TemporaryDirectory() as tmp:
            parsed = self.sample(Path(tmp) / "sample.mid")
            self.assertEqual(len(parsed[0]), 1)
            self.assertEqual(parsed[0][0].notes, 2)
            cfg = mod.defaults(parsed[0])
            self.assertEqual(cfg["1:0"].voice, 1)
            frames = mod.frames_from_midi(parsed, cfg)
            self.assertEqual(frames[0][1][0][0], mod.freq(64))
            self.assertGreater(frames[0][1][0][1], 0)
            cfg["1:0"].polyphony = "low"
            self.assertEqual(mod.frames_from_midi(parsed, cfg)[0][1][0][0],
                             mod.freq(60))
            cfg["1:0"].polyphony = "latest"
            self.assertEqual(mod.frames_from_midi(parsed, cfg)[0][1][0][0],
                             mod.freq(64))
            code = mod.render(parsed, cfg, "sample.mid")
            self.assertIn('.include "includes/ppa-slots.inc"', code)
            self.assertIn("CALL sound_init", code)
            self.assertIn("ST $0023,A", code)
            self.assertIn(".bank 0001", code)
            self.assertIn("; song finished", code)
            self.assertIn("ST $0064,A", code)
            self.assertIn("JZ midi_advance_bank", code)

    def test_config_roundtrip(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            parsed = self.sample(path / "sample.mid")
            settings = mod.defaults(parsed[0])
            settings["1:0"].waveform = "noise"
            settings["1:0"].volume = 75
            settings["1:0"].transpose = -12
            mod.save_config(path / "mix.json", parsed[0], settings)
            loaded = mod.load_config(path / "mix.json", parsed[0])
            self.assertEqual(loaded["1:0"], settings["1:0"])
            code = mod.render(parsed, loaded, "sample.mid")
            self.assertIn("    ST $0022,A", code)

    def test_data_crosses_rom_bank_without_sidecars(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as tmp:
            parsed = self.sample(Path(tmp) / "long.mid")
            cfg = mod.defaults(parsed[0])
            voice = ((440, 110), (0, 0), (0, 0), (0, 0))
            frames = [(10, voice)] * 4682
            with patch.object(mod, "frames_from_midi", return_value=frames):
                asm = mod.render(parsed, cfg, "long.mid")
                self.assertIn(".bank 0001", asm)
                self.assertIn(".bank 0002", asm)
                self.assertEqual(asm.count(".bank 0001"), 1)
                self.assertIn(".byte $FF,$FF ; switch to next ROM bank", asm)
                self.assertIn(".byte $00,$00 ; song finished", asm)
                out = Path(tmp) / "music.asm"
                total, banks = mod.export_song(parsed, cfg, "long.mid", out)
                self.assertEqual((total, banks), (4682, 2))
                self.assertTrue(out.exists())
                self.assertEqual(list(Path(tmp).glob("music.bank*.bin")), [])

    def test_longer_than_180_seconds(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "long.mid"
            path.write_bytes(mid([chunk(b"long", [
                (0, bytes((0x90, 0x3C, 0x64))),
                (480 * 400, bytes((0x80, 0x3C, 0x00)))])]))
            parsed = mod.parse_midi(path)
            frames = mod.frames_from_midi(parsed, mod.defaults(parsed[0]))
            self.assertGreater(sum(length for length, _ in frames) * 10, 180000)
            asm = mod.render(parsed, mod.defaults(parsed[0]), "long.mid")
            self.assertIn(".bank 0001", asm)

    def test_optimized_voice_reuse_and_waveform_changes(self):
        import json
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            first = chunk(b"lead", [
                (0, bytes((0x90, 60, 110))),
                (480, bytes((0x80, 60, 0)))])
            second = chunk(b"bass", [
                (480, bytes((0x91, 48, 127))),
                (480, bytes((0x81, 48, 0)))])
            source = p / "reassign.mid"
            source.write_bytes(mid([first, second]))
            parsed = mod.parse_midi(source)
            config = mod.defaults(parsed[0])
            config["0:0"].waveform = "triangle"
            config["1:1"].waveform = "saw"
            config["0:0"].enabled = True
            config["1:1"].enabled = True
            for setting in config.values():
                setting.optimize = True
                setting.voice = 0
            frames = mod.optimize_frames(parsed, config, 10)
            self.assertTrue(any(v[2] == 2 and v[1] > 0
                                for _, voices in frames for v in voices))
            self.assertTrue(any(v[2] == 1 and v[1] > 0
                                for _, voices in frames for v in voices))
            asm = mod.render(parsed, config, "reassign.mid", 10)
            self.assertIn("ST $0022,A", asm)
            self.assertIn(".bank 0001", asm)
            self.assertIn("midi_advance_bank:", asm)
            # 18-byte frames are emitted in two <=16-value .byte rows.
            self.assertIn(".byte $00,$00 ; song finished", asm)
            cfg = p / "mix.json"
            mod.save_config(cfg, parsed[0], config)
            loaded = mod.load_config(cfg, parsed[0])
            self.assertTrue(loaded["1:1"].optimize)
            self.assertTrue(loaded["1:1"].enabled)

    def test_invalid_midi_and_duplicate_voices(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.mid"
            path.write_bytes(b"not MIDI")
            with self.assertRaises(mod.MidiError):
                mod.parse_midi(path)
            settings = {
                "0:0": mod.Settings(1), "1:0": mod.Settings(1)
            }
            with self.assertRaises(mod.MidiError):
                mod.validate([mod.Track("0:0", "first", 1),
                              mod.Track("1:0", "second", 1)], settings)

    def test_note_off_and_sustain(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = mid([chunk(b"sustain", [
                (0, b"\x90\x3c\x64"), (240, b"\xb0\x40\x7f"),
                (240, b"\x80\x3c\x00"), (240, b"\xb0\x40\x00")])])
            path = Path(tmp) / "pedal.mid"
            path.write_bytes(data)
            parsed = mod.parse_midi(path)
            frames = mod.frames_from_midi(parsed, mod.defaults(parsed[0]))
            self.assertGreater(len(frames), 1)
            self.assertTrue(any(frame[1][0][0] > 0 for frame in frames))
            self.assertTrue(any(frame[1][0][0] == 0 for frame in frames))

if __name__ == "__main__":
    unittest.main()
