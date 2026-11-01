; COMMAND.EXE: actual independently loaded native Upsilon executable.
.equ C_STATE, 0x190000
.equ C_LINE, 0x191000
.equ C_WORD, 0x192000
.equ C_NAME, 0x192100
.equ C_NAME2, 0x192180
.equ C_PATH, 0x192200
.equ C_COPY, 0x1c0000
.equ C_BATCH, 0x1a0000
.entry command_start
command_start:
    CLR.q @C_STATE
    CLR.q @C_STATE+8
    MOV.q #C_PATH, R0
    MOV.q #13, R14
    SYSCALL #33
    MOVZ.b.q @C_PATH, R0
    MOV.q R0, @C_STATE+32
    MOV.b R0, c_autoexec
    MOV.b R0, c_vi_name
    LEA c_autoexec, R0
    JSR c_batch_file
command_prompt:
    MOV.q #C_PATH, R0
    MOV.q #13, R14
    SYSCALL #33
    MOV.q #C_PATH, R0
    JSR c_puts
    LEA c_prompt, R0
    JSR c_puts
    MOV.q #C_LINE, R0
    MOV.q #1024, R1
    MOV.q #1, R14
    SYSCALL #33
    CMP.q #-1, R0
    BEQ c_shutdown
    CMP.q #-2, R0
    BEQ command_prompt
    JSR c_execute
    BRA command_prompt
c_puts:
    MOV.q #0, R14
    SYSCALL #33
    RET
c_token:
    MOV.q R0, R5
    CLR.q R4
c_token_spaces:
    MOV.b (R20), R1
    CMP.b #' ', R1
    BEQ c_token_skip
    CMP.b #9, R1
    BNE c_token_loop
c_token_skip:
    INC.q R20
    BRA c_token_spaces
c_token_loop:
    MOV.b (R20), R1
    BEQ c_token_end
    CMP.b #' ', R1
    BEQ c_token_end
    CMP.b #9, R1
    BEQ c_token_end
    CMP.q #63, R4
    BHS c_token_over
    MOV.b R1, (R5)+
    INC.q R4
    INC.q R20
    BRA c_token_loop
c_token_end:
    CLR.b (R5)
    MOV.q R4, R0
    RET
c_token_over:
    CLR.b (R5)
    MOV.q #-1, R0
    RET
c_upper:
    MOV.q R0, R4
c_upper_next:
    MOV.b (R4), R1
    BEQ c_upper_done
    CMP.b #'a', R1
    BLO c_upper_advance
    CMP.b #'z', R1
    BHI c_upper_advance
    SUB.b #32, (R4)
c_upper_advance:
    INC.q R4
    BRA c_upper_next
c_upper_done:
    RET
c_match:
    MOV.q #C_WORD, R1
c_match_next:
    MOV.b (R0)+, R2
    MOV.b (R1)+, R3
    CMP.b R2, R3
    BNE c_match_no
    TST.b R2
    BNE c_match_next
    MOV.q #1, R0
    RET
c_match_no:
    CLR.q R0
    RET
; Executes C_LINE, preserving batch/interactive callers' registers.
c_execute:
    PUSHM #0x3fff
    PUSH.q @C_STATE+24
    MOV.q SP, @C_STATE+24
    MOV.q #C_LINE, R20
    CMP.b #'@', (R20)
    BNE c_execute_parse
    INC.q R20
c_execute_parse:
    MOV.q #C_WORD, R0
    JSR c_token
    CMP.q #0, R0
    BLE c_execute_done
    MOV.q #C_WORD, R0
    JSR c_upper
    MOV.q #c_commands, R21
c_lookup:
    MOV.q (R21), R0
    TST.q R0
    BEQ c_external_dispatch
    JSR c_match
    TST.q R0
    BNE c_found
    ADD.q #16, R21
    BRA c_lookup
c_found:
    MOV.q 8(R21), R15
    JSR (R15)
c_execute_done:
    POP.q @C_STATE+24
    POPM #0x3fff
    RET
c_arg1:
    MOV.q #C_NAME, R0
    JSR c_token
    CMP.q #0, R0
    BLE c_argument_error
    RET
c_arg2:
    JSR c_arg1
    MOV.q #C_NAME2, R0
    JSR c_token
    CMP.q #0, R0
    BLE c_argument_error
    RET
c_drive:
    MOV.q #C_WORD, R0
    MOV.q #12, R14
    SYSCALL #33
    JMP c_status
c_format:
    JSR c_arg1
    MOV.q #C_NAME, R0
    MOV.q #19, R14
    SYSCALL #33
    JMP c_status
c_dir:
    MOV.q #C_NAME, R0
    JSR c_token
    MOV.q #C_NAME, R0
    MOV.q #2, R14
    SYSCALL #33
    JMP c_status
