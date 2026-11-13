# PPA-1 PPAMOD modules

`modules/light.ppamod` is the first binary PPA-BUS/1 card.
The first four bytes are ASCII `PPAM` (50 50 41 4D).

## Loading

In the emulator's MODULES menu, select a slot, LOAD a `.ppamod`
file and ACTIVATE it while the PPA-1 power switch is OFF.
Use VIEW to open the card's optional hardware window.

Slot N occupies banks `(0x2000 + (N-1)*0x1000)` through
`(base + 0x0FFF)`. For LIGHT the register map, relative to slot base:

- `0000:0000`: ASCII 'P', read only
- `0000:0001`: ASCII 'P', read only
- `0000:0002`: type ID = 1 (LIGHT), read only
- `0000:0003`: PPAM version = 1
- `0000:0010`: LED state, 0/1, read only
- `0000:0011-0012`: remaining emulated cycles (low 16 bits)
- `0000:0010`: WRITE nonzero = light up for the configured duration;
  WRITE zero = turn off immediately.

Slot 1 example in PPA-1 ASM:

```asm
MOV BR-HI,#20
MOV BR-LO,#00
MOV A,#01
ST $0010,A
```

## PPAMOD file format version 1

All integers little-endian. Fixed 24-byte header:

| Offset | Bytes | Meaning |
|---|---:|---|
| 0 | 4 | `PPAM` magic |
| 4 | 2 | version (1) |
| 6 | 2 | type (1 = LIGHT) |
| 8 | 4 | bytecode length, multiple of 8, <=65536 |
| 12 | 4 | card data length (may exceed 2 MiB) |
| 16 | 4 | default LED pulse duration, emulated CPU microcycles |
| 20 | 4 | reserved, must be zero |

After the header: program bytecode, then optional card data (up to
256 MiB minus 64 KiB). Data are mapped read-only to slot bank + 1
onwards. A module file is NOT restricted by the 2 MiB RAM/EEPROM
limits. Data and bytecode are bounded by the slot's 256 MiB window.

Bytecode is a list of eight-byte rules:
`event:u8, action:u8, offset:u32, duration:u16`.
For v1: `event=1` means bus WRITE, `action=1` means pulse LIGHT
when a nonzero byte is written to the rule's relative register
offset; zero clears it. Duration 0 uses the header default. This
logic is interpreted by the emulator, NOT loaded as host machine code.
Additional module types and opcodes can be added with versioned rules.

Eject / activation requires POWER OFF; RESET clears LED state while
keeping cards attached. Modules work with GUI CPU and console CPU.
