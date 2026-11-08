#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#define PR_SET_NAME 15
#define PR_GET_NAME 16

int prctl(int option, ...);
#ifdef __cplusplus
}
#endif
