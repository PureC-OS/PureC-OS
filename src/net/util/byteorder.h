#pragma once

#include <stdint.h>

static inline uint16_t net_read_be16(const uint8_t *data){
    return (uint16_t)(((uint16_t)data[0]<<8)|data[1]);
}

static inline uint32_t net_read_be32(const uint8_t *data){
    return ((uint32_t)data[0]<<24)|((uint32_t)data[1]<<16)
        |((uint32_t)data[2]<<8)|data[3];
}

static inline void net_write_be16(uint8_t *data, uint16_t value){
    data[0]=(uint8_t)(value>>8);
    data[1]=(uint8_t)value;
}

static inline void net_write_be32(uint8_t *data, uint32_t value){
    data[0]=(uint8_t)(value>>24);
    data[1]=(uint8_t)(value>>16);
    data[2]=(uint8_t)(value>>8);
    data[3]=(uint8_t)value;
}
