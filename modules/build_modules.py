#!/usr/bin/env python3
"""Compile PPAMOD v1 source definitions into binary module images."""
import json
import struct
import sys
from pathlib import Path


def build(source: Path, destination: Path) -> None:
    definition = json.loads(source.read_text(encoding="utf-8"))
    version = int(definition.get("version", 1))
    module_type = int(definition["type"])
    pulse_cycles = int(definition.get("pulse_cycles", 0))
    rules = definition.get("rules", [])
    if version != 1 or module_type not in (1, 2):
        raise ValueError("PPAMOD v1 supports LIGHT (1) and PPA-SOUND/1 (2)")
    if not 0 <= pulse_cycles <= 0xFFFFFFFF:
        raise ValueError("pulse_cycles must fit u32")
    if len(rules) * 8 > 65536:
        raise ValueError("too many rules")
    if module_type == 2 and rules:
        raise ValueError("PPA-SOUND/1 uses device registers, not bytecode rules")

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
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(header + code + payload)
    print(f"PPAMOD: {destination} ({len(header) + len(code) + len(payload)} bytes)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("Usage: build_modules.py source.json destination.ppamod")
    try:
        build(Path(sys.argv[1]), Path(sys.argv[2]))
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        sys.exit(f"PPAMOD error: {exc}")
