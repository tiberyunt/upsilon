#include "assembler.h"
#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <errno.h>
typedef struct {
    uint64_t i;
    double f;
    int floating;
} Value;
typedef struct {
    Program *p;
    char *source;
    int pass, line;
    size_t cap, limit;
    uint64_t pc, location;
    int defined[4096];
    jmp_buf fail;
} Asm;
static _Noreturn void fail(Asm *a, const char *fmt, ...) {
    char msg[380];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    snprintf(a->p->error, sizeof a->p->error, "line %d: %s", a->line, msg);
    longjmp(a->fail, 1);
}
static char *trim(char *s) {
    while(isspace((unsigned char)*s))s++;
    char *e=s+strlen(s);
    while(e>s&&isspace((unsigned char)e[-1]))*--e=0;
    return s;
}
static void upper(char *s) {
    for(;*s;s++)*s=(char)toupper((unsigned char)*s);
}
static uint64_t mask(int n) {
    return n==8?UINT64_MAX:(UINT64_C(1)<<(8*n))-1;
}
static int64_t sext(uint64_t v, int n) {
    return n==8?(int64_t)v:(int64_t)(v<<(64-8*n))>>(64-8*n);
}
static void emit(Asm *a, uint64_t value, int n) {
    if(n<0||a->p->length+(size_t)n>a->limit)fail(a, "output exceeds %zu-byte limit", a->limit);
    if(a->p->length+(size_t)n>a->cap) {
        size_t c=a->cap?a->cap*2:4096;
        while(c<a->p->length+(size_t)n)c*=2;
        uint8_t *p=realloc(a->p->data, c);
        if(!p)fail(a, "out of memory");
        a->p->data=p;
        a->cap=c;
    }
    for(int i=0;i<n;i++)a->p->data[a->p->length++]=(uint8_t)(value>>(8*i));
    a->pc+=n;
}
static void patch(Asm *a, size_t at, uint64_t v, int n) {
    for(int i=0;i<n;i++)a->p->data[at+i]=(uint8_t)(v>>(i*8));
}
static int symbol(Asm *a, const char *name, int create) {
    for(size_t i=0;i<a->p->nsymbols;i++)if(!strcmp(name, a->p->symbols[i].name))return (int)i;
    if(!create)return -1;
    if(a->p->nsymbols==4096)fail(a, "too many symbols");
    size_t n=a->p->nsymbols++;
    a->p->symbols=realloc(a->p->symbols, (n+1)*sizeof(Symbol));
    if(!a->p->symbols)fail(a, "out of memory");
    snprintf(a->p->symbols[n].name, 96, "%s", name);
    a->p->symbols[n].value=0;
    return (int)n;
}
static void define(Asm *a, const char *name, uint64_t value) {
    size_t length=strlen(name);
    if(!length || length>95 || !(isalpha((unsigned char)name[0]) || name[0]=='_'))
        fail(a,"invalid symbol name");
    for(size_t at=1;at<length;at++)
        if(!isalnum((unsigned char)name[at]) && name[at]!='_')fail(a,"invalid symbol name");
    int i=symbol(a, name, 1);
    if(a->defined[i])fail(a, "duplicate symbol %s", name);
    a->defined[i]=1;
    a->p->symbols[i].value=value;
}
static Value integer(uint64_t v) {
    Value x= {
        v, (double)v, 0
    };
    return x;
}
typedef struct {
    Asm *a;
    const char *p;
    int depth;
} Expr;
static void space(Expr *e) {
    while(isspace((unsigned char)*e->p))e->p++;
}
static Value expression(Expr *e, int min);
static int escape(Expr *e) {
    int c=(unsigned char)*e->p++;
    if(!c)fail(e->a,"unterminated character escape");
    if(c!='\\')return c;
    c=(unsigned char)*e->p++;
    if(!c)fail(e->a,"unterminated character escape");
    switch(c) {
        case 'n':
        return 10;
        case 'r':
        return 13;
        case 't':
        return 9;
        case '0':
        return 0;
        case 'x':
        {
            int v=0;
            for(int i=0;i<2;i++) {
                int ch=tolower((unsigned char)*e->p++);
                if(!isxdigit(ch))fail(e->a, "invalid hex escape");
                v=v*16+(isdigit(ch)?ch-'0':ch-'a'+10);
            }
            return v;
        }
        default:
        return c;
    }
}
static Value primary(Expr *e) {
    space(e);
    if(++e->depth>64)fail(e->a, "expression nesting too deep");
    Value v;
    char c=*e->p;
    if(c=='+'||c=='-'||c=='~') {
        e->p++;
        v=primary(e);
        if(c=='-') {
            v.i=0-v.i;
            v.f=-v.f;
        }
        if(c=='~') {
            if(v.floating)fail(e->a, "integer required");
            v.i=~v.i;
        }
    }
    else if(c=='(') {
        e->p++;
        v=expression(e, 0);
        space(e);
        if(*e->p++!=')')fail(e->a, "expected closing parenthesis");
    }
    else if(c=='\''||c=='"') {
        e->p++;
        v=integer((uint64_t)escape(e));
        if(*e->p++!=c)fail(e->a, "expected one character");
    }
    else if(c=='.'&&!isdigit((unsigned char)e->p[1])) {
        e->p++;
        v=integer(e->a->location);
    }
    else if(isdigit((unsigned char)c)||c=='.') {
        const char *start=e->p;
        int fp=0;
        if(start[0]=='0'&&(start[1]=='x'||start[1]=='X'||start[1]=='b'||start[1]=='B')) {
            int base=tolower(start[1])=='x'?16:2;
            const char *q=start+2;
            uint64_t n=0;
            int any=0;
            while(isxdigit((unsigned char)*q)) {
                int d=isdigit((unsigned char)*q)?*q-'0':tolower((unsigned char)*q)-'a'+10;
                if(d>=base)break;
                if(n>(UINT64_MAX-(unsigned)d)/(unsigned)base)fail(e->a, "integer constant out of range");
                n=n*(unsigned)base+(unsigned)d;
                q++;
                any=1;
            }
            if(!any)fail(e->a, "invalid integer constant");
            e->p=q;
            v=integer(n);
        }
        else {
            const char *q=start;
            while(isdigit((unsigned char)*q))q++;
            if(*q=='.'||*q=='e'||*q=='E')fp=1;
            char *end;
            if(fp) {
                double d=strtod(start, &end);
                v=(Value) {
                    0, d, 1
                };
                e->p=end;
            }
            else {
                errno=0;
                uint64_t n=strtoull(start, &end, 10);
                if(errno)fail(e->a, "integer constant out of range");
                e->p=end;
                v=integer(n);
            }
        }
    }
    else if(isalpha((unsigned char)c)||c=='_') {
        char name[96];
        int n=0;
        while(isalnum((unsigned char)*e->p)||*e->p=='_') {
            if(n==95)fail(e->a, "symbol too long");
            name[n++]=*e->p++;
        }
        name[n]=0;
        if(!strcmp(name, "nan")||!strcmp(name, "inf")) {
            v=(Value) {
                0, !strcmp(name, "nan")?NAN:INFINITY, 1
            };
        }
        else {
            int i=symbol(e->a, name, 0);
            if(i<0&&e->a->pass)fail(e->a, "undefined symbol %s", name);
            v=integer(i<0?0:e->a->p->symbols[i].value);
        }
    }
    else fail(e->a, "expected expression near %.24s", e->p);
    e->depth--;
    return v;
}
static Value expression(Expr *e, int min) {
    Value l=primary(e);
    for(;;) {
        space(e);
        int op=*e->p, prec=op=='|'?1:op=='^'?2:op=='&'?3:(op=='<'&&e->p[1]=='<')||(op=='>'&&e->p[1]=='>')?4:op=='+'||op=='-'?5:op=='*'||op=='%'||(op=='/'&&e->p[1]=='/')?6:0;
        if(!prec||prec<min)break;
        e->p+=op=='<'||op=='>'||op=='/'?2:1;
        Value r=expression(e, prec+1);
        if(l.floating||r.floating) {
            double a=l.floating?l.f:(double)(int64_t)l.i, b=r.floating?r.f:(double)(int64_t)r.i;
            if(op=='+')l=(Value) {
                0, a+b, 1
            };
            else if(op=='-')l=(Value) {
                0, a-b, 1
            };
            else if(op=='*')l=(Value) {
                0, a*b, 1
            };
            else fail(e->a, "integer operator applied to float");
        }
        else switch(op) {
            case '+':
            l.i+=r.i;
            break;
            case '-':
            l.i-=r.i;
            break;
            case '*':
            l.i*=r.i;
            break;
            case '|':
            l.i|=r.i;
            break;
            case '^':
            l.i^=r.i;
            break;
            case '&':
            l.i&=r.i;
            break;
            case '<':
            case '>':
            if(r.i>128)fail(e->a, "shift out of range");
            l.i=r.i>=64?0:op=='<'?l.i<<r.i:l.i>>r.i;
            break;
            case '/':
            case '%':
            {
                if(!r.i)fail(e->a, "division by zero in expression");
                i128 x=(int64_t)l.i, y=(int64_t)r.i, q=x/y, rem=x%y;
                if(rem&&((x<0)!=(y<0)))q--;
                l.i=(uint64_t)(op=='/'?q:x-q*y);
                break;
            }
        }
    }
    return l;
}
static Value eval(Asm *a, const char *text) {
    Expr e= {
        a, text, 0
    };
    Value v=expression(&e, 0);
    space(&e);
    if(*e.p)fail(a, "unexpected expression text %.24s", e.p);
    return v;
}
static uint64_t evint(Asm *a, const char *s) {
    Value v=eval(a, s);
    if(v.floating)fail(a, "integer expression required");
    return v.i;
}
static int argsplit(Asm *a, char *s, char **args) {
    int count=0, depth=0, quote=0, esc=0;
    char *start=s;
    for(char *p=s;;p++) {
        int c=*p;
        if(quote) {
            if(esc)esc=0;
            else if(c=='\\')esc=1;
            else if(c==quote)quote=0;
        }
        else if(c=='\''||c=='"')quote=c;
        else if(c=='('||c=='[')depth++;
        else if(c==')'||c==']')depth--;
        if((!quote&&!depth&&c==',')||!c) {
            if(count==1024)fail(a, "too many arguments");
            if(c)*p=0;
            char *x=trim(start);
            if(*x)args[count++]=x;
            else if(c)fail(a, "empty argument");
            start=p+1;
        }
        if(!c)break;
        if(depth<0)fail(a, "unbalanced brackets");
    }
    if(depth||quote)fail(a, "unbalanced brackets or quotes");
    return count;
}
static int regnum(Asm *a, char *s, int fp) {
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%s", trim(s));
    upper(tmp);
    if(!strcmp(tmp, "SP"))return fp?-2:31;
    if(!strcmp(tmp, "FP"))return fp?-2:30;
    if((tmp[0]=='R'||tmp[0]=='F')&&isdigit((unsigned char)tmp[1])) {
        char *end;
        long n=strtol(tmp+1, &end, 10);
        if(!*end) {
            if(n>31||((tmp[0]=='F')!=fp))fail(a, "invalid register %s", s);
            return (int)n;
        }
    }
    return -1;
}
static void operand(Asm *a, char *text, int size, int fp, size_t *patch_at, uint64_t *patch_target, int indexed) {
    char *s=trim(text);
    size_t len=strlen(s);
    int r=regnum(a, s, fp);
    if(r>=0) {
        emit(a, (uint64_t)r, 1);
        return;
    }
    if(r==-2)fail(a, "invalid floating register");
    if(len&&s[len-1]==']'&&s[0]!='#') {
        char *open=strrchr(s, '[');
        if(open&&open>s) {
            *open=0;
            s[len-1]=0;
            char *x=trim(open+1);
            int idx=regnum(a, x, 0);
            if(idx>=0) {
                if(indexed)fail(a, "nested index prefix");
                emit(a, 0xc0+idx, 1);
                size_t base=a->p->length;
                operand(a, s, size, fp, patch_at, patch_target, 1);
                int mode=a->p->data[base]>>5, field=a->p->data[base]&31;
                if(mode!=1&&mode!=4&&mode!=5&&!(mode==7&&field>=28))fail(a, "invalid indexed base");
                return;
            }
            *open='[';
            s[len-1]=']';
        }
    }
    if(*s=='#') {
        Value v=eval(a, s+1);
        if(fp) {
            double d=v.floating?v.f:(double)(int64_t)v.i;
            for(int i=0;i<24;i++)if((isnan(d)&&isnan(fp_constants[i]))||(d==fp_constants[i]&&(d!=0||signbit(d)==signbit(fp_constants[i])))) {
                emit(a, 0xe0+i, 1);
                return;
            }
            float f=(float)d;
            if((double)f==d||isnan(d)) {
                uint32_t b;
                memcpy(&b, &f, 4);
                emit(a, 0xfa, 1);
                emit(a, b, 4);
            }
            else {
                uint64_t b;
                memcpy(&b, &d, 8);
                emit(a, 0xfb, 1);
                emit(a, b, 8);
            }
            return;
        }
        if(v.floating)fail(a, "integer immediate required");
        uint64_t n=v.i;
        if(size<8&&n>mask(size)&&((int64_t)n>=0||(int64_t)n<-(INT64_C(1)<<(size*8-1))))fail(a, "immediate does not fit operand");
        if(n<=23) {
            emit(a, 0xe0+n, 1);
            return;
        }
        int64_t value=sext(n, size);
        int w=value>=-128&&value<=127?1:value>=-32768&&value<=32767?2:value>=INT32_MIN&&value<=INT32_MAX?4:8;
        emit(a, 0xe0+24+(w==1?0:w==2?1:w==4?2:3), 1);
        emit(a, (uint64_t)value, w);
        return;
    }
    int star=*s=='*';
    if(star){s=trim(s+1);len=strlen(s);}
    int indirect=star||(*s=='['&&len>1&&s[len-1]==']');
    if(indirect&&!star) {
        s[len-1]=0;
        s=trim(s+1);
        len=strlen(s);
    }
    if(*s=='-'&&s[1]=='('&&s[len-1]==')') {
        s[len-1]=0;
        r=regnum(a, s+2, 0);
        if(r<0||indirect)fail(a, "invalid autodecrement");
        emit(a, 0x60+r, 1);
        return;
    }
    int inc=len&&s[len-1]=='+';
    if(inc) {
        s[--len]=0;
        len=strlen(s);
    }
    char *open=strrchr(s, '(');
    if(open&&len&&s[len-1]==')') {
        s[len-1]=0;
        *open=0;
        char *base=trim(open+1);
        char reg[32];
        snprintf(reg, sizeof reg, "%s", base);
        upper(reg);
        char *disp=trim(s);
        if(!strcmp(reg, "PC")) {
            if(inc||indirect)fail(a, "invalid PC mode");
            uint64_t target=*disp?evint(a, disp):0;
            emit(a, 0xfe, 1);
            *patch_at=a->p->length;
            *patch_target=target;
            emit(a, 0, 4);
            return;
        }
        if(!strcmp(reg, "TP")) {
            if(inc||indirect)fail(a, "invalid TP mode");
            int64_t d=(int64_t)(*disp?evint(a, disp):0);
            if(d<INT32_MIN||d>INT32_MAX)fail(a, "TP displacement out of range");
            emit(a, 0xff, 1);emit(a, 0x60, 1);emit(a, (uint64_t)d, 4);
            return;
        }
        r=regnum(a, base, 0);
        if(r<0)fail(a, "invalid base register");
        if(!*disp&&!indirect) {
            emit(a, (inc?0x40:0x20)+r, 1);
            return;
        }
        if(inc)fail(a, "autoincrement requires bare register deferred");
        int64_t d=(int64_t)(*disp?evint(a, disp):0);
        if(indirect) {
            if(d<INT32_MIN||d>INT32_MAX)fail(a, "indirect displacement out of range");
            emit(a, 0xff, 1);
            emit(a, 0x40+r, 1);
            emit(a, (uint64_t)d, 4);
        }
        else if(d>=-128&&d<=127) {
            emit(a, 0x80+r, 1);
            emit(a, (uint64_t)d, 1);
        }
        else if(d>=-32768&&d<=32767) {
            emit(a, 0xa0+r, 1);
            emit(a, (uint64_t)d, 2);
        }
        else {
            emit(a, 0xff, 1);
            int w=d>=INT32_MIN&&d<=INT32_MAX?4:8;
            emit(a, (w==8?0x20:0)+r, 1);
            emit(a, (uint64_t)d, w);
        }
        return;
    }
    if(indirect||inc)fail(a, "invalid memory operand");
    if(*s=='@') {
        uint64_t n=evint(a, s+1);
        int w=(int64_t)n>=INT32_MIN&&(int64_t)n<=INT32_MAX?4:8;
        emit(a, w==4?0xfc:0xfd, 1);
        emit(a, n, w);
        return;
    }
    uint64_t target=evint(a, s);
    emit(a, 0xfe, 1);
    *patch_at=a->p->length;
    *patch_target=target;
    emit(a, 0, 4);
}
static int sizecode(Asm *a, const char *s, int fp) {
    if(!s||strlen(s)!=1)fail(a, "invalid size suffix");
    char c=(char)tolower((unsigned char)*s);
    int n=fp?(c=='h'?2:c=='s'?4:c=='d'?8:0):(c=='b'?1:c=='w'?2:c=='l'?4:c=='q'?8:0);
    if(!n)fail(a, "invalid size suffix (binary128 is not implemented)");
    return n;
}
static int logsize(int n) {
    return n==1?0:n==2?1:n==4?2:3;
}
static void instruction(Asm *a, char *mnemonic, char **args, int count) {
    char *suf[3]= {
        0
    };
    int ns=0;
    for(char *p=mnemonic;*p;p++)if(*p=='.') {
        *p=0;
        if(ns==3)fail(a, "too many suffixes");
        suf[ns++]=p+1;
    }
    upper(mnemonic);
    const Op *o=find_op(mnemonic);
    if(!o)fail(a, "unknown instruction %s", mnemonic);
    int size=8, aux=0, f=o->family, n=o->op;
    if(f==F_CONV) {
        char compact[2][2]={{0}};
        if(ns==1&&strlen(suf[0])==2){compact[0][0]=suf[0][0];compact[1][0]=suf[0][1];suf[0]=compact[0];suf[1]=compact[1];ns=2;}
        if(ns!=2)fail(a, "conversion requires two sizes");
        size=sizecode(a, suf[0], fp_operand(o, 0));
        aux=sizecode(a, suf[1], fp_operand(o, 1));
        if((n==8||n==9)&&size!=aux)fail(a, "raw move size mismatch");
    }
    else if(ns) {
        if(ns!=1)fail(a, "invalid suffix");
        size=sizecode(a, suf[0], f==F_FP);
    }
    int sized=f==F_TWO||f==F_THREE||f==F_FP||f==F_BLOCK||f==F_ATOMIC||f==F_BITS||f==F_SET||f==F_CMOV;
    if(!sized&&f!=F_CONV&&size!=8)fail(a, "instruction has no size variants");
    if(f==F_TWO&&n==20&&size!=8)fail(a, "LEA is quad only");
    if(f==F_BITS&&n==3&&size==1)fail(a, "byte BSWAP is reserved");
    if(f==F_ATOMIC&&n==6&&size!=8)fail(a, "CAS2 has a fixed 16-byte width (use no suffix or .q)");
    int branch=f==F_BRANCH||f==F_BITBR||f==F_LOOP;
    if(count!=o->count+branch)fail(a, "%s expects %d operands", o->name, o->count+branch);
    size_t start=a->p->length;
    uint64_t ins_pc=a->pc;
    int sc=logsize(size);
    switch(f) {
        case F_TWO:
        emit(a, n*4+sc, 1);
        break;
        case F_THREE:
        emit(a, 0x80+n*4+sc, 1);
        break;
        case F_BRANCH:
        emit(a, 0xc0+n*2, 1);
        break;
        case F_SHORT:
        emit(a, 0xe0+n, 1);
        break;
        case F_SET:
        case F_CMOV:
        emit(a, 0xf8, 1);emit(a, (f==F_CMOV?0x40:0)+n*4+sc, 1);break;
        case F_HINT:
        emit(a, 0xf8, 1);emit(a, n, 1);break;
        case F_FP:
        emit(a, 0xf0, 1);
        emit(a, n*4+sc-1, 1);
        break;
        case F_FPSR:
        emit(a, 0xf0, 1);
        emit(a, n, 1);
        break;
        case F_BLOCK:
        case F_ATOMIC:
        case F_BITS:
        emit(a, f==F_BLOCK?0xf1:f==F_ATOMIC?0xf3:0xf5, 1);
        emit(a, n*4+sc, 1);
        break;
        case F_FIELD:
        case F_SYSTEM:
        case F_CASE:
        emit(a, f==F_FIELD?0xf2:f==F_SYSTEM?0xf6:0xf7, 1);
        emit(a, n, 1);
        break;
        case F_BITBR:
        emit(a, 0xf2, 1);
        emit(a, 4+n*2, 1);
        break;
        case F_LOOP:
        emit(a, 0xf7, 1);
        emit(a, n*2, 1);
        break;
        case F_CONV:
        emit(a, 0xf4, 1);
        emit(a, n*16+(sc-fp_operand(o, 0))*4+logsize(aux)-fp_operand(o, 1), 1);
        break;
    }
    size_t patches[4]= {
        0
    };
    uint64_t targets[4]= {
        0
    };
    for(int i=0;i<o->count;i++) {
        char special[64];
        char *text=args[i];
        if(f==F_SYSTEM&&(n==0&&i==0||n==1&&i==1)) {
            snprintf(special, sizeof special, "%s", text);
            upper(special);
            int id=system_id(special);
            if(id>=0) {
                snprintf(special, sizeof special, "#%d", id);
                text=special;
            }
        }
        size_t pos=a->p->length;
        operand(a, text, operand_size(o, size, aux, i), fp_operand(o, i), &patches[i], &targets[i], 0);
        if((f==F_SYSTEM&&(n==0&&i==0||n==1&&i==1)||f==F_CASE&&i==2)&&(a->p->data[pos]<0xe0||a->p->data[pos]>0xfb))fail(a, "operand must be a literal or immediate");
    }
    uint64_t target=0;
    int width=0;
    if(branch) {
        target=evint(a, args[count-1]);
        int64_t d=(int64_t)(target-(a->pc+1));
        width=!a->pass||(d>=-128&&d<=127)?1:4;
        a->p->data[start+(f==F_BRANCH?0:1)]|=width==4;
    }
    uint64_t end=a->pc+width;
    for(int i=0;i<4;i++)if(patches[i]) {
        int64_t d=(int64_t)(targets[i]-end);
        if(d<INT32_MIN||d>INT32_MAX)fail(a, "PC-relative target out of range");
        patch(a, patches[i], (uint64_t)d, 4);
    }
    if(branch) {
        int64_t d=(int64_t)(target-end);
        if(a->pass&&(d<INT32_MIN||d>INT32_MAX))fail(a, "branch target out of range");
        emit(a, a->pass?(uint64_t)d:0, width);
    }
    if(a->pc-ins_pc>48)fail(a, "instruction exceeds 48 bytes");
}
static void stripcomment(char *s) {
    int q=0, esc=0;
    for(;*s;s++) {
        if(q) {
            if(esc)esc=0;
            else if(*s=='\\')esc=1;
            else if(*s==q)q=0;
        }
        else if(*s=='\''||*s=='"')q=*s;
        else if(*s==';') {
            *s=0;
            return;
        }
    }
}
static void line(Asm *a, char *text, uint64_t *entry) {
    a->location=a->pc;
    stripcomment(text);
    char *s=trim(text);
    while(isalpha((unsigned char)*s)||*s=='_') {
        char *p=s;
        while(isalnum((unsigned char)*p)||*p=='_')p++;
        if(*p!=':')break;
        *p=0;
        define(a, s, a->pc);
        s=trim(p+1);
    }
    if(!*s)return;
    char *word=s;
    while(*s&&!isspace((unsigned char)*s))s++;
    if(*s)*s++=0;
    char *args[1024];
    int n=argsplit(a, trim(s), args);
    if(*word!='.') {
        instruction(a, word, args, n);
        return;
    }
    upper(word);
    if(!strcmp(word, ".TEXT")||!strcmp(word, ".DATA")||!strcmp(word, ".BSS")) {
        if(n)fail(a, "section marker takes no arguments");
        return;
    }
    if(!strcmp(word, ".GLOBL")) {
        if(!n)fail(a, ".globl requires a symbol");
        for(int i=0;i<n;i++)for(char *p=args[i];*p;p++)if(!(isalnum((unsigned char)*p)||*p=='_'))fail(a, "invalid global symbol");
        return;
    }
    if(!strcmp(word, ".EQU")) {
        if(n!=2)fail(a, ".equ requires name, expression");
        define(a, args[0], evint(a, args[1]));
        return;
    }
    if(!strcmp(word, ".ENTRY")) {
        if(n!=1)fail(a, ".entry requires an expression");
        *entry=evint(a, args[0]);
        return;
    }
    int width=!strcmp(word, ".BYTE")?1:!strcmp(word, ".WORD")?2:!strcmp(word, ".LONG")?4:!strcmp(word, ".QUAD")?8:0;
    if(width) {
        for(int i=0;i<n;i++) {
            uint64_t v=evint(a, args[i]);
            if(width<8&&v>mask(width)&&((int64_t)v>=0||(int64_t)v<-(INT64_C(1)<<(width*8-1))))fail(a, "data does not fit width");
            emit(a, v, width);
        }
        return;
    }
    if(!strcmp(word, ".ASCII")||!strcmp(word, ".ASCIZ")) {
        for(int i=0;i<n;i++) {
            Expr e= {
                a, args[i], 0
            };
            char q=*e.p++;
            if(q!='"'&&q!='\'')fail(a, "string required");
            while(*e.p&&*e.p!=q)emit(a, (uint64_t)escape(&e), 1);
            if(*e.p++!=q||*trim((char*)e.p))fail(a, "invalid string");
            if(!strcmp(word, ".ASCIZ"))emit(a, 0, 1);
        }
        return;
    }
    if(!strcmp(word, ".SPACE")||!strcmp(word, ".ORG")||!strcmp(word, ".ALIGN")) {
        if(n<1||n>2)fail(a, "padding requires value[,fill]");
        uint64_t v=evint(a, args[0]);
        if(!strcmp(word, ".ORG")) {
            if(v<a->pc)fail(a, ".org cannot go backwards");
            v-=a->pc;
        }
        if(!strcmp(word, ".ALIGN")) {
            if(!v)fail(a, "alignment must be positive");
            v=(v-a->pc%v)%v;
        }
        if(v>a->limit-a->p->length)fail(a, "output exceeds %zu-byte limit", a->limit);
        uint64_t fill=n==2?evint(a, args[1]):0;
        if(fill>255&&((int64_t)fill>=0||(int64_t)fill< -128))fail(a, "fill byte out of range");
        for(uint64_t i=0;i<v;i++)emit(a, fill, 1);
        return;
    }
    fail(a, "unknown directive %s", word);
}
int assemble(const char *source, uint64_t origin, size_t limit, Program *out) {
    memset(out, 0, sizeof *out);
    out->origin=origin;
    Asm *a=calloc(1, sizeof *a);
    if(!a) {
        snprintf(out->error, sizeof out->error, "out of memory");
        return 0;
    }
    a->p=out;
    a->limit=limit?limit:16*1024*1024;
    uint8_t *volatile old=NULL;
    size_t oldlen=0;
    uint64_t oldentry=0;
    uint64_t *values=calloc(4096, sizeof(uint64_t));
    size_t oldns=0;
    if(setjmp(a->fail)) {
        free(a->source);
        free(old);
        free(values);
        free(a);
        return 0;
    }
    for(a->pass=0;a->pass<128;a->pass++) {
        memset(a->defined, 0, sizeof a->defined);
        out->length=0;
        a->pc=origin;
        out->entry=origin;
        a->source=strdup(source);
        if(!a->source)fail(a, "out of memory");
        char *s=a->source;
        a->line=1;
        while(s) {
            char *next=strchr(s, '\n');
            if(next)*next++=0;
            line(a, s, &out->entry);
            s=next;
            a->line++;
        }
        free(a->source);
        a->source=NULL;
        int same=a->pass&&oldlen==out->length&&oldentry==out->entry&&oldns==out->nsymbols&&(!oldlen||!memcmp(old, out->data, oldlen));
        for(size_t i=0;i<out->nsymbols;i++) {
            if(values[i]!=out->symbols[i].value)same=0;
            values[i]=out->symbols[i].value;
        }
        if(same) {
            free(old);
            free(values);
            free(a);
            return 1;
        }
        old=realloc(old, out->length?out->length:1);
        if(!old)fail(a, "out of memory");
        if(out->length)memcpy(old, out->data, out->length);
        oldlen=out->length;
        oldentry=out->entry;
        oldns=out->nsymbols;
    }
    fail(a, "label relaxation did not converge");
    return 0;
}
void free_program(Program *p) {
    free(p->data);
    free(p->symbols);
    memset(p, 0, sizeof *p);
}
