#include "krandom.h"
#include "../../drivers/random/random_abi.h"
#include "../devices/device_manager.h"
#include "../module/kmod_so.h"
#include "../sync/mutex.h"

#define KRANDOM_MODULE "random"
#define KRANDOM_MODULE_PATH "/lib/modules/random.elf"
#define KRANDOM_FALLBACK_SEED 0x9E3779B97F4A7C15ULL

static mutex_t lock;
static random_fill_fn module_fill;
static random_reseed_fn module_reseed;
static bool module_tried;
static uint64_t fallback_state;

static uint64_t read_tsc(void) {
  uint32_t low, high;
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  return ((uint64_t)high << 32) | low;
}

static bool hardware_random(uint64_t *value) {
  uint32_t eax = 1, ebx, ecx = 0, edx;
  __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
  if (!(ecx & (1U << 30)))
    return false;
  for (int attempt = 0; attempt < 10; attempt++) {
    uint64_t result;
    unsigned char ok;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(result), "=qm"(ok));
    if (ok) {
      *value = result;
      return true;
    }
  }
  return false;
}

static uint64_t fallback_next(void) {
  if (!fallback_state) {
    uint64_t seed = 0;
    (void)hardware_random(&seed);
    fallback_state = seed ^ read_tsc() ^ KRANDOM_FALLBACK_SEED;
    if (!fallback_state)
      fallback_state = KRANDOM_FALLBACK_SEED;
  }
  fallback_state ^= fallback_state >> 12;
  fallback_state ^= fallback_state << 25;
  fallback_state ^= fallback_state >> 27;
  return fallback_state * 0x2545F4914F6CDD1DULL;
}

static void fallback_fill(uint8_t *bytes, uint32_t count) {
  for (uint32_t i = 0; i < count;) {
    uint64_t value = fallback_next();
    for (uint32_t j = 0; j < 8 && i < count; j++, i++)
      bytes[i] = (uint8_t)(value >> (j * 8));
  }
}

static void fallback_mix(const uint8_t *bytes, uint32_t count) {
  (void)fallback_next();
  for (uint32_t i = 0; i < count; i++)
    fallback_state = (fallback_state << 5 | fallback_state >> 59) ^ bytes[i];
  if (!fallback_state)
    fallback_state = KRANDOM_FALLBACK_SEED;
}

static void module_bind(void) {
  if (module_tried || !kmod_syms_ready())
    return;
  module_tried = true;
  if (!devman_load_module(KRANDOM_MODULE, KRANDOM_MODULE_PATH))
    return;
  uint64_t abi = kmod_get(KRANDOM_MODULE, RANDOM_SYM_ABI);
  uint64_t fill = kmod_get(KRANDOM_MODULE, RANDOM_SYM_FILL);
  uint64_t reseed = kmod_get(KRANDOM_MODULE, RANDOM_SYM_RESEED);
  if (!abi || !fill || !reseed)
    return;
  if (((random_abi_fn)abi)() != PURECRYPT_RANDOM_ABI)
    return;
  module_fill = (random_fill_fn)fill;
  module_reseed = (random_reseed_fn)reseed;
}

void krandom_init(void) {
  MUTEX_SCOPE(&lock);
  module_bind();
}

bool krandom_hardened(void) { return module_fill != 0; }

void krandom_fill(void *buffer, uint32_t count) {
  MUTEX_SCOPE(&lock);
  if (module_fill && module_fill(buffer, count) == 0)
    return;
  fallback_fill(buffer, count);
}

void krandom_mix(const void *data, uint32_t count) {
  MUTEX_SCOPE(&lock);
  if (module_reseed && count && module_reseed(data, count) == 0)
    return;
  fallback_mix(data, count);
}
