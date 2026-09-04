#include <string.h>
#include "arm_nnsupportfunctions.h"
#include "nn_offload.h"
#include "hal.h"

static inline int8_t requant_clamp(int32_t acc, int32_t mult, int32_t shift,
                                   int32_t out_offset, int32_t amin, int32_t amax)
{
    int32_t v = arm_nn_requantize(acc, mult, shift) + out_offset;
    if (v < amin) v = amin;
    if (v > amax) v = amax;
    return (int8_t)v;
}

arm_cmsis_nn_status offload_convolve_s8(const cmsis_nn_conv_params *cp,
                                        const cmsis_nn_per_channel_quant_params *qp,
                                        const cmsis_nn_dims *in_d, const int8_t *input,
                                        const cmsis_nn_dims *f_d, const int8_t *filter,
                                        const int32_t *bias,
                                        const cmsis_nn_dims *out_d, int8_t *output,
                                        int8_t *im2col, bool wait_irq)
{
    const int32_t in_h = in_d->h, in_w = in_d->w, in_c = in_d->c;
    const int32_t kh = f_d->h, kw = f_d->w;
    const int32_t out_h = out_d->h, out_w = out_d->w, out_c = out_d->c;
    const int32_t n = kh * kw * in_c;
    const int32_t in_zp = -cp->input_offset;   /* padded value contributes 0 after offset */
    int32_t results[64];

    if (out_c > 64) return ARM_CMSIS_NN_ARG_ERROR;

    for (int32_t oy = 0; oy < out_h; oy++) {
        for (int32_t ox = 0; ox < out_w; ox++) {
            /* im2col: [kh][kw][in_c] */
            int8_t *p = im2col;
            const int32_t iy0 = oy * cp->stride.h - cp->padding.h;
            const int32_t ix0 = ox * cp->stride.w - cp->padding.w;
            for (int32_t ky = 0; ky < kh; ky++) {
                const int32_t iy = iy0 + ky;
                for (int32_t kx = 0; kx < kw; kx++) {
                    const int32_t ix = ix0 + kx;
                    if (iy < 0 || iy >= in_h || ix < 0 || ix >= in_w) {
                        memset(p, (int)(int8_t)in_zp, in_c);
                    } else {
                        memcpy(p, input + (iy * in_w + ix) * in_c, in_c);
                    }
                    p += in_c;
                }
            }
            if (!hal_mac_dot_rows(im2col, cp->input_offset, filter, 0, n, out_c, bias, results, wait_irq))
                return ARM_CMSIS_NN_NO_IMPL_ERROR;
            int8_t *o = output + (oy * out_w + ox) * out_c;
            for (int32_t oc = 0; oc < out_c; oc++)
                o[oc] = requant_clamp(results[oc], qp->multiplier[oc], qp->shift[oc],
                                      cp->output_offset, cp->activation.min, cp->activation.max);
        }
    }
    return ARM_CMSIS_NN_SUCCESS;
}

arm_cmsis_nn_status offload_fully_connected_s8(const cmsis_nn_fc_params *fp,
                                               const cmsis_nn_per_tensor_quant_params *qp,
                                               const cmsis_nn_dims *in_d, const int8_t *input,
                                               const cmsis_nn_dims *f_d, const int8_t *filter,
                                               const int32_t *bias,
                                               const cmsis_nn_dims *out_d, int8_t *output,
                                               bool wait_irq)
{
    const int32_t n = f_d->n;          /* input length */
    const int32_t rows = out_d->c;     /* outputs */
    int32_t results[64];
    (void)in_d;
    if (rows > 64) return ARM_CMSIS_NN_ARG_ERROR;
    if (!hal_mac_dot_rows(input, fp->input_offset, filter, fp->filter_offset, n, rows, bias, results, wait_irq))
        return ARM_CMSIS_NN_NO_IMPL_ERROR;
    for (int32_t r = 0; r < rows; r++)
        output[r] = requant_clamp(results[r], qp->multiplier, qp->shift,
                                  fp->output_offset, fp->activation.min, fp->activation.max);
    return ARM_CMSIS_NN_SUCCESS;
}
