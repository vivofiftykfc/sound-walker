/**
 * gmm_verify.c - GMM/SVM voiceprint verification module
 *
 * Implements:
 *   1. GMM model structure (means, variances, weights)
 *   2. UBM loading function
 *   3. MAP adaptation (adapt UBM to speaker)
 *   4. Log-likelihood ratio computation
 *   5. Voiceprint verification function
 *
 * Uses config.h parameters:
 *   - GMM_N_COMPONENTS = 64
 *   - GMM_DIM = N_MFCC_COEFFS = 12
 *   - MAP_RELEVANCE_FACTOR = 16
 *   - VERIFICATION_THRESHOLD = -15.0f
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

/* ============================================================
 * GMM Model Structure
 * ============================================================ */

/**
 * GMM (Gaussian Mixture Model) parameters
 * Uses float type to save memory on embedded systems
 */
typedef struct {
    float means[GMM_N_COMPONENTS][GMM_DIM];      /* Mean vectors */
    float variances[GMM_N_COMPONENTS][GMM_DIM];  /* Variance values */
    float weights[GMM_N_COMPONENTS];             /* Mixture weights */
    int n_components;                            /* Number of components */
    int dim;                                     /* Feature dimension */
} gmm_model_t;

/**
 * Speaker-adapted GMM (after MAP adaptation)
 */
typedef struct {
    gmm_model_t model;
    int user_id;
    char username[MAX_NAME_LEN];
} speaker_gmm_t;

/* ============================================================
 * Internal Constants
 * ============================================================ */

#define GMM_MIN_VARIANCE  1e-6f    /* Floor for numerical stability */
#define GMM_LOG_2PI       2.88539f /* log(2 * PI) */
#define EPSILON           1e-10f

/* ============================================================
 * Utility Functions
 * ============================================================ */

/**
 * Compute log sum exp for numerical stability
 * Used when summing probabilities in log domain
 */
static float log_sum_exp(const float* values, int n) {
    if (n <= 0) return -INFINITY;

    /* Find maximum value for numerical stability */
    float max_val = values[0];
    for (int i = 1; i < n; i++) {
        if (values[i] > max_val) {
            max_val = values[i];
        }
    }

    /* Handle case where max_val is -infinity */
    if (!isfinite(max_val)) {
        return -INFINITY;
    }

    /* Sum exp(values[i] - max_val) */
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        if (isfinite(values[i])) {
            sum += expf(values[i] - max_val);
        }
    }

    return max_val + logf(sum + EPSILON);
}

/**
 * Compute log Gaussian probability for single component
 * log N(x|mu, sigma^2) = -0.5 * log(2*pi*sigma^2) - 0.5 * (x-mu)^2 / sigma^2
 */
static float log_gaussian_prob(float x, float mean, float variance) {
    if (variance < GMM_MIN_VARIANCE) {
        variance = GMM_MIN_VARIANCE;
    }

    float diff = x - mean;
    float log_prob = -0.5f * (GMM_LOG_2PI + logf(variance));
    log_prob -= 0.5f * (diff * diff) / variance;

    return log_prob;
}

/**
 * Compute GMM log-likelihood for one feature vector
 * log p(x|lambda) = log(sum_c w_c * N(x|mu_c, sigma_c^2))
 */
static float gmm_component_log_likelihood(const float* feature,
                                          const gmm_model_t* model) {
    float log_weights[GMM_N_COMPONENTS];

    for (int c = 0; c < model->n_components; c++) {
        float log_prob = 0.0f;

        /* Compute log probability for each dimension */
        for (int d = 0; d < model->dim; d++) {
            log_prob += log_gaussian_prob(feature[d],
                                          model->means[c][d],
                                          model->variances[c][d]);
        }

        /* Add log weight */
        log_weights[c] = log_prob + logf(model->weights[c] + EPSILON);
    }

    /* Sum in log domain using log-sum-exp */
    return log_sum_exp(log_weights, model->n_components);
}

/* ============================================================
 * UBM Loading Functions
 * ============================================================ */

/**
 * Initialize GMM model with default values
 */
