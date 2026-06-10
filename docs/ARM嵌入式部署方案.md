# 声纹锁 ARM 嵌入式部署方案

## 一、系统架构概览

### 1.1 整体模块划分

```
┌─────────────────────────────────────────────────────────┐
│                    声纹锁系统                            │
├───────────────┬─────────────────┬─────────────────────┤
│  外设控制模块  │    识别模块       │     存储模块         │
│  (Peripheral) │  (Recognition)   │     (Storage)       │
├───────────────┼─────────────────┼─────────────────────┤
│  INMP441     │  MFCC 特征提取    │  注册用户特征存储     │
│  I2S 采集    │  GMM 说话人模型   │  声纹数据库管理       │
│  音频缓存    │  SVM 分类器      │  模型参数持久化        │
│  按键/显示   │  端点检测 VAD    │                      │
└───────────────┴─────────────────┴─────────────────────┘
```

### 1.2 硬件平台选择

| 平台 | 优点 | 缺点 | 推荐度 |
|------|------|------|--------|
| Raspberry Pi 4B | 生态好，I2S支持完善，CMSIS-DSP可用 | 功耗较高，价格较高 | ⭐⭐⭐ |
| STM32MP157 | 低功耗，实时性好，CMSIS-DSP原生支持 | 开发复杂度高 | ⭐⭐⭐⭐ |
| 全志H3/H5 | 便宜，Linux完整支持 | 社区文档少 | ⭐⭐ |

**推荐：STM32MP157 或 Raspberry Pi 4B**

---

## 二、外设控制模块 — INMP441 麦克风接口

### 2.1 INMP441 硬件特性

| 参数 | 规格 |
|------|------|
| 接口 | I2S (Philips 标准) |
| 供电 | 3.3V |
| 位深 | 24-bit |
| 采样率 | 4kHz–48kHz (推荐16kHz) |
| SNR | 65dB |
| 功耗 | 3mA (active), 1μA (shutdown) |
| I2C地址 | 0x45 (固定) |

### 2.2 引脚连接 (对接 Raspberry Pi / STM32MP157)

**INMP441 → 开发板：**

| INMP441 Pin | 功能 | Raspberry Pi 40-pin | STM32MP157 |
|-------------|------|---------------------|------------|
| 1 (VDD) | 3.3V电源 | Pin 1 (3.3V) | 3.3V |
| 2 (GND) | 地 | Pin 6 (GND) | GND |
| 4 (WS) | 字选择(L/R时钟) | GPIO 19 (I2S WS) | I2S2_WS |
| 5 (DOUT) | 串行数据输出 | GPIO 21 (I2S SD) | I2S2_SD |
| 6 (SCK) | 串行时钟 | GPIO 18 (I2S CLK) | I2S2_CK |
| 7 (LRCL) | L/R通道选择 | 接地 (GND) | 接地 |
| 3 (INT) | 中断 | 可不接 | 可不接 |

**原理图示意：**
```
INMP441                    Raspberry Pi
┌─────────┐               ┌──────────────┐
│ VDD  1  │─── 3.3V ────→ │ Pin 1  (3.3V) │
│ GND  2  │─── GND  ────→ │ Pin 6  (GND)  │
│ INT  3  │               │              │
│ WS   4  │─── I2S WS ──→ │ GPIO 19      │
│ DOUT 5  │─── I2S SD ──→ │ GPIO 21      │
│ SCK  6  │─── I2S SCK ─→ │ GPIO 18      │
│ LRCL 7  │─── GND  ────→ │ GND          │
│ GND  8  │─── GND  ────→ │ Pin 6        │
└─────────┘               └──────────────┘
```

### 2.3 I2S 总线配置

**采样率配置：16kHz (16,000 samples/sec)**
**位深：24-bit (实际存储用 16-bit 或 32-bit float)**

```
I2S 时钟计算：
BCLK = 采样率 × 位数 × 通道数
     = 16000 × 24 × 2
     = 768,000 Hz (约 768kHz)

在 Raspberry Pi 上配置 /boot/config.txt:
dtparam=i2s=on
dtparam=audio=off

或使用设备树 overlay:
dtoverlay=audioinjector-addons
```

