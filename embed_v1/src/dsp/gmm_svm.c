
#include "voiceprint_verify.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>


static float dtw_distance(const float* x, int nx,
                           const float* y, int ny, int dim) {
    if (nx <= 0 || ny <= 0 || nx > MAX_ENROLL_FRAMES) {
        LOG_ERROR("DTW: invalid frames nx=%d ny=%d", nx, ny);
        return 1e10f;
    }

    int rows = nx + 1;
    int cols = ny + 1;
    float* D = (float*)malloc(rows * cols * sizeof(float));
    if (!D) { LOG_ERROR("DTW: alloc failed"); return 1e10f; }

    D[0] = 0.0f;
    for (int i = 1; i < rows; i++) D[i * cols] = 1e30f;
    for (int j = 1; j < cols; j++) D[j] = 1e30f;

    for (int i = 1; i <= nx; i++) {
        for (int j = 1; j <= ny; j++) {
            /* Cosine distance between frame i-1 and j-1 */
            /* (1 - cosine_sim), invariant to energy level, focuses on spectral shape */
            float dot = 0.0f, norm_x = 0.0f, norm_y = 0.0f;
            for (int d = 0; d < dim; d++) {
                float xv = x[(i-1) * dim + d];
                float yv = y[(j-1) * dim + d];
                dot += xv * yv;
                norm_x += xv * xv;
                norm_y += yv * yv;
            }
            float dist = 1.0f - dot / (sqrtf(norm_x) * sqrtf(norm_y) + 1e-10f);
            if (dist < 0.0f) dist = 0.0f;

            /* Minimum of three predecessors */
            float prev = D[(i-1) * cols + j];       /* up */
            if (D[i * cols + (j-1)] < prev)          /* left */
                prev = D[i * cols + (j-1)];
            if (D[(i-1) * cols + (j-1)] < prev)      /* diagonal */
                prev = D[(i-1) * cols + (j-1)];

            D[i * cols + j] = dist + prev;
        }
    }

    float result = D[nx * cols + ny] / (float)(nx + ny);
    free(D);
    return result;
}


static float cosine_similarity(const float* a, const float* b, int dim,
                               int skip_c0) {
    float dot = 0.0f, na = 0.0f, nb = 0.0f;
    int start = skip_c0 ? 1 : 0;
    for (int d = start; d < dim; d++) {
        dot += a[d] * b[d];
        na += a[d] * a[d];
        nb += b[d] * b[d];
    }
    return dot / (sqrtf(na) * sqrtf(nb) + 1e-10f);
}



voiceprint_model_t* voiceprint_init(const char* model_path) {
    LOG_INFO("=== Voiceprint init (MFCC+DTW) ===");
    voiceprint_model_t* model = (voiceprint_model_t*)calloc(1, sizeof(voiceprint_model_t));
    if (!model) return NULL;
    model->dtw_threshold = 0.30f;      /* cosine DTW: <0.30 = same speaker */
    model->cosine_threshold = 0.7f;
    (void)model_path;

    LOG_INFO("DTW threshold=%.2f (no CMVN), Cosine=%.2f",
             model->dtw_threshold, model->cosine_threshold);
    LOG_INFO("=== Voiceprint init complete ===");
    return model;
}

void voiceprint_model_free(voiceprint_model_t* model) {
    free(model);
}

int voiceprint_extract(voiceprint_model_t* model,
                       const float* mfcc, int n_frames,
                       voiceprint_t* vp) {
    if (!model || !mfcc || !vp || n_frames <= 0) {
        LOG_ERROR("extract: invalid params (n_frames=%d)", n_frames);
        return -1;
    }

    LOG_INFO("=== Voiceprint extract: %d frames ===", n_frames);

    if (n_frames > MAX_ENROLL_FRAMES) {
        LOG_WARN("Truncating %d frames to max %d", n_frames, MAX_ENROLL_FRAMES);
        n_frames = MAX_ENROLL_FRAMES;
    }

    memcpy(vp->mfcc_frames, mfcc, n_frames * N_MFCC * sizeof(float));
    vp->n_frames = n_frames;

    memset(vp->mfcc_mean, 0, sizeof(vp->mfcc_mean));
    for (int f = 0; f < n_frames; f++)
        for (int d = 0; d < N_MFCC; d++)
            vp->mfcc_mean[d] += mfcc[f * N_MFCC + d];
    for (int d = 0; d < N_MFCC; d++)
        vp->mfcc_mean[d] /= n_frames;

    vp->user_id = -1;
    LOG_INFO("=== Extract done: %d frames stored ===", vp->n_frames);
    return 0;
}

