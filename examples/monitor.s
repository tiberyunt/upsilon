; OK monitor, linked with BASIC and the native assembler by the C ROM linker.
; All commands, editing and directory operations execute on the guest CPU.
.equ MON_UART, 0xffff0000
.equ MON_STACK, 0x100000
.equ MON_BUFFER, 0x40000
.equ MON_NAME, 0x40100
.equ MON_DIAG, 0x40200
.equ MON_SCRATCH, 0x42000
.equ MON_DIRECTORY, 0x50000
.equ MON_SLOT_SIZE, 0x4000
.equ MON_SOURCE, 64
.equ MON_BINARY, 0x2040
.equ MON_ORIGIN, 0x80000
.entry monitor_start
    .quad MON_STACK, monitor_start
monitor_start:
    MOV.q #MON_UART, R16
    CLR.q R11
    MOV.q #MON_DIRECTORY, R6
    MOV.q #8, R7
monitor_clear_directory:
    CLR.b (R6)
    ADD.q #MON_SLOT_SIZE, R6
    SOB R7, monitor_clear_directory
    LEA monitor_banner, R8
    JSR basic_puts
    MOVZ.b.q @0xffff0104, R0
    AND.q #64, R0
    MOVZ.b.q @0xffff0300, R1
    AND.q #16, R1
    OR.q R1, R0
    BEQ monitor_prompt
    LEA monitor_startup_text, R8
    JSR basic_puts
    MOV.q @0xffff0208, R20
    ADD.q #2000, R20
monitor_startup_wait:
    MOVZ.b.q @MON_UART+4, R0
    AND3.q #1, R0, R1
    BNE monitor_startup_key
    AND.q #4, R0
    BNE monitor_startup_boot
    CMP.q @0xffff0208, R20
    BHI monitor_startup_wait
    BRA monitor_startup_boot
monitor_startup_key:
    MOVZ.b.q @MON_UART+5, R0
    CMP.b #65, R0
    BEQ monitor_startup_escape
    CMP.b #97, R0
    BNE monitor_startup_boot
    TST.b @0xffff0210
    BEQ monitor_startup_boot
monitor_startup_escape:
    MOV.b @MON_UART, R0
    BRA monitor_prompt
monitor_startup_boot:
    MOVZ.b.q @0xffff0300, R0
    AND.q #16, R0
    BEQ monitor_startup_tape
    MOV.q #1, R27
    CLR.q R29
    BRA monitor_disk_boot
monitor_startup_tape:
    MOVZ.b.q @0xffff0104, R0
    AND.q #64, R0
    BNE monitor_tape_boot
monitor_prompt:
    MOV.q #MON_STACK, SP
    MOV.q #MON_UART, R16
    LEA monitor_prompt_text, R8
    JSR basic_puts
    JSR monitor_read_line
    CMP.q #-1, R0
    BEQ monitor_shutdown
    CMP.q #-2, R0
    BEQ monitor_prompt
    MOV.q #MON_BUFFER, R20
    JSR basic_spaces
    TST.b R1
    BEQ monitor_prompt
    LEA monitor_kw_boot, R8
    JSR basic_keyword
    TST.q R0
    BNE monitor_boot
    LEA monitor_kw_basic, R8
    JSR basic_keyword
    TST.q R0
    BNE monitor_basic
    LEA monitor_kw_shutdown, R8
    JSR basic_keyword
    TST.q R0
    BNE monitor_shutdown_command
    LEA monitor_kw_edit, R8
    JSR basic_keyword
    TST.q R0
    BNE monitor_edit
    LEA monitor_kw_asm, R8
    JSR basic_keyword
    TST.q R0
    BNE monitor_asm
    LEA monitor_kw_files, R8
    JSR basic_keyword
    TST.q R0
    BNE monitor_files
    LEA monitor_kw_type, R8
    JSR basic_keyword
    TST.q R0
    BNE monitor_type
    LEA monitor_kw_help, R8
    JSR basic_keyword
    TST.q R0
    BEQ monitor_unknown
    JSR monitor_expect_end
    LEA monitor_help, R8
    JSR basic_puts
    BRA monitor_prompt
