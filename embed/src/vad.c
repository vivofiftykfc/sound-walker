/**
 * vad.c - Voice Activity Detection (VAD) 模块
 *
 * 基于能量阈值和过零率的端点检测算法
 * 简单高效，适合嵌入式场景
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vad.h"
#include "config.h"

/* ============================================================
 * VAD 上下文结构体
 * ============================================================ */
struct VadContext {
    /* 能量阈值 */
    int energy_threshold;           /* 能量阈值 (可调灵敏度) */
    int min_energy_threshold;       /* 最小能量阈值下限 */

    /* 过零率阈值 */
    int zcr_threshold;              /* 过零率阈值 */
    int min_zcr_threshold;          /* 最小过零率阈值 */

    /* 帧参数 */
    int frame_len;                  /* 帧长 samples */
    int frame_shift;                /* 帧移 samples */

    /* 语音段检测参数 */
    int min_speech_frames;          /* 判定为语音的最少帧数 */
    int min_silence_frames;         /* 判定为静音的最少帧数 */
    int min_utterance_frames;       /* 最小话语长度帧数 */

    /* 状态 */
    int is_speaking;                /* 当前是否在语音段内 */
    int speech_frame_count;         /* 连续语音帧计数 */
    int silence_frame_count;        /* 连续静音帧计数 */

    /* 统计信息 */
    int total_frames;               /* 处理的总帧数 */
    int speech_frames;              /* 判定为语音的帧数 */
    int silence_frames;             /* 判定为静音的帧数 */
};

/* ============================================================
 * 能量计算函数
 *
 * 计算一帧音频的短时能量
 * 使用整数运算避免浮点开销
 *
 * 参数:
 *   frame   - 输入帧 (int16_t samples)
 *   len     - 帧长
 *
 * 返回:
 *   能量值 (平方和)
 * ============================================================ */
int vad_compute_energy(const int16_t *frame, int len)
{
    int energy = 0;
    int i;

    if (frame == NULL || len <= 0) {
        LOG_ERROR("Invalid frame or length: frame=%p, len=%d", frame, len);
        return 0;
    }

    for (i = 0; i < len; i++) {
        int sample = frame[i];
        energy += sample * sample;
    }

    return energy;
}

/* ============================================================
 * 过零率计算函数
 *
 * 计算一帧音频的短时过零率
 *
 * 参数:
 *   frame   - 输入帧 (int16_t samples)
 *   len     - 帧长
 *
 * 返回:
 *   过零率 (零交叉次数)
 * ============================================================ */
int vad_compute_zcr(const int16_t *frame, int len)
{
    int zcr = 0;
    int i;

    if (frame == NULL || len <= 0) {
        LOG_ERROR("Invalid frame or length: frame=%p, len=%d", frame, len);
        return 0;
    }

    for (i = 1; i < len; i++) {
        /* 检查相邻样本符号是否相反 */
        if ((frame[i-1] >= 0 && frame[i] < 0) ||
            (frame[i-1] < 0 && frame[i] >= 0)) {
            zcr++;
        }
    }

    return zcr;
}

/* ============================================================
 * 计算自适应能量阈值
 *
 * 根据当前能量计算自适应阈值
 * 使用能量均值的一定比例
 *
 * 参数:
 *   energy     - 当前帧能量
 *   threshold  - 基础阈值
 *   min_thresh - 最小阈值
 *
 * 返回:
 *   计算后的阈值
 * ============================================================ */
static int compute_adaptive_threshold(int energy, int threshold, int min_thresh)
{
    /* 简单自适应：如果能量很低，使用最小阈值 */
    if (energy < min_thresh) {
        return min_thresh;
    }
    return threshold;
}

/* ============================================================
 * VAD 初始化
 *
 * 创建并初始化 VAD 上下文
 *
 * 参数:
 *   sensitivity - 灵敏度 (0-100, 越高越灵敏)
 *                 0: 低灵敏度，只有大声才能触发
 *                50: 中等灵敏度
 *               100: 高灵敏度，轻声也能检测
 *
 * 返回:
 *   VAD 上下文指针，失败返回 NULL
 * ============================================================ */
