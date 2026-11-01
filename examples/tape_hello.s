; A RAM-loaded tape application, assembled at 0x100000.
.entry start
start:
    MOV.q #0xffff0000, R1
    LEA greeting, R2
loop:
    MOV.b (R2)+, R0
    BEQ done
    MOV.b R0, (R1)
    BRA loop
done:
    HALT
greeting: .asciz "Hello from 9-track tape\n"
