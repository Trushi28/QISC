# QISC Build System
CC ?= cc
CFLAGS = -Wall -Wextra -std=c11 -I./include $(shell llvm-config --cflags)

ifeq ($(OS),Windows_NT)
	EXE_EXT = .exe
	THREAD_FLAGS =
else
	EXE_EXT =
	THREAD_FLAGS = -pthread
endif

CFLAGS += $(THREAD_FLAGS)
LDFLAGS = $(shell llvm-config --ldflags --libs core) -lm $(THREAD_FLAGS)

# Directories
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin
LIB_DIR = lib

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c) \
       $(wildcard $(SRC_DIR)/lexer/*.c) \
       $(wildcard $(SRC_DIR)/parser/*.c) \
       $(wildcard $(SRC_DIR)/interpreter/*.c) \
       $(wildcard $(SRC_DIR)/typechecker/*.c) \
       $(wildcard $(SRC_DIR)/ir/*.c) \
       $(wildcard $(SRC_DIR)/types/*.c) \
       $(wildcard $(SRC_DIR)/profile/*.c) \
       $(wildcard $(SRC_DIR)/achievements/*.c) \
       $(wildcard $(SRC_DIR)/codegen/*.c) \
       $(wildcard $(SRC_DIR)/cli/*.c) \
       $(wildcard $(SRC_DIR)/personality/*.c) \
       $(wildcard $(SRC_DIR)/pragma/*.c) \
       $(wildcard $(SRC_DIR)/utils/*.c) \
       $(wildcard $(SRC_DIR)/optimization/*.c) \
       $(wildcard $(SRC_DIR)/syntax/*.c)

OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Runtime library (linked into compiled programs when profiling)
RUNTIME_SRC = $(SRC_DIR)/runtime/qisc_runtime.c
RUNTIME_OBJ = $(LIB_DIR)/qisc_runtime.o

# Array runtime library
ARRAY_RUNTIME_SRC = $(SRC_DIR)/runtime/qisc_array.c
ARRAY_RUNTIME_OBJ = $(LIB_DIR)/qisc_array.o

# Stream runtime library (lazy evaluation for pipelines)
STREAM_SRC = $(SRC_DIR)/runtime/qisc_stream.c
STREAM_OBJ = $(LIB_DIR)/qisc_stream.o

# Text I/O runtime library (stdin/stdout helpers)
IO_RUNTIME_SRC = $(SRC_DIR)/runtime/qisc_io.c
IO_RUNTIME_OBJ = $(LIB_DIR)/qisc_io.o

# Error handling runtime library (try/catch with setjmp/longjmp)
ERROR_SRC = $(SRC_DIR)/runtime/qisc_error.c
ERROR_OBJ = $(LIB_DIR)/qisc_error.o

# Output
TARGET = $(BIN_DIR)/qisc$(EXE_EXT)

# Debug/Release
DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CFLAGS += -g -O0 -DDEBUG -fno-stack-protector
else
    CFLAGS += -O3 -DNDEBUG
endif

.PHONY: all clean test runtime stream error

all: dirs $(TARGET) runtime io stream error

runtime: dirs $(RUNTIME_OBJ) $(ARRAY_RUNTIME_OBJ) $(IO_RUNTIME_OBJ)

io: dirs $(IO_RUNTIME_OBJ)

stream: dirs $(STREAM_OBJ)

error: dirs $(ERROR_OBJ)

$(RUNTIME_OBJ): $(RUNTIME_SRC)
	$(CC) -Wall -Wextra -std=c11 -c $< -o $@

$(ARRAY_RUNTIME_OBJ): $(ARRAY_RUNTIME_SRC)
	$(CC) -Wall -Wextra -std=c11 -I$(SRC_DIR)/runtime -c $< -o $@

$(STREAM_OBJ): $(STREAM_SRC)
	$(CC) -Wall -Wextra -std=c11 -I$(SRC_DIR)/runtime -c $< -o $@

$(IO_RUNTIME_OBJ): $(IO_RUNTIME_SRC)
	$(CC) -Wall -Wextra -std=c11 -I$(SRC_DIR)/runtime -c $< -o $@

$(ERROR_OBJ): $(ERROR_SRC)
	$(CC) -Wall -Wextra -std=c11 -I$(SRC_DIR)/runtime -c $< -o $@

dirs:
ifeq ($(OS),Windows_NT)
	@if not exist $(BIN_DIR) mkdir $(BIN_DIR)
	@if not exist $(LIB_DIR) mkdir $(LIB_DIR)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	@if not exist $(BUILD_DIR)\lexer mkdir $(BUILD_DIR)\lexer
	@if not exist $(BUILD_DIR)\parser mkdir $(BUILD_DIR)\parser
	@if not exist $(BUILD_DIR)\interpreter mkdir $(BUILD_DIR)\interpreter
	@if not exist $(BUILD_DIR)\typechecker mkdir $(BUILD_DIR)\typechecker
	@if not exist $(BUILD_DIR)\ir mkdir $(BUILD_DIR)\ir
	@if not exist $(BUILD_DIR)\types mkdir $(BUILD_DIR)\types
	@if not exist $(BUILD_DIR)\profile mkdir $(BUILD_DIR)\profile
	@if not exist $(BUILD_DIR)\achievements mkdir $(BUILD_DIR)\achievements
	@if not exist $(BUILD_DIR)\codegen mkdir $(BUILD_DIR)\codegen
	@if not exist $(BUILD_DIR)\cli mkdir $(BUILD_DIR)\cli
	@if not exist $(BUILD_DIR)\personality mkdir $(BUILD_DIR)\personality
	@if not exist $(BUILD_DIR)\pragma mkdir $(BUILD_DIR)\pragma
	@if not exist $(BUILD_DIR)\utils mkdir $(BUILD_DIR)\utils
	@if not exist $(BUILD_DIR)\optimization mkdir $(BUILD_DIR)\optimization
	@if not exist $(BUILD_DIR)\syntax mkdir $(BUILD_DIR)\syntax
else
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(LIB_DIR)
	@mkdir -p $(BUILD_DIR)/lexer
	@mkdir -p $(BUILD_DIR)/parser
	@mkdir -p $(BUILD_DIR)/interpreter
	@mkdir -p $(BUILD_DIR)/typechecker
	@mkdir -p $(BUILD_DIR)/ir
	@mkdir -p $(BUILD_DIR)/types
	@mkdir -p $(BUILD_DIR)/profile
	@mkdir -p $(BUILD_DIR)/achievements
	@mkdir -p $(BUILD_DIR)/codegen
	@mkdir -p $(BUILD_DIR)/cli
	@mkdir -p $(BUILD_DIR)/personality
	@mkdir -p $(BUILD_DIR)/pragma
	@mkdir -p $(BUILD_DIR)/utils
	@mkdir -p $(BUILD_DIR)/optimization
	@mkdir -p $(BUILD_DIR)/syntax
endif

$(TARGET): $(OBJS) $(STREAM_OBJ) $(ARRAY_RUNTIME_OBJ)
	$(CC) $(OBJS) $(STREAM_OBJ) $(ARRAY_RUNTIME_OBJ) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
ifeq ($(OS),Windows_NT)
	@if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
	@if exist $(BIN_DIR) rmdir /s /q $(BIN_DIR)
	@if exist $(LIB_DIR) rmdir /s /q $(LIB_DIR)
else
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(LIB_DIR)
endif

test:
	@echo "Running tests..."
	@./scripts/run_tests.sh

# Install (optional)
install: all
	cp $(TARGET) /usr/local/bin/
	cp $(RUNTIME_OBJ) /usr/local/lib/
