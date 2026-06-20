# CMake Toolchain File for ARM Cross-Compilation
# Target: X210 (S5PV210, ARMv7) - Sourcery CodeBench Lite 2014.05
# Host: Linux x86_64

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Toolchain path
set(TOOLCHAIN_PATH /usr/local/arm/arm-2014.05)
set(CROSS_PREFIX ${TOOLCHAIN_PATH}/bin/arm-none-linux-gnueabi-)

set(CMAKE_C_COMPILER   ${CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++)
set(CMAKE_AR           ${CROSS_PREFIX}ar CMAKE_AR-NOTFOUND)
set(CMAKE_RANLIB       ${CROSS_PREFIX}ranlib CMAKE_RANLIB-NOTFOUND)
set(CMAKE_LINKER       ${CROSS_PREFIX}ld CMAKE_LINKER-NOTFOUND)
set(CMAKE_NM           ${CROSS_PREFIX}nm CMAKE_NM-NOTFOUND)
set(CMAKE_OBJCOPY      ${CROSS_PREFIX}objcopy CMAKE_OBJCOPY-NOTFOUND)
set(CMAKE_OBJDUMP      ${CROSS_PREFIX}objdump CMAKE_OBJDUMP-NOTFOUND)
set(CMAKE_STRIP        ${CROSS_PREFIX}strip CMAKE_STRIP-NOTFOUND)

# Sysroot and search paths
set(CMAKE_SYSROOT ${TOOLCHAIN_PATH}/arm-none-linux-gnueabi)
set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_PATH}/arm-none-linux-gnueabi)

# Search policies
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Build options for ARM
set(CMAKE_C_FLAGS "-march=armv7-a -mfpu=neon -mfloat-abi=hard -fPIC")
set(CMAKE_CXX_FLAGS "-march=armv7-a -mfpu=neon -mfloat-abi=hard -fPIC")

# Install paths
set(CMAKE_INSTALL_PREFIX /opt/voiceprint)