monitor_basic:
    JSR monitor_expect_end
    JMP basic_start
monitor_shutdown_command:
    JSR monitor_expect_end
monitor_shutdown:
    LEA monitor_shutdown_text, R8
    JSR basic_puts
    HALT
monitor_boot:
    JSR monitor_filename
    MOV.q #MON_NAME, R20
    LEA monitor_disk_prefix, R8
monitor_boot_disk_prefix:
    MOV.b (R8)+, R1
    BEQ monitor_boot_disk_number
    MOV.b (R20)+, R0
    OR.b #32, R0
    CMP.b R1, R0
    BNE monitor_boot_tape
    BRA monitor_boot_disk_prefix
monitor_boot_disk_number:
    CLR.q R29
    MOV.b (R20), R0
    TST.b R0
    BEQ monitor_boot_bad_disk
monitor_boot_disk_digit:
    MOVZ.b.q (R20)+, R0
    TST.b R0
    BEQ monitor_boot_disk
    CMP.b #'0', R0
    BLO monitor_boot_bad_disk
    CMP.b #'9', R0
    BHI monitor_boot_bad_disk
    MUL.q #10, R29
    SUB.q #'0', R0
    ADD.q R0, R29
    CMP.q #3, R29
    BHI monitor_boot_bad_disk
    BRA monitor_boot_disk_digit
monitor_boot_bad_disk:
    LEA monitor_disk_number_error, R8
    JSR basic_puts
    BRA monitor_prompt
monitor_boot_tape:
    MOV.q #MON_NAME, R20
    LEA monitor_kw_tape0, R8
    JSR basic_keyword
    TST.q R0
    BNE monitor_tape_boot
    LEA monitor_kw_tape, R8
    JSR basic_keyword
    TST.q R0
    BNE monitor_tape_boot
    LEA monitor_kw_mt0, R8
    JSR basic_keyword
    TST.q R0
    BNE monitor_tape_boot
    LEA monitor_boot_text, R8
    JSR basic_puts
    MOV.q #MON_NAME, R8
    JSR basic_puts
    LEA monitor_no_devices, R8
    JSR basic_puts
    BRA monitor_prompt

monitor_boot_disk:
    CLR.q R27
    BRA monitor_disk_boot

; Parse one 1..31 character RAM name: letters, digits, dot, underscore, dash.
monitor_filename:
    JSR basic_spaces
    MOV.q #MON_NAME, R9
    CLR.q R10
monitor_name_char:
    MOVZ.b.q (R20), R0
    TST.b R0
    BEQ monitor_name_done
    CMP.b #' ', R0
    BEQ monitor_name_done
    CMP.b #9, R0
    BEQ monitor_name_done
    CMP.b #'a', R0
    BLO monitor_name_upper
    CMP.b #'z', R0
    BLE monitor_name_valid
monitor_name_upper:
    CMP.b #'A', R0
    BLO monitor_name_digit
    CMP.b #'Z', R0
    BLE monitor_name_valid
monitor_name_digit:
    CMP.b #'0', R0
    BLO monitor_name_punctuation
    CMP.b #'9', R0
    BLE monitor_name_valid
monitor_name_punctuation:
    CMP.b #'.', R0
    BEQ monitor_name_valid
    CMP.b #'_', R0
    BEQ monitor_name_valid
    CMP.b #'-', R0
    BEQ monitor_name_valid
    CMP.b #':', R0
    BNE monitor_bad_name
monitor_name_valid:
    CMP.q #31, R10
    BGE monitor_bad_name
    MOV.b R0, (R9)+
    INC.q R10
    INC.q R20
    BRA monitor_name_char
monitor_name_done:
    TST.q R10
    BEQ monitor_bad_name
    CLR.b (R9)
    JSR monitor_expect_end
    RET
