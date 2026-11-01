; Exact decimal-to-binary64 conversion using 2048-bit integer arithmetic.
; Up to 128 significand digits; signed exponent. Nearest-even literal encoding.
.equ na_big_n, 0x7c000
.equ na_big_d, 0x7c100
na_big_clear:
    MOV.q #na_big_n, R4
    MOV.q #64, R5
na_big_clear_loop:
    CLR.q (R4)+
    SOB R5, na_big_clear_loop
    MOV.q #1, @na_big_d
    RET
; R0=array R1=added digit; return carry in R0. Clobbers R0..R7.
na_big_mul10:
    MOV.q R0, R4
    MOV.q #32, R5
    MOV.q R1, R6
na_big_mul_loop:
    UMULW.q #10, (R4), R2, R3
    ADD.q R6, R2
    ADC.q #0, R3
    MOV.q R2, (R4)+
    MOV.q R3, R6
    SOB R5, na_big_mul_loop
    MOV.q R6, R0
    RET
; R0=array, shift left one. Returns carry.
na_big_left:
    MOV.q R0, R4
    MOV.q #32, R5
    CLR.q R6
na_big_left_loop:
    MOV.q (R4), R2
    SHR3.q #63, R2, R7
    SHL.q #1, R2
    OR.q R6, R2
    MOV.q R2, (R4)+
    MOV.q R7, R6
    SOB R5, na_big_left_loop
    MOV.q R6, R0
    RET
na_big_right:
    MOV.q #na_big_d+248, R4
    MOV.q #32, R5
    CLR.q R6
na_big_right_loop:
    MOV.q (R4), R2
    AND3.q #1, R2, R7
    SHR.q #1, R2
    SHL.q #63, R6
    OR.q R6, R2
    MOV.q R2, (R4)
    SUB.q #8, R4
    MOV.q R7, R6
    SOB R5, na_big_right_loop
    RET
na_big_compare:
    MOV.q #na_big_n+248, R4
    MOV.q #na_big_d+248, R5
    MOV.q #32, R6
na_big_compare_loop:
    MOV.q (R4), R0
    CMP.q (R5), R0
    BLO na_big_less
    BHI na_big_greater
    SUB.q #8, R4
    SUB.q #8, R5
    SOB R6, na_big_compare_loop
    CLR.q R0
    RET
na_big_less:
    MOV.q #-1, R0
    RET
na_big_greater:
    MOV.q #1, R0
    RET
na_big_subtract:
    MOV.q #na_big_n, R4
    MOV.q #na_big_d, R5
    MOV.q #32, R7
    CLR.q R6
na_big_sub_loop:
    SHR.q #1, R6
    SBB.q (R5)+, (R4)+
    MOV.q #0, R6
    ADC.q #0, R6
    SOB R7, na_big_sub_loop
    RET
na_big_zero:
    MOV.q #na_big_n, R4
    MOV.q #32, R5
    CLR.q R0
na_big_zero_loop:
    OR.q (R4)+, R0
    SOB R5, na_big_zero_loop
    TST.q R0
    RET
na_float_literal:
    PUSH.q R17
    PUSH.q R18
    PUSH.q R19
    PUSH.q R21
    PUSH.q R22
    PUSH.q R23
    JSR na_big_clear
    CLR.q R17
    CLR.q R18
    CLR.q R19
    CLR.q R21
    JSR na_spaces
    CMP.b #'-', R0
    BNE na_float_plus
    MOV.q #1, R17
    INC.q R20
    BRA na_float_start
na_float_plus:
    CMP.b #'+', R0
    BNE na_float_start
    INC.q R20
na_float_start:
    JSR na_spaces
    CMP.b #'.', R0
    BEQ na_float_decimal
    CMP.b #'0', R0
    BLO na_float_symbol
    CMP.b #'9', R0
    BLE na_float_decimal
    CMP.b #'.', R0
    BEQ na_float_decimal
    BRA na_float_symbol
