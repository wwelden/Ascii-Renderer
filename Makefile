# ASCII Renderer build configuration

CC      ?= cc
CSTD    ?= c11
CFLAGS  ?= -std=$(CSTD) -Wall -Wextra -Wpedantic -Iinclude
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

.PHONY: all run clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(BIN)
	./$(BIN)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
