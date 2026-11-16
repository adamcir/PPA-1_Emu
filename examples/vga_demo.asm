; PPA-VGA/1 demo. Insert/activate ppa-vga-1.ppamod in ANY slot.
.org $0000
    JMP start
.include "includes/ppa-slots.inc"

.include "includes/ppa-vga.inc"

start:
    CALL vga_init

    CALL slots_use_vga_vram
    MOV X-HI,#00
    MOV X-LO,#00
    MOV A,#00

.fill:
    ST X,A
    INC X-LO
    JNZ .fill
    INC X-HI
    ADD A,#01
    CMP X-HI,#FA
    JNZ .fill

    HLT
