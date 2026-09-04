#include <string.h>
#include "arm_nnfunctions.h"
#include "placement.h"
#include "hal.h"
#include "ds_cnn.h"
#include "nn_offload.h"

#define ACT_SIZE (KWS_OUT_H * KWS_OUT_W * KWS_CH)   /* 8000 */
#define SCRATCH_SIZE 4096

static int8_t s_act_a[ACT_SIZE]     SECTION_ACT_A;
static int8_t s_act_b[ACT_SIZE]     SECTION_ACT_B;
static int8_t s_scratch[SCRATCH_SIZE] SECTION_NN_SCRATCH;
static int8_t s_pool_out[KWS_CH]    SECTION_NN_SCRATCH;
static uint32_t s_cksum[DS_CNN_N_LAYERS] SECTION_NN_SCRATCH;
static uint32_t s_cyc[DS_CNN_N_LAYERS]   SECTION_NN_SCRATCH;

/* Optional runtime copies of the weights (KWS_WEIGHT_LOAD_MODE 1/2). */
#if KWS_WEIGHT_LOAD_MODE != 0
static int8_t s_wcopy[KWS_WEIGHTS_TOTAL_BYTES + 64] SECTION_WEIGHT_COPY;
#endif

typedef struct {
    const char *name;
    int kind;                  /* 0 conv, 1 dw, 2 fc */
    const int8_t *w; const int32_t *b; const int32_t *mult; const int32_t *shift;
    int32_t w_len, b_len;
    int32_t in_zp, out_zp;
    int offload;
} layer_t;

