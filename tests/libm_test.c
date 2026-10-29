#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DECL1(n) double purec_##n(double);
#define DECLF1(n) float purec_##n##f(float);
DECL1(fabs) DECL1(floor) DECL1(ceil) DECL1(trunc) DECL1(round) DECL1(rint)
DECL1(sqrt) DECL1(cbrt) DECL1(exp) DECL1(exp2) DECL1(expm1) DECL1(log)
DECL1(log2) DECL1(log10) DECL1(log1p) DECL1(sin) DECL1(cos) DECL1(tan)
DECL1(asin) DECL1(acos) DECL1(atan) DECL1(sinh) DECL1(cosh) DECL1(tanh)
DECLF1(sin) DECLF1(cos) DECLF1(exp) DECLF1(log) DECLF1(sqrt) DECLF1(atan)
double purec_pow(double, double);
double purec_atan2(double, double);
double purec_fmod(double, double);
double purec_hypot(double, double);
double purec_copysign(double, double);
double purec_fmax(double, double);
double purec_fmin(double, double);
double purec_ldexp(double, int);
double purec_frexp(double, int *);
double purec_modf(double, double *);
long purec_lround(double);
float purec_powf(float, float);
float purec_atan2f(float, float);

static int failures;
static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;

static uint64_t next_random(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}

static double uniform(double low, double high) {
  double unit = (double)(next_random() >> 11) / 9007199254740992.0;
  return low + (high - low) * unit;
}

static double log_uniform(double low, double high) {
  return exp(uniform(log(low), log(high)));
}

static int64_t ordered(double value) {
  int64_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return bits < 0 ? (int64_t)0x8000000000000000ULL - bits : bits;
}

static int64_t ulp(double got, double want) {
  if (isnan(got) && isnan(want))
    return 0;
  if (isnan(got) || isnan(want))
    return INT64_MAX;
  if (got == want)
    return 0;
  int64_t a = ordered(got), b = ordered(want);
  return a > b ? a - b : b - a;
}

static int64_t ulp_float(float got, float want) {
  if (isnan(got) && isnan(want))
    return 0;
  if (isnan(got) || isnan(want))
    return INT64_MAX;
  if (got == want)
    return 0;
  int32_t a, b;
  memcpy(&a, &got, 4);
  memcpy(&b, &want, 4);
  if (a < 0) a = (int32_t)0x80000000 - a;
  if (b < 0) b = (int32_t)0x80000000 - b;
  return a > b ? (int64_t)a - b : (int64_t)b - a;
}

static void report(const char *name, int64_t worst, int64_t limit) {
  int bad = worst > limit;
  printf("%-10s max %lld ULP (limit %lld)%s\n", name, (long long)worst,
         (long long)limit, bad ? "  FAIL" : "");
  failures += bad;
}

#define SWEEP1(name, host, mine, low, high, count, limit)                      \
  do {                                                                         \
    int64_t worst = 0;                                                         \
    for (int i = 0; i < (count); i++) {                                        \
      double x = uniform((low), (high));                                       \
      int64_t d = ulp(mine(x), host(x));                                       \
      if (d > worst) { worst = d; }                                            \
      if (d > (limit) && failures < 40)                                        \
        printf("  %s(%.17g) = %.17g want %.17g\n", name, x, mine(x), host(x)); \
    }                                                                          \
    report(name, worst, (limit));                                              \
  } while (0)

static double cbrt_reference(double x) { return (double)cbrtl(x); }

#define SWEEPLOG(name, host, mine, low, high, count, limit)                    \
  do {                                                                         \
    int64_t worst = 0;                                                         \
    for (int i = 0; i < (count); i++) {                                        \
      double x = log_uniform((low), (high));                                   \
      int64_t d = ulp(mine(x), host(x));                                       \
      if (d > worst) { worst = d; }                                            \
    }                                                                          \
    report(name, worst, (limit));                                              \
  } while (0)

static void expect_same(const char *label, double got, double want) {
  int same = (isnan(got) && isnan(want)) ||
             (got == want && signbit(got) == signbit(want)) ||
             (got != 0.0 && want != 0.0 && isfinite(got) && isfinite(want) &&
              ulp(got, want) <= 3);
  if (!same) {
    printf("SPECIAL %s: got %.17g want %.17g\n", label, got, want);
    failures++;
  }
}

