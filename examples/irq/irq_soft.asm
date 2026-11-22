; Software interrupt #1 works even if maskable interrupts are disabled.
.org $0000
    JMP start
start:
    DI
    MOV IVR-HI,#FF
    MOV IVR-LO,#00
    MOV B,#00
    INT #01
    HLT

irq_one:
    INC B
    IRET

.org $FF00
    .word irq_one
    .word irq_one
    .word irq_one
    .word irq_one
    .word irq_one
    .word irq_one
    .word irq_one
    .word irq_one