### 2.4 Linux ALSA 驱动配置

**创建 /etc/asound.conf：**
```conf
# INMP441 I2S Capture Device
pcm.inmp441_capture {
    type hw
    card 1                    # 声卡编号 (通过 arecord -l 确认)
    device 0
    format S24_LE             # 24-bit little-endian
    channels 2                # 立体声 (L=R 单声道数据)
    rate 16000               # 采样率
}

# 转换为单声道 16kHz float
pcm.mono_capture {
    type plug
    slave {
        pcm "inmp441_capture"
        channels 1
    }
    ttable {
        0 [0] = 1            # 左通道
        0 [1] = 1            # 右通道 (合并为单声道)
    }
}

pcm.!default {
    type plug
    slave {
        pcm "mono_capture"
        format FLOAT
    }
}
```

**验证录音：**
```bash
arecord -l                      # 列出capture设备
arecord -D plughw:1,0 -f S24_LE -r 16000 -c 2 test.wav  # 录制测试
aplay test.wav                  # 回放确认
```

### 2.5 多麦克风方案 (ReSpeaker Mic Array v2.0)

如果需要更好的声纹采集质量，推荐使用 **Seeed ReSpeaker Mic Array v2.0**：

| 特性 | 参数 |
|------|------|
| 麦克风数量 | 4 × INMP441 |
| 接口 | USB 或 I2S (40-pin GPIO) |
| 采样率 | 16kHz |
| 特色 | 内置 beamforming，去混响 |
| 价格 | 约 ¥200 |

**连接方式：**
```
ReSpeaker Mic Array (USB)
    └── USB Type-A (连接树莓派)
    └── 即插即用，ALSA 识别为 USB 声卡
```

---

## 三、存储模块设计

### 3.1 存储需求分析

| 数据类型 | 大小估算 | 说明 |
|----------|----------|------|
| 注册语音 raw | 3 × 3秒 × 16kHz × 2B = 288KB/人 | 3条语音 |
| MFCC 特征 | 约 500B/人 | 13维 × 300帧 |
| GMM 模型参数 | ~23KB (固定) | 所有用户共享UBM |
| SVM 模型 | ~10KB | 与用户数无关 |
| 用户特征向量 | ~1KB/人 | supervector |
| 注册用户上限 | 20-50人 | 存储空间 ~50-100KB |

**总计：约 100KB - 200KB**

### 3.2 存储介质选择

| 介质 | 容量 | 读写速度 | 优点 | 缺点 |
|------|------|----------|------|------|
| SD卡 | GB级 | 较快 | 容量大 | 掉电易损 |
| SPI Flash | 1-16MB | 中等 | 焊接稳定 | 需文件系统 |
| EEPROM | 4KB-1MB | 慢 | 可用I2C | 容量小 |
| eMMC | 4-32GB | 快 | 嵌入式常用 | 需系统支持 |

**推荐：SD卡 + SQLite 数据库**

### 3.3 数据库设计 (SQLite)

```sql
-- 用户表
CREATE TABLE users (
    user_id       INTEGER PRIMARY KEY AUTOINCREMENT,
    user_name     TEXT NOT NULL,
    enrolled_at   DATETIME DEFAULT CURRENT_TIMESTAMP,
    is_active     BOOLEAN DEFAULT 1
);

-- 声纹特征表
CREATE TABLE voiceprints (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER NOT NULL,
    feature_type  TEXT NOT NULL,           -- 'supervector' 或 'gmm_params'
    feature_data  BLOB NOT NULL,           -- 二进制特征数据
    mfcc_mean     BLOB,                   -- MFCC统计量 (可选)
    created_at    DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(user_id)
);

-- 系统模型表 (UBM/SVM等)
CREATE TABLE system_models (
    model_name    TEXT PRIMARY KEY,
    model_type    TEXT NOT NULL,           -- 'ubm', 'svm', 'config'
    model_data    BLOB NOT NULL,
    version       INTEGER DEFAULT 1,
    updated_at    DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 操作日志表
CREATE TABLE operation_log (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp     DATETIME DEFAULT CURRENT_TIMESTAMP,
    operation     TEXT NOT NULL,           -- 'enroll', 'verify', 'delete'
    user_id       INTEGER,
    result        TEXT NOT NULL,           -- 'success', 'failed'
    confidence    REAL,
    duration_ms   INTEGER
);
```

