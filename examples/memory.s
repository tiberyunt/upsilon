; Indexed addressing, long-running block copies, and atomic addition.
    LEA source, R1
    LEA dest, R2
    MOV.q #4, R0
    MOVS.q
    LEA dest, R3
    MOV.q #2, R4
    MOV.q (R3)[R4], R5
    MOV.q #100, R6
    XADD.q R6, (R3)
    HALT
.align 8
source: .quad 10, 20, 30, 40
dest:   .space 32