#define SPECIAL1(fn, x) expect_same(#fn "(" #x ")", purec_##fn(x), fn(x))
#define SPECIAL2(fn, x, y) \
  expect_same(#fn "(" #x "," #y ")", purec_##fn(x, y), fn(x, y))

static void specials(void) {
  const double values[] = {0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 3.0,
                           -3.0, 1e308, -1e308, 1e-308, 5e-324, INFINITY,
                           -INFINITY, NAN, 710.0, -746.0, 1e19, 4.5, -4.5, 2.5};
  size_t count = sizeof(values) / sizeof(values[0]);
  for (size_t i = 0; i < count; i++) {
    double x = values[i];
    SPECIAL1(fabs, x); SPECIAL1(floor, x); SPECIAL1(ceil, x);
    SPECIAL1(trunc, x); SPECIAL1(round, x); SPECIAL1(rint, x);
    SPECIAL1(sqrt, x); expect_same("cbrt(x)", purec_cbrt(x), cbrt_reference(x)); SPECIAL1(exp, x);
    SPECIAL1(exp2, x); SPECIAL1(expm1, x); SPECIAL1(log, x);
    SPECIAL1(log2, x); SPECIAL1(log10, x); SPECIAL1(log1p, x);
    if (!(fabs(x) > 3e9 && isfinite(x))) {
      SPECIAL1(sin, x); SPECIAL1(cos, x); SPECIAL1(tan, x);
    }
    SPECIAL1(asin, x); SPECIAL1(acos, x); SPECIAL1(atan, x);
    SPECIAL1(sinh, x); SPECIAL1(cosh, x); SPECIAL1(tanh, x);
    for (size_t j = 0; j < count; j++) {
      double y = values[j];
      SPECIAL2(pow, x, y); SPECIAL2(atan2, x, y); SPECIAL2(fmod, x, y);
      SPECIAL2(hypot, x, y); SPECIAL2(copysign, x, y);
      SPECIAL2(fmax, x, y); SPECIAL2(fmin, x, y);
    }
  }
}

static void exact_group(void) {
  int64_t worst_floor = 0, worst_round = 0, worst_fmod = 0, worst_ldexp = 0;
  int64_t worst_frexp = 0, worst_modf = 0, worst_sqrt = 0, worst_rint = 0;
  for (int i = 0; i < 200000; i++) {
    double x = uniform(-1e6, 1e6);
    if (i % 3 == 0) x = (double)(int64_t)(x * 4) / 4.0;
    int64_t d;
    d = ulp(purec_floor(x), floor(x)); if (d > worst_floor) worst_floor = d;
    d = ulp(purec_ceil(x), ceil(x)); if (d > worst_floor) worst_floor = d;
    d = ulp(purec_trunc(x), trunc(x)); if (d > worst_floor) worst_floor = d;
    d = ulp(purec_round(x), round(x)); if (d > worst_round) worst_round = d;
    d = ulp(purec_rint(x), rint(x)); if (d > worst_rint) worst_rint = d;
    double y = uniform(0.001, 1000.0);
    d = ulp(purec_fmod(x, y), fmod(x, y)); if (d > worst_fmod) worst_fmod = d;
    int e = (int)uniform(-1100, 1100);
    d = ulp(purec_ldexp(x, e), ldexp(x, e)); if (d > worst_ldexp) worst_ldexp = d;
    int e1 = 0, e2 = 0;
    double m1 = purec_frexp(x, &e1), m2 = frexp(x, &e2);
    d = ulp(m1, m2); if (e1 != e2) d = 99; if (d > worst_frexp) worst_frexp = d;
    double w1, w2;
    double f1 = purec_modf(x, &w1), f2 = modf(x, &w2);
    d = ulp(f1, f2) + ulp(w1, w2); if (d > worst_modf) worst_modf = d;
    double s = uniform(0.0, 1e12);
    d = ulp(purec_sqrt(s), sqrt(s)); if (d > worst_sqrt) worst_sqrt = d;
  }
  report("floor/ceil", worst_floor, 0); report("round", worst_round, 0);
  report("rint", worst_rint, 0); report("fmod", worst_fmod, 0);
  report("ldexp", worst_ldexp, 0); report("frexp", worst_frexp, 0);
  report("modf", worst_modf, 0); report("sqrt", worst_sqrt, 0);
}

int main(void) {
  specials();
  exact_group();

  SWEEP1("exp", exp, purec_exp, -700.0, 700.0, 200000, 1);
  SWEEP1("exp small", exp, purec_exp, -1.0, 1.0, 100000, 1);
  SWEEP1("exp2", exp2, purec_exp2, -1000.0, 1000.0, 100000, 1);
  SWEEP1("expm1", expm1, purec_expm1, -30.0, 30.0, 100000, 2);
  SWEEP1("expm1 tiny", expm1, purec_expm1, -1e-3, 1e-3, 100000, 2);
  SWEEPLOG("log", log, purec_log, 1e-300, 1e300, 200000, 1);
  SWEEP1("log near 1", log, purec_log, 0.7, 1.4, 100000, 1);
  SWEEPLOG("log2", log2, purec_log2, 1e-300, 1e300, 100000, 2);
  SWEEPLOG("log10", log10, purec_log10, 1e-300, 1e300, 100000, 2);
  SWEEP1("log1p", log1p, purec_log1p, -0.9, 10.0, 100000, 2);
  SWEEP1("sin", sin, purec_sin, -1000.0, 1000.0, 200000, 2);
  SWEEP1("sin small", sin, purec_sin, -1.6, 1.6, 100000, 1);
  SWEEP1("cos", cos, purec_cos, -1000.0, 1000.0, 200000, 2);
  SWEEP1("tan", tan, purec_tan, -1.5, 1.5, 100000, 2);
  SWEEP1("asin", asin, purec_asin, -1.0, 1.0, 100000, 2);
  SWEEP1("acos", acos, purec_acos, -1.0, 1.0, 100000, 2);
  SWEEP1("atan", atan, purec_atan, -1e6, 1e6, 100000, 1);
  SWEEP1("sinh", sinh, purec_sinh, -20.0, 20.0, 100000, 3);
  SWEEP1("cosh", cosh, purec_cosh, -20.0, 20.0, 100000, 3);
  SWEEP1("tanh", tanh, purec_tanh, -10.0, 10.0, 100000, 3);
  SWEEP1("cbrt", cbrt_reference, purec_cbrt, -1e9, 1e9, 100000, 1);

  int64_t worst = 0;
  for (int i = 0; i < 200000; i++) {
    double x = uniform(0.0, 100.0), y = uniform(-30.0, 30.0);
    int64_t d = ulp(purec_pow(x, y), pow(x, y));
    if (d > worst) worst = d;
  }
  report("pow", worst, 2);
  worst = 0;
  for (int i = 0; i < 100000; i++) {
    double x = uniform(-20.0, 20.0);
    double y = (double)(int)uniform(-15, 15);
    int64_t d = ulp(purec_pow(x, y), pow(x, y));
    if (d > worst) worst = d;
  }
  report("pow int", worst, 2);
  worst = 0;
  for (int e = 0; e <= 60; e++)
    for (int b = 2; b <= 12; b++) {
      double want = pow((double)b, (double)e);
      if (want >= 9007199254740992.0) continue;
      int64_t d = ulp(purec_pow((double)b, (double)e), want);
      if (d > 0 && failures < 10)
        printf("  pow(%d,%d) = %.17g want %.17g\n", b, e, purec_pow((double)b, (double)e), want);
      if (d > worst) worst = d;
    }
  report("pow exact", worst, 0);
  worst = 0;
  for (int e = -30; e < 0; e++)
    for (int b = 2; b <= 12; b++) {
      int64_t d = ulp(purec_pow((double)b, (double)e), pow((double)b, (double)e));
      if (d > worst) worst = d;
    }
  report("pow neg exp", worst, 1);
  worst = 0;
  for (int i = 0; i < 200000; i++) {
    double y = uniform(-100, 100), x = uniform(-100, 100);
    int64_t d = ulp(purec_atan2(y, x), atan2(y, x));
    if (d > worst) worst = d;
  }
  report("atan2", worst, 1);
  worst = 0;
  for (int i = 0; i < 100000; i++) {
    double x = uniform(-1e5, 1e5), y = uniform(-1e5, 1e5);
    int64_t d = ulp(purec_hypot(x, y), hypot(x, y));
    if (d > worst) worst = d;
  }
  report("hypot", worst, 1);

  int64_t wf = 0;
  for (int i = 0; i < 100000; i++) {
    float x = (float)uniform(-100.0, 100.0), p = (float)uniform(0.1, 20.0);
    int64_t d;
    d = ulp_float(purec_sinf(x), sinf(x)); if (d > wf) wf = d;
    d = ulp_float(purec_cosf(x), cosf(x)); if (d > wf) wf = d;
    d = ulp_float(purec_expf(x * 0.5f), expf(x * 0.5f)); if (d > wf) wf = d;
    d = ulp_float(purec_logf(p), logf(p)); if (d > wf) wf = d;
    d = ulp_float(purec_sqrtf(p), sqrtf(p)); if (d > wf) wf = d;
    d = ulp_float(purec_atanf(x), atanf(x)); if (d > wf) wf = d;
    d = ulp_float(purec_powf(p, x * 0.1f), powf(p, x * 0.1f)); if (d > wf) wf = d;
    d = ulp_float(purec_atan2f(x, p), atan2f(x, p)); if (d > wf) wf = d;
  }
  report("float fns", wf, 1);

  if (purec_lround(2.5) != 3 || purec_lround(-2.5) != -3 ||
      purec_lround(0.49999999999999994) != 0) {
    printf("lround FAIL\n");
    failures++;
  }
  printf(failures ? "libm_test: %d failure(s)\n" : "libm_test: ok\n", failures);
  return failures != 0;
}
