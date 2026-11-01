; Kotl Upsilon BASIC: interpreted entirely by guest Upsilon instructions.
; Platform: 1 MiB RAM, serial DATA/STATUS at 0xffff0000/+4.
; R20 = parser cursor; R24 = running; R25 = line; R26 = GOTO taken.
.equ UART, 0xffff0000
.equ BUFFER, 0x10000
.equ NUMBUF, 0x10100
.equ PROGRAM, 0x20000
.equ STACK, 0x100000
.entry start
    .quad STACK, start
start:
    MOV.q #UART, R16
    CLR.q R11
    JSR clear_program
    LEA banner, R8
    JSR puts
prompt:
    MOV.q #STACK, SP
    CLR.q R24
    LEA prompt_text, R8
    JSR puts
    JSR read_line
    MOV.q #BUFFER, R20
    JSR spaces
    TST.b R1
    BEQ prompt
    CMP.b #'0', R1
    BLO immediate
    CMP.b #'9', R1
    BHI immediate
    JSR number
    CMP.q #1, R0
    BLT bad_line
    CMP.q #999, R0
    BGT bad_line
    SHL.q #7, R0
    ADD.q #PROGRAM, R0
    MOV.q R0, R9
    JSR spaces
store_line:
    MOV.b (R20)+, R0
    MOV.b R0, (R9)+
    BNE store_line
    BRA prompt
immediate:
    JSR statement
    TST.q R24
    BEQ prompt
run_next:
    CMP.q #999, R25
    BGT prompt
    MOV.q R25, R20
    SHL.q #7, R20
    ADD.q #PROGRAM, R20
    TST.b (R20)
    BEQ advance_line
    CLR.q R26
    JSR statement
    TST.q R24
    BEQ prompt
    TST.q R26
    BNE run_next
advance_line:
    INC.q R25
    BRA run_next

; A complete statement. Successful commands return to immediate/run dispatch.
statement:
    JSR spaces
    LEA kw_print, R8
    JSR keyword
    TST.q R0
    BNE do_print
    CMP.b #'?', (R20)
    BNE try_add
    INC.q R20
    BRA do_print
try_add:
    LEA kw_add, R8
    JSR keyword
    TST.q R0
    BNE do_add
    LEA kw_sub, R8
    JSR keyword
    TST.q R0
    BNE do_sub
    LEA kw_goto, R8
    JSR keyword
    TST.q R0
    BNE do_goto
    LEA kw_run, R8
    JSR keyword
    TST.q R0
    BNE do_run
    LEA kw_list, R8
    JSR keyword
    TST.q R0
    BNE do_list
    LEA kw_new, R8
    JSR keyword
    TST.q R0
    BNE do_new
    LEA kw_end, R8
    JSR keyword
    TST.q R0
    BNE do_end
    LEA kw_bye, R8
    JSR keyword
    TST.q R0
    BNE do_bye
    LEA kw_help, R8
    JSR keyword
    TST.q R0
    BEQ syntax
    JSR end_statement
    LEA banner, R8
    JSR puts
    RET

do_print:
    JSR spaces
    TST.b R1
    BEQ print_newline
print_item:
    CMP.b #'"', R1
    BNE print_expression
    INC.q R20
print_string:
    MOV.b (R20)+, R0
    BEQ syntax
    CMP.b #'"', R0
    BEQ print_after_item
    JSR putchar
    BRA print_string
print_expression:
    JSR expression
    JSR print_number
print_after_item:
    JSR spaces
    TST.b R1
    BEQ print_newline
    CMP.b #',', R1
    BNE syntax
    INC.q R20
    MOV.b #' ', R0
    JSR putchar
    JSR spaces
    BRA print_item
print_newline:
    MOV.b #10, R0
    JSR putchar
    RET

do_add:
    MOV.q #1, R15
    BRA binary_command
do_sub:
    CLR.q R15
binary_command:
    JSR expression
    MOV.q R0, R17
    JSR spaces
    CMP.b #',', R1
    BNE syntax
    INC.q R20
    JSR expression
    JSR end_statement
    TST.q R15
    BEQ subtract_command
    ADD.q R17, R0
    BVS number_range
    BRA binary_result
subtract_command:
    SUB3.q R0, R17, R0
    BVS number_range
