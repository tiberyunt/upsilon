; Append-only UPSDOS1 tape filesystem, entirely guest code.
; Public helpers preserve R16..R29; R0..R15 are scratch.
; Slot: path[64], length, kind(0=data,1=exe,2=obj,3=dir), entry, reserved, data.
fs_init:
    CLR.q @D_SEQ
    CLR.q @D_RO
    CLR.q @D_EXECSP
    CLR.q @D_CSEQ
    CLR.q @D_CRO
    CLR.q @D_CTAIL
    CLR.q @D_CREADY
    CLR.q @D_CFAILED
    MOV.l #0x005c3a41, @D_CWD
    CMP.q #67, @D_BOOTDRIVE
    BNE fs_init_drive_ready
    MOV.b #67, @D_CWD
fs_init_drive_ready:
    MOV.q #D_FILES, R4
    MOV.q #D_SLOTS, R5
fs_init_next:
    CLR.b (R4)
    ADD.q #D_SLOT, R4
    SOB R5, fs_init_next
    RET
; Resolve DOS paths into D_PATH; case-insensitive, 8.3 components, . and ...
fs_path:
    PUSHM #0x3fff
    MOV.q R0, R16
    MOV.q #3, R17
    MOV.l #0x005c3a41, @D_PATH
    MOV.b @D_CWD, @D_PATH
    MOV.b (R16), R0
    CMP.b #':', 1(R16)
    BNE fs_path_drive_done
    OR.b #32, R0
    CMP.b #'a', R0
    BEQ fs_path_drive_a
    CMP.b #'c', R0
    BNE fs_path_bad
    MOV.b #67, @D_PATH
    BRA fs_path_drive_set
fs_path_drive_a:
    MOV.b #65, @D_PATH
fs_path_drive_set:
    ADD.q #2, R16
fs_path_drive_done:
    MOV.b (R16), R0
    CMP.b #92, R0
    BEQ fs_path_component
    CMP.b #'/', R0
    BEQ fs_path_component
    MOV.b @D_PATH, R0
    CMP.b @D_CWD, R0
    BNE fs_path_component
    MOV.q #D_CWD+3, R18
fs_path_cwd:
    MOV.b (R18)+, R0
    BEQ fs_path_component
    MOV.b R0, @D_PATH[R17]
    INC.q R17
    BRA fs_path_cwd
fs_path_component:
    MOV.b (R16), R0
    BEQ fs_path_done
    CMP.b #92, R0
    BEQ fs_path_skip
    CMP.b #'/', R0
    BNE fs_path_start
fs_path_skip:
    INC.q R16
    BRA fs_path_component
fs_path_start:
    MOV.q R16, R19
    CLR.q R21
fs_path_count:
    MOV.b (R16), R0
    BEQ fs_path_counted
    CMP.b #92, R0
    BEQ fs_path_counted
    CMP.b #'/', R0
    BEQ fs_path_counted
    INC.q R21
    INC.q R16
    CMP.q #12, R21
    BHI fs_path_bad
    BRA fs_path_count
fs_path_counted:
    CMP.q #1, R21
    BNE fs_path_dotdot
    CMP.b #'.', (R19)
    BEQ fs_path_component
fs_path_dotdot:
    CMP.q #2, R21
    BNE fs_path_append
    CMP.w #0x2e2e, (R19)
    BNE fs_path_append
fs_path_pop:
    CMP.q #3, R17
    BLS fs_path_component
    DEC.q R17
    CMP.b #92, @D_PATH[R17]
    BNE fs_path_pop
    BRA fs_path_component
fs_path_append:
    ADD3.q R17, R21, R0
    CMP.q #3, R17
    BEQ fs_path_length
    INC.q R0
fs_path_length:
    CMP.q #63, R0
    BHI fs_path_bad
    CMP.q #3, R17
    BEQ fs_path_chars
    MOV.b #92, @D_PATH[R17]
    INC.q R17
fs_path_chars:
    CLR.q R23
    CLR.q R24
    CLR.q R25
fs_path_char:
    MOV.b (R19)+, R0
    CMP.b #'.', R0
    BEQ fs_path_extension
    CMP.b #'a', R0
    BLO fs_path_valid_upper
    CMP.b #'z', R0
    BHI fs_path_valid_upper
    SUB.b #32, R0
fs_path_valid_upper:
    CMP.b #'A', R0
    BLO fs_path_valid_digit
    CMP.b #'Z', R0
    BLS fs_path_valid
fs_path_valid_digit:
    CMP.b #'0', R0
    BLO fs_path_valid_punct
    CMP.b #'9', R0
    BLS fs_path_valid
