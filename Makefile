# Makefile - Modbus RTU/TCP Gateway
# Targets: all, test, clean, memcheck, install

CC      := gcc
CFLAGS  := -Wall -Wextra -Wpedantic -std=c11 -Isrc
LDFLAGS := -lpthread

BUILD_DIR := build
SRC_DIR   := src
TEST_DIR  := tests

SRCS := $(SRC_DIR)/gateway.c \
        $(SRC_DIR)/modbus_rtu.c \
        $(SRC_DIR)/modbus_tcp.c

OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

TARGET := $(BUILD_DIR)/gateway

# Debug build
ifeq ($(DEBUG),1)
    CFLAGS += -g -O0 -DLOG_LEVEL_DEFAULT=0
else
    CFLAGS += -O2
endif

.PHONY: all test clean memcheck install

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build successful: $@"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Tests ────────────────────────────────────────────────
TEST_SRCS := $(wildcard $(TEST_DIR)/*.c)
TEST_BINS := $(TEST_SRCS:$(TEST_DIR)/%.c=$(BUILD_DIR)/test_%)

test: $(BUILD_DIR) $(TEST_BINS)
	@echo "Running tests..."
	@for t in $(TEST_BINS); do \
	    echo "  $$t"; \
	    $$t && echo "    PASS" || echo "    FAIL"; \
	done

$(BUILD_DIR)/test_%: $(TEST_DIR)/%.c $(SRC_DIR)/modbus_rtu.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ── Memory check ─────────────────────────────────────────
memcheck: $(TARGET)
	valgrind --leak-check=full --error-exitcode=1 \
	         ./$(TARGET) --help

# ── Install ───────────────────────────────────────────────
INSTALL_PREFIX ?= /usr/local
install: $(TARGET)
	install -d $(INSTALL_PREFIX)/bin
	install -m 755 $(TARGET) $(INSTALL_PREFIX)/bin/modbus-gateway
	@echo "Installed to $(INSTALL_PREFIX)/bin/modbus-gateway"

# ── Clean ─────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)
	@echo "Cleaned."
