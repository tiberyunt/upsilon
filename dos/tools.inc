; Native source-object toolchain. UOBJ1 objects retain assembly IR so the existing
; guest assembler can perform final symbol resolution/relaxation across modules.
.equ T_IN, D_STATE+2048
.equ T_OUT, D_STATE+2112
.equ T_LENGTH, D_STATE+2176
.equ T_OBJECT_MAGIC, 0x000000314a424f55
tool_asm:
    PUSHM #0x3fff
    MOV.q R0, R20
    MOV.q #T_IN, R0
    JSR u_token
    CMP.q #0, R0
    BLE tool_asm_usage
    MOV.q #T_OUT, R0
    JSR u_token
    CMP.q #-1, R0
    BEQ tool_asm_usage
    TST.q R0
    BNE tool_asm_read
    ; Derive a .OBJ name from the last component of the input path.
    MOV.q #T_IN, R0
    MOV.q #T_OUT, R1
    MOV.q #64, R2
    JSR u_copystr
    MOV.q #T_OUT, R16
    CLR.q R17
tool_asm_extension:
    MOV.b (R16), R0
    BEQ tool_asm_suffix
    CMP.b #92, R0
    BEQ tool_asm_component
    CMP.b #'/', R0
    BEQ tool_asm_component
    CMP.b #'.', R0
    BNE tool_asm_ext_next
    MOV.q R16, R17
    BRA tool_asm_ext_next
tool_asm_component:
    CLR.q R17
tool_asm_ext_next:
    INC.q R16
    BRA tool_asm_extension
tool_asm_suffix:
    TST.q R17
    BEQ tool_asm_suffix_at
    MOV.q R17, R16
tool_asm_suffix_at:
    CMP.q #T_OUT+59, R16
    BHI tool_asm_usage
    MOV.l #0x4a424f2e, (R16)
    CLR.b 4(R16)
tool_asm_read:
    MOV.q #T_IN, R0
    MOV.q #D_COMBINE, R1
    MOV.q #8191, R2
    JSR fs_read
    CMP.q #0, R0
    BLT tool_return
    TST.q R1
    BNE tool_format_error
    MOV.q R0, @T_LENGTH
    CLR.b @D_COMBINE[R0]
    MOV.q R0, R1
    MOV.q #D_COMBINE, R0
    CLR.q R2
    JSR tool_prepare
    CMP.q #0, R0
    BLT tool_return
    MOV.q R0, R1
    JSR tool_native
    TST.q R0
    BNE tool_assembly_error
    MOV.q R1, R24
    MOV.q R2, R25
    MOV.q #32, R0
    MOV.q #D_OBJECT, R2
    FILL.b #0
    MOV.q #T_OBJECT_MAGIC, @D_OBJECT
    MOV.q @T_LENGTH, @D_OBJECT+8
    MOV.q R24, @D_OBJECT+16
    MOV.q R25, @D_OBJECT+24
    MOV.q @T_LENGTH, R0
    MOV.q #D_COMBINE, R1
    MOV.q #D_OBJECT+32, R2
    MOVS.b
    MOV.q R24, R0
    MOV.q #D_CODE, R1
    ADD3.q #D_OBJECT+32, @T_LENGTH, R2
    MOVS.b
    MOV.q #T_OUT, R0
    MOV.q #D_OBJECT, R1
    ADD3.q #32, @T_LENGTH, R2
    ADD.q R24, R2
    MOV.q #2, R3
    CLR.q R4
    JSR fs_write
    TST.q R0
    BNE tool_return
    LEA tool_object_saved, R0
    JSR u_puts
    MOV.q #T_OUT, R0
    JSR u_puts
    JSR u_newline
    CLR.q R0
    BRA tool_return
; LINK output.exe input.obj [...], up to eight modules and 8191 source bytes.
tool_link:
    PUSHM #0x3fff
    MOV.q R0, R20
    MOV.q #T_OUT, R0
    JSR u_token
    CMP.q #0, R0
    BLE tool_link_usage
    CLR.q R22
    CLR.q R23
