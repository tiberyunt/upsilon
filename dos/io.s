.include "layout.inc"
.entry io_start
io_start:
    MOV.q R0, @D_BOOTDRIVE
    MOV.q R1, @D_BOOTUNIT
    JSR fs_mount
    MOV.q #256, R0
    MOV.q #D_VBR, R2
    FILL.q #io_fault
    MOV.q #io_syscall, @D_VBR+96
    MTSR #D_VBR, VBR
    MOV.q @0xffff0200, R0
    SUB.q #65536, R0
    MTSR R0, ESP
    LEA io_banner, R0
    JSR u_puts
    MOV.b @D_CWD, io_command
    LEA io_command, R0
    MOV.q #D_COMMAND, R1
    MOV.q #65536, R2
    JSR fs_read
    CMP.q #0, R0
    BLE io_no_command
    CMP.q #1, R1
    BNE io_no_command
    CMP.q #D_COMMAND, R2
    BNE io_no_command
    IFENCE
    JMP (R2)
io_no_command:
    LEA io_no_command_text, R0
    JSR u_puts
    HALT
; DOS INT 21h analogue: SYSCALL #33, function in R14. R16..R29 preserved.
io_syscall:
    PUSHM #0x3fff
    MFSR FAULTINFO, R15
    CMP.q #33, R15
    BNE io_bad_service
    CASE R14, #0, #20
io_service_table:
    .long io_s_puts-io_service_table
    .long io_s_line-io_service_table
    .long io_s_dir-io_service_table
    .long io_s_read-io_service_table
    .long io_s_write-io_service_table
    .long io_s_delete-io_service_table
    .long io_s_rename-io_service_table
    .long io_s_vi-io_service_table
    .long io_s_asm-io_service_table
    .long io_s_link-io_service_table
    .long io_s_exec-io_service_table
    .long io_s_mkdir-io_service_table
    .long io_s_chdir-io_service_table
    .long io_s_cwd-io_service_table
    .long io_s_shutdown-io_service_table
    .long io_s_exit-io_service_table
    .long io_s_type-io_service_table
    .long io_s_putc-io_service_table
    .long io_s_version-io_service_table
    .long io_s_format-io_service_table
    .long io_s_syscopy-io_service_table
    BRA io_bad_service
io_s_puts:
    JSR u_puts
    CLR.q R0
    BRA io_service_return
io_s_line:
    JSR u_line
    BRA io_service_return
io_s_dir:
    JSR io_dir
    BRA io_service_return
io_s_read:
    JSR fs_read
    BRA io_service_return
io_s_write:
    JSR fs_write
    BRA io_service_return
io_s_delete:
    JSR dos_delete
    BRA io_service_return
io_s_rename:
    JSR fs_rename
    BRA io_service_return
io_s_vi:
    JSR vi_main
    BRA io_service_return
io_s_asm:
    JSR tool_asm
    BRA io_service_return
io_s_link:
    JSR tool_link
    BRA io_service_return
io_s_exec:
    JSR io_exec
    BRA io_service_return
io_s_mkdir:
    CLR.q R1
    CLR.q R2
    MOV.q #3, R3
    CLR.q R4
    JSR fs_write
    BRA io_service_return
io_s_chdir:
    JSR fs_path
    TST.q R0
    BNE io_service_return
    TST.b @D_PATH+3
    BNE io_chdir_find
    CMP.b #67, @D_PATH
    BNE io_chdir_copy
    TST.q @D_CREADY
    BNE io_chdir_copy
    BRA io_chdir_missing
io_chdir_find:
    MOV.q #D_PATH, R0
    JSR fs_find
    TST.q R0
    BEQ io_chdir_missing
    CMP.q #3, 72(R0)
    BNE io_chdir_missing
io_chdir_copy:
    MOV.q #D_PATH, R0
    MOV.q #D_CWD, R1
    MOV.q #64, R2
    JSR u_copystr
    CLR.q R0
    BRA io_service_return
