; Native Upsilon assembler. No SYSCALL, host callback or pseudo-instruction.
; ABI matches the monitor's former RAM request, but is an ordinary JSR/RET.
; R0=source R1=length R2=output R3=capacity R4=diagnostic R5=capacity R6=origin.
; Returns R0=status, R1=length, R2=entry. R16..R31 preserved.
.equ na_state, 0x78000
.equ na_src, na_state
.equ na_length, na_state+8
.equ na_dest, na_state+16
.equ na_capacity, na_state+24
.equ na_diag, na_state+32
.equ na_diagcap, na_state+40
.equ na_origin, na_state+48
.equ na_savedsp, na_state+56
.equ na_outlen, na_state+64
.equ na_pc, na_state+72
.equ na_location, na_state+80
.equ na_entry, na_state+88
.equ na_pass, na_state+96
.equ na_changed, na_state+104
.equ na_nsym, na_state+112
.equ na_sourcepos, na_state+120
.equ na_lineno, na_state+128
.equ na_oldlen, na_state+136
.equ na_oldentry, na_state+144
.equ na_patch, na_state+160
.equ na_targets, na_state+192
.equ na_opstart, na_state+224
.equ na_optype, na_state+232
.equ na_opsize, na_state+240
.equ na_basevalid, na_state+248
.equ na_branch, na_state+256
.equ na_insstart, na_state+264
.equ na_stage, 0x70000
.equ na_symbols, 0x72000
.equ na_token, 0x78400
.equ na_hold, 0x78500
.equ na_numtext, 0x78600
.equ na_linebuf, 0x79000
na_assemble:
    PUSH.q FP
    MOV.q SP, FP
    PUSHM #0x3fff
    MOV.q SP, @na_savedsp
    MOV.q R0, @na_src
    MOV.q R1, @na_length
    MOV.q R2, @na_dest
    MOV.q R3, @na_capacity
    MOV.q R4, @na_diag
    MOV.q R5, @na_diagcap
    MOV.q R6, @na_origin
    MFFS R7
    MOV.q R7, @na_state+272
    MTFS #0
    CLR.q @na_lineno
    CMP.q #8191, R1
    BHI na_bad_arguments
    CMP.q #8128, R3
    BHI na_bad_arguments
    TST.q R3
    BEQ na_bad_arguments
    TST.q R5
    BEQ na_bad_arguments
    CLR.q @na_nsym
    CLR.q @na_pass
    MOV.q #-1, @na_oldlen
na_pass_start:
    CLR.q @na_changed
    CLR.q @na_sourcepos
    CLR.q @na_lineno
    CLR.q @na_outlen
    MOV.q @na_origin, @na_pc
    MOV.q @na_origin, @na_entry
    MOV.q #na_symbols, R4
    MOV.q @na_nsym, R5
na_clear_defined:
    TST.q R5
    BEQ na_next_line
    CLR.q 72(R4)
    ADD.q #80, R4
    SOB R5, na_clear_defined
na_next_line:
    MOV.q @na_sourcepos, R4
    CMP.q @na_length, R4
    BHS na_pass_end
    INC.q @na_lineno
    ADD3.q @na_src, R4, R5
    MOV.q #na_linebuf, R6
    CLR.q R7
na_copy_line:
    CMP.q @na_length, R4
    BHS na_line_ready
    MOVZ.b.q (R5)+, R0
    INC.q R4
    CMP.b #10, R0
    BEQ na_line_ready
    CMP.q #1023, R7
    BHS na_line_too_long
    MOV.b R0, (R6)+
    INC.q R7
    BRA na_copy_line
na_line_ready:
    CLR.b (R6)
    MOV.q R4, @na_sourcepos
    MOV.q #na_linebuf, R20
    MOV.q @na_pc, @na_location
    JSR na_statement
    BRA na_next_line
na_pass_end:
    MOV.q @na_outlen, R0
    CMP.q @na_oldlen, R0
    BNE na_another_pass
    MOV.q @na_entry, R0
    CMP.q @na_oldentry, R0
    BNE na_another_pass
    TST.q @na_changed
    BNE na_another_pass
    TST.q @na_pass
    BNE na_success
na_another_pass:
    MOV.q @na_outlen, @na_oldlen
    MOV.q @na_entry, @na_oldentry
    INC.q @na_pass
    CMP.q #128, @na_pass
    BLO na_pass_start
    LEA na_e_relax, R8
    BRA na_error
na_success:
    MOV.q @na_dest, R4
    MOV.q #na_stage, R5
    MOV.q @na_outlen, R6
na_commit:
    TST.q R6
    BEQ na_success_done
    MOV.b (R5)+, (R4)+
    SOB R6, na_commit
na_success_done:
    MOV.q @na_diag, R4
    CLR.b (R4)
    CLR.q R0
    MOV.q @na_outlen, R1
    MOV.q @na_entry, R2
na_return:
    MTFS @na_state+272
    MOV.q @na_savedsp, SP
    POPM #0x3fff
    POP.q FP
    RET

; Helpers preserve R16..R29 except parser cursor R20. R0..R7 are scratch.
na_spaces:
    MOVZ.b.q (R20), R0
    CMP.b #32, R0
    BEQ na_skip
    CMP.b #9, R0
    BEQ na_skip
    CMP.b #13, R0
    BNE na_spaces_done
na_skip:
    INC.q R20
    BRA na_spaces
na_spaces_done:
    RET
na_end:
    JSR na_spaces
    TST.b R0
    BEQ na_end_yes
    CMP.b #';', R0
    BEQ na_end_yes
    BRA na_syntax
na_end_yes:
    RET
na_comma:
    JSR na_spaces
    CMP.b #',', R0
    BNE na_syntax
    INC.q R20
    RET
na_name:
    JSR na_spaces
    MOV.q #na_token, R2
    CLR.q R3
na_name_loop:
    MOVZ.b.q (R20), R0
    CMP.b #'_', R0
    BEQ na_name_accept
    CMP.b #'A', R0
    BLO na_name_digit
    CMP.b #'Z', R0
    BLE na_name_accept
    CMP.b #'a', R0
    BLO na_name_digit
    CMP.b #'z', R0
    BLE na_name_accept
na_name_digit:
    TST.q R3
    BEQ na_name_done
    CMP.b #'0', R0
    BLO na_name_done
    CMP.b #'9', R0
    BHI na_name_done
na_name_accept:
    CMP.q #63, R3
    BHS na_name_too_long
    MOV.b R0, (R2)+
    INC.q R20
    INC.q R3
    BRA na_name_loop
na_name_done:
    CLR.b (R2)
    MOV.q R3, R0
    RET
na_upper_token:
    MOV.q #na_token, R2
