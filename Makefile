# QISC Build System
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -I./include $(shell llvm-config --cflags)
LDFLAGS = $(shell llvm-config --ldflags --libs core) -lm

# Directories
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c) \
       $(wildcard $(SRC_DIR)/lexer/*.c) \
       $(wildcard $(SRC_DIR)/parser/*.c) \
       $(wildcard $(SRC_DIR)/interpreter/*.c) \
       $(wildcard $(SRC_DIR)/typechecker/*.c) \
       $(wildcard $(SRC_DIR)/ir/*.c) \
       $(wildcard $(SRC_DIR)/types/*.c) \
       $(wildcard $(SRC_DIR)/profile/*.c) \
       $(wildcard $(SRC_DIR)/codegen/*.c) \
       $(wildcard $(SRC_DIR)/cli/*.c) \
       $(wildcard $(SRC_DIR)/personality/*.c) \
       $(wildcard $(SRC_DIR)/utils/*.c)

OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Output
TARGET = $(BIN_DIR)/qisc

# Debug/Release
DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CFLAGS += -g -O0 -DDEBUG -fno-stack-protector
else
    CFLAGS += -O3 -DNDEBUG
endif

.PHONY: all clean test

all: dirs $(TARGET)

dirs:
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(BUILD_DIR)/lexer
	@mkdir -p $(BUILD_DIR)/parser
	@mkdir -p $(BUILD_DIR)/interpreter
	@mkdir -p $(BUILD_DIR)/typechecker
	@mkdir -p $(BUILD_DIR)/ir
	@mkdir -p $(BUILD_DIR)/types
	@mkdir -p $(BUILD_DIR)/profile
	@mkdir -p $(BUILD_DIR)/codegen
	@mkdir -p $(BUILD_DIR)/cli
	@mkdir -p $(BUILD_DIR)/personality
	@mkdir -p $(BUILD_DIR)/utils

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

test:
	@echo "Running tests..."
	@./scripts/run_tests.sh

# Install (optional)
install: all
	cp $(TARGET) /usr/local/bin/
