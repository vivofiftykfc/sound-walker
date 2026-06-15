# =====================================================================
# Makefile for Voiceprint Lock - X210 (S5PV210) Cross-Compilation
# =====================================================================
#
# Usage:
#   make              - Build everything
#   make clean        - Clean build artifacts
#   make deploy       - Build and create deployment package
#   make check        - Verify toolchain
#
# Environment:
#   TOOLCHAIN_PATH    - ARM toolchain path (default: /usr/local/arm/arm-2014.05)
#   STATIC            - Build static binary (default: yes)
#
# =====================================================================

# Toolchain
TOOLCHAIN_PATH ?= /usr/local/arm/arm-2014.05
CROSS_PREFIX   = $(TOOLCHAIN_PATH)/bin/arm-none-linux-gnueabi-
CC              = $(CROSS_PREFIX)gcc
AR              = $(CROSS_PREFIX)ar
STRIP           = $(CROSS_PREFIX)strip
NM              = $(CROSS_PREFIX)nm

# Build options
STATIC      ?= yes
OPTIMIZE    ?= -O3
ARCH_FLAGS  = -march=armv7-a -mfpu=neon -mfloat-abi=hard -fPIC
CFLAGS      = $(ARCH_FLAGS) $(OPTIMIZE) -Wall -Wextra -std=c99
LDFLAGS     = $(ARCH_FLAGS)

# Directories
BUILD_DIR   = build
DEPS_DIR    = deps
SRC_DIR     = src
INC_DIR     = include
LIB_DIR     = lib
MODEL_DIR   = models

# CMSIS-DSP
CMSIS_DSP_PATH  = $(DEPS_DIR)/CMSIS-DSP
CMSIS_DSP_INC   = $(CMSIS_DSP_PATH)/Include
CMSIS_DSP_LIB   = $(BUILD_DIR)/cmsis_dsp/libarm_dsp.a

# SQLite
SQLITE_INC  = $(DEPS_DIR)/sqlite
SQLITE_LIB  = $(BUILD_DIR)/sqlite/libsqlite3.a

# Output
OUTPUT      = voiceprint_lock
DEPLOY_DIR  = $(BUILD_DIR)/deploy
DEPLOY_TAR  = $(BUILD_DIR)/voiceprint_deploy.tar.gz

# Source files
AUDIO_SRC   = $(SRC_DIR)/audio/audio_capture.c \
              $(SRC_DIR)/audio/audio_preprocess.c

DSP_SRC     = $(SRC_DIR)/dsp/mfcc.c \
              $(SRC_DIR)/dsp/gmm_svm.c

STORAGE_SRC = $(SRC_DIR)/storage/storage.c \
              $(SRC_DIR)/storage/storage_sqlite.c

MAIN_SRC    = $(SRC_DIR)/main/main.c

# Object files
AUDIO_OBJ   = $(patsubst %.c,$(BUILD_DIR)/%.o,$(AUDIO_SRC))
DSP_OBJ     = $(patsubst %.c,$(BUILD_DIR)/%.o,$(DSP_SRC))
STORAGE_OBJ = $(patsubst %.c,$(BUILD_DIR)/%.o,$(STORAGE_SRC))
MAIN_OBJ    = $(patsubst %.c,$(BUILD_DIR)/%.o,$(MAIN_SRC))
ALL_OBJ     = $(AUDIO_OBJ) $(DSP_OBJ) $(STORAGE_OBJ) $(MAIN_OBJ)

# Targets
.PHONY: all clean deploy check deps cmsis_dsp sqlite

# =====================================================================
# Default target
# =====================================================================
all: check deps cmsis_dsp sqlite $(OUTPUT)

$(OUTPUT): $(ALL_OBJ) $(CMSIS_DSP_LIB) $(SQLITE_LIB)
	@echo "Linking $(OUTPUT)..."
	@mkdir -p $(BUILD_DIR)/main
	$(CC) $(LDFLAGS) $(if $(STATIC),-static) \
		$(ALL_OBJ) \
		$(CMSIS_DSP_LIB) \
		$(SQLITE_LIB) \
		-lm -lpthread \
		-o $@
	$(STRIP) -s $@
	@echo "Build complete: $(OUTPUT)"
	@ls -lh $@

# =====================================================================
# Dependencies
# =====================================================================
deps:
	@echo "=== Preparing dependencies ==="
	mkdir -p $(DEPS_DIR)
	mkdir -p $(BUILD_DIR)

	# CMSIS-DSP
	@if [ ! -d "$(CMSIS_DSP_PATH)" ]; then \
		echo "Cloning CMSIS-DSP..."; \
		git clone --depth 1 https://github.com/ARM-software/CMSIS-DSP.git $(CMSIS_DSP_PATH); \
	fi

	# SQLite amalgamation
	@if [ ! -f "$(DEPS_DIR)/sqlite/sqlite3.c" ]; then \
		echo "Downloading SQLite..."; \
		mkdir -p $(DEPS_DIR)/sqlite; \
		wget -q https://www.sqlite.org/2024/sqlite-autoconf-3460000.tar.gz -O /tmp/sqlite.tar.gz; \
		tar xzf /tmp/sqlite.tar.gz -C $(DEPS_DIR)/sqlite --strip-components=1 --one-top-level=sqlite; \
		rm /tmp/sqlite.tar.gz; \
	fi