io_chdir_missing:
    MOV.q #-4, R0
    BRA io_service_return
io_s_cwd:
    MOV.q R0, R1
    MOV.q #D_CWD, R0
    MOV.q #64, R2
    JSR u_copystr
    CLR.q R0
    BRA io_service_return
io_s_shutdown:
    LEA io_shutdown, R0
    JSR u_puts
    HALT
io_s_exit:
    TST.q @D_EXECSP
    BNE io_exec_abort
    BRA io_s_shutdown
io_s_type:
    JSR io_type
    BRA io_service_return
io_s_putc:
    JSR u_putc
    CLR.q R0
    BRA io_service_return
io_s_version:
    LEA io_banner, R0
    JSR u_puts
    CLR.q R0
    BRA io_service_return
io_s_format:
    JSR hd_format
    BRA io_service_return
io_s_syscopy:
    JSR hd_syscopy
    BRA io_service_return
io_bad_service:
    MOV.q #-10, R0
io_service_return:
    POPM #0x3fff
    RTE
io_type:
    JSR fs_path
    TST.q R0
    BNE io_type_return
    MOV.q #D_PATH, R0
    JSR fs_find
    TST.q R0
    BEQ io_type_missing
    CMP.q #3, 72(R0)
    BEQ io_type_missing
    MOV.q 64(R0), R17
    LEA D_FILEDATA(R0), R16
io_type_next:
    TST.q R17
    BEQ io_type_ok
    MOV.b (R16)+, @D_UART
    SOB R17, io_type_next
io_type_ok:
    CLR.q R0
io_type_return:
    RET
io_type_missing:
    MOV.q #-1, R0
    RET
io_dir:
    JMP dos_dir
; Load a native executable at its fixed application origin, then call its entry.
; R0=name, R1=command tail. Exit or fault unwinds to this service's caller.
io_exec:
    TST.q @D_EXECSP
    BNE io_exec_busy
    MOV.q R0, R16
    MOV.q R1, R0
    MOV.q #D_ARG, R1
    MOV.q #1024, R2
    JSR u_copystr
    MOV.q R16, R0
    MOV.q #D_APP, R1
    MOV.q #65536, R2
    JSR fs_read
    CMP.q #0, R0
    BLE io_exec_return
    CMP.q #1, R1
    BNE io_exec_bad
    CMP.q #D_APP, R2
    BLO io_exec_bad
    ADD.q #D_APP, R0
    CMP.q R0, R2
    BHS io_exec_bad
    MOV.q SP, @D_EXECSP
    MOV.q #D_ARG, R0
    IFENCE
    JSR (R2)
    CLR.q @D_EXECSP
io_exec_return:
    RET
io_exec_bad:
    MOV.q #-11, R0
    RET
io_exec_busy:
    MOV.q #-12, R0
    RET
io_exec_abort:
    MOV.q @D_EXECSP, SP
    CLR.q @D_EXECSP
    RET
io_fault:
    MFSR CAUSE, R16
    LEA io_fault_text, R0
    JSR u_puts
    MOV.q R16, R0
    JSR u_number
    JSR u_newline
    TST.q @D_EXECSP
    BEQ io_fault_fatal
    MOV.q #-9, R0
    BRA io_exec_abort
io_fault_fatal:
    HALT
io_banner: .asciz "KotlTOS 0.2 - native Upsilon operating system\n"
io_command: .asciz "A:\\COMMAND.EXE"
io_no_command_text: .asciz "Cannot load COMMAND.EXE. System halted.\n"
io_shutdown: .asciz "KotlTOS halted. Files are saved on their volumes.\n"
io_directory: .asciz "Directory of "
io_spaces: .asciz "   "
io_dir_label: .asciz "<DIR>"
io_files: .asciz " file(s)\n"
io_fault_text: .asciz "Program fault, vector "
.include "console.inc"
.include "filesystem.inc"
.include "wildcards.inc"
.include "editor.inc"
.include "tools.inc"
.include "../examples/native_assembler.s"