na_upper_loop:
    MOVZ.b.q (R2), R0
    BEQ na_upper_done
    CMP.b #'a', R0
    BLO na_upper_next
    CMP.b #'z', R0
    BHI na_upper_next
    SUB.b #32, (R2)
na_upper_next:
    INC.q R2
    BRA na_upper_loop
na_upper_done:
    RET
; string equality: R1/R2 pointers, R0 result, only R0..R3 changed.
na_equal:
    MOVZ.b.q (R1)+, R0
    MOVZ.b.q (R2)+, R3
    CMP.b R0, R3
    BNE na_equal_no
    TST.b R0
    BNE na_equal
    MOV.q #1, R0
    RET
na_equal_no:
    CLR.q R0
    RET
; Find token symbol. R0 record or zero.
na_find_symbol:
    MOV.q #na_symbols, R4
    MOV.q @na_nsym, R5
na_find_symbol_loop:
    TST.q R5
    BEQ na_find_symbol_no
    MOV.q #na_token, R1
    MOV.q R4, R2
    JSR na_equal
    TST.q R0
    BNE na_find_symbol_yes
    ADD.q #80, R4
    SOB R5, na_find_symbol_loop
na_find_symbol_no:
    CLR.q R0
    RET
na_find_symbol_yes:
    MOV.q R4, R0
    RET
; Define token = R0, preserving cursor. Duplicate definitions in a pass fail.
na_define:
    PUSH.q R0
    JSR na_find_symbol
    TST.q R0
    BNE na_define_existing
    CMP.q #256, @na_nsym
    BHS na_symbols_full
    MOV.q @na_nsym, R4
    MUL.q #80, R4
    ADD.q #na_symbols, R4
    MOV.q #na_token, R1
    MOV.q R4, R2
na_copy_symbol_name:
    MOV.b (R1)+, R3
    MOV.b R3, (R2)+
    BNE na_copy_symbol_name
    CLR.q 72(R4)
    CLR.q 64(R4)
    INC.q @na_nsym
    MOV.q #1, @na_changed
    BRA na_define_value
na_define_existing:
    MOV.q R0, R4
    TST.q 72(R4)
    BNE na_duplicate
na_define_value:
    POP.q R0
    CMP.q 64(R4), R0
    BEQ na_define_same
    MOV.q #1, @na_changed
na_define_same:
    MOV.q R0, 64(R4)
    MOV.q #1, 72(R4)
    RET
; Byte/multi-byte little-endian emission, bounds checked before every byte.
na_byte:
    MOV.q #1, R1
na_emit:
    TST.q R1
    BEQ na_emit_done
    MOV.q @na_outlen, R2
    CMP.q @na_capacity, R2
    BHS na_output_full
    ADD.q #na_stage, R2
    MOV.b R0, (R2)
    INC.q @na_outlen
    INC.q @na_pc
    SHR.q #8, R0
    DEC.q R1
    BRA na_emit
na_emit_done:
    RET
; Patch R0 at stage-relative R1 for R2 bytes without changing PC.
na_patch_bytes:
    ADD.q #na_stage, R1
na_patch_loop:
    MOV.b R0, (R1)+
    SHR.q #8, R0
    SOB R2, na_patch_loop
    RET

; Pratt integer expression parser. Unary +/-/~, + - * // % << >> & ^ |.
na_expression:
    CLR.q R0
na_expr:
    PUSH.q R17
    PUSH.q R18
    PUSH.q R19
    MOV.q R0, R17
    JSR na_primary
    MOV.q R0, R18
na_expr_next:
    JSR na_spaces
    MOV.q R0, R19
    CLR.q R1
    CMP.b #'|', R0
    BNE na_prec_xor
    MOV.q #1, R1
na_prec_xor:
    CMP.b #'^', R0
    BNE na_prec_and
    MOV.q #2, R1
na_prec_and:
    CMP.b #'&', R0
    BNE na_prec_shift
    MOV.q #3, R1
na_prec_shift:
    CMP.b #'<', R0
    BEQ na_prec_shift_check
    CMP.b #'>', R0
    BNE na_prec_add
na_prec_shift_check:
    CMP.b 1(R20), R0
    BNE na_expr_done
    MOV.q #4, R1
na_prec_add:
    CMP.b #'+', R0
    BEQ na_prec_add_yes
    CMP.b #'-', R0
    BNE na_prec_mul
na_prec_add_yes:
    MOV.q #5, R1
na_prec_mul:
    CMP.b #'*', R0
    BEQ na_prec_mul_yes
    CMP.b #'%', R0
    BEQ na_prec_mul_yes
    CMP.b #'/', R0
    BNE na_prec_ready
    CMP.b #'/', 1(R20)
    BNE na_expr_done
na_prec_mul_yes:
    MOV.q #6, R1
na_prec_ready:
    TST.q R1
    BEQ na_expr_done
    CMP.q R17, R1
    BLO na_expr_done
    INC.q R20
    CMP.b #'<', R19
    BEQ na_expr_extra
    CMP.b #'>', R19
    BEQ na_expr_extra
    CMP.b #'/', R19
    BNE na_expr_rhs
na_expr_extra:
    INC.q R20
na_expr_rhs:
    ADD3.q #1, R1, R0
    JSR na_expr
    CMP.b #'+', R19
    BNE na_expr_sub
    ADD.q R0, R18
    BRA na_expr_next
na_expr_sub:
    CMP.b #'-', R19
    BNE na_expr_mul
    SUB.q R0, R18
    BRA na_expr_next
na_expr_mul:
    CMP.b #'*', R19
    BNE na_expr_or
    MUL.q R0, R18
    BRA na_expr_next
na_expr_or:
    CMP.b #'|', R19
    BNE na_expr_xor
    OR.q R0, R18
    BRA na_expr_next
na_expr_xor:
    CMP.b #'^', R19
    BNE na_expr_and
    XOR.q R0, R18
    BRA na_expr_next
na_expr_and:
    CMP.b #'&', R19
    BNE na_expr_shl
    AND.q R0, R18
    BRA na_expr_next
na_expr_shl:
    CMP.b #'<', R19
    BNE na_expr_shr
    SHL.q R0, R18
    BRA na_expr_next
na_expr_shr:
    CMP.b #'>', R19
    BNE na_expr_div
    SHR.q R0, R18
    BRA na_expr_next
na_expr_div:
    TST.q R0
    BEQ na_div_zero
    MOV.q R18, R2
    DIV.q R0, R2
    MUL3.q R0, R2, R3
    SUB3.q R3, R18, R3
    TST.q R3
    BEQ na_div_apply
    XOR3.q R0, R18, R4
    BPL na_div_apply
    DEC.q R2
