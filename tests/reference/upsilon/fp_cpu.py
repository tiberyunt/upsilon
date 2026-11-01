"""v0.2 floating-point operations and conversions, mixed into the CPU."""
import math
from fractions import Fraction
from .isa import *
from .floating import INVALID, DIVZERO, INEXACT, sqrt_quantize

class FloatingCPU:
    def convert(self, ins, refs, values):
        name, size, width = ins.name, ins.size, ins.aux
        value = values[0]
        if name in ('MOVIF','MOVFI'):
            self.store(refs[1],self.raw(refs[0]),raw=True)
        elif name in ('MOVZ','MOVSX'):
            value = signed(value,size) if name == 'MOVSX' else value
            self.store(refs[1],value)
            self.flags(value,width)
        elif name in ('CVTIF','CVTUF'):
            self.store_float(refs[1],signed(value,size) if name == 'CVTIF' else value)
        elif name == 'CVTFF':
            if self.signaling_nan(refs[0]):
                self.fp_exception(INVALID)
            self.store_float(refs[1],value)
        else:
            lo, hi = (0,(1 << (width*8))-1) if name == 'CVTFU' else (-(1 << (width*8-1)),(1 << (width*8-1))-1)
            if math.isnan(value):
                self.fp_exception(INVALID)
                result = 0
            elif math.isinf(value):
                self.fp_exception(INVALID)
                result = hi if value > 0 else lo
            else:
                mode = (self.fpsr >> 16) & 3 if name == 'CVTFIR' else 1
                result = (round,math.trunc,math.ceil,math.floor)[mode](value)
                if not lo <= result <= hi:
                    self.fp_exception(INVALID)
                    result = min(hi,max(lo,result))
                elif result != value:
                    self.fp_exception(INEXACT)
            self.store(refs[1],result)
            self.flags(result,width)

    def signaling_nan(self, ref):
        bits = self.raw(ref)
        fb,eb = {2:(10,5),4:(23,8),8:(52,11)}[ref.size]
        return (bits >> fb) & ((1<<eb)-1) == (1<<eb)-1 and bits & ((1<<fb)-1) != 0 and not bits & (1<<(fb-1))

    def floating(self, name, refs, values):
        mode = (self.fpsr >> 16) & 3
        inputs = [v for v in values if v is not None]
        signaling = any(self.signaling_nan(ref) for ref,value in zip(refs,values) if value is not None and ref.floating)
        a = values[0]
        if name == 'FCLASS':
            neg = math.copysign(1.,a)<0
            if math.isnan(a):
                index = 8 if signaling else 9
            elif math.isinf(a):
                index = 0 if neg else 7
            elif a == 0:
                index = 3 if neg else 4
            elif abs(a) < {2:2**-14,4:2**-126,8:2**-1022}[refs[0].size]:
                index = 2 if neg else 5
            else:
                index = 1 if neg else 6
            self.store(refs[1],1<<index)
            return
        if signaling:
            self.fp_exception(INVALID)
        if name in ('FCMP','FCMPE','FTST'):
            a,b = (0.,a) if name == 'FTST' else values
            if name == 'FCMPE' and (math.isnan(a) or math.isnan(b)):
                self.fp_exception(INVALID)
            self.psr = (self.psr & ~15) | (V if math.isnan(a) or math.isnan(b) else N|C if b<a else Z if b==a else 0)
            return
        if name == 'FSQRT':
            if a < 0:
                self.fp_exception(INVALID)
                result = math.nan
            elif math.isfinite(a) and a != 0:
                bits,flags = sqrt_quantize(Fraction.from_float(a),refs[-1].size,mode)
                if flags:
                    self.fp_exception(flags)
                self.store(refs[-1],bits,raw=True)
                return
            else:
                result = a
        elif name in ('FMOV','FNEG','FABS'):
            result = a if name == 'FMOV' else -a if name == 'FNEG' else abs(a)
        elif name in ('FRINT','FFLOOR','FCEIL','FTRUNC'):
            if math.isfinite(a):
                rm = {'FRINT':mode,'FFLOOR':3,'FCEIL':2,'FTRUNC':1}[name]
                result = (round,math.trunc,math.ceil,math.floor)[rm](a)
                if result != a:
                    self.fp_exception(INEXACT)
                if result == 0:
                    result = math.copysign(0.,a)
            else:
                result = a
        elif name in ('FMIN','FMAX'):
            b = values[1]
            result = math.nan if math.isnan(a) or math.isnan(b) else min(a,b) if name == 'FMIN' else max(a,b)
            if a == b == 0:
                neg = (math.copysign(1.,a)<0 or math.copysign(1.,b)<0) if name == 'FMIN' else (math.copysign(1.,a)<0 and math.copysign(1.,b)<0)
                result = -0. if neg else 0.
        elif name == 'FCOPYSIGN':
            result = math.copysign(values[1],a)
        elif name in ('FMA','FMS','FNMA','FNMS'):
            a,b,c = values[:3]
            if all(math.isfinite(v) for v in (a,b,c)):
                product = Fraction.from_float(a)*Fraction.from_float(b)
                addend = Fraction.from_float(c)
            else:
                if math.isinf(a) and b == 0 or math.isinf(b) and a == 0:
                    self.fp_exception(INVALID)
                product,addend = a*b,c
            if name in ('FNMA','FNMS'):
                product = -product
            if name in ('FMS','FNMS'):
                addend = -addend
            result = product+addend
            if result == 0:
                ps = math.copysign(1.,a)*math.copysign(1.,b)*(-1 if name in ('FNMA','FNMS') else 1)
                cs = math.copysign(1.,c)*(-1 if name in ('FMS','FNMS') else 1)
                result = -0. if ps<0 and cs<0 or mode==3 and ps!=cs else 0.
        else:
            op = name[:-1] if name.endswith('3') else name
            b = values[1]
            if op == 'FREM':
                if math.isnan(a) or math.isnan(b):
                    result = math.nan
                elif math.isinf(b) or a == 0:
                    self.fp_exception(INVALID)
                    result = math.nan
                elif math.isinf(a):
                    result = b
                else:
                    fa,fb = Fraction.from_float(a),Fraction.from_float(b)
                    result = fb-round(fb/fa)*fa
                    if result == 0:
                        result = math.copysign(0.,b)
            elif all(math.isfinite(v) for v in (a,b)) and not (op=='FDIV' and a==0):
                fa,fb = Fraction.from_float(a),Fraction.from_float(b)
                result = {'FADD':lambda:fb+fa,'FSUB':lambda:fb-fa,'FMUL':lambda:fb*fa,'FDIV':lambda:fb/fa}[op]()
                if result == 0:
                    if op in ('FMUL','FDIV'):
                        neg = math.copysign(1.,a)!=math.copysign(1.,b)
                    else:
                        aa = -a if op=='FSUB' else a
                        neg = math.copysign(1.,b)<0 and math.copysign(1.,aa)<0 or mode==3 and math.copysign(1.,b)!=math.copysign(1.,aa)
                    result = -0. if neg else 0.
            elif any(math.isnan(v) for v in (a,b)):
                result = math.nan
            elif op == 'FADD':
                result = b+a
            elif op == 'FSUB':
                result = b-a
            elif op == 'FMUL':
                result = b*a
            elif a == 0:
                if b == 0:
                    self.fp_exception(INVALID)
                    result = math.nan
                else:
                    if math.isfinite(b):
                        self.fp_exception(DIVZERO)
                    result = math.copysign(math.inf,math.copysign(1.,a)*math.copysign(1.,b))
            else:
                result = b/a
        if isinstance(result,float) and math.isnan(result) and not any(math.isnan(v) for v in inputs):
            self.fp_exception(INVALID)
        self.store_float(refs[-1],result)
