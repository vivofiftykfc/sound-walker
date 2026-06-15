/**
 * test_wav.c - WAV文件测试
 * 使用 C 模块处理 WAV 文件
 */

#include "config.h"
#include "vad.h"
#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* 声明 */
int mfcc_init(void **ctx);
int mfcc_compute(void *ctx, const float *samples, int n_samples, float *mfcc_out);
int mfcc_extract_frames(void *ctx, const float *samples, int n_samples,
                       float *features_out, int *n_frames_out);
void mfcc_free(void *ctx);
void mfcc_print(const float *mfcc, int n_coeffs);

/* 读取 WAV 文件 */
int read_wav(const char *path, float **audio_out, int *n_samples_out) {
    FILE *f = fopen(path, "rb");
    if (!f) { LOG_ERROR("Cannot open %s", path); return -1; }
    
    /* 读取 WAV 头 */
    char riff[4], wave[4], fmt[4], data[4];
    fread(riff, 1, 4, f);
    fseek(f, 4, SEEK_CUR);  /* file_size */
    fread(wave, 1, 4, f);
    fread(fmt, 1, 4, f);
    fseek(f, 22, SEEK_CUR);  /* fmt_size, format */
    
    short channels, bits;
    int rate;
    fread(&channels, 2, 1, f);
    fread(&rate, 4, 1, f);
    fseek(f, 6, SEEK_CUR);  /* byte_rate, block_align */
    fread(&bits, 2, 1, f);
    fread(data, 1, 4, f);  /* "data" */
    
    if (strncmp(riff, "RIFF", 4) != 0 || strncmp(wave, "WAVE", 4) != 0) {
        LOG_ERROR("Not a WAV file: %s", path);
        fclose(f);
        return -1;
    }
    
    /* 读取数据 */
    int data_size;
    fread(&data_size, 4, 1, f);
    int n_samples = data_size / (bits / 8) / channels;
    
    short *pcm = malloc(data_size);
    fread(pcm, 1, data_size, f);
    fclose(f);
    
    /* 转为 float */
    float *audio = malloc(n_samples * sizeof(float));
    for (int i = 0; i < n_samples; i++) {
        audio[i] = pcm[i] / 32768.0f;
    }
    free(pcm);
    
    *audio_out = audio;
    *n_samples_out = n_samples;
    return 0;
}

/* 计算向量余弦相似度 */
float cosine_sim(const float *a, const float *b, int n) {
    float dot = 0, na = 0, nb = 0;
    for (int i = 0; i < n; i++) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    return dot / (sqrtf(na) * sqrtf(nb) + 1e-10f);
}

int main(int argc, char *argv[]) {
    const char *test_dir = "test_data/wav";
    
    LOG_SEPARATOR();
    LOG_INFO("Voiceprint Lock - WAV File Test");
    LOG_SEPARATOR();
    
    /* 初始化 */
    void *mfcc_ctx;
    if (mfcc_init(&mfcc_ctx) != 0) {
        LOG_ERROR("MFCC init failed");
        return 1;
    }
    
    VadContext *vad = vad_init(70);
    if (!vad) {
        LOG_ERROR("VAD init failed");
        return 1;
    }
    
    /* 读取说话人目录 */
    char spk_dirs[10][256];
    int n_spks = 0;
    
    DIR *d = opendir(test_dir);
    if (!d) {
        LOG_ERROR("Cannot open test directory: %s", test_dir);
        return 1;
    }
    
    struct dirent *entry;
    while ((entry = readdir(d)) && n_spks < 10) {
        if (entry->d_type == DT_DIR && entry->d_name[0] != '.') {
            strncpy(spk_dirs[n_spks], entry->d_name, 255);
            n_spks++;
        }
    }
    closedir(d);
    
    LOG_INFO("Found %d speakers", n_spks);
    
    /* 每人取前3个文件注册，后2个测试 */
    float enroll_features[3][500 * 12];  /* 3个文件，每文件最多500帧 */
    int enroll_frames[3] = {0, 0, 0};
    
    for (int s = 0; s < n_spks; s++) {
        LOG_INFO("\n=== Speaker %d/%d: %s ===", s+1, n_spks, spk_dirs[s]);
        
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", test_dir, spk_dirs[s]);
        
        /* 找 WAV 文件 */
        char wav_files[10][256];
        int n_files = 0;
        
        DIR *d2 = opendir(path);
        struct dirent *e2;
        while ((e2 = readdir(d2)) && n_files < 10) {
            if (strstr(e2->d_name, ".wav")) {
                snprintf(wav_files[n_files], 256, "%s/%s", path, e2->d_name);
                n_files++;
            }
        }
        closedir(d2);
        
        LOG_INFO("Found %d files", n_files);
        
        /* 注册前3个文件 */
        memset(enroll_features, 0, sizeof(enroll_features));
        memset(enroll_frames, 0, sizeof(enroll_frames));
        
        for (int i = 0; i < 3 && i < n_files; i++) {
            float *audio;
            int n_samples;
            
            if (read_wav(wav_files[i], &audio, &n_samples) != 0) continue;
            
            /* 提取 MFCC 帧 */
            int n_frames;
            mfcc_extract_frames(mfcc_ctx, audio, n_samples, enroll_features[i], &n_frames);
            enroll_frames[i] = n_frames;
            
            LOG_INFO("  Enroll %d: %d samples -> %d frames, first MFCC: %.2f", 
                     i, n_samples, n_frames, enroll_features[i][12]);
            
            free(audio);
        }
        
        /* 测试后2个文件 */
        for (int i = 3; i < 5 && i < n_files; i++) {
            float *audio;
            int n_samples;
            
            if (read_wav(wav_files[i], &audio, &n_samples) != 0) continue;
            
            int n_frames;
            float test_feat[500 * 12];
            mfcc_extract_frames(mfcc_ctx, audio, n_samples, test_feat, &n_frames);
            
            /* 计算与注册特征的余弦相似度 */
            float best_sim = -1;
            for (int j = 0; j < 3; j++) {
                if (enroll_frames[j] > 10) {
                    /* 取注册特征的均值 */
                    float mean_feat[12] = {0};
                    for (int f = 0; f < enroll_frames[j] && f < 100; f++) {
                        for (int k = 0; k < 12; k++) {
                            mean_feat[k] += enroll_features[j][f * 12 + k];
                        }
                    }
                    for (int k = 0; k < 12; k++) mean_feat[k] /= enroll_frames[j];
                    
                    /* 取测试特征的均值 */
                    float test_mean[12] = {0};
                    for (int f = 0; f < n_frames && f < 100; f++) {
                        for (int k = 0; k < 12; k++) {
                            test_mean[k] += test_feat[f * 12 + k];
                        }
                    }
                    for (int k = 0; k < 12; k++) test_mean[k] /= n_frames;
                    
                    float sim = cosine_sim(mean_feat, test_mean, 12);
                    if (sim > best_sim) best_sim = sim;
                }
            }
            
            LOG_INFO("  Test %d: %d frames, best_sim=%.4f %s", 
                     i, n_frames, best_sim, best_sim > 0.8 ? "✅ MATCH" : "❌ NO_MATCH");
            
            free(audio);
        }
    }
    
    vad_destroy(vad);
    mfcc_free(mfcc_ctx);
    
    LOG_SEPARATOR();
    LOG_INFO("Test complete!");
    LOG_SEPARATOR();
    
    return 0;
}
