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

Installed EEPROM remains 4 MiB, 0000-003F. Installed RAM remains
1 MiB, 1000-100F. Unpopulated memory and empty slots return FF on read
and ignore writes. Device regions are reserved, not allocated in full.

Instructions still fetch from EEPROM 0000:PC.
CPU PUSH/POP/CALL/RET stack: RAM 1000:FE00-FEFF.
SERIAL/1: F000:FF00-FF03. Reserved I/O: F000:FF80-FFFF.
Old binaries using bank 0040 for RAM or 0050 for I/O must be updated.
