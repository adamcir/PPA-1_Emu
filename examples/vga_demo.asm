; PPA-VGA/1 demo. Insert/activate ppa-vga-1.ppamod in SLOT 1.
.org $0000
    JMP start

.include "includes/ppa-vga.inc"

start:
    CALL vga_init

    MOV BR-HI,#20
    MOV BR-LO,#01
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
