#ifndef PUREC_HOSTED_ENDIAN_H
#define PUREC_HOSTED_ENDIAN_H

#include <stdint.h>

#define LITTLE_ENDIAN 1234
#define BIG_ENDIAN 4321
#define PDP_ENDIAN 3412
#define BYTE_ORDER LITTLE_ENDIAN

#define __LITTLE_ENDIAN LITTLE_ENDIAN
#define __BIG_ENDIAN BIG_ENDIAN
#define __BYTE_ORDER BYTE_ORDER

#define htole16(x) ((uint16_t)(x))
#define htole32(x) ((uint32_t)(x))
#define htole64(x) ((uint64_t)(x))
#define le16toh(x) ((uint16_t)(x))
#define le32toh(x) ((uint32_t)(x))
#define le64toh(x) ((uint64_t)(x))

#define htobe16(x) ((uint16_t)(((uint16_t)(x) >> 8) | ((uint16_t)(x) << 8)))
#define htobe32(x) ((uint32_t)(((uint32_t)(x) >> 24) | (((uint32_t)(x) >> 8) & 0xff00u) | (((uint32_t)(x) << 8) & 0xff0000u) | ((uint32_t)(x) << 24)))
#define htobe64(x) ((uint64_t)(((uint64_t)htobe32((uint32_t)(x)) << 32) | htobe32((uint32_t)((uint64_t)(x) >> 32))))
#define be16toh(x) htobe16(x)
#define be32toh(x) htobe32(x)
#define be64toh(x) htobe64(x)

#endif
