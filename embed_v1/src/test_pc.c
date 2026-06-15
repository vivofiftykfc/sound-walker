/**
 * test_pc.c - PC端测试程序
 *
 * 使用 LibriSpeech 数据集验证声纹识别算法
 * 测试流程：
 *   1. 加载数据集
 *   2. 训练/加载 UBM
 *   3. 声纹注册
 *   4. 声纹验证
 *   5. 输出准确率、EER等指标
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* 模块头文件 */
#include "audio_pcap.h"      /* 使用 audio_pcap.c 的接口 */
#include "mfcc.h"
#include "gmm_verify.h"
#include "storage_mock.h"
#include "vad.h"

/* ============================================================
 * 数据集路径配置
 * ============================================================ */
static const char *DEFAULT_DATA_DIR = "/home/miku/桌面/soundwalker/data/LibriSpeech/dev-clean";
static const char *DEFAULT_OUTPUT_DIR = "/home/miku/桌面/soundwalker/results";

/* ============================================================
 * 数据结构
 * ============================================================ */

/* 说话人信息 */
typedef struct {
    char id[32];
    char paths[20][256];  /* 最多20条语音 */
    int n_utterances;
} speaker_t;

/* 测试结果 */
typedef struct {
    int total_tests;
    int correct;
    int false_accept;
    int false_reject;
    float accuracy;
    float eer;
} test_result_t;

/* ============================================================
 * 工具函数
 * ============================================================ */

/* 递归搜索 FLAC 文件 */
static int find_flac_files(const char *dir, char (*paths)[256], int max, int *count) {
    DIR *d = opendir(dir);
    if (!d) return -1;

    struct dirent *entry;
    while ((entry = readdir(d)) && *count < max) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                find_flac_files(path, paths, max, count);
            }
        } else if (strstr(entry->d_name, ".flac")) {
            strncpy(paths[*count], path, 255);
            paths[*count][255] = '\0';
            (*count)++;
        }
    }

    closedir(d);
    return 0;
}

/* 加载说话人列表 */
static int load_speakers(const char *data_dir, speaker_t *speakers, int max_speakers) {
    DIR *d = opendir(data_dir);
    if (!d) {
        LOG_ERROR("Cannot open data directory: %s", data_dir);
        return -1;
    }

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(d)) && count < max_speakers) {
        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            char speaker_dir[512];
            snprintf(speaker_dir, sizeof(speaker_dir), "%s/%s", data_dir, entry->d_name);

            strncpy(speakers[count].id, entry->d_name, 31);
            speakers[count].id[31] = '\0';

            int file_count = 0;
            find_flac_files(speaker_dir, speakers[count].paths, 20, &file_count);
            speakers[count].n_utterances = file_count;

            if (file_count >= 8) {  /* 至少需要8条语音: 3注册 + 5测试 */
                LOG_DEBUG("Speaker %s: %d utterances", speakers[count].id, file_count);
                count++;
            }
        }
    }

    closedir(d);
    return count;
}

/* 打印彩色结果 */
static void print_result(int correct, int total, const char *speaker_id, int is_match) {
    if (is_match) {
        printf("\033[32m[PASS]\033[0m");
    } else {
        printf("\033[31m[FAIL]\033[0m");
    }
    printf(" %s: %d/%d correct\n", speaker_id, correct, total);
}

/* ============================================================
 * 测试流程
 * ============================================================ */

static int test_enrollment(void *mfcc_ctx, void *vad_ctx,
                          speaker_t *speaker, int n_enroll,
                          float *enroll_features) {
    LOG_INFO("Enrolling %d utterances for speaker %s", n_enroll, speaker->id);

    int total_frames = 0;

    for (int i = 0; i < n_enroll; i++) {
        void *audio_ctx;
        int ret = audio_init(&audio_ctx, speaker->paths[i]);
        if (ret != ERR_OK) {
            LOG_ERROR("Failed to open audio file: %s", speaker->paths[i]);
            continue;
        }

        float samples[FRAME_LEN];
        int n_speech_frames = 0;

        /* 读取并处理音频 */
        while (!audio_eof(audio_ctx)) {
            int read = audio_read_frame(audio_ctx, samples);
            if (read <= 0) break;

            /* VAD 检测 */
            int is_speech = vad_detect_frame(vad_ctx, samples);

            if (is_speech) {
                /* 提取 MFCC */
                float mfcc[N_MFCC_COEFFS];
                if (mfcc_compute(mfcc_ctx, samples, FRAME_LEN, mfcc) == ERR_OK) {
                    /* 复制到注册特征 */
                    for (int j = 0; j < N_MFCC_COEFFS; j++) {
                        enroll_features[n_speech_frames * N_MFCC_COEFFS + j] = mfcc[j];
                    }
                    n_speech_frames++;
                }
            }
        }

        total_frames += n_speech_frames;
        audio_close(audio_ctx);
    }

    LOG_INFO("  Enrolled %d speech frames", total_frames);
    return total_frames;
}