binary_result:
    JSR print_number
    BRA print_newline

do_goto:
    JSR expression
    JSR end_statement
    CMP.q #1, R0
    BLT bad_line
    CMP.q #999, R0
    BGT bad_line
    MOV.q R0, R6
    SHL.q #7, R6
    ADD.q #PROGRAM, R6
    TST.b (R6)
    BEQ undefined_line
    MOV.q R0, R25
    MOV.q #1, R24
    MOV.q #1, R26
    RET
do_run:
    JSR end_statement
    MOV.q #1, R24
    MOV.q #1, R25
    MOV.q #1, R26
    RET
do_end:
    JSR end_statement
    CLR.q R24
    RET
do_bye:
    JSR end_statement
    HALT
do_new:
    JSR end_statement
    JSR clear_program
    CLR.q R24
    RET
clear_program:
    MOV.q #PROGRAM, R6
    MOV.q #1000, R7
clear_slot:
    CLR.b (R6)
    ADD.q #128, R6
    SOB R7, clear_slot
    RET
do_list:
    JSR end_statement
    MOV.q #1, R13
list_next:
    MOV.q R13, R14
    SHL.q #7, R14
    ADD.q #PROGRAM, R14
    TST.b (R14)
    BEQ list_advance
    MOV.q R13, R0
    JSR print_number
    MOV.b #' ', R0
    JSR putchar
    MOV.q R14, R8
    JSR puts
    MOV.b #10, R0
    JSR putchar
list_advance:
    INC.q R13
    CMP.q #999, R13
    BLE list_next
    RET

; expression := signed_decimal { ('+' | '-') signed_decimal }
expression:
    JSR number
    MOV.q R0, R4
expression_next:
    JSR spaces
    MOV.q R1, R5
    CMP.b #'+', R5
    BEQ expression_term
    CMP.b #'-', R5
    BNE expression_done
expression_term:
    INC.q R20
    JSR number
    CMP.b #'+', R5
    BNE expression_sub
    ADD.q R0, R4
    BVS number_range
    BRA expression_next
expression_sub:
    SUB.q R0, R4
    BVS number_range
    BRA expression_next
expression_done:
    MOV.q R4, R0
    RET
number:
    JSR spaces
    CLR.q R3
    CMP.b #'-', R1
    BNE number_plus
    MOV.q #1, R3
    INC.q R20
    BRA number_start
number_plus:
    CMP.b #'+', R1
    BNE number_start
    INC.q R20
number_start:
    JSR spaces
    CMP.b #'0', R1
    BLO syntax
    CMP.b #'9', R1
    BHI syntax
    CLR.q R0
number_digit:
    MOVZ.b.q (R20), R2
    CMP.b #'0', R2
    BLO number_done
    CMP.b #'9', R2
    BHI number_done
    SUB.q #'0', R2
    CMP.q #922337203685477580, R0
    BHI number_range
    BNE number_accumulate
    ADD3.q #7, R3, R1
    CMP.q R1, R2
    BHI number_range
number_accumulate:
    MUL.q #10, R0
    ADD.q R2, R0
    INC.q R20
    BRA number_digit
number_done:
    TST.q R3
    BEQ number_return
    NEG.q R0, R0
number_return:
    RET
spaces:
    MOVZ.b.q (R20), R1
    CMP.b #' ', R1
    BEQ skip_space
    CMP.b #9, R1
    BNE spaces_done
skip_space:
    INC.q R20
    BRA spaces
spaces_done:
    RET
end_statement:
    JSR spaces
    TST.b R1
    BNE syntax
    RET

; Match a case-insensitive keyword at R20, preserving the cursor on failure.
keyword:
    MOV.q R20, R6
keyword_char:
    MOVZ.b.q (R8)+, R2
    BEQ keyword_boundary
    MOVZ.b.q (R6)+, R1
    CMP.b #'a', R1
    BLO keyword_compare
    CMP.b #'z', R1
    BHI keyword_compare
    SUB.b #32, R1
keyword_compare:
    CMP.b R2, R1
    BNE keyword_no
    BRA keyword_char