static int gmm_model_init(gmm_model_t* model, int n_components, int dim) {
    if (!model || n_components <= 0 || dim <= 0) {
        LOG_ERROR("Invalid parameters for GMM init");
        return ERR_INVALID_PARAM;
    }

    memset(model, 0, sizeof(gmm_model_t));
    model->n_components = n_components;
    model->dim = dim;

    /* Initialize weights uniformly */
    float uniform_weight = 1.0f / n_components;
    for (int c = 0; c < n_components; c++) {
        model->weights[c] = uniform_weight;
    }

    /* Initialize variances to identity (large values for UBM) */
    for (int c = 0; c < n_components; c++) {
        for (int d = 0; d < dim; d++) {
            model->variances[c][d] = 1.0f;
        }
    }

    LOG_DEBUG("GMM model initialized: components=%d, dim=%d", n_components, dim);
    return ERR_OK;
}

/**
 * Load UBM from binary file
 *
 * File format:
 *   - int32: n_components
 *   - int32: dim
 *   - float[n_components][dim]: means
 *   - float[n_components][dim]: variances
 *   - float[n_components]: weights
 *
 * @param path Path to UBM model file
 * @param ubm Output UBM model
 * @return ERR_OK on success, error code on failure
 */
int ubm_load(const char* path, gmm_model_t* ubm) {
    if (!path || !ubm) {
        LOG_ERROR("Invalid parameters for UBM loading");
        return ERR_INVALID_PARAM;
    }

    FILE* fp = fopen(path, "rb");
    if (!fp) {
        LOG_ERROR("Failed to open UBM file: %s (errno=%d)", path, errno);
        return ERR_FILE_NOT_FOUND;
    }

    /* Read header */
    int32_t n_components, dim;
    if (fread(&n_components, sizeof(int32_t), 1, fp) != 1 ||
        fread(&dim, sizeof(int32_t), 1, fp) != 1) {
        LOG_ERROR("Failed to read UBM header");
        fclose(fp);
        return ERR_FILE_READ;
    }

    LOG_INFO("Loading UBM: components=%d, dim=%d", n_components, dim);

    /* Validate dimensions match config */
    if (n_components != GMM_N_COMPONENTS || dim != GMM_DIM) {
        LOG_ERROR("UBM dimensions mismatch: expected %dx%d, got %dx%d",
                  GMM_N_COMPONENTS, GMM_DIM, n_components, dim);
        fclose(fp);
        return ERR_MFCC_INIT;
    }

    /* Initialize model structure */
    if (gmm_model_init(ubm, n_components, dim) != ERR_OK) {
        fclose(fp);
        return ERR_MEMORY;
    }

    /* Read means */
    size_t mean_size = n_components * dim * sizeof(float);
    if (fread(ubm->means, 1, mean_size, fp) != mean_size) {
        LOG_ERROR("Failed to read UBM means");
        fclose(fp);
        return ERR_FILE_READ;
    }

    /* Read variances */
    size_t var_size = n_components * dim * sizeof(float);
    if (fread(ubm->variances, 1, var_size, fp) != var_size) {
        LOG_ERROR("Failed to read UBM variances");
        fclose(fp);
        return ERR_FILE_READ;
    }

    /* Read weights */
    if (fread(ubm->weights, sizeof(float), n_components, fp) != (size_t)n_components) {
        LOG_ERROR("Failed to read UBM weights");
        fclose(fp);
        return ERR_FILE_READ;
    }

    fclose(fp);

    LOG_INFO("UBM loaded successfully from: %s", path);
    LOG_DEBUG("UBM means[0][0]=%f, variances[0][0]=%f, weights[0]=%f",
              ubm->means[0][0], ubm->variances[0][0], ubm->weights[0]);

    return ERR_OK;
}

/**
 * Save GMM model to binary file
 */
