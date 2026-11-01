#ifndef UPSILON_H
#define UPSILON_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>
#include "assembler.h"
typedef struct {
    int mode, reg, index, kind;
    uint64_t value;
} Operand;
typedef struct {
    const Op *op;
    int size, aux, count;
    Operand operands[4];
    uint64_t start, end, target;
    int branch;
} Ins;
#define RAM_DEFAULT (1u<<20)
#define SERIAL_BASE UINT64_C(0xffff0000)
#define UART16550_BASE UINT64_C(0xffff0400)
typedef struct { uint8_t ier,lcr,mcr,scr,dll,dlm,fcr; } UARTState;
typedef struct {
    uint64_t r[32], f[32], pc, psr, fpsr, sys[120];
    int halted, waiting;
    uint64_t emergency_frame, emergency_sp;
    int emergency_active, nmi_active;
} State;
typedef struct {
    uint64_t addr;
    uint8_t value;
} Write;
typedef struct {
    int active;
    uint64_t pc, count, src, dst, end_src, end_dst;
} Block;
#define TAPE_BASE UINT64_C(0xffff0100)
#define PLATFORM_BASE UINT64_C(0xffff0200)
enum {TAPE_ONLINE=1,TAPE_READY=2,TAPE_MARK=4,TAPE_EOM=8,TAPE_ERROR=16,TAPE_BOT=32,TAPE_AUTO=64,TAPE_READONLY=128};
enum {TAPE_REWIND=1,TAPE_READ=2,TAPE_SPACE=3,TAPE_BACKSPACE=4,TAPE_APPEND=5,TAPE_RECORD_ERROR=128};
typedef struct {size_t offset;uint32_t length,flags;} TapeObject;
typedef struct {size_t head,current,offset;uint32_t flags;int autoboot;uint8_t command;uint64_t dma_addr,dma_len;int append;} TapeState;
typedef struct {uint8_t *image;size_t size;TapeObject *objects;size_t count,capacity;int mounted,writable,lock_fd;char *path,*lock_path;TapeState state;} Tape;
int tape_open(Tape *t,const char *path,int autoboot,char *error,size_t cap);
void tape_close(Tape *t);
uint8_t tape_status(const Tape *t,const TapeState *s);
int tape_readable(unsigned reg);
uint8_t tape_read(Tape *t,TapeState *s,unsigned reg);
void tape_command(Tape *t,TapeState *s,uint8_t command);
void tape_write_register(Tape *t,TapeState *s,unsigned reg,uint8_t value);
int tape_writable_register(unsigned reg);
int tape_append(Tape *t,const uint8_t *data,size_t length,char *error,size_t cap);
int tape_create(const char *path,const uint8_t *data,size_t length,uint64_t load,uint64_t entry,uint64_t stack,size_t block,char *error,size_t cap);
#define DISK_COUNT 4
#define DISK_BASE UINT64_C(0xffff0300)
typedef struct {uint8_t *data;uint64_t size;int mounted,writable,autoboot,lock_fd;char *path,*lock_path;} Disk;
typedef struct {uint64_t offset,buffer,length;uint8_t command,error,pending;} DiskState;
typedef struct {Disk media;DiskState state,work;int dirty;} DiskDevice;
int disk_open(Disk *d,const char *path,int readonly,int autoboot,char *error,size_t cap);
void disk_close(Disk *d);
int disk_create(const char *path,uint64_t size,char *error,size_t cap);
int disk_write(Disk *d,uint64_t offset,const uint8_t *data,size_t length,char *error,size_t cap);
typedef struct CPU {
    State s;
    UARTState uart, uart_work;
    int uart_dirty;
    union {
        struct {Disk disk;DiskState disk_state,disk_work;int disk_dirty;};
        DiskDevice disks[DISK_COUNT];
    };
    Tape tape;
    TapeState tape_work;
    int tape_dirty;
    uint8_t *ram;
    size_t ram_size;
    uint64_t rom_start, rom_end, steps;
    int serial, keyboard, eof, traps, vector;
    uint64_t faultaddr, faultinfo;
    char error[256];
    uint8_t *rx;
    size_t rx_len, rx_pos, rx_cap;
    uint8_t *tx;
    size_t tx_len, tx_cap;
    int irq_vector[64], irq_level[64], irq_count;
    FILE *serial_out;
    Write writes[4096];
    size_t nwrites;
    int transaction, rx_reads, delivering;
    uint8_t tx_pending[32];
    size_t ntx;
    Block blocks[16];
    jmp_buf fault;
} CPU;
int cpu_init(CPU *c, size_t memory);
void cpu_destroy(CPU *c);
int cpu_runnable(CPU *c);
int cpu_step(CPU *c, Ins *decoded);
int cpu_decode(CPU *c, uint64_t pc, Ins *ins);
int cpu_reset(CPU *c, uint64_t addr);
int cpu_interrupt(CPU *c, int vector, int level);
void cpu_feed(CPU *c, const uint8_t *data, size_t len);
void cpu_fault(CPU *c, int vector, uint64_t addr, uint64_t info, const char *message);
uint64_t cpu_read(CPU *c, uint64_t addr, int size, int access);
void cpu_write(CPU *c, uint64_t addr, uint64_t value, int size);
void format_ins(const Ins *ins, char *out, size_t cap);
uint64_t fp_exact(int op, double a, double b, double c, int size, int mode, int *flags);
uint64_t fp_integer(uint64_t value, int is_signed, int size, int mode, int *flags);
int platform_load_elf(CPU *c, const uint8_t *data, size_t length,
                      const char *bootargs, char *error, size_t cap);
#endif
