#pragma once
// PureC hosted libc: math. Userspace MAY use SSE (kernel switches FPU
// per thread); only -mgeneral-regs-only targets (kernel/kmod) forbid it.
// Implemented: fabs, ldexp, floor, ceil, trunc, fmod, modf, frexp, sqrt,
// pow (integer-exact + sqrt special case; general fractional pow is NaN,
// full soft-fp later). Declared for parsing: sin/cos/log/exp (no impl yet).

#define HUGE_VAL (__builtin_huge_val())
#ifndef INFINITY
#define INFINITY (__builtin_huge_val())
#endif
#ifndef NAN
#define NAN (__builtin_nan(""))
#endif
#define M_PI 3.14159265358979323846
#define M_E 2.71828182845904523536

double fabs(double value);
double floor(double value);
double ceil(double value);
double trunc(double value);
double sqrt(double value);
double pow(double base, double exponent);
double sin(double value);
double cos(double value);
double log(double value);
double exp(double value);
double fmod(double left, double right);
double ldexp(double value, int exponent);
double frexp(double value, int *exponent);
double modf(double value, double *whole);
