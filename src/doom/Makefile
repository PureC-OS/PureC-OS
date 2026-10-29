ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

DOOM_REPO ?= $(ROOT_DIR)/doomgeneric
DOOM_SRC ?= $(firstword $(wildcard $(DOOM_REPO)/doomgeneric/doomgeneric.c $(DOOM_REPO)/doomgeneric.c))
DOOM_DIR := $(patsubst %/,%,$(dir $(DOOM_SRC)))

OBJ_DIR := $(BIN_DIR)/obj/doom
LIB_DIR := $(BIN_DIR)/lib
PROGRAM_DIR := $(BIN_DIR)/programs
LINKER_SCRIPT := $(ROOT_DIR)/linker-userspace.ld
TARGET := $(PROGRAM_DIR)/doom

DOOM_OBJECTS := dummy am_map doomdef doomstat dstrings d_event d_items d_iwad \
	d_loop d_main d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff info \
	i_cdmus i_endoom i_joystick i_scale i_sound i_system i_timer memio m_argv \
	m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc m_random p_ceilng \
	p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj p_plats \
	p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick p_user r_bsp \
	r_data r_draw r_main r_plane r_segs r_sky r_things sha1 sounds statdump \
	st_lib st_stuff s_sound tables v_video wi_stuff w_checksum w_file w_main \
	w_wad z_zone w_file_stdc i_input i_video doomgeneric
OBJECTS := $(DOOM_OBJECTS:%=$(OBJ_DIR)/%.o) $(OBJ_DIR)/doomgeneric_purec.o $(OBJ_DIR)/i_purecsound.o

DOOM_CFLAGS := -DNORMALUNIX -DDOOMGENERIC_RESX=640 -DDOOMGENERIC_RESY=400 \
	-w -I$(ROOT_DIR)/src/doom/include -I$(ROOT_DIR)/src -I$(ROOT_DIR)/src/libc/include/hosted

.PHONY: all
all:
	@if test -z "$(DOOM_SRC)"; then \
	  echo "doomgeneric sources not found in $(DOOM_REPO), skipping Doom"; \
	else $(MAKE) -f $(firstword $(MAKEFILE_LIST)) DOOM_SRC=$(DOOM_SRC) $(TARGET); fi

-include $(OBJECTS:.o=.d)

$(TARGET): $(OBJECTS) $(LIB_DIR)/crt0.o $(LIB_DIR)/libpuregui.a $(LIB_DIR)/libpureaudio.a $(LIB_DIR)/libpurec.a $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(OBJECTS) $(LIB_DIR)/libpuregui.a $(LIB_DIR)/libpureaudio.a $(LIB_DIR)/libpurec.a

$(OBJ_DIR)/%.o: $(DOOM_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(DEPFLAGS) $(USER_CFLAGS) $(DOOM_CFLAGS) -I$(DOOM_DIR) -c $< -o $@

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/doom/%.c
	@mkdir -p $(@D)
	$(CC) $(DEPFLAGS) $(USER_CFLAGS) $(DOOM_CFLAGS) -I$(DOOM_DIR) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(TARGET)
