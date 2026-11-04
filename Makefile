# ASCII Renderer build configuration

CC      ?= cc
CSTD    ?= c11
CFLAGS  ?= -std=$(CSTD) -Wall -Wextra -Wpedantic -Iinclude -Ithird_party
LDFLAGS ?=
LDLIBS  ?= -lm

# Toggle an optimized build with: make BUILD=release
BUILD   ?= debug
ifeq ($(BUILD),release)
CFLAGS  += -O2 -DNDEBUG
else
SANITIZE := -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS   += -O0 -g $(SANITIZE)
LDFLAGS  += $(SANITIZE)
endif

SRC_DIR   := src
BUILD_DIR := build
BIN       := $(BUILD_DIR)/ascii-renderer

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all run clean web web-doom serve doom

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Vendored stb_image: compile without our strict warnings (it is not our code).
$(BUILD_DIR)/stb_image_impl.o: CFLAGS := $(filter-out -Wall -Wextra -Wpedantic,$(CFLAGS)) -w

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(BIN)
	./$(BIN)

clean:
	rm -rf $(BUILD_DIR) $(WEB_OUT) $(WEB_OUT:.js=.wasm) \
	    $(WEB_DOOM_OUT) $(WEB_DOOM_OUT:.js=.wasm) $(WEB_DOOM_OUT:.js=.data)

# --- WebAssembly build (needs Emscripten's emcc on PATH) ---
# The browser has no tty, so src/term.c is swapped for web/term_web.c, which
# implements the same term.h interface on top of hooks in web/public/main.js.
# ASYNCIFY lets the existing blocking game loops yield to the browser inside
# emscripten_sleep() instead of being rewritten as callbacks.
EMCC     ?= emcc
WEB_OUT  := web/public/ascii-renderer.js
WEB_SRCS := web/term_web.c $(filter-out $(SRC_DIR)/term.c,$(SRCS))
EMFLAGS  := -std=$(CSTD) -O2 -Iinclude -Ithird_party \
            -sASYNCIFY -sMODULARIZE=1 -sEXPORT_NAME=createAsciiRenderer \
            -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web

web: $(WEB_OUT)

