# Voiceprint Lock - X210 (S5PV210) Cross-Compilation & Deployment

## Overview

This directory contains the complete cross-compilation infrastructure for deploying the voiceprint lock system from a PC (x86_64 Linux) to the X210 development board (S5PV210, ARMv7).

---

## Hardware & Toolchain

| Component | Details |
|-----------|---------|
| **Target** | X210 (Samsung S5PV210, ARMv7 @ 1GHz) |
| **Target OS** | Linux 2.6.35.7 (BusyBox-based minimal system) |
| **Target libc** | uClibc (no glibc) |
| **Host PC** | Linux x86_64 |
| **Cross-compiler** | `/usr/local/arm/arm-2014.05/bin/arm-none-linux-gnueabi-gcc` (Sourcery CodeBench Lite 2014.05, GCC 4.8.3) |
| **Target ABI** | `arm-none-linux-gnueabi` (soft-float) / `-mfloat-abi=hard` with NEON |

---

## Directory Structure

```
embed/
├── CMakeLists.txt                          # CMake build config (alternative to Makefile)
├── Makefile                                # Main build file
├── toolchain/
│   └── arm-none-linux-gnueabi.cmake        # CMake toolchain file
├── scripts/
│   ├── build.sh                            # Full build script (dependencies + app)
│   └── deploy.sh                           # Deployment script (scp/serial)
├── include/                                # Public headers
│   ├── audio_capture.h                     # ALSA audio capture
│   ├── audio_preprocess.h                  # Preprocessing (pre-emph, VAD, framing)
│   ├── mfcc.h                              # MFCC feature extraction
│   ├── voiceprint_verify.h                 # GMM/SVM verification
│   ├── storage.h                            # Storage interface
│   └── config.h                            # System configuration constants
├── src/
│   ├── audio/
│   │   ├── audio_capture.c                 # ALSA implementation
│   │   └── audio_preprocess.c              # Preprocessing implementation
│   ├── dsp/
│   │   ├── mfcc.c                          # MFCC using CMSIS-DSP
│   │   └── gmm_svm.c                       # GMM-UBM + SVM verification
│   ├── storage/
│   │   ├── storage.c                       # Storage interface
│   │   └── storage_sqlite.c                # SQLite implementation
│   └── main/
│       └── main.c                          # Application entry point
└── deploy/                                 # Deployment package (created by build)
```

---

## Dependencies to Cross-Compile

### 1. CMSIS-DSP

- **What**: ARM's optimized DSP library (FFT, MFCC, matrix ops)
- **Source**: https://github.com/ARM-software/CMSIS-DSP
- **Build**: Static library `libarm_dsp.a` (NEON-optimized)
- **Special considerations**:
  - Enable `ARM_MATH_NEON=ON` for Cortex-A NEON SIMD
  - Use `-march=armv7-a -mfpu=neon -mfloat-abi=hard`

### 2. SQLite (Amalgamation)

- **What**: Database for voiceprint templates and user management
- **Source**: https://www.sqlite.org/download.html (sqlite-autoconf-*.tar.gz)
- **Build**: Static library `libsqlite3.a`
- **Special considerations**:
  - Single C file (`sqlite3.c`) - easy to cross-compile
  - Disable extensions: `-DSQLITE_OMIT_LOAD_EXTENSION=1`
  - Thread-safe: `-DSQLITE_THREADSAFE=1`

### 3. ALSA-lib (Optional)

- **What**: Linux audio library for microphone capture
- **Source**: `libasound2-dev` cross-compiled OR static linking
- **Special considerations**:
  - X210 BusyBox system may not have full ALSA
  - Consider: compile ALSA as static library, or use direct I2S DMA
  - Alternative: bypass ALSA, use `/dev/snd/*` directly

### 4. NOT Needed on Target

- **No glibc** - using uClibc/BusyBox
- **No pthread** - use uClibc's pthreads or `arm-none-linux-gnueabi-gcc -static`
- **No dynamic linking** - everything static-linked

---

## Build Instructions

### Option 1: Using Makefile (Recommended)

```bash
cd /home/miku/桌面/soundwalker/embed

# Verify toolchain
make check

# Build everything (downloads deps, compiles, creates deploy tarball)
make deploy

# Or step-by-step:
make deps          # Download CMSIS-DSP, SQLite
make cmsis_dsp    # Build CMSIS-DSP
make sqlite        # Build SQLite
make               # Build main app
make deploy        # Create deploy tarball
```

### Option 2: Using CMake

```bash
cd /home/miku/桌面/soundwalker/embed
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain/arm-none-linux-gnueabi.cmake ..
make
```

### Option 3: Using build.sh Script

```bash
cd /home/miku/桌面/soundwalker/embed
./scripts/build.sh
```

### Build Output

- **Binary**: `build/voiceprint_lock` (~2-5MB static)
- **Deploy package**: `build/voiceprint_deploy.tar.gz`

---

## Deployment Methods

### Method 1: Network (SSH/SCP) - Recommended

```bash
# Edit deploy.sh with your X210 IP
TARGET_HOST="192.168.1.100"

# Deploy
cd /home/miku/桌面/soundwalker/embed
./scripts/deploy.sh -h 192.168.1.100 -k ~/.ssh/id_rsa
```

The deploy script will:
1. Create `/opt/voiceprint` on target
2. Copy and extract the deployment package
3. Create SD card directories
4. Set up init script for auto-start

### Method 2: Serial Port (lrzsz)

```bash
# Connect via serial (USB-UART @ 115200)
./scripts/deploy.sh --serial /dev/ttyUSB0

# On target console:
cd /opt/voiceprint
rz -y                    # Receive files
tar xzf voiceprint_deploy.tar.gz
chmod +x bin/voiceprint_lock
```