c_type:
    JSR c_arg1
    MOV.q #C_NAME, R0
    MOV.q #16, R14
    SYSCALL #33
    JMP c_status
c_copy:
    JSR c_arg2
    MOV.q #C_NAME, R0
    MOV.q #C_COPY, R1
    MOV.q #65536, R2
    MOV.q #3, R14
    SYSCALL #33
    CMP.q #0, R0
    BLT c_status
    MOV.q R0, R18
    MOV.q R1, R3
    MOV.q R2, R4
    MOV.q #C_NAME2, R0
    MOV.q #C_COPY, R1
    MOV.q R18, R2
    MOV.q #4, R14
    SYSCALL #33
    JMP c_status
c_del:
    JSR c_arg1
    MOV.q #C_NAME, R0
    CLR.q R1
    MOV.q #5, R14
    SYSCALL #33
    JMP c_status
c_rd:
    JSR c_arg1
    MOV.q #C_NAME, R0
    MOV.q #1, R1
    MOV.q #5, R14
    SYSCALL #33
    JMP c_status
c_ren:
    JSR c_arg2
    MOV.q #C_NAME, R0
    MOV.q #C_NAME2, R1
    MOV.q #6, R14
    SYSCALL #33
    JMP c_status
c_md:
    JSR c_arg1
    MOV.q #C_NAME, R0
    MOV.q #11, R14
    SYSCALL #33
    JMP c_status
c_cd:
    MOV.q #C_NAME, R0
    JSR c_token
    TST.q R0
    BEQ c_cd_show
    MOV.q #C_NAME, R0
    MOV.q #12, R14
    SYSCALL #33
    JMP c_status
c_cd_show:
    MOV.q #C_PATH, R0
    MOV.q #13, R14
    SYSCALL #33
    MOV.q #C_PATH, R0
    JSR c_puts
    LEA c_newline, R0
    JMP c_puts
c_edit:
    LEA c_vi_name, R0
    MOV.q R20, R1
    MOV.q #10, R14
    SYSCALL #33
    JMP c_status
c_ver:
    MOV.q #18, R14
    SYSCALL #33
    RET
c_help:
    LEA c_help_text, R0
    JMP c_puts
c_cls:
    LEA c_clear_text, R0
    JMP c_puts
c_rem:
    RET
c_shutdown:
    MOV.q #14, R14
    SYSCALL #33
    RET
c_call:
    JSR c_arg1
    MOV.q #C_NAME, R0
    JSR c_batch_file
    JMP c_status
c_status:
    CMP.q #0, R0
    BGE c_status_ok
    MOV.q R0, @C_STATE+16
    NEG.q R0, R18
    LEA c_error_text, R0
    JSR c_puts
    CMP.q #13, R18
    BHI c_status_unknown
    DEC.q R18
    MOV.q @c_errors[R18], R0
    JSR c_puts
    RET
c_status_unknown:
    LEA c_error_unknown, R0
    JSR c_puts
c_status_ok:
    RET
c_argument_error:
    ; Discard the current command's nested call frames safely.
    MOV.q @C_STATE+24, SP
    LEA c_arguments, R0
    JSR c_puts
    JMP c_execute_done
; ECHO ON/OFF, ECHO text, ECHO text > file and >> append.
c_echo:
    MOV.q R20, R16
    MOV.q #C_NAME, R0
    JSR c_token
    MOV.q #C_NAME, R0
    JSR c_upper
    CMP.l #0x0046464f, @C_NAME
    BEQ c_echo_off
    CMP.w #0x4e4f, @C_NAME
    BNE c_echo_text
    TST.b @C_NAME+2
    BEQ c_echo_on
c_echo_text:
    MOV.q R16, R20
c_echo_spaces:
    CMP.b #' ', (R20)
    BNE c_echo_begin
    INC.q R20
    BRA c_echo_spaces
c_echo_begin:
    MOV.q R20, R16
    CLR.q R17
c_echo_scan:
    MOV.b (R20), R0
    BEQ c_echo_console
    CMP.b #'>', R0
    BEQ c_echo_redirect
    INC.q R20
    BRA c_echo_scan
c_echo_console:
    MOV.q R16, R0
    JSR c_puts
    LEA c_newline, R0
    JMP c_puts
c_echo_off:
    CLR.q @C_STATE+8
    RET
c_echo_on:
    MOV.q #1, @C_STATE+8
    RET
c_echo_redirect:
    MOV.q R20, R18
    INC.q R20
    CMP.b #'>', (R20)
    BNE c_echo_filename
    MOV.q #1, R17
    INC.q R20
c_echo_filename:
    JSR c_arg1
    ; Trim whitespace preceding the redirection operator.