static float test_verification(void *mfcc_ctx, void *vad_ctx,
                              speaker_t *speaker, int start_idx) {
    LOG_DEBUG("Testing speaker %s", speaker->id);

    float best_score = -999.0f;
    int test_count = 0;

    for (int i = start_idx; i < speaker->n_utterances && i < start_idx + 5; i++) {
        void *audio_ctx;
        if (audio_init(&audio_ctx, speaker->paths[i]) != ERR_OK) continue;

        float samples[FRAME_LEN];
        int n_speech_frames = 0;
        float test_features[1000 * N_MFCC_COEFFS];  /* 最多1000帧 */

        while (!audio_eof(audio_ctx)) {
            int read = audio_read_frame(audio_ctx, samples);
            if (read <= 0) break;

            if (vad_detect_frame(vad_ctx, samples)) {
                float mfcc[N_MFCC_COEFFS];
                if (mfcc_compute(mfcc_ctx, samples, FRAME_LEN, mfcc) == ERR_OK) {
                    for (int j = 0; j < N_MFCC_COEFFS; j++) {
                        test_features[n_speech_frames * N_MFCC_COEFFS + j] = mfcc[j];
                    }
                    n_speech_frames++;
                }
            }
        }

        if (n_speech_frames > 10) {
            /* 简化的打分: 计算与注册特征的余弦相似度 */
            float score = 0.0f;
            /* TODO: 实现完整的 GMM 验证 */
            (void)score;  /* 暂时避免未使用警告 */

            test_count++;
        }

        audio_close(audio_ctx);
    }

    return best_score;
}

/* ============================================================
 * 主测试函数
 * ============================================================ */

