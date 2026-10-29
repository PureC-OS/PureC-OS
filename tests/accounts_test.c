#include "programs/accounts/accounts_core.h"
#include "fs/install_accounts.h"
#include "purecrypt.h"

#include <assert.h>
#include <string.h>

int main(void) {
  char root_hash[PURECRYPT_HASH_MAX];
  char user_hash[PURECRYPT_HASH_MAX];
  assert(purecrypt_hash("rootpass1", "rootsalt12345678", 0, root_hash) == 0);
  assert(purecrypt_hash("annpass22", "annsalt123456789", 0, user_hash) == 0);

  struct install_account_request request;
  memset(&request, 0, sizeof(request));
  request.present = 1;
  request.user_uid = 1000;
  strcpy(request.hostname, "box");
  strcpy(request.root_hash, root_hash);
  strcpy(request.user_name, "ann");
  strcpy(request.user_gecos, "Ann");
  strcpy(request.user_hash, user_hash);
  struct install_account_files files;
  assert(install_accounts_render(&request, &files) == 0);

  assert(accounts_check(files.shadow, "root", "rootpass1"));
  assert(accounts_check(files.shadow, "ann", "annpass22"));
  assert(!accounts_check(files.shadow, "ann", "rootpass1"));
  assert(!accounts_check(files.shadow, "root", "annpass22"));
  assert(!accounts_check(files.shadow, "ann", ""));
  assert(!accounts_check(files.shadow, "ann", "annpass2"));
  assert(!accounts_check(files.shadow, "bob", "annpass22"));
  assert(!accounts_check(files.shadow, "an", "annpass22"));
  assert(!accounts_check(files.shadow, "", "annpass22"));
  assert(!accounts_check("ann:!:::::::\n", "ann", "annpass22"));
  assert(!accounts_check("ann:*:::::::\n", "ann", "annpass22"));
  assert(!accounts_check("ann::::::::\n", "ann", ""));
  assert(!accounts_check(0, "ann", "x"));

  char out[PURECRYPT_HASH_MAX];
  assert(shadow_hash_for(files.shadow, "ann", out, sizeof(out)));
  assert(strcmp(out, user_hash) == 0);
  assert(!shadow_hash_for(files.shadow, "ann", out, 10));
  return 0;
}
