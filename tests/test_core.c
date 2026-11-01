#include "upsilon.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static CPU *machine(const char *source, size_t ram, Program *p) {
    CPU *c=calloc(1, sizeof *c);
    assert(c&&cpu_init(c, ram));
    assert(assemble(source, 4096, 0, p));
    assert(p->length<ram-4096);
    memcpy(c->ram+4096, p->data, p->length);
    c->s.pc=p->entry;
    return c;
}
static void dispose(CPU *c, Program *p) {
    cpu_destroy(c);
    free(c);
    free_program(p);
}
static void put(CPU *c, uint64_t address, uint64_t v) {
    for(int i=0;i<8;i++)c->ram[address+i]=(uint8_t)(v>>(8*i));
}
static uint64_t get(CPU *c, uint64_t address) {
    uint64_t v=0;
    for(int i=0;i<8;i++)v|=(uint64_t)c->ram[address+i]<<(8*i);
    return v;
}
static uint64_t sym(Program *p, const char *name) {
    for(size_t i=0;i<p->nsymbols;i++)if(!strcmp(p->symbols[i].name, name))return p->symbols[i].value;
    abort();
}
static void run(CPU *c) {
    for(int i=0;i<10000&&!c->s.halted&&!c->s.waiting;i++)assert(cpu_step(c, NULL));
    assert(c->s.halted||c->s.waiting);
}
static void fault_rollback(void) {
    Program p;
    CPU *c=machine("MOV.q #123,(R0)+", 8192, &p);
    c->s.r[0]=8188;
    State before=c->s;
    uint8_t memory[8192];
    memcpy(memory, c->ram, sizeof memory);
    assert(!cpu_step(c, NULL));
    assert(c->vector==8&&c->faultaddr==8192&&c->faultinfo==2);
    assert(!memcmp(&before, &c->s, sizeof before));
    assert(!memcmp(memory, c->ram, sizeof memory));
    dispose(c, &p);
    c=machine("XCHG.b @0xffff0000,#1", 8192, &p);
    c->serial=1;
    cpu_feed(c, (const uint8_t*)"A", 1);
    assert(!cpu_step(c, NULL));
    assert(c->vector==3&&c->rx_pos==0&&c->tx_len==0);
    dispose(c, &p);
    c=machine("XCHG.b @0xffff0000,@0xffff0000\nHALT", 8192, &p);
    c->serial=1;
    cpu_feed(c, (const uint8_t*)"AB", 2);
    run(c);
    assert(c->rx_pos==2&&c->tx_len==2&&!memcmp(c->tx, "BA", 2));
    dispose(c, &p);
}
static void mmu(void) {
    Program p;
    CPU *c=machine("MOV.q #123,@0x4000\nMOV.q @0x4000,R0\nHALT", 131072, &p);
    memcpy(c->ram+0x9000, p.data, p.length);
    put(c, 0x1000, 0x2001);
    put(c, 0x2000, 0x3001);
    put(c, 0x3000, 0x4001);
    put(c, 0x4008, 0x900f);
    put(c, 0x4020, 0xa007);
    c->s.sys[17]=0x1000;
    c->s.sys[20]=1;
    run(c);
    assert(c->s.r[0]==123&&get(c, 0xa000)==123);
    assert((get(c, 0x4020)&192)==192);
    dispose(c, &p);
    c=machine("MOV.q #123,@0x4ffc", 131072, &p);
    memcpy(c->ram+0x9000, p.data, p.length);
    put(c, 0x1000, 0x2001);
    put(c, 0x2000, 0x3001);
    put(c, 0x3000, 0x4001);
    put(c, 0x4008, 0x900f);
    put(c, 0x4020, 0xa007);
    c->s.sys[17]=0x1000;
    c->s.sys[20]=1;
    uint8_t *copy=malloc(c->ram_size);
    assert(copy);
    memcpy(copy, c->ram, c->ram_size);
    assert(!cpu_step(c, NULL));
    assert(c->vector==7&&c->faultaddr==0x5000);
    assert(!memcmp(copy, c->ram, c->ram_size));
    free(copy);
    dispose(c, &p);
}
static void traps_interrupts(void) {
    Program p;
    CPU *c=machine("SYSCALL #42\nNOP\nhandler: MFSR FAULTINFO,R0\nRTE", 65536, &p);
    c->traps=1;
    c->s.sys[24]=65536;
    c->s.sys[26]=32768;
    c->s.psr=2<<12;
    c->s.r[31]=32768;
    put(c, 12*8, sym(&p, "handler"));
    assert(cpu_step(c, NULL));
    assert(((c->s.psr>>12)&3)==0&&get(c, c->s.r[31])==(2<<12)&&get(c,c->s.r[31]+8)==4099);
    assert(cpu_step(c, NULL));
    assert(cpu_step(c, NULL));
    assert(c->s.pc==4099&&c->s.r[31]==32768&&c->s.r[0]==42&&((c->s.psr>>12)&3)==2);
    dispose(c, &p);
    c=machine("WAIT\nHALT\nhandler: INC.q R0\nRTE", 65536, &p);
    c->s.psr=0;
    put(c, 66*8, sym(&p, "handler"));
    assert(cpu_step(c, NULL)&&c->s.waiting);
    assert(cpu_interrupt(c, 66, 3));
    assert(cpu_step(c, NULL)&&!c->s.waiting);
    assert(((c->s.psr>>8)&7)==3);
    run(c);
    assert(c->s.r[0]==1&&c->irq_count==0);
    dispose(c, &p);
}
static void wide_and_fp(void) {
    Program p;
    CPU *c=machine("MOV.q #0,R0\nMOV.q #0x8000000000000000,R1\nDIVW.q #-1,R0", 65536, &p);
    assert(cpu_step(c, NULL));
    assert(cpu_step(c, NULL));
    State state=c->s;
    assert(!cpu_step(c, NULL));
    assert(c->vector==9&&!memcmp(&state, &c->s, sizeof state));
    dispose(c, &p);
    c=machine("FMOV.d #1.,F0\nMTFS #2048\nFDIV.d #0.,F0", 65536, &p);
    assert(cpu_step(c, NULL));
    assert(cpu_step(c, NULL));
    state=c->s;
    assert(!cpu_step(c, NULL));
    assert(c->vector==10&&!memcmp(&state, &c->s, sizeof state));
    dispose(c, &p);
    int flags=0;
    assert(fp_integer(UINT64_C(9007199791611905), 0, 4, 0, &flags)==UINT64_C(0x5a000001));
    assert(flags&1);
}
static void assembler_errors(void) {
    const char *cases[]= {
        "MOV.b #256,R0", ".byte 256", "MOV.q #missing,R0", "MOV.q (R0)+[R1],R2", "LEA.b (R0),R1", "MOVIF.l.d R0,F0", ".space 1000", NULL
    };
    for(int i=0;cases[i];i++) {
        Program p;
        assert(!assemble(cases[i], 4096, 256, &p));
        assert(p.error[0]);
        free_program(&p);
    }
}
static void tape_transactions(void) {
    char path[]="/tmp/upsilon-tape-XXXXXX",error[256];
    int fd=mkstemp(path);assert(fd>=0);close(fd);
    const uint8_t bytes[]={0xe0,0xf6,0x06};
    assert(tape_create(path,bytes,sizeof bytes,0x80000,0x80000,0,3,error,sizeof error));
    Program p;CPU *c=machine("MOV.b #2,@0xffff0108\nMOV.b @0xffff0100,R0\nHALT",8192,&p);
    assert(tape_open(&c->tape,path,0,error,sizeof error));run(c);
    assert(c->s.r[0]=='U'&&c->tape.state.offset==1&&c->tape.state.head==1);
    cpu_destroy(c);assert(cpu_init(c,8192));
    free_program(&p);assert(assemble("XCHG.b @0xffff0108,#1",4096,0,&p));
    memcpy(c->ram+4096,p.data,p.length);c->s.pc=4096;
    assert(tape_open(&c->tape,path,0,error,sizeof error));
    tape_command(&c->tape,&c->tape.state,TAPE_READ);
    TapeState before=c->tape.state;
    assert(!cpu_step(c,NULL)&&c->vector==3);
    assert(!memcmp(&before,&c->tape.state,sizeof before));
    free_program(&p);assert(assemble("MOV.w @0xffff0100,R0",4096,0,&p));
    memcpy(c->ram+4096,p.data,p.length);c->s.pc=4096;
    assert(!cpu_step(c,NULL)&&c->vector==8);
    assert(!memcmp(&before,&c->tape.state,sizeof before));
    tape_command(&c->tape,&c->tape.state,TAPE_READ); /* payload */
    assert(tape_read(&c->tape,&c->tape.state,0)==0xe0);
    tape_command(&c->tape,&c->tape.state,TAPE_SPACE); /* tape mark */
    assert(tape_status(&c->tape,&c->tape.state)&TAPE_MARK);
    tape_command(&c->tape,&c->tape.state,TAPE_READ);
    assert(tape_status(&c->tape,&c->tape.state)&TAPE_EOM);
    tape_command(&c->tape,&c->tape.state,TAPE_BACKSPACE);
    tape_command(&c->tape,&c->tape.state,TAPE_READ);
    assert(tape_status(&c->tape,&c->tape.state)&TAPE_MARK);
    tape_command(&c->tape,&c->tape.state,TAPE_REWIND);
    assert(tape_status(&c->tape,&c->tape.state)&TAPE_BOT);
    dispose(c,&p);
    c=machine("MOV.b #2,@0x4108\nMOV.b @0x4100,R0\nHALT",131072,&p);
    memcpy(c->ram+0x9000,p.data,p.length);
    put(c,0x1000,0x2001);put(c,0x2000,0x3001);put(c,0x3000,0x4001);
    put(c,0x4008,0x900f);put(c,0x4020,UINT64_C(0xffff0007));
    c->s.sys[17]=0x1000;c->s.sys[20]=1;
    assert(tape_open(&c->tape,path,0,error,sizeof error));run(c);
    assert(c->s.r[0]=='U'&&c->tape.state.offset==1);
    dispose(c,&p);unlink(path);
}
static void v04_values(void) {
    Program p;
    CPU *c=machine("MTSR #16384,TP\nMFTP R0\nMOV.q #42,8(TP)\nMOV.q 8(TP),R1\nCMP.q #42,R1\nSETEQ.b R2\nCMOVNE.q #99,R1\nCMOVEQ.w #123,R3\nMOV.q #-1,R4\nMOV.b #255,R4\nMOVSX.b.l #-1,R5\nFMOV.d #-1.,F0\nFMOV.s #1.,F0\nPAUSE\nPREFETCH @0xffffffffffffffff\nIOFENCE\nUAON\nUAOFF\nHALT",65536,&p);
    run(c);
    assert(c->s.r[0]==16384&&c->s.r[1]==42&&c->s.r[2]==1&&c->s.r[3]==123);
    assert(c->s.r[4]==255&&c->s.r[5]==UINT32_MAX&&c->s.f[0]==0x3f800000);
    assert(!(c->s.psr&(1u<<17)));
    dispose(c,&p);
    c=machine("CMOVNE.q @65536,R0",65536,&p);c->s.psr|=4;
    assert(!cpu_step(c,NULL)&&c->vector==8); /* False condition still reads. */
    dispose(c,&p);
    c=machine("MFSR #9,(R0)+",65536,&p);c->s.r[0]=65536;c->s.psr=2u<<12;
    assert(!cpu_step(c,NULL)&&c->vector==1&&c->s.r[0]==65536);
    dispose(c,&p);
    c=machine("FMOV.s @65536,F0",65536,&p);c->s.psr|=1u<<16;
    assert(!cpu_step(c,NULL)&&c->vector==15);
    dispose(c,&p);
    c=machine("MFTP R0\nPAUSE",65536,&p);c->s.psr=2u<<12;c->s.sys[7]=123;
    assert(cpu_step(c,NULL)&&c->s.r[0]==123);
    dispose(c,&p);
}
static void v04_cas2(void) {
    Program p;CPU *c=machine("CAS2 R0,R2,@16384\nCAS2 R0,R4,@16384\nHALT",65536,&p);
    c->s.r[0]=10;c->s.r[1]=20;c->s.r[2]=30;c->s.r[3]=40;
    put(c,16384,10);put(c,16392,20);
    assert(cpu_step(c,NULL)&&(c->s.psr&4)&&get(c,16384)==30&&get(c,16392)==40);
    assert(cpu_step(c,NULL)&&!(c->s.psr&4)&&c->s.r[0]==30&&c->s.r[1]==40);
    dispose(c,&p);
    c=machine("CAS2 R1,R2,@16384",65536,&p);
    assert(!cpu_step(c,NULL)&&c->vector==3);dispose(c,&p);
    c=machine("CAS2 R0,R2,@16392",65536,&p);
    assert(!cpu_step(c,NULL)&&c->vector==3);dispose(c,&p);
    c=machine("CAS2 R0,R2,@16384",16396,&p);
    put(c,16384,10);c->s.r[0]=10;c->s.r[2]=99;
    assert(!cpu_step(c,NULL)&&c->vector==8&&get(c,16384)==10);dispose(c,&p);
}
static void v04_regions_and_devices(void) {
    Program p;CPU *c=machine("MOV.q @16384,R0",65536,&p);
    c->s.sys[40]=1;c->s.sys[64]=4096;c->s.sys[80]=8192;c->s.sys[96]=0x2e;
    c->s.sys[65]=16384;c->s.sys[81]=20480;c->s.sys[97]=0x26;
    assert(!cpu_step(c,NULL)&&c->vector==8); /* Kernel cannot fetch a user page. */
    c->s.sys[96]=0xe;
    assert(!cpu_step(c,NULL)&&c->vector==8); /* Kernel data access needs UA. */
    c->s.psr|=1u<<17;put(c,16384,42);
    assert(cpu_step(c,NULL)&&c->s.r[0]==42);
    dispose(c,&p);
    c=machine("FMOV.h @0xffff0100,F0",65536,&p);
    /* Two-byte device read must fail before consuming the first data byte. */
    assert(!cpu_step(c,NULL)&&c->vector==8);
    assert(c->tape.state.offset==0);
    dispose(c,&p);
    c=machine("MOV.w @16385,R0",65536,&p);
    c->s.sys[40]=1;c->s.sys[64]=16384;c->s.sys[80]=20480;c->s.sys[96]=0x206;
    assert(!cpu_step(c,NULL)&&c->vector==8);dispose(c,&p);
    c=machine("NOP",65536,&p);
    c->s.sys[40]=1;c->s.sys[64]=4096;c->s.sys[80]=8192;c->s.sys[96]=0x20e;
    assert(!cpu_step(c,NULL)&&c->vector==8);dispose(c,&p);
    /* PTE MT2 is accepted for data, rejects fetch and unaligned access. */
    c=machine("MOV.q @0x4000,R0",131072,&p);
    memcpy(c->ram+0x9000,p.data,p.length);
    put(c,0x1000,0x2001);put(c,0x2000,0x3001);put(c,0x3000,0x4001);
    put(c,0x4008,0x900f);put(c,0x4020,(UINT64_C(2)<<52)|0xa007);put(c,0xa000,123);
    c->s.sys[17]=0x1000;c->s.sys[20]=1;
    assert(cpu_step(c,NULL)&&c->s.r[0]==123);
    c->s.pc=4096;put(c,0x4008,(UINT64_C(2)<<52)|0x900f);
    assert(!cpu_step(c,NULL)&&c->vector==8);dispose(c,&p);
}
static void v04_debug_timer_emergency(void) {
    Program p;CPU *c=machine("MOV.q #77,@16384\nHALT\ntrace: RTE",65536,&p);
    c->traps=1;c->s.psr=1u<<18;put(c,17*8,sym(&p,"trace"));
    assert(cpu_step(c,NULL)&&get(c,16384)==77&&c->s.sys[21]==17);
    assert(get(c,c->s.r[31])==(1u<<18)&&get(c,c->s.r[31]+8)==4104); /* MOV encoding is 8 bytes. */
    assert(!(c->s.psr&(1u<<18)));
    assert(cpu_step(c,NULL)&&c->s.pc==4104&&c->s.sys[21]==17); /* RTE is not traced again. */
    dispose(c,&p);
    c=machine("HALT\ntrace: HALT",65536,&p);c->s.psr|=1u<<18;c->traps=1;
    put(c,17*8,sym(&p,"trace"));
    assert(cpu_step(c,NULL)&&!c->s.halted&&c->s.sys[21]==17);
    assert(cpu_step(c,NULL)&&c->s.halted);dispose(c,&p);
    c=machine("MOV.q #77,@16384",65536,&p);c->s.psr|=1u<<18;c->traps=1;
    c->s.r[31]=8; /* Trace delivery fails, completed store must survive. */
    assert(!cpu_step(c,NULL)&&c->vector==19&&get(c,16384)==77&&c->steps==1);
    dispose(c,&p);
    c=machine("MOV.q #77,@16384",65536,&p);c->s.sys[112]=16384;c->s.sys[116]=1|16|256;
    State before=c->s;assert(!cpu_step(c,NULL)&&c->vector==18&&!memcmp(&before,&c->s,sizeof before)&&get(c,16384)==0);
    dispose(c,&p);
    c=machine("MTSR #3,WPADDR0\nMTSR #7,WPCTL0",65536,&p);
    assert(cpu_step(c,NULL));assert(!cpu_step(c,NULL)&&c->vector==3);dispose(c,&p);
    c=machine("WAIT\nHALT\ntick: MTSR #-1,TIMECMP\nRTE",65536,&p);
    c->s.psr=0;c->s.sys[28]=100;put(c,64*8,sym(&p,"tick"));
    assert(cpu_step(c,NULL)&&c->s.waiting&&cpu_runnable(c));
    assert(cpu_step(c,NULL)&&!c->s.waiting&&c->s.sys[1]==100&&c->s.sys[21]==64&&((c->s.psr>>8)&7)==6);
    run(c);assert(c->s.halted);dispose(c,&p);
    c=machine("SYSCALL #1\nemergency: HALT",65536,&p);c->traps=1;c->s.r[31]=8;c->s.sys[8]=32768;
    put(c,19*8,sym(&p,"emergency"));
    assert(cpu_step(c,NULL)&&c->s.sys[21]==19&&c->s.r[31]==32752&&c->s.pc==sym(&p,"emergency"));
    dispose(c,&p);
    c=machine("NOP\nnmi: RTE",65536,&p);c->s.sys[8]=32768;put(c,20*8,sym(&p,"nmi"));
    assert(cpu_interrupt(c,20,7)&&cpu_interrupt(c,20,7));
    assert(cpu_step(c,NULL)&&c->s.nmi_active&&c->irq_count==1);
    assert(cpu_step(c,NULL)&&!c->s.nmi_active&&c->s.pc==4096&&c->s.r[31]==65536);
    assert(cpu_step(c,NULL)&&c->s.nmi_active&&c->irq_count==0);
    assert(!cpu_interrupt(c,67,7)&&!cpu_interrupt(c,64,3));dispose(c,&p);
}

