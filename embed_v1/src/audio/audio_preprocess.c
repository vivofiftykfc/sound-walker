
#include "audio_preprocess.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

struct audio_preproc {
    uint32_t sample_rate;
    float prev_sample;
};

audio_preproc_t* audio_preproc_init(uint32_t sample_rate) {
    audio_preproc_t* proc = calloc(1, sizeof(audio_preproc_t));
    if (!proc) return NULL;
    proc->sample_rate = sample_rate;
    proc->prev_sample = 0.0f;
    return proc;
}

void audio_preproc_preemphasis(audio_preproc_t* proc,
                               const float* input, float* output, int n) {
    float alpha = PREEMPH_COEFF;
    output[0] = input[0];
    for (int i = 1; i < n; i++) {
        output[i] = input[i] - alpha * input[i - 1];
    }
}

static float calc_frame_energy(const float* frame) {
    float energy = 0.0f;
    for (int i = 0; i < FRAME_LEN; i++) {
        energy += frame[i] * frame[i];
    }
    return sqrtf(energy / FRAME_LEN);
}

static float calc_zero_crossing_rate(const float* frame) {
    int zcr = 0;
    for (int i = 1; i < FRAME_LEN; i++) {
        if ((frame[i] >= 0 && frame[i - 1] < 0) ||
            (frame[i] < 0 && frame[i - 1] >= 0)) {
            zcr++;
        }
    }
    return (float)zcr / FRAME_LEN;
}

int audio_preproc_frame(audio_preproc_t* proc,
                        const float* samples, int n, float* frames) {
    int n_frames = 0;
    int pos = 0;

    while (pos + FRAME_LEN <= n) {
        float* frame = &frames[n_frames * FRAME_LEN];

        /* Copy and apply Hamming window */
        float sum = 0.0f;
        for (int i = 0; i < FRAME_LEN; i++) {
            float w = 0.54f - 0.46f * cosf(2.0f * M_PI * i / (FRAME_LEN - 1));
            frame[i] = samples[pos + i] * w;
            sum += frame[i];
        }

        /* Normalize */
        float norm = sum / FRAME_LEN;
        if (norm > 1e-6f) {
            for (int i = 0; i < FRAME_LEN; i++) {
                frame[i] = (frame[i] - norm) / norm;
            }
        }

        n_frames++;
        pos += FRAME_SHIFT;
    }

    return n_frames;
}

int audio_preproc_vad(audio_preproc_t* proc,
                      const float* frames, int n_frames, bool* speech) {
    memset(speech, 0, n_frames * sizeof(bool));

    float energy_threshold = VAD_ENERGY_THRESH;
    int speech_count = 0;

    for (int i = 0; i < n_frames; i++) {
        const float* frame = &frames[i * FRAME_LEN];

        float energy = calc_frame_energy(frame);
        float zcr = calc_zero_crossing_rate(frame);

        /* Speech if energy above threshold or ZCR above threshold */
        if (energy > energy_threshold || zcr > (float)VAD_ZCR_THRESH / FRAME_LEN) {
            speech[i] = true;
            speech_count++;
        }
    }

    if (speech_count < VAD_MIN_SPEECH_FRAMES) {
        return 0;
    }

    int first_speech = -1;
    int last_speech = -1;
    for (int i = 0; i < n_frames; i++) {
        if (speech[i]) {
            if (first_speech < 0) first_speech = i;
            last_speech = i;
        }
    }

    if (first_speech >= 0) {
        /* Add padding */
        int start = (first_speech - VAD_PAD_FRAMES > 0) ?
                    first_speech - VAD_PAD_FRAMES : 0;
        int end = (last_speech + VAD_PAD_FRAMES < n_frames) ?
                  last_speech + VAD_PAD_FRAMES : n_frames;

        for (int i = start; i < end; i++) {
            speech[i] = true;
        }
    }

    speech_count = 0;
    for (int i = 0; i < n_frames; i++) {
        if (speech[i]) speech_count++;
    }

    return speech_count;
}

void audio_preproc_free(audio_preproc_t* proc) {
    free(proc);
}
