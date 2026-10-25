#pragma once

#include <stdint.h>

static inline int64_t userspace_syscall(uint64_t number, uint64_t argument1,
                                        uint64_t argument2, uint64_t argument3){
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number),"b"(argument1),"c"(argument2),"d"(argument3)
        : "r10","r8","memory"
    );
    return result;
}
