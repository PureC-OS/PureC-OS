// PureC hosted libc: libm. Transcendental functions run on the x87 FPU in
// extended precision (the kernel saves x87/SSE state per thread and starts
// every thread with the default 64-bit-mantissa control word), then round to
// double. sqrt uses SSE. Results are within 1-2 ULP of the exact value.
// sin/cos/tan reduce their argument with a split pi/2 (Cody-Waite, about
// 160 bits) up to 3.3e9; beyond that FPREM1 against a 66-bit pi is used and
// the absolute error grows with the magnitude.

#include "include/hosted/math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIBM_LOG1P_LIMIT 0.29L
#define LIBM_TRIG_LIMIT 3.3e9
#define LIBM_PIO2_1 0x6487ed51p-30L
#define LIBM_PIO2_2 0x85a308d3p-65L
#define LIBM_PIO2_3 0x98cc517p-96L
#define LIBM_PIO2_4 0x6e0e689481270445p-166L
#define LIBM_2_OVER_PI 0xa2f9836e4e441529p-64L
#define LIBM_EXP_MAX 709.782712893384
#define LIBM_EXP_MIN -745.1332191019412
#define LIBM_TWO_52 4503599627370496.0

static uint64_t bits_of(double value) {
    union { double d; uint64_t u; } c = {value};
    return c.u;
}

static double from_bits(uint64_t bits) {
    union { double d; uint64_t u; } c;
    c.u = bits;
    return c.d;
}

static bool is_nan(double x) { return x != x; }
static bool is_inf(double x) { return !is_nan(x) && is_nan(x - x); }
static bool negative_bit(double x) { return (bits_of(x) >> 63) != 0; }

static double make_nan(void) { return from_bits(0x7FF8000000000000ULL); }
static double make_inf(bool negative) {
    return from_bits(negative ? 0xFFF0000000000000ULL : 0x7FF0000000000000ULL);
}

static long double x87_log2e(void) { long double r; __asm__("fldl2e" : "=t"(r)); return r; }
static long double x87_ln2(void) { long double r; __asm__("fldln2" : "=t"(r)); return r; }
static long double x87_lg2(void) { long double r; __asm__("fldlg2" : "=t"(r)); return r; }
static long double x87_pi(void) { long double r; __asm__("fldpi" : "=t"(r)); return r; }

static long double x87_rint(long double x) {
    long double r;
    __asm__("frndint" : "=t"(r) : "0"(x));
    return r;
}

static long double x87_sqrt(long double x) {
    long double r;
    __asm__("fsqrt" : "=t"(r) : "0"(x));
    return r;
}

static long double x87_f2xm1(long double x) {
    long double r;
    __asm__("f2xm1" : "=t"(r) : "0"(x));
    return r;
}

static long double x87_yl2x(long double x, long double y) {
    long double r;
    __asm__("fyl2x" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}

static long double x87_yl2xp1(long double x, long double y) {
    long double r;
    __asm__("fyl2xp1" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}

static long double x87_atan2(long double y, long double x) {
    long double r;
    __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}

static long double x87_sin(long double x) {
    long double r;
    __asm__("fsin" : "=t"(r) : "0"(x));
    return r;
}

static long double x87_cos(long double x) {
    long double r;
    __asm__("fcos" : "=t"(r) : "0"(x));
    return r;
}

static long double x87_tan(long double x) {
    long double r;
    __asm__("fptan; fstp %%st(0)" : "=t"(r) : "0"(x));
    return r;
}

static long double x87_remainder(long double x, long double y) {
    long double r;
    __asm__ volatile("1: fprem1\n\tfnstsw %%ax\n\ttestb $4, %%ah\n\tjnz 1b"
                     : "=t"(r) : "0"(x), "u"(y) : "ax", "cc");
    return r;
}

static long double x87_remainder_trunc(long double x, long double y) {
    long double r;
    __asm__ volatile("1: fprem\n\tfnstsw %%ax\n\ttestb $4, %%ah\n\tjnz 1b"
                     : "=t"(r) : "0"(x), "u"(y) : "ax", "cc");
    return r;
}

static long double trig_reduce(double x, int *quadrant) {
    if (x > LIBM_TRIG_LIMIT || x < -LIBM_TRIG_LIMIT) {
        long double r = x87_remainder((long double)x, 2.0L * x87_pi());
        long double steps = x87_rint(r / (0.5L * x87_pi()));
        *quadrant = (int)((long)steps & 3);
        return r - steps * (0.5L * x87_pi());
    }
    long double v = x;
    long double n = x87_rint(v * LIBM_2_OVER_PI);
    long double r = v - n * LIBM_PIO2_1;
    r -= n * LIBM_PIO2_2;
    r -= n * LIBM_PIO2_3;
    r -= n * LIBM_PIO2_4;
    *quadrant = (int)((long)n & 3);
    return r;
}

double sqrt(double x) {
    double r;
    __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x));
    return r;
}