$(WEB_OUT): $(WEB_SRCS) $(wildcard include/*.h)
	$(EMCC) $(EMFLAGS) $(WEB_SRCS) -o $@ -lm

serve: web
	python3 -m http.server --directory web/public 8000

# --- Phase 5: Doom -----------------------------------------------------------
#
# Builds build/ascii-doom from the vendored doomgeneric engine plus our
# platform port (src/doom/doomgeneric_ascii.c) and the renderer library.
# Always optimized and sanitizer-free: the vanilla engine trips UBSan on
# decades-old idioms, and 35fps under ASan is not a useful benchmark.

DOOM_DIR   := third_party/doomgeneric-src/doomgeneric
DOOM_BUILD := $(BUILD_DIR)/doom
DOOM_BIN   := $(BUILD_DIR)/ascii-doom

# Render at Doom's native 320x200 so the engine's blit is a straight copy
# (fb_scaling = 1) and the half-block output is 320 cols x 100 rows.
DOOM_DEFS   := -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200
DOOM_CFLAGS := -std=$(CSTD) -O2 -DNDEBUG -Iinclude -Ithird_party -I$(DOOM_DIR) $(DOOM_DEFS)

# The engine object list mirrors upstream's SRC_DOOM (minus their platform
# port, doomgeneric_xlib) so we build exactly what upstream links.
DOOM_ENGINE_NAMES := dummy am_map doomdef doomstat dstrings d_event d_items \
    d_iwad d_loop d_main d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff \
    info i_cdmus i_endoom i_joystick i_scale i_sound i_system i_timer memio \
    m_argv m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc m_random \
    p_ceilng p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj \
    p_plats p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick \
    p_user r_bsp r_data r_draw r_main r_plane r_segs r_sky r_things sha1 \
    sounds statdump st_lib st_stuff s_sound tables v_video wi_stuff \
    w_checksum w_file w_main w_wad z_zone w_file_stdc i_input i_video \
    doomgeneric

DOOM_ENGINE_OBJS := $(addprefix $(DOOM_BUILD)/engine/, \
    $(addsuffix .o, $(DOOM_ENGINE_NAMES)))
DOOM_PORT_OBJS := $(addprefix $(DOOM_BUILD)/port/, \
    doomgeneric_ascii.o term.o canvas.o framebuffer.o render.o \
    stb_image_impl.o)
DOOM_DEPS := $(DOOM_ENGINE_OBJS:.o=.d) $(DOOM_PORT_OBJS:.o=.d)

doom: $(DOOM_BIN)

$(DOOM_BIN): $(DOOM_ENGINE_OBJS) $(DOOM_PORT_OBJS)
	$(CC) $(DOOM_ENGINE_OBJS) $(DOOM_PORT_OBJS) -o $@ $(LDLIBS)

# Engine sources are not ours: no warnings.
$(DOOM_BUILD)/engine/%.o: $(DOOM_DIR)/%.c | $(DOOM_BUILD)
	$(CC) $(DOOM_CFLAGS) -w -MMD -MP -c $< -o $@

# Our port and renderer keep the project's warning set.
$(DOOM_BUILD)/port/%.o: $(SRC_DIR)/%.c | $(DOOM_BUILD)
	$(CC) $(DOOM_CFLAGS) -Wall -Wextra -Wpedantic -MMD -MP -c $< -o $@

$(DOOM_BUILD)/port/doomgeneric_ascii.o: $(SRC_DIR)/doom/doomgeneric_ascii.c | $(DOOM_BUILD)
	$(CC) $(DOOM_CFLAGS) -Wall -Wextra -Wpedantic -MMD -MP -c $< -o $@

$(DOOM_BUILD)/port/stb_image_impl.o: $(SRC_DIR)/stb_image_impl.c | $(DOOM_BUILD)
	$(CC) $(DOOM_CFLAGS) -w -MMD -MP -c $< -o $@

$(DOOM_BUILD):
	mkdir -p $(DOOM_BUILD)/engine $(DOOM_BUILD)/port

# --- WebAssembly Doom (needs emcc; web/build.sh fetches the shareware WAD) --
# The same port as `make doom` with the same tty swap as `make web`: term.c
# is replaced by web/term_web.c, and the engine plus port compile in one emcc
# invocation. The IWAD is baked into ascii-doom.data by Emscripten's
# --preload-file and appears to the engine as /doom1.wad in MEMFS.
# ASYNCIFY_STACK_SIZE is generous because the pacing sleep unwinds from deep
# inside the engine's render path, not from a shallow main loop.
WEB_DOOM_OUT  := web/public/ascii-doom.js
WEB_DOOM_SRCS := web/term_web.c $(SRC_DIR)/doom/doomgeneric_ascii.c \
                 $(SRC_DIR)/canvas.c $(SRC_DIR)/framebuffer.c \
                 $(SRC_DIR)/render.c $(SRC_DIR)/stb_image_impl.c
WEB_DOOM_ENGINE_SRCS := $(addprefix $(DOOM_DIR)/, \
    $(addsuffix .c, $(DOOM_ENGINE_NAMES)))
# -std=gnu11 (overrides EMFLAGS' c11): strict ISO mode sets __STRICT_ANSI__,
# under which Emscripten's musl headers hide POSIX functions the vendored
# engine calls (strdup); glibc and macOS expose them by default, so only the
# wasm build ever noticed.
WEB_DOOM_FLAGS := $(EMFLAGS) -std=gnu11 -w -DNDEBUG -I$(DOOM_DIR) $(DOOM_DEFS) \
    -sEXPORT_NAME=createAsciiDoom -sASYNCIFY_STACK_SIZE=1048576 \
    -sSTACK_SIZE=4194304 --preload-file doom1.wad@/doom1.wad

web-doom: $(WEB_DOOM_OUT)

$(WEB_DOOM_OUT): $(WEB_DOOM_SRCS) $(WEB_DOOM_ENGINE_SRCS) \
                 $(wildcard include/*.h) doom1.wad
	$(EMCC) $(WEB_DOOM_FLAGS) $(WEB_DOOM_SRCS) $(WEB_DOOM_ENGINE_SRCS) -o $@ -lm

-include $(DOOM_DEPS)

-include $(DEPS)