monitor_expect_end:
    JSR basic_spaces
    TST.b R1
    BNE monitor_arguments
    RET

; R0 = matching slot or 0, R1 = first free slot or 0. Names are case sensitive.
monitor_find:
    MOV.q #MON_DIRECTORY, R6
    MOV.q #8, R7
    CLR.q R18
monitor_find_next:
    TST.b (R6)
    BNE monitor_compare_name
    TST.q R18
    BNE monitor_find_advance
    MOV.q R6, R18
    BRA monitor_find_advance
monitor_compare_name:
    MOV.q R6, R8
    MOV.q #MON_NAME, R9
monitor_compare_char:
    MOVZ.b.q (R8)+, R2
    MOVZ.b.q (R9)+, R3
    CMP.b R2, R3
    BNE monitor_find_advance
    TST.b R2
    BNE monitor_compare_char
    MOV.q R6, R0
    MOV.q R18, R1
    RET
monitor_find_advance:
    ADD.q #MON_SLOT_SIZE, R6
    SOB R7, monitor_find_next
    CLR.q R0
    MOV.q R18, R1
    RET
monitor_type:
    JSR monitor_filename
    JSR monitor_find
    TST.q R0
    BEQ monitor_not_found
    LEA MON_SOURCE(R0), R8
    JSR basic_puts
    BRA monitor_prompt
monitor_files:
    JSR monitor_expect_end
    LEA monitor_files_header, R8
    JSR basic_puts
    MOV.q #MON_DIRECTORY, R21
    MOV.q #8, R22
monitor_files_next:
    TST.b (R21)
    BEQ monitor_files_advance
    MOV.q R21, R8
    JSR basic_puts
    LEA monitor_source_label, R8
    JSR basic_puts
    MOV.q 32(R21), R0
    JSR basic_print_number
    LEA monitor_binary_label, R8
    JSR basic_puts
    MOV.q 40(R21), R0
    JSR basic_print_number
    MOV.b #10, R0
    JSR basic_putchar
monitor_files_advance:
    ADD.q #MON_SLOT_SIZE, R21
    SOB R22, monitor_files_next
    BRA monitor_prompt

monitor_edit:
    JSR monitor_filename
    JSR monitor_find
    MOV.q R0, R21
    TST.q R0
    BNE monitor_edit_existing
    MOV.q R1, R21
    TST.q R21
    BEQ monitor_directory_full
    BRA monitor_edit_begin
monitor_edit_existing:
    LEA monitor_current, R8
    JSR basic_puts
    LEA MON_SOURCE(R21), R8
    JSR basic_puts
monitor_edit_begin:
    LEA monitor_edit_text, R8
    JSR basic_puts
    CLR.q R22
    CLR.b @MON_SCRATCH
monitor_edit_line:
    LEA monitor_edit_prompt, R8
    JSR basic_puts
    JSR monitor_read_line
    CMP.q #-1, R0
    BEQ monitor_shutdown
    CMP.q #-2, R0
    BEQ monitor_edit_line
    MOV.q R0, R23
    MOV.q #MON_BUFFER, R20
    LEA monitor_kw_save, R8
    JSR basic_keyword
    TST.q R0
    BNE monitor_save
    LEA monitor_kw_cancel, R8
    JSR basic_keyword
    TST.q R0
    BNE monitor_cancel
    ADD3.q R22, R23, R0
    INC.q R0
    CMP.q #8191, R0
    BGT monitor_file_full
    MOV.q #MON_BUFFER, R6
    ADD3.q #MON_SCRATCH, R22, R7
monitor_append_char:
    MOV.b (R6)+, R0
    BEQ monitor_append_newline
    MOV.b R0, (R7)+
    BRA monitor_append_char
monitor_append_newline:
    MOV.b #10, (R7)+
    CLR.b (R7)
    ADD.q R23, R22
    INC.q R22
    BRA monitor_edit_line
