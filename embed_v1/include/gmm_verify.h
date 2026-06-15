#ifndef GMM_VERIFY_H
#define GMM_VERIFY_H

#include "config.h"

int gmm_verify_init(void);
void gmm_verify_cleanup(void);
int gmm_verify_enroll(int user_id, const float *features, int n_frames);
int gmm_verify_test(const float *features, int n_frames, float *score_out);
float gmm_verify_get_threshold(void);
void gmm_verify_set_threshold(float threshold);

#endif /* GMM_VERIFY_H */