static double exp_core(long double t) {
    long double n = x87_rint(t);
    long double fraction = t - n;
    long double scaled = 1.0L + x87_f2xm1(fraction);
    return ldexp((double)scaled, (int)n);
}

double exp(double x) {
    if (is_nan(x)) return x;
    if (x > LIBM_EXP_MAX) return make_inf(false);
    if (x < LIBM_EXP_MIN) return 0.0;
    return exp_core((long double)x * x87_log2e());
}

double exp2(double x) {
    if (is_nan(x)) return x;
    if (x >= 1024.0) return make_inf(false);
    if (x < -1075.0) return 0.0;
    return exp_core((long double)x);
}

double expm1(double x) {
    if (is_nan(x)) return x;
    if (x > LIBM_EXP_MAX) return make_inf(false);
    if (x < -40.0) return -1.0;
    long double t = (long double)x * x87_log2e();
    if (t > -1.0L && t < 1.0L)
        return (double)x87_f2xm1(t);
    return exp(x) - 1.0;
}

static double log_with_scale(double x, long double scale) {
    if (is_nan(x)) return x;
    if (x < 0.0) return make_nan();
    if (x == 0.0) return make_inf(true);
    if (is_inf(x)) return x;
    long double v = x;
    long double near_one = v - 1.0L;
    if (near_one > -LIBM_LOG1P_LIMIT && near_one < LIBM_LOG1P_LIMIT)
        return (double)x87_yl2xp1(near_one, scale);
    return (double)x87_yl2x(v, scale);
}

double log(double x) { return log_with_scale(x, x87_ln2()); }
double log2(double x) { return log_with_scale(x, 1.0L); }
double log10(double x) { return log_with_scale(x, x87_lg2()); }

double log1p(double x) {
    if (is_nan(x)) return x;
    if (x < -1.0) return make_nan();
    if (x == -1.0) return make_inf(true);
    if (is_inf(x)) return x;
    long double v = x;
    if (v > -LIBM_LOG1P_LIMIT && v < LIBM_LOG1P_LIMIT)
        return (double)x87_yl2xp1(v, x87_ln2());
    return (double)x87_yl2x(1.0L + v, x87_ln2());
}

static bool is_integer(double y) {
    if (y >= LIBM_TWO_52 || y <= -LIBM_TWO_52) return true;
    return y == trunc(y);
}

static bool is_odd_integer(double y) {
    if (y >= 9007199254740992.0 || y <= -9007199254740992.0) return false;
    if (!is_integer(y)) return false;
    return fmod(y, 2.0) != 0.0;
}

double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 1.0) return 1.0;
    if (is_nan(x) || is_nan(y)) return make_nan();
    bool neg_x = negative_bit(x);
    bool odd = is_odd_integer(y);
    if (x == 0.0) {
        if (y < 0.0) return make_inf(neg_x && odd);
        return (neg_x && odd) ? -0.0 : 0.0;
    }
    if (is_inf(y)) {
        double ax = fabs(x);
        if (ax == 1.0) return 1.0;
        bool grow = (ax > 1.0) == (y > 0.0);
        return grow ? make_inf(false) : 0.0;
    }
    if (is_inf(x)) {
        if (neg_x) {
            if (y < 0.0) return (odd) ? -0.0 : 0.0;
            return make_inf(odd);
        }
        return y < 0.0 ? 0.0 : make_inf(false);
    }
    if (neg_x && !is_integer(y)) return make_nan();
    long double t = x87_yl2x((long double)fabs(x), (long double)y);
    double magnitude;
    if (t >= 1024.0L) magnitude = make_inf(false);
    else if (t < -1076.0L) magnitude = 0.0;
    else magnitude = exp_core(t);
    return (neg_x && odd) ? -magnitude : magnitude;
}

