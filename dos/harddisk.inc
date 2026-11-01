; Disk C: uses the same guest journal events, addressed through block DMA.
; Superblock at 0x20000 publishes the durable end of the journal at 0x21000.
hd_read:
    JSR hd_controller
    MOV.q R0, 16(R15)
    JSR hd_controller
    MOV.q R1, 24(R15)
    JSR hd_controller
    MOV.q R2, 32(R15)
    JSR hd_controller
    MOV.b #1, 8(R15)
    BRA hd_status
hd_write:
    JSR hd_controller
    MOV.q R0, 16(R15)
    JSR hd_controller
    MOV.q R1, 24(R15)
    JSR hd_controller
    MOV.q R2, 32(R15)
    JSR hd_controller
    MOV.b #2, 8(R15)
hd_status:
    JSR hd_controller
    MOVZ.b.q 0(R15), R0
    AND.q #4, R0
    BEQ hd_status_ok
    MOV.q #-7, R0
    RET
hd_status_ok:
    CLR.q R0
    RET
hd_mount:
    PUSHM #0x3fff
    JSR hd_controller
    MOVZ.b.q 0(R15), R0
    AND.q #1, R0
    BEQ hd_mount_return
    MOV.q #D_VOL_OFFSET, R0
    MOV.q #D_SUPER, R1
    MOV.q #512, R2
    JSR hd_read
    TST.q R0
    BNE hd_mount_bad
    CMP.q #D_VOL_MAGIC, @D_SUPER
    BNE hd_mount_return
    CMP.q #1, @D_SUPER+8
    BNE hd_mount_bad
    MOV.q @D_SUPER+32, R18
    CLR.q @D_SUPER+32
    MOV.q #D_SUPER, R0
    MOV.q #40, R1
    JSR fs_hash
    CMP.q R18, R0
    BNE hd_mount_bad
    MOV.q @D_SUPER+16, R25
    MOV.q @D_SUPER+24, R26
    CMP.q #D_LOG_START, R25
    BLO hd_mount_bad
    JSR hd_controller
    CMP.q 40(R15), R25
    BHI hd_mount_bad
    MOV.q R25, @D_CTAIL
    MOV.q #1, @D_CREADY
    JSR hd_controller
    MOVZ.b.q 0(R15), R0
    AND.q #2, R0
    MOV.q R0, @D_CRO
    MOV.q #D_LOG_START, R24
hd_mount_next:
    CMP.q R25, R24
    BEQ hd_mount_end
    ADD3.q #192, R24, R0
    CMP.q R25, R0
    BHI hd_mount_bad
    MOV.q R24, R0
    MOV.q #D_RECOVER, R1
    MOV.q #192, R2
    JSR hd_read
    TST.q R0
    BNE hd_mount_bad
    MOV.q @D_RECOVER+152, R17
    CMP.q #D_MAXFILE, R17
    BHI hd_mount_bad
    ADD.q #192, R17
    ADD3.q R24, R17, R0
    CMP.q R25, R0
    BHI hd_mount_bad
    MOV.q @D_RECOVER+152, R2
    TST.q R2
    BEQ hd_mount_check
    ADD3.q #192, R24, R0
    MOV.q #D_RECOVER+192, R1
    JSR hd_read
    TST.q R0
    BNE hd_mount_bad
hd_mount_check:
    JMP fs_mount_check
hd_mount_applied:
    MOV.q @D_RECOVER+8, @D_CSEQ
    ADD.q R17, R24
    BRA hd_mount_next
hd_mount_end:
    CMP.q @D_CSEQ, R26
    BNE hd_mount_bad
    BRA hd_mount_return
hd_mount_bad:
    MOV.q #1, @D_CRO
    MOV.q #1, @D_CFAILED
    LEA hd_bad_text, R0
    JSR u_puts
hd_mount_return:
    CLR.q R0
    POPM #0x3fff
    RET
