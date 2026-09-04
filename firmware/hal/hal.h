/*
 * hal.h -- fixed hardware abstraction API.
 *
 * The application (app/, kernels/) only talks to hardware through this header.
 * Two implementations exist:
 *   hal/host/   x86 build for golden generation and functional testing
 *   hal/target/ the CM4 virtual platform (to be implemented against its register map)
 *
 * The optimisation loop must NOT edit this file or hal/*; it only changes
 * arguments passed through it (see app/placement.h).
 */
#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---------------- lifecycle ---------------- */
void     hal_init(void);
void     hal_exit(int code);            /* terminate simulation / process */
uint32_t hal_cycles(void);              /* free-running core cycle counter */
uint32_t hal_time_us(void);             /* simulated wall time in microseconds */

/* ---------------- logging ------------------ */
void hal_log_puts(const char *s);       /* newline not appended */
void hal_log_printf(const char *fmt, ...);

/* ---------------- interrupts --------------- */
uint32_t hal_irq_save(void);            /* disable, return previous state */
void     hal_irq_restore(uint32_t s);
void     hal_wfi(void);

/* ---------------- audio input (DMA) ----------------
 * The audio source delivers 16 kHz int16 mono. The HAL configures a DMA
 * channel that fills `ring` frame by frame (frame_len samples per frame,
 * n_frames frames in the ring) and calls `on_frame(frame_idx)` from interrupt
 * context when a frame has been filled. `burst` is the DMA burst length knob.
 */
typedef void (*hal_audio_cb_t)(uint32_t frame_idx);
void hal_audio_start(int16_t *ring, uint32_t frame_len, uint32_t n_frames,
                     uint32_t burst, hal_audio_cb_t on_frame);
bool hal_audio_eof(void);               /* true when the source has no more data */

/* ---------------- generic DMA memcpy ----------------
 * Blocking copy dst<-src using a DMA channel (src/dst must be DMA-reachable:
 * not TCM). Returns false if the platform cannot do it (host: always memcpy). */
bool hal_dma_copy(void *dst, const void *src, size_t len, uint32_t burst);

/* ---------------- periodic timer ----------------
 * Calls `cb()` from interrupt context every period_us. */
typedef void (*hal_timer_cb_t)(void);
void hal_timer_start(uint32_t period_us, hal_timer_cb_t cb);
/* Latency measurement: cycles elapsed between the timer event and the
 * moment the ISR read it. The HAL knows when the timer fired. */
uint32_t hal_timer_latency_cycles(void);

/* ---------------- control loop peripherals ---------------- */
int32_t hal_sensor_read(void);          /* synthetic plant / sensor register */
void    hal_actuator_write(int32_t v);

/* ---------------- KWS result register ---------------- */
void hal_result_write(uint32_t infer_idx, int label, int score_q7, uint32_t cycles);

/* ---------------- MAC accelerator ----------------
 * INT8 dot product engine. Computes sum_i (a[i] + a_off) * (b[i] + b_off)
 * for n elements, adds `acc_init`, returns the int32 accumulator.
 * a/b must be MAC-reachable (not TCM). Returns false if no accelerator is
 * present on this platform (caller falls back to CPU). */
bool hal_mac_present(void);
bool hal_mac_dot(const int8_t *a, int32_t a_off, const int8_t *b, int32_t b_off,
                 uint32_t n, int32_t acc_init, int32_t *result, bool wait_irq);
/* Batched form: `rows` dot products of a fixed vector `a` against
 * consecutive rows of `b` (row stride n). Lets the HAL pipeline requests. */
bool hal_mac_dot_rows(const int8_t *a, int32_t a_off, const int8_t *b, int32_t b_off,
                      uint32_t n, uint32_t rows, const int32_t *acc_init,
                      int32_t *results, bool wait_irq);

/* ---------------- profiling markers ----------------
 * No-ops on host. On target they may write to a trace register so the
 * waveform / callgrind can be annotated. */
void hal_marker(uint32_t id);

#endif /* HAL_H */