### Method 3: SD Card

```bash
# Mount SD card on PC
mount /dev/sdX1 /mnt/sd

# Copy files
cp -r build/deploy/* /mnt/sd/voiceprint/

# Unmount and insert in X210
umount /mnt/sd
```

---

## Running on X210

### Manual Start

```bash
cd /opt/voiceprint
./scripts/run.sh
```

### Auto-Start (if deployed with init script)

```bash
/etc/init.d/voiceprint start    # Start service
/etc/init.d/voiceprint stop     # Stop service
/etc/init.d/voiceprint restart  # Restart
```

### Expected Output

```
===========================================
   Voiceprint Lock v1.0
   X210 (S5PV210) Embedded System
===========================================

[*] Initializing storage...
[+] Storage ready: /sdcard/voiceprint/voiceprint.db
[*] Initializing audio capture...
[+] Audio capture ready: 16000 Hz, 1 channels
[*] Initializing preprocessor...
[+] Preprocessor ready
[*] Initializing MFCC extractor...
[+] MFCC ready: 13 coefficients, 512 FFT points
[*] Initializing voiceprint model...
[!] Voiceprint model initialization failed
[!] Note: Place ubm.bin and svm.bin in /sdcard/voiceprint/models/

===========================================
[*] System ready. Press Ctrl+C to exit.
===========================================
```

---

## Cross-Compilation Toolchain Configuration

### CMake Toolchain File

**File**: `toolchain/arm-none-linux-gnueabi.cmake`

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(TOOLCHAIN_PATH /usr/local/arm/arm-2014.05)
set(CROSS_PREFIX ${TOOLCHAIN_PATH}/bin/arm-none-linux-gnueabi-)
set(CMAKE_C_COMPILER   ${CROSS_PREFIX}gcc)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_C_FLAGS "-march=armv7-a -mfpu=neon -mfloat-abi=hard -fPIC")
```

### GCC Flags Explained

| Flag | Meaning |
|------|---------|
| `-march=armv7-a` | ARMv7-A architecture (Cortex-A8 on S5PV210) |
| `-mfpu=neon` | Enable NEON SIMD (for audio DSP) |
| `-mfloat-abi=hard` | Use hardware FPU (VFPv3) |
| `-fPIC` | Position-independent code (for shared libs) |
| `-static` | Static linking (no glibc needed on target) |

### Key Consideration for BusyBox/uClibc Target

1. **Static linking is essential** - no glibc on target
2. **Use `-Wl,--dynamic-linker` carefully** - may need to disable
3. **musl vs uClibc**: Some ARM toolchains use musl by default; check with `file` and `readelf -d`
4. **ALSA compatibility**: May need to compile ALSA as part of the build or use direct I2S register access

---

## Deployment Checklist

### Pre-Deployment

- [ ] Cross-compiler works: `arm-none-linux-gnueabi-gcc --version`
- [ ] Dependencies downloaded and built
- [ ] Binary created and stripped: `file build/voiceprint_lock`
- [ ] Binary verified with `readelf -d build/voiceprint_lock` (should show `STATIC` if static)
- [ ] Target IP address confirmed
- [ ] SSH connection works to target

### On Target (First Boot)

- [ ] SD card mounted at `/sdcard`
- [ ] `/opt/voiceprint` directory created
- [ ] Binary copied and executable
- [ ] `libsqlite3.a` linked (if not static)
- [ ] `libasound.so.2` available (if using ALSA)
- [ ] CMSIS-DSP lib available (if not static)
- [ ] `/sdcard/voiceprint/` directories created

### Verification Steps

```bash
# Check binary
file /opt/voiceprint/bin/voiceprint_lock
# Expected: ELF 32-bit ARM, statically linked

# Check libraries (if dynamic)
ldd /opt/voiceprint/bin/voiceprint_lock

# Run with verbose
/opt/voiceprint/bin/voiceprint_lock -v

# Check logs
cat /sdcard/voiceprint/logs/operation.log
```

---

## File Size Budget

| Component | Size (estimated) |
|-----------|------------------|
| voiceprint_lock binary (static) | 3-5 MB |
| SQLite (amalgamation) | ~1 MB (compiled) |
| CMSIS-DSP (full library) | ~2 MB |
| CMSIS-DSP (MFCC only) | ~500 KB |
| Models (UBM + SVM) | ~50 KB |
| Database | ~100 KB |
| **Total** | **~10 MB** |

---

## Troubleshooting

### "Cannot find arm-none-linux-gnueabi-gcc"

```bash
export PATH=/usr/local/arm/arm-2014.05/bin:$PATH
# Or use full path in Makefile
```

### "FATAL: kernel too old" when running binary

- Target kernel (2.6.35.7) is older than toolchain expects
- Solution: Use older toolchain or compile with `-mkernel=2.6.35`

### "Cannot open audio device"

- ALSA not configured on target
- Check: `arecord -l` on target
- May need to load ALSA kernel modules

### "SQLite database error"

- Check SD card is writable: `touch /sdcard/test`
- Check path exists: `mkdir -p /sdcard/voiceprint`

### Static linking fails with "cannot find -lc"

- uClibc doesn't have all symbols
- Try: `LDFLAGS="-static -muclibc -Os"`

---

## Next Steps After Deployment

1. **Train models on PC** (Python sklearn)
2. **Export models to binary format** for embedded target
3. **Place models on SD card**: `/sdcard/voiceprint/models/ubm.bin`, `svm.bin`
4. **Test enrollment**: Record voice samples, extract features, save to database
5. **Test verification**: Compare live voice against enrolled template

---

*Document version: 1.0*
*Generated: 2026-06-20*
