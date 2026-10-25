// fputest: FPU/SSE self-test for PureC OS userspace.
// Exercises doubles + xmm pressure across voluntary preemptions and
// verifies state survives context switches (the kernel eagerly
// saves/restores FPU per thread). Prints PASS/FAIL lines; exit status
// is the failure count (0 = all good).
//
// Build: like any native program (own _start). Run: /bin/program/fputest

#include "../../libc/include/purec.h"
#include "../../libc/include/hosted/stdlib.h"

static int failures = 0;

static void check(bool ok, const char *name) {
    pc_write(ok ? "[PASS] " : "[FAIL] ");
    pc_write(name);
    pc_write("\n");
    if (!ok) failures++;
}

static double touch(double x) {
    // Force the value through calls/memory so it cannot sit in one
    // register for the whole test.
    volatile double slot = x;
    return slot;
}

void _start(void) {
    pc_write("fputest: FPU context-switch self-test\n");

    // 1. Basic double arithmetic (SSE codegen in userspace).
    {
        double a = 1.5, b = 2.25;
        check(touch(a + b) == 3.75, "add");
        check(touch(a * b) == 3.375, "mul");
        check(touch(b - a) == 0.75, "sub");
        check(touch(b / 3.0) == 0.75, "div");
        check((int)touch(3.99) == 3, "double->int");
        check(touch((double)7) == 7.0, "int->double");
    }

    // 2. XMM pressure: many live doubles across calls.
    {
        double d0 = 0.5, d1 = 1.5, d2 = 2.5, d3 = 3.5;
        double d4 = 4.5, d5 = 5.5, d6 = 6.5, d7 = 7.5;
        double sum = touch(d0) + touch(d1) + touch(d2) + touch(d3)
            + touch(d4) + touch(d5) + touch(d6) + touch(d7);
        check(sum == 32.0, "xmm pressure");
    }

    // 3. Survival across preemptions: touch, sleep (blocks -> switch),
    // verify, repeat. Any FPU leak between threads flips these.
    {
        double keep = 3.14159265358979;
        double acc = 0.0;
        bool ok = true;
        for (int i = 0; i < 200; i++) {
            keep = touch(keep * 1.0 + (double)(i % 7) * 0.0);
            if (keep != 3.14159265358979) { ok = false; break; }
            acc += touch((double)i * 0.5);
            pc_sleep(1);
        }
        check(ok, "survive 200 preemptions");
        check(acc == 9950.0, "accumulate across switches");
    }

    // 4. strtod through hosted libc.
    {
        char *end = 0;
        double v = strtod("2.5", &end);
        check(v == 2.5 && end && *end == '\0', "strtod 2.5");
    }

    pc_write(failures ? "fputest: FAILURES present\n" : "fputest: ALL PASS\n");
    pc_exit(failures ? 1 : 0);
}
