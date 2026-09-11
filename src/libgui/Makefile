ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

OBJ_DIR := $(BIN_DIR)/obj/libgui
LIB_DIR := $(BIN_DIR)/lib
CORE_SOURCES := theme.c draw.c window.c event.c
CORE_OBJECTS := $(CORE_SOURCES:%.c=$(OBJ_DIR)/%.o)
WIDGET_OBJECT := $(OBJ_DIR)/widgets.o
CORE_TARGET := $(LIB_DIR)/libpuregui.a
WIDGET_TARGET := $(LIB_DIR)/libpguiw.a

.PHONY: all
all: $(CORE_TARGET) $(WIDGET_TARGET)

-include $(CORE_OBJECTS:.o=.d) $(WIDGET_OBJECT:.o=.d)

$(CORE_TARGET): $(CORE_OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(WIDGET_TARGET): $(WIDGET_OBJECT)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/libgui/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@
