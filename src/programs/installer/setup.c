#include "setup.h"
#include "../../libc/include/purec.h"
#include "../accounts/accounts.h"
#include "../../fs/install_accounts.h"

static void wipe(void *memory, uint32_t size){
    volatile uint8_t *bytes=(volatile uint8_t *)memory;
    for(uint32_t i=0;i<size;i++) bytes[i]=0;
}

bool setup_build_request(const struct install_settings *settings,
                         struct install_account_request *request){
    wipe(request,sizeof(*request));
    request->present=1;
    request->user_uid=INSTALL_FIRST_UID;
    pc_copy(request->hostname,settings->hostname,sizeof(request->hostname));
    pc_copy(request->user_name,settings->user_name,sizeof(request->user_name));
    pc_copy(request->user_gecos,settings->user_gecos,sizeof(request->user_gecos));
    pc_copy(request->timezone,settings->timezone,sizeof(request->timezone));
    if(!account_hash_password(settings->root_password,request->root_hash,
                              sizeof(request->root_hash))) return false;
    if(!account_hash_password(settings->user_password,request->user_hash,
                              sizeof(request->user_hash))) return false;
    return install_accounts_valid(request);
}

void setup_wipe(struct install_settings *settings){
    wipe(settings,sizeof(*settings));
}