fs_path_valid_punct:
    CMP.b #'_', R0
    BEQ fs_path_valid
    CMP.b #'-', R0
    BNE fs_path_bad
fs_path_valid:
    TST.q R25
    BNE fs_path_extchar
    INC.q R23
    CMP.q #8, R23
    BHI fs_path_bad
    BRA fs_path_store
fs_path_extchar:
    INC.q R24
    CMP.q #3, R24
    BHI fs_path_bad
    BRA fs_path_store
fs_path_extension:
    TST.q R23
    BEQ fs_path_bad
    TST.q R25
    BNE fs_path_bad
    MOV.q #1, R25
fs_path_store:
    MOV.b R0, @D_PATH[R17]
    INC.q R17
    SOB R21, fs_path_char
    TST.q R25
    BEQ fs_path_component
    TST.q R24
    BEQ fs_path_bad
    BRA fs_path_component
fs_path_done:
    CLR.b @D_PATH[R17]
    CLR.q R0
    POPM #0x3fff
    RET
fs_path_bad:
    MOV.q #-3, R0
    POPM #0x3fff
    RET
; Canonical R0 path -> R0=slot/0, R1=first free/0.
fs_find:
    MOV.q R0, R11
    MOV.q #D_FILES, R8
    MOV.q #D_SLOTS, R9
    CLR.q R10
    CLR.q R12
fs_find_next:
    TST.b (R8)
    BNE fs_find_compare
    TST.q R10
    BNE fs_find_advance
    MOV.q R8, R10
    BRA fs_find_advance
fs_find_compare:
    MOV.b (R11), R0
    CMP.b (R8), R0
    BNE fs_find_compare_name
    INC.q R12
fs_find_compare_name:
    MOV.q R11, R0
    MOV.q R8, R1
    JSR u_equal
    TST.q R0
    BNE fs_find_yes
fs_find_advance:
    ADD.q #D_SLOT, R8
    SOB R9, fs_find_next
    CLR.q R0
    CMP.q #D_VOLUME_SLOTS, R12
    BLO fs_find_free_ready
    CLR.q R10
fs_find_free_ready:
    MOV.q R10, R1
    RET
fs_find_yes:
    MOV.q R8, R0
    MOV.q R10, R1
    RET
; Validate parent of D_PATH exists as a directory. Root is implicit.
fs_parent:
    CMP.b #67, @D_PATH
    BNE fs_parent_media_ok
    TST.q @D_CREADY
    BEQ fs_parent_no
fs_parent_media_ok:
    MOV.q #D_PATH, R0
    MOV.q #D_PATH2, R1
    MOV.q #64, R2
    JSR u_copystr
    MOV.q #D_PATH2, R0
    JSR u_strlen
    MOV.q R0, R5
fs_parent_back:
    CMP.q #3, R5
    BLS fs_parent_yes
    DEC.q R5
    CMP.b #92, @D_PATH2[R5]
    BNE fs_parent_back
    CMP.q #2, R5
    BEQ fs_parent_yes
    CLR.b @D_PATH2[R5]
    MOV.q #D_PATH2, R0
    JSR fs_find
    TST.q R0
    BEQ fs_parent_no
    CMP.q #3, 72(R0)
    BNE fs_parent_no
fs_parent_yes:
    CLR.q R0
    RET
fs_parent_no:
    MOV.q #-4, R0
    RET
; FNV-1a, header checksum field must be zero when called.
fs_hash:
    MOV.q R0, R2
    MOV.q R1, R3
    MOV.q #0xcbf29ce484222325, R4
fs_hash_next:
    TST.q R3
    BEQ fs_hash_done
    MOVZ.b.q (R2)+, R5
    XOR.q R5, R4
    MUL.q #0x100000001b3, R4
    SOB R3, fs_hash_next
fs_hash_done:
    MOV.q R4, R0
    RET
; Record in R0. Applies a validated event to the in-memory directory.
fs_apply:
    PUSHM #0x3fff
    MOV.q R0, R16
    MOV.q 16(R16), R17
    LEA 24(R16), R0
    JSR fs_find
    MOV.q R0, R18
    CMP.q #1, R17
    BEQ fs_apply_put
    TST.q R18
    BEQ fs_apply_ok
    CMP.q #2, R17
    BNE fs_apply_rename
    CLR.b (R18)
    BRA fs_apply_ok
fs_apply_rename:
    LEA 88(R16), R0
    MOV.q R18, R1
    MOV.q #64, R2
    JSR u_copystr
    BRA fs_apply_ok