VadContext* vad_init(int sensitivity)
{
    VadContext *ctx;

    /* 参数校验 */
    if (sensitivity < 0) sensitivity = 0;
    if (sensitivity > 100) sensitivity = 100;

    ctx = (VadContext *)malloc(sizeof(VadContext));
    if (ctx == NULL) {
        LOG_ERROR("Failed to allocate VAD context");
        return NULL;
    }

    /* 清零上下文 */
    memset(ctx, 0, sizeof(VadContext));

    /* 根据灵敏度设置阈值
     * 灵敏度映射:
     *   0   -> 高阈值 (低灵敏度)
     *   50  -> 中阈值
     *   100 -> 低阈值 (高灵敏度)
     */
    {
        /* 能量阈值计算 (基于16bit音频最大平方和)
         * 最大能量 = 32767^2 * 400 ≈ 4.29e11
         * 设置阈值范围: 1e7 (低灵敏) ~ 1e5 (高灵敏)
         */
        int64_t base_energy_thresh = 10000000LL;  /* 基准阈值 */
        int64_t range = 99000000LL;               /* 范围 */
        int64_t offset = (int64_t)sensitivity * range / 100;

        /* 灵敏度越高，阈值越低 */
        int64_t thresh = base_energy_thresh + offset; ctx->energy_threshold = (int)thresh;
        ctx->min_energy_threshold = 100000; /* 最小阈值下限 */
    }

    {
        /* 过零率阈值计算
         * 帧长400样本，清音噪声过零率约 20-50 次/帧
         * 语音过零率通常在 30-150 次/帧
         * 设置范围: 15 (高灵敏) ~ 40 (低灵敏)
         */
        int base_zcr_thresh = 40;
        int range = 25;
        int64_t offset = (int64_t)sensitivity * range / 100;

        ctx->zcr_threshold = base_zcr_thresh - offset;
        ctx->min_zcr_threshold = 15;
    }

    /* 帧参数 */
    ctx->frame_len = FRAME_LEN;
    ctx->frame_shift = FRAME_SHIFT;

    /* 语音段检测参数
     * 25ms帧长，10ms帧移 -> 帧率100帧/秒
     * 最小语音时长 150ms = 15帧
     * 最小静音时长 50ms = 5帧
     */
    ctx->min_speech_frames = 15;   /* 至少150ms语音才算有效 */
    ctx->min_silence_frames = 5;   /* 至少50ms静音才算结束 */
    ctx->min_utterance_frames = 10; /* 最小话语长度 100ms */

    /* 初始化状态 */
    ctx->is_speaking = 0;
    ctx->speech_frame_count = 0;
    ctx->silence_frame_count = 0;

    /* 统计信息 */
    ctx->total_frames = 0;
    ctx->speech_frames = 0;
    ctx->silence_frames = 0;

    LOG_INFO("VAD initialized: sensitivity=%d, energy_thresh=%d, zcr_thresh=%d",
             sensitivity, ctx->energy_threshold, ctx->zcr_threshold);
    LOG_DEBUG("  frame_len=%d, frame_shift=%d", ctx->frame_len, ctx->frame_shift);
    LOG_DEBUG("  min_speech_frames=%d, min_silence_frames=%d",
              ctx->min_speech_frames, ctx->min_silence_frames);

    return ctx;
}

/* ============================================================
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
 * ============================================================ */
