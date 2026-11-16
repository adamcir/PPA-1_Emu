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
            self.assertIn(".org $1000", code)
            self.assertIn("midi_frames:", code)

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
