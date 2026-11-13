; PPA-SOUND/1 test. Insert/activate modules/ppa-sound-1.ppamod in SLOT 1.
; Load assembled EEPROM image, then PWR ON and RUN. Try 100 kHz.
.org $0000
    JMP start
.include "includes/ppa-sound.inc"

start:
    CALL sound_init
    CALL sound_silence
    MOV BR-HI,#20
    MOV A,#FF
    ST $0023,A
    MOV B,#B8
    MOV C,#01
    CALL sound_voice1
    MOV D,#00
next_wave:
    MOV BR-HI,#20
    ST $0022,D     ; 0 square, 1 saw, 2 triangle, 3 sine
    MOV X-HI,#35
outer:
    MOV X-LO,#FF
inner:
    DEC X-LO
    JNZ inner
    DEC X-HI
    JNZ outer
    INC D
    MOV A,D
    CMP #04
    JNZ next_wave
    CALL sound_stop
    HLT
