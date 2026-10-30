ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

GM_DIR ?= $(ROOT_DIR)/gnumake
OBJ_DIR := $(BIN_DIR)/obj/gnumake
LIB_DIR := $(BIN_DIR)/lib
PROGRAM_DIR := $(BIN_DIR)/programs
LINKER_SCRIPT := $(ROOT_DIR)/linker-userspace.ld
TARGET := $(PROGRAM_DIR)/make

GM_SOURCES := ar arscan commands default dir expand file function getopt getopt1 \
	guile hash implicit job load loadapi main misc output posixos read remake rule \
	shuffle signame strcache variable version vpath remote-stub
GM_LIB_SOURCES := glob fnmatch
GM_OBJECTS := $(GM_SOURCES:%=$(OBJ_DIR)/%.o) $(GM_LIB_SOURCES:%=$(OBJ_DIR)/lib_%.o) \
	$(OBJ_DIR)/shim.o

GM_CFLAGS := -w -std=gnu11 -DHAVE_CONFIG_H -DLOCALEDIR=\"/usr/share/locale\" \
	-I$(ROOT_DIR)/src/make -I$(OBJ_DIR)/inc -I$(GM_DIR)/src -I$(GM_DIR)/lib \
	-I$(ROOT_DIR)/src -I$(ROOT_DIR)/src/libc/include/hosted

LIBS := $(LIB_DIR)/libpurefs.a $(LIB_DIR)/libpurec.a

.PHONY: all
all:
	@if test ! -f "$(GM_DIR)/src/main.c"; then \
	  echo "gnumake sources not found in $(GM_DIR), skipping make port"; \
	else $(MAKE) -f $(firstword $(MAKEFILE_LIST)) $(TARGET); fi

$(OBJ_DIR)/inc/glob.h: $(GM_DIR)/lib/glob.in.h
	@mkdir -p $(@D)
	cp $< $@

$(OBJ_DIR)/inc/fnmatch.h: $(GM_DIR)/lib/fnmatch.in.h
	@mkdir -p $(@D)
	cp $< $@

HEADERS := $(OBJ_DIR)/inc/glob.h $(OBJ_DIR)/inc/fnmatch.h

$(TARGET): $(GM_OBJECTS) $(LIB_DIR)/crt0.o $(LIBS) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(GM_OBJECTS) $(LIBS)

$(OBJ_DIR)/%.o: $(GM_DIR)/src/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) $(GM_CFLAGS) -c $< -o $@

$(OBJ_DIR)/lib_%.o: $(GM_DIR)/lib/%.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) $(GM_CFLAGS) -c $< -o $@

$(OBJ_DIR)/shim.o: $(ROOT_DIR)/src/make/shim.c
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) $(GM_CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(TARGET)
