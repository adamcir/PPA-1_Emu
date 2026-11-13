; ============================================
; easy_io.inc
; PPA-1 simple terminal I/O
; (c) 2026 Adam Cír (Adava)
; ============================================

SERIAL_TX      EQU FF00
SERIAL_RX      EQU FF01
SERIAL_STATUS  EQU FF02
SERIAL_CONTROL EQU FF03

SYSTEM_BR_HI   EQU F0
SYSTEM_BR_LO   EQU 00

; Bank regions: 0x1000 banks / 256 MiB each
ROM_BR_HI      EQU 00
RAM_BR_HI      EQU 10
SLOT1_BR_HI    EQU 20
SLOT2_BR_HI    EQU 30
SLOT3_BR_HI    EQU 40
SLOT4_BR_HI    EQU 50
SLOT5_BR_HI    EQU 60
SLOT6_BR_HI    EQU 70
SLOT7_BR_HI    EQU 80
SLOT8_BR_HI    EQU 90
FREE_BR_HI     EQU A0
IO_BR_HI       EQU F0
IO_BR_LO       EQU 00

; CPU stack: RAM bank 1000, FE00-FEFF

TX_READY       EQU 01
RX_READY       EQU 02

ASCII_CR       EQU 0D
ASCII_LF       EQU 0A
ASCII_BS       EQU 08
ASCII_DEL      EQU 7F

putchar:
    PUSH BR-HI
    PUSH BR-LO
    PUSH B

    MOV B,A

    MOV BR-HI,#SYSTEM_BR_HI
    MOV BR-LO,#SYSTEM_BR_LO

.wait_tx:
    LD A,$SERIAL_STATUS
    AND A,#TX_READY
    CMP A,#00
    JZ .wait_tx

    MOV A,B
    ST $SERIAL_TX,A

    POP B
    POP BR-LO
    POP BR-HI
    RET

getchar_raw:
    PUSH BR-HI
    PUSH BR-LO

    MOV BR-HI,#SYSTEM_BR_HI
    MOV BR-LO,#SYSTEM_BR_LO

.wait_rx:
    LD A,$SERIAL_STATUS
    AND A,#RX_READY
    CMP A,#00
    JZ .wait_rx

    LD A,$SERIAL_RX

    POP BR-LO
    POP BR-HI
    RET

getchar:
.read:
    CALL getchar_raw

    CMP A,#ASCII_LF
    JZ .enter

    CMP A,#ASCII_CR
    JZ .enter

    CMP A,#ASCII_DEL
    JZ .backspace

    RET

.enter:
    MOV A,#ASCII_CR
    RET

.backspace:
    MOV A,#ASCII_BS
    RET

newline:
    PUSH A

    MOV A,#ASCII_CR
    CALL putchar

    MOV A,#ASCII_LF
    CALL putchar

    POP A
    RET

backspace_echo:
    PUSH A

    MOV A,#ASCII_BS
    CALL putchar

    MOV A,#' '
    CALL putchar

    MOV A,#ASCII_BS
    CALL putchar

    POP A
    RET

puts:
.loop:
    MOV BR-HI,B
    MOV BR-LO,C

    LD A,X

    CMP A,#00
    JZ .done

    CALL putchar

    ; X++
    INC X-LO
    JNC .loop

    INC X-HI
    JMP .loop

.done:
    RET
    
