.equ I_PATTERN, D_STATE+3072
.equ I_DIRECTORY, D_STATE+3136
; Prepare a directory and final-component wildcard. All filenames are canonical.
io_pattern:
    PUSHM #0x3fff
    MOV.q R0, R16
    TST.b (R16)
    BEQ io_pattern_cwd
    MOV.q R16, R17
    CLR.q R18
    CLR.q R19
io_pattern_scan:
    MOV.b (R17), R0
    BEQ io_pattern_scanned
    CMP.b #92, R0
    BEQ io_pattern_separator
    CMP.b #'/', R0
    BEQ io_pattern_separator
    CMP.b #'*', R0
    BEQ io_pattern_wild
    CMP.b #'?', R0
    BNE io_pattern_advance
io_pattern_wild:
    MOV.q #1, R19
    BRA io_pattern_advance
io_pattern_separator:
    MOV.q R17, R18
io_pattern_advance:
    INC.q R17
    BRA io_pattern_scan
io_pattern_scanned:
    TST.q R19
    BNE io_pattern_split
    MOV.q R16, R0
    JSR fs_path
    TST.q R0
    BNE io_pattern_return
    TST.b @D_PATH+3
    BEQ io_pattern_whole_dir
    MOV.q #D_PATH, R0
    JSR fs_find
    TST.q R0
    BEQ io_pattern_split
    CMP.q #3, 72(R0)
    BNE io_pattern_split
io_pattern_whole_dir:
    MOV.q #D_PATH, R0
    BRA io_pattern_directory
io_pattern_cwd:
    MOV.q #D_CWD, R0
io_pattern_directory:
    MOV.q #I_DIRECTORY, R1
    MOV.q #64, R2
    JSR u_copystr
    MOV.w #0x002a, @I_PATTERN
    BRA io_pattern_validate
io_pattern_split:
    TST.q R18
    BNE io_pattern_has_parent
    CMP.b #':', 1(R16)
    BNE io_pattern_no_parent
    ADD3.q #2, R16, R18
    MOV.w #0x3a41, @D_NAME2
    MOV.b (R16), @D_NAME2
    MOV.q #D_NAME2, R0
    JSR u_upper
    CLR.b @D_NAME2+2
    BRA io_pattern_parent_ready
io_pattern_no_parent:
    MOV.q R16, R18
    MOV.q #D_CWD, R0
    MOV.q #D_NAME2, R1
    MOV.q #64, R2
    JSR u_copystr
    BRA io_pattern_parent_ready
io_pattern_has_parent:
    ADD3.q #1, R18, R18
    SUB3.q R16, R18, R0
    CMP.q #63, R0
    BHI io_pattern_bad
    MOV.q R0, R22
    MOV.q R16, R1
    MOV.q #D_NAME2, R2
    MOVS.b
    CLR.b @D_NAME2[R22]
io_pattern_parent_ready:
    MOV.q R18, R0
    MOV.q #I_PATTERN, R1
    MOV.q #64, R2
    JSR u_copystr
    MOV.q #I_PATTERN, R0
    JSR u_upper
    MOV.q #D_NAME2, R0
    JSR fs_path
    TST.q R0
    BNE io_pattern_return
    MOV.q #D_PATH, R0
    MOV.q #I_DIRECTORY, R1
    MOV.q #64, R2
    JSR u_copystr
io_pattern_validate:
    CMP.b #67, @I_DIRECTORY
    BNE io_pattern_media_ready
    TST.q @D_CREADY
    BEQ io_pattern_bad
io_pattern_media_ready:
    TST.b @I_DIRECTORY+3
    BEQ io_pattern_ok
    MOV.q #I_DIRECTORY, R0
    JSR fs_find
    TST.q R0
    BEQ io_pattern_bad
    CMP.q #3, 72(R0)
    BNE io_pattern_bad
io_pattern_ok:
    ; DOS's conventional *.* also includes names with no extension.
    CMP.l #0x002a2e2a, @I_PATTERN
    BNE io_pattern_done
    MOV.w #0x002a, @I_PATTERN
io_pattern_done:
    CLR.q R0
    BRA io_pattern_return
io_pattern_bad:
    MOV.q #-4, R0
io_pattern_return:
    POPM #0x3fff
    RET
; Direct-child leaf of canonical R0 path in I_DIRECTORY, or 0.
io_leaf:
    MOV.q R0, R5
    MOV.q #I_DIRECTORY, R4
io_leaf_prefix:
    MOV.b (R4)+, R2
    BEQ io_leaf_boundary
    MOV.b (R5)+, R3
    CMP.b R2, R3
    BNE io_leaf_no
    BRA io_leaf_prefix
