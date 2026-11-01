/* Bounded exact rational arithmetic for the mandatory IEEE formats.
 * Binary64 operands/products/sums need < 8192 bits even at extreme exponents.
 * This avoids dependence on long-double width and double rounding. */
#include "upsilon.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#define LIMBS 128
typedef struct {
    uint64_t v[LIMBS];
    int n;
} Big;
typedef struct {
    Big num, den;
    int sign;
} Rat;
static void norm(Big *a) {
    while(a->n&&a->v[a->n-1]==0)a->n--;
}
static Big small(uint64_t n) {
    Big b= {
        {
            0
        }, 0
    };
    if(n) {
        b.v[0]=n;
        b.n=1;
    }
    return b;
}
static int bits(const Big *a) {
    return a->n?(a->n-1)*64+64-__builtin_clzll(a->v[a->n-1]):0;
}
static int cmp(const Big *a, const Big *b) {
    if(a->n!=b->n)return a->n>b->n?1:-1;
    for(int i=a->n-1;i>=0;i--)if(a->v[i]!=b->v[i])return a->v[i]>b->v[i]?1:-1;
    return 0;
}
static Big shift(const Big *a, int n) {
    Big b= {
        {
            0
        }, 0
    };
    if(!a->n)return b;
    int w=n/64, s=n%64;
    for(int i=0;i<a->n;i++) {
        if(i+w>=LIMBS)abort();
        b.v[i+w]|=a->v[i]<<s;
        if(s&&a->v[i]>>(64-s)) {
            if(i+w+1>=LIMBS)abort();
            b.v[i+w+1]|=a->v[i]>>(64-s);
        }
    }
    b.n=a->n+w+(s?1:0);
    if(b.n>LIMBS)b.n=LIMBS;
    norm(&b);
    return b;
}
static Big add(const Big *a, const Big *b) {
    Big r= {
        {
            0
        }, 0
    };
    int n=a->n>b->n?a->n:b->n;
    u128 carry=0;
    for(int i=0;i<n;i++) {
        u128 z=(u128)a->v[i]+b->v[i]+carry;
        r.v[i]=(uint64_t)z;
        carry=z>>64;
    }
    if(carry) {
        if(n==LIMBS)abort();
        r.v[n++]=(uint64_t)carry;
    }
    r.n=n;
    return r;
}
static Big sub(const Big *a, const Big *b) {
    Big r= {
        {
            0
        }, 0
    };
    uint64_t borrow=0;
    for(int i=0;i<a->n;i++) {
        u128 right=(u128)b->v[i]+borrow;
        r.v[i]=(uint64_t)((u128)a->v[i]-right);
        borrow=(u128)a->v[i]<right;
    }
    r.n=a->n;
    norm(&r);
    return r;
}
static Big mul(const Big *a, const Big *b) {
    Big r= {
        {
            0
        }, 0
    };
    for(int i=0;i<a->n;i++) {
        u128 carry=0;
        for(int j=0;j<b->n;j++) {
            if(i+j>=LIMBS)abort();
            u128 z=(u128)a->v[i]*b->v[j]+r.v[i+j]+carry;
            r.v[i+j]=(uint64_t)z;
            carry=z>>64;
        }
        int at=i+b->n;
        while(carry) {
            if(at>=LIMBS)abort();
            u128 z=(u128)r.v[at]+carry;
            r.v[at++]=(uint64_t)z;
            carry=z>>64;
        }
    }
    r.n=a->n+b->n;
    if(r.n>LIMBS)r.n=LIMBS;
    norm(&r);
    return r;
}
static Rat fromdouble(double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    Rat r= {
        small(0), small(1), (int)(u>>63)
    };
    int e=(u>>52)&2047;
    uint64_t m=u&UINT64_C(0xfffffffffffff);
    if(e)m|=UINT64_C(1)<<52;
    int exp=(e?e-1023:-1022)-52;
    r.num=small(m);
    if(exp>=0)r.num=shift(&r.num, exp);
    else r.den=shift(&r.den, -exp);
    return r;
}
static Rat product(const Rat *a, const Rat *b) {
    Rat r= {
        mul(&a->num, &b->num), mul(&a->den, &b->den), a->sign^b->sign
    };
    return r;
}
static Rat sum(const Rat *a, const Rat *b, int mode) {
    Big an=mul(&a->num, &b->den), bn=mul(&b->num, &a->den);
    Rat r= {
        small(0), mul(&a->den, &b->den), 0
    };
    if(a->sign==b->sign) {
        r.num=add(&an, &bn);
        r.sign=a->sign;
    }
    else {
        int c=cmp(&an, &bn);
        r.num=c>=0?sub(&an, &bn):sub(&bn, &an);
        r.sign=c>0?a->sign:c<0?b->sign:mode==3;
    }
    return r;
}
static int exponent(const Big *num, const Big *den) {
    int e=bits(num)-bits(den);
    Big x=e>=0?shift(den, e):shift(num, -e);
    if((e>=0?cmp(num, &x):cmp(&x, den))<0)e--;
    return e;
}
static u128 divide(Big *n, const Big *d) {
    u128 q=0;
    while(cmp(n, d)>=0) {
        int k=bits(n)-bits(d);
        Big x=shift(d, k);
        if(cmp(n, &x)<0) {
            k--;
            x=shift(d, k);
        }
        if(k>=128)abort();
        q|=(u128)1<<k;
        *n=sub(n, &x);
    }
    return q;
}
static uint64_t overflow(int size, int sign, int mode, int *flags) {
    int fb=size==8?52:size==4?23:10, eb=size==8?11:size==4?8:5;
    uint64_t e=(UINT64_C(1)<<eb)-1, s=(uint64_t)sign<<(size*8-1);
    *flags|=5;
    int inf=mode==0||(mode==2&&!sign)||(mode==3&&sign);
    return s|(inf?e<<fb:((e-1)<<fb)|((UINT64_C(1)<<fb)-1));
}
static uint64_t quantize(Rat *r, int size, int mode, int *flags) {
    int fb=size==8?52:size==4?23:10, eb=size==8?11:size==4?8:5, bias=(1<<(eb-1))-1, emin=1-bias;
    uint64_t sign=(uint64_t)r->sign<<(size*8-1);
    if(!r->num.n)return sign;
    int e=exponent(&r->num, &r->den);
    if(e>bias)return overflow(size, r->sign, mode, flags);
    int effective=e>emin?e:emin, k=fb-effective;
    Big n=k>=0?shift(&r->num, k):r->num, d=k<0?shift(&r->den, -k):r->den;
    uint64_t q=(uint64_t)divide(&n, &d);
    int inexact=n.n!=0;
    if(inexact) {
        Big twice=shift(&n, 1);
        int c=cmp(&twice, &d);
        int inc=mode==0?(c>0||(c==0&&(q&1))):mode==2?!r->sign:mode==3?r->sign:0;
        q+=(unsigned)inc;
        *flags|=1;
    }
    if(q>=(UINT64_C(1)<<(fb+1))) {
        q>>=1;
        effective++;
    }
    if(effective>bias)return overflow(size, r->sign, mode, flags);
    int normal=q>=(UINT64_C(1)<<fb);
    if(!normal&&inexact)*flags|=2;
    return sign|((uint64_t)(normal?effective+bias:0)<<fb)|(q&((UINT64_C(1)<<fb)-1));
}
static uint64_t isqrt128(u128 n) {
    u128 bit=(u128)1<<126, result=0;
    while(bit>n)bit>>=2;
    while(bit) {
        if(n>=result+bit) {
            n-=result+bit;
            result=(result>>1)+bit;
        }
        else result>>=1;
        bit>>=2;
    }
    return (uint64_t)result;
}
static uint64_t root(Rat *r, int size, int mode, int *flags) {
    if(!r->num.n)return (uint64_t)r->sign<<(size*8-1);
    int fb=size==8?52:size==4?23:10, eb=size==8?11:size==4?8:5, bias=(1<<(eb-1))-1, emin=1-bias, e=exponent(&r->num, &r->den);
    int half=e>=0?e/2:-((-e+1)/2), eff=half>emin?half:emin, k=2*(fb-eff);
    if(half>bias)return overflow(size,0,mode,flags);
    Big n=k>=0?shift(&r->num, k):r->num, d=k<0?shift(&r->den, -k):r->den, rem=n;
    u128 q=divide(&rem, &d);
    uint64_t m=isqrt128(q);
    Big mm=small(m), square=mul(&mm, &mm), right=mul(&square, &d);
    int exact=cmp(&n, &right)==0;
    if(!exact) {
        *flags|=1;
        uint64_t midpoint=m*2+1;
        Big mid=small(midpoint), sq=mul(&mid, &mid), rd=mul(&sq, &d), ln=shift(&n, 2);
        int c=cmp(&ln, &rd);
        if(mode==2||(mode==0&&(c>0||(c==0&&(m&1)))))m++;
    }
    if(m>=(UINT64_C(1)<<(fb+1))) {
        m>>=1;
        eff++;
    }
    if(eff>bias)return overflow(size,0,mode,flags);
    int normal=m>=(UINT64_C(1)<<fb);
    if(!normal&&!exact)*flags|=2;
    return ((uint64_t)(normal?eff+bias:0)<<fb)|(m&((UINT64_C(1)<<fb)-1));
}
uint64_t fp_exact(int op, double a, double b, double c, int size, int mode, int *flags) {
    Rat x=fromdouble(a), y=fromdouble(b), z=fromdouble(c), r=x;
    *flags=0;
    switch(op) {
        case 0:
        break;
        case 1:
        r=sum(&x, &y, mode);
        break;
        case 2:
        x.sign^=1;
        r=sum(&y, &x, mode);
        break;
        case 3:
        r=product(&x, &y);
        break;
        case 4:
        r=(Rat) {
            mul(&y.num, &x.den), mul(&y.den, &x.num), x.sign^y.sign
        };
        break;
        case 10:
        return root(&x, size, mode, flags);
        case 24:
        case 25:
        case 26:
        case 27:
        r=product(&x, &y);
        if(op==26||op==27)r.sign^=1;
        if(op==25||op==27)z.sign^=1;
        r=sum(&r, &z, mode);
        break;
    }
    return quantize(&r, size, mode, flags);
}
uint64_t fp_integer(uint64_t value, int is_signed, int size, int mode, int *flags) {
    Rat r= {
        small(0), small(1), 0
    };
    if(is_signed&&(int64_t)value<0) {
        r.sign=1;
        value=0-value;
    }
    r.num=small(value);
    *flags=0;
    return quantize(&r, size, mode, flags);
}
