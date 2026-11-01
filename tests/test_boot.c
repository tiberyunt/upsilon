/* ELF loader validation and transactional NS16550 coverage. */
#include "upsilon.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static void put(uint8_t *p,uint64_t v,int n){for(int i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static void elf(uint8_t *b){
 memset(b,0,256);memcpy(b,"\177ELF\2\1\1",7);put(b+16,2,2);put(b+18,0xf5a0,2);put(b+20,1,4);
 put(b+24,0x200000,8);put(b+32,64,8);put(b+52,64,2);put(b+54,56,2);put(b+56,1,2);
 put(b+64,1,4);put(b+68,5,4);put(b+72,240,8);put(b+80,0x200000,8);put(b+88,0x200000,8);put(b+96,2,8);put(b+104,16,8);put(b+112,1,8);
 b[240]=0xf6;b[241]=6;
}
static void loader(void){
 CPU *c=calloc(1,sizeof *c);assert(c&&cpu_init(c,16*1024*1024));uint8_t b[256];char error[256];elf(b);
 memset(c->ram+0x200000,0xa5,16);
 assert(platform_load_elf(c,b,sizeof b,"-s",error,sizeof error));
 assert(c->s.pc==0x200000&&c->s.r[0]==16*1024*1024-65536&&c->serial&&c->traps);
 assert(!memcmp(c->ram+c->s.r[0],"\xd0\x0d\xfe\xed",4));
 for(unsigned i=2;i<16;i++)assert(c->ram[0x200000+i]==0);
 uint8_t *before=malloc(c->ram_size);assert(before);memcpy(before,c->ram,c->ram_size);State regs=c->s;
 /* A valid first segment followed by an invalid one must not partly load. */
 put(b+56,2,2);memcpy(b+120,b+64,56);put(b+144,c->ram_size,8);b[240]=0;
 assert(!platform_load_elf(c,b,sizeof b,"",error,sizeof error));
 assert(!memcmp(before,c->ram,c->ram_size)&&!memcmp(&regs,&c->s,sizeof regs));
 elf(b);put(b+104,1,8);assert(!platform_load_elf(c,b,sizeof b,"",error,sizeof error));
 elf(b);put(b+24,0x200008,8);assert(!platform_load_elf(c,b,sizeof b,"",error,sizeof error));
 elf(b);b[5]=2;assert(!platform_load_elf(c,b,sizeof b,"",error,sizeof error));
 free(before);cpu_destroy(c);free(c);
}
static CPU *machine(const char *source){
 CPU *c=calloc(1,sizeof *c);Program p;assert(c&&cpu_init(c,65536));assert(assemble(source,4096,0,&p));
 memcpy(c->ram+4096,p.data,p.length);c->s.pc=4096;c->serial=1;free_program(&p);return c;
}
static void uart(void){
 CPU *c=machine("MOV.b #128,@0xffff0403\nMOV.b #52,@0xffff0400\nMOV.b #18,@0xffff0401\nMOV.b #3,@0xffff0403\nMOV.b #88,@0xffff0400\nMOV.b #165,@0xffff0407\nXCHG.b @0xffff0407,#1");
 for(int i=0;i<6;i++)assert(cpu_step(c,NULL));
 assert(c->uart.dll==52&&c->uart.dlm==18&&c->uart.lcr==3&&c->uart.scr==165);
 assert(c->tx_len==1&&c->tx[0]=='X');
 assert(!cpu_step(c,NULL)&&c->uart.scr==165);cpu_destroy(c);free(c);
 c=machine("MOV.b @0xffff0400,#1");cpu_feed(c,(const uint8_t *)"Z",1);
 assert(!cpu_step(c,NULL)&&c->rx_pos==0);cpu_destroy(c);free(c);
 c=machine("MOV.w @0xffff0400,R0");cpu_feed(c,(const uint8_t *)"Z",1);
 assert(!cpu_step(c,NULL)&&c->rx_pos==0&&c->vector==8);cpu_destroy(c);free(c);
 c=machine("MOV.b #2,@0xffff0402\nMOV.b @0xffff0405,R0\nHALT");cpu_feed(c,(const uint8_t *)"Z",1);
 assert(cpu_step(c,NULL)&&c->rx_pos==1);assert(cpu_step(c,NULL)&&c->s.r[0]==0x60);
 cpu_destroy(c);free(c);
}
int main(void){loader();uart();puts("ELF/FDT boot validation and NS16550 transaction tests passed");return 0;}
