#ifndef UPSILON_ASSEMBLER_H
#define UPSILON_ASSEMBLER_H
#include "isa.h"
typedef struct {
    char name[96];
    uint64_t value;
} Symbol;
typedef struct {
    uint8_t *data;
    size_t length;
    uint64_t origin, entry;
    Symbol *symbols;
    size_t nsymbols;
    char error[512];
} Program;
int assemble(const char *source, uint64_t origin, size_t limit, Program *out);
void free_program(Program *p);
char *readfile(const char *,size_t *);
char *sourcefile(const char *,int);
int writefile(const char *,const void *,size_t);
int compose_rom(const char *,const char *,const char *,char **);
#endif
