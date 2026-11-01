#pragma STDC FENV_ACCESS ON
#include "upsilon.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <fenv.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#define C 1u
#define V 2u
#define Z 4u
#define N 8u
#define FPD (UINT64_C(1)<<16)
#define UA (UINT64_C(1)<<17)
#define TF (UINT64_C(1)<<18)
#define PSR_MASK UINT64_C(0x7f70f)
/* Revision 1; workstation, MMU, MPU, FP; 64-byte lines, PA52/VA48. */
#define ARCH_ID ((UINT64_C(48)<<46)|(UINT64_C(52)<<40)|(UINT64_C(6)<<32)|UINT64_C(0x3b01))
static uint64_t mask(int n) {
    return n==8?UINT64_MAX:(UINT64_C(1)<<(8*n))-1;
}
static int64_t sx(uint64_t v, int n) {
    return n==8?(int64_t)v:(int64_t)(v<<(64-8*n))>>(64-8*n);
}
void cpu_fault(CPU *c, int vector, uint64_t addr, uint64_t info, const char *message) {
    c->vector=vector;
    c->faultaddr=addr;
    c->faultinfo=info;
    snprintf(c->error, sizeof c->error, "%s", message);
    longjmp(c->fault, 1);
}
static void check(CPU *c, uint64_t addr, size_t n, int write) {
    if(addr>c->ram_size||n>c->ram_size-addr)cpu_fault(c, 8, addr, write?2:1, "physical access outside RAM");
    if(write&&addr<c->rom_end&&addr+n>c->rom_start)cpu_fault(c, 8, addr, 2, "write to ROM");
}
static int serialaddr(CPU *c, uint64_t addr) {
    return c->serial&&addr>=SERIAL_BASE&&addr<SERIAL_BASE+8;
}
static int uartaddr(CPU *c,uint64_t addr){return c->serial&&addr>=UART16550_BASE&&addr<UART16550_BASE+8;}
static UARTState *uartstate(CPU *c){
    if(!c->transaction)return &c->uart;
    if(!c->uart_dirty){c->uart_work=c->uart;c->uart_dirty=1;}
    return &c->uart_work;
}
static int tapeaddr(uint64_t addr) {return addr>=TAPE_BASE&&addr<TAPE_BASE+56;}
static int platformaddr(uint64_t addr) {return addr>=PLATFORM_BASE&&addr<PLATFORM_BASE+17;}
static int diskaddr(uint64_t addr){return addr>=DISK_BASE&&addr<DISK_BASE+DISK_COUNT*64;}
static DiskState *diskstate(CPU *c,unsigned unit){
    DiskDevice *d=&c->disks[unit];
    if(!c->transaction)return &d->state;
    if(!d->dirty){d->work=d->state;d->dirty=1;}
    return &d->work;
}
static int diskreadable(unsigned r){return r==0||r==8||(r>=16&&r<48);}
static int diskwritable(unsigned r){return r==8||(r>=16&&r<40);}
static TapeState *tapestate(CPU *c) {
    if(!c->transaction)return &c->tape.state;
    if(!c->tape_dirty){c->tape_work=c->tape.state;c->tape_dirty=1;}
    return &c->tape_work;
}
void cpu_feed(CPU *c, const uint8_t *data, size_t n) {
    if(c->rx_pos>4096 && c->rx_pos>c->rx_len/2) {
        memmove(c->rx,c->rx+c->rx_pos,c->rx_len-c->rx_pos);
        c->rx_len-=c->rx_pos;c->rx_pos=0;
    }
    if(c->rx_len+n>c->rx_cap) {
        size_t cap=(c->rx_len+n)*2+256;
        void *p=realloc(c->rx, cap);
        if(!p) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        c->rx=p;
        c->rx_cap=cap;
    }
    memcpy(c->rx+c->rx_len, data, n);
    c->rx_len+=n;
}
static void pollinput(CPU *c) {
    if(!c->keyboard||c->eof||c->rx_pos+(size_t)c->rx_reads<c->rx_len)return;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval t= {
        0, 10000
    };
    int ready=select(STDIN_FILENO+1, &fds, NULL, NULL, &t);
    if(ready>0) {
        uint8_t b[256];
        ssize_t n=read(STDIN_FILENO, b, sizeof b);
        if(n>0)cpu_feed(c, b, (size_t)n);
        else if(!n)c->eof=1;
    }
    else if(ready<0&&errno!=EINTR)c->eof=1;
}
static uint64_t physical_read(CPU *c, uint64_t addr, int n) {
    if(uartaddr(c,addr)){
        if(n!=1)cpu_fault(c,8,addr,1,"invalid NS16550 register width");
        UARTState *u=uartstate(c);unsigned r=(unsigned)(addr-UART16550_BASE);
        pollinput(c);size_t pos=c->rx_pos+(size_t)c->rx_reads;int available=pos<c->rx_len;
        if((u->lcr&128)&&r<2)return r?u->dlm:u->dll;
        switch(r){
            case 0:if(!available)return 0;if(c->transaction)c->rx_reads++;else c->rx_pos++;return c->rx[pos];
            case 1:return u->ier;
            case 2:return (u->fcr&1?0xc0:0)|1; /* Polled console: no IRQ line yet. */
            case 3:return u->lcr;
            case 4:return u->mcr;
            case 5:return 0x60|(available?1:0);
            case 6:return 0xb0;
            case 7:return u->scr;
        }
    }
    if(diskaddr(addr)){
        unsigned unit=(unsigned)((addr-DISK_BASE)/64),r=(unsigned)((addr-DISK_BASE)%64);
        Disk *d=&c->disks[unit].media;DiskState *s=diskstate(c,unit);
        if(n!=1||!diskreadable(r))cpu_fault(c,8,addr,1,"invalid disk register");
        if(r==0)return (d->mounted?1:0)|(d->mounted&&!d->writable?2:0)|(s->error?4:0)|8|(d->autoboot?16:0);
        if(r==8)return s->command;
        uint64_t value=r<24?s->offset:r<32?s->buffer:r<40?s->length:d->size;
        return (value>>((r%8)*8))&255;
    }
    if(tapeaddr(addr)) {
        if(n!=1||!tape_readable((unsigned)(addr-TAPE_BASE)))cpu_fault(c,8,addr,1,"invalid tape register read");
        return tape_read(&c->tape,tapestate(c),(unsigned)(addr-TAPE_BASE));
    }
    if(platformaddr(addr)) {
        if(n!=1)cpu_fault(c,8,addr,1,"invalid platform register read");
        unsigned reg=(unsigned)(addr-PLATFORM_BASE);
        if(reg<8)return (c->ram_size>>(reg*8))&255;
        if(reg==16)return c->keyboard&&isatty(STDIN_FILENO);
        struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
        uint64_t ms=(uint64_t)ts.tv_sec*1000+(unsigned long)ts.tv_nsec/1000000;
        return (ms>>((reg-8)*8))&255;
    }
    if(serialaddr(c, addr)) {
        if(n!=1||(addr!=SERIAL_BASE&&addr!=SERIAL_BASE+4&&addr!=SERIAL_BASE+5))cpu_fault(c, 8, addr, 1, "invalid serial register read");
        pollinput(c);
        size_t pos=c->rx_pos+(size_t)c->rx_reads;
        int available=pos<c->rx_len;
        if(addr==SERIAL_BASE+4)return 2|(available?1:0)|(c->eof?4:0);
        if(!available)return 0;
        if(addr==SERIAL_BASE+5)return c->rx[pos];
        uint8_t byte=c->rx[pos];
        if(c->transaction)c->rx_reads++;
        else c->rx_pos++;
        return byte;
    }
    check(c, addr, (size_t)n, 0);
    uint64_t value=0;
    for(int i=0;i<n;i++) {
        uint8_t b=c->ram[addr+(unsigned)i];
        for(size_t j=c->nwrites;j>0;j--)if(c->writes[j-1].addr==addr+(unsigned)i) {
            b=c->writes[j-1].value;
            break;
        }
        value|=(uint64_t)b<<(8*i);
    }
    return value;
}
static void physical_write(CPU *c, uint64_t addr, uint64_t value, int n) {
    if(uartaddr(c,addr)){
        if(n!=1)cpu_fault(c,8,addr,2,"invalid NS16550 register width");
        UARTState *u=uartstate(c);unsigned r=(unsigned)(addr-UART16550_BASE);
        if((u->lcr&128)&&r<2){if(r)u->dlm=(uint8_t)value;else u->dll=(uint8_t)value;return;}
        switch(r){
            case 0:
                if(c->ntx==sizeof c->tx_pending)cpu_fault(c,14,addr,2,"I/O journal overflow");
                c->tx_pending[c->ntx++]=(uint8_t)value;break;
            case 1:u->ier=(uint8_t)value&15;break;
            case 2:u->fcr=(uint8_t)value&0xc9;if(value&2){size_t count=c->rx_len-c->rx_pos;c->rx_reads=count>INT_MAX?INT_MAX:(int)count;}break;
            case 3:u->lcr=(uint8_t)value;break;
            case 4:u->mcr=(uint8_t)value&31;break;
            case 7:u->scr=(uint8_t)value;break;
        }
        return;
    }
    if(diskaddr(addr)){
        unsigned unit=(unsigned)((addr-DISK_BASE)/64),r=(unsigned)((addr-DISK_BASE)%64);
        DiskState *s=diskstate(c,unit);
        if(n!=1||!diskwritable(r))cpu_fault(c,8,addr,2,"invalid disk register write");
        if(r==8){s->command=(uint8_t)value;if(s->pending){s->pending=0;s->error=1;}else if(value<1||value>3)s->error=1;else {s->pending=(uint8_t)value;s->error=0;}return;}
        uint64_t *target=r<24?&s->offset:r<32?&s->buffer:&s->length;
        unsigned shift=(r%8)*8;*target=(*target&~(UINT64_C(255)<<shift))|((value&255)<<shift);return;
    }
    if(tapeaddr(addr)) {
        if(n!=1||!tape_writable_register((unsigned)(addr-TAPE_BASE)))cpu_fault(c,8,addr,2,"invalid tape register write");
        tape_write_register(&c->tape,tapestate(c),(unsigned)(addr-TAPE_BASE),(uint8_t)value);return;
    }
    if(platformaddr(addr))cpu_fault(c,8,addr,2,"platform register is read-only");
    if(serialaddr(c, addr)) {
        if(n!=1||addr!=SERIAL_BASE)cpu_fault(c, 8, addr, 2, "invalid serial register write");
        if(c->ntx==sizeof c->tx_pending)cpu_fault(c, 14, addr, 2, "I/O journal overflow");
        c->tx_pending[c->ntx++]=(uint8_t)value;
        return;
    }
    check(c, addr, (size_t)n, 1);
    for(int i=0;i<n;i++) {
        if(!c->transaction) {
            c->ram[addr+i]=(uint8_t)(value>>(i*8));
            continue;
        }
        uint64_t at=addr+(unsigned)i;
        size_t j;
        for(j=0;j<c->nwrites;j++)if(c->writes[j].addr==at)break;
        if(j==4096)cpu_fault(c, 14, addr, 2, "write journal overflow");
        if(j==c->nwrites)c->nwrites++;
        c->writes[j]=(Write) {
            at, (uint8_t)(value>>(i*8))
        };
    }
}
static void permission(CPU *c, uint64_t addr, int access, int priv, uint64_t attr) {
    unsigned mode=(c->s.psr>>12)&3;
    if(priv==3||!(attr&(access==4?8:access==2?4:2))||
       (access==4?mode!=(unsigned)priv:mode>(unsigned)priv||((unsigned)priv>mode&&mode<2&&!(c->s.psr&UA))))
        cpu_fault(c,8,addr,access,"memory permission violation");
}
static uint64_t translate(CPU *c, uint64_t addr, int access, int *mt) {
    *mt=0;
    if(!(c->s.sys[20]&1)) {
        if(c->s.sys[40]&1) {
            int found=0;
            for(int i=0;i<16;i++)if(addr>=c->s.sys[64+i]&&addr<c->s.sys[80+i]) {
                uint64_t attr=c->s.sys[96+i];*mt=(int)((attr>>8)&3);
                if(*mt==3)cpu_fault(c,8,addr,access,"reserved memory type");
                permission(c,addr,access,(attr>>4)&3,attr);found=1;break;
            }
            if(!found&&((c->s.psr>>12)&3))cpu_fault(c,8,addr,access,"unmapped protection region");
        }
        return addr;
    }
    uint64_t high=addr>>48;
    if(high!=(addr&(UINT64_C(1)<<47)?0xffff:0))cpu_fault(c, 8, addr, access, "noncanonical virtual address");
    uint64_t table=c->s.sys[addr&(UINT64_C(1)<<47)?18:17]&UINT64_C(0x000ffffffffff000);
    int shifts[]= {
        39, 30, 21, 12
    };
    for(int level=0;level<4;level++) {
        int shift=shifts[level];
        uint64_t pa=table+((addr>>shift)&511)*8;
        if(pa>c->ram_size||8>c->ram_size-pa)cpu_fault(c, 13, addr, access, "page table outside RAM");
        uint64_t pte=physical_read(c, pa, 8);
        int priv=(pte>>4)&3;
        if(!(pte&1)||pte>>54||((pte>>52)&3)==3||priv==3||(!level&&(pte&512)))cpu_fault(c, access==4?5:access==2?7:6, addr, access, "invalid page table entry");
        int leaf=level==3||((level==1||level==2)&&(pte&512));
        uint64_t base=pte&UINT64_C(0x000ffffffffff000);
        if(leaf) {
            permission(c,addr,access,priv,pte);
            *mt=(int)((pte>>52)&3);
            if(base&((UINT64_C(1)<<shift)-1))cpu_fault(c, access==4?5:access==2?7:6, addr, access, "misaligned large page");
            physical_write(c, pa, pte|64|(access==2?128:0), 8);
            return base+(addr&((UINT64_C(1)<<shift)-1));
        }
        physical_write(c, pa, pte|64, 8);
        table=base;
    }
    cpu_fault(c, 8, addr, access, "unmapped address");
    return 0;
}
static void watch(CPU *c, uint64_t addr, int n, int access) {
    unsigned mode=(c->s.psr>>12)&3;
    for(int i=0;i<4;i++) {
        uint64_t ctl=c->s.sys[116+i],base=c->s.sys[112+i];
        uint64_t length=UINT64_C(1)<<((ctl>>1)&3);
        if((ctl&1)&&(ctl&(UINT64_C(1)<<(8-mode)))&&
           (ctl&(access==4?32:access==2?16:8))&&
           (addr>=base?addr-base<length:base-addr<(unsigned)n))
            cpu_fault(c,18,addr,access,"hardware watchpoint");
    }
}
/* Preflight every byte before touching a device. Translation writes are journaled. */
static void access_map(CPU *c, uint64_t addr, int n, int access, uint64_t *physical) {
    if(n<1||n>8||addr>UINT64_MAX-(unsigned)(n-1))cpu_fault(c,8,addr,access,"invalid access width or address wrap");
    for(int i=0;i<n;i++) {
        uint64_t va=addr+(unsigned)i;int mt;
        uint64_t pa=translate(c,va,access,&mt);
        int device=mt==2||uartaddr(c,pa)||serialaddr(c,pa)||tapeaddr(pa)||platformaddr(pa)||diskaddr(pa);
        if(device&&(access==4||addr%(unsigned)n))cpu_fault(c,8,va,access,"device fetch or unaligned device access");
        if(diskaddr(pa)) {
            unsigned r=(unsigned)((pa-DISK_BASE)%64);
            if(access==2?!diskwritable(r):!diskreadable(r))cpu_fault(c,8,va,access,"invalid disk register");
        } else if(tapeaddr(pa)) {
            if(access==2?!tape_writable_register((unsigned)(pa-TAPE_BASE)):!tape_readable((unsigned)(pa-TAPE_BASE)))cpu_fault(c,8,va,access,"invalid tape register");
        } else if(platformaddr(pa)) {
            if(access==2)cpu_fault(c,8,va,access,"platform register is read-only");
        } else if(uartaddr(c,pa)) {
            if(n!=1)cpu_fault(c,8,va,access,"NS16550 requires byte accesses");
        } else if(serialaddr(c,pa)) {
            if(access==2?pa!=SERIAL_BASE:pa!=SERIAL_BASE&&pa!=SERIAL_BASE+4&&pa!=SERIAL_BASE+5)cpu_fault(c,8,va,access,"invalid serial register");
        } else {
            if(pa>=c->ram_size)cpu_fault(c,c->s.sys[20]&1?13:8,va,access,"physical access outside RAM");
            check(c,pa,1,access==2);
        }
        physical[i]=pa;
    }
    watch(c,addr,n,access);
}
uint64_t cpu_read(CPU *c, uint64_t addr, int n, int access) {
    uint64_t physical[8],value=0;
    access_map(c,addr,n,access,physical);
    for(int i=0;i<n;i++)value|=physical_read(c,physical[i],1)<<(8*i);
    return value;
}
void cpu_write(CPU *c, uint64_t addr, uint64_t value, int n) {
    uint64_t physical[8];
    access_map(c,addr,n,2,physical);
    for(int i=0;i<n;i++)physical_write(c,physical[i],value>>(8*i),1);
}
static void commit(CPU *c) {
    if(c->tx_len+c->ntx>c->tx_cap) {
        size_t cap=(c->tx_len+c->ntx)*2+256;
        void *p=realloc(c->tx, cap);
        if(!p)cpu_fault(c, 14, c->s.pc, 0, "out of memory");
        c->tx=p;
        c->tx_cap=cap;
    }
    if(c->tape_dirty&&c->tape_work.append) {
        TapeState *s=&c->tape_work;
        /* DMA uses physical RAM, including earlier staged writes in this
         * instruction. The device exposes no host filesystem/OS service. */
        int ok=0;
        if(s->dma_addr<=c->ram_size&&s->dma_len<=c->ram_size-s->dma_addr) {
            const uint8_t *data=c->ram+s->dma_addr;uint8_t *copy=NULL;
            int overlay=0;
            for(size_t i=0;i<c->nwrites;i++)if(c->writes[i].addr>=s->dma_addr&&c->writes[i].addr-s->dma_addr<s->dma_len)overlay=1;
            if(overlay) {
                copy=malloc((size_t)s->dma_len);
                if(copy) {
                    memcpy(copy,data,(size_t)s->dma_len);
                    for(size_t i=0;i<c->nwrites;i++)if(c->writes[i].addr>=s->dma_addr&&c->writes[i].addr-s->dma_addr<s->dma_len)
                        copy[c->writes[i].addr-s->dma_addr]=c->writes[i].value;
                    data=copy;
                }
            }
            if(!overlay||copy)ok=tape_append(&c->tape,data,(size_t)s->dma_len,c->error,sizeof c->error);
            free(copy);
        }
        if(!ok)s->flags=TAPE_ERROR;
        else {s->head=c->tape.count;s->current=c->tape.count-1;s->offset=(size_t)s->dma_len;s->flags=0;}
        s->append=0;
    }
    for(size_t i=0;i<c->nwrites;i++)c->ram[c->writes[i].addr]=c->writes[i].value;
    for(unsigned unit=0;unit<DISK_COUNT;unit++){
        DiskDevice *d=&c->disks[unit];if(!d->dirty)continue;
        DiskState *s=&d->work;
        if(s->pending){
            int ok=d->media.mounted;
            if(s->pending!=3){
                ok=ok&&s->length&&s->length<=1048576&&s->offset<=d->media.size&&s->length<=d->media.size-s->offset&&s->buffer<=c->ram_size&&s->length<=c->ram_size-s->buffer;
                if(ok&&s->pending==1){
                    if(s->buffer<c->rom_end&&s->buffer+s->length>c->rom_start)ok=0;
                    else memcpy(c->ram+s->buffer,d->media.data+s->offset,(size_t)s->length);
                }else if(ok)ok=disk_write(&d->media,s->offset,c->ram+s->buffer,(size_t)s->length,c->error,sizeof c->error);
            }
            s->error=!ok;s->pending=0;
        }
        d->state=*s;d->dirty=0;
    }
    if(c->uart_dirty)c->uart=c->uart_work;
    c->uart_dirty=0;
    if(c->tape_dirty)c->tape.state=c->tape_work;
    c->tape_dirty=0;
    c->rx_pos+=(size_t)c->rx_reads;
    for(size_t i=0;i<c->ntx;i++)c->tx[c->tx_len++]=c->tx_pending[i];
    if(c->serial_out&&c->ntx) {
        fwrite(c->tx_pending, 1, c->ntx, c->serial_out);
        fflush(c->serial_out);
    }
    c->nwrites=c->ntx=0;
    c->rx_reads=0;c->tape_dirty=0;for(unsigned i=0;i<DISK_COUNT;i++)c->disks[i].dirty=0;
    c->transaction=0;
}
int cpu_init(CPU *c, size_t n) {
    memset(c, 0, sizeof *c);
    if(!n)return 0;
    c->ram=calloc(n,1);
    if(!c->ram)return 0;
    c->ram_size=n;
    c->uart.dll=1;c->uart.lcr=3;
    c->s.r[31]=n&~UINT64_C(15);
    c->s.psr=0x700;
    c->s.sys[0]=ARCH_ID;
    c->s.sys[6]=1000000;
    c->s.sys[28]=UINT64_MAX;
    c->s.sys[24]=c->s.r[31];
    return 1;
}
void cpu_destroy(CPU *c) {
    for(unsigned i=0;i<DISK_COUNT;i++)disk_close(&c->disks[i].media);
    tape_close(&c->tape);
    free(c->ram);
    free(c->rx);
    free(c->tx);
    memset(c, 0, sizeof *c);
}
int cpu_reset(CPU *c, uint64_t addr) {
    if(addr>c->ram_size||16>c->ram_size-addr)return 0;
    uint64_t sp=0, pc=0;
    for(int i=0;i<8;i++) {
        sp|=(uint64_t)c->ram[addr+i]<<(8*i);
        pc|=(uint64_t)c->ram[addr+8+i]<<(8*i);
    }
    memset(&c->s, 0, sizeof c->s);
    memset(c->blocks, 0, sizeof c->blocks);
    c->steps=0;
    c->irq_count=0;
    for(unsigned i=0;i<DISK_COUNT;i++){memset(&c->disks[i].state,0,sizeof(DiskState));c->disks[i].dirty=0;}
    c->tape_dirty=0;
    memset(&c->uart,0,sizeof c->uart);c->uart.dll=1;c->uart.lcr=3;c->uart_dirty=0;
    if(c->tape.mounted){int automatic=c->tape.state.autoboot;tape_command(&c->tape,&c->tape.state,TAPE_REWIND);c->tape.state.autoboot=automatic;}
    c->s.r[31]=sp;
    c->s.pc=pc;
    c->s.psr=0x700;
    c->s.sys[0]=ARCH_ID;
    c->s.sys[6]=1000000;
    c->s.sys[28]=UINT64_MAX;
    c->s.sys[24]=sp;
    return 1;
}
static void privileged(CPU *c, int allowed) {
    if(((c->s.psr>>12)&3)>(unsigned)allowed)cpu_fault(c, 4, c->s.pc, 0, "privileged instruction");
}
static void mode(CPU *c, uint64_t psr) {
    unsigned old=(c->s.psr>>12)&3, newmode=(psr>>12)&3;
    if(newmode==3)cpu_fault(c, 3, c->s.pc, 0, "reserved processor mode");
    if(old!=newmode) {
        c->s.sys[24+old]=c->s.r[31];
        c->s.r[31]=c->s.sys[24+newmode];
    }
    c->s.psr=psr&PSR_MASK;
}
static void push(CPU *c, uint64_t v, int n) {
    c->s.r[31]-=(unsigned)n;
    cpu_write(c, c->s.r[31], v, n);
}
static uint64_t pop(CPU *c, int n) {
    uint64_t v=cpu_read(c, c->s.r[31], n, 1);
    c->s.r[31]+=(unsigned)n;
    return v;
}
static void flags(CPU *c, uint64_t r, int size, int overflow, int carry) {
    r&=mask(size);
    c->s.psr&=~(uint64_t)(N|Z|V|(carry>=0?C:0));
    if(!r)c->s.psr|=Z;
    if(r&(UINT64_C(1)<<(8*size-1)))c->s.psr|=N;
    if(overflow)c->s.psr|=V;
    if(carry>0)c->s.psr|=C;
}
static uint64_t arithmetic(CPU *c, int op, uint64_t a, uint64_t b, int size) {
    int bits=8*size, carry=-1, overflow=0;
    uint64_t m=mask(size), r=0;
    int64_t sa=sx(a, size), sb=sx(b, size);
    i128 low=-((i128)1<<(bits-1)), high=((i128)1<<(bits-1))-1, exact=0;
    switch(op) {
        case 1:
        case 10:
        {
            int extra=op==10&&!!(c->s.psr&C);
            u128 u=(u128)a+b+(unsigned)extra;
            r=(uint64_t)u;
            carry=(u>>bits)!=0;
            exact=(i128)sa+sb+extra;
            overflow=exact<low||exact>high;
            break;
        }
        case 2:
        case 3:
        case 11:
        {
            int extra=op==11&&!!(c->s.psr&C);
            r=b-a-(unsigned)extra;
            carry=(u128)b<(u128)a+(unsigned)extra;
            exact=(i128)sb-sa-extra;
            overflow=exact<low||exact>high;
            break;
        }
        case 4:
        r=b&a;
        break;
        case 5:
        r=b|a;
        break;
        case 6:
        r=b^a;
        break;
        case 7:
        r=b&~a;
        break;
        case 8:
        case 9:
        if(op==8)exact=(i128)sa*sb;
        else {
            if(!a)cpu_fault(c, 9, c->s.pc, 0, "division by zero");
            exact=(i128)sb/sa;
        }
        r=(uint64_t)exact;
        overflow=exact<low||exact>high;
        break;
        case 12:
        case 13:
        case 14:
        case 15:
        case 16:
        {
            uint64_t n=op>=15?a%(unsigned)bits:a;
            if(!n) {
                r=b;
                carry=!!(c->s.psr&C);
            }
            else if(op==12) {
                r=n>=(unsigned)bits?0:b<<n;
                carry=n>(unsigned)bits?0:(b>>(bits-n))&1;
            }
            else if(op==13||op==14) {
                r=op==14?(uint64_t)(sb>>(n>=(unsigned)bits?(unsigned)bits-1:n)):n>=(unsigned)bits?0:b>>n;
                carry=n>(unsigned)bits?(op==14&&sa<0):(b>>(n-1))&1;
                if(op==14&&n>(unsigned)bits)carry=sb<0;
            }
            else if(op==15) {
                r=((b<<n)|(b>>(bits-n)))&m;
                carry=r&1;
            }
            else {
                r=((b>>n)|(b<<(bits-n)))&m;
                carry=(r>>(bits-1))&1;
            }
            break;
        }
        case 28:
        r=(uint64_t)(sa<sb?sa:sb);
        break;
        case 29:
        r=(uint64_t)(sa>sb?sa:sb);
        break;
        default:
        cpu_fault(c, 1, c->s.pc, 0, "invalid arithmetic operation");
    }
    flags(c, r, size, overflow, carry);
    return r&m;
}
static int condition(CPU *c, int n) {
    int z=!!(c->s.psr&Z), v=!!(c->s.psr&V), sg=!!(c->s.psr&N), cy=!!(c->s.psr&C);
    switch(n) {
        case 0:
        return z;
        case 1:
        return !z;
        case 2:
        return sg!=v;
        case 3:
        return z||sg!=v;
        case 4:
        return !z&&sg==v;
        case 5:
        return sg==v;
        case 6:
        return cy;
        case 7:
        return cy||z;
        case 8:
        return !cy&&!z;
        case 9:
        return !cy;
        case 10:
        return sg;
        case 11:
        return !sg;
        case 12:
        return v;
        case 13:
        return !v;
        default:
        return 1;
    }
}
static uint64_t floatbits(double d, int n) {
    if(isnan(d))return n==8?UINT64_C(0x7ff8000000000000):n==4?UINT64_C(0x7fc00000):UINT64_C(0x7e00);
    if(n==8) {
        uint64_t v;
        memcpy(&v, &d, 8);
        return v;
    }
    if(n==4) {
        volatile float f=(float)d;
        float x=f;
        uint32_t v;
        memcpy(&v, &x, 4);
        return v;
    }
    _Float16 h=(_Float16)d;
    uint16_t v;
    memcpy(&v, &h, 2);
    return v;
}
static double bitsfloat(uint64_t v, int n) {
    if(n==8) {
        double d;
        memcpy(&d, &v, 8);
        return d;
    }
    if(n==4) {
        uint32_t u=(uint32_t)v;
        float f;
        memcpy(&f, &u, 4);
        return f;
    }
    uint16_t u=(uint16_t)v;
    _Float16 h;
    memcpy(&h, &u, 2);
    return (double)h;
}
typedef struct {
    int kind, size, fp;
    uint64_t at;
} Ref;
static Ref resolve(CPU *c, Operand o, int size, int fp, uint64_t end) {
    Ref r= {
        0, size, fp, (uint64_t)o.reg
    };
    if(o.mode==0)return r;
    if(o.mode==7&&!o.kind&&o.reg<28) {
        r.kind=2;
        if(fp) {
            double d;
            if(o.reg<24) {
                int old=fegetround();
                fesetround(FE_TONEAREST);
                r.at=floatbits(fp_constants[o.reg], size);
                fesetround(old);
                r.kind=3;
                return r;
            }
            if(o.reg!=26&&o.reg!=27)cpu_fault(c, 3, c->s.pc, 0, "invalid FP immediate");
            d=bitsfloat(o.value, o.reg==26?4:8);
            memcpy(&r.at, &d, 8);
            r.kind=4;
        }
        else r.at=o.reg<24?(uint64_t)o.reg:o.value;
        return r;
    }
    r.kind=1;
    if(o.mode==1||o.mode==2) {
        r.at=c->s.r[o.reg];
        if(o.mode==2)c->s.r[o.reg]+=(unsigned)size;
    }
    else if(o.mode==3) {
        c->s.r[o.reg]-=(unsigned)size;
        r.at=c->s.r[o.reg];
    }
    else if(o.mode==4||o.mode==5||o.kind) {
        r.at=(o.kind==4?c->s.sys[7]:c->s.r[o.reg])+o.value;
        if(o.kind==3)r.at=cpu_read(c, r.at, 8, 1);
    }
    else r.at=o.reg==30?end+o.value:o.value;
    if(o.index>=0)r.at+=c->s.r[o.index]*(unsigned)size;
    return r;
}
static uint64_t raw(CPU *c, Ref r) {
    if(r.kind==0)return (r.fp?c->s.f[r.at]:c->s.r[r.at])&mask(r.size);
    if(r.kind==1)return cpu_read(c, r.at, r.size, 1);
    if(r.kind==4) {
        double d;
        memcpy(&d, &r.at, 8);
        return floatbits(d, r.size);
    }
    return r.at&mask(r.size);
}
static void store(CPU *c, Ref r, uint64_t value) {
    value&=mask(r.size);
    if(r.kind==0) {
        uint64_t *regs=r.fp?c->s.f:c->s.r;
        regs[r.at]=value;
    }
    else if(r.kind==1)cpu_write(c, r.at, value, r.size);
    else cpu_fault(c, 3, c->s.pc, 0, "literal destination");
}
static void memory_ref(CPU *c, Ref r) {
    if(r.kind!=1)cpu_fault(c, 3, c->s.pc, 0, "memory address required");
}
static void fpexcept(CPU *c, int flags) {
    c->s.fpsr|=(unsigned)flags;
    if(c->s.fpsr&((uint64_t)(unsigned)flags<<8))cpu_fault(c, 10, c->s.pc, (unsigned)flags, "floating-point exception");
}
static int fpflags(void) {
    int f=fetestexcept(FE_ALL_EXCEPT);
    return (f&FE_INEXACT?1:0)|(f&FE_UNDERFLOW?2:0)|(f&FE_OVERFLOW?4:0)|(f&FE_DIVBYZERO?8:0)|(f&FE_INVALID?16:0);
}
static int rounding(CPU *c) {
    static const int modes[]= {
        FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD
    };
    return modes[(c->s.fpsr>>16)&3];
}
static void fstore(CPU *c, Ref r, double v) {
    uint64_t bits=floatbits(v, r.size);
    int flags=fpflags();
    if(flags)fpexcept(c, flags);
    store(c, r, bits);
}
static int snan(uint64_t bits, int size) {
    int fb=size==8?52:size==4?23:10, eb=size==8?11:size==4?8:5;
    return ((bits>>fb)&((1u<<eb)-1))==((1u<<eb)-1)&&(bits&((UINT64_C(1)<<fb)-1))&&!(bits&(UINT64_C(1)<<(fb-1)));
}
static void floating(CPU *c, Ins *in, Ref *r, double *x, uint64_t *raws) {
    int op=in->op->op, n=in->size;
    double a=x[0], b=x[1], result=0;
    int signal=0;
    for(int i=0;i<in->count;i++)if(i<in->count-1||op==5||op==6||op==7||op==11||op==12||op==13||op==14||op<5&&op>0)if(r[i].fp&&snan(raws[i], r[i].size))signal=1;
    fesetround(rounding(c));
    feclearexcept(FE_ALL_EXCEPT);
    if(op==15) {
        int neg=signbit(a), idx;
        if(isnan(a))idx=snan(raws[0], n)?8:9;
        else if(isinf(a))idx=neg?0:7;
        else if(!a)idx=neg?3:4;
        else if(fabs(a)<(n==2?0x1p-14:n==4?0x1p-126:0x1p-1022))idx=neg?2:5;
        else idx=neg?1:6;
        store(c, r[1], UINT64_C(1)<<idx);
        return;
    }
    if(signal)fpexcept(c, 16);
    if(op==5||op==6||op==7) {
        if(op==7) {
            b=a;
            a=0;
        }
        if(op==6&&(isnan(a)||isnan(b)))fpexcept(c, 16);
        c->s.psr=(c->s.psr&~UINT64_C(15))|(isnan(a)||isnan(b)?V:b<a?N|C:b==a?Z:0);
        return;
    }
    if(op>=20&&op<=23)op-=19;
    if((op<=4&&isfinite(a)&&isfinite(b)&&!(op==4&&a==0))||(op==10&&isfinite(a)&&a>=0)||(op>=24&&op<=27&&isfinite(x[0])&&isfinite(x[1])&&isfinite(x[2]))) {
        int raised=0;
        uint64_t encoded=fp_exact(op, a, b, x[2], n, (c->s.fpsr>>16)&3, &raised);
        if(raised)fpexcept(c, raised);
        store(c, r[in->count-1], encoded);
        fesetround(FE_TONEAREST);
        return;
    }
    if(op>=24&&op<=27) {
        double aa=x[0], bb=x[1], cc=x[2];
        if(op==25||op==27)cc=-cc;
        if(op==26||op==27)aa=-aa;
        result=n==4?(double)fmaf((float)aa, (float)bb, (float)cc):fma(aa, bb, cc);
    }
    else switch(op) {
        case 0:
        result=a;
        break;
        case 1:
        if(n==4) {
            volatile float v=(float)b+(float)a;
            result=v;
        }
        else {
            volatile double v=b+a;
            result=v;
        }
        break;
        case 2:
        if(n==4) {
            volatile float v=(float)b-(float)a;
            result=v;
        }
        else {
            volatile double v=b-a;
            result=v;
        }
        break;
        case 3:
        if(n==4) {
            volatile float v=(float)b*(float)a;
            result=v;
        }
        else {
            volatile double v=b*a;
            result=v;
        }
        break;
        case 4:
        if(n==4) {
            volatile float v=(float)b/(float)a;
            result=v;
        }
        else {
            volatile double v=b/a;
            result=v;
        }
        break;
        case 8:
        result=-a;
        break;
        case 9:
        result=fabs(a);
        break;
        case 10:
        result=n==4?(double)sqrtf((float)a):sqrt(a);
        break;
        case 11:
        case 12:
        if(isnan(a)||isnan(b))result=NAN;
        else if(a==0&&b==0)result=copysign(0., op==11?(signbit(a)||signbit(b)?-1.:1.):(signbit(a)&&signbit(b)?-1.:1.));
        else result=op==11?fmin(a, b):fmax(a, b);
        break;
        case 13:
        result=copysign(b, a);
        break;
        case 14:
        result=remainder(b, a);
        break;
        case 16:
        result=rint(a);
        break;
        case 17:
        result=floor(a);
        break;
        case 18:
        result=ceil(a);
        break;
        case 19:
        result=trunc(a);
        break;
        default:
        cpu_fault(c, 1, in->start, 0, "invalid FP operation");
    }
    if(op>=17&&op<=19&&isfinite(a)&&result!=a)fpexcept(c, 1);
    fstore(c, r[in->count-1], result);
    fesetround(FE_TONEAREST);
}
static void convert(CPU *c, Ins *in, Ref *r, uint64_t *v, double *x) {
    int op=in->op->op, size=in->size, width=in->aux;
    if(op==8||op==9) {
        store(c, r[1], v[0]);
        return;
    }
    if(op<2) {
        uint64_t result=op==1?(uint64_t)sx(v[0], size):v[0];
        store(c, r[1], result);
        flags(c, result, width, 0, -1);
        return;
    }
    fesetround(rounding(c));
    feclearexcept(FE_ALL_EXCEPT);
    if(op==2||op==3) {
        int raised=0;
        uint64_t encoded=fp_integer(op==2?(uint64_t)sx(v[0], size):v[0], op==2, width, (c->s.fpsr>>16)&3, &raised);
        if(raised)fpexcept(c, raised);
        store(c, r[1], encoded);
    }
    else if(op==7) {
        if(snan(v[0], size))fpexcept(c, 16);
        if(isfinite(x[0])) {
            int raised=0;
            uint64_t encoded=fp_exact(0, x[0], 0, 0, width, (c->s.fpsr>>16)&3, &raised);
            if(raised)fpexcept(c, raised);
            store(c, r[1], encoded);
        }
        else fstore(c, r[1], x[0]);
    }
    else {
        long double d=x[0], lo=op==5?0:-ldexpl(1, width*8-1), hi=op==5?ldexpl(1, width*8):ldexpl(1, width*8-1);
        long double q=op==6?rintl(d):truncl(d);
        uint64_t result;
        if(isnan((double)d)) {
            fpexcept(c, 16);
            result=0;
        }
        else if(!isfinite((double)d)||q<lo||q>=hi) {
            fpexcept(c, 16);
            result=d>0?(op==5?mask(width):(mask(width)>>1)):op==5?0:(UINT64_C(1)<<(width*8-1));
        }
        else {
            if(q!=d)fpexcept(c, 1);
            if(op==5) {
                if(q>=0x1p63L)result=(uint64_t)(q-0x1p63L)+(UINT64_C(1)<<63);
                else result=(uint64_t)q;
            }
            else result=(uint64_t)(int64_t)q;
        }
        store(c, r[1], result);
        flags(c, result, width, 0, -1);
    }
    fesetround(FE_TONEAREST);
}
static u128 field_read(CPU *c, Ref r, int64_t pos, int width) {
    if(width<1||width>64)cpu_fault(c, 3, c->s.pc, 0, "bitfield width must be 1..64");
    if(r.kind==0) {
        unsigned p=(uint64_t)pos&63;
        if(p+(unsigned)width>64)cpu_fault(c, 3, c->s.pc, 0, "register bitfield exceeds register");
        return (c->s.r[r.at]>>p)&mask(width==64?8:8)&(((u128)1<<width)-1);
    }
    memory_ref(c, r);
    int64_t byte=pos/8, bit=pos%8;
    if(bit<0) {
        bit+=8;
        byte--;
    }
    int n=((int)bit+width+7)/8;
    u128 value=0;
    for(int i=0;i<n;i++)value|=(u128)cpu_read(c, r.at+(uint64_t)byte+(unsigned)i, 1, 1)<<(8*i);
    return (value>>(unsigned)bit)&(((u128)1<<width)-1);
}
static void field_write(CPU *c, Ref r, int64_t pos, int width, uint64_t value) {
    (void)field_read(c, r, pos, width);
    u128 m=((u128)1<<width)-1;
    if(r.kind==0) {
        unsigned p=(uint64_t)pos&63;
        uint64_t mm=(uint64_t)(m<<p);
        c->s.r[r.at]=(c->s.r[r.at]&~mm)|((value<<p)&mm);
        return;
    }
    int64_t byte=pos/8, bit=pos%8;
    if(bit<0) {
        bit+=8;
        byte--;
    }
    int n=((int)bit+width+7)/8;
    u128 old=0;
    for(int i=0;i<n;i++)old|=(u128)cpu_read(c, r.at+(uint64_t)byte+(unsigned)i, 1, 1)<<(8*i);
    old=(old&~(m<<(unsigned)bit))|(((u128)value&m)<<(unsigned)bit);
    for(int i=0;i<n;i++)cpu_write(c, r.at+(uint64_t)byte+(unsigned)i, (uint64_t)(old>>(8*i)), 1);
}
static int no_read(const Op *o, int i) {
    int f=o->family, n=o->op;
    if(f==F_TWO)return ((n==0||n==17||n==18||n==27)&&i==1)||(n==20)||(n==22&&i==0)||(n==26&&i==0);
    if(f==F_THREE)return i==2;
    if(f==F_CONV)return i==1;
    if(f==F_FP) {
        if(n>=24)return i==3;
        if(n>=20)return i==2;
        return i==1&&(n==0||n==8||n==9||n==10||n==15||n>=16);
    }
    if(f==F_SET||f==F_CMOV)return (f==F_SET||i==1);
    if(f==F_FPSR)return n==112;
    if(f==F_BITS)return n==8||n==9?i>=2:n<8?i==1:0;
    if(f==F_ATOMIC)return n==0||n==6?i==2:i==1;
    if(f==F_FIELD)return n<2?i>=2:n==2?i==3:i==2;
    if(f==F_BITBR)return i==1;
    if(f==F_SYSTEM)return n==0?i==1:n==2?i==1:n>=3&&n<=5?i==0:0;
    if(f==F_SHORT)return (n==8||n==9||n==14)&&i==0;
    return 0;
}
static void trap(CPU *c, int vector, uint64_t addr, uint64_t info, uint64_t pc) {
    c->delivering=vector+1;
    uint64_t old=c->s.psr;
    unsigned oldmode=(old>>12)&3;
    mode(c, (old&~(UINT64_C(0xf000)|UA|TF))|((uint64_t)oldmode<<14));
    if(vector==19||vector==20||vector==14) {
        if(c->s.emergency_active)cpu_fault(c,19,pc,vector,"emergency stack already active");
        c->s.emergency_sp=c->s.r[31];
        c->s.r[31]=c->s.sys[8];
        c->s.emergency_active=1;
        c->s.nmi_active=vector==20;
    }
    push(c, pc, 8);
    push(c, old, 8);
    if(vector==19||vector==20||vector==14)c->s.emergency_frame=c->s.r[31];
    c->s.sys[21]=(unsigned)vector;
    c->s.sys[22]=addr;
    c->s.sys[23]=info;
    uint64_t handler=cpu_read(c, c->s.sys[16]+(unsigned)vector*8, 8, 1);
    c->s.pc=handler;
    c->s.waiting=0;
    c->s.halted=0;
    c->delivering=0;
}
static void execute(CPU *c, Ins *in) {
    const Op *o=in->op;
    int f=o->family, n=o->op, size=in->size;
    Ref r[4]= {
        {
            0
        }
    };
    uint64_t v[4]= {
        0
    };
    uint64_t pair_high[2]={0};
    double x[4]= {
        0
    };
    if(f==F_HINT)return; /* Hints never evaluate their address operands. */
    if(f==F_SYSTEM&&(n==0||n==1)) {
        Operand idop=in->operands[n==0?0:1];
        if(idop.mode!=7||idop.kind||idop.reg>=28)cpu_fault(c,3,in->start,0,"system number must be immediate");
        uint64_t id=idop.reg<24?(unsigned)idop.reg:idop.value;
        if(id>=120||!system_name((int)id))cpu_fault(c,1,in->start,0,"reserved system register");
        privileged(c,id<=6?1:0);
    }
    if(f==F_SYSTEM) {
        if(n==8||n==9)privileged(c,1);
        if(n==2||n==6)privileged(c, 0);
        if(n>=3&&n<=5)privileged(c, 1);
    }
    if(f==F_SHORT&&(n==3||n==7))privileged(c, 0);
    if((c->s.psr&FPD)&&(f==F_FP||f==F_FPSR||(f==F_CONV&&n>=2)))cpu_fault(c,15,in->start,0,"floating point unavailable");
    for(int i=0;i<in->count;i++) {
        r[i]=resolve(c, in->operands[i], operand_size(o, size, in->aux, i), fp_operand(o, i), in->end);
        if(f==F_ATOMIC&&n==6&&i<2) {
            if(r[i].kind||r[i].fp||(r[i].at&1))cpu_fault(c,3,in->start,0,"CAS2 requires even register pairs");
            pair_high[i]=c->s.r[r[i].at+1];
        }
        if(!no_read(o, i)) {
            v[i]=raw(c, r[i]);
            if(r[i].fp) {
                if(r[i].kind==4)memcpy(&x[i], &r[i].at, 8);
                else x[i]=bitsfloat(v[i],r[i].size);
            }
        }
    }
    if(f==F_SET||f==F_CMOV) {
        Ref dst=r[f==F_SET?0:1];
        if(dst.kind>1)cpu_fault(c,3,in->start,0,"literal destination");
        if(f==F_SET)store(c,dst,condition(c,n)?1:0);
        else if(condition(c,n))store(c,dst,v[0]);
        return;
    }
    if(f==F_BRANCH) {
        if(condition(c, n)) {
            if(n==15)push(c, in->end, 8);
            c->s.pc=in->target;
        }
        return;
    }
    if(f==F_TWO) {
        uint64_t result=0;
        switch(n) {
            case 0:
            store(c, r[1], v[0]);
            flags(c, v[0], size, 0, -1);
            return;
            case 17:
            result=arithmetic(c, 2, v[0], 0, size);
            store(c, r[1], result);
            return;
            case 18:
            result=~v[0];
            flags(c, result, size, 0, -1);
            store(c, r[1], result);
            return;
            case 19:
            flags(c, v[0], size, 0, -1);
            return;
            case 20:
            memory_ref(c, r[0]);
            store(c, r[1], r[0].at);
            return;
            case 21:
            push(c, v[0], size);
            return;
            case 22:
            store(c, r[0], pop(c, size));
            return;
            case 23:
            store(c, r[0], v[1]);
            store(c, r[1], v[0]);
            return;
            case 24:
            case 25:
            result=arithmetic(c, n==24?1:2, 1, v[0], size);
            store(c, r[0], result);
            return;
            case 26:
            store(c, r[0], 0);
            flags(c, 0, size, 0, -1);
            return;
            case 27:
            {
                i128 z=sx(v[0], size);
                if(z<0)z=-z;
                flags(c, (uint64_t)z, size, z>((i128)1<<(size*8-1))-1, -1);
                store(c, r[1], (uint64_t)z);
                return;
            }
            default:
            result=arithmetic(c, n, v[0], v[1], size);
            if(n!=3)store(c, r[1], result);
            return;
        }
    }
    if(f==F_THREE) {
        static const int map[]= {
            1, 2, 8, 9, 4, 5, 6, 7, 12, 13, 14, 15, 16, 10, 11
        };
        store(c, r[2], arithmetic(c, map[n], v[0], v[1], size));
        return;
    }
    if(f==F_SHORT) {
        switch(n) {
            case 0:
            case 4:
            case 5:
            return;
            case 1:
            c->s.pc=pop(c, 8);
            return;
            case 2:
            {
                c->s.r[31]=c->s.r[30];
                uint64_t oldfp=pop(c, 8), m=pop(c, 8), saved[16]= {
                    0
                };
                if(m>65535)cpu_fault(c, 3, in->start, 0, "invalid saved mask");
                for(int i=0;i<16;i++)if(m&(1u<<i))saved[i]=pop(c, 8);
                uint64_t pc=pop(c, 8), sp=c->s.r[31];
                for(int i=0;i<16;i++)if(m&(1u<<i))c->s.r[16+i]=saved[i];
                c->s.r[30]=oldfp;
                c->s.r[31]=sp;
                c->s.pc=pc;
                return;
            }
            case 3:
            {
                uint64_t frame=c->s.r[31];
                uint64_t psr=pop(c, 8), pc=pop(c, 8);
                if(c->s.emergency_active&&frame==c->s.emergency_frame) {
                    c->s.r[31]=c->s.emergency_sp;
                    c->s.emergency_active=c->s.nmi_active=0;
                }
                mode(c, psr);
                c->s.pc=pc;
                return;
            }
            case 6:
            case 13:
            if(!c->traps)cpu_fault(c, n==6?11:12, in->start, n==13?v[0]:0, n==6?"breakpoint":"system call");
            trap(c, n==6?11:12, 0, n==13?v[0]:0, in->end);
            return;
            case 14:
            store(c,r[0],c->s.sys[7]);return;
            case 7:
            c->s.waiting=1;
            return;
            case 8:
            case 9:
            memory_ref(c, r[0]);
            if(n==9)push(c, in->end, 8);
            c->s.pc=r[0].at;
            return;
            case 10:
            case 11:
            case 12:
            {
                uint64_t m=v[0], saved[32];
                memcpy(saved, c->s.r, sizeof saved);
                if(m>65535)cpu_fault(c, 3, in->start, 0, "register mask exceeds 16 bits");
                if(n!=12) {
                    for(int i=31;i>=16;i--)if(m&(1u<<(i-16)))push(c, saved[i], 8);
                    if(n==10) {
                        push(c, m, 8);
                        push(c, saved[30], 8);
                        c->s.r[30]=c->s.r[31];
                        c->s.r[31]-=v[1];
                    }
                }
                else {
                    for(int i=16;i<32;i++)if(m&(1u<<(i-16)))saved[i]=pop(c, 8);
                    for(int i=16;i<32;i++)if(m&(1u<<(i-16)))c->s.r[i]=saved[i];
                }
                return;
            }
        }
    }
    if(f==F_FP) {
        floating(c, in, r, x, v);
        return;
    }
    if(f==F_FPSR) {
        if(n==112)store(c, r[0], c->s.fpsr);
        else c->s.fpsr=v[0]&0x31f1f;
        return;
    }
    if(f==F_CONV) {
        convert(c, in, r, v, x);
        return;
    }
    if(f==F_SYSTEM) {
        if(n==7)return;
        if(n==8||n==9){if(n==8)c->s.psr|=UA;else c->s.psr&=~UA;return;}
        if(n==6) {
            c->s.halted=1;
            return;
        }
        if(n==2) {
            if(v[0]>3)cpu_fault(c, 3, in->start, 0, "invalid TLBI kind");
            if(v[0]<2) {
                if(r[1].kind!=2||r[1].at)cpu_fault(c, 3, in->start, 0, "TLBI kind requires #0");
            }
            else memory_ref(c, r[1]);
            return;
        }
        if(n>=3) {
            memory_ref(c, r[0]);
            return;
        }
        unsigned id=(unsigned)(n==0?v[0]:v[1]);
        if((n==0?v[0]:v[1])>119||!system_name((int)id)||r[n==0?0:1].kind!=2)cpu_fault(c, 3, in->start, 0, "invalid system register");
        privileged(c, id<=6?1:0);
        if(!n) {
            uint64_t value=id==27?c->s.psr:id==24+((c->s.psr>>12)&3)?c->s.r[31]:c->s.sys[id];
            store(c, r[1], value);
        }
        else {
            if(id<2||id==5||id==6)cpu_fault(c, 3, in->start, 0, "read-only system register");
            uint64_t value=v[0];
            if(id==19)value&=65535;
            if(id==20)value&=1;
            if(id==2)value&=1;
            if(id==40)value&=1;
            if(id>=96&&id<112)value&=0x33e;
            if(id>=116)value&=0x1ff;
            if(id>=112) {
                unsigned i=(id-112)%4;
                uint64_t ctl=id>=116?value:c->s.sys[116+i];
                uint64_t addr=id<116?value:c->s.sys[112+i];
                if((ctl&1)&&(addr&((UINT64_C(1)<<((ctl>>1)&3))-1)))cpu_fault(c,3,in->start,0,"unaligned watchpoint");
            }
            if(id==29||id==30||id==31)value=0; /* No ECC extension. */
            if(id==27)mode(c, value);
            else c->s.sys[id]=value;
            if(id==24+((c->s.psr>>12)&3))c->s.r[31]=value;
        }
        return;
    }
    if(f==F_BITS) {
        if(n<8) {
            uint64_t a=v[0], result=0;
            int bits=size*8;
            if(n==0)result=a?(uint64_t)(__builtin_clzll(a)-(64-bits)):(unsigned)bits;
            else if(n==1)result=a?(uint64_t)__builtin_ctzll(a):(unsigned)bits;
            else if(n==2)result=(unsigned)__builtin_popcountll(a);
            else if(n==3)for(int i=0;i<size;i++)result=(result<<8)|((a>>(8*i))&255);
            else for(int i=0;i<bits;i++)result=(result<<1)|((a>>i)&1);
            store(c, r[1], result);
            flags(c, result, size, 0, -1);
            return;
        }
        int bits=size*8;
        if(n==8||n==9) {
            u128 product=n==8?(u128)((i128)sx(v[0], size)*sx(v[1], size)):(u128)v[0]*v[1];
            store(c, r[2], (uint64_t)product);
            store(c, r[3], (uint64_t)(product>>bits));
            c->s.psr=(c->s.psr&~UINT64_C(15))|(!product?Z:0)|(product&((u128)1<<(2*bits-1))?N:0);
            return;
        }
        if(r[1].kind!=0||r[1].at>30)cpu_fault(c, 3, in->start, 0, "wide divide requires R0..R30");
        unsigned reg=(unsigned)r[1].at;
        u128 dividend=((u128)(c->s.r[reg+1]&mask(size))<<bits)|v[1], quotient, rem;
        if(!v[0])cpu_fault(c, 9, in->start, 0, "wide division by zero");
        if(n==10) {
            i128 d=(i128)dividend;
            if(bits<64&&(dividend&((u128)1<<(2*bits-1))))d=(i128)(dividend-((u128)1<<(2*bits)));
            int64_t divisor=sx(v[0], size);
            if(!divisor)cpu_fault(c, 9, in->start, 0, "wide division by zero");
            if(bits==64&&dividend==((u128)1<<127)&&divisor==-1)cpu_fault(c, 9, in->start, 0, "wide quotient overflow");
            i128 q=d/divisor, rr=d%divisor;
            if(q<-((i128)1<<(bits-1))||q>((i128)1<<(bits-1))-1)cpu_fault(c, 9, in->start, 0, "wide quotient overflow");
            quotient=(u128)q;
            rem=(u128)rr;
        }
        else {
            quotient=dividend/v[0];
            rem=dividend%v[0];
            if(quotient>mask(size))cpu_fault(c, 9, in->start, 0, "wide quotient overflow");
        }
        store(c, r[1], (uint64_t)quotient);
        store(c, (Ref) {
            0, size, 0, reg+1
        }, (uint64_t)rem);
        return;
    }
    if(f==F_ATOMIC) {
        Ref address=r[in->count-1];
        memory_ref(c, address);
        if(n==6) {
            for(int i=0;i<2;i++)if(r[i].kind||r[i].fp||(r[i].at&1))cpu_fault(c,3,in->start,0,"CAS2 requires even register pairs");
            if(address.at&15)cpu_fault(c,3,address.at,0,"unaligned CAS2");
            uint64_t lo=cpu_read(c,address.at,8,1),hi=cpu_read(c,address.at+8,8,1);
            /* Check write permissions even on a failed comparison. */
            uint64_t phys[8];access_map(c,address.at,8,2,phys);access_map(c,address.at+8,8,2,phys);
            if(lo==v[0]&&hi==pair_high[0]) {
                cpu_write(c,address.at,v[1],8);cpu_write(c,address.at+8,pair_high[1],8);c->s.psr|=Z;
            } else {c->s.r[r[0].at]=lo;c->s.r[r[0].at+1]=hi;c->s.psr&=~(uint64_t)Z;}
            return;
        }
        if(address.at%(unsigned)size)cpu_fault(c, 3, address.at, 0, "unaligned atomic");
        uint64_t old=cpu_read(c, address.at, size, 1);
        if(n==0) {
            if(old==v[0]) {
                store(c, address, v[1]);
                c->s.psr|=Z;
            }
            else {
                store(c, r[0], old);
                c->s.psr&=~UINT64_C(4);
            }
        }
        else {
            uint64_t result=n==1?arithmetic(c, 1, v[0], old, size):n==2?v[0]:n==3?old&v[0]:n==4?old|v[0]:old^v[0];
            store(c, address, result);
            store(c, r[0], old);
        }
        return;
    }
    if(f==F_FIELD) {
        uint64_t width=n==2?v[2]:v[1];
        if(!width||width>64)cpu_fault(c, 3, in->start, 0, "bitfield width must be 1..64");
        if(n==2) {
            field_write(c, r[3], (int64_t)v[1], (int)v[2], v[0]);
            return;
        }
        uint64_t val=(uint64_t)field_read(c, r[2], (int64_t)v[0], (int)v[1]);
        if((n==0||n==3)&&v[1]<64&&(val&(UINT64_C(1)<<(v[1]-1))))val|=~((UINT64_C(1)<<v[1])-1);
        if(n==3)arithmetic(c, 3, val, v[3], 8);
        else {
            store(c, r[3], val);
            flags(c, val, 8, 0, -1);
        }
        return;
    }
    if(f==F_BITBR) {
        Ref base=r[1];
        int64_t pos=(int64_t)v[0];
        if(n>=4) {
            memory_ref(c, base);
            int64_t byte=pos/8, bit=pos%8;
            if(bit<0) {
                bit+=8;
                byte--;
            }
            uint64_t addr=base.at+(uint64_t)byte;
            base.at=addr&~UINT64_C(7);
            pos=(int64_t)((addr&7)*8)+(int64_t)bit;
            (void)cpu_read(c, base.at, 8, 1);
        }
        int bit=(int)field_read(c, base, pos, 1);
        if(n>=2) {
            if(n>=4) {
                uint64_t value=cpu_read(c, base.at, 8, 1), m=UINT64_C(1)<<(unsigned)pos;
                cpu_write(c, base.at, n==4?value|m:value&~m, 8);
            }
            else field_write(c, base, pos, 1, n==2);
        }
        if(bit==(n==0||n==2||n==4))c->s.pc=in->target;
        return;
    }
    if(f==F_LOOP) {
        uint64_t result;
        int take;
        if(n<=2) {
            if(r[0].kind!=0)cpu_fault(c, 3, in->start, 0, "loop counter requires register");
            result=arithmetic(c, 2, 1, v[0], 8);
            store(c, r[0], result);
            take=n==0?result!=0:n==1?(int64_t)result>=0:(int64_t)result>0;
        }
        else {
            result=arithmetic(c, 1, n==5?v[1]:1, v[in->count-1], 8);
            store(c, r[in->count-1], result);
            arithmetic(c, 3, v[0], result, 8);
            take=n==3?(int64_t)result<(int64_t)v[0]:(int64_t)result<=(int64_t)v[0];
            if(n==5&&(int64_t)v[1]<0)take=(int64_t)result>=(int64_t)v[0];
        }
        if(take)c->s.pc=in->target;
        return;
    }
    if(f==F_CASE) {
        if(r[2].kind!=2)cpu_fault(c, 3, in->start, 0, "CASE limit must be literal");
        uint64_t idx=v[0]-v[1];
        c->s.pc=idx<=v[2]?in->end+(int64_t)(int32_t)cpu_read(c, in->end+idx*4, 4, 1):in->end+(v[2]+1)*4;
        return;
    }
    if(f==F_BLOCK) {
        uint64_t count=c->s.r[0], src=c->s.r[1], dst=c->s.r[2];
        if(!count) {
            if(n>=2)c->s.psr=(c->s.psr&~UINT64_C(15))|(n==2?Z:0);
            return;
        }
        if(n==0) {
            Block *b=NULL, *freeb=NULL;
            for(int i=0;i<16;i++) {
                if(c->blocks[i].active&&c->blocks[i].pc==in->start)b=&c->blocks[i];
                if(!c->blocks[i].active&&!freeb)freeb=&c->blocks[i];
            }
            if(b&&(b->count!=count||b->src!=src||b->dst!=dst)) {
                b->active=0;
                freeb=b;
                b=NULL;
            }
            if(!b&&src<dst&&(u128)dst<(u128)src+(u128)count*size) {
                if(!freeb)cpu_fault(c, 14, in->start, 0, "too many suspended MOVS instructions");
                b=freeb;
                *b=(Block) {
                    1, in->start, count, src, dst, src+count*size, dst+count*size
                };
            }
            if(b) {
                uint64_t offset=(count-1)*size;
                cpu_write(c, dst+offset, cpu_read(c, src+offset, size, 1), size);
                b->count--;
                if(count==1) {
                    c->s.r[1]=b->end_src;
                    c->s.r[2]=b->end_dst;
                    b->active=0;
                }
            }
            else {
                cpu_write(c, dst, cpu_read(c, src, size, 1), size);
                c->s.r[1]+=size;
                c->s.r[2]+=size;
            }
        }
        else if(n==1) {
            cpu_write(c, dst, v[0], size);
            c->s.r[2]+=size;
        }
        else if(n==2) {
            uint64_t a=cpu_read(c, src, size, 1), b=cpu_read(c, dst, size, 1);
            arithmetic(c, 3, a, b, size);
            if(a!=b)return;
            c->s.r[1]+=size;
            c->s.r[2]+=size;
        }
        else {
            uint64_t a=cpu_read(c, src, size, 1);
            int found=n==4?a!=v[0]:a==v[0];
            c->s.psr=(c->s.psr&~UINT64_C(15))|(found?Z:0);
            if(found)return;
            c->s.r[1]+=size;
        }
        c->s.r[0]--;
        if(c->s.r[0])c->s.pc=in->start;
        return;
    }
    cpu_fault(c, 1, in->start, 0, "unimplemented instruction");
}
int cpu_interrupt(CPU *c, int vector, int level) {
    if(!((vector==20&&level==7)||(vector==64&&level==6)||(vector==65&&level==5)||(vector==66&&level>=1&&level<=7))||c->irq_count==64)return 0;
    c->irq_vector[c->irq_count]=vector;
    c->irq_level[c->irq_count++]=level;
    return 1;
}
static void discard(CPU *c) {
    c->transaction=0;c->nwrites=c->ntx=0;c->rx_reads=0;c->uart_dirty=0;c->tape_dirty=0;for(unsigned i=0;i<DISK_COUNT;i++)c->disks[i].dirty=0;c->delivering=0;
}
/* Delivery is a separate transaction: a trace must not undo its instruction. */
static int deliver(CPU *c, int vector, uint64_t addr, uint64_t info, uint64_t pc, int level) {
    State before=c->s;
    c->transaction=1;
    if(setjmp(c->fault)) {
        c->s=before;discard(c);
        return 0;
    }
    trap(c,vector,addr,info,pc);
    if(level>=0)c->s.psr=(c->s.psr&~UINT64_C(0x700))|((unsigned)level<<8);
    commit(c);
    return 1;
}
static int deliver_checked(CPU *c,int vector,uint64_t addr,uint64_t info,uint64_t pc,int level) {
    if(deliver(c,vector,addr,info,pc,level))return 1;
    uint64_t failed=c->faultaddr;
    if(vector!=19&&deliver(c,19,failed,(unsigned)vector,pc,-1))return 1;
    c->vector=19;
    snprintf(c->error,sizeof c->error,"double fault: emergency trap delivery failed");
    return 0;
}
static int pending_irq(CPU *c) {
    int pending=-1;
    for(int i=0;i<c->irq_count;i++) {
        int nmi=c->irq_vector[i]==20;
        if(nmi&&c->s.nmi_active)continue;
        if(!nmi&&c->irq_level[i]<=(int)((c->s.psr>>8)&7))continue;
        if(pending<0||nmi||(c->irq_vector[pending]!=20&&c->irq_level[i]>c->irq_level[pending]))pending=i;
    }
    return pending;
}
int cpu_runnable(CPU *c) {
    return !c->s.halted&&(!c->s.waiting||pending_irq(c)>=0||(((c->s.psr>>8)&7)<6&&c->s.sys[28]!=UINT64_MAX));
}
int cpu_step(CPU *c, Ins *decoded) {
    if(decoded)memset(decoded,0,sizeof *decoded);
    if(c->s.halted)return 1;
    if(c->s.waiting&&pending_irq(c)<0&&((c->s.psr>>8)&7)<6&&c->s.sys[28]!=UINT64_MAX&&c->s.sys[1]<c->s.sys[28])
        c->s.sys[1]=c->s.sys[28]; /* deterministic idle-time advance */
    int pending=pending_irq(c);
    uint64_t start=c->s.pc;
    if(c->s.sys[28]!=UINT64_MAX&&c->s.sys[1]>=c->s.sys[28]&&((c->s.psr>>8)&7)<6&&
       (pending<0||(c->irq_vector[pending]!=20&&c->irq_level[pending]<6)))
        return deliver_checked(c,64,0,0,start,6);
    if(pending>=0) {
        int vector=c->irq_vector[pending],level=c->irq_level[pending];
        if(!deliver_checked(c,vector,0,0,start,vector==20?-1:level))return 0;
        for(int i=pending+1;i<c->irq_count;i++){c->irq_level[i-1]=c->irq_level[i];c->irq_vector[i-1]=c->irq_vector[i];}
        c->irq_count--;
        return 1;
    }
    if(c->s.waiting)return 1;
    State backup=c->s;
    Block blocks[16];memcpy(blocks,c->blocks,sizeof blocks);
    c->transaction=1;
    if(setjmp(c->fault)) {
        int delivery=c->delivering;
        c->s=backup;memcpy(c->blocks,blocks,sizeof blocks);discard(c);
        if(!c->traps)return 0;
        return deliver_checked(c,delivery?19:c->vector,c->faultaddr,delivery?(unsigned)(delivery-1):c->faultinfo,start,-1);
    }
    Ins in;cpu_decode(c,start,&in);
    c->s.pc=in.end;execute(c,&in);
    commit(c);
    c->steps++;c->s.sys[1]++;
    if(c->s.sys[2]&1)c->s.sys[3]++;
    if(decoded)*decoded=in;
    int software_trap=in.op->family==F_SHORT&&(in.op->op==6||in.op->op==13);
    int incomplete_block=in.op->family==F_BLOCK&&c->s.pc==in.start;
    if((backup.psr&TF)&&!software_trap&&!incomplete_block&&in.op->family!=F_HINT) {
        if(!c->traps){c->vector=17;c->faultaddr=0;c->faultinfo=0;snprintf(c->error,sizeof c->error,"single step");return 0;}
        return deliver_checked(c,17,0,0,c->s.pc,-1);
    }
    return 1;
}