fs_apply_put:
    TST.q R18
    BNE fs_apply_slot
    MOV.q R1, R18
    TST.q R18
    BEQ fs_apply_full
fs_apply_slot:
    MOV.q #64, R0
    LEA 24(R16), R1
    MOV.q R18, R2
    MOVS.b
    MOV.q 152(R16), 64(R18)
    MOV.q 160(R16), 72(R18)
    MOV.q 168(R16), 80(R18)
    MOV.q 152(R16), R0
    LEA 192(R16), R1
    LEA D_FILEDATA(R18), R2
    MOVS.b
fs_apply_ok:
    CLR.q R0
    POPM #0x3fff
    RET
fs_apply_full:
    MOV.q #-5, R0
    POPM #0x3fff
    RET
; Replay journal after the boot file. Corruption stops replay and write-protects.
fs_mount:
    PUSHM #0x3fff
    JSR fs_init
    CLR.q @D_REPLAY
    MOVZ.b.q @D_TAPE+4, R0
    AND.q #1, R0
    BEQ fs_mount_done
    MOV.b #1, @D_TAPE+8
fs_mount_next:
    MOV.b #2, @D_TAPE+8
    MOVZ.b.q @D_TAPE+4, R16
    AND3.q #16, R16, R0
    BNE fs_mount_bad
    AND3.q #8, R16, R0
    BNE fs_mount_done
    AND3.q #4, R16, R0
    BEQ fs_mount_record
    MOV.q #1, @D_REPLAY
    BRA fs_mount_next
fs_mount_record:
    TST.q @D_REPLAY
    BEQ fs_mount_next
    MOV.q @D_TAPE+16, R17
    CMP.q #192, R17
    BLO fs_mount_bad
    CMP.q #65728, R17
    BHI fs_mount_bad
    MOV.q R17, R0
    MOV.q #D_RECOVER, R2
    FILL.b @D_TAPE
fs_mount_check:
    CMP.q #D_MAGIC, @D_RECOVER
    BNE fs_mount_bad
    MOV.q @D_RECOVER+152, R0
    CMP.q #D_MAXFILE, R0
    BHI fs_mount_bad
    ADD.q #192, R0
    CMP.q R17, R0
    BNE fs_mount_bad
    MOV.q @D_SEQ, R0
    TST.q @D_CREADY
    BEQ fs_mount_sequence
    MOV.q @D_CSEQ, R0
fs_mount_sequence:
    INC.q R0
    CMP.q @D_RECOVER+8, R0
    BNE fs_mount_bad
    MOV.q @D_RECOVER+16, R0
    CMP.q #1, R0
    BLO fs_mount_bad
    CMP.q #3, R0
    BHI fs_mount_bad
    CMP.q #3, @D_RECOVER+160
    BHI fs_mount_bad
    MOV.q @D_RECOVER+176, R18
    CLR.q @D_RECOVER+176
    MOV.q #D_RECOVER, R0
    MOV.q R17, R1
    JSR fs_hash
    MOV.q R18, @D_RECOVER+176
    CMP.q R18, R0
    BNE fs_mount_bad
    MOV.q #65, R0
    TST.q @D_CREADY
    BEQ fs_mount_drive_check
    MOV.q #67, R0
fs_mount_drive_check:
    CMP.b @D_RECOVER+24, R0
    BNE fs_mount_bad
    ; Bound strings before any path helper reads them.
    MOV.q #D_RECOVER+24, R19
    MOV.q #64, R21
fs_mount_name:
    TST.b (R19)+
    BEQ fs_mount_name_ok
    SOB R21, fs_mount_name
    BRA fs_mount_bad
fs_mount_name_ok:
    MOV.q #D_RECOVER+24, R0
    JSR fs_path
    TST.q R0
    BNE fs_mount_bad
    MOV.q #D_RECOVER+24, R0
    MOV.q #D_PATH, R1
    JSR u_equal
    TST.q R0
    BEQ fs_mount_bad
    CMP.q #3, @D_RECOVER+16
    BNE fs_mount_apply
    MOV.q #D_RECOVER+88, R19
    MOV.q #64, R21
fs_mount_newname:
    TST.b (R19)+
    BEQ fs_mount_newname_ok
    SOB R21, fs_mount_newname
    BRA fs_mount_bad
fs_mount_newname_ok:
    MOV.b @D_RECOVER+24, R0
    CMP.b @D_RECOVER+88, R0
    BNE fs_mount_bad
    MOV.q #D_RECOVER+88, R0
    JSR fs_path
    TST.q R0
    BNE fs_mount_bad
    MOV.q #D_RECOVER+88, R0
    MOV.q #D_PATH, R1
    JSR u_equal
    TST.q R0
    BEQ fs_mount_bad
