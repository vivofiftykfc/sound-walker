cmake_minimum_required(VERSION 3.10)
project(voiceprint_lock C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ============================================================
# Options
# ============================================================
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(USE_CMSIS_DSP "Use CMSIS-DSP library" ON)
option(USE_ALSA "Use ALSA for audio capture" ON)

# ============================================================
# Directories
# ============================================================
set(INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(LIB_DIR ${CMAKE_SOURCE_DIR}/lib)
set(MODEL_DIR ${CMAKE_SOURCE_DIR}/models)

# Include directories
include_directories(
    ${INCLUDE_DIR}
    ${LIB_DIR}/include
    ${CMSIS_DSP_PATH}/include
)

# ============================================================
# Source Files
# ============================================================
set(AUDIO_SOURCES
    ${SRC_DIR}/audio/audio_capture.c
    ${SRC_DIR}/audio/audio_preprocess.c
)

set(DSP_SOURCES
    ${SRC_DIR}/dsp/mfcc.c
    ${SRC_DIR}/dsp/gmm_svm.c
)

set(STORAGE_SOURCES
    ${SRC_DIR}/storage/storage.c
    ${SRC_DIR}/storage/storage_sqlite.c
)

set(APP_SOURCES
    ${SRC_DIR}/main/main.c
)

# ============================================================
# Dependencies
# ============================================================
# CMSIS-DSP (source compilation)
if(USE_CMSIS_DSP)
    add_subdirectory(${CMSIS_DSP_PATH} cmsis_dsp_build)
endif()

# SQLite (amalgamation)
add_library(sqlite3 STATIC
    ${LIB_DIR}/sqlite/sqlite3.c
)
target_include_directories(sqlite3 PUBLIC
    ${LIB_DIR}/sqlite
)

# ALSA (system library, cross-compiled separately)
if(USE_ALSA)
    find_library(ALSA_LIB NAMES asound)
    if(NOT ALSA_LIB)
        message(WARNING "ALSA library not found. Audio capture disabled.")
        set(USE_ALSA OFF)
    endif()
endif()

# ============================================================
# Main Executable
# ============================================================
add_executable(voiceprint_lock
    ${APP_SOURCES}
    ${AUDIO_SOURCES}
    ${DSP_SOURCES}
    ${STORAGE_SOURCES}
)

target_link_libraries(voiceprint_lock
    sqlite3
    m
)

if(USE_CMSIS_DSP)
    target_link_libraries(voiceprint_lock arm_dsp)
endif()

if(USE_ALSA)
    target_link_libraries(voiceprint_lock ${ALSA_LIB})
endif()

# ============================================================
# Tests
# ============================================================
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================
# Installation
# ============================================================
install(TARGETS voiceprint_lock
    RUNTIME DESTINATION bin
)

install(DIRECTORY models/
    DESTINATION models
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/deploy/
    DESTINATION deploy
    FILES_MATCHING PATTERN "*.sh"
)
