/**
 * config.h - 系统配置头文件
 *
 * 支持两种模式:
 *   - PC_BUILD: 使用PC声卡/WAV文件测试
 *   - ARM_BUILD: 使用ALSA音频采集 (默认)
 */

#ifndef CONFIG_H
#define CONFIG_H

#define _GNU_SOURCE
#include <sys/time.h>
#include <time.h>

/* ============================================================
 * 编译模式配置
 * ============================================================ */
#ifdef PC_BUILD
    #define MODE_PC 1
    #define MODE_ARM 0
#else
    /* 默认ARM模式，交叉编译时使用 */
    #define MODE_PC 0
    #define MODE_ARM 1
#endif

/* ============================================================
 * 音频参数配置
 * ============================================================ */
#define SAMPLE_RATE         44100       /* 采样率 44.1kHz (USB麦克风原生) */
#define BITS_PER_SAMPLE     16          /* 位深 16bit */
#define CHANNELS            1           /* 单声道 */
#define BYTES_PER_SAMPLE    2           /* 16bit = 2字节 */

/* 帧参数 (25ms帧长, 10ms帧移) */
#define FRAME_LEN_MS        25
#define FRAME_SHIFT_MS      10
#define FRAME_LEN           (SAMPLE_RATE * FRAME_LEN_MS / 1000)    /* 400 samples */
#define FRAME_SHIFT         (SAMPLE_RATE * FRAME_SHIFT_MS / 1000) /* 160 samples */

/* ============================================================
 * MFCC 参数配置
 * ============================================================ */
#define N_FFT               2048         /* FFT长度 (2^11, >44100*25ms=1102) */
#define N_MELS              40          /* Mel滤波器数量 */
#define N_MFCC              13          /* MFCC系数数量 (含C0) */
#define N_MFCC_COEFFS       12          /* 实际使用的MFCC系数 (跳过C0) */

/* Mel频率范围 */
#define MEL_LOWER_FREQ      0.0f
#define MEL_UPPER_FREQ      (SAMPLE_RATE / 2.0f)

/* ============================================================
 * GMM/SVM 参数配置
 * ============================================================ */
#define GMM_N_COMPONENTS    64          /* 高斯混合分量数 */
#define GMM_DIM             N_MFCC_COEFFS  /* 特征维度 = 12 */

/* MAP自适应参数 */
#define MAP_RELEVANCE_FACTOR 16         /* tau, 通常10-20 */

/* UBM参数 (通用背景模型) */
#define UBM_MODEL_PATH      "models/ubm.bin"
#define SVM_MODEL_PATH      "models/svm.bin"

/* ============================================================
 * 声纹验证参数
 * ============================================================ */
#define N_ENROLL_UTTERANCES 3           /* 注册语音条数 */
#define N_TEST_MIN          3           /* 测试语音最少条数 */
#define VERIFICATION_THRESHOLD -15.0f   /* 默认阈值 (对数似然比) */

/* ============================================================
 * 存储配置
 * ============================================================ */
#define DATABASE_PATH        "/tmp/voiceprint.db"
#define MAX_USERS           50           /* 最大用户数 */
#define MAX_NAME_LEN        32           /* 用户名最大长度 */

/* ============================================================
 * ALSA 配置 (ARM模式)
 * ============================================================ */
/* USB麦克风(C-Media PCM2902)原生44100Hz */
#ifdef RPI_BUILD
    #define CFG_ALSA_DEVICE     "hw:3"
#else
    #define CFG_ALSA_DEVICE     "default"
#endif
#define ALSA_PERIOD_SIZE    160
#define ALSA_BUFFER_SIZE    640

/* ============================================================
 * 路径配置
 * ============================================================ */
#define CFG_DB_PATH         "/tmp/voiceprint.db"
#define CFG_MODEL_DIR       "models"

/* ============================================================
 * 调试配置
 * ============================================================ */
