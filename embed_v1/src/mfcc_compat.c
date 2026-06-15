/**
 * mfcc_compat.c - Adapter: mfcc_standalone.c → mfcc.h API
 *
 * Allows main.c to use the mfcc.h interface regardless of which
 * MFCC implementation is linked.
 *
 * Build with: src/mfcc_standalone.c
 */

#include "mfcc.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>

/* Forward declarations from mfcc_standalone.c */
int mfcc_init_standalone(void **ctx);
int mfcc_compute_frame(void *ctx, const float *samples,
                        int n_samples, float *mfcc_out);
int mfcc_extract_frames(void *ctx, const float *samples, int n_samples,
                         float *features_out, int *n_frames_out);
void mfcc_free_standalone(void *ctx);

/* Opaque struct matching mfcc.h typedef */
struct mfcc_config {
    void* impl;
    uint32_t sample_rate;
};

mfcc_config_t* mfcc_init(uint32_t sample_rate) {
    struct mfcc_config* cfg = malloc(sizeof(struct mfcc_config));
    if (!cfg) return NULL;
    void* impl = NULL;
    if (mfcc_init_standalone(&impl) != 0) { free(cfg); return NULL; }
    cfg->impl = impl;
    cfg->sample_rate = sample_rate;
    LOG_DEBUG("MFCC init (standalone, %d Hz)", sample_rate);
    return cfg;
}

int mfcc_extract_frame(mfcc_config_t* config,
                       const float* frame, float* mfcc) {
    if (!config || !frame || !mfcc) return -1;
    return mfcc_compute_frame(config->impl, frame, N_FFT, mfcc);
}

int mfcc_extract(mfcc_config_t* config,
                 const float* samples, int n_samples,
                 float* mfcc, int* n_frames) {
    if (!config || !samples || !mfcc || !n_frames) return -1;
    return mfcc_extract_frames(config->impl, samples, n_samples,
                                mfcc, n_frames);
}

void mfcc_delta(const float* mfcc, int n_frames, float* delta) {
    if (!mfcc || n_frames < 2 || !delta) return;
    for (int f = 1; f < n_frames - 1; f++)
        for (int d = 0; d < N_MFCC; d++)
            delta[f * N_MFCC + d] = (mfcc[(f+1) * N_MFCC + d]
                                   - mfcc[(f-1) * N_MFCC + d]) * 0.5f;
    for (int d = 0; d < N_MFCC; d++) {
        delta[d] = 0.0f;
        delta[(n_frames-1) * N_MFCC + d] = 0.0f;
    }
}

void mfcc_free(mfcc_config_t* config) {
    if (!config) return;
    if (config->impl) mfcc_free_standalone(config->impl);
    free(config);
}