na_div_apply:
    CMP.b #'/', R19
    BEQ na_div_quotient
    MUL.q R0, R2
    SUB.q R2, R18
    BRA na_expr_next
na_div_quotient:
    MOV.q R2, R18
    BRA na_expr_next
na_expr_done:
    MOV.q R18, R0
    POP.q R19
    POP.q R18
    POP.q R17
    RET
na_primary:
    JSR na_spaces
    CMP.b #'+', R0
    BEQ na_primary_plus
    CMP.b #'-', R0
    BEQ na_primary_minus
    CMP.b #'~', R0
    BEQ na_primary_not
    CMP.b #'(', R0
    BEQ na_primary_paren
    CMP.b #39, R0
    BEQ na_primary_char
    CMP.b #34, R0
    BEQ na_primary_doublechar
    CMP.b #'.', R0
    BEQ na_primary_pc
    CMP.b #'0', R0
    BLO na_primary_symbol
    CMP.b #'9', R0
    BHI na_primary_symbol
    BRA na_integer
na_primary_plus:
    INC.q R20
    JSR na_primary
    RET
na_primary_minus:
    INC.q R20
    JSR na_primary
    NEG.q R0, R0
    RET
na_primary_not:
    INC.q R20
    JSR na_primary
    NOT.q R0, R0
    RET
na_primary_paren:
    INC.q R20
    JSR na_expression
    PUSH.q R0
    JSR na_spaces
    CMP.b #')', R0
    BNE na_syntax
    INC.q R20
    POP.q R0
    RET
na_primary_char:
    INC.q R20
    JSR na_character
    CMP.b #39, (R20)
    BNE na_syntax
    INC.q R20
    RET
na_primary_doublechar:
    INC.q R20
    JSR na_character
    CMP.b #34, (R20)
    BNE na_syntax
    INC.q R20
    RET
na_primary_pc:
    INC.q R20
    MOV.q @na_location, R0
    RET
na_primary_symbol:
    JSR na_name
    TST.q R0
    BEQ na_syntax
    JSR na_find_symbol
    TST.q R0
    BNE na_symbol_value
    TST.q @na_pass
    BNE na_undefined
    CLR.q R0
    RET
na_symbol_value:
    MOV.q 64(R0), R0
    RET
na_character:
    MOVZ.b.q (R20)+, R0
    TST.b R0
    BEQ na_syntax
    CMP.b #92, R0
    BNE na_character_done
    MOVZ.b.q (R20)+, R0
    CMP.b #'n', R0
    BNE na_char_r
    MOV.q #10, R0
    RET
na_char_r:
    CMP.b #'r', R0
    BNE na_char_t
    MOV.q #13, R0
    RET
na_char_t:
    CMP.b #'t', R0
    BNE na_char_zero
    MOV.q #9, R0
    RET
na_char_zero:
    CMP.b #'0', R0
    BNE na_char_hex
    CLR.q R0
    RET
na_char_hex:
    CMP.b #'x', R0
    BNE na_character_done
    JSR na_hex_digit
    SHL.q #4, R0
    PUSH.q R0
    JSR na_hex_digit
    POP.q R1
    ADD.q R1, R0
na_character_done:
    RET
na_hex_digit:
    MOVZ.b.q (R20)+, R0
    CMP.b #'0', R0
    BLO na_syntax
    CMP.b #'9', R0
    BLE na_hex_num
    OR.b #32, R0
    CMP.b #'a', R0
    BLO na_syntax
    CMP.b #'f', R0
    BHI na_syntax
    SUB.q #'a'-10, R0
    RET
na_hex_num:
    SUB.q #'0', R0
    RET
na_integer:
    MOV.q #10, R4
    CLR.q R5
    CMP.b #'0', (R20)
    BNE na_int_digits
    MOVZ.b.q 1(R20), R0
    OR.b #32, R0
    CMP.b #'x', R0
    BNE na_int_binary
    MOV.q #16, R4
    ADD.q #2, R20
    BRA na_int_digits
na_int_binary:
    CMP.b #'b', R0
    BNE na_int_digits
    MOV.q #2, R4
    ADD.q #2, R20
na_int_digits:
    CLR.q R6
na_int_loop:
    MOVZ.b.q (R20), R0
    CMP.b #'0', R0
    BLO na_int_done
    CMP.b #'9', R0
    BLE na_int_digit_num
    OR.b #32, R0
    CMP.b #'a', R0
    BLO na_int_done
    CMP.b #'f', R0
    BHI na_int_done
    SUB.q #'a'-10, R0
    BRA na_int_digit_ready
na_int_digit_num:
    SUB.q #'0', R0
na_int_digit_ready:
    CMP.q R4, R0
    BHS na_int_done
    ; overflow check through 128-bit unsigned product and carry
    UMULW.q R4, R5, R1, R2
    TST.q R2
    BNE na_number_range
    ADD.q R0, R1
    BLO na_number_range
    MOV.q R1, R5
    INC.q R20
    INC.q R6
    BRA na_int_loop
na_int_done:
    TST.q R6
    BEQ na_syntax
    MOV.q R5, R0
    RET

; Try a register token, restoring cursor on failure. R0=-1 or number; R1=0 R,
; 1 F, 2 PC. Integer aliases SP/FP are recognized before F-register syntax.
na_try_register:
    MOV.q R20, R7
    JSR na_name
    TST.q R0
    BEQ na_reg_no
    JSR na_upper_token
    MOV.q #na_token, R2
    MOVZ.b.q (R2), R0
    CMP.b #'S', R0
    BNE na_reg_fp_alias
    CMP.b #'P', 1(R2)
    BNE na_reg_no
    TST.b 2(R2)
    BNE na_reg_no
    MOV.q #31, R0
    CLR.q R1
    RET
na_reg_fp_alias:
    CMP.b #'F', R0
    BNE na_reg_pc_alias
    CMP.b #'P', 1(R2)
    BNE na_reg_letter
    TST.b 2(R2)
    BNE na_reg_no
    MOV.q #30, R0
    CLR.q R1
    RET
na_reg_pc_alias:
    CMP.b #'P', R0
    BNE na_reg_letter
    CMP.b #'C', 1(R2)
    BNE na_reg_no
    TST.b 2(R2)
    BNE na_reg_no
    CLR.q R0
    MOV.q #2, R1
    RET
na_reg_letter:
    CMP.b #'T', R0
    BNE na_reg_integer
    CMP.b #'P', 1(R2)
    BNE na_reg_no
    TST.b 2(R2)
    BNE na_reg_no
    CLR.q R0
    MOV.q #3, R1
    RET
na_reg_integer:
    CLR.q R1
    CMP.b #'R', R0
    BEQ na_reg_digits
    CMP.b #'F', R0
    BNE na_reg_no
    MOV.q #1, R1
