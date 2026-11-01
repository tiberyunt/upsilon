; VI.EXE's native editor service. Modal ANSI terminal UI; 8191-byte text buffer.
.equ V_NAME, D_STATE+1024
.equ V_MODE, D_STATE+1088
.equ V_PENDING, D_STATE+1096
.equ V_VIEW, D_STATE+1104
.equ V_MESSAGE, D_STATE+1112
.equ V_UNDOLEN, D_STATE+1120
.equ V_UNDOPOINT, D_STATE+1128
.equ V_UNDO, D_STATE+1136
vi_main:
    PUSHM #0x3fff
    MOV.q R0, R20
    MOV.q #D_NAME, R0
    JSR u_token
    CMP.q #0, R0
    BLE vi_usage
    MOV.q #D_NAME, R0
    JSR fs_path
    TST.q R0
    BNE vi_error
    MOV.q #D_PATH, R0
    MOV.q #V_NAME, R1
    MOV.q #64, R2
    JSR u_copystr
    MOV.q #V_NAME, R0
    MOV.q #D_EDIT, R1
    MOV.q #8191, R2
    JSR fs_read
    CMP.q #-1, R0
    BEQ vi_new
    CMP.q #0, R0
    BLT vi_error
    TST.q R1
    BNE vi_error
    BRA vi_loaded
vi_new:
    CLR.q R0
vi_loaded:
    MOV.q R0, @D_EDITLEN
    CLR.b @D_EDIT[R0]
    CLR.q @D_CURSOR
    CLR.q @D_DIRTY
    CLR.q @V_MODE
    CLR.q @V_PENDING
    CLR.q @V_VIEW
    CLR.q @V_MESSAGE
    CLR.q @V_UNDO
vi_loop:
    JSR vi_render
    JSR vi_key
    CMP.q #-1, R0
    BEQ vi_eof
    CMP.b #4, R0
    BEQ vi_eof
    TST.q @V_MODE
    BEQ vi_normal
    CMP.b #27, R0
    BEQ vi_escape
    CMP.b #13, R0
    BEQ vi_loop
    CMP.b #8, R0
    BEQ vi_backspace
    CMP.b #127, R0
    BEQ vi_backspace
    CMP.b #10, R0
    BEQ vi_insert_key
    CMP.b #9, R0
    BEQ vi_insert_key
    CMP.b #32, R0
    BLO vi_loop
    CMP.b #126, R0
    BHI vi_loop
vi_insert_key:
    PUSH.q R0
    JSR vi_snapshot
    POP.q R0
    JSR vi_insert
    BRA vi_loop
vi_backspace:
    TST.q @D_CURSOR
    BEQ vi_loop
    JSR vi_snapshot
    DEC.q @D_CURSOR
    JSR vi_delete
    BRA vi_loop
vi_escape:
    CLR.q @V_MODE
    ; Decode already queued ANSI arrows, preserving a following ':' or command.
    MOVZ.b.q @D_UART+4, R0
    AND.q #1, R0
    BEQ vi_loop
    JSR u_getc
    CMP.b #'[', R0
    BNE vi_escape_pending
    JSR u_getc
    CMP.b #'A', R0
    BEQ vi_up
    CMP.b #'B', R0
    BEQ vi_down
    CMP.b #'C', R0
    BEQ vi_right
    CMP.b #'D', R0
    BEQ vi_left
    BRA vi_loop
vi_escape_pending:
    MOV.q R0, @V_PENDING
    BRA vi_loop
vi_normal:
    CLR.q @V_MESSAGE
    CMP.b #':', R0
    BEQ vi_colon
    CMP.b #'i', R0
    BEQ vi_insert_mode
    CMP.b #'a', R0
    BEQ vi_append_mode
    CMP.b #'I', R0
    BEQ vi_insert_start
    CMP.b #'A', R0
    BEQ vi_append_end
    CMP.b #'o', R0
    BEQ vi_open_line
    CMP.b #'h', R0
    BEQ vi_left
    CMP.b #'l', R0
    BEQ vi_right
    CMP.b #'j', R0
    BEQ vi_down
    CMP.b #'k', R0
    BEQ vi_up
    CMP.b #'0', R0
    BEQ vi_start_line
    CMP.b #'$', R0
    BEQ vi_end_line
    CMP.b #'G', R0
    BEQ vi_end_file
    CMP.b #'g', R0
    BEQ vi_gg
    CMP.b #'x', R0
    BEQ vi_x
    CMP.b #'d', R0
    BEQ vi_dd
    CMP.b #'u', R0
    BEQ vi_undo
    CMP.b #27, R0
    BEQ vi_escape
    BRA vi_loop
