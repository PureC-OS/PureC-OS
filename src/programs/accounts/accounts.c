#include "accounts.h"
#include "accounts_core.h"
#include "purecrypt.h"

#define SHADOW_PATH "/etc/shadow"
#define SHADOW_CAPACITY 4096

bool account_hash_password(const char *password, char *out, uint32_t capacity) {
    if(!password || !out || capacity < PURECRYPT_HASH_MAX) return false;
    uint8_t raw[16];
    if(pc_getrandom(raw,sizeof(raw))!=(int32_t)sizeof(raw)) return false;
    char salt[17];
    purecrypt_gensalt(raw,salt);
    for(uint32_t i=0;i<sizeof(raw);i++) raw[i]=0;
    return purecrypt_hash(password,salt,0,out)==0;
}

static bool read_shadow(char *buffer, uint32_t capacity){
    int32_t descriptor=pc_file_open(SHADOW_PATH);
    if(descriptor<0) return false;
    uint32_t total=0;
    while(total+1<capacity){
        int32_t got=pc_file_read(descriptor,buffer+total,capacity-1-total);
        if(got<=0) break;
        total+=(uint32_t)got;
    }
    pc_file_close(descriptor);
    buffer[total]='\0';
    return total>0;
}

bool account_login(const char *name, const char *password,
                   struct user_info *info){
    static char shadow[SHADOW_CAPACITY];
    if(!name || !name[0] || !password) return false;
    if(!read_shadow(shadow,sizeof(shadow))) return false;
    bool ok=accounts_check(shadow,name,password);
    for(uint32_t i=0;i<sizeof(shadow);i++) shadow[i]=0;
    if(!ok) return false;
    return pc_user_lookup(name,0,info)==0;
}
