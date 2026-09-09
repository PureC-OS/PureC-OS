#pragma once
// PureC hosted libc: math.h, declarations only. Double arithmetic is
// impossible under -mgeneral-regs-only (no SSE, no kernel FPU state),
// so there are deliberately NO definitions yet: including this header
// lets TCC sources parse, and any actual use fails at link time until
// the -msoft-float experiment (TCC phase). Constants are compile-time.

#define HUGE_VAL (__builtin_huge_val())
#define INFINITY (__builtin_huge_val())
#define NAN (__builtin_nan(""))
#define M_PI 3.14159265358979323846
#define M_E 2.71828182845904523536

double fabs(double value);
double floor(double value);
double ceil(double value);
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
