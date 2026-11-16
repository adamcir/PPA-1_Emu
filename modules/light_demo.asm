; LIGHT/1 demo. Discover first active LIGHT module in any slot.
.org $0000
    JMP start
.include "../examples/includes/ppa-slots.inc"

start:
    CALL slots_bind_light
    CMP A,#FF
    JZ no_light
.loop:
    CALL slots_use_light
    MOV A,#01
    ST $0010,A
    MOV X-HI,#00
    MOV X-LO,#FF
.delay:
    DEC X-LO
    JNZ .delay
    JMP .loop
no_light:
    HLT
