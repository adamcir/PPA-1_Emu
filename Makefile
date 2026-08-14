CC = gcc

EMU = out/PPA-1_Emu
SRC = main.c
ASM_SRC = tools/asm/ppa-1_asm.c
ASM = out/asm/ppa-1_asm

.PHONY: all clean run

all: asm emu

out:
	mkdir -p out/asm/

emu: out
	$(CC) $(SRC) -o $(EMU)

asm: out
	$(CC) $(ASM_SRC) -o $(ASM)

clean:
	rm -rf out/