double sin(double x) {
    if (is_nan(x) || is_inf(x)) return make_nan();
    if (x == 0.0) return x;
    int q;
    long double r = trig_reduce(x, &q);
    switch (q) {
        case 0: return (double)x87_sin(r);
        case 1: return (double)x87_cos(r);
        case 2: return -(double)x87_sin(r);
        default: return -(double)x87_cos(r);
    }
}

double cos(double x) {
    if (is_nan(x) || is_inf(x)) return make_nan();
    int q;
    long double r = trig_reduce(x, &q);
    switch (q) {
        case 0: return (double)x87_cos(r);
        case 1: return -(double)x87_sin(r);
        case 2: return -(double)x87_cos(r);
        default: return (double)x87_sin(r);
    }
}

double tan(double x) {
    if (is_nan(x) || is_inf(x)) return make_nan();
    if (x == 0.0) return x;
    int q;
    long double r = trig_reduce(x, &q);
    long double t = x87_tan(r);
    return (q & 1) ? (double)(-1.0L / t) : (double)t;
}

double atan2(double y, double x) {
    if (is_nan(x) || is_nan(y)) return make_nan();
    return (double)x87_atan2((long double)y, (long double)x);
}

double atan(double x) {
    if (is_nan(x)) return x;
    return (double)x87_atan2((long double)x, 1.0L);
}

static long double complement_root(double x) {
    long double v = x;
    return x87_sqrt((1.0L - v) * (1.0L + v));
}

double asin(double x) {
    if (is_nan(x) || x > 1.0 || x < -1.0) return make_nan();
    return (double)x87_atan2((long double)x, complement_root(x));
}

double acos(double x) {
    if (is_nan(x) || x > 1.0 || x < -1.0) return make_nan();
    return (double)x87_atan2(complement_root(x), (long double)x);
}

double sinh(double x) {
    if (is_nan(x) || is_inf(x) || x == 0.0) return x;
    double a = fabs(x);
    double r;
    if (a > 709.0) {
        double half = exp(a / 2.0);
        r = half * (0.5 * half);
    } else if (a > 22.0) {
        r = 0.5 * exp(a);
    } else {
        double em1 = expm1(a);
        r = 0.5 * (em1 + em1 / (em1 + 1.0));
    }
    return negative_bit(x) ? -r : r;
}

double cosh(double x) {
    if (is_nan(x)) return x;
    double a = fabs(x);
    if (is_inf(a)) return a;
    if (a > 709.0) {
        double half = exp(a / 2.0);
        return half * (0.5 * half);
    }
    double e = exp(a);
    return 0.5 * (e + 1.0 / e);
}

double tanh(double x) {
    if (is_nan(x) || x == 0.0) return x;
    double a = fabs(x);
    double r;
    if (a > 22.0) {
        r = 1.0;
    } else {
        double em1 = expm1(2.0 * a);
        r = em1 / (em1 + 2.0);
    }
    return negative_bit(x) ? -r : r;
}

double cbrt(double x) {
    if (is_nan(x) || is_inf(x) || x == 0.0) return x;
    long double a = fabs(x);
    long double r = (long double)exp_core(x87_yl2x(a, 1.0L) / 3.0L);
    r = r - (r * r * r - a) / (3.0L * r * r);
    return negative_bit(x) ? -(double)r : (double)r;
}

double hypot(double x, double y) {
    if (is_inf(x) || is_inf(y)) return make_inf(false);
    if (is_nan(x) || is_nan(y)) return make_nan();
    long double lx = x, ly = y;
    return (double)x87_sqrt(lx * lx + ly * ly);
}

double copysign(double x, double y) {
    uint64_t bits = (bits_of(x) & 0x7FFFFFFFFFFFFFFFULL) |
                    (bits_of(y) & 0x8000000000000000ULL);
    return from_bits(bits);
}

