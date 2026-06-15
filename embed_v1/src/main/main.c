#include "config.h"
#include "audio_capture.h"
#include "audio_preprocess.h"
#include "mfcc.h"
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
#define MAX_MFCC_FRAMES         3000
#define READ_CHUNK              441

#define C_RESET  "\033[0m"
#define C_RED    "\033[31m"
#define C_GREEN  "\033[32m"
#define C_YELLOW "\033[33m"
#define C_BLUE   "\033[34m"
#define C_CYAN   "\033[36m"
#define C_BOLD   "\033[1m"

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

static int check_energy(const float* a, int n) {
    float peak = 0;
    for (int i = 0; i < n; i++) { float v = fabsf(a[i]); if (v > peak) peak = v; }
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
        if (i == tp) printf(C_RED "|" C_RESET);
        else if (i < sp) printf("#");
        else printf(".");
    }
    if (tp >= w) printf(C_RED "|" C_RESET);
    printf("\n");
    printf("         score=%.3f", score);
    if (score >= thresh) printf(C_GREEN " >=" C_RESET);
    else printf(C_RED " <" C_RESET);
    printf(" threshold=%.2f\n", thresh);
}

static int do_enroll(voiceprint_model_t* model, mfcc_config_t* mfcc_ctx,
                     audio_preproc_t* preproc, audio_capture_t* cap,
                     storage_t* db, tts_t* tts) {
    printf("\n" C_CYAN "+-------------------------------------+\n");
    printf("|       " C_BOLD "Enroll New User" C_CYAN "               |\n");
    printf("+-------------------------------------+\n" C_RESET);

    if (tts) tts_speak(tts, "请说出你要注册的语音");
    wait_enter();

    float* audio = malloc(MAX_AUDIO_SAMPLES * sizeof(float));
    if (!audio) return -1;
    int n = capture_audio(cap, audio, ENROLL_DURATION_SEC);
    if (n < 0) {
        printf(C_RED "  x Recording failed.\n" C_RESET);
        free(audio); return -1;
    }
    if (n < SAMPLE_RATE * 0.3) {
        printf(C_RED "  x Too short.\n" C_RESET);
        free(audio); return -1;
    }
    if (!check_energy(audio, n)) {
        printf(C_YELLOW "  w Too quiet.\n" C_RESET);
        free(audio); return -1;
    }

    float* em = malloc(n * sizeof(float));
    if (!em) { free(audio); return -1; }
    audio_preproc_preemphasis(preproc, audio, em, n);

    float* mfcc = malloc(MAX_MFCC_FRAMES * N_MFCC * sizeof(float));
    if (!mfcc) { free(audio); free(em); return -1; }
    int nf = 0;
    mfcc_extract(mfcc_ctx, em, n, mfcc, &nf);

    float peak_rms = 0;
    for (int f = 0; f < nf; f++) {
        double rms = 0; int off = f * FRAME_SHIFT;
        for (int i = 0; i < FRAME_LEN && off + i < n; i++) rms += (double)em[off + i] * em[off + i];
        rms = sqrt(rms / FRAME_LEN);
        if ((float)rms > peak_rms) peak_rms = (float)rms;
    }
    float th = fmaxf(peak_rms * 0.2f, 0.01f);
    int kept = 0;
    for (int f = 0; f < nf; f++) {
        double rms = 0; int off = f * FRAME_SHIFT;
        for (int i = 0; i < FRAME_LEN && off + i < n; i++) rms += (double)em[off + i] * em[off + i];
        rms = sqrt(rms / FRAME_LEN);
        if ((float)rms > th) {
            if (kept != f) memcpy(&mfcc[kept * N_MFCC], &mfcc[f * N_MFCC], N_MFCC * sizeof(float));
            kept++;
        }
    }
    free(em);
    if (kept < 10) {
        printf(C_RED "  x Too little speech.\n" C_RESET);
        if (tts) tts_speak(tts, "语音太短，请重试");
        free(audio); free(mfcc); return -1;
    }

    printf("  Processing" C_YELLOW "..." C_RESET); fflush(stdout);
    voiceprint_t* vp = voiceprint_enroll(model, mfcc, kept);
    if (!vp) {
        printf(C_RED "  x Enroll failed.\n" C_RESET);
        if (tts) tts_speak(tts, "注册失败，请重试");
        free(audio); free(mfcc); return -1;
    }
    printf(C_GREEN " done\n" C_RESET);

    int uid = storage_add_user(db, "user");
    if (uid >= 0 && storage_save_voiceprint(db, uid, "mfcc_frames", (uint8_t*)vp, sizeof(voiceprint_t)) == 0) {
        if (tts) tts_speak(tts, "注册成功");
        printf("\n  " C_GREEN "v Enrolled! User ID: %d\n" C_RESET, uid);
    } else {
        printf(C_RED "  x DB write failed.\n" C_RESET);
        if (tts) tts_speak(tts, "注册失败");
    }

    voiceprint_free(vp); free(audio); free(mfcc);
    return uid;
}

