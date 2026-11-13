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

STAGE := $(BIN_DIR)/ns-stage
SLIB := $(STAGE)/lib
SINC := $(STAGE)/include
SPC := $(SLIB)/pkgconfig

PNG_SOURCES := png pngerror pngget pngmem pngpread pngread pngrio pngrtran \
	pngrutil pngset pngtrans pngwio pngwrite pngwtran pngwutil
PNG_OBJECTS := $(PNG_SOURCES:%=$(OBJ_DIR)/libpng/%.o)

JPEG_SOURCES := jcapimin jcapistd jccoefct jccolor jcdctmgr jchuff jcicc jcinit \
	jcmainct jcmarker jcmaster jcomapi jcparam jcphuff jcprepct jcsample jctrans \
	jdapimin jdapistd jdatadst jdatasrc jdcoefct jdcolor jddctmgr jdhuff jdicc \
	jdinput jdmainct jdmarker jdmaster jdmerge jdphuff jdpostct jdsample jdtrans \
	jerror jfdctflt jfdctfst jfdctint jidctflt jidctfst jidctint jidctred jquant1 \
	jquant2 jutils jmemmgr jmemnobs jsimd_none
JPEG_OBJECTS := $(JPEG_SOURCES:%=$(OBJ_DIR)/libjpeg/%.o)

EXPAT_SOURCES := xmlparse xmlrole xmltok
EXPAT_OBJECTS := $(EXPAT_SOURCES:%=$(OBJ_DIR)/expat/%.o)

.PHONY: all zlib png jpeg expat curl deps
all: deps
deps: zlib png jpeg expat curl
curl: $(SLIB)/libcurl.a
zlib: $(SLIB)/libz.a
png: $(SLIB)/libpng.a
jpeg: $(SLIB)/libjpeg.a
expat: $(SLIB)/libexpat.a

-include $(PNG_OBJECTS:.o=.d) $(JPEG_OBJECTS:.o=.d) $(EXPAT_OBJECTS:.o=.d)

-include $(ZLIB_OBJECTS:.o=.d)

$(OBJ_DIR)/zlib/%.o: $(NSLIBS)/zlib/%.c
	@mkdir -p $(@D)
	$(CC) $(DEPFLAGS) $(NS_CFLAGS) -I$(NSLIBS)/zlib -DZ_HAVE_UNISTD_H -DHAVE_STDARG_H -c $< -o $@

