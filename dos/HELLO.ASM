.globl start
.entry start
start:
    LEA message, R0
    MOV.q #0, R14
    SYSCALL #33
    CLR.q R0
    RET
message: .asciz "Hello from KotlTOS!\n"