#define DEBUG_LEVEL_NONE     0
#define DEBUG_LEVEL_ERROR    1
#define DEBUG_LEVEL_WARN    2
#define DEBUG_LEVEL_INFO    3
#define DEBUG_LEVEL_DEBUG    4

/* 默认调试级别: INFO */
#ifndef DEBUG_LEVEL
    #define DEBUG_LEVEL DEBUG_LEVEL_INFO
#endif

/* 调试打印宏 */
#include <stdio.h>
#include <time.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEBUG_FILE_LINE() do { \
    fprintf(stderr, "[%s:%d] ", __FILE__, __LINE__); \
} while(0)

#define LOG_ERROR(fmt, ...) do { \
    if (DEBUG_LEVEL >= DEBUG_LEVEL_ERROR) { \
        DEBUG_FILE_LINE(); \
        fprintf(stderr, "\033[31m[ERROR]\033[0m " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

#define LOG_WARN(fmt, ...) do { \
    if (DEBUG_LEVEL >= DEBUG_LEVEL_WARN) { \
        DEBUG_FILE_LINE(); \
        fprintf(stderr, "\033[33m[WARN]\033[0m " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

#define LOG_INFO(fmt, ...) do { \
    if (DEBUG_LEVEL >= DEBUG_LEVEL_INFO) { \
        DEBUG_FILE_LINE(); \
        fprintf(stderr, "\033[32m[INFO]\033[0m " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

#define LOG_DEBUG(fmt, ...) do { \
    if (DEBUG_LEVEL >= DEBUG_LEVEL_DEBUG) { \
        DEBUG_FILE_LINE(); \
        fprintf(stderr, "\033[36m[DEBUG]\033[0m " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

/* 彩色打印辅助 */
#define LOG_PLAIN(fmt, ...) do { \
    fprintf(stderr, fmt "\n", ##__VA_ARGS__); \
} while(0)

/* 分隔线打印 */
#define LOG_SEPARATOR() do { \
    fprintf(stderr, "========================================\n"); \
} while(0)

/* ============================================================
 * 时间测量宏
 * ============================================================ */
typedef struct {
    struct timespec start;
    struct timespec end;
} VoiceTimer;

#define TIMER_START(t) do { \
    clock_gettime(CLOCK_MONOTONIC, &(t)->start); \
} while(0)

#define TIMER_STOP(t) do { \
    clock_gettime(CLOCK_MONOTONIC, &(t)->end); \
} while(0)

#define TIMER_ELAPSED_MS(t) (((t)->end.tv_sec - (t)->start.tv_sec) * 1000.0 + \
                             ((t)->end.tv_nsec - (t)->start.tv_nsec) / 1000000.0)

#define TIMER_PRINT(t, msg) do { \
    double __elapsed = TIMER_ELAPSED_MS(t); \
    LOG_INFO("%s: %.2f ms", msg, __elapsed); \
} while(0)

/* ============================================================
 * 错误码定义
 * ============================================================ */
typedef enum {
    ERR_OK = 0,
    ERR_INVALID_PARAM = -1,
    ERR_FILE_NOT_FOUND = -2,
    ERR_FILE_READ = -3,
    ERR_FILE_WRITE = -4,
    ERR_MEMORY = -5,
    ERR_AUDIO_DEVICE = -6,
    ERR_AUDIO_FORMAT = -7,
    ERR_MFCC_INIT = -8,
    ERR_MFCC_COMPUTE = -9,
    ERR_MODEL_LOAD = -10,
    ERR_MODEL_SAVE = -11,
    ERR_VERIFICATION = -12,
    ERR_DATABASE = -13,
    ERR_USER_NOT_FOUND = -14,
    ERR_USER_EXISTS = -15,
    ERR_ENROLLMENT = -16,
    ERR_VAD = -17,
    ERR_MISMATCH = -18,
} error_code_t;

/* 错误码转字符串 */
const char* err_str(error_code_t err);

#endif /* CONFIG_H */
