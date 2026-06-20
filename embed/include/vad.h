/**
 * vad.h - Voice Activity Detection (VAD) 头文件
 *
 * 基于能量阈值和过零率的端点检测算法
 */

#ifndef VAD_H
#define VAD_H

#include <stdint.h>

/* ============================================================
 * VAD 上下文类型 ( opaque pointer )
 * ============================================================ */
typedef struct VadContext VadContext;

/* ============================================================
 * 语音段结构体
 * ============================================================ */
typedef struct {
    int start;           /* 起始采样点 */
    int end;             /* 结束采样点 */
    int len;             /* 长度 (samples) */
    int frame_count;     /* 帧数 */
} VadSegment;

/* ============================================================
 * VAD 函数接口
 * ============================================================ */

/**
 * VAD 初始化
 *
 * 参数:
 *   sensitivity - 灵敏度 (0-100)
 *                 0: 低灵敏度，只有大声才能触发
 *                50: 中等灵敏度
 *               100: 高灵敏度，轻声也能检测
 *
 * 返回:
 *   VAD 上下文指针，失败返回 NULL
 */
VadContext* vad_init(int sensitivity);

/**
 * 单帧 VAD 检测
 *
 * 判断单帧是否为语音帧
 *
 * 参数:
 *   ctx   - VAD 上下文
 *   frame - 输入帧 (int16_t samples)
 *
 * 返回:
 *   1 - 语音帧
 *   0 - 非语音帧
 */
int vad_detect_frame(VadContext *ctx, const int16_t *frame);

/**
 * 获取当前 VAD 状态
 *
 * 参数:
 *   ctx - VAD 上下文
 *
 * 返回:
 *   1 - 当前在语音段内
 *   0 - 当前不在语音段内
 */
int vad_is_speaking(VadContext *ctx);

/**
 * 能量计算函数
 *
 * 计算一帧音频的短时能量
 *
 * 参数:
 *   frame - 输入帧 (int16_t samples)
 *   len   - 帧长
 *
 * 返回:
 *   能量值 (平方和)
 */
int vad_compute_energy(const int16_t *frame, int len);

/**
 * 过零率计算函数
 *
 * 计算一帧音频的短时过零率
 *
 * 参数:
 *   frame - 输入帧 (int16_t samples)
 *   len   - 帧长
 *
 * 返回:
 *   过零率 (零交叉次数)
 */
int vad_compute_zcr(const int16_t *frame, int len);

/**
 * 语音段检测函数
 *
 * 对输入音频数据进行端点检测，返回语音段列表
 *
 * 参数:
 *   ctx          - VAD 上下文
 *   audio_data   - 输入音频数据 (int16_t samples)
 *   audio_len    - 音频数据长度 (samples)
 *   segments     - 输出: 语音段数组
 *   max_segments - segments 数组最大容量
 *
 * 返回:
 *   检测到的语音段数量，失败返回负值
 */
int vad_detect_segments(VadContext *ctx,
                        const int16_t *audio_data,
                        int audio_len,
                        VadSegment *segments,
                        int max_segments);

/**
 * 重置 VAD 状态
 *
 * 清除语音段检测状态，但保留统计信息
 *
 * 参数:
 *   ctx - VAD 上下文
 */
void vad_reset(VadContext *ctx);

/**
 * 重置 VAD 完全初始化
 *
 * 清除所有状态和统计信息
 *
 * 参数:
 *   ctx - VAD 上下文
 */
void vad_reset_all(VadContext *ctx);

/**
 * 更新 VAD 灵敏度
 *
 * 参数:
 *   ctx        - VAD 上下文
 *   sensitivity - 新的灵敏度 (0-100)
 */
void vad_set_sensitivity(VadContext *ctx, int sensitivity);

/**
 * 获取 VAD 统计信息
 *
 * 参数:
 *   ctx           - VAD 上下文
 *   total_frames  - 输出: 总帧数
 *   speech_frames - 输出: 语音帧数
 *   speech_ratio  - 输出: 语音帧比例
 */
void vad_get_stats(VadContext *ctx, int *total_frames, int *speech_frames, float *speech_ratio);

/**
 * 打印语音段列表
 *
 * 参数:
 *   segments     - 语音段数组
 *   num_segments - 语音段数量
 */
void vad_print_segments(const VadSegment *segments, int num_segments);

/**
 * 打印 VAD 参数信息 (调试用)
 *
 * 参数:
 *   ctx - VAD 上下文
 */
void vad_print_params(VadContext *ctx);

/**
 * VAD 销毁
 *
 * 释放 VAD 上下文
 *
 * 参数:
 *   ctx - VAD 上下文
 */
void vad_destroy(VadContext *ctx);

#endif /* VAD_H */
