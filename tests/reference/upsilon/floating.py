"""Exact rational IEEE binary rounding; no host double-rounding in arithmetic."""
from fractions import Fraction
import math

INEXACT, UNDERFLOW, OVERFLOW, DIVZERO, INVALID = (1 << i for i in range(5))
FORMATS = {2: (5, 10), 4: (8, 23), 8: (11, 52)}

def scale(value, power):
    return value * (1 << power) if power >= 0 else value / (1 << -power)

def quantize(value, size, mode=0, negative_zero=False):
    """Return (bits, exception flags) for an int, Fraction, or Python float.

    Modes: nearest-even, toward zero, toward positive infinity, toward negative
    infinity. Tininess is detected after rounding. NaNs become quiet NaNs.
    """
    exponent_bits, fraction_bits = FORMATS[size]
    sign_bit = 1 << (size * 8 - 1)
    exponent_mask = (1 << exponent_bits) - 1
    bias = (1 << (exponent_bits - 1)) - 1
    emin, emax = 1 - bias, bias
    if isinstance(value, float):
        if math.isnan(value):
            return (exponent_mask << fraction_bits) | (1 << (fraction_bits - 1)), 0
        negative = math.copysign(1., value) < 0
        if math.isinf(value):
            return (sign_bit if negative else 0) | (exponent_mask << fraction_bits), 0
        negative_zero = negative if value == 0 else negative_zero
        value = Fraction.from_float(value)
    else:
        value = Fraction(value)
    sign = value < 0 or value == 0 and negative_zero
    sign_word = sign_bit if sign else 0
    if value == 0:
        return sign_word, 0
    value = abs(value)
    exponent = value.numerator.bit_length() - value.denominator.bit_length()
    if value < scale(Fraction(1), exponent):
        exponent -= 1
    def overflow():
        infinity = mode == 0 or mode == 2 and not sign or mode == 3 and sign
        result = exponent_mask << fraction_bits if infinity else ((exponent_mask - 1) << fraction_bits) | ((1 << fraction_bits) - 1)
        return sign_word | result, OVERFLOW | INEXACT
    if exponent > emax:
        return overflow()
    effective_exp = max(exponent, emin)
    scaled = scale(value, fraction_bits - effective_exp)
    significand, remainder = divmod(scaled.numerator, scaled.denominator)
    if remainder:
        above_half = remainder * 2 > scaled.denominator
        tie_odd = remainder * 2 == scaled.denominator and significand & 1
        increment = (above_half or tie_odd) if mode == 0 else (not sign if mode == 2 else sign if mode == 3 else False)
        significand += int(bool(increment))
    if significand >= (1 << (fraction_bits + 1)):
        significand >>= 1
        effective_exp += 1
    if effective_exp > emax:
        return overflow()
    normal = significand >= (1 << fraction_bits)
    exp_field = effective_exp + bias if normal else 0
    fraction = significand - (1 << fraction_bits) if normal else significand
    flags = INEXACT if remainder else 0
    if remainder and not normal:
        flags |= UNDERFLOW
    return sign_word | (exp_field << fraction_bits) | fraction, flags

def sqrt_quantize(value, size, mode=0):
    """Correctly rounded rational square root, using integer arithmetic."""
    value = Fraction(value)
    eb, fb = FORMATS[size]
    emin = 2 - (1 << (eb-1))
    exponent = value.numerator.bit_length() - value.denominator.bit_length()
    if value < scale(Fraction(1),exponent):
        exponent -= 1
    exponent = max(exponent // 2,emin)
    square = scale(value,2*(fb-exponent))
    root = math.isqrt(square.numerator // square.denominator)
    exact = root*root*square.denominator == square.numerator
    if not exact:
        cmp = 4*square.numerator - (2*root+1)**2*square.denominator
        root += int(bool((cmp > 0 or cmp == 0 and root & 1) if mode == 0 else mode == 2))
    bits, flags = quantize(scale(Fraction(root),exponent-fb),size,mode)
    if not exact:
        flags |= INEXACT
        if not bits >> fb & ((1<<eb)-1):
            flags |= UNDERFLOW
    return bits,flags
