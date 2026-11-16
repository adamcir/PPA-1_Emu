; PPA-1 PPAMOD slot enumeration and cached device binding.
; Compatible with PPAMOD v1 and v2; uses the system discovery controller
; at BR F000:FF30..FF36. No CPU-specific host callbacks required.
; Include ONCE per program, after the initial JMP start.
;
; slots_find:   A=type low (1..FF), B=occurrence (0=first).
;               Returns A=slot 1..8 or FF, B=bank HI, C=bank LO.
;               Preserves D, X, Y and BR; modifies flags.
; slots_find16: A=type low, D=type high, B=occurrence. Same outputs,
;               preserves D, X, Y and BR.
; slots_bind_*: search first active module of type and cache bank HI in RAM.
;               Returns A=slot or FF; modifies A, B, C, flags; preserves BR.
; slots_use_*:  select the cached first bank in BR, preserving A/B/C/D/X/Y.
;               If not bound, select unmapped A000 instead of writable ROM.
; slots_use_vga_vram: selects the VGA second (VRAM) bank.
; Use slots_find for additional instances and select their BR manually.
;
; RAM bank 1000 reserved: addresses 0060-0063.
; 0060 LIGHT, 0061 SOUND, 0062 VGA, 0063 PS2.

SLOTS_TYPE_LIGHT EQU 01
SLOTS_TYPE_SOUND EQU 02
SLOTS_TYPE_VGA   EQU 03
SLOTS_TYPE_PS2   EQU 04

SLOTS_QUERY_TYPE_LO EQU $FF30
SLOTS_QUERY_TYPE_HI EQU $FF31
SLOTS_QUERY_INDEX   EQU $FF32
SLOTS_RESULT_HI     EQU $FF33
SLOTS_RESULT_LO     EQU $FF34
SLOTS_RESULT_SLOT   EQU $FF35
SLOTS_RESULT_COUNT  EQU $FF36

SLOTS_CACHE_LIGHT EQU $0060
SLOTS_CACHE_SOUND EQU $0061
SLOTS_CACHE_VGA   EQU $0062
SLOTS_CACHE_PS2   EQU $0063

slots_find:
    PUSH D
    MOV D,#00
    CALL slots_find16
    POP D
    RET

slots_find16:
    PUSH BR-HI
    PUSH BR-LO
    MOV BR-HI,#F0
    MOV BR-LO,#00
    ST $FF30,A
    ST $FF31,D
    ST $FF32,B
    LD A,$FF35
    CMP A,#FF
    JZ .absent
    LD B,$FF33
    LD C,$FF34
    JMP .done
.absent:
    MOV B,#00
    MOV C,#00
.done:
    POP BR-LO
    POP BR-HI
    RET

slots_bind_light:
    MOV A,#SLOTS_TYPE_LIGHT
    MOV B,#00
    CALL slots_find
    CMP A,#FF
    JNZ .found
    MOV B,#A0
.found:
    PUSH BR-HI
    PUSH BR-LO
    MOV BR-HI,#10
    MOV BR-LO,#00
    ST $0060,B
    POP BR-LO
    POP BR-HI
    RET

slots_bind_sound:
    MOV A,#SLOTS_TYPE_SOUND
    MOV B,#00
    CALL slots_find
    CMP A,#FF
    JNZ .found
    MOV B,#A0
.found:
    PUSH BR-HI
    PUSH BR-LO
    MOV BR-HI,#10
    MOV BR-LO,#00
    ST $0061,B
    POP BR-LO
    POP BR-HI
    RET

slots_bind_vga:
    MOV A,#SLOTS_TYPE_VGA
    MOV B,#00
    CALL slots_find
    CMP A,#FF
    JNZ .found
    MOV B,#A0
.found:
    PUSH BR-HI
    PUSH BR-LO
    MOV BR-HI,#10
    MOV BR-LO,#00
    ST $0062,B
    POP BR-LO
    POP BR-HI
    RET

slots_bind_ps2:
    MOV A,#SLOTS_TYPE_PS2
    MOV B,#00
    CALL slots_find
    CMP A,#FF
    JNZ .found
    MOV B,#A0
.found:
    PUSH BR-HI
    PUSH BR-LO
    MOV BR-HI,#10
    MOV BR-LO,#00
    ST $0063,B
    POP BR-LO
    POP BR-HI
    RET

slots_use_light:
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD BR-HI,$0060
    CMP BR-HI,#00
    JNZ .ready
    MOV BR-HI,#A0
.ready:
    RET

slots_use_sound:
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD BR-HI,$0061
    CMP BR-HI,#00
    JNZ .ready
    MOV BR-HI,#A0
.ready:
    RET

slots_use_vga:
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD BR-HI,$0062
    CMP BR-HI,#00
    JNZ .ready
    MOV BR-HI,#A0
.ready:
    RET

slots_use_vga_vram:
    CALL slots_use_vga
    MOV BR-LO,#01
    RET

slots_use_ps2:
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD BR-HI,$0063
    CMP BR-HI,#00
    JNZ .ready
    MOV BR-HI,#A0
.ready:
    RET