$(LIBZ): $(ZLIB_OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(SLIB)/libz.a: $(LIBZ)
	@mkdir -p $(SLIB) $(SINC) $(SPC)
	cp $(LIBZ) $@
	cp $(NSLIBS)/zlib/zlib.h $(NSLIBS)/zlib/zconf.h $(SINC)/
	printf 'prefix=$(STAGE)\nName: zlib\nDescription: zlib\nVersion: 1.3.1\nLibs: -L$(SLIB) -lz\nCflags: -I$(SINC)\n' > $(SPC)/zlib.pc

$(OBJ_DIR)/libpng/%.o: $(NSLIBS)/libpng/%.c $(OBJ_DIR)/libpng/pnglibconf.h
	@mkdir -p $(@D)
	$(CC) $(DEPFLAGS) $(NS_CFLAGS) -I$(OBJ_DIR)/libpng -I$(NSLIBS)/libpng -I$(NSLIBS)/zlib -DPNG_INTEL_SSE_OPT=0 -c $< -o $@

$(OBJ_DIR)/libpng/pnglibconf.h: $(NSLIBS)/libpng/scripts/pnglibconf.h.prebuilt
	@mkdir -p $(@D)
	cp $< $@

$(SLIB)/libpng.a: $(PNG_OBJECTS)
	@mkdir -p $(SLIB) $(SINC)/libpng16 $(SPC)
	$(AR) rcs $@ $^
	cp $(NSLIBS)/libpng/png.h $(NSLIBS)/libpng/pngconf.h $(OBJ_DIR)/libpng/pnglibconf.h $(SINC)/
	cp $(NSLIBS)/libpng/png.h $(NSLIBS)/libpng/pngconf.h $(OBJ_DIR)/libpng/pnglibconf.h $(SINC)/libpng16/
	printf 'prefix=$(STAGE)\nName: libpng\nDescription: libpng\nVersion: 1.6.47\nLibs: -L$(SLIB) -lpng -lz\nCflags: -I$(SINC)\n' > $(SPC)/libpng.pc
	cp $(SPC)/libpng.pc $(SPC)/libpng16.pc

$(OBJ_DIR)/libjpeg/%.o: $(NSLIBS)/libjpeg-turbo/%.c
	@mkdir -p $(@D)
	$(CC) $(DEPFLAGS) $(NS_CFLAGS) -I$(ROOT_DIR)/mk/netsurf/jpeg -I$(NSLIBS)/libjpeg-turbo -c $< -o $@

$(SLIB)/libjpeg.a: $(JPEG_OBJECTS)
	@mkdir -p $(SLIB) $(SINC) $(SPC)
	$(AR) rcs $@ $^
	cp $(NSLIBS)/libjpeg-turbo/jpeglib.h $(NSLIBS)/libjpeg-turbo/jmorecfg.h $(NSLIBS)/libjpeg-turbo/jerror.h $(ROOT_DIR)/mk/netsurf/jpeg/jconfig.h $(SINC)/
	printf 'prefix=$(STAGE)\nName: libjpeg\nDescription: libjpeg\nVersion: 2.1.5\nLibs: -L$(SLIB) -ljpeg\nCflags: -I$(SINC)\n' > $(SPC)/libjpeg.pc

$(OBJ_DIR)/expat/%.o: $(NSLIBS)/libexpat/expat/lib/%.c
	@mkdir -p $(@D)
	$(CC) $(DEPFLAGS) $(NS_CFLAGS) -I$(ROOT_DIR)/mk/netsurf/expat -I$(NSLIBS)/libexpat/expat/lib -DHAVE_EXPAT_CONFIG_H -DXML_STATIC -c $< -o $@

$(SLIB)/libexpat.a: $(EXPAT_OBJECTS)
	@mkdir -p $(SLIB) $(SINC) $(SPC)
	$(AR) rcs $@ $^
	cp $(NSLIBS)/libexpat/expat/lib/expat.h $(NSLIBS)/libexpat/expat/lib/expat_external.h $(SINC)/
	printf 'prefix=$(STAGE)\nName: expat\nDescription: expat\nVersion: 2.6.4\nLibs: -L$(SLIB) -lexpat\nCflags: -I$(SINC) -DXML_STATIC\n' > $(SPC)/expat.pc

CURL_OBJ_DIR := $(BIN_DIR)/obj/curl/lib

.PHONY: FORCE
FORCE:

$(SLIB)/libcurl.a: $(SLIB)/libz.a FORCE
	$(MAKE) -C $(ROOT_DIR)/src/programs $(BIN_DIR)/programs/curl
	@mkdir -p $(SLIB) $(SINC)/curl $(SPC)
	$(AR) rcs $@ $$(find $(CURL_OBJ_DIR) -name '*.o')
	cp $(ROOT_DIR)/curl/include/curl/*.h $(SINC)/curl/
	cp $(BIN_DIR)/lib/libwolf.a $(SLIB)/libwolf.a
	printf 'prefix=$(STAGE)\nName: libcurl\nDescription: libcurl\nVersion: 8.23.1\nLibs: -L$(SLIB) -lcurl -lwolf -lz\nCflags: -I$(SINC) -DCURL_STATICLIB\n' > $(SPC)/libcurl.pc

BROWSER := $(BIN_DIR)/programs/netsurf
NS_BUILD := $(ROOT_DIR)/netsurf/build/Linux-framebuffer
NS_LINK_LIBS := $(addprefix $(SLIB)/,libcurl.a libwolf.a libnsfb.a libnsgif.a libnsbmp.a \
	libcss.a libdom.a libhubbub.a libparserutils.a libwapcaplet.a libnsutils.a libnslog.a \
	libutf8proc.a libpng.a libjpeg.a libexpat.a libz.a)
LIBGCC := $(shell $(CC) -print-libgcc-file-name)

.PHONY: browser nslibs nscore
browser: $(BROWSER)

nslibs: deps
	NS_EXTRA_CFLAGS="-DWITHOUT_ICONV_FILTER" $(ROOT_DIR)/mk/netsurf/build-libs.sh

nscore: nslibs
	$(ROOT_DIR)/mk/netsurf/build-netsurf.sh

NS_START := $(OBJ_DIR)/ns_start.o
NS_LINK_LIBS := $(filter-out $(SLIB)/libnsfb.a,$(NS_LINK_LIBS))

$(NS_START): $(ROOT_DIR)/mk/netsurf/ns_start.c
	@mkdir -p $(@D)
	$(CC) $(NS_CFLAGS) -c $< -o $@

NS_DOWNLOAD := $(OBJ_DIR)/ns_download.o
$(NS_DOWNLOAD): $(ROOT_DIR)/mk/netsurf/ns_download.c
	@mkdir -p $(@D)
	$(CC) $(NS_CFLAGS) -I$(ROOT_DIR)/netsurf -I$(ROOT_DIR)/netsurf/include -I$(SINC) -c $< -o $@

$(BROWSER): nscore $(NS_START) $(NS_DOWNLOAD) $(ROOT_DIR)/mk/netsurf/netsurf.ld $(BIN_DIR)/lib/crt0.o $(BIN_DIR)/lib/libpurec.a $(BIN_DIR)/lib/libpurefs.a $(BIN_DIR)/lib/libpguiw.a $(BIN_DIR)/lib/libpuregui.a
	@mkdir -p $(@D)
	$(LD) -S --wrap=main --wrap=curl_easy_setopt --wrap=netsurf_register -T $(ROOT_DIR)/mk/netsurf/netsurf.ld -o $@ $(BIN_DIR)/lib/crt0.o $(NS_START) $(NS_DOWNLOAD) \
		$$(find $(NS_BUILD) -name '*.o' ! -name '*javascript_none_none.o') --whole-archive $(SLIB)/libnsfb.a --no-whole-archive \
		--start-group $(NS_LINK_LIBS) $(BIN_DIR)/lib/libpguiw.a $(BIN_DIR)/lib/libpuregui.a \
		$(BIN_DIR)/lib/libpurefs.a $(BIN_DIR)/lib/libpurec.a $(LIBGCC) --end-group