vi_insert_mode:
    MOV.q #1, @V_MODE
    BRA vi_loop
vi_append_mode:
    MOV.q @D_CURSOR, R0
    CMP.q @D_EDITLEN, R0
    BHS vi_insert_mode
    CMP.b #10, @D_EDIT[R0]
    BEQ vi_insert_mode
    INC.q @D_CURSOR
    BRA vi_insert_mode
vi_insert_start:
    JSR vi_line_start
    MOV.q R0, @D_CURSOR
    BRA vi_insert_mode
vi_append_end:
    JSR vi_line_end
    MOV.q R0, @D_CURSOR
    BRA vi_insert_mode
vi_open_line:
    JSR vi_snapshot
    JSR vi_line_end
    MOV.q R0, @D_CURSOR
    MOV.q #10, R0
    JSR vi_insert
    BRA vi_insert_mode
vi_left:
    TST.q @D_CURSOR
    BEQ vi_loop
    DEC.q @D_CURSOR
    BRA vi_loop
vi_right:
    MOV.q @D_CURSOR, R0
    CMP.q @D_EDITLEN, R0
    BHS vi_loop
    INC.q @D_CURSOR
    BRA vi_loop
vi_start_line:
    JSR vi_line_start
    MOV.q R0, @D_CURSOR
    BRA vi_loop
vi_end_line:
    JSR vi_line_end
    MOV.q R0, @D_CURSOR
    BRA vi_loop
vi_end_file:
    MOV.q @D_EDITLEN, @D_CURSOR
    BRA vi_loop
vi_gg:
    JSR vi_key
    CMP.b #'g', R0
    BNE vi_escape_pending
    CLR.q @D_CURSOR
    BRA vi_loop
vi_up:
    JSR vi_line_start
    MOV.q R0, R16
    SUB3.q R16, @D_CURSOR, R18
    TST.q R16
    BEQ vi_loop
    DEC.q R16
vi_up_find:
    TST.q R16
    BEQ vi_vertical
    CMP.b #10, @D_EDIT-1[R16]
    BEQ vi_vertical
    DEC.q R16
    BRA vi_up_find
vi_down:
    JSR vi_line_start
    SUB3.q R0, @D_CURSOR, R18
    JSR vi_line_end
    CMP.q @D_EDITLEN, R0
    BHS vi_loop
    ADD3.q #1, R0, R16
vi_vertical:
    TST.q R18
    BEQ vi_vertical_done
    CMP.q @D_EDITLEN, R16
    BHS vi_vertical_done
    CMP.b #10, @D_EDIT[R16]
    BEQ vi_vertical_done
    INC.q R16
    SOB R18, vi_vertical
vi_vertical_done:
    MOV.q R16, @D_CURSOR
    BRA vi_loop
vi_x:
    JSR vi_snapshot
    JSR vi_delete
    BRA vi_loop
vi_dd:
    JSR vi_key
    CMP.b #'d', R0
    BNE vi_escape_pending
    JSR vi_snapshot
    JSR vi_line_start
    MOV.q R0, R16
    JSR vi_line_end
    MOV.q R0, R17
    CMP.q @D_EDITLEN, R17
    BHS vi_dd_copy
    INC.q R17
vi_dd_copy:
    SUB3.q R17, @D_EDITLEN, R0
    INC.q R0
    ADD3.q #D_EDIT, R17, R1
    ADD3.q #D_EDIT, R16, R2
    MOVS.b
    SUB3.q R16, R17, R0
    SUB.q R0, @D_EDITLEN
    MOV.q R16, @D_CURSOR
    MOV.q #1, @D_DIRTY
    BRA vi_loop
vi_undo:
    TST.q @V_UNDO
    BEQ vi_loop
    MOV.q @V_UNDOLEN, R0
    INC.q R0
    MOV.q #D_TEMP, R1
    MOV.q #D_EDIT, R2
    MOVS.b
    MOV.q @V_UNDOLEN, @D_EDITLEN
    MOV.q @V_UNDOPOINT, @D_CURSOR
    CLR.q @V_UNDO
    MOV.q #1, @D_DIRTY
    BRA vi_loop
vi_key:
    MOV.q @V_PENDING, R0
    BEQ vi_key_uart
    CLR.q @V_PENDING
    RET
vi_key_uart:
    JMP u_getc
vi_snapshot:
    MOV.q @D_EDITLEN, @V_UNDOLEN
    MOV.q @D_CURSOR, @V_UNDOPOINT
    MOV.q #1, @V_UNDO
    MOV.q @D_EDITLEN, R0
    INC.q R0
    MOV.q #D_EDIT, R1
    MOV.q #D_TEMP, R2
    MOVS.b
    RET
