#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
/* 9-track reel transport: SIMH .tap records and tape marks.
 * https://simh.trailing-edge.com/docs/simh_magtape.pdf
 * Byte/parity recovery is represented by the SIMH record-error flag. */
#include "upsilon.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void word32(FILE *f,uint32_t v) {
    for(int i=0;i<4;i++)fputc((int)((v>>(i*8))&255),f);
}
static void word64(uint8_t *p,uint64_t v) {
    for(int i=0;i<8;i++)p[i]=(uint8_t)(v>>(i*8));
}
void tape_close(Tape *t) {
    if(t->lock_path&&t->lock_fd>=0){close(t->lock_fd);}
    free(t->path);free(t->lock_path);
    free(t->image);free(t->objects);memset(t,0,sizeof *t);
}
static int add_object(Tape *t,size_t offset,uint32_t length,uint32_t type) {
    if(t->count==1000000)return 0;
    if(t->count==t->capacity) {
        size_t capacity=t->capacity?t->capacity*2:256;
        TapeObject *objects=realloc(t->objects,capacity*sizeof *objects);
        if(!objects)return 0;
        t->objects=objects;t->capacity=capacity;
    }
    t->objects[t->count++]=(TapeObject){offset,length,type};return 1;
}
int tape_open(Tape *t,const char *path,int autoboot,char *error,size_t cap) {
    Tape temp={0};
    temp.path=strdup(path);temp.lock_path=malloc(strlen(path)+6);temp.lock_fd=-1;
    if(!temp.path||!temp.lock_path){snprintf(error,cap,"out of memory mounting tape");goto bad;}
    sprintf(temp.lock_path,"%s.lock",path);
    struct stat st;
    FILE *rw=fopen(path,"rb+");
    if(rw&&fstat(fileno(rw),&st)==0&&(st.st_mode&0222)) {
        temp.lock_fd=open(temp.lock_path,O_RDWR|O_CREAT,0600);
        if(temp.lock_fd>=0&&flock(temp.lock_fd,LOCK_EX|LOCK_NB)==0)temp.writable=1;
    }
    if(rw)fclose(rw);
    FILE *f=fopen(path,"rb");
    if(!f){snprintf(error,cap,"cannot open tape %s: %s",path,strerror(errno));goto bad;}
    if(fseek(f,0,SEEK_END)){snprintf(error,cap,"cannot seek tape image");fclose(f);goto bad;}
    long end=ftell(f);
    if(end<0||(uint64_t)end>UINT64_C(512)*1024*1024){snprintf(error,cap,"tape image exceeds 512 MiB");fclose(f);goto bad;}
    rewind(f);temp.size=(size_t)end;temp.image=malloc(temp.size?temp.size:1);
    if(!temp.image){snprintf(error,cap,"out of memory mounting tape");fclose(f);goto bad;}
    if(fread(temp.image,1,temp.size,f)!=temp.size){snprintf(error,cap,"cannot read tape image");fclose(f);tape_close(&temp);return 0;}
    fclose(f);
    size_t pos=0;
    while(pos<temp.size) {
        if(temp.size-pos<4){snprintf(error,cap,"truncated tape metadata at byte %zu",pos);goto bad;}
        uint32_t header=le32(temp.image+pos);pos+=4;
        if(header==UINT32_C(0xfffffffe))continue; /* erase gap */
        if(header==UINT32_MAX) {
            if(pos!=temp.size){snprintf(error,cap,"bytes follow tape end-of-medium marker");goto bad;}
            break;
        }
        if(!header) {
            if(!add_object(&temp,pos,0,TAPE_MARK)){snprintf(error,cap,"too many tape objects or out of memory");goto bad;}
            continue;
        }
        if(header&UINT32_C(0x7f000000)){snprintf(error,cap,"unsupported tape metadata 0x%08x at byte %zu",header,pos-4);goto bad;}
        uint32_t length=header&UINT32_C(0xffffff);
        if(!length){snprintf(error,cap,"zero-length error record in tape image");goto bad;}
        size_t padded=((size_t)length+1)&~(size_t)1;
        if(padded>temp.size-pos||temp.size-pos-padded<4){snprintf(error,cap,"truncated tape record at byte %zu",pos-4);goto bad;}
        if(le32(temp.image+pos+padded)!=header){snprintf(error,cap,"mismatched tape record trailer at byte %zu",pos+padded);goto bad;}
        if(!add_object(&temp,pos,length,header>>31?TAPE_RECORD_ERROR:0)){snprintf(error,cap,"too many tape objects or out of memory");goto bad;}
        pos+=padded+4;
    }
    temp.mounted=1;temp.state.current=SIZE_MAX;temp.state.autoboot=autoboot;
    tape_close(t);*t=temp;return 1;
bad:
    tape_close(&temp);return 0;
}
static const TapeObject *current(const Tape *t,const TapeState *s) {
    return s->current<t->count?&t->objects[s->current]:NULL;
}
uint8_t tape_status(const Tape *t,const TapeState *s) {
    if(!t->mounted)return 0;
    const TapeObject *o=current(t,s);
    return TAPE_ONLINE|(o&&!(o->flags&TAPE_MARK)&&s->offset<o->length?TAPE_READY:0)|
        (s->flags&(TAPE_MARK|TAPE_EOM|TAPE_ERROR))|(s->head==0?TAPE_BOT:0)|(s->autoboot?TAPE_AUTO:0)|(!t->writable?TAPE_READONLY:0);
}
uint8_t tape_read(Tape *t,TapeState *s,unsigned reg) {
    if(!tape_readable(reg))return 0;
    const TapeObject *o=current(t,s);
    if(reg==0) {
        if(!o||(o->flags&TAPE_MARK)||s->offset>=o->length)return 0;
        return t->image[o->offset+s->offset++];
    }
    if(reg==4)return tape_status(t,s);
    if(reg==8)return s->command;
    if(reg>=40) {
        uint64_t value=reg<48?s->dma_addr:s->dma_len;
        return (uint8_t)(value>>((reg<48?reg-40:reg-48)*8));
    }
    uint64_t value=reg<24?(o?o->length:0):reg<32?s->head:(o&&s->offset<o->length?o->length-s->offset:0);
    unsigned shift=(reg<24?reg-16:reg<32?reg-24:reg-32)*8;
    return (uint8_t)(value>>shift);
}
int tape_readable(unsigned reg) {return reg==0||reg==4||reg==8||(reg>=16&&reg<56);}
void tape_command(Tape *t,TapeState *s,uint8_t command) {
    s->command=command;
    if(!t->mounted){s->flags=TAPE_ERROR;return;}
    if(s->append){s->append=0;s->flags=TAPE_ERROR;return;}
    if(command==TAPE_APPEND) {
        if(!t->writable||!s->dma_len||s->dma_len>1048576||s->append){s->flags=TAPE_ERROR;return;}
        s->append=1;s->flags=0;return;
    }
    if(command==TAPE_REWIND){s->head=0;s->current=SIZE_MAX;s->offset=0;s->flags=0;s->autoboot=0;return;}
    if(command!=TAPE_READ&&command!=TAPE_SPACE&&command!=TAPE_BACKSPACE){s->flags=TAPE_ERROR;return;}
    s->flags=0;s->offset=0;s->current=SIZE_MAX;
    if(command==TAPE_BACKSPACE) {
        if(s->head)s->head--;
        return;
    }
    if(s->head==t->count){s->flags=TAPE_EOM;return;}
    s->current=s->head++;
    const TapeObject *o=current(t,s);
    if(o->flags&TAPE_MARK)s->flags=TAPE_MARK;
    if(o->flags&TAPE_RECORD_ERROR)s->flags|=TAPE_ERROR;
    if(command==TAPE_SPACE)s->offset=o->length;
}
static int record(FILE *f,const uint8_t *data,size_t n) {
    word32(f,(uint32_t)n);
    if(fwrite(data,1,n,f)!=n)return 0;
    if(n&1)fputc(0,f);
    word32(f,(uint32_t)n);return !ferror(f);
}
int tape_create(const char *path,const uint8_t *data,size_t length,uint64_t load,uint64_t entry,uint64_t stack,size_t block,char *error,size_t cap) {
    if(!length||!block||block>65536||load<0x80000||length>UINT64_MAX-load||entry<load||entry-load>=length||
       (stack&&(stack&15||stack<load+length))) {
        snprintf(error,cap,"invalid tape boot addresses, stack, image length or record size");return 0;
    }
    FILE *f=fopen(path,"wb");
    if(!f){snprintf(error,cap,"cannot create tape %s: %s",path,strerror(errno));return 0;}
    uint8_t header[48]={0};memcpy(header,"UPS9BOOT",8);word64(header+8,1);word64(header+16,load);
    word64(header+24,entry);word64(header+32,length);word64(header+40,stack);
    int ok=record(f,header,sizeof header);
    for(size_t at=0;ok&&at<length;) {
        size_t count=length-at<block?length-at:block;ok=record(f,data+at,count);at+=count;
    }
    if(ok){word32(f,0);word32(f,UINT32_MAX);ok=!ferror(f);}
    if(fclose(f))ok=0;
    if(!ok){remove(path);snprintf(error,cap,"cannot write tape image");}return ok;
}