static int run_tests(const char *data_dir, int max_speakers, int n_enroll) {
    LOG_SEPARATOR();
    LOG_INFO("Voiceprint Lock - PC Test Suite");
    LOG_INFO("Data directory: %s", data_dir);
    LOG_INFO("Max speakers: %d, Enrollment utterances: %d", max_speakers, n_enroll);
    LOG_SEPARATOR();

    /* 初始化模块 */
    void *mfcc_ctx;
    void *vad_ctx;

    LOG_INFO("Initializing MFCC...");
    if (mfcc_init(&mfcc_ctx) != ERR_OK) {
        LOG_ERROR("MFCC init failed");
        return -1;
    }

    LOG_INFO("Initializing VAD...");
    vad_ctx = vad_init(70);  /* 灵敏度 70% */
    if (!vad_ctx) {
        LOG_ERROR("VAD init failed");
        mfcc_free(mfcc_ctx);
        return -1;
    }

    /* 加载数据集 */
    speaker_t speakers[50];
    LOG_INFO("Loading speakers from %s...", data_dir);
    int n_speakers = load_speakers(data_dir, speakers, max_speakers);
    LOG_INFO("Loaded %d speakers", n_speakers);

    if (n_speakers < 2) {
        LOG_ERROR("Not enough speakers for testing");
        vad_destroy(vad_ctx);
        mfcc_free(mfcc_ctx);
        return -1;
    }

    /* 测试变量 */
    int total_tests = 0;
    int correct = 0;
    int false_accept = 0;
    int false_reject = 0;

    VoiceTimer timer;
    TIMER_START(timer);

    /* 对每个说话人进行注册和测试 */
    for (int i = 0; i < n_speakers; i++) {
        LOG_SEPARATOR();
        LOG_INFO("=== Testing Speaker %d/%d: %s ===",
                 i + 1, n_speakers, speakers[i].id);

        /* 注册 */
        float enroll_features[1000 * N_MFCC_COEFFS];
        int enroll_frames = test_enrollment(mfcc_ctx, vad_ctx,
                                           &speakers[i], n_enroll, enroll_features);

        if (enroll_frames < 50) {
            LOG_WARN("Not enough speech frames for speaker %s", speakers[i].id);
            continue;
        }

        /* 验证 (用未注册的语音) */
        float score = test_verification(mfcc_ctx, vad_ctx, &speakers[i], n_enroll);

        /* 计分 */
        total_tests++;
        if (score > VERIFICATION_THRESHOLD) {
            correct++;
            printf("\033[32m[PASS]\033[0m Speaker %s verified\n", speakers[i].id);
        } else {
            false_reject++;
            printf("\033[33m[REJECT]\033[0m Speaker %s rejected (score=%.2f)\n",
                   speakers[i].id, score);
        }

        /* 跨说话人测试 (负样本) */
        for (int j = 0; j < n_speakers && j < 3; j++) {
            if (j == i) continue;

            float impostor_score = test_verification(mfcc_ctx, vad_ctx,
                                                      &speakers[j], n_enroll);

            total_tests++;
            if (impostor_score > VERIFICATION_THRESHOLD) {
                false_accept++;
                printf("\033[31m[FAIL]\033[0m Impostor %s accepted as %s (score=%.2f)\n",
                       speakers[j].id, speakers[i].id, impostor_score);
            }
        }
    }

    TIMER_STOP(timer);

    /* 输出结果 */
    LOG_SEPARATOR();
    LOG_INFO("=== Test Results ===");
    LOG_INFO("Total tests: %d", total_tests);
    LOG_INFO("Correct: %d (%.1f%%)", correct,
             total_tests > 0 ? 100.0f * correct / total_tests : 0);
    LOG_INFO("False accepts: %d", false_accept);
    LOG_INFO("False rejects: %d", false_reject);

    float accuracy = total_tests > 0 ? (float)correct / total_tests : 0;
    float far = false_accept > 0 ? (float)false_accept / (false_accept + total_tests - correct - false_accept) : 0;
    float frr = false_reject > 0 ? (float)false_reject / (false_reject + correct) : 0;
    float eer = (far + frr) / 2.0f;

    LOG_INFO("FAR: %.2f%%", far * 100);
    LOG_INFO("FRR: %.2f%%", frr * 100);
    LOG_INFO("EER: %.2f%%", eer * 100);
    TIMER_PRINT(&timer, "Total test time");

    /* 保存结果 */
    char result_path[512];
    snprintf(result_path, sizeof(result_path), "%s/pc_test_results.txt", DEFAULT_OUTPUT_DIR);
    FILE *fp = fopen(result_path, "w");
    if (fp) {
        fprintf(fp, "Voiceprint Lock PC Test Results\n");
        fprintf(fp, "================================\n");
        fprintf(fp, "Total tests: %d\n", total_tests);
        fprintf(fp, "Correct: %d (%.1f%%)\n", correct, accuracy * 100);
        fprintf(fp, "False accepts: %d\n", false_accept);
        fprintf(fp, "False rejects: %d\n", false_reject);
        fprintf(fp, "FAR: %.2f%%\n", far * 100);
        fprintf(fp, "FRR: %.2f%%\n", frr * 100);
        fprintf(fp, "EER: %.2f%%\n", eer * 100);
        fclose(fp);
        LOG_INFO("Results saved to: %s", result_path);
    }

    /* 清理 */
    vad_destroy(vad_ctx);
    mfcc_free(mfcc_ctx);

    return (eer < 0.1f) ? 0 : 1;  /* EER < 10% 为通过 */
}

/* ============================================================
 * 命令行接口
 * ============================================================ */

static void print_usage(const char *prog) {
    printf("Voiceprint Lock - PC Test Program\n");
    printf("\n");
    printf("Usage: %s [options]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  --data-dir <path>   LibriSpeech data directory (default: %s)\n", DEFAULT_DATA_DIR);
    printf("  --output-dir <path> Output directory (default: %s)\n", DEFAULT_OUTPUT_DIR);
    printf("  --max-speakers <n>  Maximum speakers to test (default: 10)\n");
    printf("  --enroll <n>       Utterances for enrollment (default: 3)\n");
    printf("  --debug             Enable debug output\n");
    printf("  --test              Run tests (default)\n");
    printf("  --help              Show this help\n");
    printf("\n");
    printf("Example:\n");
    printf("  %s --data-dir /path/to/LibriSpeech/dev-clean --max-speakers 10\n", prog);
}

int main(int argc, char *argv[]) {
    const char *data_dir = DEFAULT_DATA_DIR;
    const char *output_dir = DEFAULT_OUTPUT_DIR;
    int max_speakers = 10;
    int n_enroll = N_ENROLL_UTTERANCES;
    int run_test = 0;

    /* 解析参数 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--test") == 0) {
            run_test = 1;
        } else if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (strcmp(argv[i], "--output-dir") == 0 && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (strcmp(argv[i], "--max-speakers") == 0 && i + 1 < argc) {
            max_speakers = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--enroll") == 0 && i + 1 < argc) {
            n_enroll = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--debug") == 0) {
            /* 设置调试级别会被 config.h 的宏使用 */
        }
    }

    if (run_test || argc == 1) {
        /* 确保输出目录存在 */
        mkdir(output_dir, 0755);

        /* 运行测试 */
        return run_tests(data_dir, max_speakers, n_enroll);
    }

    print_usage(argv[0]);
    return 0;
}