vi_insert:
    MOV.q @D_EDITLEN, R16
    CMP.q #8191, R16
    BHS vi_buffer_full
    MOV.q @D_CURSOR, R17
    MOV.q R16, R18
vi_insert_shift:
    MOV.b @D_EDIT[R18], R1
    ADD3.q #1, R18, R2
    MOV.b R1, @D_EDIT[R2]
    CMP.q R17, R18
    BEQ vi_insert_store
    DEC.q R18
    BRA vi_insert_shift
vi_insert_store:
    MOV.b R0, @D_EDIT[R17]
    INC.q @D_EDITLEN
    INC.q @D_CURSOR
    MOV.q #1, @D_DIRTY
    RET
vi_buffer_full:
    MOV.q #vi_full_text, @V_MESSAGE
    RET
vi_delete:
    MOV.q @D_CURSOR, R16
    CMP.q @D_EDITLEN, R16
    BHS vi_delete_done
vi_delete_shift:
    ADD3.q #1, R16, R1
    MOV.b @D_EDIT[R1], R2
    MOV.b R2, @D_EDIT[R16]
    INC.q R16
    CMP.q @D_EDITLEN, R16
    BLO vi_delete_shift
    DEC.q @D_EDITLEN
    MOV.q #1, @D_DIRTY
vi_delete_done:
    RET
vi_line_start:
    MOV.q @D_CURSOR, R0
vi_line_start_next:
    TST.q R0
    BEQ vi_line_start_done
    CMP.b #10, @D_EDIT-1[R0]
    BEQ vi_line_start_done
    DEC.q R0
    BRA vi_line_start_next
vi_line_start_done:
    RET
vi_line_end:
    MOV.q @D_CURSOR, R0
vi_line_end_next:
    CMP.q @D_EDITLEN, R0
    BHS vi_line_end_done
    CMP.b #10, @D_EDIT[R0]
    BEQ vi_line_end_done
    INC.q R0
    BRA vi_line_end_next
vi_line_end_done:
    RET
vi_colon:
    LEA vi_cmd_position, R0
    JSR u_puts
    MOV.q #D_LINE, R0
    MOV.q #120, R1
    JSR u_line
    CMP.q #0, R0
    BLT vi_loop
    MOV.q #D_LINE, R20
    MOV.q #D_NAME, R0
    JSR u_token
    MOV.q #D_NAME, R0
    JSR u_upper
    CMP.w #0x0051, @D_NAME
    BEQ vi_quit_check
    CMP.w #0x2151, @D_NAME
    BNE vi_colon_w
    TST.b @D_NAME+2
    BEQ vi_quit
vi_colon_w:
    CMP.w #0x0057, @D_NAME
    BEQ vi_write_command
    CMP.w #0x2157, @D_NAME
    BNE vi_colon_wq
    TST.b @D_NAME+2
    BEQ vi_write_command
vi_colon_wq:
    CMP.w #0x5157, @D_NAME
    BNE vi_colon_x
    TST.b @D_NAME+2
    BEQ vi_write_quit
vi_colon_x:
    CMP.w #0x0058, @D_NAME
    BEQ vi_write_quit
    MOV.q #vi_commands_text, @V_MESSAGE
    BRA vi_loop
vi_write_quit:
    MOV.q #1, R29
    BRA vi_write
vi_write_command:
    CLR.q R29
vi_write:
    MOV.q #D_NAME2, R0
    JSR u_token
    CMP.q #-1, R0
    BEQ vi_write_failed
    TST.q R0
    BNE vi_write_as
    MOV.q #V_NAME, R0
    BRA vi_write_file
vi_write_as:
    MOV.q #D_NAME2, R0
vi_write_file:
    MOV.q #D_EDIT, R1
    MOV.q @D_EDITLEN, R2
    CLR.q R3
    CLR.q R4
    JSR fs_write
    TST.q R0
    BNE vi_write_failed
    MOV.q #D_PATH, R0
    MOV.q #V_NAME, R1
    MOV.q #64, R2
    JSR u_copystr
    CLR.q @D_DIRTY
    MOV.q #vi_written_text, @V_MESSAGE
    TST.q R29
    BNE vi_quit
    BRA vi_loop
vi_write_failed:
    MOV.q #vi_write_error_text, @V_MESSAGE
    BRA vi_loop
vi_quit_check:
    TST.q @D_DIRTY
    BEQ vi_quit
    MOV.q #vi_unsaved_text, @V_MESSAGE
    BRA vi_loop
vi_eof:
    TST.q @D_DIRTY
    BEQ vi_quit
    LEA vi_eof_text, R0
    JSR u_puts
