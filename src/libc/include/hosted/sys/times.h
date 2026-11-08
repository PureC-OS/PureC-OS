#pragma once

#include "../time.h"

#ifdef __cplusplus
extern "C" {
#endif
struct tms {
    clock_t tms_utime;
    clock_t tms_stime;
    clock_t tms_cutime;
    clock_t tms_cstime;
};

clock_t times(struct tms *buffer);
#ifdef __cplusplus
}
#endif
