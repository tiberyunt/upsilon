#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
/* Standalone qcow2 v2/v3 block images.
 * https://www.qemu.org/docs/master/interop/qcow2.html
 * The writer rebuilds a sparse, refcount-consistent qcow2 into a sibling file
 * and publishes it atomically. Guest filesystem/boot formats live in assembly. */
#include "upsilon.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#define CLUSTER 65536u
#define COPIED (UINT64_C(1)<<63)
#define OFFSET_MASK UINT64_C(0x00fffffffffffe00)
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t be64(const uint8_t *p){return (uint64_t)be32(p)<<32|be32(p+4);}
static void put32(uint8_t *p,uint32_t n){for(int i=3;i>=0;i--){p[i]=(uint8_t)n;n>>=8;}}
static void put64(uint8_t *p,uint64_t n){for(int i=7;i>=0;i--){p[i]=(uint8_t)n;n>>=8;}}
static int read_at(FILE *f,uint64_t at,void *data,size_t n,uint64_t size){return at<=size&&n<=size-at&&fseeko(f,(off_t)at,SEEK_SET)==0&&fread(data,1,n,f)==n;}
void disk_close(Disk *d){if(d->lock_path&&d->lock_fd>=0)close(d->lock_fd);free(d->path);free(d->lock_path);free(d->data);memset(d,0,sizeof *d);}
int disk_open(Disk *d,const char *path,int readonly,int autoboot,char *error,size_t cap){
    Disk temp={0};temp.lock_fd=-1;temp.path=strdup(path);temp.lock_path=malloc(strlen(path)+6);
    FILE *f=NULL;uint8_t *l2=NULL;
    if(!temp.path||!temp.lock_path)goto bad;
    sprintf(temp.lock_path,"%s.lock",path);
    if(!readonly){
        struct stat st;FILE *rw=fopen(path,"rb+");
        if(rw&&fstat(fileno(rw),&st)==0&&(st.st_mode&0222)){
            temp.lock_fd=open(temp.lock_path,O_RDWR|O_CREAT,0600);
            if(temp.lock_fd>=0&&flock(temp.lock_fd,LOCK_EX|LOCK_NB)==0)temp.writable=1;
        }
        if(rw)fclose(rw);
    }
    f=fopen(path,"rb");if(!f)goto bad;
    struct stat st;if(fstat(fileno(f),&st)||st.st_size<72)goto invalid;
    uint64_t hostsize=(uint64_t)st.st_size;uint8_t header[104]={0};
    if(!read_at(f,0,header,72,hostsize))goto invalid;
    unsigned version=be32(header+4),bits=be32(header+20);
    if(be32(header)!=0x514649fb||(version!=2&&version!=3)||bits<9||bits>21)goto unsupported;
    if(version==3){if(!read_at(f,72,header+72,32,hostsize))goto invalid;
        if(be64(header+72)||be64(header+88)||be32(header+96)!=4||be32(header+100)<104||be32(header+100)>(1u<<bits))goto unsupported;}
    if(be64(header+8)||be32(header+16)||be32(header+32)||be32(header+60))goto unsupported;
    temp.size=be64(header+24);
    if(!temp.size||temp.size>UINT64_C(1024)*1024*1024||(temp.size&511))goto unsupported;
    uint64_t cs=UINT64_C(1)<<bits,entries=cs/8,l1=be64(header+40),l1count=be32(header+36);
    uint64_t needed=(temp.size+cs*entries-1)/(cs*entries);
    if((l1&(cs-1))||l1count<needed||l1>hostsize||l1count>(hostsize-l1)/8)goto invalid;
    temp.data=calloc((size_t)temp.size,1);l2=malloc((size_t)cs);if(!temp.data||!l2)goto bad;
    for(uint64_t i=0;i<needed;i++){
        uint8_t word[8];if(!read_at(f,l1+i*8,word,8,hostsize))goto invalid;
        uint64_t entry=be64(word);if(entry&~(COPIED|OFFSET_MASK))goto unsupported;
        uint64_t table=entry&OFFSET_MASK;if(!table)continue;
        if((table&(cs-1))||!read_at(f,table,l2,(size_t)cs,hostsize))goto invalid;
        for(uint64_t j=0;j<entries;j++){
            uint64_t guest=(i*entries+j)*cs;if(guest>=temp.size)break;
            entry=be64(l2+j*8);
            if(entry&(UINT64_C(1)<<62))goto unsupported;
            if(entry&~(COPIED|OFFSET_MASK|UINT64_C(1)))goto unsupported;
            if(entry&1){if(version==2)goto unsupported;continue;}
            uint64_t data=entry&OFFSET_MASK;if(!data)continue;
            size_t n=(size_t)(temp.size-guest<cs?temp.size-guest:cs);
            if((data&(cs-1))||!read_at(f,data,temp.data+guest,n,hostsize))goto invalid;
        }
    }
    free(l2);fclose(f);temp.mounted=1;temp.autoboot=autoboot;disk_close(d);*d=temp;return 1;
unsupported:
    snprintf(error,cap,"unsupported qcow2: need standalone uncompressed v2/v3, 16-bit refcounts, no snapshots/backing/encryption/features, 512 B..1 GiB");goto fail;
invalid:
    snprintf(error,cap,"invalid or truncated qcow2 image");goto fail;
bad:
    snprintf(error,cap,"cannot open qcow2 image: %s",strerror(errno));
fail:
    if(f)fclose(f);free(l2);disk_close(&temp);return 0;
}
static int nonzero(const uint8_t *p,size_t n){for(size_t i=0;i<n;i++)if(p[i])return 1;return 0;}
static int persist(Disk *d,char *error,size_t cap){
    size_t count=(size_t)((d->size+CLUSTER-1)/CLUSTER),l1count=(count+8191)/8192;
    uint8_t *used=calloc(count,1);uint64_t l2index[2]={0};size_t tables=0,allocated=0;
    if(!used)goto allocation;
    for(size_t i=0;i<count;i++){
        size_t n=(size_t)(d->size-i*CLUSTER);if(n>CLUSTER)n=CLUSTER;
        if(nonzero(d->data+i*CLUSTER,n)){used[i]=1;allocated++;l2index[i/8192]=1;}
    }
    for(size_t i=0;i<l1count;i++)if(l2index[i])l2index[i]=4+tables++;
    size_t metadata=4+tables,total=metadata+allocated;
    uint8_t *meta=calloc(metadata,CLUSTER);char *temporary=malloc(strlen(d->path)+16);
    if(!meta||!temporary){free(meta);free(temporary);free(used);goto allocation;}
    put32(meta,0x514649fb);put32(meta+4,3);put32(meta+20,16);put64(meta+24,d->size);
    put32(meta+36,(uint32_t)l1count);put64(meta+40,3*CLUSTER);put64(meta+48,CLUSTER);
    put32(meta+56,1);put32(meta+96,4);put32(meta+100,104);
    put64(meta+CLUSTER,2*CLUSTER);
    for(size_t i=0;i<total;i++)meta[2*CLUSTER+i*2+1]=1;
    for(size_t i=0;i<l1count;i++)if(l2index[i])put64(meta+3*CLUSTER+i*8,COPIED|l2index[i]*CLUSTER);
    size_t next=metadata;
    for(size_t i=0;i<count;i++)if(used[i])put64(meta+l2index[i/8192]*CLUSTER+(i%8192)*8,COPIED|next++*CLUSTER);
    sprintf(temporary,"%s.tmp.XXXXXX",d->path);int fd=mkstemp(temporary),ok=fd>=0;
    FILE *f=ok?fdopen(fd,"wb"):NULL;if(ok&&!f){close(fd);ok=0;}
    struct stat st;if(ok&&stat(d->path,&st)==0&&fchmod(fd,st.st_mode&0777))ok=0;
    if(ok&&fwrite(meta,CLUSTER,metadata,f)!=metadata)ok=0;
    uint8_t zeros[CLUSTER]={0};
    for(size_t i=0;ok&&i<count;i++)if(used[i]){
        size_t n=(size_t)(d->size-i*CLUSTER);if(n>CLUSTER)n=CLUSTER;
        if(fwrite(d->data+i*CLUSTER,1,n,f)!=n||(n<CLUSTER&&fwrite(zeros,1,CLUSTER-n,f)!=CLUSTER-n))ok=0;
    }
    if(ok&&(fflush(f)||fsync(fd)))ok=0;
    if(f&&fclose(f))ok=0;
    if(ok&&rename(temporary,d->path))ok=0;
    if(!ok){unlink(temporary);snprintf(error,cap,"cannot persist qcow2: %s",strerror(errno));}
    else {char *slash=strrchr(temporary,'/');if(slash)*slash=0;int dir=open(slash?(temporary[0]?temporary:"/"):".",O_RDONLY);if(dir>=0){(void)fsync(dir);close(dir);}}
    free(temporary);free(meta);free(used);return ok;
allocation:
    snprintf(error,cap,"out of memory writing qcow2");return 0;
}
int disk_write(Disk *d,uint64_t offset,const uint8_t *data,size_t length,char *error,size_t cap){
    if(!d->mounted||!d->writable||offset>d->size||length>d->size-offset){snprintf(error,cap,"disk write protected or outside capacity");return 0;}
    uint8_t *old=malloc(length?length:1);if(!old){snprintf(error,cap,"out of memory writing disk");return 0;}
    memcpy(old,d->data+offset,length);memcpy(d->data+offset,data,length);
    int ok=persist(d,error,cap);if(!ok)memcpy(d->data+offset,old,length);free(old);return ok;
}
int disk_create(const char *path,uint64_t size,char *error,size_t cap){
    if(!size||size>UINT64_C(1024)*1024*1024||(size&511)){snprintf(error,cap,"disk size must be 512-byte aligned and at most 1 GiB");return 0;}
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0644);if(fd<0){snprintf(error,cap,"cannot create disk (existing files are preserved): %s",strerror(errno));return 0;}close(fd);
    Disk d={0};d.path=strdup(path);d.size=size;d.data=calloc((size_t)size,1);
    int ok=0;if(!d.path||!d.data)snprintf(error,cap,"out of memory creating disk");else ok=persist(&d,error,cap);free(d.path);free(d.data);if(!ok)unlink(path);return ok;
}