monitor_file_full:
    LEA monitor_file_full_text, R8
    JSR basic_puts
    BRA monitor_edit_line
monitor_cancel:
    JSR monitor_expect_end
    LEA monitor_cancel_text, R8
    JSR basic_puts
    BRA monitor_prompt
monitor_save:
    JSR monitor_expect_end
    MOV.q #MON_NAME, R6
    MOV.q R21, R7
monitor_save_name:
    MOV.b (R6)+, R0
    MOV.b R0, (R7)+
    BNE monitor_save_name
    MOV.q #MON_SCRATCH, R6
    LEA MON_SOURCE(R21), R7
monitor_save_source:
    MOV.b (R6)+, R0
    MOV.b R0, (R7)+
    BNE monitor_save_source
    MOV.q R22, 32(R21)
    CLR.q 40(R21)
    CLR.q 48(R21)
    LEA monitor_saved_text, R8
    JSR basic_puts
    MOV.q R22, R0
    JSR basic_print_number
    LEA monitor_bytes_text, R8
    JSR basic_puts
    BRA monitor_prompt
monitor_asm:
    JSR monitor_filename
    JSR monitor_find
    TST.q R0
    BEQ monitor_not_found
    MOV.q R0, R21
    LEA MON_SOURCE(R21), R0
    MOV.q 32(R21), R1
    LEA MON_BINARY(R21), R2
    MOV.q #8128, R3
    MOV.q #MON_DIAG, R4
    MOV.q #256, R5
    MOV.q #MON_ORIGIN, R6
    JSR na_assemble
    TST.q R0
    BNE monitor_assembly_error
    MOV.q R1, 40(R21)
    MOV.q R2, 48(R21)
    LEA monitor_assembled_text, R8
    JSR basic_puts
    MOV.q 40(R21), R0
    JSR basic_print_number
    LEA monitor_entry_text, R8
    JSR basic_puts
    MOV.q 48(R21), R0
    JSR basic_print_number
    MOV.b #10, R0
    JSR basic_putchar
    BRA monitor_prompt
monitor_assembly_error:
    LEA monitor_asm_error_text, R8
    JSR basic_puts
    MOV.q #MON_DIAG, R8
    JSR basic_puts
    MOV.b #10, R0
    JSR basic_putchar
    BRA monitor_prompt

; Shared UART with BASIC, independent command buffer. R0=len, -1=EOF, -2=long.
monitor_read_line:
    MOV.q #MON_BUFFER, R9
    CLR.q R10
    CLR.q R12
monitor_read_key:
    JSR basic_getchar
    CMP.b #10, R0
    BNE monitor_not_lf
    TST.q R11
    BEQ monitor_line_end
    CLR.q R11
    BRA monitor_read_key
monitor_not_lf:
    CLR.q R11
    CMP.b #13, R0
    BNE monitor_not_cr
    MOV.q #1, R11
    BRA monitor_line_end
monitor_not_cr:
    CMP.b #4, R0
    BNE monitor_not_eof
    TST.q R10
    BNE monitor_line_end
    MOV.q #-1, R0
    RET
monitor_not_eof:
    CMP.b #8, R0
    BEQ monitor_backspace
    CMP.b #127, R0
    BEQ monitor_backspace
    CMP.b #9, R0
    BEQ monitor_printable
    CMP.b #32, R0
    BLO monitor_read_key
    CMP.b #126, R0
    BHI monitor_read_key
monitor_printable:
    CMP.q #120, R10
    BGE monitor_too_long
    MOV.b R0, (R9)+
    INC.q R10
    JSR basic_putchar
    BRA monitor_read_key
monitor_backspace:
    TST.q R10
    BEQ monitor_read_key
    DEC.q R10
    DEC.q R9
    MOV.b #8, R0
    JSR basic_putchar
    MOV.b #' ', R0
    JSR basic_putchar
    MOV.b #8, R0
    JSR basic_putchar
    BRA monitor_read_key