int vad_detect_frame(VadContext *ctx, const int16_t *frame)
{
    int energy;
    int zcr;
    int is_speech;
    int adaptive_energy_thresh;

    if (ctx == NULL || frame == NULL) {
        LOG_ERROR("Invalid parameters: ctx=%p, frame=%p", ctx, frame);
        return 0;
    }

    ctx->total_frames++;

    /* 计算当前帧特征 */
    energy = vad_compute_energy(frame, ctx->frame_len);
    zcr = vad_compute_zcr(frame, ctx->frame_len);

    /* 计算自适应能量阈值 */
    adaptive_energy_thresh = compute_adaptive_threshold(
        energy, ctx->energy_threshold, ctx->min_energy_threshold);

    /* 语音检测逻辑:
     * 条件1: 能量超过阈值
     * 条件2: 过零率在合理范围 (排除纯低频噪声)
     *
     * 能量高 + 过零率合理 -> 语音
     * 能量低 -> 静音
     * 过零率异常高 -> 可能是噪声或音乐
     */
    {
        int energy_ok = (energy > adaptive_energy_thresh);

        /* 能量阈值检测 (主判据) */
        is_speech = energy_ok;

        /* 过零率作为辅助判据 (排除异常高过零率) */
        if (zcr > ctx->zcr_threshold * 4) {
            /* 过零率异常高，通常是噪声或非线性信号 */
            is_speech = 0;
        }

        /* 能量很低但过零率也低 -> 可能是静音 */
        if (energy < ctx->min_energy_threshold && zcr < ctx->min_zcr_threshold) {
            is_speech = 0;
        }
    }

    /* 更新统计 */
    if (is_speech) {
        ctx->speech_frames++;
        ctx->speech_frame_count++;
        ctx->silence_frame_count = 0;
    } else {
        ctx->silence_frames++;
        ctx->silence_frame_count++;
        ctx->speech_frame_count = 0;
    }

    LOG_DEBUG("Frame VAD: energy=%d(thresh=%d), zcr=%d(thresh=%d) -> %s",
              energy, adaptive_energy_thresh,
              zcr, ctx->zcr_threshold,
              is_speech ? "SPEECH" : "SILENCE");

    return is_speech;
}

/* ============================================================
 * 获取当前 VAD 状态
 *
 * 参数:
 *   ctx - VAD 上下文
 *
 * 返回:
 *   1 - 当前在语音段内
 *   0 - 当前不在语音段内
 * ============================================================ */
int vad_is_speaking(VadContext *ctx)
{
    if (ctx == NULL) {
        return 0;
    }
    return ctx->is_speaking;
}

/* ============================================================
 * 重置 VAD 状态
 *
 * 清除语音段检测状态，但保留统计信息
 *
 * 参数:
 *   ctx - VAD 上下文
 * ============================================================ */
void vad_reset(VadContext *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->is_speaking = 0;
    ctx->speech_frame_count = 0;
    ctx->silence_frame_count = 0;

    LOG_DEBUG("VAD state reset");
}

/* ============================================================
 * 重置 VAD 完全初始化
 *
 * 清除所有状态和统计信息
 *
 * 参数:
 *   ctx - VAD 上下文
 * ============================================================ */
void vad_reset_all(VadContext *ctx)
{
    if (ctx == NULL) {
        return;
    }

    vad_reset(ctx);

    ctx->total_frames = 0;
    ctx->speech_frames = 0;
    ctx->silence_frames = 0;

    LOG_INFO("VAD fully reset");
}

/* ============================================================
 * 更新 VAD 阈值
 *
 * 动态调整灵敏度
 *
 * 参数:
 *   ctx        - VAD 上下文
 *   sensitivity - 新的灵敏度 (0-100)
 * ============================================================ */
void vad_set_sensitivity(VadContext *ctx, int sensitivity)
{
    if (ctx == NULL) {
        return;
    }

    if (sensitivity < 0) sensitivity = 0;
    if (sensitivity > 100) sensitivity = 100;

    /* 重新计算阈值 */
    {
        int64_t base_energy_thresh = 10000000LL;
        int64_t range = 99000000LL;
        int64_t offset = (int64_t)sensitivity * range / 100;

        int64_t thresh = base_energy_thresh + offset; ctx->energy_threshold = (int)thresh;
    }

    {
        int base_zcr_thresh = 40;
        int range = 25;
        int64_t offset = (int64_t)sensitivity * range / 100;

        ctx->zcr_threshold = base_zcr_thresh - offset;
    }

    LOG_INFO("VAD sensitivity updated: %d, energy_thresh=%d, zcr_thresh=%d",
             sensitivity, ctx->energy_threshold, ctx->zcr_threshold);
}

