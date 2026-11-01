; v0.4 mnemonic metadata; fixed 32-byte records
na_op_table:
.ascii "MOV"
.byte 0
.space 12
.quad 0
.long 0, 2
.ascii "ADD"
.byte 0
.space 12
.quad 0
.long 1, 2
.ascii "SUB"
.byte 0
.space 12
.quad 0
.long 2, 2
.ascii "CMP"
.byte 0
.space 12
.quad 0
.long 3, 2
.ascii "AND"
.byte 0
.space 12
.quad 0
.long 4, 2
.ascii "OR"
.byte 0
.space 13
.quad 0
.long 5, 2
.ascii "XOR"
.byte 0
.space 12
.quad 0
.long 6, 2
.ascii "BIC"
.byte 0
.space 12
.quad 0
.long 7, 2
.ascii "MUL"
.byte 0
.space 12
.quad 0
.long 8, 2
.ascii "DIV"
.byte 0
.space 12
.quad 0
.long 9, 2
.ascii "ADC"
.byte 0
.space 12
.quad 0
.long 10, 2
.ascii "SBB"
.byte 0
.space 12
.quad 0
.long 11, 2
.ascii "SHL"
.byte 0
.space 12
.quad 0
.long 12, 2
.ascii "SHR"
.byte 0
.space 12
.quad 0
.long 13, 2
.ascii "SAR"
.byte 0
.space 12
.quad 0
.long 14, 2
.ascii "ROL"
.byte 0
.space 12
.quad 0
.long 15, 2
.ascii "ROR"
.byte 0
.space 12
.quad 0
.long 16, 2
.ascii "NEG"
.byte 0
.space 12
.quad 0
.long 17, 2
.ascii "NOT"
.byte 0
.space 12
.quad 0
.long 18, 2
.ascii "TST"
.byte 0
.space 12
.quad 0
.long 19, 1
.ascii "LEA"
.byte 0
.space 12
.quad 0
.long 20, 2
.ascii "PUSH"
.byte 0
.space 11
.quad 0
.long 21, 1
.ascii "POP"
.byte 0
.space 12
.quad 0
.long 22, 1
.ascii "XCHG"
.byte 0
.space 11
.quad 0
.long 23, 2
.ascii "INC"
.byte 0
.space 12
.quad 0
.long 24, 1
.ascii "DEC"
.byte 0
.space 12
.quad 0
.long 25, 1
.ascii "CLR"
.byte 0
.space 12
.quad 0
.long 26, 1
.ascii "ABS"
.byte 0
.space 12
.quad 0
.long 27, 2
.ascii "MIN"
.byte 0
.space 12
.quad 0
.long 28, 2
.ascii "MAX"
.byte 0
.space 12
.quad 0
.long 29, 2
.ascii "ADD3"
.byte 0
.space 11
.quad 1
.long 0, 3
.ascii "SUB3"
.byte 0
.space 11
.quad 1
.long 1, 3
.ascii "MUL3"
.byte 0
.space 11
.quad 1
.long 2, 3
.ascii "DIV3"
.byte 0
.space 11
.quad 1
.long 3, 3
.ascii "AND3"
.byte 0
.space 11
.quad 1
.long 4, 3
.ascii "OR3"
.byte 0
.space 12
.quad 1
.long 5, 3
.ascii "XOR3"
.byte 0
.space 11
.quad 1
.long 6, 3
.ascii "BIC3"
.byte 0
.space 11
.quad 1
.long 7, 3
.ascii "SHL3"
.byte 0
.space 11
.quad 1
.long 8, 3
.ascii "SHR3"
.byte 0
.space 11
.quad 1
.long 9, 3
.ascii "SAR3"
.byte 0
.space 11
.quad 1
.long 10, 3
.ascii "ROL3"
.byte 0
.space 11
.quad 1
.long 11, 3
.ascii "ROR3"
.byte 0
.space 11
.quad 1
.long 12, 3
.ascii "ADC3"
.byte 0
.space 11
.quad 1
.long 13, 3
.ascii "SBB3"
.byte 0
.space 11
.quad 1
.long 14, 3
.ascii "BEQ"
.byte 0
.space 12
.quad 2
.long 0, 0
.ascii "BNE"
.byte 0
.space 12
.quad 2
.long 1, 0
.ascii "BLT"
.byte 0
.space 12
.quad 2
.long 2, 0
.ascii "BLE"
.byte 0
.space 12
.quad 2
.long 3, 0
.ascii "BGT"
.byte 0
.space 12
.quad 2
.long 4, 0
.ascii "BGE"
.byte 0
.space 12
.quad 2
.long 5, 0
.ascii "BLO"
.byte 0
.space 12
.quad 2
.long 6, 0
.ascii "BLS"
.byte 0
.space 12
.quad 2
.long 7, 0
.ascii "BHI"
.byte 0
.space 12
.quad 2
.long 8, 0
.ascii "BHS"
.byte 0
.space 12
.quad 2
.long 9, 0
.ascii "BMI"
.byte 0
.space 12
.quad 2
.long 10, 0
.ascii "BPL"
.byte 0
.space 12
.quad 2
.long 11, 0
.ascii "BVS"
.byte 0
.space 12
.quad 2
.long 12, 0
.ascii "BVC"
.byte 0
.space 12
.quad 2
.long 13, 0
.ascii "BRA"
.byte 0
.space 12
.quad 2
.long 14, 0
.ascii "BSR"
.byte 0
.space 12
.quad 2
.long 15, 0
.ascii "NOP"
.byte 0
.space 12
.quad 3
.long 0, 0
.ascii "RET"
.byte 0
.space 12
.quad 3
.long 1, 0
.ascii "EXIT"
.byte 0
.space 11
.quad 3
.long 2, 0
.ascii "RTE"
.byte 0
.space 12
.quad 3
.long 3, 0
.ascii "IFENCE"
.byte 0
.space 9
.quad 3
.long 4, 0
.ascii "FENCE"
.byte 0
.space 10
.quad 3
.long 5, 0
.ascii "BREAK"
.byte 0
.space 10
.quad 3
.long 6, 0
.ascii "WAIT"
.byte 0
.space 11
.quad 3
.long 7, 0
.ascii "JMP"
.byte 0
.space 12
.quad 3
.long 8, 1
.ascii "JSR"
.byte 0
.space 12
.quad 3
.long 9, 1
.ascii "ENTER"
.byte 0
.space 10
.quad 3
.long 10, 2
.ascii "PUSHM"
.byte 0
.space 10
.quad 3
.long 11, 1
.ascii "POPM"
.byte 0
.space 11
.quad 3
.long 12, 1
.ascii "SYSCALL"
.byte 0
.space 8
.quad 3
.long 13, 1
.ascii "FMOV"
.byte 0
.space 11
.quad 4
.long 0, 2
.ascii "FADD"
.byte 0
.space 11
.quad 4
.long 1, 2
.ascii "FSUB"
.byte 0
.space 11
.quad 4
.long 2, 2
.ascii "FMUL"
.byte 0
.space 11
.quad 4
.long 3, 2
.ascii "FDIV"
.byte 0
.space 11
.quad 4
.long 4, 2
.ascii "FCMP"
.byte 0
.space 11
.quad 4
.long 5, 2
.ascii "FCMPE"
.byte 0
.space 10
.quad 4
.long 6, 2
.ascii "FTST"
.byte 0
.space 11
.quad 4
.long 7, 1
.ascii "FNEG"
.byte 0
.space 11
.quad 4
.long 8, 2
.ascii "FABS"
.byte 0
.space 11
.quad 4
.long 9, 2
.ascii "FSQRT"
.byte 0
.space 10
.quad 4
.long 10, 2
.ascii "FMIN"
.byte 0
.space 11
.quad 4
.long 11, 2
.ascii "FMAX"
.byte 0
.space 11
.quad 4
.long 12, 2
.ascii "FCOPYSIGN"
.byte 0
.space 6
.quad 4
.long 13, 2
.ascii "FREM"
.byte 0
.space 11
.quad 4
.long 14, 2
.ascii "FCLASS"
.byte 0
.space 9
.quad 4
.long 15, 2
.ascii "FRINT"
.byte 0
.space 10
.quad 4
.long 16, 2
.ascii "FFLOOR"
.byte 0
.space 9
.quad 4
.long 17, 2
.ascii "FCEIL"
.byte 0
.space 10
.quad 4
.long 18, 2
.ascii "FTRUNC"
.byte 0
.space 9
.quad 4
.long 19, 2
.ascii "FADD3"
.byte 0
.space 10
.quad 4
.long 20, 3
.ascii "FSUB3"
.byte 0
.space 10
.quad 4
.long 21, 3
.ascii "FMUL3"
.byte 0
.space 10
.quad 4
.long 22, 3
.ascii "FDIV3"
.byte 0
.space 10
.quad 4
.long 23, 3
.ascii "FMA"
.byte 0
.space 12
.quad 4
.long 24, 4
.ascii "FMS"
.byte 0
.space 12
.quad 4
.long 25, 4
.ascii "FNMA"
.byte 0
.space 11
.quad 4
.long 26, 4
.ascii "FNMS"
.byte 0
.space 11
.quad 4
.long 27, 4
.ascii "MOVS"
.byte 0
.space 11
.quad 5
.long 0, 0
.ascii "FILL"
.byte 0
.space 11
.quad 5
.long 1, 1
.ascii "CMPS"
.byte 0
.space 11
.quad 5
.long 2, 0
.ascii "SCAN"
.byte 0
.space 11
.quad 5
.long 3, 1
.ascii "SCANN"
.byte 0
.space 10
.quad 5
.long 4, 1
.ascii "CAS"
.byte 0
.space 12
.quad 8
.long 0, 3
.ascii "XADD"
.byte 0
.space 11
.quad 8
.long 1, 2
.ascii "SWAP"
.byte 0
.space 11
.quad 8
.long 2, 2
.ascii "ATAND"
.byte 0
.space 10
.quad 8
.long 3, 2
.ascii "ATOR"
.byte 0
.space 11
.quad 8
.long 4, 2
.ascii "ATXOR"
.byte 0
.space 10
.quad 8
.long 5, 2
.ascii "CLZ"
.byte 0
.space 12
.quad 10
.long 0, 2
.ascii "CTZ"
.byte 0
.space 12
.quad 10
.long 1, 2
.ascii "POPCNT"
.byte 0
.space 9
.quad 10
.long 2, 2
.ascii "BSWAP"
.byte 0
.space 10
.quad 10
.long 3, 2
.ascii "BREV"
.byte 0
.space 11
.quad 10
.long 4, 2
.ascii "MULW"
.byte 0
.space 11
.quad 10
.long 8, 4
.ascii "UMULW"
.byte 0
.space 10
.quad 10
.long 9, 4
.ascii "DIVW"
.byte 0
.space 11
.quad 10
.long 10, 2
.ascii "UDIVW"
.byte 0
.space 10
.quad 10
.long 11, 2
.ascii "MFFS"
.byte 0
.space 11
.quad 14
.long 112, 1
.ascii "MTFS"
.byte 0
.space 11
.quad 14
.long 116, 1
.ascii "EXTV"
.byte 0
.space 11
.quad 6
.long 0, 4
.ascii "EXTZV"
.byte 0
.space 10
.quad 6
.long 1, 4
.ascii "INSV"
.byte 0
.space 11
.quad 6
.long 2, 4
.ascii "CMPV"
.byte 0
.space 11
.quad 6
.long 3, 4
.ascii "MFSR"
.byte 0
.space 11
.quad 11
.long 0, 2
.ascii "MTSR"
.byte 0
.space 11
.quad 11
.long 1, 2
.ascii "TLBI"
.byte 0
.space 11
.quad 11
.long 2, 2
.ascii "DCLEAN"
.byte 0
.space 9
.quad 11
.long 3, 2
.ascii "DINVAL"
.byte 0
.space 9
.quad 11
.long 4, 2
.ascii "DFLUSH"
.byte 0
.space 9
.quad 11
.long 5, 2
.ascii "HALT"
.byte 0
.space 11
.quad 11
.long 6, 0
.ascii "CASE"
.byte 0
.space 11
.quad 13
.long 12, 3
.ascii "MOVZ"
.byte 0
.space 11
.quad 9
.long 0, 2
.ascii "MOVSX"
.byte 0
.space 10
.quad 9
.long 1, 2
.ascii "CVTIF"
.byte 0
.space 10
.quad 9
.long 2, 2
.ascii "CVTUF"
.byte 0
.space 10
.quad 9
.long 3, 2
.ascii "CVTFI"
.byte 0
.space 10
.quad 9
.long 4, 2
.ascii "CVTFU"
.byte 0
.space 10
.quad 9
.long 5, 2
.ascii "CVTFIR"
.byte 0
.space 9
.quad 9
.long 6, 2
.ascii "CVTFF"
.byte 0
.space 10
.quad 9
.long 7, 2
.ascii "MOVIF"
.byte 0
.space 10
.quad 9
.long 8, 2
.ascii "MOVFI"
.byte 0
.space 10
.quad 9
.long 9, 2
.ascii "BBS"
.byte 0
.space 12
.quad 7
.long 0, 2
.ascii "BBC"
.byte 0
.space 12
.quad 7
.long 1, 2
.ascii "BBSS"
.byte 0
.space 11
.quad 7
.long 2, 2
.ascii "BBCC"
.byte 0
.space 11
.quad 7
.long 3, 2
.ascii "BBSSI"
.byte 0
.space 10
.quad 7
.long 4, 2
.ascii "BBCCI"
.byte 0
.space 10
.quad 7
.long 5, 2
.ascii "SOB"
.byte 0
.space 12
.quad 12
.long 0, 1
.ascii "SOBGE"
.byte 0
.space 10
.quad 12
.long 1, 1
.ascii "SOBGT"
.byte 0
.space 10
.quad 12
.long 2, 1
.ascii "AOBLT"
.byte 0
.space 10
.quad 12
.long 3, 2
.ascii "AOBLE"
.byte 0
.space 10
.quad 12
.long 4, 2
.ascii "ACB"
.byte 0
.space 12
.quad 12
.long 5, 3
.ascii "ITOF"
.byte 0
.space 11
.quad 9
.long 2, 2
.ascii "FTOI"
.byte 0
.space 11
.quad 9
.long 6, 2
.asciz "MFTP"
.space 11
.quad 3
.long 14, 1
.asciz "CAS2"
.space 11
.quad 8
.long 6, 3
.asciz "IOFENCE"
.space 8
.quad 11
.long 7, 0
.asciz "UAON"
.space 11
.quad 11
.long 8, 0
.asciz "UAOFF"
.space 10
.quad 11
.long 9, 0
.asciz "SETEQ"
.space 10
.quad 15
.long 0, 1
.asciz "SETNE"
.space 10
.quad 15
.long 1, 1
.asciz "SETLT"
.space 10
.quad 15
.long 2, 1
.asciz "SETLE"
.space 10
.quad 15
.long 3, 1
.asciz "SETGT"
.space 10
.quad 15
.long 4, 1
.asciz "SETGE"
.space 10
.quad 15
.long 5, 1
.asciz "SETLO"
.space 10
.quad 15
.long 6, 1
.asciz "SETLS"
.space 10
.quad 15
.long 7, 1
.asciz "SETHI"
.space 10
.quad 15
.long 8, 1
.asciz "SETHS"
.space 10
.quad 15
.long 9, 1
.asciz "SETMI"
.space 10
.quad 15
.long 10, 1
.asciz "SETPL"
.space 10
.quad 15
.long 11, 1
.asciz "SETVS"
.space 10
.quad 15
.long 12, 1
.asciz "SETVC"
.space 10
.quad 15
.long 13, 1
.asciz "SETAL"
.space 10
.quad 15
.long 14, 1
.asciz "CMOVEQ"
.space 9
.quad 16
.long 0, 2
.asciz "CMOVNE"
.space 9
.quad 16
.long 1, 2
.asciz "CMOVLT"
.space 9
.quad 16
.long 2, 2
.asciz "CMOVLE"
.space 9
.quad 16
.long 3, 2
.asciz "CMOVGT"
.space 9
.quad 16
.long 4, 2
.asciz "CMOVGE"
.space 9
.quad 16
.long 5, 2
.asciz "CMOVLO"
.space 9
.quad 16
.long 6, 2
.asciz "CMOVLS"
.space 9
.quad 16
.long 7, 2
.asciz "CMOVHI"
.space 9
.quad 16
.long 8, 2
.asciz "CMOVHS"
.space 9
.quad 16
.long 9, 2
.asciz "CMOVMI"
.space 9
.quad 16
.long 10, 2
.asciz "CMOVPL"
.space 9
.quad 16
.long 11, 2
.asciz "CMOVVS"
.space 9
.quad 16
.long 12, 2
.asciz "CMOVVC"
.space 9
.quad 16
.long 13, 2
.asciz "CMOVAL"
.space 9
.quad 16
.long 14, 2
.asciz "PAUSE"
.space 10
.quad 17
.long 128, 0
.asciz "PREFETCH"
.space 7
.quad 17
.long 129, 1
.asciz "PREFETCHW"
.space 6
.quad 17
.long 130, 1
.space 32
na_system_table:
.asciz "ID"
.space 13
.quad 0
.asciz "CYCLES"
.space 9
.quad 1
.asciz "PERFCTL"
.space 8
.quad 2
.asciz "PERFCNT"
.space 8
.quad 3
.asciz "SIMASK"
.space 9
.quad 4
.asciz "CPUID"
.space 10
.quad 5
.asciz "TIMEFREQ"
.space 7
.quad 6
.asciz "TP"
.space 13
.quad 7
.asciz "ESP"
.space 12
.quad 8
.asciz "VBR"
.space 12
.quad 16
.asciz "PTBR0"
.space 10
.quad 17
.asciz "PTBR1"
.space 10
.quad 18
.asciz "ASID"
.space 11
.quad 19
.asciz "MMUCTL"
.space 9
.quad 20
.asciz "CAUSE"
.space 10
.quad 21
.asciz "FAULTADDR"
.space 6
.quad 22
.asciz "FAULTINFO"
.space 6
.quad 23
.asciz "SPK"
.space 12
.quad 24
.asciz "SPS"
.space 12
.quad 25
.asciz "SPU"
.space 12
.quad 26
.asciz "PSR"
.space 12
.quad 27
.asciz "TIMECMP"
.space 8
.quad 28
.asciz "MCSTATUS"
.space 7
.quad 29
.asciz "MCADDR"
.space 9
.quad 30
.asciz "MCCTL"
.space 10
.quad 31
.asciz "SCRATCH0"
.space 7
.quad 32
.asciz "SCRATCH1"
.space 7
.quad 33
.asciz "SCRATCH2"
.space 7
.quad 34
.asciz "SCRATCH3"
.space 7
.quad 35
.asciz "MPUCTL"
.space 9
.quad 40
.asciz "PRBASE0"
.space 8
.quad 64
.asciz "PRBASE1"
.space 8
.quad 65
.asciz "PRBASE2"
.space 8
.quad 66
.asciz "PRBASE3"
.space 8
.quad 67
.asciz "PRBASE4"
.space 8
.quad 68
.asciz "PRBASE5"
.space 8
.quad 69
.asciz "PRBASE6"
.space 8
.quad 70
.asciz "PRBASE7"
.space 8
.quad 71
.asciz "PRBASE8"
.space 8
.quad 72
.asciz "PRBASE9"
.space 8
.quad 73
.asciz "PRBASE10"
.space 7
.quad 74
.asciz "PRBASE11"
.space 7
.quad 75
.asciz "PRBASE12"
.space 7
.quad 76
.asciz "PRBASE13"
.space 7
.quad 77
.asciz "PRBASE14"
.space 7
.quad 78
.asciz "PRBASE15"
.space 7
.quad 79
.asciz "PRLIM0"
.space 9
.quad 80
.asciz "PRLIM1"
.space 9
.quad 81
.asciz "PRLIM2"
.space 9
.quad 82
.asciz "PRLIM3"
.space 9
.quad 83
.asciz "PRLIM4"
.space 9
.quad 84
.asciz "PRLIM5"
.space 9
.quad 85
.asciz "PRLIM6"
.space 9
.quad 86
.asciz "PRLIM7"
.space 9
.quad 87
.asciz "PRLIM8"
.space 9
.quad 88
.asciz "PRLIM9"
.space 9
.quad 89
.asciz "PRLIM10"
.space 8
.quad 90
.asciz "PRLIM11"
.space 8
.quad 91
.asciz "PRLIM12"
.space 8
.quad 92
.asciz "PRLIM13"
.space 8
.quad 93
.asciz "PRLIM14"
.space 8
.quad 94
.asciz "PRLIM15"
.space 8
.quad 95
.asciz "PRATTR0"
.space 8
.quad 96
.asciz "PRATTR1"
.space 8
.quad 97
.asciz "PRATTR2"
.space 8
.quad 98
.asciz "PRATTR3"
.space 8
.quad 99
.asciz "PRATTR4"
.space 8
.quad 100
.asciz "PRATTR5"
.space 8
.quad 101
.asciz "PRATTR6"
.space 8
.quad 102
.asciz "PRATTR7"
.space 8
.quad 103
.asciz "PRATTR8"
.space 8
.quad 104
.asciz "PRATTR9"
.space 8
.quad 105
.asciz "PRATTR10"
.space 7
.quad 106
.asciz "PRATTR11"
.space 7
.quad 107
.asciz "PRATTR12"
.space 7
.quad 108
.asciz "PRATTR13"
.space 7
.quad 109
.asciz "PRATTR14"
.space 7
.quad 110
.asciz "PRATTR15"
.space 7
.quad 111
.asciz "WPADDR0"
.space 8
.quad 112
.asciz "WPADDR1"
.space 8
.quad 113
.asciz "WPADDR2"
.space 8
.quad 114
.asciz "WPADDR3"
.space 8
.quad 115
.asciz "WPCTL0"
.space 9
.quad 116
.asciz "WPCTL1"
.space 9
.quad 117
.asciz "WPCTL2"
.space 9
.quad 118
.asciz "WPCTL3"
.space 9
.quad 119
.space 24
na_directive_table:
.asciz "BSS"
.space 12
.quad 0
.asciz "GLOBL"
.space 10
.quad 22
.asciz "TEXT"
.space 11
.quad 0
.asciz "DATA"
.space 11
.quad 0
.asciz "EQU"
.space 12
.quad 1
.asciz "ENTRY"
.space 10
.quad 2
.asciz "BYTE"
.space 11
.quad 3
.asciz "WORD"
.space 11
.quad 4
.asciz "LONG"
.space 11
.quad 5
.asciz "QUAD"
.space 11
.quad 6
.asciz "SPACE"
.space 10
.quad 10
.asciz "ALIGN"
.space 10
.quad 11
.asciz "ORG"
.space 12
.quad 12
.asciz "ASCII"
.space 10
.quad 20
.asciz "ASCIZ"
.space 10
.quad 21
.space 24
na_fp_table:
.quad 0x0, 0x3ff0000000000000, 0x4000000000000000, 0x3fe0000000000000, 0x4008000000000000, 0x4010000000000000, 0x4014000000000000, 0x4020000000000000, 0x4024000000000000, 0x4030000000000000, 0x4059000000000000, 0x3fd0000000000000, 0x3fb999999999999a, 0xbff0000000000000, 0xc000000000000000, 0xbfe0000000000000, 0x400921fb54442d18, 0x3ff921fb54442d18, 0x4005bf0a8b145769, 0x3fe62e42fefa39ef, 0x3ff6a09e667f3bcd, 0x7ff0000000000000, 0x7ff8000000000000, 0x8000000000000000
