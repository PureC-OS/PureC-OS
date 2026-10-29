#pragma once
// PureC hosted libc: math. Userspace MAY use SSE and x87 (kernel switches
// FPU state per thread); only -mgeneral-regs-only targets (kernel/kmod)
// forbid it. Everything below is implemented in math.c and libm.c; the
// transcendental functions are accurate to 1-2 ULP.

#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#ifndef INFINITY
#define INFINITY (__builtin_huge_valf())
#endif
#ifndef NAN
#define NAN (__builtin_nanf(""))
#endif

#define M_E 2.71828182845904523536
#define M_LOG2E 1.44269504088896340736
#define M_LOG10E 0.434294481903251827651
#define M_LN2 0.693147180559945309417
#define M_LN10 2.30258509299404568402
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.785398163397448309616
#define M_1_PI 0.318309886183790671538
#define M_2_PI 0.636619772367581343076
#define M_SQRT2 1.41421356237309504880
#define M_SQRT1_2 0.707106781186547524401

#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4

#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define isnormal(x) __builtin_isnormal(x)
#define signbit(x) __builtin_signbit(x)
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)

double fabs(double value);
double floor(double value);
double ceil(double value);
double trunc(double value);
double round(double value);
double rint(double value);
double nearbyint(double value);
long lround(double value);
long long llround(double value);
long lrint(double value);
long long llrint(double value);
double fmod(double left, double right);
double ldexp(double value, int exponent);
double scalbn(double value, int exponent);
double frexp(double value, int *exponent);
double modf(double value, double *whole);
double copysign(double value, double sign);
double fmax(double left, double right);
double fmin(double left, double right);
double fdim(double left, double right);
double nan(const char *tag);

double sqrt(double value);
double cbrt(double value);
double hypot(double x, double y);
double pow(double base, double exponent);
double exp(double value);
double exp2(double value);
double expm1(double value);
double log(double value);
double log2(double value);
double log10(double value);
double log1p(double value);
double sin(double value);
double cos(double value);
double tan(double value);
double asin(double value);
double acos(double value);
double atan(double value);
double atan2(double y, double x);
double sinh(double value);
double cosh(double value);
double tanh(double value);

float fabsf(float value);
float floorf(float value);
float ceilf(float value);
float truncf(float value);
float roundf(float value);
float rintf(float value);
float nearbyintf(float value);
long lroundf(float value);
long long llroundf(float value);
long lrintf(float value);
float fmodf(float left, float right);
float ldexpf(float value, int exponent);
float scalbnf(float value, int exponent);
float frexpf(float value, int *exponent);
float modff(float value, float *whole);
float copysignf(float value, float sign);
float fmaxf(float left, float right);
float fminf(float left, float right);
float fdimf(float left, float right);
float nanf(const char *tag);

float sqrtf(float value);
float cbrtf(float value);
float hypotf(float x, float y);
float powf(float base, float exponent);
float expf(float value);
float exp2f(float value);
float expm1f(float value);
float logf(float value);
float log2f(float value);
float log10f(float value);
float log1pf(float value);
float sinf(float value);
float cosf(float value);
float tanf(float value);
float asinf(float value);
float acosf(float value);
float atanf(float value);
float atan2f(float y, float x);
float sinhf(float value);
float coshf(float value);
float tanhf(float value);