static int do_verify(voiceprint_model_t* model, mfcc_config_t* mfcc_ctx,
                     audio_preproc_t* preproc, audio_capture_t* cap,
                     storage_t* db, tts_t* tts) {
    printf("\n" C_CYAN "+-------------------------------------+\n");
    printf("|       " C_BOLD "Verify Voiceprint" C_CYAN "              |\n");
    printf("+-------------------------------------+\n" C_RESET);

    user_info_t users[50]; int nu = 50;
    if (storage_list_users(db, users, &nu) != 0 || nu == 0) {
        printf(C_YELLOW "  w No users. Press 'e' first.\n" C_RESET); return -1;
    }
    printf("  " C_BLUE "Speak for %d seconds.\n" C_RESET, VERIFY_DURATION_SEC);

    if (tts) tts_speak(tts, "请说出你的语音口令");
    wait_enter();

    float* audio = malloc(MAX_AUDIO_SAMPLES * sizeof(float));
    if (!audio) return -1;
    int n = capture_audio(cap, audio, VERIFY_DURATION_SEC);
    if (n < 0) {
        printf(C_RED "  x Failed.\n" C_RESET);
        free(audio); return -1;
    }
    if (!check_energy(audio, n)) { free(audio); return -1; }

    float* em = malloc(n * sizeof(float));
    audio_preproc_preemphasis(preproc, audio, em, n);

    float* mfcc = malloc(MAX_MFCC_FRAMES * N_MFCC * sizeof(float));
    int nf = 0;
    mfcc_extract(mfcc_ctx, em, n, mfcc, &nf);

    float peak_rms = 0;
    for (int f = 0; f < nf; f++) {
        double rms = 0; int off = f * FRAME_SHIFT;
        for (int i = 0; i < FRAME_LEN && off + i < n; i++) rms += (double)em[off + i] * em[off + i];
        rms = sqrt(rms / FRAME_LEN);
        if ((float)rms > peak_rms) peak_rms = (float)rms;
    }
    float th = fmaxf(peak_rms * 0.2f, 0.01f);
    int kept = 0;
    for (int f = 0; f < nf; f++) {
        double rms = 0; int off = f * FRAME_SHIFT;
        for (int i = 0; i < FRAME_LEN && off + i < n; i++) rms += (double)em[off + i] * em[off + i];
        rms = sqrt(rms / FRAME_LEN);
        if ((float)rms > th) {
            if (kept != f) memcpy(&mfcc[kept * N_MFCC], &mfcc[f * N_MFCC], N_MFCC * sizeof(float));
            kept++;
        }
    }
    free(em);
    if (kept < 5) { printf(C_YELLOW "  w Too little speech.\n" C_RESET); free(audio); free(mfcc); return -1; }

    printf("  Verifying" C_YELLOW "..." C_RESET); fflush(stdout);

    int matched = 0, best_i = -1;
    float best_s = -999;
    for (int i = 0; i < nu && !matched; i++) {
        voiceprint_t enrolled;
        size_t sz = sizeof(voiceprint_t);
        if (storage_load_voiceprint(db, users[i].user_id, "mfcc_frames", (uint8_t*)&enrolled, &sz)) continue;
        float s;
        verify_result_t res = voiceprint_verify(model, mfcc, kept, &enrolled, &s);
        if (s > best_s) { best_s = s; best_i = i; }
        if (res == VERIFY_RESULT_MATCH) matched = 1;
    }

    printf(C_GREEN " done\n" C_RESET); printf("\n");

    if (matched && best_i >= 0) {
        printf("  " C_GREEN "+-------------------------------------+\n");
        printf("  |       v  VOICEPRINT MATCHED !      |\n");
        printf("  +-------------------------------------+\n" C_RESET);
        printf("    User:  %s (ID=%d)\n", users[best_i].name, users[best_i].user_id);
        printf("    Score: %.4f\n", best_s);
        draw_score(best_s, 0.5f);
        if (tts) tts_speak(tts, "验证通过");
    } else {
        printf("  " C_RED "+-------------------------------------+\n");
        printf("  |       x  NO MATCH FOUND            |\n");
        printf("  +-------------------------------------+\n" C_RESET);
        if (best_i >= 0) {
            printf("    Best: %s (score=%.4f)\n", users[best_i].name, best_s);
            draw_score(best_s, 0.5f);
        }
        printf("    " C_YELLOW "Tips: same phrase as enrollment,\n");
        printf("    speak clearly.\n" C_RESET);
        if (tts) tts_speak(tts, "验证失败，请重试");
    }

    free(audio); free(mfcc);
    return matched ? 1 : 0;
}