na_reg_digits:
    INC.q R2
    TST.b (R2)
    BEQ na_reg_no
    CLR.q R0
na_reg_digit:
    MOVZ.b.q (R2)+, R3
    TST.b R3
    BEQ na_reg_done
    CMP.b #'0', R3
    BLO na_reg_no
    CMP.b #'9', R3
    BHI na_reg_no
    MUL.q #10, R0
    SUB.q #'0', R3
    ADD.q R3, R0
    CMP.q #31, R0
    BHI na_reg_no
    BRA na_reg_digit
na_reg_done:
    RET
na_reg_no:
    MOV.q R7, R20
    MOV.q #-1, R0
    RET

; Encode integer R0 in shortest immediate form for @na_opsize.
na_immediate:
    CMP.q #23, R0
    BHI na_imm_not_literal
    ADD.q #0xe0, R0
    JMP na_byte
na_imm_not_literal:
    MOV.q R0, R4
    MOV.q @na_opsize, R5
    CMP.q #8, R5
    BEQ na_imm_signed
    MUL.q #8, R5
    SUB3.q R5, #64, R5
    SHL.q R5, R4
    SAR.q R5, R4
    ; accepted values must be the unsigned or signed interpretation of low bits
    CMP.q R4, R0
    BEQ na_imm_signed
    MOV.q @na_opsize, R5
    MUL.q #8, R5
    MOV.q #1, R6
    SHL.q R5, R6
    CMP.q R6, R0
    BHS na_number_range
na_imm_signed:
    MOV.q #8, R5
    MOV.q #0xfb, R0
    CMP.q #-128, R4
    BLT na_imm_word
    CMP.q #127, R4
    BGT na_imm_word
    MOV.q #1, R5
    MOV.q #0xf8, R0
    BRA na_imm_emit
na_imm_word:
    CMP.q #-32768, R4
    BLT na_imm_long
    CMP.q #32767, R4
    BGT na_imm_long
    MOV.q #2, R5
    MOV.q #0xf9, R0
    BRA na_imm_emit
na_imm_long:
    CMP.q #-2147483648, R4
    BLT na_imm_emit
    CMP.q #2147483647, R4
    BGT na_imm_emit
    MOV.q #4, R5
    MOV.q #0xfa, R0
na_imm_emit:
    PUSH.q R4
    PUSH.q R5
    JSR na_byte
    POP.q R1
    POP.q R0
    JMP na_emit

na_operand:
    PUSH.q R17
    PUSH.q R18
    PUSH.q R19
    MOV.q @na_outlen, R19
    MOV.q R19, @na_opstart
    CLR.q @na_basevalid
    JSR na_spaces
    CMP.b #'#', R0
    BEQ na_operand_immediate
    JSR na_try_register
    CMP.q #-1, R0
    BEQ na_operand_memory
    CMP.q @na_optype, R1
    BNE na_bad_register
    JSR na_byte
    BRA na_operand_index
na_operand_immediate:
    INC.q R20
    TST.q @na_optype
    BEQ na_operand_int_imm
    JSR na_float_literal
    JSR na_fp_immediate
    BRA na_operand_index
na_operand_int_imm:
    JSR na_expression
    JSR na_immediate
    BRA na_operand_index
na_operand_memory:
    JSR na_spaces
    CMP.b #'@', R0
    BEQ na_operand_absolute
    CMP.b #'-', R0
    BNE na_operand_paren
    CMP.b #'(', 1(R20)
    BEQ na_operand_autodec
na_operand_paren:
    CMP.b #'(', R0
    BNE na_operand_displacement
    MOV.q R20, R18
    INC.q R20
    JSR na_try_register
    CMP.q #-1, R0
    BEQ na_operand_paren_expr
    CMP.q #3, R1
    BEQ na_operand_bare_tp
    CMP.q #2, R1
    BEQ na_operand_bare_pc
    CMP.q #0, R1
    BNE na_bad_register
    MOV.q R0, R17
    JSR na_spaces
    CMP.b #')', R0
    BNE na_syntax
    INC.q R20
    JSR na_spaces
    CMP.b #'+', R0
    BEQ na_operand_autoinc
    MOV.q #1, @na_basevalid
    ADD3.q #0x20, R17, R0
    JSR na_byte
    BRA na_operand_index
na_operand_bare_tp:
    CLR.q R18
    CLR.q R17
    BRA na_operand_tpbase
na_operand_bare_pc:
    JSR na_spaces
    CMP.b #')', R0
    BNE na_syntax
    INC.q R20
    CLR.q R17
    BRA na_pcrel_emit
na_operand_paren_expr:
    MOV.q R18, R20
    BRA na_operand_displacement
na_operand_autoinc:
    INC.q R20
    ADD3.q #0x40, R17, R0
    JSR na_byte
    BRA na_operand_index
na_operand_autodec:
    ADD.q #2, R20
    JSR na_try_register
    CMP.q #-1, R0
    BEQ na_syntax
    TST.q R1
    BNE na_bad_register
    MOV.q R0, R17
    JSR na_spaces
    CMP.b #')', R0
    BNE na_syntax
    INC.q R20
    ADD3.q #0x60, R17, R0
    JSR na_byte
    BRA na_operand_index
na_operand_absolute:
    INC.q R20
    JSR na_expression
    MOV.q R0, R17
    MOV.q #1, @na_basevalid
    CMP.q #-2147483648, R17
    BLT na_abs64
    CMP.q #2147483647, R17
    BGT na_abs64
    MOV.q #0xfc, R0
    JSR na_byte
    MOV.q R17, R0
    MOV.q #4, R1
    JSR na_emit
    BRA na_operand_index
na_abs64:
    MOV.q #0xfd, R0
    JSR na_byte
    MOV.q R17, R0
    MOV.q #8, R1
    JSR na_emit
    BRA na_operand_index
na_operand_displacement:
    CLR.q R18
    CMP.b #'*', (R20)
    BNE na_operand_bracket
    INC.q R20
    MOV.q #2, R18
    BRA na_operand_expr
na_operand_bracket:
    CMP.b #'[', (R20)
    BNE na_operand_expr
    INC.q R20
    MOV.q #1, R18
na_operand_expr:
    JSR na_expression
    MOV.q R0, R17
    JSR na_spaces
    CMP.b #'(', R0
    BNE na_operand_pcrel
    INC.q R20
    JSR na_try_register
    CMP.q #-1, R0
    BEQ na_syntax
    CMP.q #1, R1
    BEQ na_bad_register
    CMP.q #3, R1
    BEQ na_operand_tpbase
    CMP.q #2, R1
    BEQ na_operand_pcbase
    PUSH.q R0
    JSR na_spaces
    CMP.b #')', R0
    BNE na_syntax
    INC.q R20
    POP.q R6
    MOV.q #1, @na_basevalid
    TST.q R18
    BNE na_disp_indirect
    CMP.q #-128, R17
    BLT na_disp_word
    CMP.q #127, R17
    BGT na_disp_word
    ADD3.q #0x80, R6, R0
    JSR na_byte
    MOV.q R17, R0
    JSR na_byte
    BRA na_operand_index