# =====================================================================
# CMSIS-DSP
# =====================================================================
cmsis_dsp: $(CMSIS_DSP_LIB)

$(CMSIS_DSP_LIB): $(CMSIS_DSP_PATH)
	@echo "=== Building CMSIS-DSP ==="
	mkdir -p $(BUILD_DIR)/cmsis_dsp
	cd $(BUILD_DIR)/cmsis_dsp && \
	cmake $(CMSIS_DSP_PATH) \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_COMPILER=$(CC) \
		-DCMAKE_C_FLAGS="$(ARCH_FLAGS) -O3" \
		-DBUILD_SHARED_LIBS=OFF \
		-DARM_MATH_NEON=ON \
		-DARM_MATH_ARMV8M=OFF \
		-DARM_MATH_MP=ON && \
	make -j$$(nproc) && \
	mkdir -p $(BUILD_DIR)/cmsis_dsp/install && \
	make install
	@echo "CMSIS-DSP built"

# =====================================================================
# SQLite
# =====================================================================
sqlite: $(SQLITE_LIB)

$(SQLITE_LIB): $(DEPS_DIR)/sqlite/sqlite3.c
	@echo "=== Building SQLite ==="
	mkdir -p $(BUILD_DIR)/sqlite
	$(CC) $(ARCH_FLAGS) -O3 -c $(DEPS_DIR)/sqlite/sqlite3.c \
		-o $(BUILD_DIR)/sqlite/sqlite3.o \
		-DSQLITE_TEMP_STORE=2 \
		-DSQLITE_THREADSAFE=1 \
		-DSQLITE_ENABLE_FTS4 \
		-DSQLITE_ENABLE_FTS5 \
		-DSQLITE_OMIT_LOAD_EXTENSION
	$(AR) rcs $@ $(BUILD_DIR)/sqlite/sqlite3.o
	@echo "SQLite built"

# =====================================================================
# Object files
# =====================================================================
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(INC_DIR) -I$(CMSIS_DSP_INC) -I$(SQLITE_INC) -c $< -o $@

# =====================================================================
# Deploy
# =====================================================================
deploy: $(OUTPUT)
	@echo "=== Creating deployment package ==="
	mkdir -p $(DEPLOY_DIR)/{bin,models,config,scripts}

	# Copy binary
	cp $(OUTPUT) $(DEPLOY_DIR)/bin/

	# Copy models
	cp -r $(MODEL_DIR)/* $(DEPLOY_DIR)/models/ 2>/dev/null || true

	# Config
	@cat > $(DEPLOY_DIR)/config/voiceprint.conf << 'EOF'
{
    "sample_rate": 16000,
    "frame_len_ms": 25,
    "frame_shift_ms": 10,
    "n_mfcc": 13,
    "n_fft": 512,
    "n_mels": 40,
    "gmm_components": 16,
    "svm_threshold": 0.7,
    "db_path": "/sdcard/voiceprint/voiceprint.db",
    "log_path": "/sdcard/voiceprint/logs/operation.log"
}
EOF

	# Run script
	@cat > $(DEPLOY_DIR)/scripts/run.sh << 'SCRIPT'
#!/bin/sh
APP_DIR="/opt/voiceprint"
BINARY="$APP_DIR/bin/voiceprint_lock"
CONFIG="$APP_DIR/config/voiceprint.conf"

mkdir -p /sdcard/voiceprint/models
mkdir -p /sdcard/voiceprint/logs

exec $$BINARY -c $$CONFIG
SCRIPT
	chmod +x $(DEPLOY_DIR)/scripts/run.sh

	# Tarball
	cd $(BUILD_DIR) && tar czf $(DEPLOY_TAR) deploy/
	@echo "Deploy package: $(DEPLOY_TAR)"

# =====================================================================
# Utilities
# =====================================================================
check:
	@echo "=== Toolchain Check ==="
	@if [ -x "$(CC)" ]; then \
		echo "Compiler: $(CC)"; \
		$(CC) --version | head -1; \
	else \
		echo "ERROR: Compiler not found: $(CC)"; \
		exit 1; \
	fi

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(OUTPUT)

# =====================================================================
# Print build info
# =====================================================================
info:
	@echo "=== Build Configuration ==="
	@echo "TOOLCHAIN:  $(TOOLCHAIN_PATH)"
	@echo "PREFIX:     $(CROSS_PREFIX)"
	@echo "CC:         $(CC)"
	@echo "STATIC:     $(STATIC)"
	@echo "OUTPUT:     $(OUTPUT)"
	@echo "BUILD_DIR:  $(BUILD_DIR)"
	@echo "DEPS_DIR:   $(DEPS_DIR)"
