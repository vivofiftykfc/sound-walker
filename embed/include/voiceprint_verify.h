/**
 * voiceprint_verify.h - Speaker verification using sherpa-onnx CAM++
 *
 * Uses pre-trained CAM++ model via sherpa-onnx C API.
 * Fully offline, on-device inference.
 *
 * Pipeline:
 *   16kHz raw audio → CAM++ model → 512-dim embedding
 *   → cosine similarity → threshold → match/no-match
 */

#ifndef VOICEPRINT_VERIFY_H
#define VOICEPRINT_VERIFY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPEAKER_EMBEDDING_DIM  512

typedef enum {
    VERIFY_RESULT_UNKNOWN = 0,
    VERIFY_RESULT_MATCH,
    VERIFY_RESULT_NO_MATCH
} verify_result_t;

typedef struct {
    int user_id;
    float embedding[SPEAKER_EMBEDDING_DIM];
} voiceprint_t;

typedef struct {
    void* extractor;    /* SherpaOnnxSpeakerEmbeddingExtractor* */
    void* manager;      /* SherpaOnnxSpeakerEmbeddingManager* */
    int dim;
    float threshold;    /* cosine similarity threshold */
} voiceprint_model_t;

voiceprint_model_t* voiceprint_init(const char* model_path);
void voiceprint_model_free(voiceprint_model_t* model);

int voiceprint_extract(voiceprint_model_t* model,
                       const float* samples, int n_samples,
                       voiceprint_t* vp);

voiceprint_t* voiceprint_enroll(voiceprint_model_t* model,
                                 const float* samples, int n_samples);

verify_result_t voiceprint_verify(voiceprint_model_t* model,
                                   const float* test_samples, int n_samples,
                                   const voiceprint_t* enrolled,
                                   float* score_out);

void voiceprint_free(voiceprint_t* vp);

#ifdef __cplusplus
}
#endif

#endif /* VOICEPRINT_VERIFY_H */
