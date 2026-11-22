; PPA-1 IRQ0 TIMER/1 demo: increments RAM[1000:0000] every 0x20 microcycles.
; CPU fetches all instructions and interrupt vectors from EEPROM bank 0000.
; IRQ vector entries are 16-bit BIG ENDIAN, starting at IVR.
.org $0000
    JMP start

start:
    DI
    MOV IVR-HI,#FF
    MOV IVR-LO,#00

    MOV BR-HI,#10
    MOV BR-LO,#00
    MOV A,#00
    ST $0000,A

    MOV BR-HI,#F0
    MOV BR-LO,#00
    MOV A,#01
    ST $FF11,A       ; IMR: unmask IRQ0
    MOV A,#20
    ST $FF20,A       ; TIMER PERIOD low = 0x20
    MOV A,#00
    ST $FF21,A       ; TIMER PERIOD high
    MOV A,#03
    ST $FF22,A       ; timer enable + periodic
    EI

.loop:
    JMP .loop        ; interrupted on instruction boundaries

irq_timer:
    PUSH A           ; IRQ entry saves SR, PC, BR; general regs are caller-saved
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0000
    INC A
    ST $0000,A
    MOV BR-HI,#F0
    MOV BR-LO,#00
    MOV A,#01
    ST $FF12,A       ; W1C acknowledge IRQ0
    POP A
    IRET

.org $FF00
    .word irq_timer  ; IRQ0
    .word irq_timer  ; IRQ1 (unused example placeholder)
    .word irq_timer  ; IRQ2
    .word irq_timer  ; IRQ3
    .word irq_timer  ; IRQ4
    .word irq_timer  ; IRQ5
    .word irq_timer  ; IRQ6
    .word irq_timer  ; IRQ7