static void list_users(storage_t* db) {
    user_info_t u[50]; int c = 50;
    if (storage_list_users(db, u, &c) != 0 || c == 0) { printf(C_YELLOW "  No users.\n" C_RESET); return; }
    printf("\n" C_CYAN "-- Enrolled users --\n" C_RESET);
    for (int i = 0; i < c; i++) printf("  %d. " C_BOLD "%s" C_RESET " (ID=%d)\n", i+1, u[i].name, u[i].user_id);
}

static void delete_all_users(storage_t* db) {
    user_info_t u[50]; int c = 50;
    if (storage_list_users(db, u, &c) != 0) return;
    for (int i = 0; i < c; i++) storage_delete_user(db, u[i].user_id);
    printf(C_GREEN "  v Deleted %d user(s).\n" C_RESET, c);
}

int main(int argc, char* argv[]) {
    const char* md = "models";
    const char* dbp = CFG_DB_PATH;
    (void)argc; (void)argv;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("\n" C_CYAN C_BOLD "  o Voiceprint Lock v1 -- MFCC+DTW o\n" C_RESET);
    printf("  " C_BLUE "No external models required\n" C_RESET "\n");

    printf(C_BOLD "  Init" C_RESET " ");
    storage_t* db = storage_init(dbp); if (!db) return 1;
    printf(C_GREEN "#" C_RESET " ");
    audio_capture_t* cap = audio_capture_init(NULL);
    if (!cap) { storage_close(db); return 1; }
    printf(C_GREEN "#" C_RESET " ");
    audio_preproc_t* preproc = audio_preproc_init(SAMPLE_RATE);
    if (!preproc) { audio_capture_close(cap); storage_close(db); return 1; }
    printf(C_GREEN "#" C_RESET " ");
    mfcc_config_t* mfcc_ctx = mfcc_init(SAMPLE_RATE);
    if (!mfcc_ctx) { audio_preproc_free(preproc); audio_capture_close(cap); storage_close(db); return 1; }
    printf(C_GREEN "#" C_RESET " ");

    tts_t* tts = tts_init("/dev/ttyUSB0", 115200);
    if (tts) {
        printf(C_GREEN "#" C_RESET " ");
    } else {
        printf(C_YELLOW "?" C_RESET " ");
    }

    voiceprint_model_t* model = voiceprint_init(md);
    if (!model) {
        printf(C_RED "\n  x Init failed\n" C_RESET);
        tts_destroy(tts);
        mfcc_free(mfcc_ctx); audio_preproc_free(preproc);
        audio_capture_close(cap); storage_close(db);
        return 1;
    }
    printf(C_GREEN "#\n" C_RESET);

    if (tts) tts_speak(tts, "系统启动，声纹锁就绪");

    printf("\n" C_BOLD "  Ready." C_RESET);
    printf("  " C_CYAN "DTW threshold=%.2f\n\n" C_RESET, model->dtw_threshold);

    printf("  " C_BOLD "Commands:\n" C_RESET);
    printf("    " C_GREEN "[e]" C_RESET "nroll    -- Register (3s)\n");
    printf("    " C_GREEN "[v]" C_RESET "erify    -- Verify (2s)\n");
    printf("    " C_BLUE "[l]" C_RESET "ist      -- List users\n");
    printf("    " C_YELLOW "[d]" C_RESET "elete    -- Remove all\n");
    printf("    " C_RED "[q]" C_RESET "uit\n");

    while (g_running) {
        printf("\n" C_BOLD "> " C_RESET); fflush(stdout);
        char line[64];
        if (!fgets(line, sizeof(line), stdin)) break;
        char cmd = line[0];

        switch (cmd) {
            case 'e': case 'E':
                do_enroll(model, mfcc_ctx, preproc, cap, db, tts);
                break;
            case 'v': case 'V': {
                int ok = do_verify(model, mfcc_ctx, preproc, cap, db, tts);
                if (ok > 0) {
                    printf("\n  " C_YELLOW "+-------------------------------------+\n");
                    printf("  |        @@  Door Open  5s           |\n");
                    printf("  +-------------------------------------+\n" C_RESET);
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
                printf("  " C_YELLOW "Delete ALL? (y/N): " C_RESET); fflush(stdout);
                if (fgets(line, sizeof(line), stdin) && (line[0]=='y'||line[0]=='Y')) delete_all_users(db);
                break;
            case 'q': case 'Q': g_running = 0; break;
            default: if (cmd != '\0') printf(C_YELLOW "  Unknown.\n" C_RESET); break;
        }
    }

    if (tts) tts_speak(tts, "系统关闭");
    printf("\n" C_BOLD "  Bye!\n" C_RESET);
    voiceprint_model_free(model); mfcc_free(mfcc_ctx);
    tts_destroy(tts);
    audio_preproc_free(preproc); audio_capture_close(cap); storage_close(db);
    return 0;
}
