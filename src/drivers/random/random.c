#include "random_abi.h"
#include "random_core.h"
#include "sha512.h"

#define ENTROPY_BYTES 64u
#define RESEED_EVERY 4096u
#define RDRAND_RETRIES 16

static struct hdrbg generator;
static uint32_t since_reseed;
static const uint8_t personalization[] = "PureC-OS purecrypt hash-drbg v1";

static int rdrand_available(void) {
  uint32_t eax = 1, ebx, ecx = 0, edx;
  __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
  return (ecx >> 30) & 1u;
}

static int rdrand_u64(uint64_t *out) {
  for (int attempt = 0; attempt < RDRAND_RETRIES; attempt++) {
    uint64_t value;
    unsigned char ok;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(value), "=qm"(ok));
    if (ok) {
      *out = value;
      return 1;
    }
  }
  return 0;
}

static uint64_t read_tsc(void) {
  uint32_t low, high;
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  return ((uint64_t)high << 32) | low;
}

static void gather_entropy(uint8_t out[ENTROPY_BYTES]) {
  uint8_t pool[PURE_SHA512_DIGEST_LEN];
  pure_sha512_ctx ctx;
  pure_sha512_init(&ctx);
  int hardware = rdrand_available();
  for (uint32_t i = 0; i < ENTROPY_BYTES / 8u; i++) {
    uint64_t word = 0;
    if (!hardware || !rdrand_u64(&word))
      word = 0;
    uint64_t first = read_tsc();
    for (volatile int spin = 0; spin < 64; spin++)
      ;
    uint64_t second = read_tsc();
    uint64_t mix[3] = {word, first, second - first};
    pure_sha512_update(&ctx, (const uint8_t *)mix, sizeof(mix));
  }
  pure_sha512_final(&ctx, pool);
  for (uint32_t i = 0; i < ENTROPY_BYTES; i++)
    out[i] = pool[i % PURE_SHA512_DIGEST_LEN];
  pure_memzero(pool, sizeof(pool));
  pure_memzero(&ctx, sizeof(ctx));
}

static void generator_open(void) {
  if (generator.ready)
    return;
  uint8_t entropy[ENTROPY_BYTES];
  gather_entropy(entropy);
  hdrbg_instantiate(&generator, entropy, sizeof(entropy), personalization,
                    sizeof(personalization) - 1);
  pure_memzero(entropy, sizeof(entropy));
  since_reseed = 0;
}

uint32_t purec_random_abi(void) { return PURECRYPT_RANDOM_ABI; }

int purec_random_fill(void *buffer, uint32_t count) {
  if (!buffer)
    return -1;
  generator_open();
  uint8_t *bytes = buffer;
  while (count) {
    uint32_t chunk =
        count > PURECRYPT_RANDOM_CHUNK ? PURECRYPT_RANDOM_CHUNK : count;
    if (since_reseed >= RESEED_EVERY) {
      uint8_t entropy[ENTROPY_BYTES];
      gather_entropy(entropy);
      hdrbg_reseed(&generator, entropy, sizeof(entropy), 0, 0);
      pure_memzero(entropy, sizeof(entropy));
      since_reseed = 0;
    }
    int status = hdrbg_generate(&generator, bytes, chunk);
    if (status != 0)
      return status;
    since_reseed++;
    bytes += chunk;
    count -= chunk;
  }
  return 0;
}

int purec_random_reseed(const void *data, uint32_t count) {
  if (!data || !count)
    return -1;
  generator_open();
  uint8_t entropy[ENTROPY_BYTES];
  gather_entropy(entropy);
  hdrbg_reseed(&generator, entropy, sizeof(entropy), data, count);
  pure_memzero(entropy, sizeof(entropy));
  since_reseed = 0;
  return 0;
}