na_disp_word:
    CMP.q #-32768, R17
    BLT na_disp_long
    CMP.q #32767, R17
    BGT na_disp_long
    ADD3.q #0xa0, R6, R0
    JSR na_byte
    MOV.q R17, R0
    MOV.q #2, R1
    JSR na_emit
    BRA na_operand_index
na_disp_long:
    PUSH.q R6
    MOV.q #0xff, R0
    JSR na_byte
    POP.q R6
    CMP.q #-2147483648, R17
    BLT na_disp_quad
    CMP.q #2147483647, R17
    BGT na_disp_quad
    MOV.q R6, R0
    JSR na_byte
    MOV.q R17, R0
    MOV.q #4, R1
    JSR na_emit
    BRA na_operand_index
na_disp_quad:
    ADD3.q #0x20, R6, R0
    JSR na_byte
    MOV.q R17, R0
    MOV.q #8, R1
    JSR na_emit
    BRA na_operand_index
na_disp_indirect:
    PUSH.q R6
    CMP.q #2, R18
    BEQ na_disp_indirect_emit
    JSR na_spaces
    CMP.b #']', R0
    BNE na_syntax
    INC.q R20
na_disp_indirect_emit:
    CMP.q #-2147483648, R17
    BLT na_number_range
    CMP.q #2147483647, R17
    BGT na_number_range
    MOV.q #0xff, R0
    JSR na_byte
    POP.q R6
    ADD3.q #0x40, R6, R0
    JSR na_byte
    MOV.q R17, R0
    MOV.q #4, R1
    JSR na_emit
    BRA na_operand_index
na_operand_tpbase:
    TST.q R18
    BNE na_bad_mode
    JSR na_spaces
    CMP.b #')', R0
    BNE na_syntax
    INC.q R20
    CMP.q #-2147483648, R17
    BLT na_number_range
    CMP.q #2147483647, R17
    BGT na_number_range
    MOV.q #1, @na_basevalid
    MOV.q #0xff, R0
    JSR na_byte
    MOV.q #0x60, R0
    JSR na_byte
    MOV.q R17, R0
    MOV.q #4, R1
    JSR na_emit
    BRA na_operand_index
na_operand_pcbase:
    TST.q R18
    BNE na_bad_mode
    JSR na_spaces
    CMP.b #')', R0
    BNE na_syntax
    INC.q R20
    BRA na_pcrel_emit
na_operand_pcrel:
    TST.q R18
    BNE na_syntax
na_pcrel_emit:
    MOV.q #1, @na_basevalid
    MOV.q #0xfe, R0
    JSR na_byte
    LEA @na_patch[R27], R6
    MOV.q @na_outlen, (R6)
    LEA @na_targets[R27], R6
    MOV.q R17, (R6)
    CLR.q R0
    MOV.q #4, R1
    JSR na_emit
na_operand_index:
    JSR na_spaces
    CMP.b #'[', R0
    BNE na_operand_done
    TST.q @na_basevalid
    BEQ na_bad_mode
    INC.q R20
    JSR na_try_register
    CMP.q #-1, R0
    BEQ na_syntax
    TST.q R1
    BNE na_bad_register
    ADD3.q #0xc0, R0, R17
    JSR na_spaces
    CMP.b #']', R0
    BNE na_syntax
    INC.q R20
    MOV.q @na_outlen, R0
    CMP.q @na_capacity, R0
    BHS na_output_full
    ADD.q #na_stage, R0
    MOV.q R19, R1
    ADD.q #na_stage, R1
na_insert_index:
    CMP.q R1, R0
    BEQ na_insert_index_done
    MOV.b -1(R0), (R0)
    DEC.q R0
    BRA na_insert_index
na_insert_index_done:
    MOV.b R17, (R0)
    INC.q @na_outlen
    INC.q @na_pc
    LEA @na_patch[R27], R6
    TST.q (R6)
    BEQ na_operand_done
    INC.q (R6)
na_operand_done:
    POP.q R19
    POP.q R18
    POP.q R17
    RET

na_statement:
    JSR na_spaces
    TST.b R0
    BEQ na_statement_return
    CMP.b #';', R0
    BEQ na_statement_return
    CMP.b #'.', R0
    BEQ na_directive
na_statement_name:
    JSR na_name
    TST.q R0
    BEQ na_syntax
    MOV.q R20, R23
    JSR na_spaces
    CMP.b #':', R0
    BNE na_instruction
    INC.q R20
    MOV.q @na_pc, R0
    JSR na_define
    BRA na_statement
na_statement_return:
    RET
na_instruction:
    MOV.q R23, R20
    JSR na_upper_token
    MOV.q #na_op_table, R4
na_op_lookup:
    TST.b (R4)
    BEQ na_unknown_instruction
    MOV.q #na_token, R1
    MOV.q R4, R2
    JSR na_equal
    TST.q R0
    BNE na_op_found
    ADD.q #32, R4
    BRA na_op_lookup
na_op_found:
    MOV.q 16(R4), R24
    MOVZ.l.q 24(R4), R25
    MOVZ.l.q 28(R4), R26
    MOV.q #8, R28
    CLR.q R29
    MOVZ.b.q (R20), R0
    CMP.q #9, R24
    BEQ na_conversion_sizes
    CMP.b #'.', R0
    BNE na_sizes_ready
    INC.q R20
    CMP.q #4, R24
    BNE na_size_integer
    MOV.q #1, R0
    BRA na_size_one
na_size_integer:
    CLR.q R0
na_size_one:
    JSR na_read_size
    MOV.q R0, R28
    BRA na_sizes_ready
na_conversion_sizes:
    CMP.b #'.', R0
    BNE na_syntax
    INC.q R20
    CLR.q R27
    JSR na_operand_type
    MOV.q @na_optype, R0
    JSR na_read_size
    MOV.q R0, R28
    CMP.b #'.', (R20)
    BNE na_conversion_dest
    INC.q R20
na_conversion_dest:
    MOV.q #1, R27
    JSR na_operand_type
    MOV.q @na_optype, R0
    JSR na_read_size
    MOV.q R0, R29
    CMP.q #8, R25
    BLO na_sizes_ready
    CMP.q R28, R29
    BNE na_bad_mode
na_sizes_ready:
    CMP.q #8, R24
    BNE na_check_cond_sizes
    CMP.q #6, R25
    BNE na_check_cond_sizes
    CMP.q #8, R28
    BNE na_bad_mode