; Persist a canonical C: event, then publish its new tail. Orphans are ignored.
hd_commit:
    PUSHM #0x3fff
    TST.q @D_CREADY
    BEQ hd_commit_missing
    TST.q @D_CRO
    BNE hd_commit_ro
    JSR hd_controller
    MOVZ.b.q 0(R15), R0
    AND.q #2, R0
    BNE hd_commit_ro
    MOV.q @D_JOURNAL+152, R17
    ADD.q #192, R17
    ADD3.q @D_CTAIL, R17, R18
    JSR hd_controller
    CMP.q 40(R15), R18
    BHI hd_commit_io
    ADD3.q #1, @D_CSEQ, @D_JOURNAL+8
    CLR.q @D_JOURNAL+176
    MOV.q #D_JOURNAL, R0
    MOV.q R17, R1
    JSR fs_hash
    MOV.q R0, @D_JOURNAL+176
    MOV.q @D_CTAIL, R0
    MOV.q #D_JOURNAL, R1
    MOV.q R17, R2
    JSR hd_write
    TST.q R0
    BNE hd_commit_return
    MOV.q #512, R0
    MOV.q #D_SUPER, R2
    FILL.b #0
    MOV.q #D_VOL_MAGIC, @D_SUPER
    MOV.q #1, @D_SUPER+8
    MOV.q R18, @D_SUPER+16
    ADD3.q #1, @D_CSEQ, @D_SUPER+24
    MOV.q #D_SUPER, R0
    MOV.q #40, R1
    JSR fs_hash
    MOV.q R0, @D_SUPER+32
    MOV.q #D_VOL_OFFSET, R0
    MOV.q #D_SUPER, R1
    MOV.q #512, R2
    JSR hd_write
    TST.q R0
    BNE hd_commit_return
    MOV.q #D_JOURNAL, R0
    JSR fs_apply
    TST.q R0
    BNE hd_commit_return
    MOV.q R18, @D_CTAIL
    INC.q @D_CSEQ
    BRA hd_commit_return
hd_commit_missing:
    MOV.q #-4, R0
    BRA hd_commit_return
hd_commit_ro:
    MOV.q #-6, R0
    BRA hd_commit_return
hd_commit_io:
    MOV.q #-7, R0
hd_commit_return:
    POPM #0x3fff
    RET
; FORMAT accepts C or C: (optionally parentheses), never formats the boot tape.
hd_drive_arg:
    MOV.q R0, R4
    CMP.b #'(', (R4)
    BNE hd_drive_letter
    INC.q R4
hd_drive_letter:
    MOV.b (R4)+, R0
    OR.b #32, R0
    CMP.b #'c', R0
    BNE hd_drive_bad
    CMP.b #':', (R4)
    BNE hd_drive_close
    INC.q R4
hd_drive_close:
    CMP.b #')', (R4)
    BNE hd_drive_end
    INC.q R4
hd_drive_end:
    TST.b (R4)
    BNE hd_drive_bad
    CLR.q R0
    RET
hd_drive_bad:
    MOV.q #-3, R0
    RET
hd_format:
    PUSHM #0x3fff
    JSR hd_drive_arg
    TST.q R0
    BNE hd_format_return
    JSR hd_controller
    MOVZ.b.q 0(R15), R0
    AND3.q #1, R0, R1
    BEQ hd_format_missing
    AND.q #2, R0
    BNE hd_format_ro
    JSR hd_controller
    CMP.q #1048576, 40(R15)
    BLO hd_format_missing
    MOV.q #0x20200, R0
    MOV.q #D_FORMAT, R2
    FILL.b #0
    MOV.q #D_VOL_MAGIC, @D_FORMAT+D_VOL_OFFSET
    MOV.q #1, @D_FORMAT+D_VOL_OFFSET+8
    MOV.q #D_LOG_START, @D_FORMAT+D_VOL_OFFSET+16
    MOV.q #D_FORMAT+D_VOL_OFFSET, R0
    MOV.q #40, R1
    JSR fs_hash
    MOV.q R0, @D_FORMAT+D_VOL_OFFSET+32
    CLR.q R0
    MOV.q #D_FORMAT, R1
    MOV.q #0x20200, R2
    JSR hd_write
    TST.q R0
    BNE hd_format_return
    MOV.q #D_FILES, R16
    MOV.q #D_SLOTS, R17
hd_format_clear:
    CMP.b #67, (R16)
    BNE hd_format_next
    CLR.b (R16)
