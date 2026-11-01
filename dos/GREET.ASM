.globl greet
greet:
    LEA greeting, R0
    MOV.q #0, R14
    SYSCALL #33
    RET
greeting: .asciz "Linked two native modules.\n"