int gmm_save(const char* path, const gmm_model_t* model) {
    if (!path || !model) {
        LOG_ERROR("Invalid parameters for GMM save");
        return ERR_INVALID_PARAM;
    }

    FILE* fp = fopen(path, "wb");
    if (!fp) {
        LOG_ERROR("Failed to create file: %s (errno=%d)", path, errno);
        return ERR_FILE_WRITE;
    }

    /* Write header */
    int32_t n_components = model->n_components;
    int32_t dim = model->dim;
    if (fwrite(&n_components, sizeof(int32_t), 1, fp) != 1 ||
        fwrite(&dim, sizeof(int32_t), 1, fp) != 1) {
        LOG_ERROR("Failed to write GMM header");
        fclose(fp);
        return ERR_FILE_WRITE;
    }

    /* Write means */
    size_t mean_size = n_components * dim * sizeof(float);
    if (fwrite(model->means, 1, mean_size, fp) != mean_size) {
        LOG_ERROR("Failed to write GMM means");
        fclose(fp);
        return ERR_FILE_WRITE;
    }

    /* Write variances */
    size_t var_size = n_components * dim * sizeof(float);
    if (fwrite(model->variances, 1, var_size, fp) != var_size) {
        LOG_ERROR("Failed to write GMM variances");
        fclose(fp);
        return ERR_FILE_WRITE;
    }

    /* Write weights */
    if (fwrite(model->weights, sizeof(float), n_components, fp) != (size_t)n_components) {
        LOG_ERROR("Failed to write GMM weights");
        fclose(fp);
        return ERR_FILE_WRITE;
    }

    fclose(fp);
    LOG_INFO("GMM saved to: %s", path);
    return ERR_OK;
}

/* ============================================================
 * MAP Adaptation Functions
 * ============================================================ */

/**
 * Compute posterior responsibilities for each component
 * gamma_c(x) = w_c * N(x|mu_c, sigma_c) / sum_k(w_k * N(x|mu_k, sigma_k))
 *
 * @param feature Input feature vector (GMM_DIM)
 * @param model GMM model
 * @param responsibilities Output: posterior responsibilities [n_components]
 */
static void compute_responsibilities(const float* feature,
                                     const gmm_model_t* model,
                                     float* responsibilities) {
    float log_weights[GMM_N_COMPONENTS];

    /* Compute unnormalized log responsibilities */
    for (int c = 0; c < model->n_components; c++) {
        float log_prob = 0.0f;
        for (int d = 0; d < model->dim; d++) {
            log_prob += log_gaussian_prob(feature[d],
                                          model->means[c][d],
                                          model->variances[c][d]);
        }
        log_weights[c] = log_prob + logf(model->weights[c] + EPSILON);
    }

    /* Normalize using log-sum-exp */
    float log_sum = log_sum_exp(log_weights, model->n_components);

    /* Convert to linear domain and store */
    for (int c = 0; c < model->n_components; c++) {
        responsibilities[c] = expf(log_weights[c] - log_sum);
    }
}

/**
 * Compute sufficient statistics from feature frames
 *
 * @param features Feature matrix [n_frames x GMM_DIM]
 * @param n_frames Number of frames
 * @param model GMM model
 * @param N Output: component occupation counts [n_components]
 * @param F Output: weighted sum of features [n_components x GMM_DIM]
 */
static void compute_sufficient_stats(const float* features, int n_frames,
                                     const gmm_model_t* model,
                                     float* N, float* F) {
    /* Initialize outputs */
    memset(N, 0, model->n_components * sizeof(float));
    memset(F, 0, model->n_components * model->dim * sizeof(float));

    /* Accumulate statistics for each frame */
    for (int f = 0; f < n_frames; f++) {
        const float* frame = &features[f * model->dim];
        float responsibilities[GMM_N_COMPONENTS];

        compute_responsibilities(frame, model, responsibilities);

        /* Update sufficient statistics */
        for (int c = 0; c < model->n_components; c++) {
            N[c] += responsibilities[c];
            for (int d = 0; d < model->dim; d++) {
                F[c * model->dim + d] += responsibilities[c] * frame[d];
            }
        }
    }
}

