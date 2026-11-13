; PPA-SOUND/1 test. Insert/activate modules/ppa-sound-1.ppamod in SLOT 1.
; Load assembled EEPROM image, then PWR ON and RUN. Try 100 kHz.
.org $0000
    JMP start
.include "includes/ppa-sound.inc"

start:
    CALL sound_init
    ; A4 (440 Hz); other channels silent
    MOV B,#00
    MOV C,#00
    CALL sound_voice2
    CALL sound_voice3
    CALL sound_voice4
    MOV B,#B8
    MOV C,#01
    CALL sound_voice1
    MOV D,#80
hold_outer:
    MOV X-LO,#FF
hold_inner:
    DEC X-LO
    JNZ hold_inner
    DEC D
    JNZ hold_outer
    CALL sound_stop
    HLT
