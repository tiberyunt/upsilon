.entry syscopy_start
syscopy_start:
    MOV.q #20, R14
    SYSCALL #33
    RET
