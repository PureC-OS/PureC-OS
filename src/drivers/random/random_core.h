#pragma once

#include <stddef.h>
#include <stdint.h>

#define HDRBG_OUT_LEN 64u
#define HDRBG_SEED_LEN 111u
#define HDRBG_MAX_REQUEST 65536u
#define HDRBG_RESEED_INTERVAL (1ull << 48)

struct hdrbg {
  uint8_t v[HDRBG_SEED_LEN];
  uint8_t c[HDRBG_SEED_LEN];
  uint64_t counter;
  int ready;
};

void hdrbg_instantiate(struct hdrbg *state, const uint8_t *entropy,
                       size_t entropy_len, const uint8_t *personal,
                       size_t personal_len);
void hdrbg_reseed(struct hdrbg *state, const uint8_t *entropy,
                  size_t entropy_len, const uint8_t *additional,
                  size_t additional_len);
int hdrbg_generate(struct hdrbg *state, uint8_t *out, size_t count);
