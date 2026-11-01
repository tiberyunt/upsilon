#include "upsilon.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
static const Op *lookup(int f, int n) {
    for(size_t i=0;i<nops;i++)if(ops[i].family==f&&ops[i].op==n)return &ops[i];
    return NULL;
}
static uint64_t get(CPU *c, uint64_t *p, uint64_t start, int n, int sign) {
    if(*p-start+(unsigned)n>48)cpu_fault(c, 1, start, 0, "instruction exceeds 48 bytes");
    uint64_t v=cpu_read(c, *p, n, 4);
    *p+=n;
    if(sign&&n<8)v=(uint64_t)((int64_t)(v<<(64-n*8))>>(64-n*8));
    return v;
}
static Operand operand(CPU *c, uint64_t *pc, uint64_t start, int indexed) {
    int b=(int)get(c, pc, start, 1, 0);
    Operand o= {
        b>>5, b&31, -1, 0, 0
    };
    if(o.mode==6) {
        if(indexed)cpu_fault(c, 2, start, 0, "nested index prefix");
        Operand base=operand(c, pc, start, 1);
        if(base.mode!=1&&base.mode!=4&&base.mode!=5&&!(base.mode==7&&(base.kind||base.reg>=28)))cpu_fault(c, 2, start, 0, "invalid indexed base");
        base.index=o.reg;
        return base;
    }
    if(o.mode==4||o.mode==5)o.value=get(c, pc, start, o.mode==4?1:2, 1);
    else if(o.mode==7&&o.reg>=24) {
        int r=o.reg;
        if(r<28)o.value=get(c, pc, start, 1<<(r-24), 1);
        else if(r<31)o.value=get(c, pc, start, r==29?8:4, r!=29);
        else {
            int b2=(int)get(c, pc, start, 1, 0), kind=b2>>5;
            if(kind>3)cpu_fault(c, 2, start, 0, "reserved long mode");
            o.reg=b2&31;
            o.kind=kind+1;
            o.value=get(c, pc, start, kind==1?8:4, 1);
        }
    }
    return o;
}
int cpu_decode(CPU *c, uint64_t pc, Ins *in) {
    memset(in, 0, sizeof *in);
    in->start=pc;
    in->size=8;
    int b=(int)get(c, &pc, in->start, 1, 0), f=-1, n=-1, w=0;
    if(b<0x80) {
        f=F_TWO;
        n=b>>2;
        in->size=1<<(b&3);
        if(n==20&&in->size!=8)cpu_fault(c, 1, in->start, 0, "LEA is quad only");
    }
    else if(b<0xc0) {
        f=F_THREE;
        n=(b-0x80)>>2;
        in->size=1<<(b&3);
    }
    else if(b<0xe0) {
        f=F_BRANCH;
        n=(b-0xc0)>>1;
        w=b&1?4:1;
    }
    else if(b<0xf0) {
        f=F_SHORT;
        n=b-0xe0;
    }
    else if(b<=0xf8) {
        int x=(int)get(c, &pc, in->start, 1, 0);
        switch(b) {
            case 0xf0:
            if(x==112||x==116) {
                f=F_FPSR;
                n=x;
            }
            else {
                if((x&3)==3)cpu_fault(c, 1, in->start, 0, "binary128 not implemented");
                f=F_FP;
                n=x>>2;
                in->size=2<<(x&3);
            }
            break;
            case 0xf1:
            f=F_BLOCK;
            n=x>>2;
            in->size=1<<(x&3);
            break;
            case 0xf2:
            if(x<4) {
                f=F_FIELD;
                n=x;
            }
            else if(x<16) {
                f=F_BITBR;
                n=(x-4)>>1;
                w=x&1?4:1;
            }
            break;
            case 0xf3:
            f=F_ATOMIC;
            n=x>>2;
            in->size=1<<(x&3);
            break;
            case 0xf4:
            {
                f=F_CONV;
                n=x>>4;
                const Op *o=lookup(f, n);
                if(!o)break;
                int sc=(x>>2)&3, dc=x&3;
                if((fp_operand(o, 0)&&sc==3)||(fp_operand(o, 1)&&dc==3))cpu_fault(c, 1, in->start, 0, "binary128 conversion not implemented");
                in->size=1<<(sc+fp_operand(o, 0));
                in->aux=1<<(dc+fp_operand(o, 1));
                if((n==8||n==9)&&in->size!=in->aux)cpu_fault(c, 3, in->start, 0, "raw move size mismatch");
                break;
            }
            case 0xf5:
            f=F_BITS;
            n=x>>2;
            in->size=1<<(x&3);
            if(n==3&&in->size==1)cpu_fault(c, 1, in->start, 0, "byte BSWAP reserved");
            break;
            case 0xf6:
            f=F_SYSTEM;
            n=x;
            break;
            case 0xf8:
            if(x<0x80){f=x<0x40?F_SET:F_CMOV;n=(x&0x3f)>>2;in->size=1<<(x&3);}
            else {f=F_HINT;n=x;}
            break;
            case 0xf7:
            if(x<12) {
                f=F_LOOP;
                n=x>>1;
                w=x&1?4:1;
            }
            else {
                f=F_CASE;
                n=x;
            }
            break;
        }
    }
    in->op=lookup(f, n);
    if(!in->op)cpu_fault(c, 1, in->start, 0, "illegal instruction");
    if(f==F_ATOMIC&&n==6&&in->size!=8)cpu_fault(c,1,in->start,0,"reserved CAS2 size");
    in->count=in->op->count;
    for(int i=0;i<in->count;i++)in->operands[i]=operand(c, &pc, in->start, 0);
    if(w) {
        uint64_t disp=get(c, &pc, in->start, w, 1);
        in->target=pc+disp;
        in->branch=1;
    }
    in->end=pc;
    return 1;
}
static void append(char *out, size_t cap, const char *s) {
    size_t n=strlen(out);
    if(n<cap)snprintf(out+n, cap-n, "%s", s);
}
static void fmtop(const Operand *o, int size, uint64_t end, int fp, char *out, size_t cap) {
    int m=o->mode, r=o->reg;
    int64_t v=(int64_t)o->value;
    switch(m) {
        case 0:
        snprintf(out, cap, "%c%d", fp?'F':'R', r);
        break;
        case 1:
        snprintf(out, cap, "(R%d)", r);
        break;
        case 2:
        snprintf(out, cap, "(R%d)+", r);
        break;
        case 3:
        snprintf(out, cap, "-(R%d)", r);
        break;
        case 4:
        case 5:
        snprintf(out, cap, "%lld(R%d)", (long long)v, r);
        break;
        default:
        if(o->kind==4)snprintf(out, cap, "%lld(TP)", (long long)v);
        else if(o->kind)snprintf(out, cap, o->kind==3?"[%lld(R%d)]":"%lld(R%d)", (long long)v, r);
        else if(r<28) {
            if(fp) {
                double d;
                if(r<24)d=fp_constants[r];
                else if(r==26) {
                    uint32_t bits=(uint32_t)o->value;
                    float f;
                    memcpy(&f, &bits, 4);
                    d=f;
                }
                else if(r==27)memcpy(&d, &o->value, 8);
                else d=(double)v;
                if(d==0&&signbit(d))snprintf(out, cap, "#-0.0");
                else snprintf(out, cap, "#%.17g", d);
            }
            else snprintf(out, cap, "#%lld", (long long)(r<24?r:v));
        }
        else if(r<30)snprintf(out, cap, "@0x%llx", (unsigned long long)o->value);
        else snprintf(out, cap, "0x%llx(PC)", (unsigned long long)(end+o->value));
        break;
    }
    if(o->index>=0) {
        char t[32];
        snprintf(t, sizeof t, "[R%d]", o->index);
        append(out, cap, t);
    }
    (void)size;
}
void format_ins(const Ins *in, char *out, size_t cap) {
    const Op *o=in->op;
    snprintf(out, cap, "%s", o->name);
    char tail[128];
    int f=o->family;
    const char *isz="bwlq", *fsz="hsd";
    int sc=in->size==1?0:in->size==2?1:in->size==4?2:3;
    if(f==F_CONV) {
        int dc=in->aux==1?0:in->aux==2?1:in->aux==4?2:3;
        snprintf(tail, sizeof tail, ".%c.%c", fp_operand(o, 0)?fsz[sc-1]:isz[sc], fp_operand(o, 1)?fsz[dc-1]:isz[dc]);
        append(out, cap, tail);
    }
    else if(f==F_TWO||f==F_THREE||f==F_FP||f==F_BLOCK||f==F_ATOMIC||f==F_BITS||f==F_SET||f==F_CMOV) {
        snprintf(tail, sizeof tail, ".%c", f==F_FP?fsz[sc-1]:isz[sc]);
        append(out, cap, tail);
    }
    for(int i=0;i<in->count;i++) {
        append(out, cap, i?", ":" ");
        fmtop(&in->operands[i], operand_size(o, in->size, in->aux, i), in->end, fp_operand(o, i), tail, sizeof tail);
        append(out, cap, tail);
    }
    if(in->branch) {
        snprintf(tail, sizeof tail, "%s0x%llx", in->count?", ":" ", (unsigned long long)in->target);
        append(out, cap, tail);
    }
}
