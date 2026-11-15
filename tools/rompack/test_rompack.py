#!/usr/bin/env python3
"""Smoke-test the native C PPA-1 EEPROM packer without external libraries."""
import os
import pathlib
import struct
import subprocess
import sys
import tempfile

BANK_SIZE = 65536
ROM_SIZE = 32 * BANK_SIZE


def make_bmp24(path: pathlib.Path, width: int, height: int) -> None:
    row_bytes = ((width * 3 + 3) // 4) * 4
    pixels = bytearray()
    for y in range(height - 1, -1, -1):  # Windows bottom-up rows
        row = bytearray()
        for x in range(width):
            red = (x * 255) // max(width - 1, 1)
            green = (y * 255) // max(height - 1, 1)
            blue = 127
            row += bytes((blue, green, red))
        row += bytes(row_bytes - width * 3)
        pixels += row
    head = struct.pack(
        "<2sIHHI IiiHHIIiiII",
        b"BM", 54 + len(pixels), 0, 0, 54,
        40, width, height, 1, 24, 0, len(pixels),
        2835, 2835, 0, 0,
    )
    path.write_bytes(head + pixels)


def run(argv: list[str], expected_success: bool = True) -> subprocess.CompletedProcess:
    proc = subprocess.run(argv, capture_output=True, text=True)
    if (proc.returncode == 0) != expected_success:
        raise AssertionError(
            f"Unexpected exit {proc.returncode}: {argv}\n"
            f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )
    return proc


def test(rompack: pathlib.Path) -> None:
    with tempfile.TemporaryDirectory(prefix="ppa-rompack-test-") as t:
        d = pathlib.Path(t)
        # ASM --program produces exactly one 64 KiB executable bank.
        blank = d / "program.prg"
        program = bytearray((0xFF,)) * BANK_SIZE
        program[:7] = bytes((0x25, 0, 0, 0x01, 0x02, 0x03, 0x04))
        blank.write_bytes(program)
        bmp = d / "test.bmp"
        make_bmp24(bmp, 320, 200)
        raw = d / "test.dat"
        raw.write_bytes(b"Hello from PPA-1\x00\xFF")
        packed = d / "packed.bin"

        run([
            str(rompack), str(blank), str(packed),
            "--bmp", f"0001:{bmp}", "--put", f"0002:0100:{raw}", "--info",
        ])
        rom = packed.read_bytes()
        assert len(rom) == ROM_SIZE
        assert rom[:BANK_SIZE] == blank.read_bytes()
        b1 = rom[BANK_SIZE:2 * BANK_SIZE]
        assert b1[:4] == b"PIMG"
        assert b1[4] == 1 and b1[5] == 1
        assert struct.unpack_from("<HHHHH", b1, 6) == (320, 200, 64000, 0x400, 0x10)
        assert len(b1[0x400:0x400 + 64000]) == 64000
        assert all(16 <= c <= 255 for c in b1[0x400:0x400 + 64000])
        assert rom[2 * BANK_SIZE + 0x100:2 * BANK_SIZE + 0x100 + len(raw.read_bytes())] == raw.read_bytes()

        # Even with NO assets, ROMPACK is the last stage producing a 2 MiB image.
        plain = d / "no-assets.bin"
        run([str(rompack), str(blank), str(plain)])
        assert len(plain.read_bytes()) == ROM_SIZE
        assert plain.read_bytes()[:BANK_SIZE] == program
        assert plain.read_bytes()[BANK_SIZE:] == bytes((0xFF,)) * (ROM_SIZE - BANK_SIZE)

        # Preserve older tool callers supplying a complete EEPROM as input.
        legacy = d / "legacy.bin"
        legacy.write_bytes(plain.read_bytes())
        legacy_out = d / "legacy-out.bin"
        run([str(rompack), str(legacy), str(legacy_out)])
        assert legacy_out.read_bytes() == legacy.read_bytes()

        # Reject files that are neither an executable bank nor an EEPROM.
        bad_input = d / "bad-size.prg"
        bad_input.write_bytes(b"short")
        run([str(rompack), str(bad_input), str(d / "invalid.bin")],
            expected_success=False)

        # No writes to the executable bank.
        run([
            str(rompack), str(blank), str(d / "bad.bin"),
            "--put", f"0000:0000:{raw}"
        ], expected_success=False)

        # No resource may cross a bank boundary.
        run([
            str(rompack), str(blank), str(d / "oversize.bin"),
            "--put", f"0003:FFF0:{raw}"
        ], expected_success=False)

        # Two PIMG assets in the same bank must be rejected.
        run([
            str(rompack), str(blank), str(d / "conflict.bin"),
            "--bmp", f"0001:{bmp}", "--bmp", f"0001:{bmp}"
        ], expected_success=False)

        # The standard palette must retain the 16 EGA colors.
        assert b1[0x10:0x13] == bytes((0, 0, 0))
        assert b1[0x10 + 15 * 3:0x10 + 15 * 3 + 3] == bytes((255, 255, 255))
        print("PPA-1 ROM packer: OK (64K program, 2MiB output, BMP, raw, safety, compatibility)")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: test_rompack.py path/to/ppa_rompack")
    test(pathlib.Path(os.path.abspath(sys.argv[1])))
