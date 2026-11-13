; PPA-SOUND/1 test. Insert/activate modules/ppa-sound-1.ppamod in SLOT 1.
; Load assembled EEPROM image, then PWR ON and RUN. Try 100 kHz.
.org $0000
    JMP start
.include "includes/ppa-sound.inc"

start:
    CALL sound_init
    CALL sound_silence
    MOV BR-HI,#20
    MOV A,#01
    ST $0022,A     ; saw wave
    MOV A,#C0
    ST $0023,A
    MOV B,#64      ; 100 Hz
    MOV C,#00
sweep_loop:
    CALL sound_voice1
    MOV D,#06
hold_outer:
    MOV X-LO,#FF
hold_inner:
    DEC X-LO
    JNZ hold_inner
    DEC D
    JNZ hold_outer
    INC B
    JNZ sweep_loop
    INC C
    MOV B,#00
    CALL sound_voice1
    MOV D,#70
end_delay:
    MOV X-LO,#FF
end_inner:
    DEC X-LO
    JNZ end_inner
    DEC D
    JNZ end_delay
    CALL sound_stop
    HLT