#define L_CONV(nm, upper, off) { #nm, 0, nm##_w, nm##_b, nm##_mult, nm##_shift, \
    (int32_t)sizeof(nm##_w), (int32_t)(sizeof(nm##_b)/4), upper##_IN_ZP, upper##_OUT_ZP, off }
#define L_DW(nm, upper) { #nm, 1, nm##_w, nm##_b, nm##_mult, nm##_shift, \
    (int32_t)sizeof(nm##_w), (int32_t)(sizeof(nm##_b)/4), upper##_IN_ZP, upper##_OUT_ZP, 0 }

static layer_t s_layers[] = {
    L_CONV(conv1, CONV1, OFFLOAD_CONV1),
    L_DW(dw1, DW1), L_CONV(pw1, PW1, OFFLOAD_PW1),
    L_DW(dw2, DW2), L_CONV(pw2, PW2, OFFLOAD_PW2),
    L_DW(dw3, DW3), L_CONV(pw3, PW3, OFFLOAD_PW3),
    L_DW(dw4, DW4), L_CONV(pw4, PW4, OFFLOAD_PW4),
    { "fc", 2, fc_w, fc_b, fc_mult, fc_shift, (int32_t)sizeof(fc_w), KWS_N_CLASSES, FC_IN_ZP, FC_OUT_ZP, OFFLOAD_FC },
};
#define N_WLAYERS ((int)(sizeof(s_layers) / sizeof(s_layers[0])))

static const char *const s_names[DS_CNN_N_LAYERS] = {
    "conv1", "dw1", "pw1", "dw2", "pw2", "dw3", "pw3", "dw4", "pw4", "avgpool", "fc"
};
const char *ds_cnn_layer_name(int i) { return s_names[i]; }

static uint32_t checksum(const int8_t *p, int32_t n)
{
    uint32_t h = 2166136261u;
    for (int32_t i = 0; i < n; i++) h = (h ^ (uint8_t)p[i]) * 16777619u;
    return h;
}

void ds_cnn_init(void)
{
#if KWS_WEIGHT_LOAD_MODE != 0
    /* Copy every layer's weights + bias into the copy region and repoint. */
    int8_t *p = s_wcopy;
    for (int i = 0; i < N_WLAYERS; i++) {
        layer_t *L = &s_layers[i];
        int32_t wl = (L->w_len + 3) & ~3, bl = L->b_len * 4;
#if KWS_WEIGHT_LOAD_MODE == 2
        hal_dma_copy(p, L->w, L->w_len, AUDIO_DMA_BURST);
        hal_dma_copy(p + wl, L->b, bl, AUDIO_DMA_BURST);
#else
        memcpy(p, L->w, L->w_len);
        memcpy(p + wl, L->b, bl);
#endif
        L->w = p;
        L->b = (const int32_t *)(p + wl);
        p += wl + bl;
    }
#endif
}

SECTION_CODE_NN
int ds_cnn_run(const int8_t *features, int8_t *logits, int8_t *probs)
{
    cmsis_nn_context ctx = { s_scratch, SCRATCH_SIZE };
    const int8_t *in = features;
    int8_t *out = s_act_a;
    cmsis_nn_dims in_d = { 1, KWS_IN_H, KWS_IN_W, KWS_IN_C };
    cmsis_nn_dims out_d = { 1, KWS_OUT_H, KWS_OUT_W, KWS_CH };
    cmsis_nn_dims f_d, b_d = { 1, 1, 1, KWS_CH };
    int li = 0;
    const bool mac = hal_mac_present();

    for (int i = 0; i < N_WLAYERS; i++, li++) {
        layer_t *L = &s_layers[i];
        cmsis_nn_per_channel_quant_params qp = { (int32_t *)L->mult, (int32_t *)L->shift };
        uint32_t t0 = hal_cycles();
        hal_marker(0x100 + li);

        if (L->kind == 0) {                       /* conv / pointwise */
            const int first = (i == 0);
            cmsis_nn_conv_params cp;
            cp.input_offset = -L->in_zp; cp.output_offset = L->out_zp;
            cp.stride.h = first ? KWS_CONV1_SH : 1; cp.stride.w = first ? KWS_CONV1_SW : 1;
            cp.padding.h = first ? KWS_CONV1_PAD_H : 0; cp.padding.w = first ? KWS_CONV1_PAD_W : 0;
            cp.dilation.h = 1; cp.dilation.w = 1;
            cp.activation.min = -128; cp.activation.max = 127;
            f_d.n = KWS_CH; f_d.h = first ? KWS_CONV1_KH : 1; f_d.w = first ? KWS_CONV1_KW : 1;
            f_d.c = in_d.c;
            if (L->offload && mac)
                offload_convolve_s8(&cp, &qp, &in_d, in, &f_d, L->w, L->b, &out_d, out, s_scratch, OFFLOAD_WAIT_IRQ);
            else
                arm_convolve_wrapper_s8(&ctx, &cp, &qp, &in_d, in, &f_d, L->w, &b_d, L->b, &out_d, out);
        } else if (L->kind == 1) {                /* depthwise 3x3 */
            cmsis_nn_dw_conv_params dp;
            dp.input_offset = -L->in_zp; dp.output_offset = L->out_zp; dp.ch_mult = 1;
            dp.stride.h = 1; dp.stride.w = 1; dp.padding.h = KWS_DW_PAD; dp.padding.w = KWS_DW_PAD;
            dp.dilation.h = 1; dp.dilation.w = 1;
            dp.activation.min = -128; dp.activation.max = 127;
            f_d.n = 1; f_d.h = KWS_DW_K; f_d.w = KWS_DW_K; f_d.c = KWS_CH;
            arm_depthwise_conv_wrapper_s8(&ctx, &dp, &qp, &in_d, in, &f_d, L->w, &b_d, L->b, &out_d, out);
        } else {                                  /* fc (preceded by avgpool) */
            /* global average pool 25x5 -> 1x1 */
            {
                uint32_t p0 = hal_cycles();
                hal_marker(0x100 + li);
                cmsis_nn_pool_params pp;
                pp.stride.h = 1; pp.stride.w = 1; pp.padding.h = 0; pp.padding.w = 0;
                pp.activation.min = -128; pp.activation.max = 127;
                cmsis_nn_dims pf = { 1, KWS_OUT_H, KWS_OUT_W, 1 };
                cmsis_nn_dims po = { 1, 1, 1, KWS_CH };
                arm_avgpool_s8(&ctx, &pp, &in_d, in, &pf, &po, s_pool_out);
                s_cksum[li] = checksum(s_pool_out, KWS_CH);
                s_cyc[li] = hal_cycles() - p0;
                li++;
                t0 = hal_cycles();
                hal_marker(0x100 + li);
            }
            cmsis_nn_fc_params fp;
            fp.input_offset = -L->in_zp; fp.filter_offset = 0; fp.output_offset = L->out_zp;
            fp.activation.min = -128; fp.activation.max = 127;
            cmsis_nn_per_tensor_quant_params tq = { L->mult[0], L->shift[0] };
            cmsis_nn_quant_params wq = { (int32_t *)L->mult, (int32_t *)L->shift, KWS_FC_PER_CHANNEL };
            cmsis_nn_dims fi = { 1, 1, 1, KWS_CH };
            cmsis_nn_dims ff = { KWS_CH, 1, 1, KWS_N_CLASSES };
            cmsis_nn_dims fb = { 1, 1, 1, KWS_N_CLASSES };
            cmsis_nn_dims fo = { 1, 1, 1, KWS_N_CLASSES };
            if (L->offload && mac)
                offload_fully_connected_s8(&fp, &wq, &fi, s_pool_out, &ff, L->w, L->b, &fo, logits, OFFLOAD_WAIT_IRQ);
            else
                arm_fully_connected_wrapper_s8(&ctx, &fp, &wq, &fi, s_pool_out, &ff, L->w, &fb, L->b, &fo, logits);
            (void)tq;
            s_cksum[li] = checksum(logits, KWS_N_CLASSES);
            s_cyc[li] = hal_cycles() - t0;
            break;
        }
        s_cksum[li] = checksum(out, ACT_SIZE);
        s_cyc[li] = hal_cycles() - t0;
        /* next layer */
        in = out;
        out = (out == s_act_a) ? s_act_b : s_act_a;
        in_d = out_d;
    }

    arm_softmax_s8(logits, 1, KWS_N_CLASSES, KWS_SOFTMAX_MULT, KWS_SOFTMAX_SHIFT, KWS_SOFTMAX_DIFF_MIN, probs);
    int best = 0;
    for (int i = 1; i < KWS_N_CLASSES; i++)
        if (probs[i] > probs[best]) best = i;
    return best;
}

const uint32_t *ds_cnn_layer_checksums(void) { return s_cksum; }
const uint32_t *ds_cnn_layer_cycles(void) { return s_cyc; }
