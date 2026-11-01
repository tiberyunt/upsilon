.include "layout.inc"
.entry dos_boot
; The firmware loads this first tape file. IO.SYS is a real journal file.
dos_boot:
    MOV.q R0, @D_BOOTDRIVE
    MOV.q R1, @D_BOOTUNIT
    MOV.q @0xffff0200, R0
    CMP.q #0x1000000, R0
    BLO dos_boot_memory
    JSR fs_mount
    MOV.b @D_CWD, dos_boot_io
    LEA dos_boot_io, R0
    MOV.q #0x100000, R1
    MOV.q #65536, R2
    JSR fs_read
    CMP.q #0, R0
    BLE dos_boot_missing
    CMP.q #1, R1
    BNE dos_boot_missing
    CMP.q #0x100000, R2
    BNE dos_boot_missing
    MOV.q @D_BOOTDRIVE, R0
    MOV.q @D_BOOTUNIT, R1
    IFENCE
    JMP @0x100000
dos_boot_memory:
    LEA dos_boot_memory_text, R0
    JSR u_puts
    HALT
dos_boot_missing:
    LEA dos_boot_missing_text, R0
    JSR u_puts
    HALT
dos_boot_io: .asciz "A:\\IO.SYS"
dos_boot_memory_text: .asciz "KotlTOS needs at least 16 MiB RAM.\n"
dos_boot_missing_text: .asciz "Non-system volume: missing or invalid IO.SYS.\n"
.include "console.inc"
.include "filesystem.inc"
