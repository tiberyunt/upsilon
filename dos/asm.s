.entry asm_start
asm_start:
    MOV.q #8, R14
    SYSCALL #33
    RET
