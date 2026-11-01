; R0 = fibonacci(20) = 6765. Arithmetic uses the three-operand form.
.entry start
start:
    MOV.q #0, R0
    MOV.q #1, R1
    MOV.q #20, R2
loop:
    ADD3.q R0, R1, R3
    MOV.q R1, R0
    MOV.q R3, R1
    SOB R2, loop
    HALT