monitor_too_long:
    MOV.q #1, R12
    BRA monitor_read_key
monitor_line_end:
    CLR.b (R9)
    MOV.b #10, R0
    JSR basic_putchar
    TST.q R12
    BNE monitor_long_line
    MOV.q R10, R0
    RET
monitor_long_line:
    LEA monitor_long_text, R8
    JSR basic_puts
    MOV.q #-2, R0
    RET
monitor_unknown:
    LEA monitor_unknown_text, R8
    BRA monitor_error
monitor_arguments:
    LEA monitor_arguments_text, R8
    BRA monitor_error
monitor_bad_name:
    LEA monitor_bad_name_text, R8
    BRA monitor_error
monitor_not_found:
    LEA monitor_not_found_text, R8
    BRA monitor_error
monitor_directory_full:
    LEA monitor_directory_full_text, R8
monitor_error:
    MOV.q #MON_STACK, SP
    JSR basic_puts
    BRA monitor_prompt

monitor_banner: .asciz "KotlTOS firmware for Upsilon v0.4\nType help for commands. Monitor RAM files are lost on reset.\n"
monitor_prompt_text: .asciz "OK> "
monitor_help: .asciz "boot diskN or tape0 | basic | shutdown | edit filename | asm filename\nfiles | type filename | help\n"
monitor_shutdown_text: .asciz "Shutdown.\n"
monitor_boot_text: .asciz "BOOT "
monitor_no_devices: .asciz ": unknown boot device; use boot diskN or boot tape0.\n"
monitor_files_header: .asciz "RAM files (8 slots):\n"
monitor_source_label: .asciz " source="
monitor_binary_label: .asciz " binary="
monitor_current: .asciz "Current contents:\n"
monitor_edit_text: .asciz "Enter replacement text. .save saves; .cancel discards.\n"
monitor_edit_prompt: .asciz "edit> "
monitor_cancel_text: .asciz "Cancelled; RAM file unchanged.\n"
monitor_saved_text: .asciz "Saved "
monitor_bytes_text: .asciz " bytes in RAM.\n"
monitor_file_full_text: .asciz "?FILE FULL (8191 bytes); line ignored. Use .save or .cancel.\n"
monitor_assembled_text: .asciz "Assembled "
monitor_entry_text: .asciz " bytes in RAM; entry="
monitor_asm_error_text: .asciz "?ASSEMBLY ERROR: "
monitor_unknown_text: .asciz "?UNKNOWN COMMAND; type help.\n"
monitor_arguments_text: .asciz "?UNEXPECTED ARGUMENTS\n"
monitor_bad_name_text: .asciz "?NAME: 1..31 letters, digits, dot, underscore or dash\n"
monitor_not_found_text: .asciz "?FILE NOT FOUND\n"
monitor_directory_full_text: .asciz "?RAM DIRECTORY FULL (8 files)\n"
monitor_long_text: .asciz "?LINE TOO LONG (120 characters)\n"
monitor_kw_boot: .asciz "BOOT"
monitor_kw_basic: .asciz "BASIC"
monitor_kw_shutdown: .asciz "SHUTDOWN"
monitor_kw_edit: .asciz "EDIT"
monitor_kw_asm: .asciz "ASM"
monitor_kw_files: .asciz "FILES"
monitor_kw_type: .asciz "TYPE"
monitor_kw_help: .asciz "HELP"
monitor_kw_save: .asciz ".SAVE"
monitor_kw_cancel: .asciz ".CANCEL"

monitor_kw_tape0: .asciz "TAPE0"
monitor_kw_tape: .asciz "TAPE"
monitor_kw_mt0: .asciz "MT0"
monitor_startup_text: .asciz "Press A within 2 seconds for OK; otherwise boot disks, then tape0.\n"
monitor_disk_prefix: .asciz "disk"
monitor_disk_number_error: .asciz "?DISK NUMBER: use boot disk0 through boot disk3\n"
.include "tape_boot.inc"
.include "disk_boot.inc"
