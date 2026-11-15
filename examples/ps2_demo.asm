; PPA-PS/2/1 keyboard scan-code logger.
; Install/activate PPA-PS/2/1 in SLOT 2 (bank 3000).
; The program logs raw PS/2 Set-2 bytes in RAM bank 1000.
; Inspect RAM via HEX VIEW, bank 1000, addresses 0000 onward.
.org $0000

start:
    MOV X-HI,#00
    MOV X-LO,#00

.poll:
    MOV BR-HI,#30
    MOV BR-LO,#00
    LD A,$0011         ; STATUS
    AND A,#01
    JZ .poll           ; wait for a byte
    LD A,$0010         ; DATA: pop next Set-2 byte
    MOV B,A

    MOV BR-HI,#10      ; RAM bank 1000
    MOV BR-LO,#00
    ST X,B
    INC X-LO
    JNZ .poll
    INC X-HI
    JMP .poll
