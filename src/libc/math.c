// PureC hosted libc: minimal math for TCC's own needs (ldexp for hex
// float parsing; fabs for codegen). Bit-level implementations, exact,
// nonameof FP-environment handling (round-to-nearest hardware default).

#include "include/hosted/math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "include/hosted/string.h"

double fabs(double value) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    bits &= ~(1ULL << 63);
    memcpy(&value, &bits, sizeof(value));
    return value;
}

double ldexp(double value, int exponent) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    int exp = (int)((bits >> 52) & 0x7FF);
    uint64_t mant = bits & 0xFFFFFFFFFFFFFULL;
    bool negative = (bits >> 63) != 0;
    if (exp == 0x7FF) return value; // inf/nan pass through
    if (exp == 0) {
        if (mant == 0) return value; // signed zero
        // Subnormal: normalize by scaling into the normal range.
        value *= 4503599627370496.0; // 2^52, exact
        memcpy(&bits, &value, sizeof(bits));
        exp = (int)((bits >> 52) & 0x7FF);
        exponent -= 52;
        if (exp == 0) return negative ? -0.0 : 0.0; // still subnormal
    }
    long result = (long)exp + exponent;
    if (result >= 0x7FF) {
        bits = ((uint64_t)negative << 63) | (0x7FFULL << 52);
        memcpy(&value, &bits, sizeof(value));
        return value; // overflow to inf
    }
    if (result <= 0) {
        // Gradual underflow: shift mantissa (with hidden bit) right,
        // round-half-even on the dropped bits.
        mant |= 1ULL << 52;
        long shift = 1 - result;
        if (shift >= 64) {
            bits = (uint64_t)negative << 63;
            memcpy(&value, &bits, sizeof(value));
            return value; // signed zero
        }
        uint64_t mask = (shift == 64) ? ~0ULL : ((1ULL << shift) - 1);
        uint64_t dropped = mant & mask;
        uint64_t half = 1ULL << (shift - 1);
        mant >>= shift;
        if (dropped > half || (dropped == half && (mant & 1))) mant++;
        if (mant >= (1ULL << 52)) {
            // Rounded up into the smallest normal.
            bits = ((uint64_t)negative << 63) | (1ULL << 52);
        } else {
            bits = ((uint64_t)negative << 63) | mant;
        }
        memcpy(&value, &bits, sizeof(value));
        return value;
    }
    bits = ((uint64_t)negative << 63) | ((uint64_t)result << 52) | mant;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
