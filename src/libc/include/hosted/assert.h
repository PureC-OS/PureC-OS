#pragma once
// PureC hosted libc: assert. Active unless NDEBUG.

#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
extern void assert_fail(const char *expression, const char *file, int line);
#define assert(expression) \
    ((expression) ? (void)0 : assert_fail(#expression, __FILE__, __LINE__))
#endif