na_check_cond_sizes:
    CMP.q #15, R24
    BEQ na_header
    CMP.q #16, R24
    BEQ na_header
    ; Fixed families have no size variants. LEA is always quad.
    CMP.q #8, R28
    BEQ na_header
    CMP.q #0, R24
    BEQ na_check_lea
    CMP.q #1, R24
    BEQ na_header
    CMP.q #4, R24
    BEQ na_header
    CMP.q #5, R24
    BEQ na_header
    CMP.q #8, R24
    BEQ na_header
    CMP.q #9, R24
    BEQ na_header
    CMP.q #10, R24
    BNE na_bad_mode
    CMP.q #3, R25
    BNE na_header
    CMP.q #1, R28
    BEQ na_bad_mode
    BRA na_header
na_check_lea:
    CMP.q #20, R25
    BEQ na_bad_mode
na_header:
    MOV.q @na_outlen, @na_insstart
    CLR.q @na_branch
    CLR.q @na_patch
    CLR.q @na_patch+8
    CLR.q @na_patch+16
    CLR.q @na_patch+24
    CMP.q #0, R24
    BEQ na_header_two
    CMP.q #1, R24
    BEQ na_header_three
    CMP.q #2, R24
    BEQ na_header_branch
    CMP.q #3, R24
    BEQ na_header_short
    CMP.q #4, R24
    BEQ na_header_fp
    CMP.q #5, R24
    BEQ na_header_block
    CMP.q #6, R24
    BEQ na_header_field
    CMP.q #7, R24
    BEQ na_header_bitbr
    CMP.q #8, R24
    BEQ na_header_atomic
    CMP.q #9, R24
    BEQ na_header_conv
    CMP.q #10, R24
    BEQ na_header_bits
    CMP.q #11, R24
    BEQ na_header_system
    CMP.q #12, R24
    BEQ na_header_loop
    CMP.q #13, R24
    BEQ na_header_case
    CMP.q #15, R24
    BEQ na_header_cond
    CMP.q #16, R24
    BEQ na_header_cond
    CMP.q #17, R24
    BEQ na_header_hint
    MOV.q #0xf0, R0
    JSR na_byte
    MOV.q R25, R0
    JSR na_byte
    BRA na_encode_operands
na_header_hint:
    MOV.q #0xf8, R0
    BRA na_header_fixed
na_header_cond:
    MOV.q #0xf8, R0
    JSR na_byte
    SHL3.q #2, R25, R17
    CMP.q #16, R24
    BNE na_header_cond_size
    ADD.q #0x40, R17
na_header_cond_size:
    MOV.q R28, R0
    JSR na_logsize
    ADD.q R17, R0
    JSR na_byte
    BRA na_encode_operands
na_header_two:
    SHL3.q #2, R25, R17
    BRA na_header_intsize
na_header_three:
    SHL3.q #2, R25, R17
    ADD.q #0x80, R17
na_header_intsize:
    MOV.q R28, R0
    JSR na_logsize
    ADD.q R17, R0
    JSR na_byte
    BRA na_encode_operands
na_header_branch:
    MOV.q #1, @na_branch
    SHL3.q #1, R25, R0
    ADD.q #0xc0, R0
    JSR na_byte
    BRA na_encode_operands
na_header_short:
    ADD3.q #0xe0, R25, R0
    JSR na_byte
    BRA na_encode_operands
na_header_fp:
    MOV.q #0xf0, R0
    JSR na_byte
    MOV.q R28, R0
    JSR na_logsize
    DEC.q R0
    SHL3.q #2, R25, R17
    ADD.q R17, R0
    JSR na_byte
    BRA na_encode_operands
na_header_block:
    MOV.q #0xf1, R0
    BRA na_header_sized
na_header_atomic:
    MOV.q #0xf3, R0
    BRA na_header_sized
na_header_bits:
    MOV.q #0xf5, R0
na_header_sized:
    JSR na_byte
    MOV.q R28, R0
    JSR na_logsize
    SHL3.q #2, R25, R17
    ADD.q R17, R0
    JSR na_byte
    BRA na_encode_operands
na_header_field:
    MOV.q #0xf2, R0
    BRA na_header_fixed
na_header_system:
    MOV.q #0xf6, R0
    BRA na_header_fixed
na_header_case:
    MOV.q #0xf7, R0
na_header_fixed:
    JSR na_byte
    MOV.q R25, R0
    JSR na_byte
    BRA na_encode_operands
na_header_bitbr:
    MOV.q #0xf2, R0
    JSR na_byte
    SHL3.q #1, R25, R0
    ADD.q #4, R0
    BRA na_header_branch_ext
na_header_loop:
    MOV.q #0xf7, R0
    JSR na_byte
    SHL3.q #1, R25, R0
na_header_branch_ext:
    MOV.q #1, @na_branch
    JSR na_byte
    BRA na_encode_operands
na_header_conv:
    MOV.q #0xf4, R0
    JSR na_byte
    SHL3.q #4, R25, R17
    CLR.q R27
    JSR na_operand_type
    MOV.q R28, R0
    JSR na_logsize
    SUB.q @na_optype, R0
    SHL.q #2, R0
    ADD.q R0, R17
    MOV.q #1, R27
    JSR na_operand_type
    MOV.q R29, R0
    JSR na_logsize
    SUB.q @na_optype, R0
    ADD.q R17, R0
    JSR na_byte
na_encode_operands:
    CLR.q R27
na_operand_loop:
    CMP.q R26, R27
    BHS na_operands_done
    TST.q R27
    BEQ na_operand_first
    JSR na_comma
na_operand_first:
    MOV.q @na_outlen, @na_opstart
    JSR na_operand_type
    CMP.q #11, R24
    BNE na_operand_normal
    CMP.q #0, R25
    BNE na_system_write_operand
    TST.q R27
    BEQ na_system_name_operand
    BRA na_operand_normal
na_system_write_operand:
    CMP.q #1, R25
    BNE na_operand_normal
    CMP.q #1, R27
    BNE na_operand_normal
na_system_name_operand:
    JSR na_system_number
    BRA na_operand_next
na_operand_normal:
    JSR na_operand
na_operand_next:
    CMP.q #13, R24
    BNE na_validate_system_literal
    CMP.q #2, R27
    BEQ na_require_literal
na_validate_system_literal:
    CMP.q #11, R24
    BNE na_operand_validated
    CMP.q #0, R25
    BNE na_validate_write_literal
    TST.q R27
    BEQ na_require_literal
    BRA na_operand_validated
na_validate_write_literal:
    CMP.q #1, R25
    BNE na_operand_validated
    CMP.q #1, R27
    BNE na_operand_validated
