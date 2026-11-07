ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

NSLIBS := $(ROOT_DIR)/netsurf-libs
OBJ_DIR := $(BIN_DIR)/obj/netsurf
LIB_DIR := $(BIN_DIR)/lib
HOSTED := $(ROOT_DIR)/src/libc/include/hosted
NS_CFLAGS := $(USER_CFLAGS) -w -I$(HOSTED)

ZLIB_SOURCES := adler32 compress crc32 deflate gzclose gzlib gzread gzwrite \
	infback inffast inflate inftrees trees uncompr zutil
ZLIB_OBJECTS := $(ZLIB_SOURCES:%=$(OBJ_DIR)/zlib/%.o)
LIBZ := $(LIB_DIR)/libz.a

.PHONY: all zlib
all: zlib
zlib: $(LIBZ)

-include $(ZLIB_OBJECTS:.o=.d)

$(OBJ_DIR)/zlib/%.o: $(NSLIBS)/zlib/%.c
	@mkdir -p $(@D)
	$(CC) $(DEPFLAGS) $(NS_CFLAGS) -I$(NSLIBS)/zlib -DZ_HAVE_UNISTD_H -DHAVE_STDARG_H -c $< -o $@

$(LIBZ): $(ZLIB_OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^