io_leaf_boundary:
    TST.b @I_DIRECTORY+3
    BEQ io_leaf_begin
    CMP.b #92, (R5)
    BNE io_leaf_no
    INC.q R5
io_leaf_begin:
    TST.b (R5)
    BEQ io_leaf_no
    MOV.q R5, R0
io_leaf_scan:
    MOV.b (R5)+, R2
    BEQ io_leaf_yes
    CMP.b #92, R2
    BEQ io_leaf_no
    BRA io_leaf_scan
io_leaf_yes:
    RET
io_leaf_no:
    CLR.q R0
    RET
; R0=leaf, R1=pattern. ? matches a byte; * matches zero or more.
io_match:
    MOV.q R0, R2
    MOV.q R1, R3
    CLR.q R4
    CLR.q R5
io_match_next:
    MOV.b (R2), R6
    BEQ io_match_tail
    MOV.b (R3), R7
    CMP.b #'*', R7
    BEQ io_match_star
    CMP.b #'?', R7
    BEQ io_match_char
    CMP.b R6, R7
    BEQ io_match_char
    TST.q R4
    BEQ io_match_no
    INC.q R5
    MOV.q R5, R2
    MOV.q R4, R3
    BRA io_match_next
io_match_star:
    INC.q R3
    MOV.q R3, R4
    MOV.q R2, R5
    BRA io_match_next
io_match_char:
    INC.q R2
    INC.q R3
    BRA io_match_next
io_match_tail:
    CMP.b #'*', (R3)
    BNE io_match_end
    INC.q R3
    BRA io_match_tail
io_match_end:
    TST.b (R3)
    BNE io_match_no
    MOV.q #1, R0
    RET
io_match_no:
    CLR.q R0
    RET
dos_dir:
    JSR io_pattern
    TST.q R0
    BNE dos_dir_return
    LEA io_directory, R0
    JSR u_puts
    MOV.q #I_DIRECTORY, R0
    JSR u_puts
    JSR u_newline
    MOV.q #D_FILES, R16
    MOV.q #D_SLOTS, R17
    CLR.q R18
dos_dir_next:
    TST.b (R16)
    BEQ dos_dir_advance
    MOV.q R16, R0
    JSR io_leaf
    TST.q R0
    BEQ dos_dir_advance
    MOV.q R0, R21
    MOV.q #I_PATTERN, R1
    JSR io_match
    TST.q R0
    BEQ dos_dir_advance
    MOV.q R21, R0
    JSR u_puts
    LEA io_spaces, R0
    JSR u_puts
    CMP.q #3, 72(R16)
    BEQ dos_dir_folder
    MOV.q 64(R16), R0
    JSR u_number
    BRA dos_dir_newline
dos_dir_folder:
    LEA io_dir_label, R0
    JSR u_puts
dos_dir_newline:
    JSR u_newline
    INC.q R18
dos_dir_advance:
    ADD.q #D_SLOT, R16
    SOB R17, dos_dir_next
    MOV.q R18, R0
    JSR u_number
    LEA io_files, R0
    JSR u_puts
    CLR.q R0
dos_dir_return:
    RET
dos_delete:
    MOV.q R0, R4
dos_delete_scan:
    MOV.b (R4)+, R2
    BEQ dos_delete_plain
    CMP.b #'*', R2
    BEQ dos_delete_wild
    CMP.b #'?', R2
    BNE dos_delete_scan
dos_delete_wild:
    TST.q R1
    BNE dos_delete_bad
    JSR io_pattern
    TST.q R0
    BNE dos_delete_return
    MOV.q #D_FILES, R16
    MOV.q #D_SLOTS, R17
    CLR.q R18
dos_delete_next:
    TST.b (R16)
    BEQ dos_delete_advance
    CMP.q #3, 72(R16)
    BEQ dos_delete_advance
    MOV.q R16, R0
    JSR io_leaf
    TST.q R0
    BEQ dos_delete_advance
    MOV.q #I_PATTERN, R1
    JSR io_match
    TST.q R0
    BEQ dos_delete_advance
    MOV.q R16, R0
    CLR.q R1
    JSR fs_delete
    TST.q R0
    BNE dos_delete_return
    INC.q R18
dos_delete_advance:
    ADD.q #D_SLOT, R16
    SOB R17, dos_delete_next
    TST.q R18
    BEQ dos_delete_missing
    CLR.q R0
    RET
dos_delete_plain:
    JMP fs_delete
dos_delete_missing:
    MOV.q #-1, R0
    RET
dos_delete_bad:
    MOV.q #-3, R0
dos_delete_return:
    RET