na_require_literal:
    MOV.q @na_opstart, R0
    ADD.q #na_stage, R0
    MOVZ.b.q (R0), R0
    CMP.q #0xe0, R0
    BLO na_bad_mode
    CMP.q #0xfb, R0
    BHI na_bad_mode
na_operand_validated:
    INC.q R27
    BRA na_operand_loop
na_operands_done:
    TST.q @na_branch
    BEQ na_fix_pcrel
    TST.q R26
    BEQ na_branch_argument
    JSR na_comma
na_branch_argument:
    JSR na_expression
    MOV.q R0, R17
    MOV.q #1, R18
    TST.q @na_pass
    BEQ na_branch_width_ready
    SUB3.q @na_pc, R17, R0
    DEC.q R0
    CMP.q #-128, R0
    BLT na_branch_long
    CMP.q #127, R0
    BLE na_branch_width_ready
na_branch_long:
    MOV.q #4, R18
    MOV.q @na_insstart, R0
    CMP.q #2, R24
    BEQ na_branch_set_form
    INC.q R0
na_branch_set_form:
    ADD.q #na_stage, R0
    OR.b #1, (R0)
na_branch_width_ready:
    SUB3.q @na_pc, R17, R0
    SUB.q R18, R0
    TST.q @na_pass
    BNE na_branch_not_first
    CLR.q R0
na_branch_not_first:
    CMP.q #-2147483648, R0
    BLT na_number_range
    CMP.q #2147483647, R0
    BGT na_number_range
    MOV.q R18, R1
    JSR na_emit
na_fix_pcrel:
    CLR.q R27
na_fix_next:
    CMP.q #4, R27
    BHS na_instruction_end
    MOV.q @na_patch[R27], R1
    TST.q R1
    BEQ na_fix_advance
    MOV.q @na_targets[R27], R0
    SUB.q @na_pc, R0
    CMP.q #-2147483648, R0
    BLT na_number_range
    CMP.q #2147483647, R0
    BGT na_number_range
    MOV.q #4, R2
    JSR na_patch_bytes
na_fix_advance:
    INC.q R27
    BRA na_fix_next
na_instruction_end:
    SUB3.q @na_location, @na_pc, R0
    CMP.q #48, R0
    BHI na_bad_mode
    JSR na_end
    RET
na_logsize:
    CLR.q R1
na_logsize_loop:
    CMP.q #1, R0
    BLE na_logsize_done
    SHR.q #1, R0
    INC.q R1
    BRA na_logsize_loop
na_logsize_done:
    MOV.q R1, R0
    RET
na_read_size:
    MOV.q R0, R2
    MOVZ.b.q (R20)+, R0
    OR.b #32, R0
    TST.q R2
    BNE na_read_fp_size
    CMP.b #'b', R0
    BNE na_read_w
    MOV.q #1, R0
    RET
na_read_w:
    CMP.b #'w', R0
    BNE na_read_l
    MOV.q #2, R0
    RET
na_read_l:
    CMP.b #'l', R0
    BNE na_read_q
    MOV.q #4, R0
    RET
na_read_q:
    CMP.b #'q', R0
    BNE na_bad_mode
    MOV.q #8, R0
    RET
na_read_fp_size:
    CMP.b #'h', R0
    BNE na_read_s
    MOV.q #2, R0
    RET
na_read_s:
    CMP.b #'s', R0
    BNE na_read_d
    MOV.q #4, R0
    RET
na_read_d:
    CMP.b #'d', R0
    BNE na_bad_mode
    MOV.q #8, R0
    RET
na_operand_type:
    CLR.q @na_optype
    MOV.q R28, @na_opsize
    CMP.q #4, R24
    BNE na_operand_convtype
    CMP.q #15, R25
    BNE na_operand_float_type
    CMP.q #1, R27
    BNE na_operand_float_type
    MOV.q #8, @na_opsize
    RET
na_operand_float_type:
    MOV.q #1, @na_optype
    RET
na_operand_convtype:
    CMP.q #9, R24
    BNE na_operand_type_done
    TST.q R27
    BEQ na_operand_conv_src
    MOV.q R29, @na_opsize
na_operand_conv_src:
    CMP.q #7, R25
    BEQ na_operand_float_type
    CMP.q #2, R25
    BEQ na_operand_float_dest
    CMP.q #3, R25
    BEQ na_operand_float_dest
    CMP.q #8, R25
    BEQ na_operand_float_dest
    CMP.q #4, R25
    BLO na_operand_type_done
    TST.q R27
    BEQ na_operand_float_type
    RET
na_operand_float_dest:
    TST.q R27
    BNE na_operand_float_type
na_operand_type_done:
    RET
na_system_number:
    MOV.q R20, R18
    JSR na_name
    TST.q R0
    BEQ na_system_normal
    JSR na_upper_token
    MOV.q #na_system_table, R4
na_system_find:
    TST.b (R4)
    BEQ na_system_normal
    MOV.q #na_token, R1
    MOV.q R4, R2
    JSR na_equal
    TST.q R0
    BNE na_system_found
    ADD.q #24, R4
    BRA na_system_find
na_system_found:
    MOV.q 16(R4), R0
    JSR na_immediate
    RET
na_system_normal:
    MOV.q R18, R20
    JSR na_operand
    RET

na_directive:
    INC.q R20
    JSR na_name
    JSR na_upper_token
    MOV.q #na_directive_table, R4
na_directive_find:
    TST.b (R4)
    BEQ na_unknown_directive
    MOV.q #na_token, R1
    MOV.q R4, R2
    JSR na_equal
    TST.q R0
    BNE na_directive_found
    ADD.q #24, R4
    BRA na_directive_find
na_directive_found:
    MOV.q 16(R4), R17
    CMP.q #0, R17
    BEQ na_directive_section
    CMP.q #1, R17
    BEQ na_directive_equ
    CMP.q #2, R17
    BEQ na_directive_entry
    CMP.q #10, R17
    BEQ na_directive_space
    CMP.q #11, R17
    BEQ na_directive_align
    CMP.q #12, R17
    BEQ na_directive_org
    CMP.q #22, R17
    BEQ na_directive_global
    CMP.q #20, R17
    BHS na_directive_string
    SUB.q #3, R17
    MOV.q #1, R0
    SHL.q R17, R0
    MOV.q R0, R17
na_directive_values:
    JSR na_expression
    CMP.q #8, R17
    BEQ na_directive_emit
    MOV.q R17, R1
    MUL.q #8, R1
    MOV.q #1, R2
    SHL.q R1, R2
    CMP.q R2, R0
    BLO na_directive_emit
    DEC.q R1
    MOV.q #-1, R2
    SHL.q R1, R2
    CMP.q R2, R0
    BLT na_number_range