### 3.4 存储模块 C 语言接口

```c
// storage.h - 存储模块接口

#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_USERS         50
#define MAX_NAME_LEN      32

typedef struct {
    uint8_t supervector[512];  // GMM supervector
    float mfcc_mean[13];        // MFCC统计量
    uint32_t feature_len;
} voiceprint_t;

typedef struct {
    int user_id;
    char name[MAX_NAME_LEN];
    voiceprint_t voiceprint;
    bool is_active;
} user_info_t;

// 数据库初始化
int storage_init(const char* db_path);

// 用户管理
int storage_add_user(const char* name);
int storage_delete_user(int user_id);
int storage_get_user(int user_id, user_info_t* info);
int storage_list_users(user_info_t* users, int* count);

// 声纹特征
int storage_save_voiceprint(int user_id, const voiceprint_t* vp);
int storage_get_voiceprint(int user_id, voiceprint_t* vp);

// 系统模型
int storage_save_model(const char* model_name, const void* data, size_t len);
int storage_load_model(const char* model_name, void* data, size_t* len);

// 日志
int storage_log_operation(const char* op, int user_id, const char* result,
                         float confidence, int duration_ms);

#endif // STORAGE_H
```

### 3.5 存储模块文件布局

```
/sdcard/voiceprint/
├── voiceprint.db          # SQLite 数据库
├── models/
│   ├── ubm.gmm            # UBM 模型 (GMM参数)
│   └── svm.model          # SVM 模型
├── backup/
│   └── voiceprint_YYYYMMDD.db  # 每日备份
└── logs/
    └── operation.log      # 操作日志
```

---

## 四、识别模块设计 (MFCC + GMM/SVM)

### 4.1 识别流程

```
┌──────────────────────────────────────────────────────────────┐
│                      声纹识别流程                             │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  音频采集 (I2S) ──→ 预处理 ──→ VAD ──→ MFCC ──→ 识别 ──→ 输出 │
│      │              │           │       │        │          │
│      │              │           │       │        │          │
│      ▼              ▼           ▼       ▼        ▼          │
│   16kHz,         去噪/         端点    13维     GMM-        │
│   24-bit       预加重/         检测    特征    SVM          │
│   raw audio   归一化                   提取                │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 4.2 音频预处理

```c
// audio_preprocess.h

#define SAMPLE_RATE     16000
#define FRAME_LEN_MS    25       // 帧长 25ms
#define FRAME_SHIFT_MS  10       // 帧移 10ms
#define FRAME_LEN       (SAMPLE_RATE * FRAME_LEN_MS / 1000)  // 400 samples
#define FRAME_SHIFT     (SAMPLE_RATE * FRAME_SHIFT_MS / 1000) // 160 samples

typedef struct {
    float preemph;              // 预加重系数 (0.95-0.97)
    float frame_energy;         // 当前帧能量
} audio_preprocess_t;

// 预加重
void preemphasis(float* samples, int n, float coeff);

// 加窗 (Hamming)
void apply_hamming_window(float* frame, int n);

// 端点检测 (能量 + 过零率)
typedef struct {
    int speech_start;
    int speech_end;
    float energy_threshold;
    float zcr_threshold;
} vad_result_t;

vad_result_t detect_speech_endpoint(const float* samples, int n);
```

### 4.3 MFCC 特征提取 (CMSIS-DSP)

```c
// mfcc.h - 基于 CMSIS-DSP 的 MFCC 实现

#include "arm_math.h"

#define N_FFT              512
#define N_MEL_FILTERS      40
#define N_MFCC             13
#define N_MELS             40

typedef struct {
    // FFT 相关
    arm_rfft_fast_instance_f32 rfft;
    float fft_window[N_FFT];

    // Mel 滤波器组
    float mel_filterbank[N_MEL_FILTERS][N_FFT/2 + 1];

    // DCT 矩阵
    float dct_matrix[N_MEL_FILTERS][N_MFCC];

    // 配置
    uint32_t sample_rate;
    uint32_t n_fft;
    uint32_t n_mfcc;
    uint32_t n_mels;
} mfcc_config_t;

