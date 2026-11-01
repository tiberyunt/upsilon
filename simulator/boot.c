/* Upsilon workstation ELF/FDT boot interface. */
#include "upsilon.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#define DTB_SPACE 16384
#define DTB_RESERVE 65536
static uint64_t le(const uint8_t *b,int n){uint64_t v=0;for(int i=0;i<n;i++)v|=(uint64_t)b[i]<<(8*i);return v;}
static void be32(uint8_t *p,uint32_t v){for(int i=0;i<4;i++)p[i]=(uint8_t)(v>>(24-8*i));}
static void be64(uint8_t *p,uint64_t v){be32(p,(uint32_t)(v>>32));be32(p+4,(uint32_t)v);}
static int bad(char *error,size_t cap,const char *fmt,...){va_list ap;va_start(ap,fmt);vsnprintf(error,cap,fmt,ap);va_end(ap);return 0;}
typedef struct {uint8_t data[DTB_SPACE],strings[2048];size_t at,ns;int failed;} Tree;
static void bytes(Tree *t,const void *p,size_t n){if(n>sizeof t->data-t->at){t->failed=1;return;}memcpy(t->data+t->at,p,n);t->at+=n;}
static void word(Tree *t,uint32_t v){uint8_t b[4];be32(b,v);bytes(t,b,4);}
static void align4(Tree *t){while(!t->failed&&(t->at&3)){uint8_t z=0;bytes(t,&z,1);}}
static void node(Tree *t,const char *name){word(t,1);bytes(t,name,strlen(name)+1);align4(t);}
static void end(Tree *t){word(t,2);}
static void prop(Tree *t,const char *name,const void *data,size_t n){
 size_t off=0;while(off<t->ns&&strcmp((char *)t->strings+off,name))off+=strlen((char *)t->strings+off)+1;
 if(off==t->ns){size_t len=strlen(name)+1;if(t->ns+len>sizeof t->strings){t->failed=1;return;}memcpy(t->strings+t->ns,name,len);t->ns+=len;}
 word(t,3);word(t,(uint32_t)n);word(t,(uint32_t)off);bytes(t,data,n);align4(t);
}
static void textprop(Tree *t,const char *name,const char *value){prop(t,name,value,strlen(value)+1);}
static void cell(Tree *t,const char *name,uint32_t n){uint8_t b[4];be32(b,n);prop(t,name,b,4);}
static void regprop(Tree *t,uint64_t base,uint64_t size){uint8_t b[16];be64(b,base);be64(b+8,size);prop(t,"reg",b,16);}
static size_t make_fdt(uint8_t *out,uint64_t memory,uint64_t dtb,const char *args){
 Tree *t=calloc(1,sizeof *t);if(!t)return 0;
 /* 40-byte header, one memory reservation and its terminating zero entry. */
 t->at=72;node(t,"");textprop(t,"compatible","kotl,upsilon-virt");textprop(t,"model","Upsilon functional workstation");
 cell(t,"#address-cells",2);cell(t,"#size-cells",2);
 node(t,"chosen");textprop(t,"stdout-path","/soc/serial@ffff0400");textprop(t,"bootargs",args);end(t);
 node(t,"memory@0");textprop(t,"device_type","memory");regprop(t,0,memory);end(t);
 node(t,"cpus");cell(t,"#address-cells",1);cell(t,"#size-cells",0);cell(t,"timebase-frequency",1000000);
 node(t,"cpu@0");textprop(t,"device_type","cpu");textprop(t,"compatible","kotl,upsilon-v04");cell(t,"reg",0);cell(t,"clock-frequency",1000000);end(t);end(t);
 node(t,"soc");textprop(t,"compatible","simple-bus");cell(t,"#address-cells",2);cell(t,"#size-cells",2);prop(t,"ranges","",0);
 node(t,"serial@ffff0400");textprop(t,"compatible","ns16550a");regprop(t,UART16550_BASE,8);cell(t,"clock-frequency",1843200);cell(t,"current-speed",115200);cell(t,"reg-shift",0);cell(t,"reg-io-width",1);end(t);
 node(t,"platform@ffff0200");textprop(t,"compatible","kotl,upsilon-platform");regprop(t,PLATFORM_BASE,17);end(t);
 end(t);end(t);word(t,9);
 size_t size_struct=t->at-72,off_strings=t->at;bytes(t,t->strings,t->ns);
 if(t->failed){free(t);return 0;}
 uint32_t h[]={0xd00dfeed,(uint32_t)t->at,72,(uint32_t)off_strings,40,17,16,0,(uint32_t)t->ns,(uint32_t)size_struct};
 for(int i=0;i<10;i++)be32(t->data+4*i,h[i]);be64(t->data+40,dtb);be64(t->data+48,DTB_RESERVE);
 memcpy(out,t->data,t->at);size_t result=t->at;free(t);return result;
}
int platform_load_elf(CPU *c,const uint8_t *data,size_t length,const char *bootargs,char *error,size_t cap){
 if(!bootargs)bootargs="";
 if(strlen(bootargs)>1023)return bad(error,cap,"boot arguments exceed 1023 bytes");
 if(c->ram_size<1024*1024)return bad(error,cap,"ELF boot requires at least 1 MiB RAM");
 if(length<64||memcmp(data,"\177ELF",4)||data[4]!=2||data[5]!=1||data[6]!=1)return bad(error,cap,"expected ELF64 little-endian image");
 if(le(data+16,2)!=2||le(data+18,2)!=0xf5a0||le(data+20,4)!=1||le(data+52,2)!=64)return bad(error,cap,"expected executable Upsilon ELF (machine 0xf5a0)");
 uint64_t entry=le(data+24,8),phoff=le(data+32,8);unsigned count=(unsigned)le(data+56,2);
 if(le(data+54,2)!=56||!count||count>128||phoff>length||(uint64_t)count*56>length-phoff)return bad(error,cap,"invalid ELF program headers");
 uint64_t dtb=(c->ram_size-DTB_RESERVE)&~UINT64_C(4095);int loaded=0,entry_ok=0;
 if(dtb<c->rom_end&&dtb+DTB_RESERVE>c->rom_start)return bad(error,cap,"FDT memory overlaps ROM");
 for(unsigned i=0;i<count;i++){
  const uint8_t *h=data+phoff+i*56;unsigned type=(unsigned)le(h,4);
  if(type==2||type==3)return bad(error,cap,"dynamic ELF kernels are not supported");
  if(type!=1)continue;
  uint64_t off=le(h+8,8),va=le(h+16,8),pa=le(h+24,8),filesz=le(h+32,8),memsz=le(h+40,8),align=le(h+48,8);
  if(filesz>memsz||off>length||filesz>length-off||pa>c->ram_size||memsz>c->ram_size-pa)return bad(error,cap,"ELF segment outside file or RAM");
  if(align>1 && ((align&(align-1)) || (pa%align)!=(off%align)))return bad(error,cap,"invalid ELF segment alignment");
  if(memsz&&pa<dtb+DTB_RESERVE&&pa+memsz>dtb)return bad(error,cap,"ELF segment overlaps reserved FDT memory");
  if(memsz&&pa<c->rom_end&&pa+memsz>c->rom_start)return bad(error,cap,"ELF segment overlaps ROM");
  if(va==pa&&entry>=pa&&entry-pa<filesz&&(le(h+4,4)&1))entry_ok=1;
  for(unsigned j=0;j<i;j++){
   const uint8_t *other=data+phoff+j*56;if(le(other,4)!=1)continue;
   uint64_t base=le(other+24,8),size=le(other+40,8);
   if(memsz&&size&&pa<base+size&&base<pa+memsz)return bad(error,cap,"overlapping ELF load segments");
  }
  loaded++;
 }
 if(!loaded||!entry_ok)return bad(error,cap,"ELF entry is not in an identity-mapped executable segment");
 uint8_t fdt[DTB_SPACE];size_t size=make_fdt(fdt,c->ram_size,dtb,bootargs);
 if(!size)return bad(error,cap,"unable to construct device tree");
 /* Every segment and the FDT are validated before mutating guest memory. */
 for(unsigned i=0;i<count;i++){
  const uint8_t *h=data+phoff+i*56;if(le(h,4)!=1)continue;
  size_t pa=(size_t)le(h+24,8),filesz=(size_t)le(h+32,8),memsz=(size_t)le(h+40,8);
  memcpy(c->ram+pa,data+le(h+8,8),filesz);memset(c->ram+pa+filesz,0,memsz-filesz);
 }
 memcpy(c->ram+dtb,fdt,size);c->s.pc=entry;c->s.r[0]=dtb;c->s.r[1]=c->s.r[2]=0;
 c->s.r[31]=(dtb-16)&~UINT64_C(15);c->s.sys[24]=c->s.r[31];
 c->s.psr=0x700;c->s.sys[20]=0;c->s.sys[28]=UINT64_MAX;c->traps=1;c->serial=1;
 return 1;
}
