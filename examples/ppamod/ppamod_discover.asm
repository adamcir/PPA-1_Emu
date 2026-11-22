; PPAMOD scan example: find PPA-SOUND/1 in any active slot.
.org $0000
    JMP start
.include "includes/ppa-slots.inc"
start:
    MOV A,#SLOTS_TYPE_SOUND
    MOV B,#00
    CALL slots_find
    CMP A,#FF
    JZ no_sound
    MOV BR-HI,B
    MOV BR-LO,C
    LD A,$0002
    CMP A,#02
    JNZ no_sound
    LD A,$0080
    CMP A,#50
    JNZ no_sound
    MOV A,#01
    ST $0010,A
    HLT
no_sound:
    HLT
