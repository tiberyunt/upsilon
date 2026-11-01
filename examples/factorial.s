; Calls, stack frames, and callee-saved registers. R0 = 10! = 3628800.
    MOV.q #10, R0
    JSR factorial
    HALT
factorial:
    ENTER #0x0001, #16
    MOV.q R0, R16
    MOV.q #1, R0
loop:
    MUL.q R16, R0
    SOB R16, loop
    EXIT
