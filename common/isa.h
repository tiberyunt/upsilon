#ifndef UPSILON_ISA_H
#define UPSILON_ISA_H
#include <stdint.h>
#include <stddef.h>
typedef unsigned __int128 u128;
typedef __int128 i128;
enum {
    F_TWO, F_THREE, F_BRANCH, F_SHORT, F_FP, F_BLOCK, F_FIELD, F_BITBR, F_ATOMIC, F_CONV, F_BITS, F_SYSTEM, F_LOOP, F_CASE, F_FPSR, F_SET, F_CMOV, F_HINT
};
typedef struct {
    const char *name;
    int family, op, count;
} Op;
extern const Op ops[];
extern const size_t nops;
extern const double fp_constants[24];
const Op *find_op(const char *name);
int fp_operand(const Op *op, int index);
int operand_size(const Op *op, int size, int aux, int index);
int system_id(const char *name);
const char *system_name(int id);
#endif