verify_result_t voiceprint_verify(voiceprint_model_t* model,
                                   const float* test_mfcc, int n_frames,
                                   const voiceprint_t* enrolled,
                                   float* score_out) {
    if (!model || !test_mfcc || !enrolled || !score_out) {
        LOG_ERROR("verify: null param");
        return VERIFY_RESULT_UNKNOWN;
    }

    LOG_INFO("=== Verify: test=%d frames, enrolled=%d frames ===",
             n_frames, enrolled->n_frames);

    float dtw_dist = dtw_distance(&enrolled->mfcc_frames[0][0],
                                   enrolled->n_frames,
                                   test_mfcc, n_frames, N_MFCC);

    float test_mean[N_MFCC] = {0};
    for (int f = 0; f < n_frames; f++)
        for (int d = 0; d < N_MFCC; d++)
            test_mean[d] += test_mfcc[f * N_MFCC + d];
    for (int d = 0; d < N_MFCC; d++) test_mean[d] /= n_frames;

    float cosine = cosine_similarity(test_mean, enrolled->mfcc_mean,
                                      N_MFCC, 1);  /* skip C0 */

    LOG_DEBUG("  DTW distance: %.4f (threshold: %.2f)", dtw_dist, model->dtw_threshold);
    LOG_DEBUG("  Cosine sim:   %.4f (threshold: %.2f)", cosine, model->cosine_threshold);

    float score;
    float threshold;
    verify_result_t result;

    /* DTW ~3-6 for same speaker, ~10+ for different after CMVN */
    float dtw_score = 1.0f / (1.0f + dtw_dist);

    if (enrolled->n_frames >= 10 && n_frames >= 10) {
        score = dtw_score;
        threshold = 1.0f / (1.0f + model->dtw_threshold);  /* dtw=7 → score=0.125 */

        LOG_DEBUG("  DTW score=%.4f >= %.4f (raw dist=%.2f)?", score, threshold, dtw_dist);

        if (score >= threshold) {
            result = VERIFY_RESULT_MATCH;
        } else if (cosine > model->cosine_threshold) {
            /* DTW borderline but cosine high — accept */
            LOG_DEBUG("  Cosine fallback: %.4f >= %.2f", cosine, model->cosine_threshold);
            result = VERIFY_RESULT_MATCH;
            score = (score + cosine) * 0.5f;
        } else {
            result = VERIFY_RESULT_NO_MATCH;
        }
    } else {
        /* Fallback: cosine similarity */
        LOG_DEBUG("  Too few frames, using cosine fallback");
        score = cosine;
        threshold = model->cosine_threshold;
        result = (score >= threshold)
                 ? VERIFY_RESULT_MATCH : VERIFY_RESULT_NO_MATCH;
    }

    *score_out = score;
    LOG_INFO("  DTW=%.2f(d=%04.2f), Cos=%.4f, Scr=%.4f, Th=%.2f → %s",
             dtw_score, dtw_dist, cosine, score, threshold,
             (result == VERIFY_RESULT_MATCH) ? "MATCH" : "NO_MATCH");
    LOG_INFO("=== Verify done ===");
    return result;
}

voiceprint_t* voiceprint_enroll(voiceprint_model_t* model,
                                 const float* mfcc, int n_frames) {
    if (!model || !mfcc || n_frames <= 0) {
        LOG_ERROR("enroll: invalid params");
        return NULL;
    }

    LOG_INFO("=== Enroll new user ===");
    voiceprint_t* vp = (voiceprint_t*)calloc(1, sizeof(voiceprint_t));
    if (!vp) { LOG_ERROR("enroll: alloc failed"); return NULL; }

    if (voiceprint_extract(model, mfcc, n_frames, vp) != 0) {
        free(vp);
        return NULL;
    }

    LOG_INFO("=== Enroll complete (%d frames) ===", vp->n_frames);
    return vp;
}

void voiceprint_free(voiceprint_t* vp) {
    free(vp);
}
