; LIGHT/1 demo for PPA-1 (SLOT 1: banks 2000-2FFF)
; Requires: load modules/light.ppamod into slot 1 and ACTIVATE it
; The LED pulses for the module's configured duration on each write.

    MOV BR-HI,#20
    MOV BR-LO,#00

.loop:
    MOV A,#01
    ST $0010,A
    ; A visible period of CPU execution, then repeat.
    MOV X-HI,#00
    MOV X-LO,#FF
.delay:
    DEC X-LO
    JNZ .delay
    JMP .loop