/* ============================================================
 * 获取 VAD 统计信息
 *
 * 参数:
 *   ctx           - VAD 上下文
 *   total_frames  - 输出: 总帧数
 *   speech_frames - 输出: 语音帧数
 *   speech_ratio  - 输出: 语音帧比例
 * ============================================================ */
void vad_get_stats(VadContext *ctx, int *total_frames, int *speech_frames, float *speech_ratio)
{
    if (ctx == NULL) {
        return;
    }

    if (total_frames) *total_frames = ctx->total_frames;
    if (speech_frames) *speech_frames = ctx->speech_frames;
    if (speech_ratio) {
        *speech_ratio = (ctx->total_frames > 0)
                        ? (float)ctx->speech_frames / ctx->total_frames
                        : 0.0f;
    }
}

/* ============================================================
 * 语音段检测函数
 *
 * 对输入音频数据进行端点检测，返回语音段列表
 * 使用能量包络法进行端点检测
 *
 * 参数:
 *   ctx          - VAD 上下文
 *   audio_data   - 输入音频数据 (int16_t samples)
 *   audio_len    - 音频数据长度 (samples)
 *   segments     - 输出: 语音段数组 (VadSegment 类型)
 *   max_segments - segments 数组最大容量
 *
 * 返回:
 *   检测到的语音段数量，失败返回负值
 * ============================================================ */
int vad_detect_segments(VadContext *ctx,
                        const int16_t *audio_data,
                        int audio_len,
                        VadSegment *segments,
                        int max_segments)
{
    int n_frames;
    int frame_idx;
    int segment_count = 0;
    int speech_start = -1;
    int speech_end = -1;
    int consec_speech = 0;
    int consec_silence = 0;

    if (ctx == NULL || audio_data == NULL || segments == NULL) {
        LOG_ERROR("Invalid parameters: ctx=%p, audio_data=%p, segments=%p",
                  ctx, audio_data, segments);
        return ERR_INVALID_PARAM;
    }

    if (audio_len < ctx->frame_len) {
        LOG_ERROR("Audio too short: %d samples (min %d)", audio_len, ctx->frame_len);
        return ERR_INVALID_PARAM;
    }

    if (max_segments <= 0) {
        LOG_ERROR("Invalid max_segments: %d", max_segments);
        return ERR_INVALID_PARAM;
    }

    /* 计算可以处理的帧数 */
    n_frames = (audio_len - ctx->frame_len) / ctx->frame_shift + 1;

    LOG_INFO("Detecting segments: audio_len=%d, n_frames=%d", audio_len, n_frames);

    /* 重置VAD状态 */
    vad_reset_all(ctx);

    /* 逐帧处理 */
    for (frame_idx = 0; frame_idx < n_frames; frame_idx++) {
        int frame_start = frame_idx * ctx->frame_shift;
        const int16_t *frame = audio_data + frame_start;
        int is_speech = vad_detect_frame(ctx, frame);

        if (is_speech) {
            consec_speech++;
            consec_silence = 0;

            /* 开始新的语音段 */
            if (speech_start < 0) {
                speech_start = frame_start;
            }
            speech_end = frame_start + ctx->frame_len;

        } else {
            consec_silence++;
            consec_speech = 0;

            /* 语音段结束 */
            if (speech_start >= 0) {
                int segment_len = speech_end - speech_start;
                int segment_frames = segment_len / ctx->frame_shift;

                /* 检查是否满足最小话语长度 */
                if (segment_frames >= ctx->min_utterance_frames) {
                    if (segment_count < max_segments) {
                        segments[segment_count].start = speech_start;
                        segments[segment_count].end = speech_end;
                        segments[segment_count].len = segment_len;
                        segments[segment_count].frame_count = segment_frames;
                        segment_count++;

                        LOG_DEBUG("Speech segment %d: start=%d, end=%d, len=%d frames=%d",
                                  segment_count, speech_start, speech_end,
                                  segment_len, segment_frames);
                    } else {
                        LOG_WARN("Max segments reached: %d", max_segments);
                    }
                } else {
                    LOG_DEBUG("Segment too short (%d frames), discarded", segment_frames);
                }

                /* 重置状态 */
                speech_start = -1;
                speech_end = -1;
            }
        }
    }

    /* 处理最后一段语音 (如果音频以语音结束) */
    if (speech_start >= 0) {
        int segment_len = speech_end - speech_start;
        int segment_frames = segment_len / ctx->frame_shift;

        if (segment_frames >= ctx->min_utterance_frames) {
            if (segment_count < max_segments) {
                segments[segment_count].start = speech_start;
                segments[segment_count].end = speech_end;
                segments[segment_count].len = segment_len;
                segments[segment_count].frame_count = segment_frames;
                segment_count++;
            }
        }
    }

    LOG_INFO("Detection complete: %d segments found", segment_count);

    return segment_count;
}

