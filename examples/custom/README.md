# Custom PPA-1 programs

Place your own `.asm` files in this directory. The regular `make` and
`make examples` targets build only the shipped examples, so unfinished
personal programs do not interrupt the emulator build.

From the repository root:

```sh
make custom-examples                         # compile all examples/custom/*.asm
make out/examples/custom/my_program.bin     # compile one program
make all-examples                           # shipped + personal examples
```

The build pipeline is:

```text
examples/custom/my_program.asm
  -> out/examples/custom/my_program.prg (assembler --program)
  -> out/examples/custom/my_program.bin (ROMPACK, EEPROM image)
```

Shared device libraries can be included with
`.include "includes/ppa-slots.inc"` (and other files from
`examples/includes/`): the assembler resolves them through its
repository-root include fallback.

Your personal files in this directory are ignored by Git by default.
This README is tracked so the directory exists after cloning the repository.
