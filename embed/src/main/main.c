
#include "config.h"
#include "audio_capture.h"
#include "voiceprint_verify.h"
#include "storage.h"
#include "tts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <math.h>

#define ENROLL_DURATION_SEC     3
#define VERIFY_DURATION_SEC     2
#define MAX_AUDIO_SAMPLES       (5 * SAMPLE_RATE)
#define READ_CHUNK              441


#define C_RESET   "\033[0m"
#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_BLUE    "\033[34m"
#define C_CYAN    "\033[36m"
#define C_BOLD    "\033[1m"

static volatile int g_running = 1;

static void signal_handler(int sig) {
    (void)sig; printf("\n"); g_running = 0;
}


static int capture_audio(audio_capture_t* cap, float* buffer, int sec) {
    int target = sec * SAMPLE_RATE;
    audio_capture_stop(cap); usleep(50000);
    int r = 3; while (r-- > 0) { if (audio_capture_start(cap) == 0) break; usleep(200000); }
    if (r < 0) return -1;

    int bar_w = 30, total = 0;
    printf("  " C_BOLD );
    while (total < target && g_running) {
        int n = audio_capture_read(cap, buffer + total, READ_CHUNK);
        if (n > 0) {
            total += n;
            int pct = total * 100 / target;
            int pos = total * bar_w / target;
            printf("\r  \033[36m[");
            for (int i = 0; i < bar_w; i++) putchar(i < pos ? '#' : ' ');
            printf("] %3d%%\033[0m", pct); fflush(stdout);
        } else if (n == 0) { usleep(1000); }
        else { audio_capture_stop(cap); usleep(20000); audio_capture_start(cap); }
    }
    audio_capture_stop(cap);
    printf("\r  \033[36m[##############################] 100%%\033[0m\n");
    return total;
}


static int check_energy(const float* audio, int n) {
    float peak = 0;
    for (int i = 0; i < n; i++) { float a = fabsf(audio[i]); if (a > peak) peak = a; }
    return peak >= 0.005f;
}


static void wait_enter(void) {
    printf("  " C_YELLOW "Press ENTER when ready..." C_RESET);
    fflush(stdout); char tmp[4]; fgets(tmp, sizeof(tmp), stdin);
}


static void draw_score(float score, float thresh) {
    int w = 25;
    int sp = (int)(score * w); if (sp < 0) sp = 0; if (sp > w) sp = w;
    int tp = (int)(thresh * w); if (tp < 0) tp = 0; if (tp > w) tp = w;

    printf("         ");
    for (int i = 0; i < w; i++) {
        if (i == tp) printf(C_RED "│" C_RESET);
        else if (i < sp) printf("▓");
        else printf("░");
    }
    if (tp >= w) printf(C_RED "│" C_RESET);
    printf("\n");
    printf("         score=%.3f", score);
    if (score >= thresh) printf(C_GREEN " ≥" C_RESET);
    else printf(C_RED " <" C_RESET);
    printf(" threshold=%.2f\n", thresh);
}

/* ═══════════════════════════════════════
 * ENROLL
 * ═══════════════════════════════════════ */
static int do_enroll(voiceprint_model_t* model, audio_capture_t* cap,
                     storage_t* db, tts_t* tts) {
    printf("\n" C_CYAN "┌─────────────────────────────────────┐\n");
    printf("│       " C_BOLD "Enroll New User" C_CYAN "               │\n");
    printf("└─────────────────────────────────────┘\n" C_RESET);

    if (tts) tts_speak(tts, "请说出你要注册的语音");
    wait_enter();

    float* audio = malloc(MAX_AUDIO_SAMPLES * sizeof(float));
    if (!audio) return -1;

    int n = capture_audio(cap, audio, ENROLL_DURATION_SEC);
    if (n < 0) {
        printf(C_RED "  ✗ Recording failed. Check mic.\n" C_RESET);
        if (tts) tts_speak(tts, "录音失败，请检查麦克风");
        free(audio); return -1;
    }
    if (n < SAMPLE_RATE * 0.3) {
        printf(C_RED "  ✗ Too short.\n" C_RESET);
        free(audio); return -1;
    }
    if (!check_energy(audio, n)) {
        printf(C_YELLOW "  ⚠ Too quiet. Speak up!\n" C_RESET);
        free(audio); return -1;
    }

    printf("  Processing" C_YELLOW "..." C_RESET); fflush(stdout);
    voiceprint_t* vp = voiceprint_enroll(model, audio, n);
    if (!vp) {
        printf(C_RED "\n  ✗ Enrollment failed. Model loaded?\n" C_RESET);
        if (tts) tts_speak(tts, "注册失败，请重试");
        free(audio); return -1;
    }
    printf(C_GREEN " done\n" C_RESET);

    int uid = storage_add_user(db, "user");
    if (uid < 0) {
        voiceprint_free(vp); free(audio);
        if (tts) tts_speak(tts, "注册失败");
        return -1;
    }

    if (storage_save_voiceprint(db, uid, "sherpa_emb", (uint8_t*)vp, sizeof(voiceprint_t)) == 0) {
        if (tts) tts_speak(tts, "注册成功");
        printf("\n  " C_GREEN "✓ Enrolled! User ID: %d\n" C_RESET, uid);
        printf("  " C_BLUE "You can now verify by pressing 'v'.\n" C_RESET);
    } else {
        if (tts) tts_speak(tts, "注册失败");
        printf(C_RED "  ✗ DB write failed.\n" C_RESET);
    }
    voiceprint_free(vp); free(audio);
    return uid;
}