na_directive_emit:
    MOV.q R17, R1
    JSR na_emit
    JSR na_spaces
    CMP.b #',', R0
    BNE na_directive_section
    INC.q R20
    BRA na_directive_values
na_directive_section:
    JSR na_end
    RET
na_directive_global:
    JSR na_name
    TST.q R0
    BEQ na_syntax
    JSR na_spaces
    CMP.b #',', R0
    BNE na_directive_section
    INC.q R20
    BRA na_directive_global
na_directive_entry:
    JSR na_expression
    MOV.q R0, @na_entry
    JMP na_end
na_directive_equ:
    JSR na_name
    TST.q R0
    BEQ na_syntax
    MOV.q #na_token, R1
    MOV.q #na_hold, R2
na_hold_name:
    MOV.b (R1)+, R3
    MOV.b R3, (R2)+
    BNE na_hold_name
    JSR na_comma
    JSR na_expression
    PUSH.q R0
    MOV.q #na_hold, R1
    MOV.q #na_token, R2
na_restore_name:
    MOV.b (R1)+, R3
    MOV.b R3, (R2)+
    BNE na_restore_name
    POP.q R0
    JSR na_define
    JMP na_end
na_directive_space:
    JSR na_expression
    MOV.q R0, R18
    BRA na_padding_fill
na_directive_org:
    JSR na_expression
    CMP.q @na_pc, R0
    BLO na_bad_org
    SUB3.q @na_pc, R0, R18
    BRA na_padding_fill
na_directive_align:
    JSR na_expression
    TST.q R0
    BEQ na_bad_mode
    MOV.q R0, R18
    MOV.q @na_pc, R2
    CLR.q R3
    UDIVW.q R18, R2
    TST.q R3
    BEQ na_padding_zero
    SUB3.q R3, R18, R18
    BRA na_padding_fill
na_padding_zero:
    CLR.q R18
na_padding_fill:
    CLR.q R19
    JSR na_spaces
    CMP.b #',', R0
    BNE na_padding_check
    INC.q R20
    JSR na_expression
    CMP.q #255, R0
    BLS na_padding_byte_valid
    CMP.q #-128, R0
    BLT na_number_range
na_padding_byte_valid:
    MOV.q R0, R19
na_padding_check:
    SUB3.q @na_outlen, @na_capacity, R0
    CMP.q R0, R18
    BHI na_output_full
na_padding_loop:
    TST.q R18
    BEQ na_directive_section
    MOV.q R19, R0
    JSR na_byte
    SOB R18, na_padding_loop
    BRA na_directive_section
na_directive_string:
    JSR na_spaces
    CMP.b #34, R0
    BEQ na_string_begin
    CMP.b #39, R0
    BNE na_syntax
na_string_begin:
    MOV.q R0, R18
    INC.q R20
na_string_char:
    MOVZ.b.q (R20), R0
    TST.b R0
    BEQ na_syntax
    CMP.b R18, R0
    BEQ na_string_end
    JSR na_character
    JSR na_byte
    BRA na_string_char
na_string_end:
    INC.q R20
    CMP.q #21, R17
    BNE na_string_comma
    CLR.q R0
    JSR na_byte
na_string_comma:
    JSR na_spaces
    CMP.b #',', R0
    BNE na_directive_section
    INC.q R20
    BRA na_directive_string

; Bounded line diagnostics. Errors reset only the private assembler call stack.
na_syntax:
    LEA na_e_syntax, R8
    BRA na_error
na_unknown_instruction:
    LEA na_e_instruction, R8
    BRA na_error
na_unknown_directive:
    LEA na_e_directive, R8
    BRA na_error
na_bad_arguments:
    LEA na_e_arguments, R8
    BRA na_error
na_line_too_long:
    LEA na_e_line, R8
    BRA na_error
na_name_too_long:
    LEA na_e_name, R8
    BRA na_error
na_symbols_full:
    LEA na_e_symbols, R8
    BRA na_error
na_duplicate:
    LEA na_e_duplicate, R8
    BRA na_error
na_undefined:
    LEA na_e_undefined, R8
    BRA na_error
na_number_range:
    LEA na_e_range, R8
    BRA na_error
na_div_zero:
    LEA na_e_divzero, R8
    BRA na_error
na_output_full:
    LEA na_e_full, R8
    BRA na_error
na_bad_register:
    LEA na_e_register, R8
    BRA na_error
na_bad_mode:
    LEA na_e_mode, R8
    BRA na_error
na_bad_org:
    LEA na_e_org, R8
na_error:
    MOV.q R8, R15
    MOV.q @na_diag, R13
    MOV.q @na_diagcap, R14
    TST.q R14
    BEQ na_error_done
    DEC.q R14
    LEA na_e_lineprefix, R8
    JSR na_diag_string
    MOV.q #na_numtext+32, R9
    CLR.b (R9)
    MOV.q @na_lineno, R2
na_diag_digits:
    CLR.q R3
    UDIVW.q #10, R2
    ADD.b #'0', R3
    MOV.b R3, -(R9)
    TST.q R2
    BNE na_diag_digits
    MOV.q R9, R8
    JSR na_diag_string
    LEA na_e_separator, R8
    JSR na_diag_string
    MOV.q R15, R8
    JSR na_diag_string
    CLR.b (R13)
na_error_done:
    MOV.q #1, R0
    CLR.q R1
    CLR.q R2
    JMP na_return
na_diag_string:
    TST.q R14
    BEQ na_diag_done
    MOV.b (R8)+, R0
    BEQ na_diag_done
    MOV.b R0, (R13)+
    DEC.q R14
    BRA na_diag_string
na_diag_done:
    RET
na_e_lineprefix: .asciz "line "
na_e_separator: .asciz ": "
na_e_syntax: .asciz "invalid syntax or missing operand"
na_e_instruction: .asciz "unknown instruction"
na_e_directive: .asciz "unknown directive"
na_e_arguments: .asciz "invalid assembler buffers"
na_e_line: .asciz "source line exceeds 1023 bytes"
na_e_name: .asciz "symbol exceeds 63 characters"
na_e_symbols: .asciz "symbol table full (256 symbols)"
na_e_duplicate: .asciz "duplicate symbol"
na_e_undefined: .asciz "undefined symbol"
na_e_range: .asciz "number or displacement out of range"
na_e_divzero: .asciz "division by zero in expression"
na_e_full: .asciz "output exceeds RAM file capacity"
na_e_register: .asciz "invalid register type"
na_e_mode: .asciz "reserved addressing mode or size"
na_e_org: .asciz "ORG cannot go backwards"
na_e_relax: .asciz "label relaxation did not converge"

.include "native_float.inc"
.include "native_tables.inc"
