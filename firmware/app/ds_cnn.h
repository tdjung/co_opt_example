/* ds_cnn.h -- DS-CNN Small inference (CMSIS-NN int8, optional MAC offload). */
#ifndef DS_CNN_H
#define DS_CNN_H
#include <stdint.h>
#include "kws_weights.h"

#define DS_CNN_N_LAYERS (1 + 2 * KWS_N_DS + 2)   /* conv1, (dw,pw)x4, avgpool, fc */

void ds_cnn_init(void);
/* features: KWS_IN_H*KWS_IN_W int8 (time-major, [49][10]).
 * logits/probs: KWS_N_CLASSES int8. Returns argmax. */
int  ds_cnn_run(const int8_t *features, int8_t *logits, int8_t *probs);
/* per-layer output checksums of the last run (debugging golden mismatches) */
const uint32_t *ds_cnn_layer_checksums(void);
/* per-layer cycles of the last run */
const uint32_t *ds_cnn_layer_cycles(void);
const char *ds_cnn_layer_name(int i);
#endif
