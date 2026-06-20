
#include "mfcc.h"
#include "audio_preprocess.h"
#include "config.h"

#include "arm_math.h"
#include "arm_mfcc.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

struct mfcc_config {
    arm_rfft_fast_instance_f32 rfft;

    float hamming_window[N_FFT];

    float mel_filterbank[N_MEL_FILTERS][N_FFT / 2 + 1];

    float dct_matrix[N_MEL_FILTERS][N_MFCC];

    uint32_t sample_rate;
    uint32_t n_fft;
    uint32_t n_mfcc;
    uint32_t n_mels;

    float* fft_input;
    float* fft_output;
    float* mel_energies;
    float* log_mel;
};

/* Convert Hz to Mel */
static float hz_to_mel(float hz) {
    return 2595.0f * log10f(1.0f + hz / 700.0f);
}

/* Convert Mel to Hz */
static float mel_to_hz(float mel) {
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

/* Initialize Hamming window */
static void init_hamming_window(float* window, int n) {
    for (int i = 0; i < n; i++) {
        window[i] = 0.54f - 0.46f * cosf(2.0f * M_PI * i / (n - 1));
    }
}

/* Initialize Mel filterbank */
static void init_mel_filterbank(mfcc_config_t* config) {
    float mel_low = hz_to_mel(MEL_LOW_FREQ);
    float mel_high = hz_to_mel(MEL_HIGH_FREQ);

    float* mel_points = malloc((config->n_mels + 2) * sizeof(float));
    for (int i = 0; i < config->n_mels + 2; i++) {
        mel_points[i] = mel_low + (mel_high - mel_low) * i / (config->n_mels + 1);
        mel_points[i] = mel_to_hz(mel_points[i]);
    }

    int n_fft = config->n_fft;
    float freq_per_bin = (float)config->sample_rate / n_fft;

    for (int m = 0; m < config->n_mels; m++) {
        int left = (int)(mel_points[m] / freq_per_bin);
        int center = (int)(mel_points[m + 1] / freq_per_bin);
        int right = (int)(mel_points[m + 2] / freq_per_bin);

        for (int k = 0; k <= n_fft / 2; k++) {
            float weight;
            if (k < left || k > right) {
                weight = 0.0f;
            } else if (k < center) {
                weight = (float)(k - left) / (center - left);
            } else {
                weight = (float)(right - k) / (right - center);
            }
            config->mel_filterbank[m][k] = weight;
        }
    }

    free(mel_points);
}

/* Initialize DCT matrix */
static void init_dct_matrix(mfcc_config_t* config) {
    float sqrt_2_over_n = sqrtf(2.0f / config->n_mels);
    float coef = sqrt_2_over_n * M_PI / config->n_mels;

    for (int i = 0; i < config->n_mfcc; i++) {
        for (int j = 0; j < config->n_mels; j++) {
            config->dct_matrix[i][j] = cosf(coef * (j + 0.5f) * i);
        }
    }
    for (int j = 0; j < config->n_mels; j++) {
        config->dct_matrix[0][j] *= sqrtf(1.0f / (float)config->n_mels);
    }
}

mfcc_config_t* mfcc_init(uint32_t sample_rate) {
    mfcc_config_t* config = calloc(1, sizeof(mfcc_config_t));
    if (!config) return NULL;

    config->sample_rate = sample_rate;
    config->n_fft = N_FFT;
    config->n_mfcc = N_MFCC;
    config->n_mels = N_MEL_FILTERS;

    config->fft_input = calloc(config->n_fft, sizeof(float));
    config->fft_output = calloc(config->n_fft, sizeof(float));
    config->mel_energies = calloc(config->n_mels, sizeof(float));
    config->log_mel = calloc(config->n_mels, sizeof(float));

    if (!config->fft_input || !config->fft_output ||
        !config->mel_energies || !config->log_mel) {
        mfcc_free(config);
        return NULL;
    }

    arm_rfft_fast_init_f32(&config->rfft, config->n_fft);
    init_hamming_window(config->hamming_window, config->n_fft);
    init_mel_filterbank(config);
    init_dct_matrix(config);

    return config;
}

int mfcc_extract_frame(mfcc_config_t* config,
                       const float* frame, float* mfcc_out) {
    for (int i = 0; i < config->n_fft; i++) {
        config->fft_input[i] = frame[i] * config->hamming_window[i];
    }

    arm_rfft_fast_f32(&config->rfft, config->fft_input, config->fft_output);

    float magnitude[N_FFT / 2 + 1];
    magnitude[0] = fabsf(config->fft_output[0]);
    for (int i = 1; i < config->n_fft / 2; i++) {
        float real = config->fft_output[2 * i];
        float imag = config->fft_output[2 * i + 1];
        magnitude[i] = sqrtf(real * real + imag * imag);
    }
    magnitude[config->n_fft / 2] = fabsf(config->fft_output[1]);

    for (int m = 0; m < config->n_mels; m++) {
        float energy = 0.0f;
        for (int k = 0; k <= config->n_fft / 2; k++) {
            energy += magnitude[k] * config->mel_filterbank[m][k];
        }
        config->mel_energies[m] = energy;

        /* Log */
        config->log_mel[m] = logf(energy + 1e-10f);
    }

    for (int i = 0; i < config->n_mfcc; i++) {
        float sum = 0.0f;
        for (int j = 0; j < config->n_mels; j++) {
            sum += config->log_mel[j] * config->dct_matrix[i][j];
        }
        mfcc_out[i] = sum;
    }

    return 0;
}

int mfcc_extract(mfcc_config_t* config,
                 const float* samples, int n_samples,
                 float* mfcc_out, int* n_frames) {
    int frame_count = 0;
    int sample_pos = 0;

    float frame[N_FFT];
    float mfcc_frame[N_MFCC];

    while (sample_pos + N_FFT <= n_samples) {
        /* Copy frame */
        for (int i = 0; i < N_FFT; i++) {
            frame[i] = samples[sample_pos + i];
        }

        /* Extract MFCC */
        if (mfcc_extract_frame(config, frame, mfcc_frame) == 0) {
            for (int i = 0; i < N_MFCC; i++) {
                mfcc_out[frame_count * N_MFCC + i] = mfcc_frame[i];
            }
            frame_count++;
        }

        sample_pos += FRAME_SHIFT;
    }

    *n_frames = frame_count;
    return 0;
}

void mfcc_delta(const float* mfcc, int n_frames, float* delta) {
    for (int i = 0; i < n_frames - 1; i++) {
        for (int j = 0; j < N_MFCC; j++) {
            delta[i * N_MFCC + j] = mfcc[(i + 1) * N_MFCC + j] - mfcc[i * N_MFCC + j];
        }
    }
    if (n_frames > 1) {
        for (int j = 0; j < N_MFCC; j++) {
            delta[(n_frames - 1) * N_MFCC + j] = delta[(n_frames - 2) * N_MFCC + j];
        }
    }
}

void mfcc_free(mfcc_config_t* config) {
    if (!config) return;
    free(config->fft_input);
    free(config->fft_output);
    free(config->mel_energies);
    free(config->log_mel);
    free(config);
}