/* ═══════════════════════════════════════
 * VERIFY
 * ═══════════════════════════════════════ */
static int do_verify(voiceprint_model_t* model, audio_capture_t* cap,
                     storage_t* db, tts_t* tts) {
    printf("\n" C_CYAN "┌─────────────────────────────────────┐\n");
    printf("│       " C_BOLD "Verify Voiceprint" C_CYAN "              │\n");
    printf("└─────────────────────────────────────┘\n" C_RESET);

    user_info_t users[50];
    int n_users = 50;
    if (storage_list_users(db, users, &n_users) != 0 || n_users == 0) {
        printf(C_YELLOW "  ⚠ No users enrolled. Press 'e' first.\n" C_RESET);
        return -1;
    }

    printf("  " C_BLUE "You will speak for %d seconds.\n" C_RESET, VERIFY_DURATION_SEC);
    if (tts) tts_speak(tts, "请说出你的语音口令");
    wait_enter();

    float* audio = malloc(MAX_AUDIO_SAMPLES * sizeof(float));
    if (!audio) return -1;

    int n = capture_audio(cap, audio, VERIFY_DURATION_SEC);
    if (n < 0) {
        printf(C_RED "  ✗ Recording failed.\n" C_RESET);
        free(audio); return -1;
    }
    if (n < SAMPLE_RATE * 0.3) { free(audio); return -1; }
    if (!check_energy(audio, n)) {
        printf(C_YELLOW "  ⚠ Too quiet.\n" C_RESET);
        free(audio); return -1;
    }

    printf("  Verifying" C_YELLOW "..." C_RESET); fflush(stdout);

    int matched = 0, best_idx = -1;
    float best_score = -1;
    for (int i = 0; i < n_users && !matched; i++) {
        voiceprint_t enrolled;
        size_t sz = sizeof(voiceprint_t);
        if (storage_load_voiceprint(db, users[i].user_id, "sherpa_emb", (uint8_t*)&enrolled, &sz) != 0)
            continue;

        float score;
        verify_result_t res = voiceprint_verify(model, audio, n, &enrolled, &score);

        if (score > best_score) { best_score = score; best_idx = i; }
        matched = (res == VERIFY_RESULT_MATCH);
    }

    printf(C_GREEN " done\n" C_RESET);
    printf("\n");

    if (matched && best_idx >= 0) {
        printf("  " C_GREEN "┌─────────────────────────────────────┐\n");
        printf("  │       ✓  VOICEPRINT MATCHED !      │\n");
        printf("  └─────────────────────────────────────┘\n" C_RESET);
        printf("    User:      %s (ID=%d)\n", users[best_idx].name, users[best_idx].user_id);
        printf("    Score:     %.4f\n", best_score);
        draw_score(best_score, model->threshold);
        if (tts) tts_speak(tts, "验证通过");
    } else {
        printf("  " C_RED "┌─────────────────────────────────────┐\n");
        printf("  │       ✗  NO MATCH FOUND            │\n");
        printf("  └─────────────────────────────────────┘\n" C_RESET);
        if (best_idx >= 0) {
            printf("    Best:     %s (score=%.4f)\n", users[best_idx].name, best_score);
            draw_score(best_score, model->threshold);
        }
        printf("    " C_YELLOW "Tips: speak clearly, same phrase as enrollment,\n");
        printf("    or check if you have enrolled first.\n" C_RESET);
        if (tts) tts_speak(tts, "验证失败，请重试");
    }

    free(audio);
    return matched ? 1 : 0;
}

/* ═══════════════════════════════════════
 * LIST / DELETE
 * ═══════════════════════════════════════ */
static void list_users(storage_t* db) {
    user_info_t users[50]; int cnt = 50;
    if (storage_list_users(db, users, &cnt) != 0 || cnt == 0) {
        printf(C_YELLOW "  No users.\n" C_RESET); return;
    }
    printf("\n" C_CYAN "── Enrolled users ──\n" C_RESET);
    for (int i = 0; i < cnt; i++)
        printf("  %d. " C_BOLD "%s" C_RESET " (ID=%d)\n", i+1, users[i].name, users[i].user_id);
    printf("\n");
}

