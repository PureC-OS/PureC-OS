#include "../sync/mutex.h"
static mutex_t process_mutex;
#include "../../drivers/display/gop.h"
#include "../../drivers/interrupts/timer.h"
#include "../../fs/initramfs.h"
#include "../../fs/vfs.h"
#include "../../fs/vfs_alias.h"
#include "../../fs/vfs_path.h"
#include "../../lib/string.h"
#include "../../mm/oom/oom_account.h"
#include "../../mm/oom/oom_gate.h"
#include "../../mm/oom/oom_slab.h"
#include "../../mm/oom/oom_victim.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../../net/transport/tcp.h"
#include "../../userspace/window_manager.h"
#include "../diagnostics/klog.h"
#include "../diagnostics/panic.h"
#include "../syscall/syscall.h"
#include "../ipc/ipc.h"
#include "elf.h"
#include "process.h"
#include "program_alias.h"
#include "scheduler.h"
#include "signal.h"
#include "fdtable.h"
#include "../../arch/x86_64/fpu/include/fpu.h"

#define USER_STACK_TOP 0x00007FFFFFF00000ULL
#define USER_STACK_PAGES 16
#define USER_STACK_MAX_PAGES 2048
#define USER_HEAP_GUARD_PAGES 1
#define USER_PROCESS_PRIORITY 1
#define SPAWN_MAX_OBJECTS (ELF_MAX_NEEDED + 1)
#define SPAWN_PATH_CAP 128
#define SPAWN_FILE_LIMIT (64ULL * 1024ULL * 1024ULL)
#define SPAWN_READ_CHUNK 1048576U

struct spawn_image {
  const void *data;
  uint64_t size;
  uint64_t phys;
  uint64_t pages;
  bool owned;
  char path[SPAWN_PATH_CAP];
};

extern void arch_enter_user(uint64_t instruction_pointer,
                            uint64_t stack_pointer) __attribute__((noreturn));

static struct process *process_list;
static uint32_t next_pid = 1;
static uint64_t process_sample_tick;
static const char *last_spawn_error = "unknown process load failure";

const char *process_last_spawn_error(void) { return last_spawn_error; }

#define PROCESS_NODE_PAGES ((sizeof(struct process) + 4095) / 4096)

#define PHASH_BITS 8
#define PHASH_SIZE (1u << PHASH_BITS)
static struct process *phash_buckets[PHASH_SIZE];
static uint32_t phash_idx(uint32_t pid) {
  pid ^= pid >> PHASH_BITS;
  return pid & (PHASH_SIZE - 1);
}
static void phash_insert(struct process *p) {
  uint32_t i = phash_idx(p->pid);
  p->hnext = phash_buckets[i];
  phash_buckets[i] = p;
}
static void phash_remove(struct process *p) {
  uint32_t i = phash_idx(p->pid);
  struct process **link = &phash_buckets[i];
  while (*link && *link != p)
    link = &(*link)->hnext;
  if (*link)
    *link = p->hnext;
  p->hnext = NULL;
}
static struct process *process_node_alloc(void) {
  uint64_t phys = pmm_allocate_contiguous(PROCESS_NODE_PAGES);
  if (!phys)
    return NULL;
  struct process *process = (struct process *)pmm_physical_to_virtual(phys);
  memset(process, 0, sizeof(*process));
  process->node_phys = phys;
  return process;
}

static void process_node_free(struct process *process) {
  if (!process)
    return;
  oom_gate_remove_process(process->pid);
  ipc_cleanup(process);
  signal_free(process);
  uint64_t phys = process->node_phys;
  struct process **link = &process_list;
  while (*link && *link != process)
    link = &(*link)->next;
  if (*link)
    *link = process->next;
  phash_remove(process);
  if (phys)
    pmm_free_contiguous(phys, PROCESS_NODE_PAGES);
}

static int32_t spawn_fail(struct process *process, struct spawn_image *images,
                          uint64_t image_count, const char *message) {
  last_spawn_error = message;
  for (uint64_t i = 0; i < image_count; i++) {
    if (images[i].owned && images[i].phys)
      pmm_free_contiguous(images[i].phys, images[i].pages);
  }
  vmm_destroy_address_space(process->address_space);
  process->address_space = 0;
  process_node_free(process);
  return -1;
}

static void spawn_join_lib_path(char *out, const char *name) {
  uint64_t pos = 0;
  const char *prefix = "/lib/";
  while (*prefix && pos + 1 < SPAWN_PATH_CAP)
    out[pos++] = *prefix++;
  bool slash = false;
  for (const char *c = name; *c; c++) {
    if (*c == '/') {
      slash = true;
      break;
    }
  }
  if (slash) {
    while (*name && pos + 1 < SPAWN_PATH_CAP)
      out[pos++] = *name++;
  } else {
    while (*name && pos + 1 < SPAWN_PATH_CAP)
      out[pos++] = *name++;
  }
  out[pos] = 0;
}

static bool spawn_images_have(struct spawn_image *images, uint64_t count,
                              const char *path) {
  for (uint64_t i = 0; i < count; i++) {
    if (strcmp(images[i].path, path) == 0)
      return true;
  }
  return false;
}

static const char *canonical_path(const char *path, char *buffer,
                                  uint32_t capacity) {
  return vfs_alias_resolve(path, buffer, capacity) >= 0 ? buffer : path;
}

