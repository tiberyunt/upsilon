; Native boot of a KotlTOS disk. R27=1 automatic (fall back to tape), 0 manual.
.equ MON_DISK, 0xffff0300
monitor_disk_boot:
    SHL3.q #6, R29, R17
    ADD.q #MON_DISK, R17
    MOV.q #0, 16(R17)
    MOV.q #MON_TAPE_HEADER, 24(R17)
    MOV.q #512, 32(R17)
    MOV.b #1, 8(R17)
    MOVZ.b.q 0(R17), R0
    AND.q #4, R0
    BNE monitor_disk_failed
    CMP.q #0x544f4f424c544f4b, @MON_TAPE_HEADER
    BNE monitor_disk_failed
    MOV.q @MON_TAPE_HEADER+8, R21
    MOV.q @MON_TAPE_HEADER+16, R22
    MOV.q @MON_TAPE_HEADER+24, R23
    MOV.q @MON_TAPE_HEADER+32, R24
    CMP.q #0x80000, R21
    BNE monitor_disk_failed
    TST.q R23
    BEQ monitor_disk_failed
    CMP.q #65536, R23
    BHI monitor_disk_failed
    CMP.q R21, R22
    BLO monitor_disk_failed
    ADD3.q R21, R23, R0
    CMP.q R0, R22
    BHS monitor_disk_failed
    CMP.q @MON_RAM_SIZE, R0
    BHI monitor_disk_failed
    MOV.q #4096, 16(R17)
    MOV.q R21, 24(R17)
    MOV.q R23, 32(R17)
    MOV.b #1, 8(R17)
    MOVZ.b.q 0(R17), R0
    AND.q #4, R0
    BNE monitor_disk_failed
    MOV.q R21, R25
    MOV.q R23, R26
    MOV.q #0xcbf29ce484222325, R28
monitor_disk_hash:
    MOVZ.b.q (R25)+, R0
    XOR.q R0, R28
    MUL.q #0x100000001b3, R28
    SOB R26, monitor_disk_hash
    CMP.q R24, R28
    BNE monitor_disk_failed
    LEA monitor_disk_boot_text, R8
    JSR basic_puts
    MOV.q R29, R0
    JSR basic_print_number
    LEA monitor_disk_boot_suffix, R8
    JSR basic_puts
    MTSR #0, MMUCTL
    MTSR #0x700, PSR
    MOV.q @MON_RAM_SIZE, SP
    MOV.q #67, R0
    MOV.q R29, R1
    IFENCE
    JMP (R22)
monitor_disk_failed:
    TST.q R27
    BEQ monitor_disk_report
    INC.q R29
    CMP.q #4, R29
    BLO monitor_disk_boot
    MOVZ.b.q @MON_TAPE+4, R0
    AND.q #64, R0
    BNE monitor_tape_boot
    LEA monitor_disk_none, R8
    JSR basic_puts
    BRA monitor_prompt
monitor_disk_report:
    LEA monitor_disk_error, R8
    JSR basic_puts
    MOV.q R29, R0
    JSR basic_print_number
    LEA monitor_disk_error_suffix, R8
    JSR basic_puts
    BRA monitor_prompt
monitor_disk_boot_text: .asciz "Booting disk"
monitor_disk_boot_suffix: .asciz "...\n"
monitor_disk_error: .asciz "?DISK"
monitor_disk_error_suffix: .asciz " NOT BOOTABLE: boot tape0 to install KotlTOS.\n"
monitor_disk_none: .asciz "?NO BOOTABLE DISK: use boot diskN or boot tape0\n"
