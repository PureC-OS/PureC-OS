ROOT_DIR ?= $(abspath ../../..)
include $(ROOT_DIR)/mk/toolchain.mk

CROSS_COMPILE = x86_64-elf-
BUILD ?= $(BIN_DIR)/obj/programs/micropython
PROGRAM := $(BIN_DIR)/programs/micropython

QSTR_DEFS = qstrdefsport.h

MICROPY_ROM_TEXT_COMPRESSION = 0

include $(ROOT_DIR)/src/programs/micropython/py/mkenv.mk
include $(TOP)/py/py.mk

INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)
INC += -I$(ROOT_DIR)/src/libc/include/hosted

CFLAGS += $(INC) -Wall -std=gnu99 -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mno-red-zone -mcmodel=small -DNDEBUG -DPUREC=1

SRC_C = \
	main.c \
	mphalport.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	shared/readline/readline.c \

SRC_QSTR += $(SRC_C)

OBJ += $(PY_CORE_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

LIBPUREC := $(BIN_DIR)/lib/libpurec.a
CRT0 := $(BIN_DIR)/lib/crt0.o
LINKER_SCRIPT := $(ROOT_DIR)/linker-userspace.ld

.PHONY: all
all: $(PROGRAM)

$(PROGRAM): $(OBJ) $(CRT0) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(CRT0) $(OBJ) $(LIBPUREC)

include $(TOP)/py/mkrules.mk