static bool spawn_load_image(const char *requested, struct spawn_image *out) {
  char canonical[512];
  const char *path = canonical_path(requested, canonical, sizeof(canonical));
  memset(out, 0, sizeof(*out));
  strncpy(out->path, path, sizeof(out->path) - 1);
  out->path[sizeof(out->path) - 1] = 0;
  const void *ram = 0;
  uint32_t ram_size = 0;
  if (initramfs_find(path, &ram, &ram_size)) {
    out->data = ram;
    out->size = ram_size;
    out->owned = false;
    return out->size > 0;
  }
  int32_t fd = vfs_open(path);
  if (fd < 0)
    return false;
  struct file_stat_info st = {0};
  uint64_t blob_size = 0;
  if (vfs_stat(path, &st) >= 0)
    blob_size = st.size;
  if (!blob_size || blob_size > SPAWN_FILE_LIMIT) {
    vfs_close(fd);
    return false;
  }
  uint64_t pages = (blob_size + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
  uint64_t phys = pmm_allocate_contiguous(pages);
  if (!phys) {
    vfs_close(fd);
    return false;
  }
  uint8_t *vbuf = (uint8_t *)pmm_physical_to_virtual(phys);
  uint64_t done = 0;
  bool failed = false;
  while (done < blob_size) {
    uint64_t left = blob_size - done;
    uint32_t want = left > SPAWN_READ_CHUNK ? SPAWN_READ_CHUNK : (uint32_t)left;
    int32_t got = vfs_read(fd, vbuf + done, want);
    if (got <= 0) {
      failed = true;
      break;
    }
    done += (uint64_t)got;
  }
  vfs_close(fd);
  if (failed || done != blob_size) {
    pmm_free_contiguous(phys, pages);
    return false;
  }
  out->data = vbuf;
  out->size = done;
  out->phys = phys;
  out->pages = pages;
  out->owned = true;
  return true;
}

static struct process *process_find_by_pid(uint32_t pid) {
  if (!pid)
    return NULL;
  uint32_t i = phash_idx(pid);
  for (struct process *p = phash_buckets[i]; p; p = p->hnext)
    if (p->state != PROCESS_FREE && p->pid == pid)
      return p;
  return NULL;
}

struct process *process_lookup_by_pid(uint32_t pid) {
  return process_find_by_pid(pid);
}

void process_table_lock(void) {
  mutex_lock(&process_mutex);
}

void process_table_unlock(void) {
  mutex_unlock(&process_mutex);
}

static bool environment_name_valid(const char *name) {
  if (!name || !name[0])
    return false;
  for (uint32_t index = 0; name[index]; index++) {
    char character = name[index];
    if (index >= PROCESS_ENVIRONMENT_NAME_CAPACITY - 1 ||
        !((character >= 'a' && character <= 'z') ||
          (character >= 'A' && character <= 'Z') || character == '_' ||
          (index > 0 && character >= '0' && character <= '9')))
      return false;
  }
  return true;
}

static int32_t environment_find(const struct process *process,
                                const char *name) {
  for (uint32_t index = 0; index < PROCESS_ENVIRONMENT_COUNT; index++) {
    if (process->environment[index].used &&
        strcmp(process->environment[index].name, name) == 0)
      return (int32_t)index;
  }
  return -1;
}

static void environment_put(struct process *process, const char *name,
                            const char *value) {
  int32_t slot = environment_find(process, name);
  if (slot < 0) {
    for (uint32_t index = 0; index < PROCESS_ENVIRONMENT_COUNT; index++) {
      if (!process->environment[index].used) {
        slot = (int32_t)index;
        break;
      }
    }
  }
  if (slot < 0)
    return;
  struct process_environment_entry *entry = &process->environment[slot];
  entry->used = true;
  strncpy(entry->name, name, sizeof(entry->name) - 1);
  entry->name[sizeof(entry->name) - 1] = '\0';
  strncpy(entry->value, value, sizeof(entry->value) - 1);
  entry->value[sizeof(entry->value) - 1] = '\0';
}

static void environment_initialize(struct process *process,
                                   const struct process *parent) {
  if (parent) {
    memcpy(process->environment, parent->environment,
           sizeof(process->environment));
    return;
  }
  environment_put(process, "HOME", "/");
  environment_put(process, "PWD", "/");
  environment_put(process, "USER", "purec");
  environment_put(process, "SHELL", "/bin/program/terminal");
  environment_put(process, "PATH", "/usr/bin:/bin:/usr/games:/sbin");
}

static struct process *allocate_process(void) {
  struct process *process = process_node_alloc();
  if (!process)
    return 0;
  process->state = PROCESS_LOADING;
  process->waiter_thread_id = -1;
  process->child_waiter_tid = -1;
  process->cwd[0] = '/';
  for (uint32_t fd = 0; fd < PROCESS_FD_COUNT; fd++)
    process->descriptors[fd] = -1;
  process->descriptors[0] = VFS_FD_STDIN;
  process->descriptors[1] = VFS_FD_STDOUT;
  process->descriptors[2] = VFS_FD_STDERR;
  process->next = NULL;
  if (!process_list)
    process_list = process;
  else {
    struct process *tail = process_list;
    while (tail->next)
      tail = tail->next;
    tail->next = process;
  }
  return process;
}

struct built_image {
  uint64_t entry;
  uint64_t heap_base;
  uint64_t heap_limit;
  uint64_t stack_limit;
  uint64_t stack_low;
};

static bool build_fail(struct spawn_image *images, uint64_t image_count,
                       const char *message) {
  last_spawn_error = message;
  for (uint64_t i = 0; i < image_count; i++) {
    if (images[i].owned && images[i].phys)
      pmm_free_contiguous(images[i].phys, images[i].pages);
  }
  return false;
}

static bool build_user_image(const void *image, uint64_t image_size,
                             uint64_t address_space, uint64_t stack_pages,
                             struct built_image *out) {
  struct elf_load_result loaded;
  uint64_t heap_base = 0;
  uint16_t image_type = elf_image_type(image, image_size);
  if (image_type != 2 && image_type != 3) {
    last_spawn_error = "ELF image validation or mapping failed";
    return false;
  }
  if (image_type == 2) {
    if (!elf_load_user_image(image, image_size, address_space, &loaded)) {
      last_spawn_error = "ELF image validation or mapping failed";
      return false;
    }
    heap_base = loaded.highest_address + USER_HEAP_GUARD_PAGES * PMM_PAGE_SIZE;
  } else {
    struct spawn_image images[SPAWN_MAX_OBJECTS];
    struct elf_object scope[SPAWN_MAX_OBJECTS];
    memset(images, 0, sizeof(images));
    memset(scope, 0, sizeof(scope));
    images[0].data = image;
    images[0].size = image_size;
    images[0].owned = false;
    uint64_t image_count = 1;
    uint64_t so_index = 0;
    if (!elf_load_user_image_biased(images[0].data, images[0].size,
                                    address_space, ELF_PIE_DEFAULT_BASE,
                                    &loaded, &scope[0].dyn))
      return build_fail(images, image_count,
                        "ELF image validation or mapping failed");
    scope[0].top = loaded.highest_address;
    uint64_t top = loaded.highest_address;
    for (uint64_t i = 0; i < image_count; i++) {
      uint64_t need = elf_needed_count(images[i].data, images[i].size);
      if (need > ELF_MAX_NEEDED)
        return build_fail(images, image_count,
                          "too many shared library dependencies");
      for (uint64_t j = 0; j < need; j++) {
        char name[ELF_NAME_CAP + 1];
        char path[SPAWN_PATH_CAP];
        if (!elf_needed_name(images[i].data, images[i].size, j, name,
                             sizeof(name)))
          return build_fail(images, image_count,
                            "cannot read shared library name");
        spawn_join_lib_path(path, name);
        if (spawn_images_have(images, image_count, path))
          continue;
        if (image_count >= SPAWN_MAX_OBJECTS)
          return build_fail(images, image_count,
                            "too many shared library dependencies");
        if (!spawn_load_image(path, &images[image_count]))
          return build_fail(images, image_count,
                            "shared library not found in initramfs or VFS");
        uint64_t base = elf_dyn_base_for_index(so_index++);
        if (!base)
          return build_fail(images, image_count,
                            "no address range left for shared library");
        struct elf_load_result lib_loaded;
        if (!elf_load_user_image_biased(
                images[image_count].data, images[image_count].size,
                address_space, base, &lib_loaded, &scope[image_count].dyn))
          return build_fail(images, image_count,
                            "shared library mapping failed");
        scope[image_count].top = lib_loaded.highest_address;
        if (lib_loaded.highest_address > top)
          top = lib_loaded.highest_address;
        image_count++;
      }
    }
    for (uint64_t k = 0; k < image_count; k++) {
      if (!elf_apply_relocs_with_scope(address_space, scope, image_count, k))
        return build_fail(images, image_count,
                          "shared library relocation failed");
    }
    heap_base = top + USER_HEAP_GUARD_PAGES * PMM_PAGE_SIZE;
    for (uint64_t i = 1; i < image_count; i++) {
      if (images[i].owned && images[i].phys) {
        pmm_free_contiguous(images[i].phys, images[i].pages);
        images[i].owned = false;
        images[i].phys = 0;
      }
    }
  }
  uint64_t stack_base = USER_STACK_TOP - stack_pages * PMM_PAGE_SIZE;
  uint64_t stack_limit = USER_STACK_TOP - USER_STACK_MAX_PAGES * PMM_PAGE_SIZE;
  uint64_t heap_limit = stack_limit - USER_HEAP_GUARD_PAGES * PMM_PAGE_SIZE;
  if (heap_base >= heap_limit) {
    last_spawn_error = "ELF address range collides with user stack";
    return false;
  }
  if (!vmm_map_new_pages(address_space, stack_base, stack_pages,
                         VMM_PAGE_USER | VMM_PAGE_WRITABLE | VMM_PAGE_NX)) {
    last_spawn_error = "cannot allocate user stack pages";
    return false;
  }
  out->entry = loaded.entry;
  out->heap_base = heap_base;
  out->heap_limit = heap_limit;
  out->stack_limit = stack_limit;
  out->stack_low = stack_base;
  return true;
}

static void assign_pid(struct process *process) {
  process->pid = next_pid++;
  if (process->pid == 0)
    process->pid = next_pid++;
  for (;;) {
    struct process *hit = process_find_by_pid(process->pid);
    if (!hit || hit == process)
      break;
    process->pid = next_pid++;
    if (process->pid == 0)
      process->pid = next_pid++;
  }
}

extern void arch_enter_user_frame(const uint64_t *frame);

static void fork_child_entry(void *argument) {
  struct process *process = (struct process *)argument;
  mutex_lock(&process_mutex);
  process->state = PROCESS_RUNNING;
  mutex_unlock(&process_mutex);
  scheduler_leave_kernel();
  arch_enter_user_frame(process->fork_frame);
}

static void user_process_entry(void *argument) {
  struct process *process = (struct process *)argument;
  mutex_lock(&process_mutex);
  process->state = PROCESS_RUNNING;
  mutex_unlock(&process_mutex);
  scheduler_leave_kernel();
  arch_enter_user(process->entry, process->user_stack_top);
}

static uint32_t oom_reap_zombie(uint32_t pid);
void process_init(void) {
  process_list = NULL;
  next_pid = 1;
  for (uint32_t i = 0; i < PHASH_SIZE; i++)
    phash_buckets[i] = NULL;
  oom_victim_set_killer(oom_reap_zombie);
  klog(KLOG_OK, "process: dynamic table ready (limit=RAM)");
}

static uint32_t oom_reap_zombie(uint32_t pid) {
  struct process *target = process_find_by_pid(pid);
  if (!target)
    return 0;
  if (target->pid == 1)
    return 0;
  if (target->state != PROCESS_EXITED)
    return 0;
  if (!scheduler_thread_stopped(target->thread_id))
    return 0;
  int exiting_tid = target->thread_id;
  vmm_destroy_address_space(target->address_space);
  target->address_space = 0;
  scheduler_free_thread_by_id(exiting_tid);
  process_node_free(target);
  return pid;
}

static bool session_active;
static uint32_t session_uid;
static uint32_t session_gid;

int32_t process_spawn_elf(const void *image, uint64_t image_size,
                          const char *name, const char *command_line) {
  MUTEX_SCOPE(&process_mutex);
  struct process *parent = process_current();
  if (!oom_gate_can_spawn()) {
    last_spawn_error = "out of memory";
    klog(KLOG_WARN, "oom: spawn denied, kernel reserve kept");
    return -1;
  }
  struct process *process = allocate_process();
  if (!process) {
    last_spawn_error = "cannot allocate process descriptor";
    return -1;
  }
  process->address_space = vmm_create_address_space();
  if (!process->address_space) {
    last_spawn_error = "cannot create process address space";
    process_node_free(process);
    return -1;
  }
  struct built_image built;
  if (!build_user_image(image, image_size, process->address_space,
                        USER_STACK_PAGES, &built)) {
    vmm_destroy_address_space(process->address_space);
    process->address_space = 0;
    process_node_free(process);
    return -1;
  }
  struct elf_load_result loaded = {.entry = built.entry};
  uint64_t heap_base = built.heap_base;
  uint64_t heap_limit = built.heap_limit;
  assign_pid(process);
  phash_insert(process);
  {
    uint64_t initial_user_pages = vmm_user_page_count(process->address_space);
    if (!oom_gate_try_charge_user(process->pid, initial_user_pages)) {
      last_spawn_error = "out of memory";
      klogf(KLOG_WARN, "oom: spawn pid charge denied pages=%llu",
            (unsigned long long)initial_user_pages);
      vmm_destroy_address_space(process->address_space);
      process->address_space = 0;
      process_node_free(process);
      return -1;
    }
  }
  process->parent_pid =
      (uint32_t)(process_current_pid() > 0 ? process_current_pid() : 0);
  process->state = PROCESS_READY;
  process->entry = loaded.entry;
  process->user_stack_top = USER_STACK_TOP - 24;
  process->heap_base = heap_base;
  process->heap_break = heap_base;
  process->heap_mapped_end = heap_base;
  process->heap_limit = heap_limit;
  process->stack_limit = built.stack_limit;
  process->stack_low = built.stack_low;
  environment_initialize(process, parent);
  if (parent)
    memcpy(process->cwd, parent->cwd, sizeof(process->cwd));
  process->sig_blocked = 0;
  process->uid = parent ? parent->uid : 0;
  process->gid = parent ? parent->gid : 0;
  if (session_active && parent && parent->uid == 0 && parent->pid != 1) {
    process->uid = session_uid;
    process->gid = session_gid;
  }
  if (command_line) {
    strncpy(process->command_line, command_line,
            sizeof(process->command_line) - 1);
    process->command_line[sizeof(process->command_line) - 1] = '\0';
  }
  strncpy(process->name, name ? name : "process", sizeof(process->name) - 1);
  if (name && (strcmp(name, "installer") == 0 || strcmp(name, "disks") == 0))
    process->capabilities |= PROCESS_CAP_STORAGE_ADMIN;
  if (name && (strcmp(name, "netd") == 0 || strcmp(name, "net-rx") == 0 ||
               strcmp(name, "dhcp") == 0 || strcmp(name, "dns") == 0))
    process->capabilities |= PROCESS_CAP_NET_ADMIN;
  process->thread_id = scheduler_create_user_thread(
      user_process_entry, process, process->name, USER_PROCESS_PRIORITY, -1,
      process->address_space, process);
  if (process->thread_id < 0) {
    last_spawn_error = "cannot create initial process thread";
    vmm_destroy_address_space(process->address_space);
    process->address_space = 0;
    process_node_free(process);
    return -1;
  }
  klogf(KLOG_OK, "process: pid=%u name=%s entry=0x%llx cr3=0x%llx",
        process->pid, process->name, process->entry, process->address_space);
  return (int32_t)process->pid;
}

int32_t process_fork(const struct syscall_regs *regs) {
  MUTEX_SCOPE(&process_mutex);
  struct process *parent = process_current();
  if (!parent || !process_current_is_user() || !regs)
    return -1;
  if (!oom_gate_can_spawn()) {
    klog(KLOG_WARN, "oom: fork denied, kernel reserve kept");
    return -1;
  }
  struct process *child = allocate_process();
  if (!child)
    return -1;
  child->address_space = vmm_clone_address_space(parent->address_space);
  if (!child->address_space) {
    process_node_free(child);
    return -1;
  }
  assign_pid(child);
  phash_insert(child);
  uint64_t pages = vmm_user_page_count(child->address_space);
  if (!oom_gate_try_charge_user(child->pid, pages)) {
    klogf(KLOG_WARN, "oom: fork pid charge denied pages=%llu",
          (unsigned long long)pages);
    vmm_destroy_address_space(child->address_space);
    child->address_space = 0;
    process_node_free(child);
    return -1;
  }
  child->parent_pid = parent->pid;
  child->entry = parent->entry;
  child->user_stack_top = parent->user_stack_top;
  child->heap_base = parent->heap_base;
  child->heap_break = parent->heap_break;
  child->heap_mapped_end = parent->heap_mapped_end;
  child->heap_limit = parent->heap_limit;
  child->capabilities = parent->capabilities;
  child->uid = parent->uid;
  child->gid = parent->gid;
  memcpy(child->environment, parent->environment, sizeof(child->environment));
  memcpy(child->command_line, parent->command_line,
         sizeof(child->command_line));
  memcpy(child->name, parent->name, sizeof(child->name));
  memcpy(child->cwd, parent->cwd, sizeof(child->cwd));
  child->stack_limit = parent->stack_limit;
  child->stack_low = parent->stack_low;
  if (!signal_clone(child, parent)) {
    vmm_destroy_address_space(child->address_space);
    child->address_space = 0;
    process_node_free(child);
    return -1;
  }
  fd_clone(child, parent);
  memcpy(child->fork_frame, regs, sizeof(child->fork_frame));
  child->fork_frame[14] = 0;
  child->state = PROCESS_READY;
  child->thread_id = scheduler_create_forked_thread(
      fork_child_entry, child, child->name, USER_PROCESS_PRIORITY, -1,
      child->address_space, child);
  if (child->thread_id < 0) {
    fd_close_all(child);
    vmm_destroy_address_space(child->address_space);
    child->address_space = 0;
    process_node_free(child);
    return -1;
  }
  klogf(KLOG_OK, "process: fork pid=%u ppid=%u name=%s", child->pid,
        child->parent_pid, child->name);
  return (int32_t)child->pid;
}

int32_t process_spawn_module(const char *path, const char *command_line) {
  MUTEX_SCOPE(&process_mutex);
  last_spawn_error = "unknown process load failure";
  const char *module_path = path;
  if (!path) {
    last_spawn_error = "process path is null";
    klog(KLOG_ERROR, "process: spawn requested with no path");
    return -1;
  }

  const char *name = path;
  for (const char *cursor = path; *cursor; cursor++) {
    if (*cursor == '/' && cursor[1])
      name = cursor + 1;
  }

  /* Initramfs files already reside in kernel-readable memory. Loading the
     ELF directly avoids allocating a VFS handle and a second contiguous
     copy of the executable during the memory-constrained boot phase. */
  const void *initramfs_image = 0;
  uint32_t initramfs_size = 0;
  char canonical[512];
  const char *resolved = canonical_path(path, canonical, sizeof(canonical));
  if (initramfs_find(resolved, &initramfs_image, &initramfs_size))
    return process_spawn_elf(initramfs_image, initramfs_size, name,
                             command_line);
  if (program_alias_resolve(path, &module_path) &&
      initramfs_find(module_path, &initramfs_image, &initramfs_size))
    return process_spawn_elf(initramfs_image, initramfs_size, name,
                             command_line);

  int32_t fd = vfs_open(path);
  const char *used_path = (fd >= 0) ? path : 0;
  if (fd < 0 && module_path && module_path != path) {
    fd = vfs_open(module_path);
    if (fd >= 0)
      used_path = module_path;
  }
  if (fd < 0) {
    last_spawn_error = fd == FS_ERROR_NO_SPACE
                           ? "VFS cannot allocate a file handle"
                           : "executable not found in initramfs or VFS";
    klogf(KLOG_ERROR, "process: cannot open %s (vfs=%d, root=%s)", path, fd,
          vfs_root_device_name());
    return -1;
  }
  {
    struct file_stat_info st = {0};
    uint64_t blob_size = 0;
    if (used_path && vfs_stat(used_path, &st) >= 0)
      blob_size = st.size;
    if (!blob_size || blob_size > 64ULL * 1024ULL * 1024ULL) {
      last_spawn_error = "invalid executable size in initramfs";
      klogf(KLOG_ERROR, "process: invalid size for %s: %llu", used_path,
            (unsigned long long)blob_size);
      vfs_close(fd);
      return -1;
    }
    uint64_t pages = (blob_size + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    uint64_t phys = pmm_allocate_contiguous(pages);
    if (phys) {
      uint8_t *vbuf = (uint8_t *)pmm_physical_to_virtual(phys);
      uint64_t done = 0;
      int32_t failed = 0;
      while (done < blob_size) {
        uint64_t left = blob_size - done;
        uint32_t want = left > 1048576 ? 1048576 : (uint32_t)left;
        int32_t got = vfs_read(fd, vbuf + done, want);
        if (got <= 0) {
          last_spawn_error = "short read from initramfs";
          klogf(KLOG_ERROR, "process: read %s failed at %llu/%llu (vfs=%d)",
                used_path, (unsigned long long)done,
                (unsigned long long)blob_size, got);
          failed = 1;
          break;
        }
        done += (uint64_t)got;
      }
      vfs_close(fd);
      if (!failed) {
        int32_t pid = process_spawn_elf(vbuf, done, name, command_line);
        pmm_free_contiguous(phys, pages);
        if (pid < 0)
          klogf(KLOG_ERROR, "process: ELF load failed for %s (%llu bytes)",
                used_path, (unsigned long long)done);
        return pid;
      }
      pmm_free_contiguous(phys, pages);
      return -1;
    }
    klogf(KLOG_ERROR, "process: cannot allocate %llu pages for %s",
          (unsigned long long)pages, used_path);
    last_spawn_error = "cannot allocate executable read buffer";
    vfs_close(fd);
  }
  return -1;
}

int32_t process_spawn_service(void (*entry)(void *arg), void *arg,
                              const char *name, uint8_t priority,
                              int16_t affinity, uint8_t qos,
                              uint32_t capabilities) {
  if (!entry || !name)
    return -1;
  MUTEX_SCOPE(&process_mutex);
  if (!oom_gate_can_spawn()) {
    last_spawn_error = "out of memory";
    return -1;
  }
  struct process *process = allocate_process();
  if (!process) {
    last_spawn_error = "cannot allocate process descriptor";
    return -1;
  }
  process->pid = next_pid++;
  if (process->pid == 0)
    process->pid = next_pid++;
  for (;;) {
    struct process *hit = process_find_by_pid(process->pid);
    if (!hit || hit == process)
      break;
    process->pid = next_pid++;
    if (process->pid == 0)
      process->pid = next_pid++;
  }
  phash_insert(process);
  struct process *init = process_find_by_pid(1);
  process->parent_pid = init ? 1 : 0;
  process->state = PROCESS_RUNNING;
  process->entry = (uint64_t)(uintptr_t)entry;
  process->address_space = vmm_kernel_address_space();
  process->capabilities = capabilities;
  process->uid = 0;
  process->gid = 0;
  strncpy(process->name, name, sizeof(process->name) - 1);
  process->name[sizeof(process->name) - 1] = '\0';
  environment_initialize(process, init);
  int32_t tid = scheduler_create_service_thread(
      entry, arg, process->name, priority, affinity, process->address_space,
      process);
  if (tid < 0) {
    last_spawn_error = "cannot create service thread";
    process_node_free(process);
    return -1;
  }
  process->thread_id = tid;
  scheduler_set_qos(tid, qos);
  klogf(KLOG_OK, "process: pid=%u ppid=%u service=%s entry=0x%llx",
        process->pid, process->parent_pid, process->name, process->entry);
  return (int32_t)process->pid;
}

int32_t process_wait(uint32_t pid, int32_t *status, bool nohang) {
  if (!pid)
    return -1;
  for (;;) {
    bool exiting = false;
    {
      MUTEX_SCOPE(&process_mutex);
      struct process *target = process_find_by_pid(pid);
      if (!target)
        return -1;
      int32_t caller = process_current_pid();
      if (caller > 0 && target->parent_pid != (uint32_t)caller)
        return -1;
      if (target->state == PROCESS_EXITED &&
          scheduler_thread_stopped(target->thread_id)) {
        if (status)
          *status = target->exit_code;
        int exiting_tid = target->thread_id;
        vmm_destroy_address_space(target->address_space);
        target->address_space = 0;
        scheduler_free_thread_by_id(exiting_tid);
        process_node_free(target);
        return (int32_t)pid;
      }
      if (nohang)
        return 0;
      int32_t waiter = scheduler_current_tid();
      if (waiter < 0 ||
          (target->waiter_thread_id >= 0 && target->waiter_thread_id != waiter))
        return -1;
      target->waiter_thread_id = waiter;
      exiting = target->state == PROCESS_EXITED;
    }
    if (exiting)
      scheduler_sleep(1);
    else
      scheduler_block();
  }
}

struct process *process_current(void) {
  struct thread *thread = scheduler_current_thread();
  return thread ? thread->process : 0;
}

int32_t process_current_pid(void) {
  struct process *process = process_current();
  return process ? (int32_t)process->pid : 0;
}

int32_t process_parent_pid(uint32_t pid) {
  MUTEX_SCOPE(&process_mutex);
  struct process *process = process_find_by_pid(pid);
  if (!process)
    return -1;
  return (int32_t)process->parent_pid;
}

bool process_current_is_user(void) {
  struct thread *thread = scheduler_current_thread();
  return thread && thread->user_mode;
}

bool process_has_capability(uint32_t capability) {
  struct process *process = process_current();
  return !process || (process->capabilities & capability) == capability;
}

uint64_t process_current_address_space(void) {
  struct process *process = process_current();
  return process ? process->address_space : vmm_kernel_address_space();
}

uint64_t process_heap_grow(uint64_t size) {
  MUTEX_SCOPE(&process_mutex);
  struct process *process = process_current();
  if (!process || !process_current_is_user())
    return 0;
  if (!size)
    return process->heap_break;
  if (size > process->heap_limit - process->heap_break)
    return 0;
  uint64_t previous_break = process->heap_break;
  uint64_t requested_break = previous_break + size;
  uint64_t requested_mapping =
      (requested_break + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
  uint64_t need_pages =
      (requested_mapping > process->heap_mapped_end)
          ? (requested_mapping - process->heap_mapped_end + PMM_PAGE_SIZE - 1) /
                PMM_PAGE_SIZE
          : 0;
  if (need_pages && !oom_gate_try_charge_user(process->pid, need_pages)) {
    klogf(KLOG_WARN, "oom: heap grow denied pid=%u pages=%llu", process->pid,
          (unsigned long long)need_pages);
    return 0;
  }
  uint64_t mapped_pages = 0;
  while (process->heap_mapped_end < requested_mapping) {
    if (!vmm_map_new_pages(process->address_space, process->heap_mapped_end, 1,
                           VMM_PAGE_USER | VMM_PAGE_WRITABLE | VMM_PAGE_NX)) {
      if (mapped_pages < need_pages)
        oom_gate_release_user(process->pid, need_pages - mapped_pages);
      klogf(KLOG_WARN, "oom: heap map denied pid=%u", process->pid);
      return 0;
    }
    process->heap_mapped_end += PMM_PAGE_SIZE;
    mapped_pages++;
  }
  process->heap_break = requested_break;
  return previous_break;
}

static void process_terminate(int32_t status, int32_t signal)
    __attribute__((noreturn));

static void process_terminate(int32_t status, int32_t signal) {
  mutex_lock(&process_mutex);
  struct process *process = process_current();
  int32_t parent_waiter = -1;
  if (process) {
    if (process->pid == 1)
      kernel_panic("PID 1 exited");
    process->exit_code = status;
    process->term_signal = signal;
    process->runtime_ticks = scheduler_thread_runtime_ticks(process->thread_id);
    process->state = PROCESS_EXITED;
    for (struct process *child = process_list; child; child = child->next) {
      if (child->state != PROCESS_FREE && child->parent_pid == process->pid) {
        child->parent_pid = 1;
        child->waiter_thread_id = -1;
      }
    }
    window_manager_unregister(process->pid);
    gop_console_release(process->pid);
    tcp_release_owner(process->pid);
    fd_close_all(process);
    klogf(KLOG_INFO, "process: pid=%u exited status=%d signal=%d", process->pid,
          status, signal);
    struct process *parent = process_find_by_pid(process->parent_pid);
    if (parent) {
      signal_notify_child_exit(parent);
      parent_waiter = parent->thread_id;
    }
    if (process->waiter_thread_id >= 0)
      scheduler_unblock(process->waiter_thread_id);
  }
  mutex_unlock(&process_mutex);
  if (parent_waiter >= 0)
    scheduler_unblock(parent_waiter);
  scheduler_exit();
  __builtin_unreachable();
}

void process_exit_current(int32_t status) { process_terminate(status, 0); }

void process_exit_signal(int32_t signal) {
  process_terminate(128 + signal, signal);
}

static void process_reap(struct process *target) {
  int tid = target->thread_id;
  vmm_destroy_address_space(target->address_space);
  target->address_space = 0;
  scheduler_free_thread_by_id(tid);
  process_node_free(target);
}

int32_t process_waitpid(int32_t pid, int32_t *status, uint32_t options) {
  struct process *caller = process_current();
  if (!caller)
    return -KERR_CHILD;
  for (;;) {
    bool have_child = false;
    bool exiting = false;
    mutex_lock(&process_mutex);
    struct process *zombie = NULL;
    for (struct process *p = process_list; p; p = p->next) {
      if (p->state == PROCESS_FREE || p->parent_pid != caller->pid)
        continue;
      if (pid > 0 && p->pid != (uint32_t)pid)
        continue;
      have_child = true;
      if (p->state == PROCESS_EXITED) {
        if (scheduler_thread_stopped(p->thread_id)) {
          zombie = p;
          break;
        }
        exiting = true;
      }
    }
    if (zombie) {
      uint32_t reaped = zombie->pid;
      if (status)
        *status = zombie->term_signal ? zombie->term_signal
                                      : ((zombie->exit_code & 0xFF) << 8);
      process_reap(zombie);
      mutex_unlock(&process_mutex);
      return (int32_t)reaped;
    }
    if (!have_child) {
      mutex_unlock(&process_mutex);
      return -KERR_CHILD;
    }
    if (options & 1U) {
      mutex_unlock(&process_mutex);
      return 0;
    }
    if (signal_pending_current()) {
      mutex_unlock(&process_mutex);
      return -KERR_INTR;
    }
    caller->child_waiter_tid = scheduler_current_tid();
    mutex_unlock(&process_mutex);
    if (exiting)
      scheduler_sleep(1);
    else
      scheduler_block();
  }
}

int32_t process_resolve_path(const char *path, char *out, uint32_t capacity) {
  struct process *process = process_current();
  char joined[VFS_PATH_MAX];
  if (!path || !path[0])
    return -KERR_NOENT;
  if (path[0] == '/' || !process) {
    if (strlen(path) >= sizeof(joined))
      return -KERR_NAMETOOLONG;
    strncpy(joined, path, sizeof(joined) - 1);
    joined[sizeof(joined) - 1] = '\0';
  } else {
    size_t base = strlen(process->cwd);
    size_t extra = strlen(path);
    if (base + extra + 2 > sizeof(joined))
      return -KERR_NAMETOOLONG;
    memcpy(joined, process->cwd, base);
    if (base && joined[base - 1] != '/')
      joined[base++] = '/';
    memcpy(joined + base, path, extra + 1);
  }
  return vfs_path_normalize(joined, out, capacity) < 0 ? -KERR_INVAL : 0;
}

int32_t process_chdir(const char *path) {
  MUTEX_SCOPE(&process_mutex);
  struct process *process = process_current();
  if (!process)
    return -KERR_INVAL;
  char resolved[VFS_PATH_MAX];
  int32_t status = process_resolve_path(path, resolved, sizeof(resolved));
  if (status < 0)
    return status;
  struct file_stat_info info = {0};
  if (vfs_stat(resolved, &info) < 0)
    return -KERR_NOENT;
  if (!info.is_directory)
    return -KERR_NOTDIR;
  strncpy(process->cwd, resolved, sizeof(process->cwd) - 1);
  process->cwd[sizeof(process->cwd) - 1] = '\0';
  environment_put(process, "PWD", process->cwd);
  return 0;
}

int32_t process_getcwd(char *buffer, uint32_t capacity) {
  struct process *process = process_current();
  if (!process || !buffer || !capacity)
    return -KERR_INVAL;
  uint32_t length = (uint32_t)strlen(process->cwd);
  if (length + 1 > capacity)
    return -KERR_INVAL;
  memcpy(buffer, process->cwd, length + 1);
  return (int32_t)length;
}

bool process_handle_stack_fault(uint64_t address) {
  struct process *process = process_current();
  if (!process || !process_current_is_user() || !process->stack_limit)
    return false;
  if (address >= USER_STACK_TOP || address < process->stack_limit)
    return false;
  uint64_t page = address & ~(PMM_PAGE_SIZE - 1);
  if (page >= process->stack_low)
    return false;
  uint64_t pages = (process->stack_low - page) / PMM_PAGE_SIZE;
  if (!oom_gate_try_charge_user(process->pid, pages))
    return false;
  if (!vmm_map_new_pages_lazy(process->address_space, page, pages,
                              VMM_PAGE_USER | VMM_PAGE_WRITABLE |
                                  VMM_PAGE_NX)) {
    oom_gate_release_user(process->pid, pages);
    return false;
  }
  process->stack_low = page;
  return true;
}

#define EXEC_ARG_MAX (128U * 1024U)
#define EXEC_ARG_PAGES (EXEC_ARG_MAX / 4096U)
#define EXEC_VECTOR_MAX 2048U

struct exec_args {
  char *data;
  uint64_t phys;
  uint64_t size;
  uint32_t argc;
  uint32_t envc;
};

static int64_t user_string_length(const char *text, uint64_t limit) {
  uint64_t length = 0;
  while (length < limit) {
    uint64_t address = (uint64_t)(uintptr_t)text + length;
    uint64_t chunk = PMM_PAGE_SIZE - (address & (PMM_PAGE_SIZE - 1));
    if (chunk > limit - length)
      chunk = limit - length;
    if (!process_user_buffer((const void *)(uintptr_t)address, chunk, false))
      return -1;
    const char *cursor = (const char *)(uintptr_t)address;
    for (uint64_t i = 0; i < chunk; i++) {
      if (!cursor[i])
        return (int64_t)(length + i);
    }
    length += chunk;
  }
  return -2;
}

static int32_t exec_copy_vector(const char *const *vector, struct exec_args *args,
                                uint32_t *count) {
  *count = 0;
  if (!vector)
    return 0;
  for (uint32_t i = 0; i < EXEC_VECTOR_MAX; i++) {
    if (!process_user_buffer(&vector[i], sizeof(vector[i]), false))
      return -KERR_FAULT;
    const char *text = vector[i];
    if (!text)
      return 0;
    int64_t length = user_string_length(text, EXEC_ARG_MAX - args->size);
    if (length == -1)
      return -KERR_FAULT;
    if (length < 0 || args->size + (uint64_t)length + 1 > EXEC_ARG_MAX)
      return -KERR_2BIG;
    memcpy(args->data + args->size, text, (size_t)length + 1);
    args->size += (uint64_t)length + 1;
    (*count)++;
  }
  return -KERR_2BIG;
}

static bool write_user_bytes(uint64_t address_space, uint64_t address,
                             const void *source, uint64_t size) {
  const uint8_t *bytes = (const uint8_t *)source;
  while (size) {
    uint64_t physical = vmm_translate(address_space, address);
    if (!physical)
      return false;
    uint64_t chunk = PMM_PAGE_SIZE - (address & (PMM_PAGE_SIZE - 1));
    if (chunk > size)
      chunk = size;
    memcpy(pmm_physical_to_virtual(physical), bytes, (size_t)chunk);
    address += chunk;
    bytes += chunk;
    size -= chunk;
  }
  return true;
}

static void exec_environment_apply(struct process *process,
                                   const struct exec_args *args) {
  const char *cursor = args->data;
  for (uint32_t i = 0; i < args->argc; i++)
    cursor += strlen(cursor) + 1;
  memset(process->environment, 0, sizeof(process->environment));
  for (uint32_t i = 0; i < args->envc; i++) {
    const char *entry = cursor;
    cursor += strlen(cursor) + 1;
    const char *equals = entry;
    while (*equals && *equals != '=')
      equals++;
    if (!*equals)
      equals = 0;
    if (!equals)
      continue;
    char name[PROCESS_ENVIRONMENT_NAME_CAPACITY];
    size_t name_length = (size_t)(equals - entry);
    if (name_length == 0 || name_length >= sizeof(name))
      continue;
    memcpy(name, entry, name_length);
    name[name_length] = '\0';
    if (!environment_name_valid(name))
      continue;
    if (environment_find(process, name) < 0) {
      bool room = false;
      for (uint32_t slot = 0; slot < PROCESS_ENVIRONMENT_COUNT; slot++)
        if (!process->environment[slot].used)
          room = true;
      if (!room)
        continue;
    }
    environment_put(process, name, equals + 1);
  }
}

static void exec_command_line_apply(struct process *process,
                                    const struct exec_args *args) {
  const char *cursor = args->data;
  size_t used = 0;
  process->command_line[0] = '\0';
  for (uint32_t i = 0; i < args->argc; i++) {
    size_t length = strlen(cursor);
    if (i > 0) {
      if (used + length + 2 > sizeof(process->command_line))
        break;
      if (used)
        process->command_line[used++] = ' ';
      memcpy(process->command_line + used, cursor, length + 1);
      used += length;
    }
    cursor += length + 1;
  }
}

int32_t process_execve(struct syscall_regs *regs, const char *path,
                       const char *const *argv, const char *const *envp) {
  MUTEX_SCOPE(&process_mutex);
  struct process *process = process_current();
  if (!process || !process_current_is_user() || !regs)
    return -KERR_INVAL;
  char resolved[VFS_PATH_MAX];
  int32_t status = process_resolve_path(path, resolved, sizeof(resolved));
  if (status < 0)
    return status;

  struct exec_args args = {0};
  args.phys = pmm_allocate_contiguous(EXEC_ARG_PAGES);
  if (!args.phys)
    return -KERR_NOMEM;
  args.data = (char *)pmm_physical_to_virtual(args.phys);
  if (argv) {
    status = exec_copy_vector(argv, &args, &args.argc);
  } else {
    size_t length = strlen(path);
    memcpy(args.data, path, length + 1);
    args.size = length + 1;
    args.argc = 1;
  }
  if (status >= 0)
    status = exec_copy_vector(envp, &args, &args.envc);
  if (status < 0) {
    pmm_free_contiguous(args.phys, EXEC_ARG_PAGES);
    return status;
  }

  struct spawn_image image;
  const char *loaded_path = resolved;
  const char *alias = 0;
  bool loaded = spawn_load_image(resolved, &image);
  if (!loaded && program_alias_resolve(resolved, &alias) && alias)
    loaded = spawn_load_image(alias, &image);
  if (!loaded) {
    pmm_free_contiguous(args.phys, EXEC_ARG_PAGES);
    return -KERR_NOENT;
  }
  (void)loaded_path;

  uint64_t pointer_bytes = 8ULL * (1ULL + args.argc + 1ULL + args.envc + 1ULL);
  uint64_t strings_start = USER_STACK_TOP - args.size;
  uint64_t sp = (strings_start - pointer_bytes) & ~15ULL;
  uint64_t stack_pages =
      (USER_STACK_TOP - (sp - 8) + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE +
      USER_STACK_PAGES;
  if (stack_pages > USER_STACK_MAX_PAGES / 2) {
    if (image.owned && image.phys)
      pmm_free_contiguous(image.phys, image.pages);
    pmm_free_contiguous(args.phys, EXEC_ARG_PAGES);
    return -KERR_2BIG;
  }

  uint64_t new_space = vmm_create_address_space();
  struct built_image built;
  bool built_ok = new_space && build_user_image(image.data, image.size,
                                                new_space, stack_pages, &built);
  if (image.owned && image.phys)
    pmm_free_contiguous(image.phys, image.pages);
  if (!built_ok) {
    if (new_space)
      vmm_destroy_address_space(new_space);
    pmm_free_contiguous(args.phys, EXEC_ARG_PAGES);
    return new_space ? -KERR_NOEXEC : -KERR_NOMEM;
  }

  uint64_t table_size = pointer_bytes;
  uint64_t table_pages = (table_size + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
  uint64_t table_phys = pmm_allocate_contiguous(table_pages);
  bool layout_ok = table_phys != 0;
  if (layout_ok) {
    uint64_t *table = (uint64_t *)pmm_physical_to_virtual(table_phys);
    uint64_t index = 0;
    uint64_t offset = 0;
    table[index++] = args.argc;
    for (uint32_t i = 0; i < args.argc; i++) {
      table[index++] = strings_start + offset;
      offset += strlen(args.data + offset) + 1;
    }
    table[index++] = 0;
    for (uint32_t i = 0; i < args.envc; i++) {
      table[index++] = strings_start + offset;
      offset += strlen(args.data + offset) + 1;
    }
    table[index++] = 0;
    layout_ok = write_user_bytes(new_space, strings_start, args.data,
                                 args.size) &&
                write_user_bytes(new_space, sp, table, table_size);
    pmm_free_contiguous(table_phys, table_pages);
  }
  uint64_t new_pages = vmm_user_page_count(new_space);
  if (!layout_ok || !oom_gate_try_charge_user(process->pid, new_pages)) {
    vmm_destroy_address_space(new_space);
    pmm_free_contiguous(args.phys, EXEC_ARG_PAGES);
    return layout_ok ? -KERR_NOMEM : -KERR_2BIG;
  }

  uint64_t old_space = process->address_space;
  uint64_t old_pages = vmm_user_page_count(old_space);
  fd_close_on_exec(process);
  signal_exec_reset(process);
  window_manager_unregister(process->pid);
  struct thread *thread = scheduler_current_thread();
  process->address_space = new_space;
  thread->address_space = new_space;
  vmm_switch_address_space(new_space);
  vmm_destroy_address_space(old_space);
  oom_gate_release_user(process->pid, old_pages);

  const char *base = path;
  for (const char *cursor = path; *cursor; cursor++)
    if (*cursor == '/' && cursor[1])
      base = cursor + 1;
  memset(process->name, 0, sizeof(process->name));
  strncpy(process->name, base, sizeof(process->name) - 1);
  exec_command_line_apply(process, &args);
  exec_environment_apply(process, &args);
  process->entry = built.entry;
  process->user_stack_top = sp - 8;
  process->heap_base = built.heap_base;
  process->heap_break = built.heap_base;
  process->heap_mapped_end = built.heap_base;
  process->heap_limit = built.heap_limit;
  process->stack_limit = built.stack_limit;
  process->stack_low = built.stack_low;
  if (strcmp(process->name, "installer") == 0 ||
      strcmp(process->name, "disks") == 0)
    process->capabilities |= PROCESS_CAP_STORAGE_ADMIN;
  if (strcmp(process->name, "netd") == 0 ||
      strcmp(process->name, "net-rx") == 0 ||
      strcmp(process->name, "dhcp") == 0 || strcmp(process->name, "dns") == 0)
    process->capabilities |= PROCESS_CAP_NET_ADMIN;
  pmm_free_contiguous(args.phys, EXEC_ARG_PAGES);

  uint8_t fpu_area[FPU_STATE_SIZE] __attribute__((aligned(16)));
  fpu_thread_init(fpu_area);
  fpu_restore(fpu_area);
  memset(regs, 0, sizeof(uint64_t) * 15);
  regs->rip = built.entry;
  regs->rsp = sp - 8;
  regs->rflags = 0x202;
  klogf(KLOG_OK, "process: pid=%u exec %s entry=0x%llx", process->pid,
        process->name, built.entry);
  return 0;
}

int32_t process_monitor_list_page(struct process_monitor_info *entries,
                                  uint32_t capacity, uint32_t offset) {
  MUTEX_SCOPE(&process_mutex);
  uint64_t now = timer_ticks();
  uint64_t elapsed = now - process_sample_tick;
  uint32_t count = 0;
  for (struct process *process = process_list; process;
       process = process->next) {
    if (process->state == PROCESS_FREE)
      continue;
    uint64_t runtime = process->state == PROCESS_EXITED
                           ? process->runtime_ticks
                           : scheduler_thread_runtime_ticks(process->thread_id);
    if (count >= offset && count - offset < capacity) {
      struct process_monitor_info *entry = &entries[count - offset];
      memset(entry, 0, sizeof(*entry));
      entry->pid = process->pid;
      entry->parent_pid = process->parent_pid;
      entry->state = (uint32_t)process->state;
      entry->exit_code = process->exit_code;
      entry->runtime_ms = runtime;
      entry->resident_bytes =
          vmm_user_page_count(process->address_space) * PMM_PAGE_SIZE;
      uint64_t delta = runtime - process->sampled_runtime_ticks;
      entry->cpu_percent = elapsed ? (uint32_t)((delta * 100) / elapsed) : 0;
      if (entry->cpu_percent > 100)
        entry->cpu_percent = 100;
      strncpy(entry->name, process->name, sizeof(entry->name) - 1);
    }
    process->sampled_runtime_ticks = runtime;
    count++;
  }
  process_sample_tick = now;
  return (int32_t)count;
}

int32_t process_monitor_list(struct process_monitor_info *entries,
                             uint32_t capacity) {
  return process_monitor_list_page(entries, capacity, 0);
}

int32_t process_fd_install(int32_t kernel_descriptor) {
  MUTEX_SCOPE(&process_mutex);
  struct process *process = process_current();
  if (!process)
    return kernel_descriptor;
  for (int32_t fd = 3; fd < PROCESS_FD_COUNT; fd++) {
    if (process->descriptors[fd] < 0) {
      process->descriptors[fd] = kernel_descriptor;
      return fd;
    }
  }
  return -1;
}

int32_t process_fd_resolve(int32_t descriptor) {
  MUTEX_SCOPE(&process_mutex);
  struct process *process = process_current();
  if (!process)
    return descriptor;
  if (descriptor < 0 || descriptor >= PROCESS_FD_COUNT)
    return -1;
  return process->descriptors[descriptor];
}

int32_t process_fd_close(int32_t descriptor) {
  struct process *process = process_current();
  if (!process)
    return vfs_close(descriptor);
  return fd_close(descriptor) < 0 ? -1 : 0;
}

bool process_user_buffer(const void *buffer, uint64_t size, bool writable) {
  if (size == 0)
    return true;
  if (!process_current_is_user())
    return buffer != NULL;
  return buffer &&
         vmm_user_range_accessible(process_current_address_space(),
                                   (uint64_t)(uintptr_t)buffer, size, writable);
}

bool process_user_string(const char *text, uint64_t capacity) {
  if (!process_current_is_user())
    return text != 0;
  if (!text || !capacity)
    return false;
  for (uint64_t index = 0; index < capacity; index++) {
    if (!process_user_buffer(text + index, 1, false))
      return false;
    if (text[index] == '\0')
      return true;
  }
  return false;
}

int32_t process_command_line(char *buffer, uint32_t capacity) {
  struct process *process = process_current();
  if (!process || !buffer || !capacity)
    return -1;
  uint32_t length = (uint32_t)strlen(process->command_line);
  if (length + 1 > capacity)
    return -1;
  memcpy(buffer, process->command_line, length + 1);
  return (int32_t)length;
}

int32_t process_name(char *buffer, uint32_t capacity) {
  struct process *process = process_current();
  if (!process || !buffer || !capacity)
    return -1;
  uint32_t length = (uint32_t)strlen(process->name);
  if (length + 1 > capacity)
    return -1;
  memcpy(buffer, process->name, length + 1);
  return (int32_t)length;
}

int32_t process_environment_get(const char *name, char *buffer,
                                uint32_t capacity) {
  MUTEX_SCOPE(&process_mutex);
  struct process *process = process_current();
  if (!process || !environment_name_valid(name) || !buffer || !capacity)
    return -1;
  int32_t slot = environment_find(process, name);
  if (slot < 0)
    return -1;
  uint32_t length = (uint32_t)strlen(process->environment[slot].value);
  if (length + 1 > capacity)
    return -1;
  memcpy(buffer, process->environment[slot].value, length + 1);
  return (int32_t)length;
}

int32_t process_environment_set(const char *name, const char *value) {
  MUTEX_SCOPE(&process_mutex);
  struct process *process = process_current();
  if (!process || !environment_name_valid(name) || !value ||
      strlen(value) >= PROCESS_ENVIRONMENT_VALUE_CAPACITY)
    return -1;
  int32_t slot = environment_find(process, name);
  if (slot < 0) {
    for (uint32_t index = 0; index < PROCESS_ENVIRONMENT_COUNT; index++) {
      if (!process->environment[index].used) {
        slot = (int32_t)index;
        break;
      }
    }
  }
  if (slot < 0)
    return -1;
  environment_put(process, name, value);
  if (strcmp(name, "PWD") == 0 && value[0] == '/')
    strncpy(process->cwd, value, sizeof(process->cwd) - 1);
  return 0;
}

int32_t process_environment_unset(const char *name) {
  MUTEX_SCOPE(&process_mutex);
  struct process *process = process_current();
  if (!process || !environment_name_valid(name))
    return -1;
  int32_t slot = environment_find(process, name);
  if (slot < 0)
    return -1;
  memset(&process->environment[slot], 0, sizeof(process->environment[slot]));
  return 0;
}

int32_t process_environment_list(struct process_environment_entry *entries,
                                 uint32_t capacity) {
  MUTEX_SCOPE(&process_mutex);
  struct process *process = process_current();
  if (!process || (!entries && capacity))
    return -1;
  uint32_t count = 0;
  for (uint32_t index = 0; index < PROCESS_ENVIRONMENT_COUNT; index++) {
    if (!process->environment[index].used)
      continue;
    if (count < capacity)
      entries[count] = process->environment[index];
    count++;
  }
  return (int32_t)count;
}

bool process_set_affinity(uint32_t pid, int16_t core) {
  MUTEX_SCOPE(&process_mutex);
  for (struct process *p = process_list; p; p = p->next) {
    if (p->pid == pid &&
        (p->state == PROCESS_READY || p->state == PROCESS_RUNNING)) {
      scheduler_set_affinity(p->thread_id, core);
      return true;
    }
  }
  return false;
}

uint32_t process_current_uid(void) {
  struct process *process = process_current();
  return process ? process->uid : 0;
}

uint32_t process_current_gid(void) {
  struct process *process = process_current();
  return process ? process->gid : 0;
}

int32_t process_set_uid(uint32_t uid) {
  struct process *process = process_current();
  if (!process)
    return -1;
  if (process->uid != 0 && process->uid != uid)
    return -1;
  process->uid = uid;
  return 0;
}

int32_t process_set_gid(uint32_t gid) {
  struct process *process = process_current();
  if (!process)
    return -1;
  if (process->uid != 0 && process->gid != gid)
    return -1;
  process->gid = gid;
  return 0;
}

int32_t process_session_begin(uint32_t uid, uint32_t gid) {
  struct process *process = process_current();
  if (!process || process->uid != 0)
    return -1;
  session_uid = uid;
  session_gid = gid;
  session_active = true;
  return 0;
}