static void delete_all_users(storage_t* db) {
    user_info_t users[50]; int cnt = 50;
    if (storage_list_users(db, users, &cnt) != 0) return;
    for (int i = 0; i < cnt; i++) storage_delete_user(db, users[i].user_id);
    printf(C_GREEN "  ✓ Deleted %d user(s).\n" C_RESET, cnt);
}

/* ═══════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════ */
int main(int argc, char* argv[]) {
    const char* model_dir = "models";
    const char* db_path = CFG_DB_PATH;
    (void)argc; (void)argv;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    
    printf("\n" C_CYAN C_BOLD "  ◇ Voiceprint Lock ◇\n" C_RESET);
    printf("  " C_BLUE "sherpa-onnx CAM++ engine\n" C_RESET "\n");

    printf(C_BOLD "  System init" C_RESET " ");
    storage_t* db = storage_init(db_path);
    if (!db) return 1;
    printf(C_GREEN "■" C_RESET " ");

    audio_capture_t* cap = audio_capture_init(NULL);
    if (!cap) { storage_close(db); return 1; }
    printf(C_GREEN "■" C_RESET " ");

    tts_t* tts = tts_init("/dev/ttyUSB0", 115200);
    if (tts) {
        printf(C_GREEN "■" C_RESET " ");
    } else {
        printf(C_YELLOW "⚠" C_RESET " ");
    }

    voiceprint_model_t* model = voiceprint_init(model_dir);
    if (!model) {
        printf(C_RED "\n\n  ✗ Model not found!\n" C_RESET);
        printf("    " C_YELLOW "1. pip install sherpa-onnx\n");
        printf("    2. Download model from GitHub releases\n");
        printf("    3. Place in models/ directory\n" C_RESET);
        if (tts) tts_speak(tts, "模型未找到");
        tts_destroy(tts);
        audio_capture_close(cap); storage_close(db);
        return 1;
    }
    printf(C_GREEN "■\n" C_RESET);

    if (tts) tts_speak(tts, "系统启动，声纹锁就绪");

    
    printf("\n" C_BOLD "  Ready." C_RESET);
    printf("  " C_CYAN "threshold=%.2f\n\n" C_RESET, model->threshold);

    printf("  " C_BOLD "Commands:\n" C_RESET);
    printf("    " C_GREEN "[e]" C_RESET "nroll    — Register your voiceprint (3s)\n");
    printf("    " C_GREEN "[v]" C_RESET "erify    — Verify your identity (2s)\n");
    printf("    " C_BLUE "[l]" C_RESET "ist      — Show enrolled users\n");
    printf("    " C_YELLOW "[d]" C_RESET "elete    — Remove all users\n");
    printf("    " C_RED "[q]" C_RESET "uit      — Exit\n");

    while (g_running) {
        printf("\n" C_BOLD "▶ " C_RESET); fflush(stdout);
        char line[64];
        if (!fgets(line, sizeof(line), stdin)) break;
        char cmd = line[0];

        switch (cmd) {
            case 'e': case 'E': do_enroll(model, cap, db, tts); break;
            case 'v': case 'V': {
                int ok = do_verify(model, cap, db, tts);
                if (ok > 0) {
                    printf("\n  " C_YELLOW "┌─────────────────────────────────────┐\n");
                    printf("  │        🔓  Door Open  5s           │\n");
                    printf("  └─────────────────────────────────────┘\n" C_RESET);
                    if (tts) tts_speak(tts, "五、四、三、二、一");
                    for (int i = 5; i > 0; i--) {
                        printf("\r  " C_YELLOW "  Locking in %d..." C_RESET, i); fflush(stdout);
                        sleep(1);
                    }
                    printf("\r  " C_YELLOW "  Locked.                  \n" C_RESET);
                    if (tts) tts_speak(tts, "已锁定");
                }
                break;
            }
            case 'l': case 'L': list_users(db); break;
            case 'd': case 'D':
                printf("  " C_YELLOW "Delete ALL users? (y/N): " C_RESET); fflush(stdout);
                if (fgets(line, sizeof(line), stdin) && (line[0]=='y'||line[0]=='Y'))
                    delete_all_users(db);
                break;
            case 'q': case 'Q': g_running = 0; break;
            default: if (cmd != '\0') printf(C_YELLOW "  Unknown. Use e, v, l, d, q\n" C_RESET); break;
        }
    }

    if (tts) tts_speak(tts, "系统关闭");
    printf("\n" C_BOLD "  Bye!\n" C_RESET);
    voiceprint_model_free(model);
    tts_destroy(tts);
    audio_capture_close(cap);
    storage_close(db);
    return 0;
}
