/*
 * nn_offload.h -- layer variants that run their dot products on the MAC
 * accelerator through hal_mac_dot_rows(). Results are bit-exact with the
 * CMSIS-NN CPU kernels (same integer arithmetic and requantisation).
 */
#ifndef NN_OFFLOAD_H
#define NN_OFFLOAD_H
#include <stdint.h>
#include <stdbool.h>
#include "arm_nnfunctions.h"

/* Generic NHWC conv (used for conv1 and 1x1 pointwise convs). */
arm_cmsis_nn_status offload_convolve_s8(const cmsis_nn_conv_params *conv_params,
                                        const cmsis_nn_per_channel_quant_params *quant_params,
                                        const cmsis_nn_dims *input_dims, const int8_t *input,
                                        const cmsis_nn_dims *filter_dims, const int8_t *filter,
                                        const int32_t *bias,
                                        const cmsis_nn_dims *output_dims, int8_t *output,
                                        int8_t *im2col_scratch, bool wait_irq);

arm_cmsis_nn_status offload_fully_connected_s8(const cmsis_nn_fc_params *fc_params,
                                               const cmsis_nn_per_tensor_quant_params *quant_params,
                                               const cmsis_nn_dims *input_dims, const int8_t *input,
                                               const cmsis_nn_dims *filter_dims, const int8_t *filter,
                                               const int32_t *bias,
                                               const cmsis_nn_dims *output_dims, int8_t *output,
                                               bool wait_irq);
#endif
