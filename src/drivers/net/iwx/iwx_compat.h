#pragma once

#include <stddef.h>
#include <stdint.h>

typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;
typedef uint64_t bus_addr_t;

#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define nitems(a) (sizeof(a) / sizeof((a)[0]))
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))

#define htole16(x) ((uint16_t)(x))
#define htole32(x) ((uint32_t)(x))
#define htole64(x) ((uint64_t)(x))
#define le16toh(x) ((uint16_t)(x))
#define le32toh(x) ((uint32_t)(x))
#define le64toh(x) ((uint64_t)(x))
#define le32_to_cpup(p) (*(const uint32_t *)(p))

#define ETHER_ADDR_LEN 6
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_NWID_LEN 32

struct ieee80211_frame {
    uint8_t i_fc[2];
    uint8_t i_dur[2];
    uint8_t i_addr1[IEEE80211_ADDR_LEN];
    uint8_t i_addr2[IEEE80211_ADDR_LEN];
    uint8_t i_addr3[IEEE80211_ADDR_LEN];
    uint8_t i_seq[2];
} __packed;

#define setbit(a, i) ((a)[(i) / 8] |= (uint8_t)(1u << ((i) % 8)))
#define clrbit(a, i) ((a)[(i) / 8] &= (uint8_t)~(1u << ((i) % 8)))
#define isset(a, i) ((a)[(i) / 8] & (1u << ((i) % 8)))

static inline int fls(int x)
{
    return x ? 32 - __builtin_clz((unsigned int)x) : 0;
}

static inline int flsl(long x)
{
    return x ? 64 - __builtin_clzl((unsigned long)x) : 0;
}

#include "../../../../third_party/openbsd-iwx/if_iwxreg.h"
