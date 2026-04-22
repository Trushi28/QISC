# QISC Build System
CC ?= cc
LLVM_TOOLCHAIN_DIR = toolchains/llvm
LLVM_TOOLCHAIN_BIN_DIR = $(LLVM_TOOLCHAIN_DIR)/bin
LLVM_TOOLCHAIN_LIB_DIR = $(LLVM_TOOLCHAIN_DIR)/lib
LLVM_TOOLCHAIN_INCLUDE_DIR = $(LLVM_TOOLCHAIN_DIR)/include
LLVM_TOOLCHAIN_CONFIG = $(LLVM_TOOLCHAIN_BIN_DIR)/llvm-config
LLVM_TOOLCHAIN_READY = $(LLVM_TOOLCHAIN_DIR)/.ready
LLVM_CONFIG_WRAPPER = scripts/qisc-llvm-config.sh
LLVM_TOOLCHAIN_ARCHIVE ?= llvm-toolchain.tar.xz
GIT_REMOTE_ORIGIN := $(strip $(shell git config --get remote.origin.url 2>/dev/null))
GITHUB_REPO := $(patsubst %.git,%,$(patsubst https://github.com/%,%,$(patsubst git@github.com:%,%,$(GIT_REMOTE_ORIGIN))))
ifeq ($(strip $(GITHUB_REPO)),)
LLVM_TOOLCHAIN_URL ?=
else
LLVM_TOOLCHAIN_URL ?= https://github.com/$(GITHUB_REPO)/releases/latest/download/$(notdir $(LLVM_TOOLCHAIN_ARCHIVE))
endif
HOST_LLVM_CONFIG ?= llvm-config
LLVM_CONFIG ?= $(LLVM_CONFIG_WRAPPER)
export LLVM_TOOLCHAIN_DIR
export LLVM_TOOLCHAIN_ARCHIVE
export LLVM_TOOLCHAIN_URL
export HOST_LLVM_CONFIG
LLVM_CFLAGS := $(shell $(LLVM_CONFIG) --cflags)
LLVM_LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags)
LLVM_LIBS := $(shell $(LLVM_CONFIG) --libs core)
LLVM_SHARED_MODE := $(shell $(LLVM_CONFIG) --shared-mode 2>/dev/null || echo shared)
LLVM_LIBFILES := $(shell $(LLVM_CONFIG) --libfiles core 2>/dev/null)

CFLAGS = -Wall -Wextra -std=c11 -I./include $(LLVM_CFLAGS)

ifeq ($(OS),Windows_NT)
	EXE_EXT = .exe
	THREAD_FLAGS =
	QISC_RPATH =
else
	EXE_EXT =
	THREAD_FLAGS = -pthread
	QISC_RPATH = -Wl,-rpath,'$$ORIGIN/../lib/llvm' -Wl,--enable-new-dtags
endif

CFLAGS += $(THREAD_FLAGS)
LDFLAGS = $(LLVM_LDFLAGS) $(LLVM_LIBS) -lm $(THREAD_FLAGS) $(QISC_RPATH)

# Directories
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin
LIB_DIR = lib
LLVM_BUNDLE_DIR = $(LIB_DIR)/llvm
DIST_DIR = dist
DIST_ROOT = $(DIST_DIR)/qisc-portable

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

.PHONY: all clean test runtime stream error bundle-llvm portable bootstrap-llvm-toolchain fetch-llvm-toolchain dist

all: dirs $(TARGET) runtime io stream error bundle-llvm

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
	@if not exist $(LLVM_BUNDLE_DIR) mkdir $(LLVM_BUNDLE_DIR)
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
	@mkdir -p $(LLVM_BUNDLE_DIR)
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

bundle-llvm: $(TARGET)
ifeq ($(OS),Windows_NT)
	@echo "LLVM bundling is not configured on Windows in this Makefile."
else
ifeq ($(LLVM_SHARED_MODE),shared)
	@if [ -n "$(LLVM_LIBFILES)" ]; then \
		echo "Bundling LLVM runtime into $(LLVM_BUNDLE_DIR)"; \
		for lib in $(LLVM_LIBFILES); do \
			resolved="$$(readlink -f "$$lib" 2>/dev/null || printf '%s' "$$lib")"; \
			cp -Lf "$$resolved" $(LLVM_BUNDLE_DIR)/; \
			if [ "$$(basename "$$resolved")" != "$$(basename "$$lib")" ]; then \
				ln -sf "$$(basename "$$resolved")" "$(LLVM_BUNDLE_DIR)/$$(basename "$$lib")"; \
			fi; \
		done; \
	fi
else
	@echo "LLVM is not in shared mode; no runtime bundling needed."
endif
endif

portable: all
	@echo "Portable QISC build is ready at $(TARGET)"

bootstrap-llvm-toolchain:
	@mkdir -p $(LLVM_TOOLCHAIN_BIN_DIR)
	@mkdir -p $(LLVM_TOOLCHAIN_LIB_DIR)
	@mkdir -p $(LLVM_TOOLCHAIN_INCLUDE_DIR)
	@mkdir -p $(LLVM_TOOLCHAIN_INCLUDE_DIR)/llvm
	@echo "Bootstrapping vendored LLVM toolchain into $(LLVM_TOOLCHAIN_DIR)"
	@host_inc="$$($(HOST_LLVM_CONFIG) --includedir)"; \
	host_ver="$$($(HOST_LLVM_CONFIG) --version)"; \
	host_cflags="$$($(HOST_LLVM_CONFIG) --cflags)"; \
	host_cflags="$$(printf '%s\n' "$$host_cflags" | sed -E 's/(^| )-I[^ ]+//g; s/^ +//; s/ +/ /g')"; \
	cp -R "$$host_inc/llvm-c" "$(LLVM_TOOLCHAIN_INCLUDE_DIR)/"; \
	cp -R "$$host_inc/llvm/Config" "$(LLVM_TOOLCHAIN_INCLUDE_DIR)/llvm/"; \
	printf '%s\n' "$$host_ver" > "$(LLVM_TOOLCHAIN_DIR)/VERSION"; \
	printf '%s\n' "$$host_cflags" > "$(LLVM_TOOLCHAIN_DIR)/CFLAGS"
	@for lib in $$($(HOST_LLVM_CONFIG) --libfiles core); do \
		resolved="$$(readlink -f "$$lib" 2>/dev/null || printf '%s' "$$lib")"; \
		cp -Lf "$$resolved" "$(LLVM_TOOLCHAIN_LIB_DIR)/"; \
		if [ "$$(basename "$$resolved")" != "$$(basename "$$lib")" ]; then \
			ln -sf "$$(basename "$$resolved")" "$(LLVM_TOOLCHAIN_LIB_DIR)/$$(basename "$$lib")"; \
		fi; \
	done
	@chmod +x $(LLVM_TOOLCHAIN_CONFIG)
	@touch $(LLVM_TOOLCHAIN_READY)
	@echo "Vendored LLVM toolchain ready. Future builds will prefer $(LLVM_TOOLCHAIN_CONFIG)."

fetch-llvm-toolchain:
	@chmod +x $(LLVM_CONFIG_WRAPPER)
	@chmod +x scripts/ensure_llvm_toolchain.sh
	@LLVM_TOOLCHAIN_DIR="$(LLVM_TOOLCHAIN_DIR)" \
	LLVM_TOOLCHAIN_ARCHIVE="$(LLVM_TOOLCHAIN_ARCHIVE)" \
	LLVM_TOOLCHAIN_URL="$(LLVM_TOOLCHAIN_URL)" \
	HOST_LLVM_CONFIG="$(HOST_LLVM_CONFIG)" \
	scripts/ensure_llvm_toolchain.sh

dist: all
	@rm -rf $(DIST_ROOT)
	@mkdir -p $(DIST_ROOT)
	@mkdir -p $(DIST_ROOT)/bin
	@mkdir -p $(DIST_ROOT)/lib
	@mkdir -p $(DIST_ROOT)/toolchains
	@cp -R $(TARGET) $(DIST_ROOT)/bin/
	@cp -R $(LIB_DIR) $(DIST_ROOT)/
	@cp -R $(LLVM_TOOLCHAIN_DIR) $(DIST_ROOT)/toolchains/
	@cp -R include $(DIST_ROOT)/
	@cp -R scripts $(DIST_ROOT)/
	@cp -R src $(DIST_ROOT)/
	@cp -R stdlib $(DIST_ROOT)/
	@cp -R examples $(DIST_ROOT)/
	@cp Makefile $(DIST_ROOT)/
	@tar -czf $(DIST_DIR)/qisc-portable.tar.gz -C $(DIST_DIR) qisc-portable
	@echo "Portable distribution written to $(DIST_DIR)/qisc-portable.tar.gz"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
ifeq ($(OS),Windows_NT)
	@if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
	@if exist $(BIN_DIR) rmdir /s /q $(BIN_DIR)
	@if exist $(LIB_DIR) rmdir /s /q $(LIB_DIR)
else
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(LIB_DIR) $(DIST_DIR)
endif

test:
	@echo "Running tests..."
	@./scripts/run_tests.sh

# Install (optional)
install: all
	cp $(TARGET) /usr/local/bin/
	cp $(RUNTIME_OBJ) /usr/local/lib/
