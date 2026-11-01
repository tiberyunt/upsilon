; R0..R7 caller-clobbered; R8..R29 preserved by these primitives.
u_putc:
    MOV.b R0, @D_UART
    RET
u_puts:
    MOV.q R0, R6
u_puts_loop:
    MOV.b (R6)+, R0
    BEQ u_puts_done
    MOV.b R0, @D_UART
    BRA u_puts_loop
u_puts_done:
    RET
u_newline:
    MOV.b #10, @D_UART
    RET
u_getc:
    MOVZ.b.q @D_UART+4, R0
    AND3.q #1, R0, R1
    BNE u_getc_ready
    AND.q #4, R0
    BEQ u_getc
    MOV.q #-1, R0
    RET
u_getc_ready:
    MOVZ.b.q @D_UART, R0
    RET
; R0=buffer R1=capacity incl NUL. R0=len, -1 EOF, -2 overflow.
u_line:
    PUSHM #0x000f
    MOV.q R0, R16
    MOV.q R1, R17
    CLR.q R18
    CLR.q R19
u_line_key:
    JSR u_getc
    CMP.q #-1, R0
    BEQ u_line_eof
    CMP.b #4, R0
    BEQ u_line_eof
    CMP.b #13, R0
    BEQ u_line_key
    CMP.b #10, R0
    BEQ u_line_done
    CMP.b #8, R0
    BEQ u_line_back
    CMP.b #127, R0
    BEQ u_line_back
    CMP.b #9, R0
    BEQ u_line_char
    CMP.b #32, R0
    BLO u_line_key
    CMP.b #126, R0
    BHI u_line_key
u_line_char:
    ADD3.q #1, R18, R2
    CMP.q R17, R2
    BHS u_line_over
    MOV.b R0, (R16)[R18]
    INC.q R18
    JSR u_putc
    BRA u_line_key
u_line_over:
    MOV.q #1, R19
    BRA u_line_key
u_line_back:
    TST.q R18
    BEQ u_line_key
    DEC.q R18
    MOV.b #8, @D_UART
    MOV.b #' ', @D_UART
    MOV.b #8, @D_UART
    BRA u_line_key
u_line_eof:
    TST.q R18
    BNE u_line_done
    CLR.b (R16)
    MOV.q #-1, R0
    BRA u_line_return
u_line_done:
    CLR.b (R16)[R18]
    JSR u_newline
    MOV.q R18, R0
    TST.q R19
    BEQ u_line_return
    MOV.q #-2, R0
u_line_return:
    POPM #0x000f
    RET
; decimal unsigned output
u_number:
    MOV.q #D_STATE+512, R6
    CLR.b (R6)
    MOV.q R0, R3
u_number_next:
    MOV.q R3, R4
    DIV.q #10, R3
    MUL3.q #10, R3, R5
    SUB3.q R5, R4, R4
    ADD.q #'0', R4
    DEC.q R6
    MOV.b R4, (R6)
    TST.q R3
    BNE u_number_next
    MOV.q R6, R0
    JMP u_puts
; R0=source,R1=destination. Does not copy more than R2 bytes including NUL.
u_copystr:
    TST.q R2
    BEQ u_copystr_done
    MOV.b (R0)+, R3
    MOV.b R3, (R1)+
    BEQ u_copystr_done
    SOB R2, u_copystr
u_copystr_done:
    RET
; R0/R1 strings, result R0=1 equal, 0 unequal (case-sensitive).
u_equal:
    MOV.b (R0)+, R2
    MOV.b (R1)+, R3
    CMP.b R2, R3
    BNE u_equal_no
    TST.b R2
    BNE u_equal
    MOV.q #1, R0
    RET
u_equal_no:
    CLR.q R0
    RET
; R0 pointer, result R0 length; R1 preserved.
u_strlen:
    MOV.q R0, R4
u_strlen_loop:
    TST.b (R4)
    BEQ u_strlen_done
    INC.q R4
    BRA u_strlen_loop
u_strlen_done:
    SUB.q R0, R4
    MOV.q R4, R0
    RET
; Command argument tokenizer: R20 cursor, R0=destination cap64, result R0=len.
; Skips spaces; optional double quotes. R20 advances; source untouched.
u_token:
    MOV.q R0, R5
    CLR.q R7
    CLR.q R4
u_token_space:
    MOV.b (R20), R1
    CMP.b #' ', R1
    BEQ u_token_skip
    CMP.b #9, R1
    BNE u_token_begin
u_token_skip:
    INC.q R20
    BRA u_token_space
u_token_begin:
    CMP.b #34, R1
    BNE u_token_loop
    INC.q R20
    MOV.q #1, R7
u_token_loop:
    MOV.b (R20), R1
    BEQ u_token_done
    TST.q R7
    BEQ u_token_unquoted
    CMP.b #34, R1
    BEQ u_token_quote_end
    BRA u_token_char
u_token_unquoted:
    CMP.b #' ', R1
    BEQ u_token_done
    CMP.b #9, R1
    BEQ u_token_done
u_token_char:
    CMP.q #63, R4
    BHS u_token_long
    MOV.b R1, (R5)+
    INC.q R4
    INC.q R20
    BRA u_token_loop
u_token_quote_end:
    INC.q R20
u_token_done:
    CLR.b (R5)
    MOV.q R4, R0
    RET
u_token_long:
    ; Consume the rest, then reject instead of truncating a path silently.
    MOV.b (R20)+, R1
    BEQ u_token_bad
    CMP.b #' ', R1
    BNE u_token_long
u_token_bad:
    CLR.b (R5)
    MOV.q #-1, R0
    RET
u_upper:
    MOV.q R0, R4
u_upper_loop:
    MOV.b (R4), R1
    BEQ u_upper_done
    CMP.b #'a', R1
    BLO u_upper_next
    CMP.b #'z', R1
    BHI u_upper_next
    SUB.b #32, (R4)
u_upper_next:
    INC.q R4
    BRA u_upper_loop
u_upper_done:
    RET