/**
 * MAP adaptation of GMM means to speaker
 *
 * Uses relevance MAP adaptation:
 * mu_speaker = (1 - alpha) * mu_ubm + alpha * mu_adapt
 * where alpha = relevance_factor / (relevance_factor + N_c)
 *
 * @param ubm Universal Background Model
 * @param features Training feature frames [n_frames x GMM_DIM]
 * @param n_frames Number of frames
 * @param adapted Output: adapted GMM model
 * @param relevance_factor MAP relevance factor (tau, typically 10-20)
 * @return ERR_OK on success, error code on failure
 */
int map_adapt(const gmm_model_t* ubm, const float* features, int n_frames,
              gmm_model_t* adapted, int relevance_factor) {
    if (!ubm || !features || !adapted || n_frames <= 0) {
        LOG_ERROR("Invalid parameters for MAP adaptation");
        return ERR_INVALID_PARAM;
    }

    if (relevance_factor <= 0) {
        LOG_ERROR("Invalid relevance factor: %d", relevance_factor);
        return ERR_INVALID_PARAM;
    }

    LOG_INFO("Starting MAP adaptation: n_frames=%d, relevance_factor=%d",
             n_frames, relevance_factor);
    LOG_DEBUG("UBM weights[0]=%f, means[0][0]=%f", ubm->weights[0], ubm->means[0][0]);

    /* Copy UBM structure as base */
    memcpy(adapted, ubm, sizeof(gmm_model_t));

    /* Compute sufficient statistics */
    float* N = malloc(ubm->n_components * sizeof(float));
    float* F = malloc(ubm->n_components * ubm->dim * sizeof(float));

    if (!N || !F) {
        LOG_ERROR("Failed to allocate memory for sufficient statistics");
        free(N);
        free(F);
        return ERR_MEMORY;
    }

    compute_sufficient_stats(features, n_frames, ubm, N, F);

    LOG_DEBUG("Sufficient stats computed: N[0]=%f, F[0]=%f", N[0], F[0]);

    /* Apply MAP adaptation to means only */
    for (int c = 0; c < ubm->n_components; c++) {
        /* Compute adaptation coefficient */
        float alpha_c = (float)relevance_factor / (relevance_factor + N[c] + EPSILON);

        LOG_DEBUG("Component %d: N=%f, alpha=%f", c, N[c], alpha_c);

        /* Adapt each dimension of the mean */
        for (int d = 0; d < ubm->dim; d++) {
            float mu_ubm = ubm->means[c][d];
            float mu_adapt = (N[c] > EPSILON) ? (F[c * ubm->dim + d] / N[c]) : mu_ubm;

            adapted->means[c][d] = (1.0f - alpha_c) * mu_ubm + alpha_c * mu_adapt;
        }
    }

    free(N);
    free(F);

    LOG_INFO("MAP adaptation completed");
    LOG_DEBUG("Adapted means[0][0]=%f (was %f)", adapted->means[0][0], ubm->means[0][0]);

    return ERR_OK;
}

/* ============================================================
 * Log-Likelihood Ratio Computation
 * ============================================================ */

/**
 * Compute log-likelihood of features given a GMM model
 *
 * @param features Feature matrix [n_frames x GMM_DIM]
 * @param n_frames Number of frames
 * @param model GMM model
 * @return Log-likelihood value
 */
static float compute_gmm_log_likelihood(const float* features, int n_frames,
                                        const gmm_model_t* model) {
    float total_ll = 0.0f;

    for (int f = 0; f < n_frames; f++) {
        const float* frame = &features[f * model->dim];
        float frame_ll = gmm_component_log_likelihood(frame, model);
        total_ll += frame_ll;
    }

    return total_ll;
}

/**
 * Compute log-likelihood ratio
 * LLR = log p(X | speaker_model) - log p(X | UBM)
 *
 * Higher LLR indicates better match to speaker
 *
 * @param features Feature matrix [n_frames x GMM_DIM]
 * @param n_frames Number of frames
 * @param speaker_model Speaker-adapted GMM
 * @param ubm Universal Background Model
 * @return Log-likelihood ratio
 */