c_echo_trim:
    CMP.q R16, R18
    BLS c_echo_content
    CMP.b #' ', -1(R18)
    BNE c_echo_content
    DEC.q R18
    BRA c_echo_trim
c_echo_content:
    CLR.q R19
    TST.q R17
    BEQ c_echo_copy
    MOV.q #C_NAME, R0
    MOV.q #C_COPY, R1
    MOV.q #64512, R2
    MOV.q #3, R14
    SYSCALL #33
    CMP.q #-1, R0
    BEQ c_echo_copy
    CMP.q #0, R0
    BLT c_status
    MOV.q R0, R19
c_echo_copy:
    ADD3.q #C_COPY, R19, R21
c_echo_char:
    CMP.q R18, R16
    BHS c_echo_save
    MOV.b (R16)+, (R21)+
    INC.q R19
    BRA c_echo_char
c_echo_save:
    MOV.b #10, (R21)
    INC.q R19
    MOV.q #C_NAME, R0
    MOV.q #C_COPY, R1
    MOV.q R19, R2
    CLR.q R3
    CLR.q R4
    MOV.q #4, R14
    SYSCALL #33
    JMP c_status
; External programs and .BAT files, current directory then A:\ fallback.
c_external_dispatch:
    JSR c_external
    BRA c_execute_done
c_external:
    MOV.q R20, R22
    MOV.q #C_WORD, R0
    MOV.q #C_NAME, R1
    JSR c_string_copy
    MOV.q #C_NAME, R16
    CLR.q R17
c_external_scan:
    MOV.b (R16)+, R0
    BEQ c_external_suffix
    CMP.b #'.', R0
    BNE c_external_scan
    MOV.q #1, R17
    BRA c_external_scan
c_external_suffix:
    TST.q R17
    BNE c_external_try
    DEC.q R16
    MOV.l #0x5441422e, (R16)
    CLR.b 4(R16)
c_external_try:
    MOV.q #C_NAME, R0
    JSR c_batch_file
    CMP.q #-1, R0
    BNE c_status
    TST.q R17
    BNE c_external_exe
    MOV.q #C_NAME, R16
c_external_find_dot:
    CMP.b #'.', (R16)
    BEQ c_external_set_exe
    INC.q R16
    BRA c_external_find_dot
c_external_set_exe:
    MOV.l #0x4558452e, (R16)
    CLR.b 4(R16)
c_external_exe:
    MOV.q #C_NAME, R0
    MOV.q R22, R1
    MOV.q #10, R14
    SYSCALL #33
    CMP.q #-1, R0
    BNE c_status
    MOV.q #C_NAME, R0
    JSR c_root_name
    MOV.q #C_NAME2, R0
    MOV.q R22, R1
    MOV.q #10, R14
    SYSCALL #33
    JMP c_status
c_string_copy:
    MOV.b (R0)+, R2
    MOV.b R2, (R1)+
    BNE c_string_copy
    RET
c_root_name:
    MOV.q #C_NAME2, R1
    MOV.l #0x005c3a41, (R1)
    MOV.b @C_STATE+32, (R1)
    ADD.q #3, R1
    JMP c_string_copy
; Run a text .BAT only; reject EXEs/objects. Four nested CALL levels.
c_batch_file:
    PUSHM #0x3fff
    MOV.q R0, R16
    MOV.q @C_STATE, R17
    CMP.q #4, R17
    BHS c_batch_error
    MUL3.q #0x2200, R17, R18
    ADD.q #C_BATCH, R18
    MOV.q R18, R1
    MOV.q #8191, R2
    MOV.q #3, R14
    SYSCALL #33
    CMP.q #-1, R0
    BNE c_batch_read
    MOV.q R16, R0
    JSR c_root_name
    MOV.q #C_NAME2, R0
    MOV.q R18, R1
    MOV.q #8191, R2
    MOV.q #3, R14
    SYSCALL #33
c_batch_read:
    CMP.q #0, R0
    BLT c_batch_return
    TST.q R1
    BNE c_batch_not_text
    ADD3.q R18, R0, R19
    CLR.b (R19)
    INC.q @C_STATE
c_batch_line:
    CMP.q R19, R18
    BHS c_batch_done
    MOV.q #C_LINE, R20
    CLR.q R21
c_batch_copy_line:
    CMP.q R19, R18
    BHS c_batch_ready
    MOV.b (R18)+, R0
    CMP.b #10, R0
    BEQ c_batch_ready
    CMP.b #13, R0
    BEQ c_batch_copy_line
    CMP.q #1023, R21
    BHS c_batch_long
    MOV.b R0, (R20)+
    INC.q R21
    BRA c_batch_copy_line
