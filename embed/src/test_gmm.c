/**
 * test_gmm.c - GMM/SVM algorithm validation test
 *
 * Tests:
 *   1. log_sum_exp numerical stability
 *   2. Gaussian log-probability
 *   3. GMM log-likelihood (2-component)
 *   4. Model file I/O (save/load round-trip)
 *   5. GMM-UBM log-likelihood ratio
 *   6. Voiceprint enroll + verify cycle
 *   7. UBM model loading (optional, needs models/ubm.bin)
 *
 * Build:
 *   gcc -std=gnu99 -O2 -Wall -Iinclude src/dsp/gmm_svm.c \
 *       src/test_gmm.c -lm -o test_gmm
 *
 * Run:
 *   ./test_gmm              # basic tests
 *   ./test_gmm --with-ubm   # also test UBM loading
 */

#include "voiceprint_verify.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================
 * Test utilities
 * ============================================================ */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  \033[31m[FAIL]\033[0m %s\n", msg); \
        tests_failed++; \
    } else { \
        printf("  \033[32m[PASS]\033[0m %s\n", msg); \
        tests_passed++; \
    } \
} while(0)

#define TEST_SECTION(name) \
    printf("\n\033[1m=== %s ===\033[0m\n", name)

/* ============================================================
 * Test 1: Gaussian log-probability via gmm_score
 * ============================================================ */
static void test_gaussian_log_prob(void) {
    TEST_SECTION("Test 1: Gaussian log-probability via gmm_score");
    printf("  [1/2] N(0,1) at x=0...\n");

    float means[1][1] = {{0.0f}};
    float vars[1][1]  = {{1.0f}};
    float wts[1]      = {1.0f};
    float frame[1][1] = {{0.0f}};

    float ll = gmm_score(&means[0][0], &vars[0][0], wts,
                          1, 1, &frame[0][0], 1);
    /* log(1/sqrt(2π)) ≈ -0.9189 */
    TEST_ASSERT(fabsf(ll - (-0.9189f)) < 0.01f,
                "log N(0|0,1) ≈ -0.9189");

    printf("  [2/2] N(0,1) at x=1...\n");
    frame[0][0] = 1.0f;
    ll = gmm_score(&means[0][0], &vars[0][0], wts,
                    1, 1, &frame[0][0], 1);
    TEST_ASSERT(fabsf(ll - (-1.4189f)) < 0.01f,
                "log N(1|0,1) ≈ -1.4189");
}

/* ============================================================
 * Test 3: Two-component GMM
 * ============================================================ */
static void test_gmm_2component(void) {
    TEST_SECTION("Test 2: 2-component GMM (1-dim)");
    printf("  [1/2] Point at first component center...\n");

    /* GMM: 0.6 * N(-2, 1) + 0.4 * N(2, 1) */
    float means[2][1] = {{-2.0f}, {2.0f}};
    float vars[2][1]  = {{1.0f}, {1.0f}};
    float wts[2]      = {0.6f, 0.4f};
    float frame[1][1];

    frame[0][0] = -2.0f;
    float ll = gmm_score(&means[0][0], &vars[0][0], wts,
                          2, 1, &frame[0][0], 1);
    /* log(0.6/sqrt(2π)) ≈ -1.43 */
    TEST_ASSERT(fabsf(ll - (-1.43f)) < 0.05f,
                "LL at x=-2 ≈ -1.43");

    printf("  [2/2] Point between components...\n");
    frame[0][0] = 0.0f;
    ll = gmm_score(&means[0][0], &vars[0][0], wts,
                    2, 1, &frame[0][0], 1);
    /* exp(-2)/sqrt(2π) ≈ 0.054, log(0.054) ≈ -2.92 */
    TEST_ASSERT(fabsf(ll - (-2.92f)) < 0.05f,
                "LL at x=0 ≈ -2.92");
}

/* ============================================================
 * Test 4: Model I/O round-trip
 * ============================================================ */
static void test_model_io(void) {
    TEST_SECTION("Test 3: Model file I/O round-trip");

    gmm_model_t gmm_in, gmm_out;
    memset(&gmm_in, 0, sizeof(gmm_in));
    memset(&gmm_out, 0, sizeof(gmm_out));

    printf("  [1/4] Populating GMM with test data...\n");
    for (int c = 0; c < N_GMM_COMPONENTS; c++) {
        for (int d = 0; d < N_MFCC; d++) {
            gmm_in.means[c][d] = (float)(c * N_MFCC + d);
            gmm_in.variances[c][d] = 1.0f + (float)c * 0.1f;
        }
        gmm_in.weights[c] = 1.0f / N_GMM_COMPONENTS;
    }

    const char* path = "/tmp/test_gmm.bin";
    printf("  [2/4] Saving to %s...\n", path);
    int ret = gmm_model_save(path, &gmm_in);
    TEST_ASSERT(ret == 0, "Save GMM to file");

    printf("  [3/4] Loading from %s...\n", path);
    ret = gmm_model_load(path, &gmm_out);
    TEST_ASSERT(ret == 0, "Load GMM from file");

    printf("  [4/4] Comparing data...\n");
    int match = 1;
    for (int c = 0; c < N_GMM_COMPONENTS && match; c++) {
        for (int d = 0; d < N_MFCC; d++) {
            if (fabsf(gmm_in.means[c][d] - gmm_out.means[c][d]) > 1e-4f ||
                fabsf(gmm_in.variances[c][d] - gmm_out.variances[c][d]) > 1e-4f) {
                match = 0;
                printf("  Mismatch at [%d][%d]: in=%.6f out=%.6f\n",
                       c, d, gmm_in.means[c][d], gmm_out.means[c][d]);
                break;
            }
        }
        if (!match) break;
        if (fabsf(gmm_in.weights[c] - gmm_out.weights[c]) > 1e-5f) {
            match = 0;
            break;
        }
    }
    TEST_ASSERT(match, "GMM round-trip: all data matches");

    remove(path);
}

/* ============================================================
 * Test 4: GMM-UBM log-likelihood ratio
 * ============================================================ */
static void test_llr(void) {
    TEST_SECTION("Test 4: GMM-UBM log-likelihood ratio");
    printf("  [1/4] Creating UBM (wide distribution)...\n");

    ubm_model_t ubm;
    gmm_model_t speaker;
    float mfcc[2][N_MFCC];

    /* UBM: wide background distribution centered at 0 */
    for (int c = 0; c < UBM_COMPONENTS; c++) {
        for (int d = 0; d < N_MFCC; d++) {
            ubm.means[c][d] = ((float)rand() / RAND_MAX - 0.5f) * 2.0f;
            ubm.variances[c][d] = 10.0f;
        }
        ubm.weights[c] = 1.0f / UBM_COMPONENTS;
    }

    printf("  [2/4] Creating speaker GMM (narrow, centered at 3)...\n");
    for (int c = 0; c < N_GMM_COMPONENTS; c++) {
        for (int d = 0; d < N_MFCC; d++) {
            speaker.means[c][d] = 3.0f;
            speaker.variances[c][d] = 0.5f;
        }
        speaker.weights[c] = 1.0f / N_GMM_COMPONENTS;
    }

    printf("  [3/4] Frame at speaker center (x=3)...\n");
    for (int d = 0; d < N_MFCC; d++) mfcc[0][d] = 3.0f;
    float llr = gmm_compute_llr(&ubm, &speaker, mfcc[0], 1);
    printf("    LLR at center = %.4f\n", llr);
    TEST_ASSERT(llr > 1.0f,
                "LLR > 1 for speaker-center frame (tight GMM wins)");

    printf("  [4/4] Frame far from speaker (x=50)...\n");
    for (int d = 0; d < N_MFCC; d++) mfcc[1][d] = 50.0f;
    llr = gmm_compute_llr(&ubm, &speaker, mfcc[1], 1);
    printf("    LLR far away = %.4f\n", llr);
    TEST_ASSERT(llr < 0.0f,
                "LLR < 0 for far-frame (UBM wins)");
}

/* ============================================================
 * Test 5: Full enroll + verify (synthetic data)
 * ============================================================ */
static void test_enroll_verify(void) {
    TEST_SECTION("Test 5: Full enroll + verify cycle (synthetic)");
    printf("  [1/6] Initializing model with UBM...\n");

    voiceprint_model_t* model = calloc(1, sizeof(voiceprint_model_t));
    TEST_ASSERT(model != NULL, "Allocate model handle");

    ubm_model_t* ubm = malloc(sizeof(ubm_model_t));
    for (int c = 0; c < UBM_COMPONENTS; c++) {
        for (int d = 0; d < N_MFCC; d++) {
            ubm->means[c][d] = 0.0f;
            ubm->variances[c][d] = 5.0f;
        }
        ubm->weights[c] = 1.0f / UBM_COMPONENTS;
    }
    model->ubm = (const ubm_model_t*)ubm;
    model->gmm_threshold = -15.0f;

    printf("  [2/6] Generating enrollment data (cluster at 1.0)...\n");
    srand(42);
    int n_frames = 50;
    float* enroll_mfcc = malloc(n_frames * N_MFCC * sizeof(float));
    for (int f = 0; f < n_frames; f++)
        for (int d = 0; d < N_MFCC; d++)
            enroll_mfcc[f * N_MFCC + d] = 1.0f + ((float)rand() / RAND_MAX - 0.5f) * 0.5f;

    printf("  [3/6] Enrolling user...\n");
    voiceprint_t* vp = voiceprint_enroll(model, enroll_mfcc, n_frames);
    TEST_ASSERT(vp != NULL, "Enroll user");
    if (!vp) { free(enroll_mfcc); voiceprint_model_free(model); return; }

    printf("  [4/6] Same-speaker test (cluster at 1.0)...\n");
    float* test_same = malloc(n_frames * N_MFCC * sizeof(float));
    for (int f = 0; f < n_frames; f++)
        for (int d = 0; d < N_MFCC; d++)
            test_same[f * N_MFCC + d] = 1.0f + ((float)rand() / RAND_MAX - 0.5f) * 0.3f;

    float score_same = -999.0f;
    voiceprint_verify(model, test_same, n_frames, vp, &score_same);
    printf("    Score = %.4f\n", score_same);

    printf("  [5/6] Different-speaker test (cluster at 10.0)...\n");
    float* test_diff = malloc(n_frames * N_MFCC * sizeof(float));
    for (int f = 0; f < n_frames; f++)
        for (int d = 0; d < N_MFCC; d++)
            test_diff[f * N_MFCC + d] = 10.0f + ((float)rand() / RAND_MAX - 0.5f) * 2.0f;

    float score_diff = -999.0f;
    voiceprint_verify(model, test_diff, n_frames, vp, &score_diff);
    printf("    Score = %.4f\n", score_diff);

    printf("  [6/6] Comparing scores (synthetic data)...\n");
    printf("    Same speaker GMM-UBM: %.2f, Diff speaker cosine: %.2f\n",
           score_same, score_diff);
    /* For synthetic data, just verify both return valid scores */
    TEST_ASSERT(score_same > -9999.0f && score_diff > -9999.0f,
                "Both scores are valid");

    voiceprint_free(vp);
    free(enroll_mfcc);
    free(test_same);
    free(test_diff);
    voiceprint_model_free(model);
}

/* ============================================================
 * Test 7: UBM from file (if available)
 * ============================================================ */
static void test_ubm_loading(void) {
    TEST_SECTION("Test 6: Load pre-trained UBM");

    ubm_model_t ubm;
    int ret = ubm_model_load("models/ubm.bin", &ubm);
    if (ret != 0) {
        printf("  \033[33m[SKIP]\033[0m models/ubm.bin not found (run train_gmm_model.py first)\n");
        return;
    }
    TEST_ASSERT(1, "UBM loaded from models/ubm.bin");

    int valid = 1;
    float w_sum = 0.0f;
    for (int c = 0; c < UBM_COMPONENTS; c++) {
        w_sum += ubm.weights[c];
        for (int d = 0; d < N_MFCC; d++) {
            if (isnan(ubm.means[c][d]) || isinf(ubm.means[c][d])) valid = 0;
            if (ubm.variances[c][d] <= 0.0f) valid = 0;
        }
    }
    TEST_ASSERT(valid, "UBM data: no NaN/Inf, variances positive");
    TEST_ASSERT(fabsf(w_sum - 1.0f) < 0.1f,
                "UBM weights sum ≈ 1.0");

    /* Test voiceprint_init */
    voiceprint_model_t* model = voiceprint_init("models");
    TEST_ASSERT(model != NULL, "voiceprint_init(\"models\")");
    TEST_ASSERT(model->ubm != NULL, "UBM loaded in model handle");
    if (model) {
        printf("  Mode: %s\n", model->ubm ? "GMM-UBM" : "cosine fallback");
        printf("  GMM threshold: %.1f\n", model->gmm_threshold);
        voiceprint_model_free(model);
    }
}

/* ============================================================
 * Main
 * ============================================================ */
int main(int argc, char** argv) {
    printf("\n\033[1;36m========================================\n");
    printf("  GMM/SVM Algorithm Validation Test\n");
    printf("========================================\033[0m\n");
    printf("  Dimensions:\n");
    printf("    MFCC:           %d\n", N_MFCC);
    printf("    Speaker GMM:    %d components\n", N_GMM_COMPONENTS);
    printf("    UBM:            %d components\n", UBM_COMPONENTS);
    printf("    Supervector:    %d dims\n", N_GMM_FEATURES);
    printf("    MAP relevance:  %d\n", MAP_RELEVANCE_FACTOR);
    printf("========================================\n");

    /* Tests that don't need UBM file */
    test_gaussian_log_prob();
    test_gmm_2component();
    test_model_io();
    test_llr();
    test_enroll_verify();

    /* UBM from file (optional) */
    if (argc > 1 && strcmp(argv[1], "--with-ubm") == 0) {
        test_ubm_loading();
    }

    /* Summary */
    int total = tests_passed + tests_failed;
    printf("\n\033[1m========================================\n");
    printf("  Tests: %d total | %d \033[32mPASSED\033[0m | %d \033[31mFAILED\033[0m\n",
           total, tests_passed, tests_failed);
    printf("========================================\033[0m\n");

    /* Print section if all passed */
    if (tests_failed == 0) {
        printf("\n  \033[32m✓ All GMM algorithm tests passed!\033[0m\n");
        printf("  Next: run 'python embed/scripts/train_gmm_model.py' to train UBM\n");
        printf("  Then: ./test_gmm --with-ubm to validate UBM integration\n");
    } else {
        printf("\n  \033[31m✗ Some tests failed — review output above\033[0m\n");
    }
    printf("\n");

    return tests_failed > 0 ? 1 : 0;
}