vi_quit:
    LEA vi_exit_screen, R0
    JSR u_puts
    CLR.q R0
    POPM #0x3fff
    RET
vi_usage:
    LEA vi_usage_text, R0
    JSR u_puts
    MOV.q #-3, R0
    POPM #0x3fff
    RET
vi_error:
    LEA vi_read_error, R0
    JSR u_puts
    MOV.q #-2, R0
    POPM #0x3fff
    RET
; Twenty text rows, status/message below; cursor and viewport follow the buffer.
vi_render:
    CLR.q R16
    CLR.q R17
    CLR.q R18
vi_cursor_scan:
    CMP.q @D_CURSOR, R16
    BHS vi_cursor_counted
    MOV.b @D_EDIT[R16], R0
    CMP.b #10, R0
    BNE vi_cursor_column
    INC.q R17
    CLR.q R18
    BRA vi_cursor_scan_next
vi_cursor_column:
    INC.q R18
vi_cursor_scan_next:
    INC.q R16
    BRA vi_cursor_scan
vi_cursor_counted:
    CMP.q @V_VIEW, R17
    BHS vi_view_down
    MOV.q R17, @V_VIEW
vi_view_down:
    ADD3.q #20, @V_VIEW, R0
    CMP.q R0, R17
    BLO vi_view_ready
    SUB3.q #19, R17, @V_VIEW
vi_view_ready:
    LEA vi_home, R0
    JSR u_puts
    MOV.q #D_EDIT, R19
    CLR.q R21
vi_skip_view:
    CMP.q @V_VIEW, R21
    BHS vi_draw
    MOV.b (R19)+, R0
    BEQ vi_draw
    CMP.b #10, R0
    BNE vi_skip_view
    INC.q R21
    BRA vi_skip_view
vi_draw:
    MOV.q #20, R22
vi_draw_row:
    CLR.q R23
    TST.b (R19)
    BNE vi_draw_char
    MOV.b #'~', @D_UART
    BRA vi_draw_newline
vi_draw_char:
    MOV.b (R19), R0
    BEQ vi_draw_newline
    INC.q R19
    CMP.b #10, R0
    BEQ vi_draw_newline
    CMP.q #78, R23
    BHS vi_draw_char
    CMP.b #9, R0
    BNE vi_draw_print
    MOV.q #' ', R0
vi_draw_print:
    MOV.b R0, @D_UART
    INC.q R23
    BRA vi_draw_char
vi_draw_newline:
    JSR u_newline
    SOB R22, vi_draw_row
    TST.q @V_MODE
    BEQ vi_draw_normal
    LEA vi_insert_text, R0
    BRA vi_draw_status
vi_draw_normal:
    LEA vi_normal_text, R0
vi_draw_status:
    JSR u_puts
    MOV.q #V_NAME, R0
    JSR u_puts
    TST.q @D_DIRTY
    BEQ vi_draw_length
    LEA vi_modified_text, R0
    JSR u_puts
vi_draw_length:
    JSR u_newline
    MOV.q @V_MESSAGE, R0
    TST.q R0
    BEQ vi_position
    JSR u_puts
vi_position:
    LEA vi_esc_bracket, R0
    JSR u_puts
    SUB3.q @V_VIEW, R17, R0
    INC.q R0
    JSR u_number
    MOV.b #';', @D_UART
    MOV.q R18, R0
    CMP.q #78, R0
    BLS vi_position_col
    MOV.q #78, R0
vi_position_col:
    INC.q R0
    JSR u_number
    MOV.b #'H', @D_UART
    RET
vi_home:
    .byte 27
    .ascii "[H"
    .byte 27
    .asciz "[2J"
vi_cmd_position:
    .byte 27
    .ascii "[22;1H"
    .byte 27
    .asciz "[K:"
vi_exit_screen:
    .byte 27
    .ascii "[H"
    .byte 27
    .asciz "[2J"
vi_esc_bracket:
    .byte 27
    .asciz "["
vi_insert_text: .asciz "-- INSERT -- "
vi_normal_text: .asciz "-- NORMAL -- "
vi_modified_text: .asciz " [+]"
vi_written_text: .asciz "Written to volume."
vi_write_error_text: .asciz "Write failed: full, invalid path or write-protected volume."
vi_unsaved_text: .asciz "Unsaved changes. Use :w, :wq or :q!."
vi_full_text: .asciz "Buffer full (8191 bytes)."
vi_commands_text: .asciz ":w [file] | :q | :q! | :wq | :x"
vi_eof_text: .asciz "Editor EOF: unsaved changes discarded.\n"
vi_usage_text: .asciz "Usage: VI filename\n"
vi_read_error: .asciz "Cannot edit file: plain text only, maximum 8191 bytes.\n"
