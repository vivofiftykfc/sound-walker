#ifndef AUDIO_PCAP_H
#define AUDIO_PCAP_H

int audio_init(void **ctx, const char *source);
int audio_read_samples(void *ctx, float *samples, int n_samples);
int audio_read_frame(void *ctx, float *frame);
int audio_seek(void *ctx, float position_sec);
int audio_eof(void *ctx);
int audio_close(void *ctx);
int audio_get_duration(void *ctx);
int audio_get_position(void *ctx);

#endif /* AUDIO_PCAP_H */
