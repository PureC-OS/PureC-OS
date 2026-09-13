ROOT_DIR ?= $(abspath ..)
BIN_DIR ?= $(ROOT_DIR)/bin
PROGRAM_DIR := $(BIN_DIR)/programs
LIB_DIR := $(BIN_DIR)/lib
LD_SCRIPT := $(ROOT_DIR)/userspace/ld/userspace.ld

CARGO ?= cargo
TARGET := x86_64-unknown-none
PROFILE_FLAG := --release
OUT_DIR := $(CURDIR)/target/$(TARGET)/release

export RUSTFLAGS := \
	-C no-redzone=yes \
	-C relocation-model=static \
	-C code-model=small \
	-C link-arg=-T$(LD_SCRIPT) \
	-C link-arg=-L$(LIB_DIR) \
	-C link-arg=-lpurec

.PHONY: all libpurec bins install clean

all: install

libpurec:
	$(MAKE) -C $(ROOT_DIR)/src/libc

bins: libpurec $(LD_SCRIPT)
	$(CARGO) build $(PROFILE_FLAG) --target $(TARGET)

install: bins
	@mkdir -p $(PROGRAM_DIR)
	cp $(OUT_DIR)/pl $(PROGRAM_DIR)/pl
	@echo "staged: $(PROGRAM_DIR)/pl"

clean:
	$(CARGO) clean
	rm -f $(PROGRAM_DIR)/pl
