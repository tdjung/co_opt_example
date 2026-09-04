/* mfcc.h -- 40 ms window / 20 ms hop MFCC front-end (10 coefficients). */
#ifndef MFCC_H
#define MFCC_H
#include <stdint.h>
#include "mfcc_tables.h"

void mfcc_init(void);

/* Push one 20 ms hop (MFCC_HOP int16 samples). Returns 1 and writes
 * MFCC_NDCT int8-quantised coefficients into `out` once a full 40 ms window
 * is available (i.e. from the second hop onwards), 0 otherwise. */
int mfcc_push_hop(const int16_t *hop, int8_t *out);

/* Raw float coefficients of the last computed frame (debug/golden). */
const float *mfcc_last_float(void);

/* Deterministic natural log (no libm) -- exported for tests. */
float det_logf(float x);

#endif
