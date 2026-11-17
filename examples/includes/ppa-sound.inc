; PPA-SOUND/1 shared ASM driver (automatic type-2 slot discovery).
; Include AFTER ppa-slots.inc and an initial JMP start; routines live in EEPROM.
; CALL sound_init: enable master + four voices, config waveforms/volume.
; CALL sound_voice1..4: B=frequency low, C=frequency high.
; CALL sound_silence: all frequency registers zero.
; CALL sound_stop: master off.
; CALL sound_adsr_defaults: zero-time ADSR reset.
; CALL sound_gate1..4: A=0 release, 1 hold, 2 retrigger.
; sound_init discovers the first active SOUND module and caches its bank.
; Calls modify BR; sound_init / sound_stop also modify A.
; Keep BR=0 when reading EEPROM with LD A,Y.

SOUND_ON EQU $0010
SOUND_MASTER_VOL EQU $0011
SOUND_CH1_LO EQU $0020
SOUND_CH1_HI EQU $0021
SOUND_CH1_WAVE EQU $0022
SOUND_CH1_VOL EQU $0023
SOUND_CH1_ENABLE EQU $0024
SOUND_CH2_LO EQU $0030
SOUND_CH2_HI EQU $0031
SOUND_CH2_WAVE EQU $0032
SOUND_CH2_VOL EQU $0033
SOUND_CH2_ENABLE EQU $0034
SOUND_CH3_LO EQU $0040
SOUND_CH3_HI EQU $0041
SOUND_CH3_WAVE EQU $0042
SOUND_CH3_VOL EQU $0043
SOUND_CH3_ENABLE EQU $0044
SOUND_CH4_LO EQU $0050
SOUND_CH4_HI EQU $0051
SOUND_CH4_WAVE EQU $0052
SOUND_CH4_VOL EQU $0053
SOUND_CH4_ENABLE EQU $0054

; Optional ADSR extension (0..255): A/D 0..2s, R 0..4s,
; S is sustain level. GATE: 0 release, 1 hold, 2 retrigger.
; Legacy programs not writing GATE retain the old non-ADSR behavior.
SOUND_CH1_ATTACK EQU $0025
SOUND_CH1_DECAY EQU $0026
SOUND_CH1_SUSTAIN EQU $0027
SOUND_CH1_RELEASE EQU $0028
SOUND_CH1_GATE EQU $0029
SOUND_CH2_ATTACK EQU $0035
SOUND_CH2_DECAY EQU $0036
SOUND_CH2_SUSTAIN EQU $0037
SOUND_CH2_RELEASE EQU $0038
SOUND_CH2_GATE EQU $0039
SOUND_CH3_ATTACK EQU $0045
SOUND_CH3_DECAY EQU $0046
SOUND_CH3_SUSTAIN EQU $0047
SOUND_CH3_RELEASE EQU $0048
SOUND_CH3_GATE EQU $0049
SOUND_CH4_ATTACK EQU $0055
SOUND_CH4_DECAY EQU $0056
SOUND_CH4_SUSTAIN EQU $0057
SOUND_CH4_RELEASE EQU $0058
SOUND_CH4_GATE EQU $0059

sound_init:
    CALL slots_bind_sound
    CMP A,#FF
    JZ .missing
    CALL slots_use_sound
    MOV A,#FF
    ST $0011,A
    MOV A,#01
    ST $0010,A
    CALL sound_waveforms
    RET
.missing:
    RET

sound_waveforms:
    CALL slots_use_sound
    MOV A,#02
    ST $0022,A
    ST $0042,A
    MOV A,#01
    ST $0032,A
    MOV A,#03
    ST $0052,A
    MOV A,#C8
    ST $0023,A
    MOV A,#A0
    ST $0033,A
    ST $0043,A
    MOV A,#FF
    ST $0053,A
    MOV A,#01
    ST $0024,A
    ST $0034,A
    ST $0044,A
    ST $0054,A
    RET

sound_voice1:
    CALL slots_use_sound
    ST $0020,B
    ST $0021,C
    RET
sound_voice2:
    CALL slots_use_sound
    ST $0030,B
    ST $0031,C
    RET
sound_voice3:
    CALL slots_use_sound
    ST $0040,B
    ST $0041,C
    RET
sound_voice4:
    CALL slots_use_sound
    ST $0050,B
    ST $0051,C
    RET


; CALL sound_adsr_defaults: initialize all four envelopes to
; instantaneous attack/decay, full sustain, instant release.
; Preserves B and C; modifies A and BR.
sound_adsr_defaults:
    CALL slots_use_sound
    MOV A,#00
    ST SOUND_CH1_ATTACK,A
    ST SOUND_CH1_DECAY,A
    ST SOUND_CH1_RELEASE,A
    ST SOUND_CH2_ATTACK,A
    ST SOUND_CH2_DECAY,A
    ST SOUND_CH2_RELEASE,A
    ST SOUND_CH3_ATTACK,A
    ST SOUND_CH3_DECAY,A
    ST SOUND_CH3_RELEASE,A
    ST SOUND_CH4_ATTACK,A
    ST SOUND_CH4_DECAY,A
    ST SOUND_CH4_RELEASE,A
    MOV A,#FF
    ST SOUND_CH1_SUSTAIN,A
    ST SOUND_CH2_SUSTAIN,A
    ST SOUND_CH3_SUSTAIN,A
    ST SOUND_CH4_SUSTAIN,A
    RET

; CALL sound_gate1: A=0 (release), 1 (hold), 2 (retrigger).
; Gate is optional; caller controls the envelope independently of ENABLE.
sound_gate1:
    CALL slots_use_sound
    ST SOUND_CH1_GATE,A
    RET
; CALL sound_gate2: A=0 (release), 1 (hold), 2 (retrigger).
; Gate is optional; caller controls the envelope independently of ENABLE.
sound_gate2:
    CALL slots_use_sound
    ST SOUND_CH2_GATE,A
    RET
; CALL sound_gate3: A=0 (release), 1 (hold), 2 (retrigger).
; Gate is optional; caller controls the envelope independently of ENABLE.
sound_gate3:
    CALL slots_use_sound
    ST SOUND_CH3_GATE,A
    RET
; CALL sound_gate4: A=0 (release), 1 (hold), 2 (retrigger).
; Gate is optional; caller controls the envelope independently of ENABLE.
sound_gate4:
    CALL slots_use_sound
    ST SOUND_CH4_GATE,A
    RET

; Set individual ADSR using direct ST SOUND_CHn_* with BR bound
; through slots_use_sound. GATE=0 does not disable the channel, allowing
; the oscillator to ring out during its RELEASE stage.

sound_silence:
    MOV B,#00
    MOV C,#00
    CALL sound_voice1
    CALL sound_voice2
    CALL sound_voice3
    CALL sound_voice4
    RET

sound_stop:
    CALL slots_use_sound
    MOV A,#00
    ST $0010,A
    RET