float compute_llr(const float* features, int n_frames,
                  const gmm_model_t* speaker_model,
                  const gmm_model_t* ubm) {
    if (!features || !speaker_model || !ubm || n_frames <= 0) {
        LOG_ERROR("Invalid parameters for LLR computation");
        return -INFINITY;
    }

    /* Compute log-likelihood for speaker model */
    float ll_speaker = compute_gmm_log_likelihood(features, n_frames, speaker_model);

    /* Compute log-likelihood for UBM */
    float ll_ubm = compute_gmm_log_likelihood(features, n_frames, ubm);

    /* Normalize by number of frames for comparability */
    float llr = (ll_speaker - ll_ubm) / n_frames;

    LOG_DEBUG("LLR computation: ll_speaker=%.2f, ll_ubm=%.2f, llr=%.4f",
              ll_speaker, ll_ubm, llr);

    return llr;
}

/* ============================================================
 * Speaker GMM Management
 * ============================================================ */

/**
 * Create speaker GMM by adapting UBM with enrollment features
 *
 * @param ubm Universal Background Model
 * @param enrollment_features Feature matrix from enrollment utterances [n_frames x GMM_DIM]
 * @param n_frames Number of frames
 * @param user_id User identifier
 * @param username User name
 * @return Speaker GMM or NULL on failure
 */
speaker_gmm_t* speaker_gmm_create(const gmm_model_t* ubm,
                                  const float* enrollment_features,
                                  int n_frames, int user_id,
                                  const char* username) {
    if (!ubm || !enrollment_features || n_frames <= 0) {
        LOG_ERROR("Invalid parameters for speaker GMM creation");
        return NULL;
    }

    speaker_gmm_t* speaker = malloc(sizeof(speaker_gmm_t));
    if (!speaker) {
        LOG_ERROR("Failed to allocate speaker GMM");
        return NULL;
    }

    memset(speaker, 0, sizeof(speaker_gmm_t));
    speaker->user_id = user_id;

    if (username) {
        strncpy(speaker->username, username, MAX_NAME_LEN - 1);
        speaker->username[MAX_NAME_LEN - 1] = '\0';
    }

    /* Perform MAP adaptation */
    int ret = map_adapt(ubm, enrollment_features, n_frames,
                        &speaker->model, MAP_RELEVANCE_FACTOR);

    if (ret != ERR_OK) {
        LOG_ERROR("MAP adaptation failed: %s", err_str(ret));
        free(speaker);
        return NULL;
    }

    LOG_INFO("Speaker GMM created for user %d (%s)", user_id,
             username ? username : "unknown");

    return speaker;
}

/**
 * Free speaker GMM
 */
void speaker_gmm_free(speaker_gmm_t* speaker) {
    if (speaker) {
        free(speaker);
    }
}

/* ============================================================
 * Voiceprint Verification
 * ============================================================ */

/**
 * Verify voiceprint against enrolled speaker
 *
 * @param ubm Universal Background Model
 * @param speaker Speaker GMM to verify against
 * @param test_features Feature matrix from test utterance [n_frames x GMM_DIM]
 * @param n_frames Number of frames
 * @param score Output: verification score (log-likelihood ratio per frame)
 * @return VERIFY_RESULT_MATCH if score >= threshold, VERIFY_RESULT_NO_MATCH otherwise
 */
int voice_verify(const gmm_model_t* ubm, const speaker_gmm_t* speaker,
                 const float* test_features, int n_frames, float* score) {
    if (!ubm || !speaker || !test_features || n_frames <= 0) {
        LOG_ERROR("Invalid parameters for voice verification");
        return ERR_INVALID_PARAM;
    }

    /* Compute log-likelihood ratio */
    float llr = compute_llr(test_features, n_frames, &speaker->model, ubm);

    /* Store score (normalized per frame) */
    if (score) {
        *score = llr;
    }

    LOG_INFO("Voice verification score: %.4f (threshold: %.4f)",
             llr, VERIFICATION_THRESHOLD);

    /* Compare against threshold */
    if (llr >= VERIFICATION_THRESHOLD) {
        LOG_INFO("Verification PASSED for user %d (%s)",
                 speaker->user_id, speaker->username);
        return ERR_OK;  /* Match */
    } else {
        LOG_INFO("Verification FAILED for user %d (%s)",
                 speaker->user_id, speaker->username);
        return ERR_MISMATCH;  /* No match */
    }
}

