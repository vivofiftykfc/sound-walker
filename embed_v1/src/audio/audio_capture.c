
#include "audio_capture.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alsa/asoundlib.h>

struct audio_capture {
    snd_pcm_t* pcm;
    audio_config_t config;
    char error[256];
    float buffer[AUDIO_BUFFER_FRAMES];
};

static audio_config_t default_config = {
    .sample_rate = AUDIO_SAMPLE_RATE,
    .channels = AUDIO_CHANNELS,
    .bit_depth = 16,  /* Use 16-bit internally */
    .buffer_frames = AUDIO_BUFFER_FRAMES,
    .period_size = AUDIO_PERIOD_SIZE
};

audio_capture_t* audio_capture_init(const audio_config_t* config) {
    audio_capture_t* cap = calloc(1, sizeof(audio_capture_t));
    if (!cap) return NULL;

    cap->config = config ? *config : default_config;

    int err;
    snd_pcm_t* pcm;

    err = snd_pcm_open(&pcm, CFG_ALSA_DEVICE, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        snprintf(cap->error, sizeof(cap->error),
                 "Cannot open audio device: %s", snd_strerror(err));
        free(cap);
        return NULL;
    }

    snd_pcm_hw_params_t* params;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm, params);

    err = snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) goto setup_failed;

    err = snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE);
    if (err < 0) goto setup_failed;

    err = snd_pcm_hw_params_set_rate_near(pcm, params, &cap->config.sample_rate, 0);
    if (err < 0) goto setup_failed;

    err = snd_pcm_hw_params_set_channels(pcm, params, cap->config.channels);
    if (err < 0) goto setup_failed;

    snd_pcm_uframes_t period = cap->config.period_size;
    err = snd_pcm_hw_params_set_period_size_near(pcm, params, &period, 0);
    if (err < 0) goto setup_failed;
    cap->config.period_size = (uint16_t)period;

    snd_pcm_uframes_t buf_frames = cap->config.buffer_frames;
    err = snd_pcm_hw_params_set_buffer_size_near(pcm, params, &buf_frames);
    if (err < 0) goto setup_failed;

    err = snd_pcm_hw_params(pcm, params);
    if (err < 0) goto setup_failed;

    cap->pcm = pcm;
    LOG_INFO("ALSA capture: %s, rate=%u Hz, channels=%u, format=S16_LE",
             CFG_ALSA_DEVICE, cap->config.sample_rate, cap->config.channels);
    return cap;

setup_failed:
    snprintf(cap->error, sizeof(cap->error),
             "Setup failed: %s", snd_strerror(err));
    snd_pcm_close(pcm);
    free(cap);
    return NULL;
}

int audio_capture_start(audio_capture_t* cap) {
    if (!cap || !cap->pcm) return -1;
    snd_pcm_prepare(cap->pcm);
    int err = snd_pcm_start(cap->pcm);
    if (err < 0) {
        snprintf(cap->error, sizeof(cap->error),
                 "Start failed: %s", snd_strerror(err));
        return -1;
    }
    return 0;
}

int audio_capture_stop(audio_capture_t* cap) {
    if (!cap || !cap->pcm) return -1;
    snd_pcm_drop(cap->pcm);
    return 0;
}

int audio_capture_read(audio_capture_t* cap, float* buffer, int n_frames) {
    if (!cap || !cap->pcm) return -1;

    int16_t* pcm_buf = malloc(n_frames * sizeof(int16_t));
    if (!pcm_buf) return -1;

    int err = snd_pcm_readi(cap->pcm, pcm_buf, n_frames);
    if (err == -EAGAIN) {
        snd_pcm_wait(cap->pcm, 100);
        err = snd_pcm_readi(cap->pcm, pcm_buf, n_frames);
    }

    if (err > 0) {
        
        for (int i = 0; i < err; i++) {
            buffer[i] = pcm_buf[i] / 32768.0f;
        }
    }

    free(pcm_buf);
    return (err > 0) ? err : -1;
}

void audio_capture_close(audio_capture_t* cap) {
    if (!cap) return;
    if (cap->pcm) {
        snd_pcm_drop(cap->pcm);
        snd_pcm_close(cap->pcm);
    }
    free(cap);
}

const char* audio_capture_error(audio_capture_t* cap) {
    return cap ? cap->error : "NULL handle";
}