static void tape_persistent_writes(void) {
    char path[]="/tmp/upsilon-write-XXXXXX",error[256];
    int fd=mkstemp(path);assert(fd>=0);close(fd);
    const uint8_t boot[]={0xe0,0xf6,0x06};
    assert(tape_create(path,boot,sizeof boot,0x80000,0x80000,0,3,error,sizeof error));
    Program p;CPU *c=machine("MOV.q #data,@0xffff0128\nMOV.q #3,@0xffff0130\nMOV.b #5,@0xffff0108\nHALT\ndata: .ascii \"abc\"",65536,&p);
    assert(tape_open(&c->tape,path,0,error,sizeof error));
    size_t count=c->tape.count;
    run(c);assert(c->tape.count==count+1&&!(c->tape.state.flags&TAPE_ERROR));
    Tape check={0};assert(tape_open(&check,path,0,error,sizeof error));
    assert(check.count==count+1&&check.objects[count].length==3);
    assert(!memcmp(check.image+check.objects[count].offset,"abc",3));
    tape_close(&check);dispose(c,&p);
    c=machine("XCHG.b @0xffff0108,#5",65536,&p);
    assert(tape_open(&c->tape,path,0,error,sizeof error));
    c->tape.state.dma_addr=8192;c->tape.state.dma_len=1;c->ram[8192]='z';
    size_t size=c->tape.size;
    assert(!cpu_step(c,NULL)&&c->vector==3&&c->tape.size==size&&c->tape.count==count+1);
    assert(!c->tape.state.append);dispose(c,&p);
    c=machine("MOV.b #5,@0xffff0108\nHALT",65536,&p);
    assert(tape_open(&c->tape,path,0,error,sizeof error));
    c->tape.state.dma_addr=65535;c->tape.state.dma_len=2;
    run(c);assert(c->tape.state.flags&TAPE_ERROR);assert(c->tape.size==size);
    dispose(c,&p);
    c=machine("XCHG.b @8192,@0xffff0108\nHALT",65536,&p);
    assert(tape_open(&c->tape,path,0,error,sizeof error));
    c->tape.state.dma_addr=8192;c->tape.state.dma_len=1;c->ram[8192]=5;
    run(c);assert(c->ram[8192]==0&&c->tape.count==count+2);
    assert(c->tape.image[c->tape.objects[count+1].offset]==0); /* DMA observes prior staged store. */
    dispose(c,&p);
    c=machine("MOV.b #5,@0xffff0108\nHALT",65536,&p);
    assert(tape_open(&c->tape,path,0,error,sizeof error));
    c->tape.state.dma_addr=8192;c->tape.state.dma_len=1;c->ram[8192]='z';
    free(c->tape.path);
    c->tape.path=malloc(strlen(path)+32);assert(c->tape.path);
    sprintf(c->tape.path,"%s/not-a-directory/reel.tap",path);
    size=c->tape.size;run(c);
    assert(c->tape.state.flags&TAPE_ERROR);assert(c->tape.size==size&&c->tape.count==count+2);
    assert(tape_open(&check,path,0,error,sizeof error));
    assert(check.size==size&&!memcmp(check.image,c->tape.image,size));
    tape_close(&check);dispose(c,&p);unlink(path);
    char lockpath[128];snprintf(lockpath,sizeof lockpath,"%s.lock",path);unlink(lockpath);
}