// 初始化 MFCC 配置
int mfcc_init(mfcc_config_t* config, uint32_t sample_rate);

// 提取一帧的 MFCC
int mfcc_extract_frame(mfcc_config_t* config,
                       const float* frame,
                       float* mfcc_out);

// 提取整段语音的 MFCC (返回多帧)
int mfcc_extract(mfcc_config_t* config,
                 const float* samples,
                 int n_samples,
                 float* mfcc_out,
                 int* n_frames);
```

### 4.4 GMM + SVM 声纹验证

```c
// voiceprint_verify.h

#define N_MFCC             13
#define N_GMM_COMPONENTS   16
#define UBM_COMPONENTS     64
#define SUPER_VECTOR_LEN    (N_MFCC * N_GMM_COMPONENTS)  // 208

typedef struct {
    // UBM (通用背景模型)
    float ubm_means[N_GMM_COMPONENTS][N_MFCC];
    float ubm_covariances[N_GMM_COMPONENTS][N_MFCC];
    float ubm_weights[N_GMM_COMPONENTS];

    // 当前注册的 GMM
    float gmm_means[N_GMM_COMPONENTS][N_MFCC];
    float gmm_covariances[N_GMM_COMPONENTS][N_MFCC];
    float gmm_weights[N_GMM_COMPONENTS];
} gmm_ubm_t;

typedef struct {
    // SVM 模型 (线性)
    float svm_weights[SUPER_VECTOR_LEN];
    float svm_bias;
    float svm_thresh;
} svm_model_t;

typedef struct {
    gmm_ubm_t* ubm;
    svm_model_t* svm;
    float threshold;
    float last_score;
} voiceprint_model_t;

// 提取 supervector
void extract_supervector(const float* mfcc,
                         int n_frames,
                         const gmm_ubm_t* ubm,
                         float* supervector_out);

// GMM 对数似然比验证
float gmm_verify(const float* mfcc,
                 int n_frames,
                 const gmm_ubm_t* ubm,
                 const gmm_ubm_t* speaker_gmm);

// SVM 验证
float svm_verify(const float* supervector,
                  const svm_model_t* svm);

// 综合验证 (GMM + SVM)
typedef enum {
    VERIFY_RESULT_UNKNOWN = 0,
    VERIFY_RESULT_MATCH,
    VERIFY_RESULT_NO_MATCH
} verify_result_t;

verify_result_t voiceprint_verify(voiceprint_model_t* model,
                                   const float* mfcc,
                                   int n_frames,
                                   float* score_out);
```

### 4.5 主程序流程

```c
// main.c - 主程序框架

#include "audio_capture.h"
#include "audio_preprocess.h"
#include "mfcc.h"
#include "voiceprint_verify.h"
#include "storage.h"
#include "display.h"

typedef enum {
    STATE_IDLE,
    STATE_ENROLLING,
    STATE_VERIFYING,
    STATE_UNLOCKED
} app_state_t;