hd_format_next:
    ADD.q #D_SLOT, R16
    SOB R17, hd_format_clear
    MOV.q #1, @D_CREADY
    CLR.q @D_CSEQ
    CLR.q @D_CRO
    CLR.q @D_CFAILED
    MOV.q #D_LOG_START, @D_CTAIL
    CMP.b #67, @D_CWD
    BNE hd_format_print
    MOV.l #0x005c3a43, @D_CWD
hd_format_print:
    LEA hd_formatted, R0
    JSR u_puts
    CLR.q R0
    BRA hd_format_return
hd_format_missing:
    MOV.q #-4, R0
    BRA hd_format_return
hd_format_ro:
    MOV.q #-6, R0
hd_format_return:
    POPM #0x3fff
    RET
.equ H_SRC, D_STATE+3456
.equ H_DEST, D_STATE+3520
.equ H_ARG, D_STATE+3584
; SYSCOPY C: or SYSCOPY A: C:. Destination is C:; source defaults to boot drive.
hd_syscopy:
    PUSHM #0x3fff
    MOV.q R0, R20
    MOV.q #H_ARG, R0
    JSR u_token
    CMP.q #0, R0
    BLE hd_syscopy_usage
    MOV.q #H_DEST, R0
    JSR u_token
    CMP.q #-1, R0
    BEQ hd_syscopy_usage
    TST.q R0
    BNE hd_syscopy_source
    MOV.q @D_BOOTDRIVE, R24
    MOV.q #H_ARG, R0
    BRA hd_syscopy_target
hd_syscopy_source:
    MOV.b @H_ARG, R24
    AND.q #0xdf, R24
    CMP.q #65, R24
    BEQ hd_syscopy_source_end
    CMP.q #67, R24
    BNE hd_syscopy_usage
hd_syscopy_source_end:
    CMP.b #':', @H_ARG+1
    BNE hd_syscopy_usage
    TST.b @H_ARG+2
    BNE hd_syscopy_usage
    MOV.q #H_DEST, R0
hd_syscopy_target:
    JSR hd_drive_arg
    TST.q R0
    BNE hd_syscopy_usage
    TST.q @D_CREADY
    BEQ hd_syscopy_unformatted
    TST.q @D_CRO
    BNE hd_syscopy_ro
    MOV.q #hd_system_files, R21
    CLR.q R23
; Preflight all source files before invalidating any existing disk boot sector.
hd_syscopy_preflight:
    TST.b (R21)
    BEQ hd_syscopy_begin
    MOV.q R21, R0
    JSR hd_names
    MOV.q #H_SRC, R0
    MOV.q #D_TEMP, R1
    MOV.q #65536, R2
    JSR fs_read
    CMP.q #0, R0
    BLT hd_syscopy_return
    CMP.q #2, R23
    BEQ hd_syscopy_check_text
    CMP.q #7, R23
    BEQ hd_syscopy_check_boot
    CMP.q #0, R0
    BLE hd_syscopy_invalid
    CMP.q #1, R1
    BNE hd_syscopy_invalid
    CMP.q #0, R23
    BNE hd_syscopy_check_command
    CMP.q #0x100000, R2
    BNE hd_syscopy_invalid
    BRA hd_syscopy_checked
hd_syscopy_check_command:
    CMP.q #1, R23
    BNE hd_syscopy_check_program
    CMP.q #D_COMMAND, R2
    BNE hd_syscopy_invalid
    BRA hd_syscopy_checked
hd_syscopy_check_program:
    CMP.q #D_APP, R2
    BLO hd_syscopy_invalid
    ADD.q #D_APP, R0
    CMP.q R0, R2
    BHS hd_syscopy_invalid
    BRA hd_syscopy_checked
hd_syscopy_check_boot:
    CMP.q #0, R0
    BLE hd_syscopy_invalid
hd_syscopy_check_text:
    TST.q R1
    BNE hd_syscopy_invalid
hd_syscopy_checked:
    INC.q R23
    JSR hd_next_name
    BRA hd_syscopy_preflight
hd_syscopy_begin:
    MOV.q #512, R0
    MOV.q #D_SUPER, R2
    FILL.b #0
    CLR.q R0
    MOV.q #D_SUPER, R1
    MOV.q #512, R2
    JSR hd_write
    TST.q R0
    BNE hd_syscopy_return
    MOV.q #hd_system_files, R21
