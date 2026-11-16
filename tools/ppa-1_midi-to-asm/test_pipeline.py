"""Integration test: multi-bank ASM -> multi-bank PRG -> ROMPACK 2 MiB BIN.

Run with 'make test-midi' so C assembler and ROMPACK are built first.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import midi_to_asm as conv


class MultiBankPipelineTests(unittest.TestCase):
    def test_assembler_and_rompack_preserve_midi_banks(self):
        assembler = ROOT / "out/asm/ppa-1_asm"
        rompack = ROOT / "out/tools/ppa_rompack"
        if not assembler.is_file() or not rompack.is_file():
            self.skipTest("Build tools first: make asm rompack")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "includes").mkdir()
            for name in ("ppa-slots.inc", "ppa-sound.inc"):
                shutil.copy2(ROOT / "examples/includes" / name,
                             root / "includes" / name)
            src, prg, rom = root / "music.asm", root / "music.prg", root / "music.bin"
            voice = ((440, 180), (0, 0), (0, 0), (0, 0))
            frames = [(4, voice)] * (conv.MIDI_FRAMES_PER_BANK + 1)
            tracks = [conv.Track("0:0", "test", 1)]
            config = {"0:0": conv.Settings(voice=1, waveform="sine")}
            parsed = (tracks, [], [(0, 500000, -1)], 480, 0)
            with patch.object(conv, "frames_from_midi", return_value=frames):
                count, banks = conv.export_song(parsed, config, "test.mid", src)
            self.assertEqual((count, banks), (conv.MIDI_FRAMES_PER_BANK + 1, 2))
            subprocess.run([str(assembler), str(src), str(prg), "--program"],
                           check=True, capture_output=True, text=True)
            self.assertEqual(prg.stat().st_size, 3 * 65536)
            content = prg.read_bytes()
            self.assertEqual(content[65536:65538], bytes((0, 4)))
            self.assertEqual(content[2 * 65536 - 2:2 * 65536], bytes((255, 255)))
            self.assertEqual(content[2 * 65536:2 * 65536 + 2], bytes((0, 4)))
            self.assertIn(bytes((0, 0)), content[2 * 65536:])
            subprocess.run([str(rompack), str(prg), str(rom)],
                           check=True, capture_output=True, text=True)
            self.assertEqual(rom.stat().st_size, 2 * 1024 * 1024)
            self.assertEqual(rom.read_bytes()[:3 * 65536], content)


if __name__ == "__main__":
    unittest.main()
