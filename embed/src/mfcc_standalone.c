/**
 * mfcc_standalone.c - Standalone MFCC Implementation
 *
 * Simplified MFCC without CMSIS-DSP dependency.
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    float mel_bank[N_MELS][N_FFT / 2 + 1];
    float dct_matrix[N_MELS][N_MFCC];
    float window[N_FFT];
} mfcc_context_t;

static float hz_to_mel(float hz) {
    return 2595.0f * log10f(1.0f + hz / 700.0f);
}

static float mel_to_hz(float mel) {
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

static void init_hamming_window(float *window, int n) {
    for (int i = 0; i < n; i++) {
        window[i] = 0.54f - 0.46f * cosf(2.0f * M_PI * i / (n - 1));
    }
}

static int init_mel_filterbank(mfcc_context_t *ctx) {
    float min_mel = hz_to_mel(MEL_LOWER_FREQ);
    float max_mel = hz_to_mel(MEL_UPPER_FREQ);
    float mel_points[N_MELS + 2];
    for (int i = 0; i < N_MELS + 2; i++) {
        mel_points[i] = min_mel + (max_mel - min_mel) * i / (N_MELS + 1);
    }
    int fft_bins[N_MELS + 2];
    for (int i = 0; i < N_MELS + 2; i++) {
        float hz = mel_to_hz(mel_points[i]);
        fft_bins[i] = (int)floorf((N_FFT + 1) * hz / SAMPLE_RATE);
    }
    memset(ctx->mel_bank, 0, sizeof(ctx->mel_bank));
    for (int m = 1; m <= N_MELS; m++) {
        for (int k = fft_bins[m - 1]; k < fft_bins[m]; k++) {
            if (k >= 0 && k < N_FFT / 2 + 1)
                ctx->mel_bank[m - 1][k] = (float)(k - fft_bins[m - 1] + 1) / (fft_bins[m] - fft_bins[m - 1] + 1);
        }
        for (int k = fft_bins[m]; k < fft_bins[m + 1]; k++) {
            if (k >= 0 && k < N_FFT / 2 + 1)
                ctx->mel_bank[m - 1][k] = (float)(fft_bins[m + 1] - k) / (fft_bins[m + 1] - fft_bins[m] + 1);
        }
    }
    for (int n = 0; n < N_MFCC; n++) {
        for (int m = 0; m < N_MELS; m++) {
            ctx->dct_matrix[m][n] = cosf(M_PI * n * (m + 0.5f) / N_MELS);
        }
    }
    return 0;
}

static void fft_float(float *real, float *imag, int n) {
    int j = 0;
    for (int i = 0; i < n - 1; i++) {
        if (i < j) { float t=real[i]; real[i]=real[j]; real[j]=t; t=imag[i]; imag[i]=imag[j]; imag[j]=t; }
        int k = n / 2;
        while (k <= j) { j -= k; k /= 2; }
        j += k;
    }
    for (int step = 1; step < n; step *= 2) {
        float angle = M_PI / step, wreal = 1.0f, wimag = 0.0f;
        float wstep_real = cosf(angle), wstep_imag = sinf(angle);
        for (int m = 0; m < step; m++) {
            for (int i = m; i < n; i += step * 2) {
                int j2 = i + step;
                float tr = wreal * real[j2] - wimag * imag[j2];
                float ti = wreal * imag[j2] + wimag * real[j2];
                real[j2] = real[i] - tr; imag[j2] = imag[i] - ti;
                real[i] += tr; imag[i] += ti;
            }
            float nwr = wreal * wstep_real - wimag * wstep_imag;
            wimag = wreal * wstep_imag + wimag * wstep_real;
            wreal = nwr;
        }
    }
}

int mfcc_init_standalone(void **ctx) {
    mfcc_context_t *mc = calloc(1, sizeof(mfcc_context_t));
    if (!mc) { LOG_ERROR("Failed to allocate MFCC context"); return ERR_MFCC_INIT; }
    init_hamming_window(mc->window, N_FFT);
    if (init_mel_filterbank(mc) != 0) { LOG_ERROR("Failed to init mel filterbank"); free(mc); return ERR_MFCC_INIT; }
    LOG_INFO("MFCC init: FFT=%d, MEL=%d, MFCC=%d", N_FFT, N_MELS, N_MFCC);
    *ctx = mc;
    return ERR_OK;
}

int mfcc_compute_frame(void *ctx, const float *samples, int n_samples, float *mfcc_out) {
    mfcc_context_t *mc = (mfcc_context_t *)ctx;
    if (!mc || !samples || !mfcc_out || n_samples <= 0) return ERR_INVALID_PARAM;
    float buffer[N_FFT];
    /* Zero-pad if frame is shorter than N_FFT */
    int copy_len = (n_samples < N_FFT) ? n_samples : N_FFT;
    for (int i = 0; i < copy_len; i++) buffer[i] = samples[i] * mc->window[i];
    for (int i = copy_len; i < N_FFT; i++) buffer[i] = 0.0f;
    float imag[N_FFT]; memset(imag, 0, sizeof(imag));
    fft_float(buffer, imag, N_FFT);
    float power[N_FFT / 2 + 1];
    for (int i = 0; i < N_FFT / 2 + 1; i++) power[i] = buffer[i] * buffer[i] + imag[i] * imag[i];
    float mel_energy[N_MELS];
    for (int m = 0; m < N_MELS; m++) {
        mel_energy[m] = 0.0f;
        for (int k = 0; k < N_FFT / 2 + 1; k++) mel_energy[m] += mc->mel_bank[m][k] * power[k];
        mel_energy[m] += 1e-10f;
    }
    float log_mel[N_MELS];
    for (int m = 0; m < N_MELS; m++) log_mel[m] = logf(mel_energy[m]);
    float mfcc_all[N_MFCC];
    for (int n = 0; n < N_MFCC; n++) {
        mfcc_all[n] = 0.0f;
        for (int m = 0; m < N_MELS; m++) mfcc_all[n] += mc->dct_matrix[m][n] * log_mel[m];
    }
    for (int i = 0; i < N_MFCC; i++) mfcc_out[i] = mfcc_all[i];
    return ERR_OK;
}