fs_mount_apply:
    MOV.q #D_RECOVER, R0
    JSR fs_apply
    TST.q R0
    BNE fs_mount_bad
    TST.q @D_CREADY
    BNE hd_mount_applied
    MOV.q @D_RECOVER+8, @D_SEQ
    BRA fs_mount_next
fs_mount_bad:
    TST.q @D_CREADY
    BNE hd_mount_bad
    MOV.q #1, @D_RO
    LEA fs_bad_text, R0
    JSR u_puts
fs_mount_done:
    JSR hd_mount
    CLR.q R0
    POPM #0x3fff
    RET
; Clear journal header, set magic and next sequence. R16.. unchanged.
fs_header:
    MOV.q #192, R0
    MOV.q #D_JOURNAL, R2
    FILL.b #0
    MOV.q #D_MAGIC, @D_JOURNAL
    ADD3.q #1, @D_SEQ, @D_JOURNAL+8
    RET
; Commit D_JOURNAL, then update RAM only after tape reports durable success.
fs_commit:
    CMP.b #67, @D_JOURNAL+24
    BEQ hd_commit
    PUSHM #0x000f
    TST.q @D_RO
    BNE fs_commit_ro
    MOVZ.b.q @D_TAPE+4, R0
    AND.q #128, R0
    BNE fs_commit_ro
    MOV.q @D_JOURNAL+152, R17
    ADD.q #192, R17
    CLR.q @D_JOURNAL+176
    MOV.q #D_JOURNAL, R0
    MOV.q R17, R1
    JSR fs_hash
    MOV.q R0, @D_JOURNAL+176
    MOV.q #D_JOURNAL, @D_TAPE+40
    MOV.q R17, @D_TAPE+48
    MOV.b #5, @D_TAPE+8
    MOVZ.b.q @D_TAPE+4, R0
    AND.q #16, R0
    BNE fs_commit_io
    MOV.q #D_JOURNAL, R0
    JSR fs_apply
    TST.q R0
    BNE fs_commit_return
    MOV.q @D_JOURNAL+8, @D_SEQ
    BRA fs_commit_return
fs_commit_ro:
    MOV.q #-6, R0
    BRA fs_commit_return
fs_commit_io:
    MOV.q #-7, R0
fs_commit_return:
    POPM #0x000f
    RET
; R0=name,R1=data,R2=len,R3=kind,R4=entry. Status R0.
fs_write:
    PUSHM #0x3fff
    MOV.q R1, R16
    MOV.q R2, R17
    MOV.q R3, R18
    MOV.q R4, R19
    CMP.q #D_MAXFILE, R17
    BHI fs_write_size
    CMP.q #3, R18
    BHI fs_write_size
    JSR fs_path
    TST.q R0
    BNE fs_write_return
    TST.b @D_PATH+3
    BEQ fs_write_directory
    JSR fs_parent
    TST.q R0
    BNE fs_write_return
    MOV.q #D_PATH, R0
    JSR fs_find
    TST.q R0
    BEQ fs_write_new
    CMP.q #3, 72(R0)
    BEQ fs_write_directory
    CMP.q #3, R18
    BEQ fs_write_directory
    BRA fs_write_build
fs_write_new:
    TST.q R1
    BEQ fs_write_full
fs_write_build:
    JSR fs_header
    MOV.q #1, @D_JOURNAL+16
    MOV.q #64, R0
    MOV.q #D_PATH, R1
    MOV.q #D_JOURNAL+24, R2
    MOVS.b
    MOV.q R17, @D_JOURNAL+152
    MOV.q R18, @D_JOURNAL+160
    MOV.q R19, @D_JOURNAL+168
    MOV.q R17, R0
    MOV.q R16, R1
    MOV.q #D_JOURNAL+192, R2
    MOVS.b
    JSR fs_commit
    BRA fs_write_return
fs_write_size:
    MOV.q #-2, R0
    BRA fs_write_return
fs_write_directory:
    MOV.q #-8, R0
    BRA fs_write_return
fs_write_full:
    MOV.q #-5, R0
fs_write_return:
    POPM #0x3fff
    RET
