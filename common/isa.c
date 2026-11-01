#include "isa.h"
#include <string.h>
#include <math.h>
const Op ops[]= {
    {
        "MOV", F_TWO, 0, 2
    }, {
        "ADD", F_TWO, 1, 2
    }, {
        "SUB", F_TWO, 2, 2
    }, {
        "CMP", F_TWO, 3, 2
    }, {
        "AND", F_TWO, 4, 2
    }, {
        "OR", F_TWO, 5, 2
    }, {
        "XOR", F_TWO, 6, 2
    }, {
        "BIC", F_TWO, 7, 2
    }, {
        "MUL", F_TWO, 8, 2
    }, {
        "DIV", F_TWO, 9, 2
    }, {
        "ADC", F_TWO, 10, 2
    }, {
        "SBB", F_TWO, 11, 2
    }, {
        "SHL", F_TWO, 12, 2
    }, {
        "SHR", F_TWO, 13, 2
    }, {
        "SAR", F_TWO, 14, 2
    }, {
        "ROL", F_TWO, 15, 2
    }, {
        "ROR", F_TWO, 16, 2
    }, {
        "NEG", F_TWO, 17, 2
    }, {
        "NOT", F_TWO, 18, 2
    }, {
        "TST", F_TWO, 19, 1
    }, {
        "LEA", F_TWO, 20, 2
    }, {
        "PUSH", F_TWO, 21, 1
    }, {
        "POP", F_TWO, 22, 1
    }, {
        "XCHG", F_TWO, 23, 2
    }, {
        "INC", F_TWO, 24, 1
    }, {
        "DEC", F_TWO, 25, 1
    }, {
        "CLR", F_TWO, 26, 1
    }, {
        "ABS", F_TWO, 27, 2
    }, {
        "MIN", F_TWO, 28, 2
    }, {
        "MAX", F_TWO, 29, 2
    }, {
        "ADD3", F_THREE, 0, 3
    }, {
        "SUB3", F_THREE, 1, 3
    }, {
        "MUL3", F_THREE, 2, 3
    }, {
        "DIV3", F_THREE, 3, 3
    }, {
        "AND3", F_THREE, 4, 3
    }, {
        "OR3", F_THREE, 5, 3
    }, {
        "XOR3", F_THREE, 6, 3
    }, {
        "BIC3", F_THREE, 7, 3
    }, {
        "SHL3", F_THREE, 8, 3
    }, {
        "SHR3", F_THREE, 9, 3
    }, {
        "SAR3", F_THREE, 10, 3
    }, {
        "ROL3", F_THREE, 11, 3
    }, {
        "ROR3", F_THREE, 12, 3
    }, {
        "ADC3", F_THREE, 13, 3
    }, {
        "SBB3", F_THREE, 14, 3
    }, {
        "BEQ", F_BRANCH, 0, 0
    }, {
        "BNE", F_BRANCH, 1, 0
    }, {
        "BLT", F_BRANCH, 2, 0
    }, {
        "BLE", F_BRANCH, 3, 0
    }, {
        "BGT", F_BRANCH, 4, 0
    }, {
        "BGE", F_BRANCH, 5, 0
    }, {
        "BLO", F_BRANCH, 6, 0
    }, {
        "BLS", F_BRANCH, 7, 0
    }, {
        "BHI", F_BRANCH, 8, 0
    }, {
        "BHS", F_BRANCH, 9, 0
    }, {
        "BMI", F_BRANCH, 10, 0
    }, {
        "BPL", F_BRANCH, 11, 0
    }, {
        "BVS", F_BRANCH, 12, 0
    }, {
        "BVC", F_BRANCH, 13, 0
    }, {
        "BRA", F_BRANCH, 14, 0
    }, {
        "BSR", F_BRANCH, 15, 0
    }, {
        "NOP", F_SHORT, 0, 0
    }, {
        "RET", F_SHORT, 1, 0
    }, {
        "EXIT", F_SHORT, 2, 0
    }, {
        "RTE", F_SHORT, 3, 0
    }, {
        "IFENCE", F_SHORT, 4, 0
    }, {
        "FENCE", F_SHORT, 5, 0
    }, {
        "BREAK", F_SHORT, 6, 0
    }, {
        "WAIT", F_SHORT, 7, 0
    }, {
        "JMP", F_SHORT, 8, 1
    }, {
        "JSR", F_SHORT, 9, 1
    }, {
        "ENTER", F_SHORT, 10, 2
    }, {
        "PUSHM", F_SHORT, 11, 1
    }, {
        "POPM", F_SHORT, 12, 1
    }, {
        "SYSCALL", F_SHORT, 13, 1
    }, {
        "FMOV", F_FP, 0, 2
    }, {
        "FADD", F_FP, 1, 2
    }, {
        "FSUB", F_FP, 2, 2
    }, {
        "FMUL", F_FP, 3, 2
    }, {
        "FDIV", F_FP, 4, 2
    }, {
        "FCMP", F_FP, 5, 2
    }, {
        "FCMPE", F_FP, 6, 2
    }, {
        "FTST", F_FP, 7, 1
    }, {
        "FNEG", F_FP, 8, 2
    }, {
        "FABS", F_FP, 9, 2
    }, {
        "FSQRT", F_FP, 10, 2
    }, {
        "FMIN", F_FP, 11, 2
    }, {
        "FMAX", F_FP, 12, 2
    }, {
        "FCOPYSIGN", F_FP, 13, 2
    }, {
        "FREM", F_FP, 14, 2
    }, {
        "FCLASS", F_FP, 15, 2
    }, {
        "FRINT", F_FP, 16, 2
    }, {
        "FFLOOR", F_FP, 17, 2
    }, {
        "FCEIL", F_FP, 18, 2
    }, {
        "FTRUNC", F_FP, 19, 2
    }, {
        "FADD3", F_FP, 20, 3
    }, {
        "FSUB3", F_FP, 21, 3
    }, {
        "FMUL3", F_FP, 22, 3
    }, {
        "FDIV3", F_FP, 23, 3
    }, {
        "FMA", F_FP, 24, 4
    }, {
        "FMS", F_FP, 25, 4
    }, {
        "FNMA", F_FP, 26, 4
    }, {
        "FNMS", F_FP, 27, 4
    }, {
        "MOVS", F_BLOCK, 0, 0
    }, {
        "FILL", F_BLOCK, 1, 1
    }, {
        "CMPS", F_BLOCK, 2, 0
    }, {
        "SCAN", F_BLOCK, 3, 1
    }, {
        "SCANN", F_BLOCK, 4, 1
    }, {
        "CAS", F_ATOMIC, 0, 3
    }, {
        "XADD", F_ATOMIC, 1, 2
    }, {
        "SWAP", F_ATOMIC, 2, 2
    }, {
        "ATAND", F_ATOMIC, 3, 2
    }, {
        "ATOR", F_ATOMIC, 4, 2
    }, {
        "ATXOR", F_ATOMIC, 5, 2
    }, {
        "CLZ", F_BITS, 0, 2
    }, {
        "CTZ", F_BITS, 1, 2
    }, {
        "POPCNT", F_BITS, 2, 2
    }, {
        "BSWAP", F_BITS, 3, 2
    }, {
        "BREV", F_BITS, 4, 2
    }, {
        "MULW", F_BITS, 8, 4
    }, {
        "UMULW", F_BITS, 9, 4
    }, {
        "DIVW", F_BITS, 10, 2
    }, {
        "UDIVW", F_BITS, 11, 2
    }, {
        "MFFS", F_FPSR, 112, 1
    }, {
        "MTFS", F_FPSR, 116, 1
    }, {
        "EXTV", F_FIELD, 0, 4
    }, {
        "EXTZV", F_FIELD, 1, 4
    }, {
        "INSV", F_FIELD, 2, 4
    }, {
        "CMPV", F_FIELD, 3, 4
    }, {
        "MFSR", F_SYSTEM, 0, 2
    }, {
        "MTSR", F_SYSTEM, 1, 2
    }, {
        "TLBI", F_SYSTEM, 2, 2
    }, {
        "DCLEAN", F_SYSTEM, 3, 2
    }, {
        "DINVAL", F_SYSTEM, 4, 2
    }, {
        "DFLUSH", F_SYSTEM, 5, 2
    }, {
        "HALT", F_SYSTEM, 6, 0
    }, {
        "CASE", F_CASE, 12, 3
    }, {
        "MOVZ", F_CONV, 0, 2
    }, {
        "MOVSX", F_CONV, 1, 2
    }, {
        "CVTIF", F_CONV, 2, 2
    }, {
        "CVTUF", F_CONV, 3, 2
    }, {
        "CVTFI", F_CONV, 4, 2
    }, {
        "CVTFU", F_CONV, 5, 2
    }, {
        "CVTFIR", F_CONV, 6, 2
    }, {
        "CVTFF", F_CONV, 7, 2
    }, {
        "MOVIF", F_CONV, 8, 2
    }, {
        "MOVFI", F_CONV, 9, 2
    }, {
        "BBS", F_BITBR, 0, 2
    }, {
        "BBC", F_BITBR, 1, 2
    }, {
        "BBSS", F_BITBR, 2, 2
    }, {
        "BBCC", F_BITBR, 3, 2
    }, {
        "BBSSI", F_BITBR, 4, 2
    }, {
        "BBCCI", F_BITBR, 5, 2
    }, {
        "SOB", F_LOOP, 0, 1
    }, {
        "SOBGE", F_LOOP, 1, 1
    }, {
        "SOBGT", F_LOOP, 2, 1
    }, {
        "AOBLT", F_LOOP, 3, 2
    }, {
        "AOBLE", F_LOOP, 4, 2
    }, {
        "ACB", F_LOOP, 5, 3
    },
    {"MFTP", F_SHORT, 14, 1},
    {"CAS2", F_ATOMIC, 6, 3},
    {"IOFENCE", F_SYSTEM, 7, 0},
    {"UAON", F_SYSTEM, 8, 0},
    {"UAOFF", F_SYSTEM, 9, 0},
    {"SETEQ", F_SET, 0, 1},
    {"SETNE", F_SET, 1, 1},
    {"SETLT", F_SET, 2, 1},
    {"SETLE", F_SET, 3, 1},
    {"SETGT", F_SET, 4, 1},
    {"SETGE", F_SET, 5, 1},
    {"SETLO", F_SET, 6, 1},
    {"SETLS", F_SET, 7, 1},
    {"SETHI", F_SET, 8, 1},
    {"SETHS", F_SET, 9, 1},
    {"SETMI", F_SET, 10, 1},
    {"SETPL", F_SET, 11, 1},
    {"SETVS", F_SET, 12, 1},
    {"SETVC", F_SET, 13, 1},
    {"SETAL", F_SET, 14, 1},
    {"CMOVEQ", F_CMOV, 0, 2},
    {"CMOVNE", F_CMOV, 1, 2},
    {"CMOVLT", F_CMOV, 2, 2},
    {"CMOVLE", F_CMOV, 3, 2},
    {"CMOVGT", F_CMOV, 4, 2},
    {"CMOVGE", F_CMOV, 5, 2},
    {"CMOVLO", F_CMOV, 6, 2},
    {"CMOVLS", F_CMOV, 7, 2},
    {"CMOVHI", F_CMOV, 8, 2},
    {"CMOVHS", F_CMOV, 9, 2},
    {"CMOVMI", F_CMOV, 10, 2},
    {"CMOVPL", F_CMOV, 11, 2},
    {"CMOVVS", F_CMOV, 12, 2},
    {"CMOVVC", F_CMOV, 13, 2},
    {"CMOVAL", F_CMOV, 14, 2},
    {"PAUSE", F_HINT, 128, 0},
    {"PREFETCH", F_HINT, 129, 1},
    {"PREFETCHW", F_HINT, 130, 1},
};
const size_t nops=sizeof(ops)/sizeof(ops[0]);
const double fp_constants[24]= {
    0x0.0p+0, 0x1.0000000000000p+0, 0x1.0000000000000p+1, 0x1.0000000000000p-1, 0x1.8000000000000p+1, 0x1.0000000000000p+2, 0x1.4000000000000p+2, 0x1.0000000000000p+3, 0x1.4000000000000p+3, 0x1.0000000000000p+4, 0x1.9000000000000p+6, 0x1.0000000000000p-2, 0x1.999999999999ap-4, -0x1.0000000000000p+0, -0x1.0000000000000p+1, -0x1.0000000000000p-1, 0x1.921fb54442d18p+1, 0x1.921fb54442d18p+0, 0x1.5bf0a8b145769p+1, 0x1.62e42fefa39efp-1, 0x1.6a09e667f3bcdp+0, INFINITY, NAN, -0x0.0p+0
};
const Op *find_op(const char *name) {
    if(!strcmp(name, "ITOF"))name="CVTIF";
    if(!strcmp(name, "FTOI"))name="CVTFIR";
    for(size_t i=0;i<nops;i++)if(!strcmp(name, ops[i].name))return &ops[i];
    return NULL;
}
int fp_operand(const Op *o, int i) {
    if(o->family==F_FP)return !(o->op==15&&i==1);
    if(o->family!=F_CONV)return 0;
    int n=o->op;
    return n==7||(n==2||n==3||n==8?i==1:n==4||n==5||n==6||n==9?i==0:0);
}
int operand_size(const Op *o, int s, int a, int i) {
    return o->family==F_CONV?(i?a:s):o->family==F_FP&&o->op==15&&i==1?8:s;
}
static const char *system_names[120]= {
    [0]="ID",
    [1]="CYCLES",
    [2]="PERFCTL",
    [3]="PERFCNT",
    [4]="SIMASK",
    [5]="CPUID",
    [6]="TIMEFREQ",
    [7]="TP",
    [8]="ESP",
    [16]="VBR",
    [17]="PTBR0",
    [18]="PTBR1",
    [19]="ASID",
    [20]="MMUCTL",
    [21]="CAUSE",
    [22]="FAULTADDR",
    [23]="FAULTINFO",
    [24]="SPK",
    [25]="SPS",
    [26]="SPU",
    [27]="PSR",
    [28]="TIMECMP",
    [29]="MCSTATUS",
    [30]="MCADDR",
    [31]="MCCTL",
    [32]="SCRATCH0",
    [33]="SCRATCH1",
    [34]="SCRATCH2",
    [35]="SCRATCH3",
    [40]="MPUCTL",
    [64]="PRBASE0",
    [65]="PRBASE1",
    [66]="PRBASE2",
    [67]="PRBASE3",
    [68]="PRBASE4",
    [69]="PRBASE5",
    [70]="PRBASE6",
    [71]="PRBASE7",
    [72]="PRBASE8",
    [73]="PRBASE9",
    [74]="PRBASE10",
    [75]="PRBASE11",
    [76]="PRBASE12",
    [77]="PRBASE13",
    [78]="PRBASE14",
    [79]="PRBASE15",
    [80]="PRLIM0",
    [81]="PRLIM1",
    [82]="PRLIM2",
    [83]="PRLIM3",
    [84]="PRLIM4",
    [85]="PRLIM5",
    [86]="PRLIM6",
    [87]="PRLIM7",
    [88]="PRLIM8",
    [89]="PRLIM9",
    [90]="PRLIM10",
    [91]="PRLIM11",
    [92]="PRLIM12",
    [93]="PRLIM13",
    [94]="PRLIM14",
    [95]="PRLIM15",
    [96]="PRATTR0",
    [97]="PRATTR1",
    [98]="PRATTR2",
    [99]="PRATTR3",
    [100]="PRATTR4",
    [101]="PRATTR5",
    [102]="PRATTR6",
    [103]="PRATTR7",
    [104]="PRATTR8",
    [105]="PRATTR9",
    [106]="PRATTR10",
    [107]="PRATTR11",
    [108]="PRATTR12",
    [109]="PRATTR13",
    [110]="PRATTR14",
    [111]="PRATTR15",
    [112]="WPADDR0",
    [113]="WPADDR1",
    [114]="WPADDR2",
    [115]="WPADDR3",
    [116]="WPCTL0",
    [117]="WPCTL1",
    [118]="WPCTL2",
    [119]="WPCTL3",
};
int system_id(const char *n) {
    for(int i=0;i<120;i++)if(system_names[i]&&!strcmp(n, system_names[i]))return i;
    return -1;
}
const char *system_name(int i) {
    return i>=0&&i<120?system_names[i]:NULL;
}