double scalbn(double x, int n) { return ldexp(x, n); }

double rint(double x) {
    if (is_nan(x) || is_inf(x)) return x;
    double a = fabs(x);
    if (a >= LIBM_TWO_52) return x;
    double r = (double)x87_rint((long double)x);
    return r == 0.0 ? copysign(0.0, x) : r;
}

double nearbyint(double x) { return rint(x); }

double round(double x) {
    if (is_nan(x) || is_inf(x)) return x;
    double a = fabs(x);
    if (a >= LIBM_TWO_52) return x;
    double t = trunc(a);
    if (a - t >= 0.5) t += 1.0;
    return copysign(t, x);
}

long lround(double x) { return (long)round(x); }
long long llround(double x) { return (long long)round(x); }
long lrint(double x) { return (long)rint(x); }
long long llrint(double x) { return (long long)rint(x); }

double fmax(double a, double b) {
    if (is_nan(a)) return b;
    if (is_nan(b)) return a;
    if (a == b) return negative_bit(a) ? b : a;
    return a > b ? a : b;
}

double fmin(double a, double b) {
    if (is_nan(a)) return b;
    if (is_nan(b)) return a;
    if (a == b) return negative_bit(a) ? a : b;
    return a < b ? a : b;
}

double fmod(double x, double y) {
    if (is_nan(x) || is_nan(y) || is_inf(x) || y == 0.0) return make_nan();
    if (is_inf(y) || x == 0.0) return x;
    long double r = x87_remainder_trunc((long double)x, (long double)y);
    double result = (double)r;
    return result == 0.0 ? copysign(0.0, x) : result;
}

double fdim(double a, double b) {
    if (is_nan(a) || is_nan(b)) return make_nan();
    return a > b ? a - b : 0.0;
}

double nan(const char *tag) {
    (void)tag;
    return make_nan();
}

#define FLOAT_WRAP1(name) \
    float name##f(float x) { return (float)name((double)x); }
#define FLOAT_WRAP2(name) \
    float name##f(float x, float y) { return (float)name((double)x, (double)y); }

FLOAT_WRAP1(sin)
FLOAT_WRAP1(cos)
FLOAT_WRAP1(tan)
FLOAT_WRAP1(asin)
FLOAT_WRAP1(acos)
FLOAT_WRAP1(atan)
FLOAT_WRAP1(sinh)
FLOAT_WRAP1(cosh)
FLOAT_WRAP1(tanh)
FLOAT_WRAP1(exp)
FLOAT_WRAP1(exp2)
FLOAT_WRAP1(expm1)
FLOAT_WRAP1(log)
FLOAT_WRAP1(log2)
FLOAT_WRAP1(log10)
FLOAT_WRAP1(log1p)
FLOAT_WRAP1(sqrt)
FLOAT_WRAP1(cbrt)
FLOAT_WRAP1(fabs)
FLOAT_WRAP1(floor)
FLOAT_WRAP1(ceil)
FLOAT_WRAP1(trunc)
FLOAT_WRAP1(round)
FLOAT_WRAP1(rint)
FLOAT_WRAP1(nearbyint)
FLOAT_WRAP2(atan2)
FLOAT_WRAP2(pow)
FLOAT_WRAP2(fmod)
FLOAT_WRAP2(hypot)
FLOAT_WRAP2(copysign)
FLOAT_WRAP2(fmax)
FLOAT_WRAP2(fmin)
FLOAT_WRAP2(fdim)

float ldexpf(float x, int n) { return (float)ldexp((double)x, n); }
float scalbnf(float x, int n) { return (float)ldexp((double)x, n); }

float frexpf(float x, int *exponent) {
    return (float)frexp((double)x, exponent);
}

float modff(float x, float *whole) {
    double integral;
    double fraction = modf((double)x, &integral);
    *whole = (float)integral;
    return (float)fraction;
}

long lroundf(float x) { return lround((double)x); }
long long llroundf(float x) { return llround((double)x); }
long lrintf(float x) { return lrint((double)x); }
float nanf(const char *tag) { return (float)nan(tag); }
