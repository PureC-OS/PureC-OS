#include "auth.h"
#include "accounts.h"

#define SHADOW_PATH "/etc/shadow"

bool purec_auth_live_session(void){
    int32_t shadow=pc_file_open(SHADOW_PATH);
    if(shadow>=0){
        pc_file_close(shadow);
        return false;
    }
    return true;
}

bool purec_auth_check(const char *name, const char *password,
                      uint32_t *uid, uint32_t *gid){
    struct user_info info;
    if(!account_login(name,password,&info)) return false;
    *uid=info.uid;
    *gid=info.gid;
    return true;
}

bool purec_auth_begin_session(uint32_t uid, uint32_t gid){
    return pc_session_begin(uid,gid)==0;
}

void purec_auth_desktop_redraw(void){
    pc_desktop_redraw();
}
