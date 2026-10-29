#include "random_core.h"
#include "sha512.h"

static void hash_df(const uint8_t *part_a, size_t len_a, const uint8_t *part_b,
                    size_t len_b, const uint8_t *part_c, size_t len_c,
                    uint8_t *out, size_t out_len) {
  uint32_t bits = (uint32_t)(out_len * 8u);
  uint8_t counter = 1;
  size_t done = 0;
  while (done < out_len) {
    pure_sha512_ctx ctx;
    uint8_t digest[PURE_SHA512_DIGEST_LEN];
    uint8_t header[5] = {counter, (uint8_t)(bits >> 24), (uint8_t)(bits >> 16),
                         (uint8_t)(bits >> 8), (uint8_t)bits};
    pure_sha512_init(&ctx);
    pure_sha512_update(&ctx, header, sizeof(header));
    if (len_a)
      pure_sha512_update(&ctx, part_a, len_a);
    if (len_b)
      pure_sha512_update(&ctx, part_b, len_b);
    if (len_c)
      pure_sha512_update(&ctx, part_c, len_c);
    pure_sha512_final(&ctx, digest);
    size_t take = out_len - done;
    if (take > HDRBG_OUT_LEN)
      take = HDRBG_OUT_LEN;
    for (size_t i = 0; i < take; i++)
      out[done + i] = digest[i];
    done += take;
    counter++;
    pure_memzero(digest, sizeof(digest));
    pure_memzero(&ctx, sizeof(ctx));
  }
}

static void derive_constant(struct hdrbg *state) {
  const uint8_t prefix = 0x00;
  hash_df(&prefix, 1, state->v, HDRBG_SEED_LEN, 0, 0, state->c,
          HDRBG_SEED_LEN);
}

static void add_into_v(struct hdrbg *state, const uint8_t *addend,
                       size_t addend_len) {
  unsigned carry = 0;
  size_t index = HDRBG_SEED_LEN;
  size_t offset = addend_len;
  while (index > 0) {
    index--;
    unsigned sum = state->v[index] + carry;
    if (offset > 0) {
      offset--;
      sum += addend[offset];
    }
    state->v[index] = (uint8_t)sum;
    carry = sum >> 8;
  }
}

void hdrbg_instantiate(struct hdrbg *state, const uint8_t *entropy,
                       size_t entropy_len, const uint8_t *personal,
                       size_t personal_len) {
  hash_df(entropy, entropy_len, personal, personal_len, 0, 0, state->v,
          HDRBG_SEED_LEN);
  derive_constant(state);
  state->counter = 1;
  state->ready = 1;
}

void hdrbg_reseed(struct hdrbg *state, const uint8_t *entropy,
                  size_t entropy_len, const uint8_t *additional,
                  size_t additional_len) {
  uint8_t head[1 + HDRBG_SEED_LEN];
  head[0] = 0x01;
  for (size_t i = 0; i < HDRBG_SEED_LEN; i++)
    head[1 + i] = state->v[i];
  hash_df(head, sizeof(head), entropy, entropy_len, additional, additional_len,
          state->v, HDRBG_SEED_LEN);
  derive_constant(state);
  state->counter = 1;
  pure_memzero(head, sizeof(head));
}

static void increment(uint8_t *value, size_t len) {
  for (size_t i = len; i > 0; i--) {
    if (++value[i - 1] != 0)
      return;
  }
}

int hdrbg_generate(struct hdrbg *state, uint8_t *out, size_t count) {
  if (!state->ready || count > HDRBG_MAX_REQUEST)
    return -1;
  if (state->counter > HDRBG_RESEED_INTERVAL)
    return -2;
  uint8_t data[HDRBG_SEED_LEN];
  for (size_t i = 0; i < HDRBG_SEED_LEN; i++)
    data[i] = state->v[i];
  size_t done = 0;
  while (done < count) {
    uint8_t digest[PURE_SHA512_DIGEST_LEN];
    pure_sha512(data, HDRBG_SEED_LEN, digest);
    size_t take = count - done;
    if (take > HDRBG_OUT_LEN)
      take = HDRBG_OUT_LEN;
    for (size_t i = 0; i < take; i++)
      out[done + i] = digest[i];
    done += take;
    increment(data, HDRBG_SEED_LEN);
    pure_memzero(digest, sizeof(digest));
  }
  uint8_t block[1 + HDRBG_SEED_LEN];
  uint8_t h[PURE_SHA512_DIGEST_LEN];
  block[0] = 0x03;
  for (size_t i = 0; i < HDRBG_SEED_LEN; i++)
    block[1 + i] = state->v[i];
  pure_sha512(block, sizeof(block), h);
  add_into_v(state, h, sizeof(h));
  add_into_v(state, state->c, HDRBG_SEED_LEN);
  uint8_t counter_bytes[8];
  for (int i = 0; i < 8; i++)
    counter_bytes[i] = (uint8_t)(state->counter >> (56 - 8 * i));
  add_into_v(state, counter_bytes, sizeof(counter_bytes));
  state->counter++;
  pure_memzero(data, sizeof(data));
  pure_memzero(block, sizeof(block));
  pure_memzero(h, sizeof(h));
  return 0;
}