tool_link_next:
    MOV.q #T_IN, R0
    JSR u_token
    TST.q R0
    BEQ tool_link_ready
    CMP.q #-1, R0
    BEQ tool_link_usage
    INC.q R22
    CMP.q #8, R22
    BHI tool_link_usage
    MOV.q #T_IN, R0
    MOV.q #D_OBJECT, R1
    MOV.q #16351, R2
    JSR fs_read
    CMP.q #0, R0
    BLT tool_return
    CMP.q #2, R1
    BNE tool_format_error
    CMP.q #32, R0
    BLO tool_format_error
    CMP.q #T_OBJECT_MAGIC, @D_OBJECT
    BNE tool_format_error
    MOV.q @D_OBJECT+8, R24
    CMP.q #8191, R24
    BHI tool_format_error
    MOV.q @D_OBJECT+16, R25
    CMP.q #8128, R25
    BHI tool_format_error
    ADD3.q #32, R24, R1
    ADD.q R25, R1
    CMP.q R0, R1
    BNE tool_format_error
    ADD3.q R23, R24, R1
    INC.q R1
    CMP.q #8191, R1
    BHI tool_size_error
    MOV.q R24, R0
    MOV.q #D_OBJECT+32, R1
    ADD3.q #D_COMBINE, R23, R2
    MOVS.b
    ADD.q R24, R23
    MOV.b #10, @D_COMBINE[R23]
    INC.q R23
    BRA tool_link_next
tool_link_ready:
    TST.q R22
    BEQ tool_link_usage
    CLR.b @D_COMBINE[R23]
    MOV.q #D_COMBINE, R0
    MOV.q R23, R1
    MOV.q #1, R2
    JSR tool_prepare
    CMP.q #0, R0
    BLT tool_return
    MOV.q R0, R1
    JSR tool_native
    TST.q R0
    BNE tool_assembly_error
    MOV.q R1, R24
    MOV.q R2, R25
    TST.q R24
    BEQ tool_format_error
    CMP.q #D_APP, R25
    BLO tool_format_error
    ADD3.q #D_APP, R24, R0
    CMP.q R0, R25
    BHS tool_format_error
    MOV.q #T_OUT, R0
    MOV.q #D_CODE, R1
    MOV.q R24, R2
    MOV.q #1, R3
    MOV.q R25, R4
    JSR fs_write
    TST.q R0
    BNE tool_return
    LEA tool_link_saved, R0
    JSR u_puts
    MOV.q #T_OUT, R0
    JSR u_puts
    JSR u_newline
    CLR.q R0
    BRA tool_return
tool_native:
    MOV.q #D_PRE, R0
    MOV.q #D_CODE, R2
    MOV.q #8128, R3
    MOV.q #D_DIAG, R4
    MOV.q #256, R5
    MOV.q #D_APP, R6
    JMP na_assemble
tool_assembly_error:
    LEA tool_error_text, R0
    JSR u_puts
    MOV.q #D_DIAG, R0
    JSR u_puts
    JSR u_newline
    MOV.q #-13, R0
    BRA tool_return
tool_format_error:
    LEA tool_format_text, R0
    JSR u_puts
    MOV.q #-11, R0
    BRA tool_return
tool_size_error:
    LEA tool_size_text, R0
    JSR u_puts
    MOV.q #-2, R0
    BRA tool_return
tool_asm_usage:
    LEA tool_asm_help, R0
    JSR u_puts
    MOV.q #-3, R0
    BRA tool_return
tool_link_usage:
    LEA tool_link_help, R0
    JSR u_puts
    MOV.q #-3, R0
tool_return:
    POPM #0x3fff
    RET
; R0=source,R1=length,R2=0 object validation / 1 final link.
; Output D_PRE, result R0=len or negative. .extern placeholders are validation
; only: final link removes declarations and resolves real module definitions.
tool_prepare:
    PUSHM #0x3fff
    MOV.q R0, R16
    ADD3.q R0, R1, R17
    MOV.q #D_PRE, R18
    MOV.q R2, R22
    CLR.q R23
    CLR.q R28
tool_pre_line:
    CMP.q R17, R16
    BHS tool_pre_done
    MOV.q R16, R19
    MOV.q R16, R21
tool_pre_end:
    CMP.q R17, R21
    BHS tool_pre_found
    CMP.b #10, (R21)
    BEQ tool_pre_found
    INC.q R21
    BRA tool_pre_end
tool_pre_found:
    MOV.q R19, R24
tool_pre_indent:
    CMP.q R21, R24
    BHS tool_pre_copy
    MOV.b (R24), R0
    CMP.b #' ', R0
    BEQ tool_pre_space
    CMP.b #9, R0
    BNE tool_pre_directive
tool_pre_space:
    INC.q R24
    BRA tool_pre_indent
tool_pre_directive:
    MOV.q R24, R0
    LEA tool_kw_extern, R1
    JSR tool_directive
    TST.q R0
    BNE tool_pre_extern
    MOV.q R24, R0
    LEA tool_kw_entry, R1
    JSR tool_directive
    TST.q R0
    BEQ tool_pre_copy
    INC.q R23
    CMP.q #1, R23
    BHI tool_pre_bad
tool_pre_copy:
    CMP.q R21, R19
    BHS tool_pre_next
    MOV.b (R19)+, R0
    JSR tool_outchar
    BRA tool_pre_copy
tool_pre_extern:
    MOV.q R0, R24
    TST.q R22
    BNE tool_pre_next