keyword_boundary:
    MOVZ.b.q (R6), R1
    TST.b R1
    BEQ keyword_yes
    CMP.b #' ', R1
    BEQ keyword_yes
    CMP.b #9, R1
    BEQ keyword_yes
    CMP.b #'"', R1
    BNE keyword_no
keyword_yes:
    MOV.q R6, R20
    MOV.q #1, R0
    RET
keyword_no:
    CLR.q R0
    RET

; Serial routines. putchar changes only R1; puts also uses R0/R8.
putchar:
    MOV.b 4(R16), R1
    AND.b #2, R1
    BEQ putchar
    MOV.b R0, (R16)
    RET
puts:
    MOV.b (R8)+, R0
    BEQ puts_done
    JSR putchar
    BRA puts
puts_done:
    RET
getchar:
    MOVZ.b.q 4(R16), R1
    AND3.q #1, R1, R0
    BNE receive
    AND.q #4, R1
    BEQ getchar
    MOV.q #4, R0
    RET
receive:
    MOVZ.b.q (R16), R0
    RET
read_line:
    MOV.q #BUFFER, R9
    CLR.q R10
    CLR.q R12
read_key:
    JSR getchar
    CMP.b #10, R0
    BNE not_lf
    TST.q R11
    BEQ line_end
    CLR.q R11
    BRA read_key
not_lf:
    CLR.q R11
    CMP.b #13, R0
    BNE not_cr
    MOV.q #1, R11
    BRA line_end
not_cr:
    CMP.b #4, R0
    BNE not_eof
    TST.q R10
    BNE line_end
    HALT
not_eof:
    CMP.b #8, R0
    BEQ backspace
    CMP.b #127, R0
    BEQ backspace
    CMP.b #9, R0
    BEQ printable
    CMP.b #32, R0
    BLO read_key
    CMP.b #126, R0
    BHI read_key
printable:
    CMP.q #120, R10
    BGE too_long
    MOV.b R0, (R9)+
    INC.q R10
    JSR putchar
    BRA read_key
backspace:
    TST.q R10
    BEQ read_key
    DEC.q R10
    DEC.q R9
    MOV.b #8, R0
    JSR putchar
    MOV.b #' ', R0
    JSR putchar
    MOV.b #8, R0
    JSR putchar
    BRA read_key
too_long:
    MOV.q #1, R12
    BRA read_key
line_end:
    CLR.b (R9)
    MOV.b #10, R0
    JSR putchar
    TST.q R12
    BNE line_too_long
    RET

; Signed decimal formatting, including -9223372036854775808.
print_number:
    MOV.q R0, R2
    MOV.q #NUMBUF+32, R9
    CLR.b (R9)
    TST.q R2
    BPL digit_loop
    NEG.q R2, R2
    MOV.b #'-', R0
    JSR putchar
digit_loop:
    CLR.q R3
    UDIVW.q #10, R2
    ADD.b #'0', R3
    MOV.b R3, -(R9)
    TST.q R2
    BNE digit_loop
    MOV.q R9, R8
    JSR puts
    RET
syntax:
    LEA syntax_text, R8
    BRA error
bad_line:
    LEA line_text, R8
    BRA error
undefined_line:
    LEA undefined_text, R8
    BRA error
number_range:
    LEA range_text, R8
    BRA error
line_too_long:
    LEA long_text, R8
error:
    MOV.q #STACK, SP
    JSR puts
    BRA prompt

banner: .asciz "Kotl Upsilon BASIC v0.4\nPRINT, ADD, SUB, GOTO | RUN, LIST, NEW, END, BYE\n"
prompt_text: .asciz "> "
syntax_text: .asciz "?SYNTAX ERROR\n"
line_text: .asciz "?LINE NUMBER 1..999\n"
undefined_text: .asciz "?UNDEFINED LINE\n"
range_text: .asciz "?NUMBER RANGE\n"
long_text: .asciz "?LINE TOO LONG\n"
kw_print: .asciz "PRINT"
kw_add: .asciz "ADD"
kw_sub: .asciz "SUB"
kw_goto: .asciz "GOTO"
kw_run: .asciz "RUN"
kw_list: .asciz "LIST"
kw_new: .asciz "NEW"
kw_end: .asciz "END"
kw_bye: .asciz "BYE"
kw_help: .asciz "HELP"
