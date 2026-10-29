ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

CHOC_DIR ?= $(ROOT_DIR)/chocolate-doom
SDL_DIR ?= $(ROOT_DIR)/SDL
OBJ_DIR := $(BIN_DIR)/obj/chocolate
LIB_DIR := $(BIN_DIR)/lib
PROGRAM_DIR := $(BIN_DIR)/programs
LINKER_SCRIPT := $(ROOT_DIR)/linker-userspace.ld
TARGET := $(PROGRAM_DIR)/chocolate-doom

CHOC_SOURCES := \
	src/i_main.c \
	src/i_system.c \
	src/m_argv.c \
	src/m_misc.c \
	src/aes_prng.c \
	src/d_event.c \
	src/d_iwad.c \
	src/d_loop.c \
	src/d_mode.c \
	src/deh_str.c \
	src/gusconf.c \
	src/i_cdmus.c \
	src/i_endoom.c \
	src/i_flmusic.c \
	src/i_glob.c \
	src/i_input.c \
	src/i_joystick.c \
	src/i_musicpack.c \
	src/i_oplmusic.c \
	src/i_pcsound.c \
	src/i_sdlmusic.c \
	src/i_sdlsound.c \
	src/i_sound.c \
	src/i_timer.c \
	src/i_video.c \
	src/i_videohr.c \
	src/midifallback.c \
	src/midifile.c \
	src/mus2mid.c \
	src/m_bbox.c \
	src/m_cheat.c \
	src/m_config.c \
	src/m_controls.c \
	src/m_fixed.c \
	src/net_client.c \
	src/net_common.c \
	src/net_dedicated.c \
	src/net_gui.c \
	src/net_io.c \
	src/net_loop.c \
	src/net_packet.c \
	src/net_petname.c \
	src/net_query.c \
	src/net_sdl.c \
	src/net_server.c \
	src/net_structrw.c \
	src/p_rejectpad.c \
	src/sha1.c \
	src/memio.c \
	src/tables.c \
	src/v_diskicon.c \
	src/v_video.c \
	src/w_checksum.c \
	src/w_main.c \
	src/w_wad.c \
	src/w_file.c \
	src/w_file_stdc.c \
	src/w_merge.c \
	src/z_zone.c \
	src/deh_io.c \
	src/deh_main.c \
	src/deh_mapping.c \
	src/deh_text.c \
	src/doom/am_map.c \
	src/doom/deh_ammo.c \
	src/doom/deh_bexstr.c \
	src/doom/deh_cheat.c \
	src/doom/deh_doom.c \
	src/doom/deh_frame.c \
	src/doom/deh_misc.c \
	src/doom/deh_ptr.c \
	src/doom/deh_sound.c \
	src/doom/deh_thing.c \
	src/doom/deh_weapon.c \
	src/doom/d_items.c \
	src/doom/d_main.c \
	src/doom/d_net.c \
	src/doom/doomdef.c \
	src/doom/doomstat.c \
	src/doom/dstrings.c \
	src/doom/f_finale.c \
	src/doom/f_wipe.c \
	src/doom/g_game.c \
	src/doom/hu_lib.c \
	src/doom/hu_stuff.c \
	src/doom/info.c \
	src/doom/m_menu.c \
	src/doom/m_random.c \
	src/doom/p_ceilng.c \
	src/doom/p_doors.c \
	src/doom/p_enemy.c \
	src/doom/p_floor.c \
	src/doom/p_inter.c \
	src/doom/p_lights.c \
	src/doom/p_map.c \
	src/doom/p_maputl.c \
	src/doom/p_mobj.c \
	src/doom/p_plats.c \
	src/doom/p_pspr.c \
	src/doom/p_saveg.c \
	src/doom/p_setup.c \
	src/doom/p_sight.c \
	src/doom/p_spec.c \
	src/doom/p_switch.c \
	src/doom/p_telept.c \
	src/doom/p_tick.c \
	src/doom/p_user.c \
	src/doom/r_bsp.c \
	src/doom/r_data.c \
	src/doom/r_draw.c \
	src/doom/r_main.c \
	src/doom/r_plane.c \
	src/doom/r_segs.c \
	src/doom/r_sky.c \
	src/doom/r_things.c \
	src/doom/s_sound.c \
	src/doom/sounds.c \
	src/doom/statdump.c \
	src/doom/st_lib.c \
	src/doom/st_stuff.c \
	src/doom/wi_stuff.c \
	opl/opl.c \
	opl/opl3.c \
	opl/opl_queue.c \
	opl/opl_sdl.c \
	opl/opl_timer.c \
	pcsound/pcsound.c \
	pcsound/pcsound_sdl.c \
	$(filter-out textscreen/txt_fileselect.c,$(patsubst $(CHOC_DIR)/%,%,$(wildcard $(CHOC_DIR)/textscreen/txt_*.c)))
OBJECTS := $(CHOC_SOURCES:%.c=$(OBJ_DIR)/%.o)

CHOC_CFLAGS := -D__PUREC__ -DDISABLE_SDL2NET=1 -w \
	-I$(ROOT_DIR)/src/chocolate -I$(CHOC_DIR)/src -I$(CHOC_DIR)/src/doom \
	-I$(CHOC_DIR)/opl -I$(CHOC_DIR)/pcsound -I$(CHOC_DIR)/textscreen \
	-I$(SDL_DIR)/include -I$(ROOT_DIR)/src/libsdl2_mixer -I$(ROOT_DIR)/src \
	-I$(ROOT_DIR)/src/libc/include/hosted

LIBS := $(LIB_DIR)/libSDL2_mixer.a $(LIB_DIR)/libSDL2.a $(LIB_DIR)/libpureaudio.a \
	$(LIB_DIR)/libpuregui.a $(LIB_DIR)/libpurec.a

.PHONY: all
all:
	@if test ! -f "$(CHOC_DIR)/src/i_main.c"; then \
	  echo "chocolate-doom not found in $(CHOC_DIR), skipping"; \
	elif test ! -f "$(LIB_DIR)/libSDL2.a"; then \
	  echo "libSDL2.a missing, skipping chocolate-doom"; \
	else $(MAKE) -C $(ROOT_DIR)/src/libsdl2_mixer && $(MAKE) -f $(firstword $(MAKEFILE_LIST)) $(TARGET); fi

-include $(OBJECTS:.o=.d)

$(TARGET): $(OBJECTS) $(LIB_DIR)/crt0.o $(LIBS) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(OBJECTS) $(LIBS)

$(OBJ_DIR)/%.o: $(CHOC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(DEPFLAGS) $(USER_CFLAGS) $(CHOC_CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(TARGET)
