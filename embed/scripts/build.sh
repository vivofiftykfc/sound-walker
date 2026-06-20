#!/bin/bash
# =====================================================================
# Cross-Compilation Build Script for X210 (S5PV210)
# =====================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
DEPS_DIR="$PROJECT_ROOT/deps"

TOOLCHAIN_PATH=/usr/local/arm/arm-2014.05
CROSS_PREFIX=${TOOLCHAIN_PATH}/bin/arm-none-linux-gnueabi-

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# =====================================================================
# Step 0: Check toolchain
# =====================================================================
check_toolchain() {
    log_info "Checking cross-compilation toolchain..."

    if [ ! -x "${CROSS_PREFIX}gcc" ]; then
        log_error "Cross-compiler not found: ${CROSS_PREFIX}gcc"
        log_error "Please install ARM toolchain to $TOOLCHAIN_PATH"
        exit 1
    fi

    log_info "Toolchain found: ${CROSS_PREFIX}gcc"
    ${CROSS_PREFIX}gcc --version | head -1
}

# =====================================================================
# Step 1: Download and prepare dependencies
# =====================================================================
prepare_dependencies() {
    log_info "Preparing dependencies..."

    mkdir -p "$DEPS_DIR"
    cd "$DEPS_DIR"

    # 1.1 CMSIS-DSP
    if [ ! -d "CMSIS-DSP" ]; then
        log_info "Downloading CMSIS-DSP..."
        git clone --depth 1 https://github.com/ARM-software/CMSIS-DSP.git CMSIS-DSP
    fi

    # 1.2 SQLite (amalgamation - single file, easy to cross-compile)
    if [ ! -f "sqlite/sqlite3.c" ]; then
        log_info "Downloading SQLite amalgamation..."
        mkdir -p sqlite
        wget -q https://www.sqlite.org/2024/sqlite-autoconf-3460000.tar.gz -O sqlite.tar.gz
        tar xzf sqlite.tar.gz -C sqlite --strip-components=1 --one-top-level=sqlite
        rm sqlite.tar.gz
    fi

    # 1.3 ALSA-lib (cross-compiled, or static for BusyBox)
    # Note: X210 BusyBox system may not have glibc, consider static linking

    log_info "Dependencies prepared in $DEPS_DIR"
}

# =====================================================================
# Step 2: Build CMSIS-DSP
# =====================================================================
build_cmsis_dsp() {
    log_info "Building CMSIS-DSP..."

    CMSIS_DSP_PATH="$DEPS_DIR/CMSIS-DSP"

    mkdir -p "$BUILD_DIR/cmsis_dsp"
    cd "$BUILD_DIR/cmsis_dsp"

    cmake "$CMSIS_DSP_PATH" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Linux \
        -DCMAKE_C_COMPILER="${CROSS_PREFIX}gcc" \
        -DCMAKE_C_FLAGS="-march=armv7-a -mfpu=neon -mfloat-abi=hard" \
        -DBUILD_SHARED_LIBS=OFF \
        -DARM_MATH_NEON=ON \
        -DARM_MATH_ARMV8M=OFF

    make -j$(nproc)
    make install

    log_info "CMSIS-DSP built and installed to $BUILD_DIR/install"
}

# =====================================================================
# Step 3: Build SQLite
# =====================================================================
build_sqlite() {
    log_info "Building SQLite..."

    mkdir -p "$BUILD_DIR/sqlite"
    cd "$BUILD_DIR/sqlite"

    "${CROSS_PREFIX}gcc" \
        -march=armv7-a \
        -mfpu=neon \
        -mfloat-abi=hard \
        -fPIC \
        -O3 \
        -DSQLITE_TEMP_STORE=2 \
        -DSQLITE_THREADSAFE=1 \
        -DSQLITE_ENABLE_FTS4 \
        -DSQLITE_ENABLE_FTS5 \
        -DSQLITE_ENABLE_STAT4 \
        -DSQLITE_OMIT_LOAD_EXTENSION \
        -shared \
        "$DEPS_DIR/sqlite/sqlite3.c" \
        -o libsqlite3.so

    # Static library
    "${CROSS_PREFIX}ar" rcs libsqlite3.a "$DEPS_DIR/sqlite/sqlite3.c"

    log_info "SQLite built"
}

