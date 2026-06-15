/**
 * audio_preprocess.h - Audio preprocessing for voiceprint
 *
 * Operations:
 *   - Pre-emphasis filter
 *   - Framing and windowing (Hamming)
 *   - Energy-based VAD (Voice Activity Detection)
 */

#ifndef AUDIO_PREPROCESS_H
#define AUDIO_PREPROCESS_H

#include <stdint.h>
#include <stdbool.h>

/* DSP constants */
#define PREEMPH_COEFF       0.97f
#define FRAME_LEN_MS        25       /* 25ms frame */
#define FRAME_SHIFT_MS       10      /* 10ms frame shift */
/* FRAME_LEN and FRAME_SHIFT are in config.h (uses SAMPLE_RATE) */

/* VAD thresholds */
#define VAD_ENERGY_THRESH   0.03f
#define VAD_ZCR_THRESH      10
#define VAD_MIN_SPEECH_FRAMES  15   /* Minimum 150ms of speech */
#define VAD_PAD_FRAMES      5        /* Padding before/after speech */

/* Preprocessing handle */
typedef struct audio_preproc audio_preproc_t;

/**
 * Initialize preprocessor
 * @param sample_rate  Sample rate (must be 16000)
 * @return             Preprocessor handle
 */
audio_preproc_t* audio_preproc_init(uint32_t sample_rate);

/**
 * Apply pre-emphasis filter
 * @param proc    Preprocessor
 * @param input   Input samples
 * @param output  Output samples (can be same as input)
 * @param n       Number of samples
 */
void audio_preproc_preemphasis(audio_preproc_t* proc,
                               const float* input, float* output, int n);

/**
 * Frame and window samples
 * @param proc    Preprocessor
 * @param samples Input samples
 * @param n       Number of samples
 * @param frames  Output frames (must have room for n_frames)
 * @return        Number of frames extracted
 */
int audio_preproc_frame(audio_preproc_t* proc,
                        const float* samples, int n, float* frames);

/**
 * Detect speech regions (VAD)
 * @param proc     Preprocessor
 * @param frames   Input frames
 * @param n_frames Number of frames
 * @param speech   Output: speech region flags
 * @return         Number of speech frames
 */
int audio_preproc_vad(audio_preproc_t* proc,
                      const float* frames, int n_frames, bool* speech);

/**
 * Free preprocessor
 */
void audio_preproc_free(audio_preproc_t* proc);

#endif /* AUDIO_PREPROCESS_H */