int tape_writable_register(unsigned reg) {return reg==8||(reg>=40&&reg<56);}
void tape_write_register(Tape *t,TapeState *s,unsigned reg,uint8_t value) {
    if(reg==8){tape_command(t,s,value);return;}
    uint64_t *target=reg<48?&s->dma_addr:&s->dma_len;
    unsigned shift=(reg<48?reg-40:reg-48)*8;
    *target=(*target&~(UINT64_C(255)<<shift))|((uint64_t)value<<shift);
}
/* Append at EOM without rewriting the boot file. Atomic host replacement ensures
 * a failed save leaves the previous reel intact, including on short writes. */
int tape_append(Tape *t,const uint8_t *data,size_t length,char *error,size_t cap) {
    if(!t->mounted||!t->writable||!length||length>1048576||t->count==1000000) {
        snprintf(error,cap,"tape is write-protected or record size is invalid");return 0;
    }
    size_t at=t->size;
    if(at>=4&&le32(t->image+at-4)==UINT32_MAX)at-=4;
    size_t padded=(length+1)&~(size_t)1,newsize=at+padded+12;
    if(newsize>512u*1024u*1024u){snprintf(error,cap,"tape is full (512 MiB)");return 0;}
    uint8_t *image=malloc(newsize);
    if(!image){snprintf(error,cap,"out of memory writing tape");return 0;}
    memcpy(image,t->image,at);
    uint32_t n=(uint32_t)length;
    for(unsigned i=0;i<4;i++){image[at+i]=(uint8_t)(n>>(i*8));image[at+4+padded+i]=(uint8_t)(n>>(i*8));image[newsize-4+i]=255;}
    memcpy(image+at+4,data,length);if(length&1)image[at+4+length]=0;
    if(t->count==t->capacity) {
        size_t capacity=t->capacity?t->capacity*2:256;
        TapeObject *objects=realloc(t->objects,capacity*sizeof *objects);
        if(!objects){free(image);snprintf(error,cap,"out of memory indexing tape");return 0;}
        t->objects=objects;t->capacity=capacity;
    }
    char *temporary=malloc(strlen(t->path)+16);
    if(!temporary){free(image);snprintf(error,cap,"out of memory writing tape");return 0;}
    sprintf(temporary,"%s.tmp.XXXXXX",t->path);
    int fd=mkstemp(temporary),ok=fd>=0;struct stat st;
    if(ok&&stat(t->path,&st)==0&&fchmod(fd,st.st_mode&0777))ok=0;
    size_t done=0;
    while(ok&&done<newsize) {
        ssize_t written=write(fd,image+done,newsize-done);
        if(written<0&&errno==EINTR)continue;
        if(written<=0){ok=0;break;}done+=(size_t)written;
    }
    if(ok&&fsync(fd))ok=0;
    if(fd>=0&&close(fd))ok=0;
    if(ok&&rename(temporary,t->path))ok=0;
    if(!ok){unlink(temporary);free(temporary);free(image);snprintf(error,cap,"cannot persist tape record: %s",strerror(errno));return 0;}
    /* Best effort directory synchronization after the committed rename. */
    char *slash=strrchr(temporary,'/');
    if(slash)*slash=0;
    int directory=open(slash?(temporary[0]?temporary:"/"):".",O_RDONLY);
    if(directory>=0){(void)fsync(directory);close(directory);}
    free(temporary);free(t->image);t->image=image;t->size=newsize;
    t->objects[t->count++]=(TapeObject){at+4,(uint32_t)length,0};
    return 1;
}