hd_syscopy_file:
    TST.b (R21)
    BEQ hd_syscopy_boot
    MOV.q R21, R0
    JSR hd_names
    MOV.q #H_SRC, R0
    MOV.q #D_TEMP, R1
    MOV.q #65536, R2
    JSR fs_read
    CMP.q #0, R0
    BLT hd_syscopy_return
    MOV.q R0, R16
    MOV.q R1, R3
    MOV.q R2, R4
    MOV.q #H_DEST, R0
    MOV.q #D_TEMP, R1
    MOV.q R16, R2
    JSR fs_write
    TST.q R0
    BNE hd_syscopy_return
    JSR hd_next_name
    BRA hd_syscopy_file
hd_syscopy_boot:
    LEA hd_boot_file, R0
    JSR hd_names
    MOV.q #H_SRC, R0
    MOV.q #D_TEMP, R1
    MOV.q #65536, R2
    JSR fs_read
    CMP.q #0, R0
    BLE hd_syscopy_return
    MOV.q R0, R25
    MOV.q R0, R1
    MOV.q #D_TEMP, R0
    JSR fs_hash
    MOV.q R0, R26
    MOV.q #4096, R0
    MOV.q #D_TEMP, R1
    MOV.q R25, R2
    JSR hd_write
    TST.q R0
    BNE hd_syscopy_return
    MOV.q #512, R0
    MOV.q #D_SUPER, R2
    FILL.b #0
    MOV.q #D_BOOT_MAGIC, @D_SUPER
    MOV.q #0x80000, @D_SUPER+8
    MOV.q #0x80000, @D_SUPER+16
    MOV.q R25, @D_SUPER+24
    MOV.q R26, @D_SUPER+32
    CLR.q R0
    MOV.q #D_SUPER, R1
    MOV.q #512, R2
    JSR hd_write
    TST.q R0
    BNE hd_syscopy_return
    LEA hd_system_copied, R0
    JSR u_puts
    CLR.q R0
    BRA hd_syscopy_return
hd_syscopy_invalid:
    MOV.q #-11, R0
    BRA hd_syscopy_return
hd_syscopy_unformatted:
    LEA hd_need_format, R0
    JSR u_puts
    MOV.q #-4, R0
    BRA hd_syscopy_return
hd_syscopy_ro:
    MOV.q #-6, R0
    BRA hd_syscopy_return
hd_syscopy_usage:
    LEA hd_syscopy_help, R0
    JSR u_puts
    MOV.q #-3, R0
hd_syscopy_return:
    POPM #0x3fff
    RET
hd_names:
    MOV.q R0, R22
    MOV.l #0x005c3a41, @H_SRC
    MOV.b R24, @H_SRC
    MOV.l #0x005c3a43, @H_DEST
    MOV.q #H_SRC+3, R1
    MOV.q #61, R2
    JSR u_copystr
    MOV.q R22, R0
    MOV.q #H_DEST+3, R1
    MOV.q #61, R2
    JSR u_copystr
    RET
hd_next_name:
    MOV.q R21, R0
    JSR u_strlen
    ADD.q R0, R21
    INC.q R21
    RET
hd_system_files:
    .asciz "IO.SYS", "COMMAND.EXE", "AUTOEXEC.BAT", "VI.EXE", "ASM.EXE", "LINK.EXE", "SYSCOPY.EXE", "BOOT.SYS"
    .byte 0
hd_boot_file: .asciz "BOOT.SYS"
hd_bad_text: .asciz "C: journal damaged: recovered files mounted read-only.\n"
hd_formatted: .asciz "C: formatted for KotlTOS. Run SYSCOPY C: to make it bootable.\n"
hd_system_copied: .asciz "KotlTOS system copied. C: is bootable.\n"
hd_need_format: .asciz "C: is not formatted. Use FORMAT C: first.\n"
hd_syscopy_help: .asciz "SYSCOPY [A:] C:\n"

; C: is the selected boot disk, or disk0 when booted from tape.
hd_controller:
    MOV.q @D_BOOTUNIT, R15
    SHL.q #6, R15
    ADD.q #D_DISK, R15
    RET
