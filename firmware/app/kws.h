#ifndef KWS_H
#define KWS_H
#include <stdint.h>
void kws_init(void);
/* Called from main loop with each completed 20 ms audio hop. */
void kws_process_hop(const int16_t *hop, uint32_t frame_idx);
uint32_t kws_inferences_done(void);
#endif
