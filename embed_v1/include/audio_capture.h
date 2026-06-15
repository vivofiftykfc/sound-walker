/**
 * audio_capture.h - Audio capture interface for X210
 *
 * Supports:
 *   - ALSA (advanced Linux sound architecture)
 *   - I2S via DMA (for INMP441 microphone)
 *
 * Hardware: X210 (S5PV210) + INMP441 I2S microphone
 */

#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <stdint.h>
#include <stddef.h>

#define AUDIO_SAMPLE_RATE     44100
#define AUDIO_BIT_DEPTH       16      /* PCM2902 is 16-bit */
#define AUDIO_CHANNELS        1       /* Mono */
#define AUDIO_BUFFER_FRAMES   2048    /* Buffer size */
#define AUDIO_PERIOD_SIZE     441     /* Period (10ms at 44100Hz) */

/* Capture state */
typedef enum {
    AUDIO_STATE_IDLE = 0,
    AUDIO_STATE_CAPTURING,
    AUDIO_STATE_ERROR
} audio_state_t;

/* Capture configuration */
typedef struct {
    uint32_t sample_rate;
    uint8_t  channels;
    uint8_t  bit_depth;
    uint16_t buffer_frames;
    uint16_t period_size;
} audio_config_t;

/* Capture handle */
typedef struct audio_capture audio_capture_t;

/**
 * Initialize audio capture device
 * @param config   Capture configuration (NULL for defaults)
 * @return         Capture handle or NULL on error
 */
audio_capture_t* audio_capture_init(const audio_config_t* config);

/**
 * Start audio capture
 * @param cap   Capture handle
 * @return      0 on success, -1 on error
 */
int audio_capture_start(audio_capture_t* cap);

/**
 * Stop audio capture
 * @param cap   Capture handle
 * @return      0 on success, -1 on error
 */
int audio_capture_stop(audio_capture_t* cap);

/**
 * Read audio samples (blocking)
 * @param cap      Capture handle
 * @param buffer   Output buffer
 * @param n_frames Number of frames to read
 * @return         Number of frames read, or -1 on error
 */
int audio_capture_read(audio_capture_t* cap, float* buffer, int n_frames);

/**
 * Close audio capture
 * @param cap   Capture handle
 */
void audio_capture_close(audio_capture_t* cap);

/**
 * Get error message
 * @param cap   Capture handle
 * @return      Error string
 */
const char* audio_capture_error(audio_capture_t* cap);

#endif /* AUDIO_CAPTURE_H */
