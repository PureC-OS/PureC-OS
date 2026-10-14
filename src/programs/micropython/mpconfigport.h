#include <stdint.h>

#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)

#define MICROPY_ENABLE_COMPILER     (1)

#define MICROPY_ENABLE_GC                 (1)
#define MICROPY_HELPER_REPL               (1)
#define MICROPY_MODULE_FROZEN_MPY         (0)
#define MICROPY_ENABLE_EXTERNAL_IMPORT    (1)

#define MICROPY_ALLOC_PATH_MAX            (256)

#define MICROPY_ALLOC_PARSE_CHUNK_INIT    (16)

#define MICROPY_PY_SYS_MODULES            (0)
#define MICROPY_PY_SYS_EXIT               (0)
#define MICROPY_PY_SYS_PATH               (0)
#define MICROPY_PY_SYS_ARGV               (0)

typedef long mp_off_t;

#ifndef alloca
#define alloca(x) __builtin_alloca(x)
#endif

#define MICROPY_HW_BOARD_NAME "purec"
#define MICROPY_HW_MCU_NAME "x86_64"

#define MICROPY_MIN_USE_STDOUT (1)
#define MICROPY_HEAP_SIZE      (262144)

#define MP_STATE_PORT MP_STATE_VM