na_float_decimal:
    CMP.b #'0', (R20)
    BNE na_float_digits
    MOVZ.b.q 1(R20), R0
    OR.b #32, R0
    CMP.b #'x', R0
    BEQ na_float_integer
    CMP.b #'b', R0
    BEQ na_float_integer
na_float_digits:
    MOVZ.b.q (R20), R0
    CMP.b #'.', R0
    BNE na_float_digit
    TST.q R19
    BNE na_float_exponent
    MOV.q #1, R19
    INC.q R20
    BRA na_float_digits
na_float_digit:
    CMP.b #'0', R0
    BLO na_float_exponent
    CMP.b #'9', R0
    BHI na_float_exponent
    INC.q R21
    CMP.q #128, R21
    BHI na_float_precision
    SUB.q #'0', R0
    MOV.q R0, R1
    MOV.q #na_big_n, R0
    JSR na_big_mul10
    TST.q R0
    BNE na_float_precision
    ADD.q R19, R18
    INC.q R20
    BRA na_float_digits
na_float_exponent:
    TST.q R21
    BEQ na_syntax
    NEG.q R18, R23
    MOVZ.b.q (R20), R0
    OR.b #32, R0
    CMP.b #'e', R0
    BNE na_float_scale
    INC.q R20
    CLR.q R22
    CMP.b #'-', (R20)
    BNE na_float_exp_plus
    MOV.q #1, R22
    INC.q R20
    BRA na_float_exp_digits
na_float_exp_plus:
    CMP.b #'+', (R20)
    BNE na_float_exp_digits
    INC.q R20
na_float_exp_digits:
    CLR.q R18
    CLR.q R19
na_float_exp_loop:
    MOVZ.b.q (R20), R0
    CMP.b #'0', R0
    BLO na_float_exp_done
    CMP.b #'9', R0
    BHI na_float_exp_done
    CMP.q #10000, R18
    BHI na_float_exp_skip_accum
    MUL.q #10, R18
    SUB.q #'0', R0
    ADD.q R0, R18
na_float_exp_skip_accum:
    INC.q R20
    INC.q R19
    BRA na_float_exp_loop
na_float_exp_done:
    TST.q R19
    BEQ na_syntax
    TST.q R22
    BEQ na_float_exp_apply
    NEG.q R18, R18
na_float_exp_apply:
    ADD.q R18, R23
    BRA na_float_scale
na_float_symbol:
    MOV.q R20, R23
    JSR na_name
    JSR na_upper_token
    MOV.q #na_token, R1
    LEA na_word_nan, R2
    JSR na_equal
    TST.q R0
    BNE na_float_nan
    MOV.q #na_token, R1
    LEA na_word_inf, R2
    JSR na_equal
    TST.q R0
    BNE na_float_infinity
    MOV.q R23, R20
na_float_integer:
    JSR na_expression
    TST.q R0
    BPL na_float_integer_positive
    NEG.q R0, R0
    XOR.q #1, R17
na_float_integer_positive:
    MOV.q R0, @na_big_n
    CLR.q R23
na_float_scale:
    JSR na_big_zero
    BEQ na_float_zero
    CMP.q #500, R23
    BGT na_float_infinity
    CMP.q #-500, R23
    BLT na_float_zero
    TST.q R23
    BEQ na_float_normalize
    BPL na_float_scale_n
    NEG.q R23, R23
na_float_scale_d_loop:
    MOV.q #na_big_d, R0
    CLR.q R1
    JSR na_big_mul10
    SOB R23, na_float_scale_d_loop
    BRA na_float_normalize
na_float_scale_n:
    MOV.q #na_big_n, R0
    CLR.q R1
    JSR na_big_mul10
    TST.q R0
    BNE na_float_infinity
    SOB R23, na_float_scale_n
na_float_normalize:
    CLR.q R23
    JSR na_big_compare
    TST.q R0
    BMI na_float_shift_n
