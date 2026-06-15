/**
 * voiceprint_verify.h - Speaker verification using MFCC + DTW
 *
 * Method: MFCC feature extraction + Dynamic Time Warping
 * No training, no pre-trained models, fully transparent.
 *
 * Reference: scripts/compare_methods.py :: MFCC_DTW
 * Accuracy on LibriSpeech dev-clean: ~92%
 */

#ifndef VOICEPRINT_VERIFY_H
#define VOICEPRINT_VERIFY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define N_MFCC                 13
#define MAX_ENROLL_FRAMES      600   /* ~6 seconds at 100fps */

/* Verification result */
typedef enum {
    VERIFY_RESULT_UNKNOWN = 0,
    VERIFY_RESULT_MATCH,
    VERIFY_RESULT_NO_MATCH
} verify_result_t;

/* Enrolled user voiceprint */
typedef struct {
    int user_id;
    /* Enrollment MFCC frames (for DTW comparison) */
    float mfcc_frames[MAX_ENROLL_FRAMES][N_MFCC];
    int n_frames;                       /* actual number of frames stored */
    float mfcc_mean[N_MFCC];            /* for cosine fallback */
} voiceprint_t;

/* Model handle (kept for API compatibility — no model needed for DTW) */
typedef struct {
    float dtw_threshold;                /* normalized DTW distance threshold */
    float cosine_threshold;             /* cosine similarity fallback */
} voiceprint_model_t;

/* ========== API ========== */

voiceprint_model_t* voiceprint_init(const char* model_path);
void voiceprint_model_free(voiceprint_model_t* model);

/**
 * Extract voiceprint from MFCC features
 * Stores the raw MFCC frames and computes the mean.
 */
int voiceprint_extract(voiceprint_model_t* model,
                       const float* mfcc, int n_frames,
                       voiceprint_t* vp);

/**
 * Verify against enrolled user
 * Primary: DTW distance on full MFCC sequences
 * Fallback: cosine similarity on MFCC means
 */
verify_result_t voiceprint_verify(voiceprint_model_t* model,
                                   const float* test_mfcc, int n_frames,
                                   const voiceprint_t* enrolled,
                                   float* score_out);

voiceprint_t* voiceprint_enroll(voiceprint_model_t* model,
                                 const float* mfcc, int n_frames);
void voiceprint_free(voiceprint_t* vp);

#ifdef __cplusplus
}
#endif

#endif /* VOICEPRINT_VERIFY_H */