int mfcc_extract_frames(void *ctx, const float *samples, int n_samples, float *features_out, int *n_frames_out) {
    if (!ctx || !samples || !features_out || !n_frames_out) return ERR_INVALID_PARAM;
    int max_frames = (n_samples - FRAME_LEN) / FRAME_SHIFT + 1;
    *n_frames_out = 0;
    for (int fi = 0; fi < max_frames; fi++) {
        int offset = fi * FRAME_SHIFT;
        float feat[N_MFCC];
        if (mfcc_compute_frame(ctx, samples + offset, FRAME_LEN, feat) == ERR_OK) {
            for (int i = 0; i < N_MFCC; i++) features_out[fi * N_MFCC + i] = feat[i];
            (*n_frames_out)++;
        }
    }
    return ERR_OK;
}

void mfcc_free_standalone(void *ctx) { free(ctx); }
void mfcc_print(const float *mfcc, int n) { printf("MFCC[%d]: ", n); for (int i = 0; i < n; i++) printf("%.2f ", mfcc[i]); printf("\n"); }

/* err_str implementation */
const char* err_str(error_code_t err) {
    switch (err) {
        case ERR_OK: return "OK";
        case ERR_INVALID_PARAM: return "Invalid parameter";
        case ERR_FILE_NOT_FOUND: return "File not found";
        case ERR_FILE_READ: return "File read error";
        case ERR_FILE_WRITE: return "File write error";
        case ERR_MEMORY: return "Memory allocation error";
        case ERR_AUDIO_DEVICE: return "Audio device error";
        case ERR_AUDIO_FORMAT: return "Audio format error";
        case ERR_MFCC_INIT: return "MFCC init error";
        case ERR_MFCC_COMPUTE: return "MFCC compute error";
        case ERR_MODEL_LOAD: return "Model load error";
        case ERR_MODEL_SAVE: return "Model save error";
        case ERR_VERIFICATION: return "Verification error";
        case ERR_DATABASE: return "Database error";
        case ERR_USER_NOT_FOUND: return "User not found";
        case ERR_USER_EXISTS: return "User exists";
        case ERR_ENROLLMENT: return "Enrollment error";
        case ERR_VAD: return "VAD error";
        case ERR_MISMATCH: return "Mismatch";
        default: return "Unknown error";
    }
}
