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

.PHONY: all run clean web serve

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
	rm -rf $(BUILD_DIR) $(WEB_OUT) $(WEB_OUT:.js=.wasm)

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

-include $(DEPS)
