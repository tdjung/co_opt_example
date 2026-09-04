#include <string.h>
#include "placement.h"
#include "hal.h"
#include "mfcc.h"
#include "ds_cnn.h"
#include "kws.h"

#define FEAT_LEN (KWS_IN_H * KWS_IN_W)   /* 490 */

static int8_t   s_feat[FEAT_LEN]        SECTION_FEATURES;
static int8_t   s_logits[KWS_N_CLASSES] SECTION_FEATURES;
static int8_t   s_probs[KWS_N_CLASSES]  SECTION_FEATURES;
static uint32_t s_frames_in_window      SECTION_FEATURES;
static uint32_t s_hop_count             SECTION_FEATURES;
static uint32_t s_infer_count           SECTION_FEATURES;

void kws_init(void)
{
    memset(s_feat, KWS_INPUT_ZP, sizeof(s_feat));
    s_frames_in_window = s_hop_count = s_infer_count = 0;
    mfcc_init();
    ds_cnn_init();
}

static void log_mfcc(uint32_t frame_idx, const int8_t *f)
{
    hal_log_printf("MFCC frame=%u", (unsigned)frame_idx);
    for (int i = 0; i < KWS_IN_W; i++) hal_log_printf(" %d", f[i]);
    hal_log_puts("\n");
}

void kws_process_hop(const int16_t *hop, uint32_t frame_idx)
{
    int8_t coef[KWS_IN_W];
    uint32_t t0 = hal_cycles();
    hal_marker(0x10);
    s_hop_count++;
    if (!mfcc_push_hop(hop, coef))
        return;
    uint32_t t_mfcc = hal_cycles() - t0;

    /* slide the 49-frame window and append */
    memmove(s_feat, s_feat + KWS_IN_W, FEAT_LEN - KWS_IN_W);
    memcpy(s_feat + FEAT_LEN - KWS_IN_W, coef, KWS_IN_W);
    if (s_frames_in_window < KWS_IN_H) s_frames_in_window++;
    log_mfcc(frame_idx, coef);
    hal_log_printf("PROF frame=%u mfcc_cycles=%u\n", (unsigned)frame_idx, (unsigned)t_mfcc);

    if (s_hop_count % KWS_INFER_HOP_FRAMES != 0)
        return;
    if (s_frames_in_window < KWS_IN_H)
        return;                       /* window not yet full */

    uint32_t t1 = hal_cycles();
    hal_marker(0x20);
    int best = ds_cnn_run(s_feat, s_logits, s_probs);
    uint32_t cyc = hal_cycles() - t1;
    hal_marker(0x21);
    s_infer_count++;

    int second = (best == 0) ? 1 : 0;
    for (int i = 0; i < KWS_N_CLASSES; i++)
        if (i != best && s_probs[i] > s_probs[second]) second = i;

    hal_result_write(s_infer_count, best, s_probs[best], cyc);

    /* human-readable line */
    hal_log_printf("[infer %u] frame=%u t=%ums top1=%s (%d) top2=%s (%d) cycles=%u",
                   (unsigned)s_infer_count, (unsigned)frame_idx, (unsigned)(frame_idx * 20),
                   KWS_LABELS[best], (int)s_probs[best] + 128,
                   KWS_LABELS[second], (int)s_probs[second] + 128, (unsigned)cyc);
    hal_log_puts("\n");

    /* machine-readable golden line */
    hal_log_printf("GOLDEN infer=%u frame=%u logits=", (unsigned)s_infer_count, (unsigned)frame_idx);
    for (int i = 0; i < KWS_N_CLASSES; i++) hal_log_printf("%s%d", i ? "," : "", s_logits[i]);
    hal_log_puts(" probs=");
    for (int i = 0; i < KWS_N_CLASSES; i++) hal_log_printf("%s%d", i ? "," : "", s_probs[i]);
    hal_log_puts(" cksum=");
    const uint32_t *ck = ds_cnn_layer_checksums();
    for (int i = 0; i < DS_CNN_N_LAYERS; i++) hal_log_printf("%s%08x", i ? "," : "", (unsigned)ck[i]);
    hal_log_puts("\n");

    const uint32_t *lc = ds_cnn_layer_cycles();
    hal_log_printf("LAYERS infer=%u", (unsigned)s_infer_count);
    for (int i = 0; i < DS_CNN_N_LAYERS; i++) hal_log_printf(" %s=%u", ds_cnn_layer_name(i), (unsigned)lc[i]);
    hal_log_puts("\n");
}

uint32_t kws_inferences_done(void) { return s_infer_count; }