na_float_shift_d:
    MOV.q #na_big_d, R0
    JSR na_big_left
    INC.q R23
    CMP.q #1024, R23
    BGT na_float_infinity
    JSR na_big_compare
    TST.q R0
    BPL na_float_shift_d
    JSR na_big_right
    DEC.q R23
    BRA na_float_precision_bits
na_float_shift_n:
    MOV.q #na_big_n, R0
    JSR na_big_left
    DEC.q R23
    JSR na_big_compare
    TST.q R0
    BMI na_float_shift_n
na_float_precision_bits:
    CMP.q #1023, R23
    BGT na_float_infinity
    CMP.q #-1075, R23
    BLT na_float_zero
    MOV.q #53, R22
    CMP.q #-1022, R23
    BGE na_float_divide
    ADD.q R23, R22
    ADD.q #1022, R22
na_float_divide:
    CLR.q R21
    TST.q R22
    BEQ na_float_half_minimum
na_float_divide_bit:
    SHL.q #1, R21
    JSR na_big_compare
    TST.q R0
    BMI na_float_divide_shift
    OR.q #1, R21
    JSR na_big_subtract
na_float_divide_shift:
    MOV.q #na_big_n, R0
    JSR na_big_left
    SOB R22, na_float_divide_bit
    JSR na_big_compare
    TST.q R0
    BMI na_float_pack
    BGT na_float_round_up
    AND3.q #1, R21, R0
    BEQ na_float_pack
na_float_round_up:
    INC.q R21
    BRA na_float_pack
na_float_half_minimum:
    JSR na_big_compare
    TST.q R0
    BEQ na_float_zero
    MOV.q #1, R21
na_float_pack:
    CMP.q #-1022, R23
    BLT na_float_subnormal
    CMP.q #0x20000000000000, R21
    BLO na_float_exponent_bits
    SHR.q #1, R21
    INC.q R23
na_float_exponent_bits:
    CMP.q #1023, R23
    BGT na_float_infinity
    AND.q #0xfffffffffffff, R21
    ADD.q #1023, R23
    SHL.q #52, R23
    OR3.q R23, R21, R0
    BRA na_float_sign
na_float_subnormal:
    MOV.q R21, R0
    BRA na_float_sign
na_float_zero:
    CLR.q R0
    BRA na_float_sign
na_float_nan:
    MOV.q #0x7ff8000000000000, R0
    BRA na_float_return
na_float_infinity:
    MOV.q #0x7ff0000000000000, R0
na_float_sign:
    TST.q R17
    BEQ na_float_return
    OR.q #0x8000000000000000, R0
na_float_return:
    POP.q R23
    POP.q R22
    POP.q R21
    POP.q R19
    POP.q R18
    POP.q R17
    RET
na_float_precision:
    LEA na_e_float_precision, R8
    BRA na_error
na_fp_immediate:
    PUSH.q R17
    MOV.q R0, R17
    MOV.q #na_fp_table, R4
    CLR.q R5
na_fp_table_next:
    CMP.q (R4), R17
    BEQ na_fp_short
    ADD.q #8, R4
    INC.q R5
    CMP.q #24, R5
    BLO na_fp_table_next
    MTFS #0
    MOVIF.q.d R17, F0
    CVTFF.d.s F0, F1
    CVTFF.s.d F1, F2
    MOVFI.d.q F2, R0
    CMP.q R17, R0
    BNE na_fp_full
    MOVFI.s.l F1, R0
    PUSH.q R0
    MOV.q #0xfa, R0
    JSR na_byte
    POP.q R0
    MOV.q #4, R1
    JSR na_emit
    BRA na_fp_done
na_fp_full:
    MOV.q #0xfb, R0
    JSR na_byte
    MOV.q R17, R0
    MOV.q #8, R1
    JSR na_emit
    BRA na_fp_done
na_fp_short:
    ADD3.q #0xe0, R5, R0
    JSR na_byte
na_fp_done:
    POP.q R17
    RET
na_word_nan: .asciz "NAN"
na_word_inf: .asciz "INF"
na_e_float_precision: .asciz "floating literal exceeds 128 significand digits"
