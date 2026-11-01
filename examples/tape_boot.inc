; Native firmware boot of the first SIMH tape file. Two passes validate record
; lengths/errors before loading. No host loader service or special instruction.
.equ MON_TAPE, 0xffff0100
.equ MON_RAM_SIZE, 0xffff0200
.equ MON_TAPE_HEADER, 0x44000
monitor_tape_boot:
    MOV.b #1, @MON_TAPE+8
    MOVZ.b.q @MON_TAPE+4, R28
    AND3.q #1, R28, R0
    BEQ monitor_tape_offline
    JSR monitor_tape_next
    AND3.q #12, R28, R0
    BNE monitor_tape_not_bootable
    MOV.q @MON_TAPE+16, R0
    CMP.q #48, R0
    BNE monitor_tape_not_bootable
    MOV.q #MON_TAPE_HEADER, R2
    FILL.b @MON_TAPE
    CMP.q #0x544f4f4239535055, @MON_TAPE_HEADER
    BNE monitor_tape_not_bootable
    CMP.q #1, @MON_TAPE_HEADER+8
    BNE monitor_tape_not_bootable
    MOV.q @MON_TAPE_HEADER+16, R21
    MOV.q @MON_TAPE_HEADER+24, R22
    MOV.q @MON_TAPE_HEADER+32, R23
    MOV.q @MON_TAPE_HEADER+40, R24
    MOV.q @MON_RAM_SIZE, R29
    CMP.q #0x80000, R21
    BLO monitor_tape_bad_address
    TST.q R23
    BEQ monitor_tape_bad_address
    CMP.q R29, R21
    BHS monitor_tape_bad_address
    SUB3.q R21, R29, R0
    CMP.q R0, R23
    BHI monitor_tape_bad_address
    CMP.q R21, R22
    BLO monitor_tape_bad_address
    ADD3.q R21, R23, R0
    CMP.q R0, R22
    BHS monitor_tape_bad_address
    TST.q R24
    BNE monitor_tape_check_stack
    MOV.q R29, R24
monitor_tape_check_stack:
    AND3.q #15, R24, R0
    BNE monitor_tape_bad_address
    CMP.q R29, R24
    BHI monitor_tape_bad_address
    ADD3.q R21, R23, R0
    CMP.q R0, R24
    BLO monitor_tape_bad_address
    MOV.q R23, R25
monitor_tape_validate:
    JSR monitor_tape_next
    AND3.q #12, R28, R0
    BNE monitor_tape_validated
    MOV.q @MON_TAPE+16, R0
    TST.q R0
    BEQ monitor_tape_bad_length
    CMP.q R25, R0
    BHI monitor_tape_bad_length
    SUB.q R0, R25
    BRA monitor_tape_validate
monitor_tape_validated:
    TST.q R25
    BNE monitor_tape_bad_length
    MOV.b #1, @MON_TAPE+8
    JSR monitor_tape_next
    MOV.q R23, R25
    MOV.q R21, R26
monitor_tape_load:
    TST.q R25
    BEQ monitor_tape_loaded
    JSR monitor_tape_next
    AND3.q #12, R28, R0
    BNE monitor_tape_bad_length
    MOV.q @MON_TAPE+16, R0
    SUB.q R0, R25
    MOV.q R26, R2
    FILL.b @MON_TAPE
    MOV.q R2, R26
    BRA monitor_tape_load
monitor_tape_loaded:
    JSR monitor_tape_next
    LEA monitor_tape_boot_text, R8
    JSR basic_puts
    MOV.q R23, R0
    JSR basic_print_number
    LEA monitor_tape_boot_suffix, R8
    JSR basic_puts
    MTSR #0, MMUCTL
    MTSR #0x700, PSR
    MOV.q R24, SP
    MOV.q #65, R0
    CLR.q R1
    IFENCE
    JMP (R22)
monitor_tape_next:
    MOV.b #2, @MON_TAPE+8
    MOVZ.b.q @MON_TAPE+4, R28
    AND3.q #16, R28, R0
    BNE monitor_tape_record_error
    RET
monitor_tape_offline:
    LEA monitor_tape_offline_text, R8
    BRA monitor_tape_failed
monitor_tape_not_bootable:
    LEA monitor_tape_format_text, R8
    BRA monitor_tape_failed
monitor_tape_bad_address:
    LEA monitor_tape_address_text, R8
    BRA monitor_tape_failed
monitor_tape_bad_length:
    LEA monitor_tape_length_text, R8
    BRA monitor_tape_failed
monitor_tape_record_error:
    LEA monitor_tape_error_text, R8
monitor_tape_failed:
    JSR basic_puts
    BRA monitor_prompt
monitor_tape_boot_text: .asciz "Booting tape0 ("
monitor_tape_boot_suffix: .asciz " bytes)...\n"
monitor_tape_offline_text: .asciz "?TAPE0 OFFLINE: mount a reel with -t image\n"
monitor_tape_format_text: .asciz "?TAPE NOT BOOTABLE: expected UPS9BOOT v1 header\n"
monitor_tape_address_text: .asciz "?TAPE BOOT ADDRESS OR STACK OUTSIDE AVAILABLE RAM\n"
monitor_tape_length_text: .asciz "?TAPE BOOT LENGTH MISMATCH\n"
monitor_tape_error_text: .asciz "?TAPE RECORD ERROR\n"