/**
 * Verify and get detailed score
 *
 * @param ubm Universal Background Model
 * @param speaker Speaker GMM to verify against
 * @param test_features Feature matrix from test utterance
 * @param n_frames Number of frames
 * @param llr_speaker Output: speaker log-likelihood
 * @param llr_ubm Output: UBM log-likelihood
 * @param score Output: final verification score
 * @return ERR_OK if match, ERR_MISMATCH if no match, error code on failure
 */
int voice_verify_detailed(const gmm_model_t* ubm, const speaker_gmm_t* speaker,
                          const float* test_features, int n_frames,
                          float* llr_speaker, float* llr_ubm, float* score) {
    if (!ubm || !speaker || !test_features || n_frames <= 0) {
        LOG_ERROR("Invalid parameters for detailed verification");
        return ERR_INVALID_PARAM;
    }

    /* Compute individual log-likelihoods */
    float speaker_ll = compute_gmm_log_likelihood(test_features, n_frames, &speaker->model);
    float ubm_ll = compute_gmm_log_likelihood(test_features, n_frames, ubm);

    if (llr_speaker) *llr_speaker = speaker_ll / n_frames;
    if (llr_ubm) *llr_ubm = ubm_ll / n_frames;

    /* Compute final score */
    float final_score = (speaker_ll - ubm_ll) / n_frames;
    if (score) *score = final_score;

    LOG_INFO("Detailed verification:");
    LOG_INFO("  Speaker LL: %.4f (per frame: %.4f)", speaker_ll, speaker_ll / n_frames);
    LOG_INFO("  UBM LL:     %.4f (per frame: %.4f)", ubm_ll, ubm_ll / n_frames);
    LOG_INFO("  LLR Score:  %.4f (threshold: %.4f)", final_score, VERIFICATION_THRESHOLD);

    if (final_score >= VERIFICATION_THRESHOLD) {
        return ERR_OK;
    } else {
        return ERR_MISMATCH;
    }
}

/* ============================================================
 * Debug and Utility Functions
 * ============================================================ */

#ifdef DEBUG

/**
 * Print GMM model parameters (debug only)
 */
void gmm_model_print(const gmm_model_t* model, const char* label) {
    if (!model) return;

    LOG_INFO("========== GMM Model: %s ==========", label ? label : "unnamed");
    LOG_INFO("Components: %d, Dimension: %d", model->n_components, model->dim);

    /* Print first component details */
    LOG_INFO("Component 0:");
    LOG_INFO("  Weight: %.6f", model->weights[0]);
    LOG_INFO("  Mean[0..3]: %.4f, %.4f, %.4f, %.4f",
             model->means[0][0], model->means[0][1],
             model->means[0][2], model->means[0][3]);
    LOG_INFO("  Var[0..3]:  %.4f, %.4f, %.4f, %.4f",
             model->variances[0][0], model->variances[0][1],
             model->variances[0][2], model->variances[0][3]);

    /* Print weight sum as sanity check */
    float weight_sum = 0.0f;
    for (int c = 0; c < model->n_components; c++) {
        weight_sum += model->weights[c];
    }
    LOG_INFO("Weight sum: %.6f", weight_sum);
}

#else
#define gmm_model_print(m, l) ((void)0)  /* No-op in release */
#endif

/**
 * Get verification threshold
 */
float voice_verify_get_threshold(void) {
    return VERIFICATION_THRESHOLD;
}

/**
 * Compute minimum memory required for GMM operations
 */
size_t gmm_get_memory_requirement(void) {
    /* Sufficient statistics buffers */
    size_t n_size = GMM_N_COMPONENTS * sizeof(float);
    size_t f_size = GMM_N_COMPONENTS * GMM_DIM * sizeof(float);
    size_t resp_size = GMM_N_COMPONENTS * sizeof(float);
    size_t log_weight_size = GMM_N_COMPONENTS * sizeof(float);

    return n_size + f_size + resp_size + log_weight_size;
}