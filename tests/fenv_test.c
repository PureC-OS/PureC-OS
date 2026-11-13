/* Host test for src/libc/fenv.c: flags are produced by real SSE operations. */
#include <stdio.h>
#include <string.h>

#define FE_ALL_EXCEPT_HOST 0x3d
int pc_feclearexcept(int);
int pc_fetestexcept(int);
int pc_feraiseexcept(int);
int pc_fegetround(void);
int pc_fesetround(int);
int pc_fegetenv(void *);
int pc_fesetenv(const void *);
int pc_feholdexcept(void *);
int pc_feupdateenv(const void *);
int pc_fegetexceptflag(unsigned short *, int);
int pc_fesetexceptflag(const unsigned short *, int);

#define FE_INVALID 0x01
#define FE_DIVBYZERO 0x04
#define FE_OVERFLOW 0x08
#define FE_INEXACT 0x20
#define FE_TONEAREST 0x000
#define FE_DOWNWARD 0x400
#define FE_UPWARD 0x800
#define FE_TOWARDZERO 0xc00

static int failures;
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition);         \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static volatile double zero = 0.0, one = 1.0, big = 1e308, three = 3.0;

int main(void)
{
    pc_feclearexcept(FE_ALL_EXCEPT_HOST);
    CHECK(pc_fetestexcept(FE_ALL_EXCEPT_HOST) == 0);

    volatile double r = one / zero;
    (void)r;
    CHECK(pc_fetestexcept(FE_DIVBYZERO) == FE_DIVBYZERO);
    CHECK(pc_fetestexcept(FE_OVERFLOW) == 0);
    pc_feclearexcept(FE_DIVBYZERO);
    CHECK(pc_fetestexcept(FE_DIVBYZERO) == 0);

    r = big * big;
    CHECK(pc_fetestexcept(FE_OVERFLOW) == FE_OVERFLOW);
    r = one / three;
    CHECK(pc_fetestexcept(FE_INEXACT) == FE_INEXACT);
    r = zero / zero;
    CHECK(pc_fetestexcept(FE_INVALID) == FE_INVALID);
    pc_feclearexcept(FE_ALL_EXCEPT_HOST);
    CHECK(pc_fetestexcept(FE_ALL_EXCEPT_HOST) == 0);

    CHECK(pc_feraiseexcept(FE_INVALID | FE_INEXACT) == 0);
    CHECK(pc_fetestexcept(FE_ALL_EXCEPT_HOST) == (FE_INVALID | FE_INEXACT));
    unsigned short saved;
    pc_fegetexceptflag(&saved, FE_ALL_EXCEPT_HOST);
    pc_feclearexcept(FE_ALL_EXCEPT_HOST);
    pc_fesetexceptflag(&saved, FE_INVALID);
    CHECK(pc_fetestexcept(FE_ALL_EXCEPT_HOST) == FE_INVALID);
    pc_feclearexcept(FE_ALL_EXCEPT_HOST);

    CHECK(pc_fegetround() == FE_TONEAREST);
    int modes[] = {FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO, FE_TONEAREST};
    for (int i = 0; i < 4; i++) {
        CHECK(pc_fesetround(modes[i]) == 0);
        CHECK(pc_fegetround() == modes[i]);
    }
    CHECK(pc_fesetround(0x123) != 0);
    CHECK(pc_fegetround() == FE_TONEAREST);

    pc_fesetround(FE_UPWARD);
    r = one / three;
    volatile double up = r;
    pc_fesetround(FE_DOWNWARD);
    r = one / three;
    volatile double down = r;
    CHECK(up > down);
    pc_fesetround(FE_TONEAREST);

    unsigned char env[32], held[32];
    pc_fesetround(FE_TOWARDZERO);
    CHECK(pc_fegetenv(env) == 0);
    pc_fesetround(FE_UPWARD);
    CHECK(pc_fesetenv(env) == 0);
    CHECK(pc_fegetround() == FE_TOWARDZERO);

    pc_fesetround(FE_TONEAREST);
    pc_feraiseexcept(FE_OVERFLOW);
    CHECK(pc_feholdexcept(held) == 0);
    CHECK(pc_fetestexcept(FE_ALL_EXCEPT_HOST) == 0);
    pc_feraiseexcept(FE_INEXACT);
    CHECK(pc_feupdateenv(held) == 0);
    CHECK(pc_fetestexcept(FE_ALL_EXCEPT_HOST) == (FE_OVERFLOW | FE_INEXACT));

    CHECK(pc_fesetenv((const void *)-1) == 0);
    CHECK(pc_fetestexcept(FE_ALL_EXCEPT_HOST) == 0);
    CHECK(pc_fegetround() == FE_TONEAREST);

    if (failures) {
        printf("fenv_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("fenv_test: ok\n");
    return 0;
}
