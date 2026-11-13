#include "setup.h"
#include "../../libc/include/purec.h"
#include "../accounts/accounts.h"
#include "../../fs/install_accounts.h"

static void wipe(void *memory, uint32_t size){
    volatile uint8_t *bytes=(volatile uint8_t *)memory;
    for(uint32_t i=0;i<size;i++) bytes[i]=0;
}

static bool same(const char *a, const char *b){
    while(*a && *a==*b){ a++; b++; }
    return *a==*b;
}

bool setup_user_taken(const struct install_settings *settings, const char *name){
    for(uint32_t i=0;i<settings->user_count;i++)
        if(same(settings->users[i].name,name)) return true;
    return false;
}

bool setup_add_user(struct install_settings *settings){
    if(settings->user_count>=INSTALL_USERS_MAX) return false;
    if(setup_user_taken(settings,settings->user_name)) return false;
    struct setup_user *user=&settings->users[settings->user_count];
    wipe(user,sizeof(*user));
    pc_copy(user->name,settings->user_name,sizeof(user->name));
    pc_copy(user->gecos,settings->user_gecos,sizeof(user->gecos));
    if(!account_hash_password(settings->user_password,user->hash,sizeof(user->hash))){
        wipe(user,sizeof(*user));
        return false;
    }
    user->admin=settings->user_admin;
    settings->user_count++;
    wipe(settings->user_name,sizeof(settings->user_name));
    wipe(settings->user_gecos,sizeof(settings->user_gecos));
    wipe(settings->user_password,sizeof(settings->user_password));
    settings->user_admin=false;
    return true;
}

void setup_remove_user(struct install_settings *settings, uint32_t index){
    if(index>=settings->user_count) return;
    for(uint32_t i=index;i+1<settings->user_count;i++)
        settings->users[i]=settings->users[i+1];
    settings->user_count--;
    wipe(&settings->users[settings->user_count],sizeof(settings->users[0]));
}

bool setup_build_request(const struct install_settings *settings,
                         struct install_account_request *request){
    wipe(request,sizeof(*request));
    request->present=1;
    pc_copy(request->hostname,settings->hostname,sizeof(request->hostname));
    pc_copy(request->timezone,settings->timezone,sizeof(request->timezone));
    if(!account_hash_password(settings->root_password,request->root_hash,
                              sizeof(request->root_hash))) return false;
    request->user_count=settings->user_count;
    for(uint32_t i=0;i<settings->user_count;i++){
        struct install_user *out=&request->users[i];
        const struct setup_user *in=&settings->users[i];
        pc_copy(out->name,in->name,sizeof(out->name));
        pc_copy(out->gecos,in->gecos,sizeof(out->gecos));
        pc_copy(out->hash,in->hash,sizeof(out->hash));
        out->uid=INSTALL_FIRST_UID+i;
        out->admin=in->admin ? 1u : 0u;
    }
    return install_accounts_valid(request);
}

void setup_wipe(struct install_settings *settings){
    wipe(settings,sizeof(*settings));
}
