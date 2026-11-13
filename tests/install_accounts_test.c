#include "fs/install_accounts.h"
#include "fs/vfs_perm.h"

#include <assert.h>
#include <string.h>

static struct install_account_request sample(void) {
  struct install_account_request r;
  memset(&r, 0, sizeof(r));
  r.present = 1;
  r.user_count = 2;
  strcpy(r.hostname, "purec-box");
  strcpy(r.root_hash, "$6$rootsalt$aaaaaaaaaaaaaaaaaaaaaaaa");
  r.users[0].uid = 1000;
  r.users[0].admin = 1;
  strcpy(r.users[0].name, "ann");
  strcpy(r.users[0].gecos, "Ann Lee");
  strcpy(r.users[0].hash, "$6$usersalt$bbbbbbbbbbbbbbbbbbbbbbbb");
  r.users[1].uid = 1001;
  strcpy(r.users[1].name, "bob");
  strcpy(r.users[1].gecos, "Bob");
  strcpy(r.users[1].hash, "$6$bobsalt$cccccccccccccccccccccccc");
  return r;
}

int main(void) {
  struct install_account_request r = sample();
  assert(install_accounts_valid(&r));

  struct install_account_files files;
  assert(install_accounts_render(&r, &files) == 0);
  assert(strcmp(files.hostname, "purec-box\n") == 0);

  struct user_entry e;
  assert(user_lookup_name(files.passwd, "root", &e));
  assert(e.uid == 0 && e.gid == 0 && strcmp(e.home, "/root") == 0);
  assert(user_lookup_name(files.passwd, "ann", &e));
  assert(e.uid == 1000 && e.gid == 100 && strcmp(e.gecos, "Ann Lee") == 0);
  assert(strcmp(e.home, "/home/ann") == 0);
  assert(user_lookup_uid(files.passwd, 1000, &e) && strcmp(e.name, "ann") == 0);

  assert(strstr(files.shadow, "root:$6$rootsalt$") == files.shadow);
  assert(strstr(files.shadow, "\nann:$6$usersalt$") != 0);
  assert(strstr(files.group, "users:x:100:ann,bob\n") != 0);
  assert(strstr(files.group, "wheel:x:10:ann\n") != 0);
  assert(user_lookup_uid(files.passwd, 1001, &e) && strcmp(e.name, "bob") == 0);

  struct install_account_request none = sample();
  none.user_count = 0;
  assert(install_accounts_render(&none, &files) == 0);
  assert(strstr(files.group, "wheel:x:10:\nusers:x:100:\n") != 0);

  struct install_account_request dup = sample();
  strcpy(dup.users[1].name, "ann");
  assert(!install_accounts_valid(&dup));
  dup = sample();
  dup.users[1].uid = 1000;
  assert(!install_accounts_valid(&dup));

  struct install_account_request bad = sample();
  strcpy(bad.users[0].name, "root");
  assert(!install_accounts_valid(&bad));
  bad = sample();
  strcpy(bad.users[0].name, "Ann");
  assert(!install_accounts_valid(&bad));
  bad = sample();
  strcpy(bad.users[0].name, "a:b");
  assert(!install_accounts_valid(&bad));
  bad = sample();
  strcpy(bad.users[0].gecos, "x:y");
  assert(!install_accounts_valid(&bad));
  bad = sample();
  strcpy(bad.root_hash, "plain");
  assert(!install_accounts_valid(&bad));
  bad = sample();
  strcpy(bad.users[0].hash, "$6$s$ha:sh");
  assert(!install_accounts_valid(&bad));
  bad = sample();
  strcpy(bad.hostname, "-bad");
  assert(!install_accounts_valid(&bad));
  bad = sample();
  strcpy(bad.hostname, "Bad_Host");
  assert(!install_accounts_valid(&bad));
  bad = sample();
  bad.users[0].uid = 0;
  assert(!install_accounts_valid(&bad));
  bad = sample();
  bad.present = 0;
  assert(!install_accounts_valid(&bad));
  bad = sample();
  memset(bad.users[0].gecos, 'g', sizeof(bad.users[0].gecos));
  assert(!install_accounts_valid(&bad));
  assert(install_accounts_render(&bad, &files) < 0);
  return 0;
}