static void qcow_disk_transactions(void) {
    char path[]="/tmp/upsilon-disk-XXXXXX",error[256];
    int fd=mkstemp(path);assert(fd>=0);close(fd);unlink(path);
    assert(disk_create(path,1024*1024,error,sizeof error));
    assert(!disk_create(path,1024*1024,error,sizeof error));
    Program p;CPU *c=machine("MOV.q #0,@0xffff0310\nMOV.q #8192,@0xffff0318\nMOV.q #512,@0xffff0320\nMOV.b #2,@0xffff0308\nHALT",65536,&p);
    assert(disk_open(&c->disk,path,0,0,error,sizeof error));
    for(int i=0;i<512;i++)c->ram[8192+i]=(uint8_t)i;
    run(c);assert(!c->disk_state.error&&!memcmp(c->disk.data,c->ram+8192,512));
    dispose(c,&p);
    c=machine("XCHG.b @0xffff0308,#2",65536,&p);
    assert(disk_open(&c->disk,path,0,0,error,sizeof error));
    c->disk_state.buffer=8192;c->disk_state.length=512;memset(c->ram+8192,99,512);
    uint8_t old[512];memcpy(old,c->disk.data,512);
    assert(!cpu_step(c,NULL)&&c->vector==3&&!memcmp(old,c->disk.data,512)&&!c->disk_state.pending);
    dispose(c,&p);
    c=machine("MOV.b #1,@0xffff0308\nHALT",65536,&p);
    assert(disk_open(&c->disk,path,0,0,error,sizeof error));
    c->disk_state.buffer=8192;c->disk_state.length=512;c->rom_start=8192;c->rom_end=8704;
    run(c);assert(c->disk_state.error&&c->ram[8193]==0);dispose(c,&p);
    c=machine("MOV.b #2,@0xffff0308\nHALT",65536,&p);
    assert(disk_open(&c->disk,path,1,0,error,sizeof error));
    c->disk_state.buffer=8192;c->disk_state.length=512;
    run(c);assert(c->disk_state.error&&!memcmp(old,c->disk.data,512));dispose(c,&p);
    c=machine("MOV.b #2,@0xffff0308\nHALT",65536,&p);
    assert(disk_open(&c->disk,path,0,0,error,sizeof error));
    c->disk_state.buffer=8192;c->disk_state.length=512;
    free(c->disk.path);c->disk.path=malloc(strlen(path)+32);assert(c->disk.path);
    sprintf(c->disk.path,"%s/not-a-directory/image",path);
    run(c);assert(c->disk_state.error&&!memcmp(old,c->disk.data,512));dispose(c,&p);
    Disk check={0};assert(disk_open(&check,path,1,0,error,sizeof error));
    assert(!memcmp(old,check.data,512));disk_close(&check);unlink(path);
    char lockpath[128];snprintf(lockpath,sizeof lockpath,"%s.lock",path);unlink(lockpath);
}

int main(void) {
    qcow_disk_transactions();
    tape_persistent_writes();
    v04_values();v04_cas2();v04_regions_and_devices();v04_debug_timer_emergency();
    tape_transactions();
    fault_rollback();
    mmu();
    traps_interrupts();
    wide_and_fp();
    assembler_errors();
    puts("C core: v0.4 values, CAS2, MPU/device memory, debug/timer/emergency traps; tape, MMU, arithmetic and FP passed");
    return 0;
}
