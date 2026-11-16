#!/usr/bin/env python3
"""Compile PPAMOD v1/v2 definitions into binary module images."""
import json
import struct
import sys
from pathlib import Path


def build(source: Path, destination: Path) -> None:
    definition = json.loads(source.read_text(encoding="utf-8"))
    version = int(definition.get("version", 2))
    module_type = int(definition["type"])
    pulse_cycles = int(definition.get("pulse_cycles", 0))
    rules = definition.get("rules", [])
    if version not in (1, 2) or module_type not in (1, 2, 3, 4):
        raise ValueError("PPAMOD file format must be 1 or 2, type must be LIGHT(1), SOUND(2), VGA(3), PS2(4)")
    if not 0 <= pulse_cycles <= 0xFFFFFFFF:
        raise ValueError("pulse_cycles must fit u32")
    if len(rules) * 8 > 65536:
        raise ValueError("too many rules")
    if module_type in (2, 3, 4) and rules:
        raise ValueError("PPA-SOUND/1, PPA-VGA/1 and PPA-PS/2/1 use device registers, not bytecode rules")

    code = bytearray()
    for rule in rules:
        event = int(rule["event"])
        action = int(rule["action"])
        offset = int(rule["offset"])
        duration = int(rule.get("duration", 0))
        if (event, action) != (1, 1) or not 0 <= offset < 65536 or not 0 <= duration <= 65535:
            raise ValueError("unsupported event, action, offset or duration")
        code += struct.pack("<BBIH", event, action, offset, duration)
    payload = bytes.fromhex(definition.get("data_hex", ""))
    if len(payload) > 256 * 1024 * 1024 - 65536:
        raise ValueError("module data exceeds PPAMOD v1 limit")
    header = struct.pack("<4sHHIIII", b"PPAM", version, module_type,
                         len(code), len(payload), pulse_cycles, 0)
    identity = b""
    if version == 2:
        name = definition.get("name")
        if not isinstance(name, str) or not name or not name.isascii() or not name.isprintable():
            raise ValueError("v2 name must be non-empty printable ASCII")
        name_bytes = name.encode("ascii")
        if len(name_bytes) > 47:
            raise ValueError("v2 name must be at most 47 bytes")
        semver = definition.get("module_version", [1, 0, 0])
        if (not isinstance(semver, list) or len(semver) != 3 or
                any(not isinstance(n, int) or isinstance(n, bool) or n < 0 or n > 255 for n in semver)):
            raise ValueError("module_version must contain three u8 integers")
        identity = (name_bytes.ljust(48, bytes(1)) +
                    bytes(semver) + bytes(13))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(header + identity + code + payload)
    print(f"PPAMOD: {destination} ({len(header) + len(identity) + len(code) + len(payload)} bytes)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("Usage: build_modules.py source.json destination.ppamod")
    try:
        build(Path(sys.argv[1]), Path(sys.argv[2]))
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        sys.exit(f"PPAMOD error: {exc}")