c_batch_ready:
    CLR.b (R20)
    TST.q @C_STATE+8
    BEQ c_batch_execute
    CMP.b #'@', @C_LINE
    BEQ c_batch_execute
    MOV.q #C_LINE, R0
    JSR c_puts
    LEA c_newline, R0
    JSR c_puts
c_batch_execute:
    JSR c_execute
    BRA c_batch_line
c_batch_long:
    LEA c_arguments, R0
    JSR c_puts
    BRA c_batch_done
c_batch_done:
    DEC.q @C_STATE
    CLR.q R0
    BRA c_batch_return
c_batch_not_text:
    MOV.q #-1, R0
    BRA c_batch_return
c_batch_error:
    MOV.q #-12, R0
c_batch_return:
    POPM #0x3fff
    RET
c_commands:
    .quad c_kw_dir,c_dir,c_kw_type,c_type,c_kw_copy,c_copy,c_kw_del,c_del
    .quad c_kw_erase,c_del,c_kw_ren,c_ren,c_kw_rename,c_ren,c_kw_md,c_md
    .quad c_kw_mkdir,c_md,c_kw_rd,c_rd,c_kw_rmdir,c_rd,c_kw_cd,c_cd
    .quad c_kw_chdir,c_cd,c_kw_echo,c_echo,c_kw_ver,c_ver,c_kw_help,c_help
    .quad c_kw_cls,c_cls,c_kw_rem,c_rem,c_kw_call,c_call,c_kw_exit,c_shutdown
    .quad c_kw_shutdown,c_shutdown,c_kw_edit,c_edit
    .quad c_kw_a,c_drive,c_kw_c,c_drive,c_kw_format,c_format,0,0
c_kw_a: .asciz "A:"
c_kw_c: .asciz "C:"
c_kw_format: .asciz "FORMAT"
c_kw_edit: .asciz "EDIT"
c_vi_name: .asciz "A:\\VI.EXE"
c_kw_dir: .asciz "DIR"
c_kw_type: .asciz "TYPE"
c_kw_copy: .asciz "COPY"
c_kw_del: .asciz "DEL"
c_kw_erase: .asciz "ERASE"
c_kw_ren: .asciz "REN"
c_kw_rename: .asciz "RENAME"
c_kw_md: .asciz "MD"
c_kw_mkdir: .asciz "MKDIR"
c_kw_rd: .asciz "RD"
c_kw_rmdir: .asciz "RMDIR"
c_kw_cd: .asciz "CD"
c_kw_chdir: .asciz "CHDIR"
c_kw_echo: .asciz "ECHO"
c_kw_ver: .asciz "VER"
c_kw_help: .asciz "HELP"
c_kw_cls: .asciz "CLS"
c_kw_rem: .asciz "REM"
c_kw_call: .asciz "CALL"
c_kw_exit: .asciz "EXIT"
c_kw_shutdown: .asciz "SHUTDOWN"
c_autoexec: .asciz "A:\\AUTOEXEC.BAT"
c_prompt: .asciz "> "
c_newline: .asciz "\n"
c_error_text: .asciz "KotlTOS error: "
c_errors:
    .quad c_e1,c_e2,c_e3,c_e4,c_e5,c_e6,c_e7,c_e8,c_e9,c_e10,c_e11,c_e12,c_e13
c_e1: .asciz "File not found.\n"
c_e2: .asciz "File or tool size limit exceeded.\n"
c_e3: .asciz "Invalid filename or arguments.\n"
c_e4: .asciz "Path not found.\n"
c_e5: .asciz "Volume full (64 entries).\n"
c_e6: .asciz "Volume is write-protected.\n"
c_e7: .asciz "Volume write failed.\n"
c_e8: .asciz "Operation not allowed (directory/file type, existing target or nonempty directory).\n"
c_e9: .asciz "Application fault.\n"
c_e10: .asciz "Unsupported KotlTOS service.\n"
c_e11: .asciz "Invalid object or executable.\n"
c_e12: .asciz "Nesting limit reached.\n"
c_e13: .asciz "Assembly/link failed.\n"
c_error_unknown: .asciz "Unknown error.\n"
c_arguments: .asciz "Invalid or missing command arguments.\n"
c_help_text: .asciz "DIR [path] | TYPE file | COPY source dest | DEL file | REN old new\nMD dir | RD dir | CD [path] | ECHO text [> or >> file] | CALL file.bat\nVI/EDIT file | ASM source.asm output.obj | LINK output.exe input.obj [...] | FORMAT C: | SYSCOPY C: | A: | C:\nVER | CLS | HELP | EXIT | SHUTDOWN\nVI: i/a/o insert, Esc command, h/j/k/l, x, dd, :w, :q, :q!, :wq\n"
c_clear_text:
    .byte 27
    .ascii "[2J"
    .byte 27
    .asciz "[H"
