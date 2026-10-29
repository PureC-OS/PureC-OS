#include "setup.h"
#include "../accounts/accounts.h"
#include "../../fs/install_accounts.h"

#define SETUP_DEFAULT_HOSTNAME "purec"

static void wipe(void *memory, uint32_t size){
    volatile uint8_t *bytes=(volatile uint8_t *)memory;
    for(uint32_t i=0;i<size;i++) bytes[i]=0;
}

static void heading(const char *title, uint32_t step, uint32_t total){
    pc_console_clear();
    pc_write("Pure OS installation - step ");
    pc_write_u64(step);
    pc_write(" of ");
    pc_write_u64(total);
    pc_write("\n");
    pc_write("------------------------------------------------------------\n");
    pc_write(title);
    pc_write("\n\n");
}

static bool ask_hostname(struct terminal_window *window, char *out){
    char input[INSTALL_HOSTNAME_MAX];
    for(;;){
        if(!terminal_window_read_line(window,
                "Computer name (Enter for \"" SETUP_DEFAULT_HOSTNAME "\"): ",
                input,sizeof(input))) return false;
        if(!input[0]) pc_copy(input,SETUP_DEFAULT_HOSTNAME,sizeof(input));
        if(install_account_hostname_valid(input)){
            pc_copy(out,input,INSTALL_HOSTNAME_MAX);
            return true;
        }
        pc_write("Use lowercase letters, digits, '-' and '.' only.\n");
    }
}

static bool ask_password(struct terminal_window *window, const char *label,
                         char *out){
    char first[SETUP_PASSWORD_MAX];
    char second[SETUP_PASSWORD_MAX];
    bool ok=false;
    for(;;){
        if(!terminal_window_read_secret(window,label,first,sizeof(first)))
            break;
        if(pc_strlen(first)<ACCOUNT_PASSWORD_MIN){
            pc_write("The password must have at least ");
            pc_write_u64(ACCOUNT_PASSWORD_MIN);
            pc_write(" characters.\n");
            continue;
        }
        if(!terminal_window_read_secret(window,"Repeat the password: ",
                                        second,sizeof(second)))
            break;
        if(pc_strcmp(first,second)!=0){
            pc_write("The passwords do not match. Try again.\n");
            continue;
        }
        pc_copy(out,first,SETUP_PASSWORD_MAX);
        ok=true;
        break;
    }
    wipe(first,sizeof(first));
    wipe(second,sizeof(second));
    return ok;
}

static bool ask_user_name(struct terminal_window *window, char *out){
    char input[INSTALL_USER_MAX];
    for(;;){
        if(!terminal_window_read_line(window,"User name: ",input,
                                      sizeof(input))) return false;
        if(pc_strcmp(input,"root")==0){
            pc_write("\"root\" is the administrator account created above.\n");
            continue;
        }
        if(input[0]=='_' || !install_account_name_valid(input)){
            pc_write("Start with a lowercase letter; use lowercase letters, "
                     "digits, '-' and '_'.\n");
            continue;
        }
        pc_copy(out,input,INSTALL_USER_MAX);
        return true;
    }
}

static bool ask_full_name(struct terminal_window *window, const char *fallback,
                          char *out){
    char input[INSTALL_GECOS_MAX];
    for(;;){
        if(!terminal_window_read_line(window,
                "Full name (Enter to use the user name): ",input,
                sizeof(input))) return false;
        if(!input[0]) pc_copy(input,fallback,sizeof(input));
        if(install_account_gecos_valid(input)){
            pc_copy(out,input,INSTALL_GECOS_MAX);
            return true;
        }
        pc_write("The name cannot contain ':' or control characters.\n");
    }
}

bool setup_collect(struct terminal_window *window,
                   struct install_settings *settings){
    setup_wipe(settings);
    heading("Computer name",2,5);
    if(!ask_hostname(window,settings->hostname)) return false;
    heading("Administrator (root) password",3,5);
    pc_write("The root account can change anything on this computer.\n\n");
    if(!ask_password(window,"Root password: ",settings->root_password))
        return false;
    heading("Your user account",4,5);
    pc_write("This account is for everyday work. Its files are private.\n\n");
    if(!ask_user_name(window,settings->user_name)) return false;
    if(!ask_full_name(window,settings->user_name,settings->user_gecos))
        return false;
    if(!ask_password(window,"Password for the new user: ",
                     settings->user_password)) return false;
    return true;
}

void setup_summary(const struct install_settings *settings){
    pc_write("Computer name: ");
    pc_write(settings->hostname);
    pc_write("\nRoot account:  root (password set)\n");
    pc_write("User account:  ");
    pc_write(settings->user_name);
    pc_write(" (");
    pc_write(settings->user_gecos);
    pc_write("), password set, home /home/");
    pc_write(settings->user_name);
    pc_write("\n");
}

bool setup_build_request(const struct install_settings *settings,
                         struct install_account_request *request){
    wipe(request,sizeof(*request));
    request->present=1;
    request->user_uid=INSTALL_FIRST_UID;
    pc_copy(request->hostname,settings->hostname,sizeof(request->hostname));
    pc_copy(request->user_name,settings->user_name,sizeof(request->user_name));
    pc_copy(request->user_gecos,settings->user_gecos,sizeof(request->user_gecos));
    if(!account_hash_password(settings->root_password,request->root_hash,
                              sizeof(request->root_hash))) return false;
    if(!account_hash_password(settings->user_password,request->user_hash,
                              sizeof(request->user_hash))) return false;
    return install_accounts_valid(request);
}

void setup_wipe(struct install_settings *settings){
    wipe(settings,sizeof(*settings));
}