tool_pre_extern_name:
    CMP.q R21, R24
    BHS tool_pre_next
    MOV.b (R24), R0
    CMP.b #';', R0
    BEQ tool_pre_next
    CMP.b #' ', R0
    BEQ tool_pre_extern_skip
    CMP.b #9, R0
    BEQ tool_pre_extern_skip
    CMP.b #13, R0
    BEQ tool_pre_extern_skip
    CMP.b #',', R0
    BEQ tool_pre_extern_skip
    MOV.q #tool_equ, R25
tool_pre_equ:
    MOV.b (R25)+, R0
    BEQ tool_pre_symbol
    JSR tool_outchar
    BRA tool_pre_equ
tool_pre_symbol:
    CLR.q R26
tool_pre_symbol_next:
    CMP.q R21, R24
    BHS tool_pre_symbol_end
    MOV.b (R24), R0
    CMP.b #'_', R0
    BEQ tool_pre_symbol_char
    CMP.b #'A', R0
    BLO tool_pre_symbol_lower
    CMP.b #'Z', R0
    BLS tool_pre_symbol_char
tool_pre_symbol_lower:
    CMP.b #'a', R0
    BLO tool_pre_symbol_digit
    CMP.b #'z', R0
    BLS tool_pre_symbol_char
tool_pre_symbol_digit:
    CMP.b #'0', R0
    BLO tool_pre_symbol_end
    CMP.b #'9', R0
    BHI tool_pre_symbol_end
    TST.q R26
    BEQ tool_pre_bad
tool_pre_symbol_char:
    INC.q R26
    CMP.q #63, R26
    BHI tool_pre_bad
    JSR tool_outchar
    INC.q R24
    BRA tool_pre_symbol_next
tool_pre_symbol_end:
    TST.q R26
    BEQ tool_pre_bad
    MOV.q #',', R0
    JSR tool_outchar
    MOV.q #'0', R0
    JSR tool_outchar
    MOV.q #10, R0
    JSR tool_outchar
    BRA tool_pre_extern_name
tool_pre_extern_skip:
    INC.q R24
    BRA tool_pre_extern_name
tool_pre_next:
    MOV.q #10, R0
    JSR tool_outchar
    MOV.q R21, R16
    CMP.q R17, R16
    BHS tool_pre_done
    INC.q R16
    BRA tool_pre_line
tool_pre_done:
    TST.q R28
    BNE tool_pre_bad
    CLR.b (R18)
    SUB3.q #D_PRE, R18, R0
    POPM #0x3fff
    RET
tool_pre_bad:
    LEA tool_pre_error, R0
    JSR u_puts
    MOV.q #-13, R0
    POPM #0x3fff
    RET
tool_outchar:
    CMP.q #D_PRE+8191, R18
    BHS tool_out_full
    MOV.b R0, (R18)+
    RET
tool_out_full:
    MOV.q #1, R28
    RET
; Match directive token ignoring case. R0=cursor/R1=keyword -> R0=end or 0.
tool_directive:
    MOV.q R0, R2
tool_directive_next:
    MOV.b (R1)+, R3
    BEQ tool_directive_boundary
    MOV.b (R2)+, R4
    CMP.b #'a', R4
    BLO tool_directive_cmp
    CMP.b #'z', R4
    BHI tool_directive_cmp
    SUB.b #32, R4
tool_directive_cmp:
    CMP.b R3, R4
    BNE tool_directive_no
    BRA tool_directive_next
tool_directive_boundary:
    MOV.b (R2), R3
    TST.b R3
    BEQ tool_directive_yes
    CMP.b #' ', R3
    BEQ tool_directive_yes
    CMP.b #9, R3
    BEQ tool_directive_yes
    CMP.b #10, R3
    BEQ tool_directive_yes
    CMP.b #13, R3
    BNE tool_directive_no
tool_directive_yes:
    MOV.q R2, R0
    RET
tool_directive_no:
    CLR.q R0
    RET
tool_kw_extern: .asciz ".EXTERN"
tool_kw_entry: .asciz ".ENTRY"
tool_equ: .asciz ".equ "
tool_object_saved: .asciz "Object saved: "
tool_link_saved: .asciz "Executable linked: "
tool_error_text: .asciz "Assembly error: "
tool_format_text: .asciz "Invalid object or executable entry.\n"
tool_size_text: .asciz "Link limit: 8 modules, 8191 combined source bytes, 8128 code bytes.\n"
tool_pre_error: .asciz "Invalid .extern, duplicate .entry, or expanded source exceeds 8191 bytes.\n"
tool_asm_help: .asciz "ASM source.asm [output.obj]\n"
tool_link_help: .asciz "LINK output.exe input.obj [input.obj ...]\n"
