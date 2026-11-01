#include "upsilon.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#ifndef UPSILON_BOOTSTRAP
#include "firmware_image.h"
#endif
static struct termios terminal;
static int terminal_saved;
static volatile sig_atomic_t interrupted;
static void restore_terminal(void) {
    if(terminal_saved)tcsetattr(STDIN_FILENO, TCSADRAIN, &terminal);
    terminal_saved=0;
}
static void on_signal(int sig) {
    (void)sig;
    interrupted=1;
}
static void json_state(FILE *f, CPU *c) {
    fprintf(f, "{\n\"pc\":%llu,\"psr\":%llu,\"fpsr\":%llu,\"steps\":%llu,\"halted\":%s,\"waiting\":%s,\n\"registers\":{", (unsigned long long)c->s.pc, (unsigned long long)c->s.psr, (unsigned long long)c->s.fpsr, (unsigned long long)c->steps, c->s.halted?"true":"false", c->s.waiting?"true":"false");
    for(int i=0;i<32;i++)fprintf(f, "%s\"R%d\":%llu", i?",":"", i, (unsigned long long)c->s.r[i]);
    fprintf(f, "},\n\"float_bits\":{");
    for(int i=0;i<32;i++)fprintf(f, "%s\"F%d\":%llu", i?",":"", i, (unsigned long long)c->s.f[i]);
    fprintf(f, "},\n\"system_registers\":{");
    int first=1;
    for(int i=0;i<120;i++) {
        const char *name=system_name(i);
        if(!name)continue;
        uint64_t value=i==27?c->s.psr:(unsigned)i==24+((c->s.psr>>12)&3)?c->s.r[31]:c->s.sys[i];
        fprintf(f, "%s\"%s\":%llu", first?"":",", name, (unsigned long long)value);
        first=0;
    }
    fprintf(f, "},\n\"memory_bytes\":%zu,\"tape\":{\"mounted\":%s,\"position\":%zu,\"offset\":%zu,\"status\":%u}\n}\n",
            c->ram_size,c->tape.mounted?"true":"false",c->tape.state.head,c->tape.state.offset,
            (unsigned)tape_status(&c->tape,&c->tape.state));
}
static uint64_t number(const char *s) {
    char *end;
    errno=0;
    unsigned long long n=strtoull(s, &end, 0);
    if(errno||!*s||*end) {
        fprintf(stderr, "invalid number: %s\n", s);
        exit(2);
    }
    return n;
}
static void usage(void) {
    puts("Kernel boot: upsilon -m 256 --kernel kernel.elf [--bootargs string]\nDirect ELF: upsilon run image.elf --elf --serial --memory bytes");
    fprintf(stderr, "Kotl Upsilon v0.4 simulator (assembly commands use upsilon-as)\n"
        "  upsilon [-m MiB] [-t reel.tap] [-d disk.qcow2] [--tape-readonly] [--disk-readonly] [--rom firmware.bin] [--no-autoboot]\n"
        "  upsilon-as source -o binary [--origin n] [--map file]\n"
        "  upsilon disasm binary [--origin n]\n"
        "  upsilon run program [--binary] [--boot] [--serial|--keyboard] [-t reel.tap]\n"
        "      [--serial-input text] [--json] [--trace] [--max-steps n] [--memory bytes]\n"
        "  upsilon mktape binary -o reel.tap [--load n] [--entry n] [--stack n] [--record-size n] [--add NAME=path]...\n"
        "  upsilon mkdisk disk.qcow2 [--size MiB]\n"
        "  upsilon-as rom-link monitor.s basic.s native.s -o linked.s\n");
}
static int assembly_command(int argc,char **argv){
    const char *slash=strrchr(argv[0],'/');char path[4096];
    if(slash){size_t n=(size_t)(slash-argv[0]+1);if(n+sizeof("upsilon-as")>sizeof path)return 2;
        memcpy(path,argv[0],n);strcpy(path+n,"upsilon-as");}
    else strcpy(path,"upsilon-as");
    if(!strcmp(argv[1],"link"))argv[1]="rom-link";
    argv[0]=path;(void)argc;execvp(path,argv);perror(path);return 1;
}
int main(int argc, char **argv) {
    if(argc>1&&(!strcmp(argv[1],"asm")||!strcmp(argv[1],"link")||!strcmp(argv[1],"embed")))
        return assembly_command(argc,argv);
    if(argc==2&&(!strcmp(argv[1],"--help")||!strcmp(argv[1],"-h"))){usage();return 0;}
    if(argc==2&&!strcmp(argv[1],"--version")){puts("Kotl Upsilon 0.4 C emulator");return 0;}
    int launch=argc<2||argv[1][0]=='-';
    if(!launch&&argc<3){usage();return 2;}
    const char *command=launch?"run":argv[1], *path=launch?NULL:argv[2];
    const char *kernel=NULL,*bootargs="";int elf=0;
    const char *output=NULL,*statepath=NULL,*dump=NULL,*input="",*tapefile=NULL;
    uint64_t origin=4096,entry=0,maxsteps=launch?100000000:1000000,memory=launch?UINT64_C(256)*1024*1024:RAM_DEFAULT;
    uint64_t tape_load=0x100000,tape_stack=0,record_size=4096,disk_mib=128;
    int binary=launch,boot=launch,serial=launch,keyboard=launch,json=0,trace=0,traps=launch,hasentry=0,autoboot=launch,tape_ro=0,disk_ro=0;
    const char *setregs[128],*addfiles[64],*diskfiles[DISK_COUNT];int nset=0,nadd=0,ndisks=0;
    int start=launch?1:3;
    for(int i=start;i<argc;i++) {
        const char *arg=argv[i];
        if(!strcmp(arg,"--elf"))elf=1;
        else if(!strcmp(arg,"--binary"))binary=1;
        else if(!strcmp(arg,"--boot"))boot=1;
        else if(!strcmp(arg,"--serial"))serial=1;
        else if(!strcmp(arg,"--keyboard"))keyboard=serial=1;
        else if(!strcmp(arg,"--no-keyboard"))keyboard=0;
        else if(!strcmp(arg,"--tape-readonly"))tape_ro=1;
        else if(!strcmp(arg,"--disk-readonly"))disk_ro=1;
        else if(!strcmp(arg,"--autoboot"))autoboot=1;
        else if(!strcmp(arg,"--no-autoboot"))autoboot=0;
        else if(!strcmp(arg,"--json"))json=1;
        else if(!strcmp(arg,"--trace"))trace=1;
        else if(!strcmp(arg,"--traps"))traps=1;
        else {
            if(i+1==argc){fprintf(stderr,"missing argument for %s\n",arg);return 2;}
            const char *value=argv[++i];
            if(!strcmp(arg,"--kernel")&&launch)kernel=value;
            else if(!strcmp(arg,"--bootargs"))bootargs=value;
            else if(!strcmp(arg,"-o")||!strcmp(arg,"--output"))output=value;
            else if(!strcmp(arg,"--add")&&nadd<64)addfiles[nadd++]=value;
            else if(!strcmp(arg,"--map")){fprintf(stderr,"--map is an assembler option\n");return 2;}
            else if(!strcmp(arg,"--state"))statepath=value;
            else if(!strcmp(arg,"--dump-memory"))dump=value;
            else if(!strcmp(arg,"--origin"))origin=number(value);
            else if(!strcmp(arg,"--entry")){entry=number(value);hasentry=1;}
            else if(!strcmp(arg,"--memory"))memory=number(value);
            else if(!strcmp(arg,"-m")) {
                uint64_t mib=number(value);
                if(!mib||mib>SERIAL_BASE/(1024*1024)){fprintf(stderr,"RAM must be 1..4095 MiB\n");return 2;}
                memory=mib*1024*1024;
            }
            else if(!strcmp(arg,"-d")||!strcmp(arg,"--disk")){if(ndisks==DISK_COUNT){fprintf(stderr,"at most four disks may be attached\n");return 2;}diskfiles[ndisks++]=value;}
            else if(!strcmp(arg,"--size"))disk_mib=number(value);
            else if(!strcmp(arg,"-t")||!strcmp(arg,"--tape"))tapefile=value;
            else if(!strcmp(arg,"--rom")&&launch)path=value;
            else if(!strcmp(arg,"--load"))tape_load=number(value);
            else if(!strcmp(arg,"--stack"))tape_stack=number(value);
            else if(!strcmp(arg,"--record-size"))record_size=number(value);
            else if(!strcmp(arg,"--max-steps"))maxsteps=number(value);
            else if(!strcmp(arg,"--serial-input"))input=value;
            else if(!strcmp(arg,"--set")&&nset<128)setregs[nset++]=value;
            else {fprintf(stderr,"unknown option %s\n",arg);return 2;}
        }
    }
    if(kernel){if(path){fprintf(stderr,"--kernel cannot be combined with --rom\n");return 2;}path=kernel;elf=serial=traps=1;boot=0;}
    if(elf&&(!path||strcmp(command,"run"))){fprintf(stderr,"ELF boot requires an image path and run command\n");return 2;}
    if(elf)serial=traps=1;
    if(elf&&(boot||hasentry)){fprintf(stderr,"ELF entry comes from the image; do not use --boot or --entry\n");return 2;}
    if(!strcmp(command,"mkdisk")){
        char error[256];
        if(!disk_mib||disk_mib>1024){fprintf(stderr,"disk size must be 1..1024 MiB\n");return 2;}
        if(!disk_create(path,disk_mib*1024*1024,error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
        printf("Created qcow2 disk %s (%llu MiB)\n",path,(unsigned long long)disk_mib);return 0;
    }
    if(!strcmp(command,"mktape")) {
        if(!output){usage();return 2;}
        size_t length;char *bytes=readfile(path,&length);char error[512];
        if(!bytes){perror(path);return 1;}
        char dosnames[64][13]={{0}};
        for(int i=0;i<nadd;i++) {
            const char *equal=strchr(addfiles[i],'=');int base=0,ext=0,dot=0,valid=1;
            if(!equal||equal==addfiles[i]||equal-addfiles[i]>12){fprintf(stderr,"--add requires DOSNAME=path (8.3 name)\n");free(bytes);return 1;}
            for(int j=0;j<equal-addfiles[i];j++) {
                unsigned char ch=(unsigned char)toupper((unsigned char)addfiles[i][j]);dosnames[i][j]=(char)ch;
                if(ch=='.'){if(!base||dot)valid=0;dot=1;}
                else {if(!((ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_'||ch=='-'))valid=0;if(dot)ext++;else base++;}
            }
            if(!valid||!base||base>8||ext>3||(dot&&!ext)){fprintf(stderr,"invalid DOS 8.3 name: %s\n",dosnames[i]);free(bytes);return 1;}
            for(int j=0;j<i;j++)if(!strcmp(dosnames[j],dosnames[i])){fprintf(stderr,"duplicate DOS name: %s\n",dosnames[i]);free(bytes);return 1;}
            size_t filelen;char *check=readfile(equal+1,&filelen);
            if(!check||filelen>65536){fprintf(stderr,"cannot package %s (maximum 65536 bytes)\n",equal+1);free(check);free(bytes);return 1;}
            free(check);
        }
        int ok=tape_create(output,(const uint8_t*)bytes,length,tape_load,hasentry?entry:tape_load,tape_stack,(size_t)record_size,error,sizeof error);
        free(bytes);if(!ok){fprintf(stderr,"upsilon: %s\n",error);return 1;}
        if(nadd) {
            Tape tape={0};
            if(!tape_open(&tape,output,0,error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
            for(int i=0;i<nadd;i++) {
                const char *equal=strchr(addfiles[i],'=');
                if(!equal||equal==addfiles[i]||(size_t)(equal-addfiles[i])>12){fprintf(stderr,"--add requires DOSNAME=path (8.3 name)\n");tape_close(&tape);return 1;}
                size_t size;char *file=readfile(equal+1,&size);
                if(!file||size>65536){fprintf(stderr,"cannot package %s (maximum 65536 bytes)\n",equal+1);free(file);tape_close(&tape);return 1;}
                uint8_t *record=calloc(192+size,1);
                if(!record){free(file);tape_close(&tape);return 1;}
                memcpy(record,"UPSDOS1",7);record[8]=(uint8_t)(i+1);record[16]=1;
                memcpy(record+24,"A:\\",3);
                for(size_t j=0;j<(size_t)(equal-addfiles[i]);j++)record[27+j]=(uint8_t)toupper((unsigned char)addfiles[i][j]);
                uint64_t kind=0,at=0;const char *name=(const char*)record+27;
                if(!strcmp(name,"IO.SYS")){kind=1;at=0x100000;}
                else if(strstr(name,".EXE")){kind=1;at=!strcmp(name,"COMMAND.EXE")?0x180000:0x200000;}
                else if(strstr(name,".OBJ"))kind=2;
                for(int j=0;j<8;j++){record[152+j]=(uint8_t)((uint64_t)size>>(j*8));record[160+j]=(uint8_t)(kind>>(j*8));record[168+j]=(uint8_t)(at>>(j*8));}
                memcpy(record+192,file,size);free(file);
                uint64_t hash=UINT64_C(14695981039346656037);
                for(size_t j=0;j<192+size;j++){hash^=record[j];hash*=UINT64_C(1099511628211);}
                for(int j=0;j<8;j++)record[176+j]=(uint8_t)(hash>>(j*8));
                ok=tape_append(&tape,record,192+size,error,sizeof error);free(record);
                if(!ok){fprintf(stderr,"%s\n",error);tape_close(&tape);return 1;}
            }
            tape_close(&tape);
        }
        printf("Created bootable 9-track reel %s (%zu payload bytes, %d DOS files)\n",output,length,nadd);return 0;
    }
    if(!strcmp(command, "disasm")) {
        size_t len;
        char *bytes=readfile(path, &len);
        if(!bytes) {
            perror(path);
            return 1;
        }
        CPU *c=calloc(1, sizeof *c);
        if(!c||origin>64*1024*1024||!cpu_init(c, (size_t)origin+len+1)) {
            free(bytes);
            free(c);
            return 1;
        }
        memcpy(c->ram+origin, bytes, len);
        c->ram_size=(size_t)origin+len;
        free(bytes);
        uint64_t at=origin;
        while(at<origin+len) {
            Ins in;
            char text[1024];
            uint64_t end;
            if(setjmp(c->fault)) {
                snprintf(text, sizeof text, ".byte 0x%02x ; %s", c->ram[at], c->error);
                end=at+1;
            }
            else {
                cpu_decode(c, at, &in);
                format_ins(&in, text, sizeof text);
                end=in.end;
            }
            printf("%016llx: ", (unsigned long long)at);
            for(uint64_t i=at;i<end;i++)printf("%02x ", c->ram[i]);
            printf(" %s\n", text);
            at=end;
        }
        cpu_destroy(c);
        free(c);
        return 0;
    }
    if(strcmp(command, "run")) {
        usage();
        return 2;
    }
    if(!maxsteps||!memory||memory>SERIAL_BASE||(boot&&hasentry)) {
        fprintf(stderr, "invalid run limits/entry\n");
        return 2;
    }
    Program p= {
        0
    };
    if(launch&&!path&&!elf) {
#ifdef UPSILON_BOOTSTRAP
        fprintf(stderr,"bootstrap tool requires --rom; build the final emulator binary\n");return 1;
#else
        p.length=sizeof upsilon_firmware_image;p.data=malloc(p.length);
        if(!p.data){fprintf(stderr,"out of memory\n");return 1;}
        memcpy(p.data,upsilon_firmware_image,p.length);p.origin=p.entry=4096;
#endif
    }
    else if(binary||elf) {
        size_t len;
        char *data=readfile(path, &len);
        if(!data) {
            perror(path);
            return 1;
        }
        p.data=(uint8_t*)data;
        p.length=len;
        p.origin=p.entry=origin;
    }
    else {
        char *source=sourcefile(path, 0);
        if(!source) {
            perror(path);
            return 1;
        }
        if(!assemble(source, origin, 0, &p)) {
            fprintf(stderr, "upsilon: %s\n", p.error);
            free(source);
            free_program(&p);
            return 1;
        }
        free(source);
    }
    CPU *c=calloc(1, sizeof *c);
    if(!c||!cpu_init(c, (size_t)memory)) {
        free(c);
        free_program(&p);
        return 1;
    }
    if(!elf&&(p.origin>memory||p.length>memory-p.origin)) {
        fprintf(stderr, "image outside RAM\n");
        cpu_destroy(c);
        free(c);
        free_program(&p);
        return 1;
    }
    if(elf){
        if(!platform_load_elf(c,p.data,p.length,bootargs,c->error,sizeof c->error)){
            fprintf(stderr,"upsilon: %s\n",c->error);cpu_destroy(c);free(c);free_program(&p);return 1;
        }
    }else memcpy(c->ram+p.origin, p.data, p.length);
    if(!elf)c->s.pc=hasentry?entry:p.entry;
    c->serial=serial||elf;
    c->keyboard=keyboard;
    c->traps=traps||elf;
    c->serial_out=serial?(json?stderr:stdout):NULL;
    if(tapefile&&!tape_open(&c->tape,tapefile,autoboot,c->error,sizeof c->error)) {
        fprintf(stderr,"upsilon: %s\n",c->error);cpu_destroy(c);free(c);free_program(&p);return 1;
    }
    for(int i=0;i<ndisks;i++)if(!disk_open(&c->disks[i].media,diskfiles[i],disk_ro,autoboot,c->error,sizeof c->error)){
        fprintf(stderr,"upsilon: %s\n",c->error);cpu_destroy(c);free(c);free_program(&p);return 1;
    }
    if(tape_ro){c->tape.writable=0;if(c->tape.lock_path&&c->tape.lock_fd>=0){close(c->tape.lock_fd);c->tape.lock_fd=-1;}}
    if(boot) {
        c->rom_start=p.origin;
        c->rom_end=p.origin+p.length;
        if(!cpu_reset(c, p.origin)) {
            fprintf(stderr, "invalid reset header\n");
            return 1;
        }
    }
    free_program(&p);
    if(*input) {
        if(!serial) {
            fprintf(stderr, "--serial-input requires --serial\n");
            cpu_destroy(c);
            free(c);
            return 2;
        }
        cpu_feed(c, (const uint8_t*)input, strlen(input));
        if(!keyboard)c->eof=1;
    }
    for(int i=0;i<nset;i++) {
        char name[64];
        const char *eq=strchr(setregs[i], '=');
        if(!eq||eq-setregs[i]>63) {
            fprintf(stderr, "invalid --set\n");
            return 2;
        }
        memcpy(name, setregs[i], (size_t)(eq-setregs[i]));
        name[eq-setregs[i]]=0;
        uint64_t value=number(eq+1);
        if(name[0]=='R'&&isdigit((unsigned char)name[1])) {
            int reg=atoi(name+1);
            if(reg>31)return 2;
            c->s.r[reg]=value;
        }
        else if(name[0]=='F'&&isdigit((unsigned char)name[1])) {
            int reg=atoi(name+1);
            if(reg>31)return 2;
            c->s.f[reg]=value;
        }
        else if(!strcmp(name, "PSR"))c->s.psr=value;
        else if(!strcmp(name, "FPSR"))c->s.fpsr=value;
        else {
            int id=system_id(name);
            if(id<0)return 2;
            c->s.sys[id]=value;
        }
    }
    if(keyboard&&isatty(STDIN_FILENO)) {
        if(tcgetattr(STDIN_FILENO, &terminal)==0) {
            struct termios t=terminal;
            t.c_lflag&=(tcflag_t)~(ICANON|ECHO);
            t.c_cc[VMIN]=1;
            t.c_cc[VTIME]=0;
            if(tcsetattr(STDIN_FILENO, TCSADRAIN, &t)==0) {
                terminal_saved=1;
                atexit(restore_terminal);
            }
        }
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    int ok=1;
    uint64_t iterations=0;
    while(cpu_runnable(c)&&!interrupted&&iterations++<maxsteps) {
        Ins in;
        if(!cpu_step(c, &in)) {
            ok=0;
            fprintf(stderr, "upsilon at PC 0x%llx: vector %d, address 0x%llx: %s\n", (unsigned long long)c->s.pc, c->vector, (unsigned long long)c->faultaddr, c->error);
            break;
        }
        if(trace&&in.op) {
            char text[1024];
            format_ins(&in, text, sizeof text);
            fprintf(stderr, "%016llx: %-48s R0=%016llx PSR=%04llx\n", (unsigned long long)in.start, text, (unsigned long long)c->s.r[0], (unsigned long long)c->s.psr);
        }
    }
    restore_terminal();
    if(interrupted) {
        fprintf(stderr, "\nupsilon: interrupted\n");
        ok=0;
    }
    else if(ok&&cpu_runnable(c)) {
        fprintf(stderr, "upsilon: execution limit reached at 0x%llx\n", (unsigned long long)c->s.pc);
        ok=0;
    }
    if(json)json_state(stdout, c);
    else if(ok) {
        FILE *f=serial?stderr:stdout;
        fprintf(f, "%s after %llu steps; PC=0x%llx; PSR=0x%llx\n", c->s.halted?"halted":"waiting", (unsigned long long)c->steps, (unsigned long long)c->s.pc, (unsigned long long)c->s.psr);
        for(int i=0;i<32;i++)fprintf(f, "R%02d=%016llx%s", i, (unsigned long long)c->s.r[i], i%4==3?"\n":"  ");
    }
    if(statepath) {
        FILE *f=fopen(statepath, "w");
        if(!f)ok=0;
        else {
            json_state(f, c);
            fclose(f);
        }
    }
    if(dump&&!writefile(dump, c->ram, c->ram_size))ok=0;
    cpu_destroy(c);
    free(c);
    return interrupted?130:ok?0:1;
}
