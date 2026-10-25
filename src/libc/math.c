// PureC hosted libc: minimal math for ported programs (Lua first).
// Bit-level + CVTTSD2SI/CVTSI2SD/DIVSD/SQRTSD only: no libm calls, so it
// links under -ffreestanding. Userspace MAY use SSE (kernel keeps FPU
// state per thread); -mgeneral-regs-only targets must not link this.
// pow is integer-exact + x^0.5 via sqrt; general fractional pow is NaN
// (documented v1 limitation, full soft-fp later).

#include "include/hosted/math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "include/hosted/string.h"

double fabs(double value) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    bits &= ~(1ULL << 63);
    memcpy(&value, &bits, sizeof(value));
    return value;
}

double ldexp(double value, int exponent) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    int exp = (int)((bits >> 52) & 0x7FF);
    uint64_t mant = bits & 0xFFFFFFFFFFFFFULL;
    bool negative = (bits >> 63) != 0;
    if (exp == 0x7FF) return value; // inf/nan pass through
    if (exp == 0) {
        if (mant == 0) return value; // signed zero
        // Subnormal: normalize by scaling into the normal range.
        value *= 4503599627370496.0; // 2^52, exact
        memcpy(&bits, &value, sizeof(bits));
        exp = (int)((bits >> 52) & 0x7FF);
        exponent -= 52;
        if (exp == 0) return negative ? -0.0 : 0.0; // still subnormal
    }
    long result = (long)exp + exponent;
    if (result >= 0x7FF) {
        bits = ((uint64_t)negative << 63) | (0x7FFULL << 52);
        memcpy(&value, &bits, sizeof(value));
        return value; // overflow to inf
    }
    if (result <= 0) {
        // Gradual underflow: shift mantissa (with hidden bit) right,
        // round-half-even on the dropped bits.
        mant |= 1ULL << 52;
        long shift = 1 - result;
        if (shift >= 64) {
            bits = (uint64_t)negative << 63;
            memcpy(&value, &bits, sizeof(value));
            return value; // signed zero
        }
        uint64_t mask = (shift == 64) ? ~0ULL : ((1ULL << shift) - 1);
        uint64_t dropped = mant & mask;
        uint64_t half = 1ULL << (shift - 1);
        mant >>= shift;
        if (dropped > half || (dropped == half && (mant & 1))) mant++;
        if (mant >= (1ULL << 52)) {
            // Rounded up into the smallest normal.
            bits = ((uint64_t)negative << 63) | (1ULL << 52);
        } else {
            bits = ((uint64_t)negative << 63) | mant;
        }
        memcpy(&value, &bits, sizeof(value));
        return value;
    }
    bits = ((uint64_t)negative << 63) | ((uint64_t)result << 52) | mant;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static double math_nan(void) {
    uint64_t b = 0x7FF8000000000000ULL;
    double d = 0;
    memcpy(&d, &b, sizeof(d));
    return d;
}

static double math_inf(int neg) {
    uint64_t b = neg ? 0xFFF0000000000000ULL : 0x7FF0000000000000ULL;
    double d = 0;
    memcpy(&d, &b, sizeof(d));
    return d;
}

static bool math_is_nan(double x) {
    uint64_t b = 0;
    memcpy(&b, &x, sizeof(b));
    return ((b >> 52) & 0x7FF) == 0x7FF && (b & 0xFFFFFFFFFFFFFULL) != 0;
}

static bool math_is_inf(double x) {
    uint64_t b = 0;
    memcpy(&b, &x, sizeof(b));
    return ((b >> 52) & 0x7FF) == 0x7FF && (b & 0xFFFFFFFFFFFFFULL) == 0;
}

double trunc(double x) {
    if (math_is_nan(x) || math_is_inf(x) || x == 0.0)
        return x;
    if (x >= 9.223372036854776e18 || x <= -9.223372036854776e18)
        return x;
    int64_t i = (int64_t)x;
    return (double)i;
}

double floor(double x) {
    if (math_is_nan(x) || math_is_inf(x) || x == 0.0)
        return x;
    if (x >= 9.223372036854776e18 || x <= -9.223372036854776e18)
        return x;
    int64_t i = (int64_t)x;
    double t = (double)i;
    if (x >= 0.0)
        return t;
    return (t == x) ? x : t - 1.0;
}

double ceil(double x) {
    if (math_is_nan(x) || math_is_inf(x) || x == 0.0)
        return x;
    if (x >= 9.223372036854776e18 || x <= -9.223372036854776e18)
        return x;
    return -floor(-x);
}

double fmod(double a, double b) {
    if (math_is_nan(a) || math_is_nan(b) || math_is_inf(a) || b == 0.0)
        return math_nan();
    if (math_is_inf(b))
        return a;
    double q = trunc(a / b);
    return a - q * b;
}

double modf(double x, double *whole) {
    double t;
    if (math_is_nan(x) || math_is_inf(x)) {
        if (whole)
            *whole = x;
        return x;
    }
    t = trunc(x);
    if (whole)
        *whole = t;
    return x - t;
}

double frexp(double x, int *exp) {
    uint64_t b = 0;
    int e;
    memcpy(&b, &x, sizeof(b));
    e = (int)((b >> 52) & 0x7FF);
    if (e == 0) {
        if ((b & 0xFFFFFFFFFFFFFULL) == 0) {
            if (exp)
                *exp = 0;
            return x;
        }
        double s = ldexp(x, 52);
        double m = frexp(s, exp);
        if (exp)
            *exp -= 52;
        return m;
    }
    if (e == 0x7FF) {
        if (exp)
            *exp = 0;
        return x;
    }
    if (exp)
        *exp = e - 1022;
    b = (b & 0x800FFFFFFFFFFFFFULL) | (0x3FEULL << 52);
    memcpy(&x, &b, sizeof(x));
    return x;
}

double sqrt(double x) {
    return __builtin_sqrt(x);
}

double pow(double base, double expn) {
    if (expn == 2.0)
        return base * base;
    if (expn == 0.0)
        return 1.0;
    if (base == 0.0)
        return (expn > 0.0) ? 0.0 : math_inf(0);
    if (expn == 0.5 && base >= 0.0)
        return __builtin_sqrt(base);
    if (expn == trunc(expn) && expn >= -64.0 && expn <= 64.0) {
        int n = (int)expn;
        int neg = 0;
        double r = 1.0;
        if (n < 0) {
            neg = 1;
            n = -n;
        }
        while (n) {
            if (n & 1)
                r *= base;
            base *= base;
            n >>= 1;
        }
        return neg ? 1.0 / r : r;
    }
    return math_nan();
}
