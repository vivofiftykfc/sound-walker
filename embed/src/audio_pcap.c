/**
 * audio_pcap.c - 音频采集模块 (PC模式: WAV文件)
 *
 * 接口与 audio_alsa.c 完全兼容
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* WAV文件头结构 */
typedef struct {
    char riff[4];           /* "RIFF" */
    uint32_t file_size;
    char wave[4];          /* "WAVE" */
    char fmt[4];           /* "fmt " */
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];          /* "data" */
    uint32_t data_size;
} wav_header_t;

/* 音频上下文 */
typedef struct {
    FILE *file;
    wav_header_t header;
    int eof;
    uint32_t total_samples;
    uint32_t current_sample;
} audio_context_t;

/* ============================================================
 * 公共接口
 * ============================================================ */

int audio_init(void **ctx, const char *source) {
    audio_context_t *ac = calloc(1, sizeof(audio_context_t));
    if (!ac) {
        LOG_ERROR("Failed to allocate audio context");
        return ERR_MEMORY;
    }

    /* 如果传入的是WAV文件路径 */
    if (source) {
        ac->file = fopen(source, "rb");
        if (!ac->file) {
            LOG_ERROR("Failed to open WAV file: %s", source);
            free(ac);
            return ERR_FILE_NOT_FOUND;
        }

        /* 读取WAV头 */
        size_t read = fread(&ac->header, 1, sizeof(wav_header_t), ac->file);
        if (read != sizeof(wav_header_t)) {
            LOG_ERROR("Failed to read WAV header");
            fclose(ac->file);
            free(ac);
            return ERR_AUDIO_FORMAT;
        }

        /* 验证WAV格式 */
        if (strncmp(ac->header.riff, "RIFF", 4) != 0 ||
            strncmp(ac->header.wave, "WAVE", 4) != 0) {
            LOG_ERROR("Not a valid WAV file");
            fclose(ac->file);
            free(ac);
            return ERR_AUDIO_FORMAT;
        }

        /* 验证音频格式 */
        if (ac->header.num_channels != CHANNELS ||
            ac->header.sample_rate != SAMPLE_RATE ||
            ac->header.bits_per_sample != BITS_PER_SAMPLE) {
            LOG_WARN("WAV format mismatch: ch=%d rate=%d bits=%d, expected ch=%d rate=%d bits=%d",
                ac->header.num_channels, ac->header.sample_rate, ac->header.bits_per_sample,
                CHANNELS, SAMPLE_RATE, BITS_PER_SAMPLE);
        }

        ac->total_samples = ac->header.data_size / BYTES_PER_SAMPLE;
        ac->current_sample = 0;
        ac->eof = 0;

        LOG_INFO("Opened WAV file: %s", source);
        LOG_INFO("  Channels: %d", ac->header.num_channels);
        LOG_INFO("  Sample rate: %d", ac->header.sample_rate);
        LOG_INFO("  Bits per sample: %d", ac->header.bits_per_sample);
        LOG_INFO("  Total samples: %u (%.2f sec)", ac->total_samples,
                 (float)ac->total_samples / SAMPLE_RATE);
    } else {
        /* 无输入，返回空上下文 */
        LOG_INFO("Audio context created (no file loaded)");
    }

    *ctx = ac;
    return ERR_OK;
}

int audio_read_samples(void *ctx, float *samples, int n_samples) {
    audio_context_t *ac = (audio_context_t *)ctx;

    if (!ac || !samples || n_samples <= 0) {
        return ERR_INVALID_PARAM;
    }

    if (!ac->file || ac->eof) {
        return 0;
    }

    /* 读取原始PCM数据 */
    int16_t *pcm = (int16_t *)malloc(n_samples * sizeof(int16_t));
    if (!pcm) {
        return ERR_MEMORY;
    }

    size_t read = fread(pcm, sizeof(int16_t), n_samples, ac->file);
    ac->current_sample += read;

    if (read < (size_t)n_samples) {
        ac->eof = 1;
    }

    /* 转换为float (归一化到 [-1, 1]) */
    for (int i = 0; i < (int)read; i++) {
        samples[i] = pcm[i] / 32768.0f;
    }

    /* 清零剩余部分 */
    for (size_t i = read; i < (size_t)n_samples; i++) {
        samples[i] = 0.0f;
    }

    free(pcm);
    return (int)read;
}

int audio_read_frame(void *ctx, float *frame) {
    return audio_read_samples(ctx, frame, FRAME_LEN);
}

int audio_seek(void *ctx, float position_sec) {
    audio_context_t *ac = (audio_context_t *)ctx;

    if (!ac || !ac->file) {
        return ERR_INVALID_PARAM;
    }

    uint32_t sample_pos = (uint32_t)(position_sec * SAMPLE_RATE);
    uint32_t byte_pos = sizeof(wav_header_t) + sample_pos * BYTES_PER_SAMPLE;

    if (fseek(ac->file, byte_pos, SEEK_SET) != 0) {
        return ERR_FILE_READ;
    }

    ac->current_sample = sample_pos;
    ac->eof = 0;
    return ERR_OK;
}

int audio_eof(void *ctx) {
    audio_context_t *ac = (audio_context_t *)ctx;
    return ac ? ac->eof : 1;
}

int audio_close(void *ctx) {
    audio_context_t *ac = (audio_context_t *)ctx;
    if (!ac) return ERR_OK;

    if (ac->file) {
        fclose(ac->file);
    }
    free(ac);
    return ERR_OK;
}

int audio_get_duration(void *ctx) {
    audio_context_t *ac = (audio_context_t *)ctx;
    if (!ac) return 0;
    return (int)(ac->total_samples / SAMPLE_RATE);
}

int audio_get_position(void *ctx) {
    audio_context_t *ac = (audio_context_t *)ctx;
    if (!ac) return 0;
    return (int)(ac->current_sample / SAMPLE_RATE);
}