/* ============================================================
 * 打印语音段列表
 *
 * 参数:
 *   segments     - 语音段数组
 *   num_segments - 语音段数量
 * ============================================================ */
void vad_print_segments(const VadSegment *segments, int num_segments)
{
    int i;

    if (segments == NULL || num_segments <= 0) {
        LOG_PLAIN("No speech segments");
        return;
    }

    LOG_PLAIN("Speech Segments (%d total):", num_segments);
    LOG_SEPARATOR();

    for (i = 0; i < num_segments; i++) {
        float duration_ms = (float)segments[i].len * 1000.0f / SAMPLE_RATE;
        LOG_PLAIN("  [%2d] start=%6d, end=%6d, len=%5d samples (%.1f ms), frames=%d",
                  i + 1,
                  segments[i].start,
                  segments[i].end,
                  segments[i].len,
                  duration_ms,
                  segments[i].frame_count);
    }

    /* 汇总统计 */
    {
        int total_samples = 0;
        float total_duration = 0.0f;

        for (i = 0; i < num_segments; i++) {
            total_samples += segments[i].len;
        }
        total_duration = (float)total_samples * 1000.0f / SAMPLE_RATE;

        LOG_PLAIN("  Total: %d samples, %.1f ms", total_samples, total_duration);
    }
}

/* ============================================================
 * VAD 销毁
 *
 * 释放 VAD 上下文
 *
 * 参数:
 *   ctx - VAD 上下文
 * ============================================================ */
void vad_destroy(VadContext *ctx)
{
    if (ctx != NULL) {
        LOG_DEBUG("Destroying VAD context");
        free(ctx);
    }
}

/* ============================================================
 * 获取 VAD 参数信息 (调试用)
 *
 * 参数:
 *   ctx - VAD 上下文
 * ============================================================ */
void vad_print_params(VadContext *ctx)
{
    if (ctx == NULL) {
        return;
    }

    LOG_PLAIN("VAD Parameters:");
    LOG_SEPARATOR();
    LOG_PLAIN("  Energy threshold:     %d", ctx->energy_threshold);
    LOG_PLAIN("  Min energy threshold:  %d", ctx->min_energy_threshold);
    LOG_PLAIN("  ZCR threshold:         %d", ctx->zcr_threshold);
    LOG_PLAIN("  Min ZCR threshold:     %d", ctx->min_zcr_threshold);
    LOG_PLAIN("  Frame length:         %d samples (%g ms)",
              ctx->frame_len, (float)ctx->frame_len * 1000.0f / SAMPLE_RATE);
    LOG_PLAIN("  Frame shift:          %d samples (%g ms)",
              ctx->frame_shift, (float)ctx->frame_shift * 1000.0f / SAMPLE_RATE);
    LOG_PLAIN("  Min speech frames:    %d (%g ms)",
              ctx->min_speech_frames,
              (float)ctx->min_speech_frames * ctx->frame_shift * 1000.0f / SAMPLE_RATE);
    LOG_PLAIN("  Min silence frames:   %d (%g ms)",
              ctx->min_silence_frames,
              (float)ctx->min_silence_frames * ctx->frame_shift * 1000.0f / SAMPLE_RATE);
    LOG_PLAIN("  Min utterance frames: %d (%g ms)",
              ctx->min_utterance_frames,
              (float)ctx->min_utterance_frames * ctx->frame_shift * 1000.0f / SAMPLE_RATE);
}
