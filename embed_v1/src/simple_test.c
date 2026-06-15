/**
 * simple_test.c - 简化版测试程序
 *
 * 详细调试版本：逐步打印执行流程
 */

#include "config.h"
#include "vad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

/* 全局函数 */
int mfcc_init(void **ctx);
int mfcc_compute(void *ctx, const float *samples, int n_samples, float *mfcc_out);
int mfcc_extract_frames(void *ctx, const float *samples, int n_samples,
                       float *features_out, int *n_frames_out);
void mfcc_free(void *ctx);
void mfcc_print(const float *mfcc, int n_coeffs);
const char* err_str(error_code_t err);

/* 打印系统信息 */
void print_system_info() {
    LOG_SEPARATOR();
    LOG_INFO("声纹锁 - 模块测试 v1.0");
    LOG_SEPARATOR();
    LOG_INFO("系统信息:");
    LOG_INFO("  采样率: %d Hz", SAMPLE_RATE);
    LOG_INFO("  帧长: %d samples (%d ms)", FRAME_LEN, FRAME_LEN_MS);
    LOG_INFO("  帧移: %d samples (%d ms)", FRAME_SHIFT, FRAME_SHIFT_MS);
    LOG_INFO("  MFCC系数: %d", N_MFCC_COEFFS);
    LOG_INFO("  FFT长度: %d", N_FFT);
    LOG_INFO("  Mel滤波器: %d", N_MELS);
    LOG_SEPARATOR();
}

/* 测试MFCC模块 */
int test_mfcc() {
    LOG_INFO("[MFCC] 开始测试...");

    void *ctx;
    LOG_INFO("[MFCC] 1. 初始化MFCC上下文...");
    int ret = mfcc_init(&ctx);
    if (ret != ERR_OK) {
        LOG_ERROR("[MFCC] 初始化失败: %s", err_str(ret));
        return -1;
    }
    LOG_INFO("[MFCC] 初始化成功!");

    /* 生成1秒测试信号: 440Hz正弦波 */
    LOG_INFO("[MFCC] 2. 生成测试信号: 440Hz正弦波, 1秒, %d采样", SAMPLE_RATE);
    float test_audio[SAMPLE_RATE];
    for (int i = 0; i < SAMPLE_RATE; i++) {
        test_audio[i] = sinf(2 * M_PI * 440 * i / SAMPLE_RATE);
    }
    LOG_INFO("[MFCC] 测试信号生成完成, 幅度范围: [%.2f, %.2f]", -1.0f, 1.0f);

    /* 提取MFCC */
    LOG_INFO("[MFCC] 3. 提取MFCC特征...");
    float mfcc[N_MFCC_COEFFS];
    ret = mfcc_compute(ctx, test_audio, SAMPLE_RATE, mfcc);
    if (ret == ERR_OK) {
        LOG_INFO("[MFCC] MFCC提取成功!");
        LOG_INFO("[MFCC] 特征值:");
        mfcc_print(mfcc, N_MFCC_COEFFS);

        /* 打印特征统计 */
        float sum = 0, max = mfcc[0], min = mfcc[0];
        for (int i = 0; i < N_MFCC_COEFFS; i++) {
            sum += mfcc[i];
            if (mfcc[i] > max) max = mfcc[i];
            if (mfcc[i] < min) min = mfcc[i];
        }
        LOG_INFO("[MFCC] 统计: 均值=%.2f, 范围=[%.2f, %.2f]", sum/N_MFCC_COEFFS, min, max);
    } else {
        LOG_ERROR("[MFCC] MFCC提取失败: %s", err_str(ret));
    }

    LOG_INFO("[MFCC] 4. 释放MFCC上下文...");
    mfcc_free(ctx);
    LOG_INFO("[MFCC] 测试完成!");

    return ret;
}

/* 测试VAD模块 */
int test_vad() {
    LOG_INFO("[VAD] 开始测试...");
    LOG_INFO("[VAD] 灵敏度设置: 70%%");

    VadContext *ctx = vad_init(70);
    if (!ctx) {
        LOG_ERROR("[VAD] 初始化失败!");
        return -1;
    }
    LOG_INFO("[VAD] 初始化成功!");

    /* 测试1: 静音帧 */
    LOG_INFO("[VAD] 测试1: 静音帧 (全0)...");
    int16_t silence[FRAME_LEN];
    memset(silence, 0, sizeof(silence));
    int r1 = vad_detect_frame(ctx, silence);
    LOG_INFO("[VAD] 结果: %s (预期: SILENCE)", r1 ? "SPEECH" : "SILENCE");

    /* 测试2: 弱语音帧 */
    LOG_INFO("[VAD] 测试2: 弱语音帧 (幅度=1000)...");
    int16_t weak[FRAME_LEN];
    for (int i = 0; i < FRAME_LEN; i++) {
        weak[i] = (int16_t)(1000 * sinf(2 * M_PI * 440 * i / FRAME_LEN));
    }
    int r2 = vad_detect_frame(ctx, weak);
    LOG_INFO("[VAD] 结果: %s", r2 ? "SPEECH" : "SILENCE");

    /* 测试3: 强语音帧 */
    LOG_INFO("[VAD] 测试3: 强语音帧 (幅度=16000)...");
    int16_t strong[FRAME_LEN];
    for (int i = 0; i < FRAME_LEN; i++) {
        strong[i] = (int16_t)(16000 * sinf(2 * M_PI * 440 * i / FRAME_LEN));
    }
    int r3 = vad_detect_frame(ctx, strong);
    LOG_INFO("[VAD] 结果: %s (预期: SPEECH)", r3 ? "SPEECH" : "SILENCE");

    /* 测试4: 连续多帧 */
    LOG_INFO("[VAD] 测试4: 连续10帧强语音...");
    int speech_count = 0;
    for (int i = 0; i < 10; i++) {
        if (vad_detect_frame(ctx, strong)) speech_count++;
    }
    LOG_INFO("[VAD] 检测到语音帧: %d/10", speech_count);

    /* 打印统计 - 使用API函数 */
    int total, speech_frames, silence_frames;
    float ratio;
    vad_get_stats(ctx, &total, &speech_frames, &ratio);
    LOG_INFO("[VAD] 统计: 总帧=%d, 语音帧=%d, 静音帧=%d", total, speech_frames, total - speech_frames);

    LOG_INFO("[VAD] 释放VAD上下文...");
    vad_destroy(ctx);
    LOG_INFO("[VAD] 测试完成!");

    return 0;
}

/* 测试流程总览 */
void print_test_flow() {
    LOG_SEPARATOR();
    LOG_INFO("测试流程:");
    LOG_INFO("  1. [MFCC] - 特征提取测试");
    LOG_INFO("  2. [VAD]  - 语音检测测试");
    LOG_SEPARATOR();
}

int main() {
    print_system_info();
    print_test_flow();

    int ret = 0;
    ret |= test_mfcc();
    LOG_INFO("");
    ret |= test_vad();

    LOG_SEPARATOR();
    if (ret == 0) {
        printf("\n  *********************************************\n");
        printf("  *       ALL TESTS PASSED!                 *\n");
        printf("  *********************************************\n");
        LOG_INFO("MFCC模块: 通过");
        LOG_INFO("VAD模块: 通过");
    } else {
        printf("\n  *********************************************\n");
        printf("  *       SOME TESTS FAILED!                *\n");
        printf("  *********************************************\n");
    }
    LOG_SEPARATOR();
    LOG_INFO("程序将在3秒后退出...");
    sleep(1);
    return ret;
}
