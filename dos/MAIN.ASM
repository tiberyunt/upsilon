.globl main
.extern greet
.entry main
main:
    JSR greet
    CLR.q R0
    RET