# =====================================================================
# Step 4: Build main application
# =====================================================================
build_app() {
    log_info "Building voiceprint_lock application..."

    mkdir -p "$BUILD_DIR/app"
    cd "$BUILD_DIR/app"

    CMSIS_DSP_INSTALL="$BUILD_DIR/cmsis_dsp/install"
    SQLITE_LIB="$BUILD_DIR/sqlite"

    # Build object files
    "${CROSS_PREFIX}gcc" \
        -c "${PROJECT_ROOT}/src/audio/audio_capture.c" \
        -o audio_capture.o \
        -I"${PROJECT_ROOT}/include" \
        -I"${PROJECT_ROOT}/include/sys" \
        -I"${CMSIS_DSP_INSTALL}/include" \
        -march=armv7-a -mfpu=neon -mfloat-abi=hard -fPIC -O3

    "${CROSS_PREFIX}gcc" \
        -c "${PROJECT_ROOT}/src/audio/audio_preprocess.c" \
        -o audio_preprocess.o \
        -I"${PROJECT_ROOT}/include" \
        -I"${CMSIS_DSP_INSTALL}/include" \
        -march=armv7-a -mfpu=neon -mfloat-abi=hard -fPIC -O3

    "${CROSS_PREFIX}gcc" \
        -c "${PROJECT_ROOT}/src/dsp/mfcc.c" \
        -o mfcc.o \
        -I"${PROJECT_ROOT}/include" \
        -I"${CMSIS_DSP_INSTALL}/include" \
        -march=armv7-a -mfpu=neon -mfloat-abi=hard -fPIC -O3

    "${CROSS_PREFIX}gcc" \
        -c "${PROJECT_ROOT}/src/dsp/gmm_svm.c" \
        -o gmm_svm.o \
        -I"${PROJECT_ROOT}/include" \
        -I"${CMSIS_DSP_INSTALL}/include" \
        -march=armv7-a -mfpu=neon -mfloat-abi=hard -fPIC -O3

    "${CROSS_PREFIX}gcc" \
        -c "${PROJECT_ROOT}/src/storage/storage.c" \
        -o storage.o \
        -I"${PROJECT_ROOT}/include" \
        -march=armv7-a -mfpu=neon -mfloat-abi=hard -fPIC -O3

    "${CROSS_PREFIX}gcc" \
        -c "${PROJECT_ROOT}/src/storage/storage_sqlite.c" \
        -o storage_sqlite.o \
        -I"${PROJECT_ROOT}/include" \
        -I"${DEPS_DIR}/sqlite" \
        -march=armv7-a -mfpu=neon -mfloat-abi=hard -fPIC -O3

    "${CROSS_PREFIX}gcc" \
        -c "${PROJECT_ROOT}/src/main/main.c" \
        -o main.o \
        -I"${PROJECT_ROOT}/include" \
        -I"${CMSIS_DSP_INSTALL}/include" \
        -march=armv7-a -mfpu=neon -mfloat-abi=hard -fPIC -O3

    # Link CMSIS-DSP
    CMSIS_DSP_LIB="$CMSIS_DSP_INSTALL/lib/arm/armv7-a-neon-hard-float/libarm_dsp.a"

    # Static link everything (no glibc on target BusyBox system)
    "${CROSS_PREFIX}gcc" \
        -static \
        -march=armv7-a -mfpu=neon -mfloat-abi=hard \
        -o voiceprint_lock \
        main.o \
        audio_capture.o \
        audio_preprocess.o \
        mfcc.o \
        gmm_svm.o \
        storage.o \
        storage_sqlite.o \
        "$CMSIS_DSP_LIB" \
        -lm \
        -lpthread

    # Strip symbols to reduce size
    "${CROSS_PREFIX}strip" -s voiceprint_lock

    log_info "Application built: $BUILD_DIR/app/voiceprint_lock"
}

# =====================================================================
# Step 5: Create deployment package
# =====================================================================
create_deploy_package() {
    log_info "Creating deployment package..."

    DEPLOY_DIR="$BUILD_DIR/deploy"
    mkdir -p "$DEPLOY_DIR"/{bin,models,config,scripts}

    # Copy binary
    cp "$BUILD_DIR/app/voiceprint_lock" "$DEPLOY_DIR/bin/"

    # Copy models (placeholders)
    cp -r "${PROJECT_ROOT}/models/"* "$DEPLOY_DIR/models/" 2>/dev/null || true

    # Create config
    cat > "$DEPLOY_DIR/config/voiceprint.conf" << 'EOF'
# Voiceprint Lock Configuration
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

    # Create run script
    cat > "$DEPLOY_DIR/scripts/run.sh" << 'EOF'
#!/bin/sh
# Voiceprint Lock startup script

MOUNT_POINT="/sdcard"
APP_DIR="/opt/voiceprint"
BINARY="$APP_DIR/bin/voiceprint_lock"
CONFIG="$APP_DIR/config/voiceprint.conf"

# Check if SD card is mounted
if [ ! -d "$MOUNT_POINT" ]; then
    echo "Error: SD card not mounted at $MOUNT_POINT"
    exit 1
fi

# Create directories
mkdir -p "$MOUNT_POINT/voiceprint/models"
mkdir -p "$MOUNT_POINT/voiceprint/logs"

# Copy models to SD if needed
if [ ! -f "$MOUNT_POINT/voiceprint/models/ubm.bin" ]; then
    cp -r "$APP_DIR/models/"* "$MOUNT_POINT/voiceprint/models/" 2>/dev/null || true
fi

# Run application
exec "$BINARY" -c "$CONFIG"
EOF
    chmod +x "$DEPLOY_DIR/scripts/run.sh"

    # Create tarball
    cd "$BUILD_DIR"
    tar czf voiceprint_deploy.tar.gz deploy/

    log_info "Deployment package created: $BUILD_DIR/voiceprint_deploy.tar.gz"
}

# =====================================================================
# Main
# =====================================================================
main() {
    log_info "=== Voiceprint Lock Cross-Compilation ==="
    log_info "Project: $PROJECT_ROOT"
    log_info "Build: $BUILD_DIR"

    check_toolchain
    prepare_dependencies
    build_sqlite
    build_app
    create_deploy_package

    log_info "=== Build Complete ==="
    log_info "Binary: $BUILD_DIR/app/voiceprint_lock"
    log_info "Deploy: $BUILD_DIR/voiceprint_deploy.tar.gz"
}

main "$@"