; R0=name,R1=buffer,R2=capacity -> R0=len/-error,R1=kind,R2=entry.
fs_read:
    PUSHM #0x000f
    MOV.q R1, R16
    MOV.q R2, R17
    JSR fs_path
    TST.q R0
    BNE fs_read_return
    MOV.q #D_PATH, R0
    JSR fs_find
    TST.q R0
    BEQ fs_read_missing
    MOV.q R0, R18
    CMP.q #3, 72(R18)
    BEQ fs_read_directory
    MOV.q 64(R18), R19
    CMP.q R17, R19
    BHI fs_read_size
    MOV.q R19, R0
    LEA D_FILEDATA(R18), R1
    MOV.q R16, R2
    MOVS.b
    MOV.q R19, R0
    MOV.q 72(R18), R1
    MOV.q 80(R18), R2
    BRA fs_read_return
fs_read_missing:
    MOV.q #-1, R0
    BRA fs_read_return
fs_read_size:
    MOV.q #-2, R0
    BRA fs_read_return
fs_read_directory:
    MOV.q #-8, R0
fs_read_return:
    POPM #0x000f
    RET
; R0=name,R1=0 delete file / 1 remove empty directory.
fs_delete:
    PUSHM #0x3fff
    MOV.q R1, R16
    JSR fs_path
    TST.q R0
    BNE fs_delete_return
    MOV.q #D_PATH, R0
    JSR fs_find
    TST.q R0
    BEQ fs_delete_missing
    CMP.q #3, 72(R0)
    BEQ fs_delete_dir
    TST.q R16
    BNE fs_delete_wrong
    BRA fs_delete_build
fs_delete_dir:
    TST.q R16
    BEQ fs_delete_wrong
    ; Do not remove CWD or a directory with descendants.
    MOV.q #D_PATH, R0
    MOV.q #D_CWD, R1
    JSR u_equal
    TST.q R0
    BNE fs_delete_wrong
    MOV.q #D_PATH, R0
    JSR u_strlen
    MOV.q R0, R17
    MOV.q #D_FILES, R18
    MOV.q #D_SLOTS, R19
fs_delete_child:
    MOV.q #D_PATH, R4
    MOV.q R18, R5
    MOV.q R17, R7
fs_delete_prefix:
    MOV.b (R4)+, R2
    MOV.b (R5)+, R3
    CMP.b R2, R3
    BNE fs_delete_next
    SOB R7, fs_delete_prefix
    CMP.b #92, (R5)
    BEQ fs_delete_wrong
fs_delete_next:
    ADD.q #D_SLOT, R18
    SOB R19, fs_delete_child
fs_delete_build:
    JSR fs_header
    MOV.q #2, @D_JOURNAL+16
    MOV.q #64, R0
    MOV.q #D_PATH, R1
    MOV.q #D_JOURNAL+24, R2
    MOVS.b
    JSR fs_commit
    BRA fs_delete_return
fs_delete_missing:
    MOV.q #-1, R0
    BRA fs_delete_return
fs_delete_wrong:
    MOV.q #-8, R0
fs_delete_return:
    POPM #0x3fff
    RET
; R0=old,R1=new. File rename only; both may include directory paths.
fs_rename:
    PUSHM #0x3fff
    MOV.q R0, R16
    MOV.q R1, R17
    MOV.q R17, R0
    JSR fs_path
    TST.q R0
    BNE fs_rename_return
    JSR fs_parent
    TST.q R0
    BNE fs_rename_return
    MOV.q #D_PATH, R0
    JSR fs_find
    TST.q R0
    BNE fs_rename_exists
    MOV.q #D_PATH, R0
    MOV.q #D_NAME2, R1
    MOV.q #64, R2
    JSR u_copystr
    MOV.q R16, R0
    JSR fs_path
    TST.q R0
    BNE fs_rename_return
    MOV.q #D_PATH, R0
    JSR fs_find
    TST.q R0
    BEQ fs_rename_missing
    CMP.q #3, 72(R0)
    BEQ fs_rename_exists
    MOV.b @D_PATH, R0
    CMP.b @D_NAME2, R0
    BNE fs_rename_exists
    JSR fs_header
    MOV.q #3, @D_JOURNAL+16
    MOV.q #D_PATH, R0
    MOV.q #D_JOURNAL+24, R1
    MOV.q #64, R2
    JSR u_copystr
    MOV.q #D_NAME2, R0
    MOV.q #D_JOURNAL+88, R1
    MOV.q #64, R2
    JSR u_copystr
    JSR fs_commit
    BRA fs_rename_return
fs_rename_missing:
    MOV.q #-1, R0
    BRA fs_rename_return
fs_rename_exists:
    MOV.q #-8, R0
fs_rename_return:
    POPM #0x3fff
    RET
fs_bad_text: .asciz "Tape journal damaged: recovered files mounted read-only.\n"
.include "harddisk.inc"
