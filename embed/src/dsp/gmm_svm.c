
#include "voiceprint_verify.h"
#include "config.h"
#include "sherpa-onnx/c-api/c-api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


static const float* compute_embedding(
    const SherpaOnnxSpeakerEmbeddingExtractor* ex,
    const float* samples, int n_samples) {

    const SherpaOnnxOnlineStream* stream =
        SherpaOnnxSpeakerEmbeddingExtractorCreateStream(ex);
    if (!stream) return NULL;

    SherpaOnnxOnlineStreamAcceptWaveform(stream, SAMPLE_RATE, samples, n_samples);
    SherpaOnnxOnlineStreamInputFinished(stream);

    if (!SherpaOnnxSpeakerEmbeddingExtractorIsReady(ex, stream)) {
        LOG_WARN("Audio too short (%d samples, need ~0.5s)", n_samples);
        SherpaOnnxDestroyOnlineStream(stream);
        return NULL;
    }

    const float* emb = SherpaOnnxSpeakerEmbeddingExtractorComputeEmbedding(ex, stream);
    SherpaOnnxDestroyOnlineStream(stream);
    return emb;
}



voiceprint_model_t* voiceprint_init(const char* model_path) {
    LOG_INFO("=== Voiceprint init (sherpa-onnx CAM++) ===");

    char model_file[512];
    if (model_path)
        snprintf(model_file, sizeof(model_file),
                 "%s/3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx", model_path);
    else
        snprintf(model_file, sizeof(model_file), "models/3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx");

    LOG_INFO("Loading model: %s", model_file);

    SherpaOnnxSpeakerEmbeddingExtractorConfig config;
    memset(&config, 0, sizeof(config));
    config.model = model_file;
    config.num_threads = 2;
    config.debug = 0;
    config.provider = "cpu";

    const SherpaOnnxSpeakerEmbeddingExtractor* extractor =
        SherpaOnnxCreateSpeakerEmbeddingExtractor(&config);
    if (!extractor) {
        LOG_ERROR("Failed to load model: %s", model_file);
        LOG_ERROR("Download from: https://github.com/k2-fsa/sherpa-onnx/releases");
        return NULL;
    }

    int32_t dim = SherpaOnnxSpeakerEmbeddingExtractorDim(extractor);
    LOG_INFO("Model loaded: dim=%d", dim);

    const SherpaOnnxSpeakerEmbeddingManager* manager =
        SherpaOnnxCreateSpeakerEmbeddingManager(dim);
    if (!manager) {
        SherpaOnnxDestroySpeakerEmbeddingExtractor(extractor);
        return NULL;
    }

    voiceprint_model_t* model = calloc(1, sizeof(voiceprint_model_t));
    if (!model) {
        SherpaOnnxDestroySpeakerEmbeddingManager(manager);
        SherpaOnnxDestroySpeakerEmbeddingExtractor(extractor);
        return NULL;
    }

    model->extractor = (void*)extractor;
    model->manager = (void*)manager;
    model->dim = dim;
    model->threshold = 0.55f;
    LOG_INFO("=== Init complete (threshold=%.2f) ===", model->threshold);
    return model;
}

void voiceprint_model_free(voiceprint_model_t* model) {
    if (!model) return;
    if (model->manager)
        SherpaOnnxDestroySpeakerEmbeddingManager(
            (const SherpaOnnxSpeakerEmbeddingManager*)model->manager);
    if (model->extractor)
        SherpaOnnxDestroySpeakerEmbeddingExtractor(
            (const SherpaOnnxSpeakerEmbeddingExtractor*)model->extractor);
    free(model);
}

int voiceprint_extract(voiceprint_model_t* model,
                       const float* samples, int n_samples,
                       voiceprint_t* vp) {
    if (!model || !samples || !vp || n_samples <= 0) return -1;

    LOG_INFO("=== Extract embedding: %d samples ===", n_samples);

    const SherpaOnnxSpeakerEmbeddingExtractor* ex =
        (const SherpaOnnxSpeakerEmbeddingExtractor*)model->extractor;

    const float* emb = compute_embedding(ex, samples, n_samples);
    if (!emb) {
        LOG_ERROR("Embedding extraction failed");
        return -1;
    }

    memcpy(vp->embedding, emb, model->dim * sizeof(float));
    SherpaOnnxSpeakerEmbeddingExtractorDestroyEmbedding(emb);
    vp->user_id = -1;
    LOG_INFO("=== Embedding extracted ===");
    return 0;
}

voiceprint_t* voiceprint_enroll(voiceprint_model_t* model,
                                 const float* samples, int n_samples) {
    if (!model || !samples || n_samples <= 0) return NULL;
    LOG_INFO("=== Enroll ===");
    voiceprint_t* vp = calloc(1, sizeof(voiceprint_t));
    if (!vp) return NULL;
    if (voiceprint_extract(model, samples, n_samples, vp) != 0) {
        free(vp); return NULL;
    }
    LOG_INFO("=== Enroll complete ===");
    return vp;
}

verify_result_t voiceprint_verify(voiceprint_model_t* model,
                                   const float* test_samples, int n_samples,
                                   const voiceprint_t* enrolled,
                                   float* score_out) {
    if (!model || !test_samples || !enrolled || !score_out)
        return VERIFY_RESULT_UNKNOWN;

    LOG_INFO("=== Verify: %d samples ===", n_samples);

    const SherpaOnnxSpeakerEmbeddingExtractor* ex =
        (const SherpaOnnxSpeakerEmbeddingExtractor*)model->extractor;

    const float* test_emb = compute_embedding(ex, test_samples, n_samples);
    if (!test_emb) {
        *score_out = 0.0f;
        return VERIFY_RESULT_UNKNOWN;
    }

    float dot = 0.0f, n1 = 0.0f, n2 = 0.0f;
    for (int i = 0; i < model->dim; i++) {
        dot += test_emb[i] * enrolled->embedding[i];
        n1  += test_emb[i] * test_emb[i];
        n2  += enrolled->embedding[i] * enrolled->embedding[i];
    }
    float cosine = dot / (sqrtf(n1) * sqrtf(n2) + 1e-10f);

    SherpaOnnxSpeakerEmbeddingExtractorDestroyEmbedding(test_emb);

    *score_out = cosine;
    verify_result_t result = (cosine >= model->threshold)
        ? VERIFY_RESULT_MATCH : VERIFY_RESULT_NO_MATCH;

    LOG_INFO("  Cosine=%.4f (threshold=%.2f) → %s",
             cosine, model->threshold,
             result == VERIFY_RESULT_MATCH ? "MATCH" : "NO_MATCH");
    return result;
}

void voiceprint_free(voiceprint_t* vp) { free(vp); }
