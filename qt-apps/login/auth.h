#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool purec_auth_live_session(void);
bool purec_auth_check(const char *name, const char *password, uint32_t *uid, uint32_t *gid);
bool purec_auth_begin_session(uint32_t uid, uint32_t gid);
void purec_auth_desktop_redraw(void);

#ifdef __cplusplus
}
#endif
