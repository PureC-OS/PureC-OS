// PureC hosted libc: floating-point environment (x87 + SSE MXCSR). Userspace may use both units.

#include "include/hosted/fenv.h"

#define X87_DEFAULT_CONTROL 0x037f
#define MXCSR_DEFAULT 0x1f80
#define MXCSR_FLAGS 0x3f
#define MXCSR_MASKS 0x1f80
#define MXCSR_ROUNDING 0x6000

static inline unsigned int mxcsr_read(void)
{
    unsigned int value;
    __asm__ volatile("stmxcsr %0" : "=m"(value));
    return value;
}

static inline void mxcsr_write(unsigned int value)
{
    __asm__ volatile("ldmxcsr %0" : : "m"(value));
}

static inline unsigned short x87_status(void)
{
    unsigned short value;
    __asm__ volatile("fnstsw %0" : "=am"(value));
    return value;
}

static inline unsigned short x87_control(void)
{
    unsigned short value;
    __asm__ volatile("fnstcw %0" : "=m"(value));
    return value;
}

static inline void x87_set_control(unsigned short value)
{
    __asm__ volatile("fldcw %0" : : "m"(value));
}

int fetestexcept(int excepts)
{
    int raised = (int)(x87_status() | mxcsr_read());
    return raised & excepts & FE_ALL_EXCEPT;
}

int feclearexcept(int excepts)
{
    excepts &= FE_ALL_EXCEPT;
    unsigned char env[28];
    __asm__ volatile("fnstenv %0" : "=m"(env));
    unsigned short status = (unsigned short)(env[4] | (env[5] << 8));
    status = (unsigned short)(status & ~excepts);
    env[4] = (unsigned char)status;
    env[5] = (unsigned char)(status >> 8);
    __asm__ volatile("fldenv %0" : : "m"(env));
    mxcsr_write(mxcsr_read() & ~(unsigned int)excepts);
    return 0;
}

int feraiseexcept(int excepts)
{
    excepts &= FE_ALL_EXCEPT;
    unsigned char env[28];
    __asm__ volatile("fnstenv %0" : "=m"(env));
    unsigned short status = (unsigned short)(env[4] | (env[5] << 8));
    status = (unsigned short)(status | excepts);
    env[4] = (unsigned char)status;
    env[5] = (unsigned char)(status >> 8);
    __asm__ volatile("fldenv %0" : : "m"(env));
    mxcsr_write(mxcsr_read() | (unsigned int)excepts);
    return 0;
}

int fegetexceptflag(fexcept_t *flagp, int excepts)
{
    *flagp = (fexcept_t)fetestexcept(excepts);
    return 0;
}

int fesetexceptflag(const fexcept_t *flagp, int excepts)
{
    excepts &= FE_ALL_EXCEPT;
    feclearexcept(excepts);
    return feraiseexcept((int)*flagp & excepts);
}

int fegetround(void)
{
    return x87_control() & FE_TOWARDZERO;
}

int fesetround(int mode)
{
    if (mode != FE_TONEAREST && mode != FE_DOWNWARD && mode != FE_UPWARD && mode != FE_TOWARDZERO)
        return 1;
    x87_set_control((unsigned short)((x87_control() & ~FE_TOWARDZERO) | mode));
    mxcsr_write((mxcsr_read() & ~MXCSR_ROUNDING) | ((unsigned int)mode << 3));
    return 0;
}

int fegetenv(fenv_t *envp)
{
    __asm__ volatile("fnstenv %0" : "=m"(envp->x87));
    __asm__ volatile("fldenv %0" : : "m"(envp->x87));
    envp->mxcsr = mxcsr_read();
    return 0;
}

int feholdexcept(fenv_t *envp)
{
    fegetenv(envp);
    __asm__ volatile("fnclex");
    mxcsr_write((mxcsr_read() | MXCSR_MASKS) & ~(unsigned int)MXCSR_FLAGS);
    return 0;
}

int fesetenv(const fenv_t *envp)
{
    if (envp == FE_DFL_ENV) {
        __asm__ volatile("fnclex");
        x87_set_control(X87_DEFAULT_CONTROL);
        mxcsr_write(MXCSR_DEFAULT);
        return 0;
    }
    __asm__ volatile("fldenv %0" : : "m"(envp->x87));
    mxcsr_write(envp->mxcsr);
    return 0;
}

int feupdateenv(const fenv_t *envp)
{
    int pending = fetestexcept(FE_ALL_EXCEPT);
    fesetenv(envp);
    return feraiseexcept(pending);
}
