# PPA-1 memory map

All values hexadecimal. Bank size: 64 KiB. Every device region
occupies 0x1000 banks (256 MiB of address space).

| Banks | Assignment |
|---|---|
| 0000-0FFF | ROM / EEPROM |
| 1000-1FFF | RAM |
| 2000-2FFF | SLOT 1 |
| 3000-3FFF | SLOT 2 |
| 4000-4FFF | SLOT 3 |
| 5000-5FFF | SLOT 4 |
| 6000-6FFF | SLOT 5 |
| 7000-7FFF | SLOT 6 |
| 8000-8FFF | SLOT 7 |
| 9000-9FFF | SLOT 8 |
| A000-EFFF | Free / reserved |
| F000-FFFF | Internal I/O |

EEPROM and RAM are configurable independently from 1 byte through
2 MiB (2097152 bytes), defaulting to 2 MiB each.
At maximum EEPROM occupies banks 0000-001F and RAM occupies 1000-101F. Unpopulated memory and empty slots return FF on read
and ignore writes. Device regions are reserved, not allocated in full.

Instructions still fetch from EEPROM 0000:PC.
CPU PUSH/POP/CALL/RET stack is always the final 256 bytes of
installed RAM. At 2 MiB this is 101F:FF00-FFFF, with first PUSH writing
to 101F:FFFF. For less than 256 bytes RAM, stack operations halt with ERROR.
RAM outside the installed byte count remains unpopulated.
SERIAL/1: F000:FF00-FF03. Reserved I/O: F000:FF80-FFFF.
Old binaries using bank 0040 for RAM or 0050 for I/O must be updated.

Configure GUI capacities in Preferences (decimal byte values, PWR OFF).
Command-line equivalents: --ram BYTES and --rom-size BYTES.
EEPROM images larger than the configured installed capacity cannot be loaded;
shrinking attached EEPROM below image file size is rejected. Exports write
exactly the installed EEPROM size. Assembler ROM output limit is 2 MiB.
