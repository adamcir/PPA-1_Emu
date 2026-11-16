#!/usr/bin/env python3
"""PPA-1_MIDI-to-ASM launcher (CLI and optional Tkinter editor)."""
from pathlib import Path
import runpy
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / "ppa-1_midi-to-asm"))
runpy.run_path(
    str(Path(__file__).resolve().parent / "ppa-1_midi-to-asm" / "midi_to_asm.py"),
    run_name="__main__",
)
