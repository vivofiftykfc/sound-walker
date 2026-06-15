/**
 * mfcc.h - MFCC feature extraction using CMSIS-DSP
 *
 * Extraction pipeline:
 *   PCM -> Pre-emphasis -> Framing -> Hamming Window ->
 *   FFT -> Mel Filterbank -> Log -> DCT -> MFCC
 *
 * Uses ARM CMSIS-DSP library for optimized operations.
 */

#ifndef MFCC_H
#define MFCC_H

#include <stdint.h>
#include <stdbool.h>

/* MFCC configuration */
#define N_FFT           512
#define N_MEL_FILTERS   40
#define N_MFCC          13
#define N_MELS          40
#define MEL_LOW_FREQ    0
#define MEL_HIGH_FREQ   8000

/* MFCC handle */
typedef struct mfcc_config mfcc_config_t;

/**
 * Initialize MFCC extractor
 * @param sample_rate Sample rate (default 16000)
 * @return            MFCC handle or NULL on error
 */
mfcc_config_t* mfcc_init(uint32_t sample_rate);

/**
 * Extract MFCC from preprocessed frame
 * @param config  MFCC config
 * @param frame   Input frame (N_FFT samples, windowed)
 * @param mfcc   Output MFCC coefficients (N_MFCC)
 * @return        0 on success, -1 on error
 */
int mfcc_extract_frame(mfcc_config_t* config,
                       const float* frame, float* mfcc);

/**
 * Extract MFCC from audio samples
 * @param config    MFCC config
 * @param samples   Input samples (pre-emphasized)
 * @param n_samples Number of samples
 * @param mfcc     Output MFCC (n_frames x N_MFCC)
 * @param n_frames  Output: number of frames
 * @return          0 on success, -1 on error
 */
int mfcc_extract(mfcc_config_t* config,
                 const float* samples, int n_samples,
                 float* mfcc, int* n_frames);

/**
 * Get delta MFCC (first-order difference)
 * @param mfcc    Input MFCC
 * @param n_frames Number of frames
 * @param delta   Output delta MFCC
 */
void mfcc_delta(const float* mfcc, int n_frames, float* delta);

/**
 * Free MFCC extractor
 */
void mfcc_free(mfcc_config_t* config);

#endif /* MFCC_H */
