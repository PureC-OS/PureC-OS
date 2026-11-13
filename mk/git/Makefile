ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

GIT_SRC := $(ROOT_DIR)/git
STAGE := $(BIN_DIR)/ns-stage
HOSTED := $(ROOT_DIR)/src/libc/include/hosted
LIBGCC := $(shell $(CC) -print-libgcc-file-name)
PROGRAM_DIR := $(BIN_DIR)/programs

GIT_CFLAGS := $(USER_CFLAGS) -w -isystem $(HOSTED) -isystem $(STAGE)/include -D_GNU_SOURCE
GIT_LDFLAGS := -nostdlib -static -Wl,-T,$(ROOT_DIR)/linker-userspace.ld -Wl,-S $(BIN_DIR)/lib/crt0.o
GIT_LIBS := -Wl,--start-group $(STAGE)/lib/libcurl.a $(STAGE)/lib/libwolf.a $(STAGE)/lib/libz.a \
	$(BIN_DIR)/lib/libpurefs.a $(BIN_DIR)/lib/libpurec.a $(LIBGCC) -Wl,--end-group

GIT_MAKE := $(MAKE) -C $(GIT_SRC) \
	CC=$(CC) AR=$(AR) \
	CFLAGS="$(GIT_CFLAGS)" \
	LDFLAGS="$(GIT_LDFLAGS)" \
	EXTLIBS="$(GIT_LIBS)" \
	CURL_LDFLAGS="" CURL_CFLAGS="-I$(STAGE)/include -DCURL_STATICLIB" \
	ZLIB_PATH="$(STAGE)" \
	uname_S=PureC uname_O=PureC uname_M=x86_64 uname_R=2 uname_V=1 uname_P=x86_64 \
	prefix=/usr gitexecdir=/usr/libexec/git-core template_dir=/usr/share/git-core/templates \
	sysconfdir=/etc SHELL_PATH=/bin/sh \
	NO_RUST=YesPlease NO_PERL=YesPlease NO_PYTHON=YesPlease NO_TCLTK=YesPlease \
	NO_GETTEXT=YesPlease NO_ICONV=YesPlease NO_OPENSSL=YesPlease NO_EXPAT=YesPlease \
	NO_PTHREADS=YesPlease NO_REGEX=YesPlease NO_MMAP=YesPlease NO_IPV6=YesPlease \
	NO_UNIX_SOCKETS=YesPlease NO_INSTALL_HARDLINKS=YesPlease SKIP_DASHED_BUILT_INS=YesPlease \
	NO_GITWEB=YesPlease NO_SVN_TESTS=YesPlease NO_NSEC=YesPlease \
	USE_LIBPCRE=  NO_CURL=

.PHONY: all git-core remote-http clean
all: git-core remote-http

git-core:
	$(GIT_MAKE) git
	@mkdir -p $(PROGRAM_DIR)
	cp $(GIT_SRC)/git $(PROGRAM_DIR)/git

remote-http:
	$(GIT_MAKE) git-remote-http
	@mkdir -p $(PROGRAM_DIR)
	cp $(GIT_SRC)/git-remote-http $(PROGRAM_DIR)/git-remote-http

clean:
	$(GIT_MAKE) clean
