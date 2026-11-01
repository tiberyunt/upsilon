; v0.4 values, TLS addressing, conditional moves and 16-byte CAS.
.entry start
.text
.globl start
start:
    MTSR #0x30000, TP
    MOV.q #42, 8(TP)
    MFTP R0
    MOV.q 8(TP), R1
    CMP.q #42, R1
    SETEQ.b R2
    CMOVEQ.q #123, R3
    MOV.q #-1, R4
    MOV.b #255, R4            ; R4 is now 255, all upper bits clear.
    MOVSX.bq #-1, R5         ; Explicit sign extension to quad.
    CLR.q R6
    CLR.q R7
    MOV.q #100, R8
    MOV.q #200, R9
    CAS2 R6, R8, @0x30010    ; Replaces {0,0} with {100,200}.
    SETEQ.q R10
    IOFENCE
    PAUSE
    PREFETCH 8(TP)
    HALT