int main(int argc, char* argv[]) {
    // 初始化
    printf("声纹锁系统初始化...\n");

    // 1. 初始化存储
    if (storage_init("/sdcard/voiceprint/voiceprint.db") != 0) {
        fprintf(stderr, "存储初始化失败\n");
        return -1;
    }

    // 2. 初始化音频采集 (I2S/INMP441)
    if (audio_capture_init(SAMPLE_RATE) != 0) {
        fprintf(stderr, "音频采集初始化失败\n");
        return -1;
    }

    // 3. 初始化 MFCC
    mfcc_config_t mfcc_config;
    mfcc_init(&mfcc_config, SAMPLE_RATE);

    // 4. 加载声纹模型
    voiceprint_model_t model;
    load_voiceprint_model(&model);

    // 5. 主循环
    app_state_t state = STATE_IDLE;
    while (1) {
        switch (state) {
            case STATE_IDLE:
                display_show_idle();
                if (button_pressed(BUTTON_ENROLL)) {
                    state = STATE_ENROLLING;
                } else if (button_pressed(BUTTON_VERIFY)) {
                    state = STATE_VERIFYING;
                }
                break;

            case STATE_ENROLLING:
                enroll_new_user(&model, &mfcc_config);
                state = STATE_IDLE;
                break;

            case STATE_VERIFYING:
                if (verify_user(&model, &mfcc_config) == VERIFY_RESULT_MATCH) {
                    state = STATE_UNLOCKED;
                    display_show_success();
                    unlock_door();
                    sleep(5);
                } else {
                    display_show_failed();
                    sleep(2);
                }
                state = STATE_IDLE;
                break;

            case STATE_UNLOCKED:
                // 已解锁状态
                if (button_pressed(BUTTON_LOCK)) {
                    lock_door();
                    state = STATE_IDLE;
                }
                break;
        }
        usleep(100000);  // 100ms
    }

    return 0;
}
```

---

## 五、完整项目文件结构

```
voiceprint_lock/
├── include/
│   ├── audio_capture.h    # 音频采集 (I2S/INMP441)
│   ├── audio_preprocess.h # 预处理 (去噪/预加重/VAD)
│   ├── mfcc.h            # MFCC 特征提取
│   ├── voiceprint_verify.h # GMM/SVM 声纹验证
│   ├── storage.h          # 存储模块接口
│   ├── storage_sqlite.h   # SQLite 实现
│   ├── display.h         # 显示/LED
│   └── buzzer.h          # 蜂鸣器
├── src/
│   ├── audio_capture.c    # ALSA I2S 采集
│   ├── audio_preprocess.c
│   ├── mfcc.c            # MFCC 实现
│   ├── mfcc_cmsis.c     # CMSIS-DSP 版本
│   ├── voiceprint_verify.c
│   ├── gmm_svm.c         # GMM/SVM 实现
│   ├── storage.c
│   ├── storage_sqlite.c
│   ├── display.c
│   └── main.c
├── models/                # 预训练的 UBM/SVM 模型
│   ├── ubm.bin
│   └── svm.bin
├── tests/
│   ├── test_audio.c
│   ├── test_mfcc.c
│   └── test_verify.c
├── Makefile
├── CMakeLists.txt
└── README.md
```

---

## 六、ARM 交叉编译环境搭建

### 6.1 工具链安装

```bash
# Raspberry Pi (ARMv7)
sudo apt install gcc-arm-linux-gnueabihf

# STM32MP157 (ARMv8 aarch64)
sudo apt install gcc-aarch64-linux-gnu

# 验证
arm-linux-gnueabihf-gcc --version
aarch64-linux-gnu-gcc --version
```

### 6.2 CMake 交叉编译配置

```cmake
# toolchain.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 目标树莓派
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

set(CMAKE_FIND_ROOT_PATH /usr/arm-linux-gnueabihf)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

### 6.3 编译步骤

```bash
# 创建 build 目录
mkdir build && cd build

# 配置 (使用 ARM 工具链)
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake ..

# 编译
make -j$(nproc)

# 部署到树莓派
scp voiceprint_lock pi@raspberrypi:/home/pi/voiceprint_lock/
```

---

## 七、部署检查清单

### 硬件检查
- [ ] INMP441 正确连接到 I2S 总线
- [ ] 电源 3.3V 正常
- [ ] 接地连接良好
- [ ] SD 卡已格式化和挂载

### 软件检查
- [ ] Linux I2S 内核模块已加载 (`lsmod | grep i2s`)
- [ ] ALSA 设备可识别 (`arecord -l`)
- [ ] SQLite 已安装 (`sqlite3 --version`)
- [ ] CMSIS-DSP 库已交叉编译

### 功能检查
- [ ] 可以录制音频 (`arecord test.wav`)
- [ ] 数据库可创建和读写
- [ ] MFCC 特征提取输出正确
- [ ] 声纹注册功能正常
- [ ] 声纹验证功能正常

### 性能检查
- [ ] 单次验证延迟 < 100ms
- [ ] CPU 占用 < 50%
- [ ] 内存占用 < 50MB

---

*文档版本：v1.0*
*生成时间：2026-06-12*
