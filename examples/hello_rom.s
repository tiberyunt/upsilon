; Boot ROM for the default 1 MiB Upsilon reference machine.
; The first two quadwords are the reset SP and PC, loaded by --boot.
.equ SERIAL_DATA,   0xffff0000
.equ SERIAL_STATUS, 4
.equ TX_READY,      2
.entry start

    .quad 0x100000, start

start:
    MOV.q #SERIAL_DATA, R1
    LEA greeting, R2
next_char:
    MOV.b (R2)+, R0
    BEQ done
wait_tx:
    MOV.b SERIAL_STATUS(R1), R3
    AND.b #TX_READY, R3
    BEQ wait_tx
    MOV.b R0, (R1)
    BRA next_char
done:
    HALT

greeting:
    .asciz "Hello from CISCland\n"
