#ifndef PUREC_HOSTED_SCHED_H
#define PUREC_HOSTED_SCHED_H

#ifdef __cplusplus
extern "C"
{
#endif

    struct sched_param
    {
        int sched_priority;
    };

#define SCHED_OTHER 0
#define SCHED_FIFO 1
#define SCHED_RR 2

    int sched_yield(void);

#ifdef __cplusplus
}
#endif

#endif
