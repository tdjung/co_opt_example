/*
 * hal_target.c -- HAL for the CM4 virtual platform.
 *
 * Written against the PROPOSED register map in platform_regs.h. Once the
 * real map is known, adapt platform_regs.h first; only touch this file where
 * the register semantics differ (e.g. no circular DMA mode, different IRQ
 * numbering, semihosting instead of a UART model).
 *
 * Every function here is short and single-purpose so the closed-network port
 * can be validated one peripheral at a time with tests/target_smoke.c.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "device_cm4.h"
#include "platform_regs.h"
#include "hal.h"

/* ---------------- lifecycle ---------------- */
void hal_init(void)
{
    /* DWT cycle counter as the canonical cycle source */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __enable_irq();
}

void hal_exit(int code)
{
    __disable_irq();
    SIM_EXIT = (uint32_t)code;
    for (;;) { __WFI(); }
}

uint32_t hal_cycles(void) { return DWT->CYCCNT; }
uint32_t hal_time_us(void) { return TIME_US; }

/* ---------------- logging ---------------- */
void hal_log_puts(const char *s)
{
    while (*s) LOG_TX = (uint32_t)(uint8_t)*s++;
}

void hal_log_printf(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    hal_log_puts(buf);
}

/* ---------------- interrupts ---------------- */
uint32_t hal_irq_save(void) { uint32_t p = __get_PRIMASK(); __disable_irq(); return p; }
void hal_irq_restore(uint32_t s) { __set_PRIMASK(s); }
void hal_wfi(void) { __WFI(); }

/* ---------------- audio DMA ---------------- */
static hal_audio_cb_t s_audio_cb;
static uint32_t s_audio_frame;
static int16_t *s_ring;
static uint32_t s_frame_len, s_n_frames;

void hal_audio_start(int16_t *ring, uint32_t frame_len, uint32_t n_frames,
                     uint32_t burst, hal_audio_cb_t on_frame)
{
    s_audio_cb = on_frame; s_audio_frame = 0;
    s_ring = ring; s_frame_len = frame_len; s_n_frames = n_frames;
    /* One DMA transfer per frame; the ISR re-arms for the next slot.
     * If the platform supports circular + half/full interrupts, use that
     * instead (DMA_CTRL_CIRC) and derive the slot from the counter. */
    DMA_SRC(DMA_CH_AUDIO) = (uint32_t)&AUDIO_FIFO;
    DMA_DST(DMA_CH_AUDIO) = (uint32_t)ring;
    DMA_LEN(DMA_CH_AUDIO) = frame_len * sizeof(int16_t);
    DMA_CTRL(DMA_CH_AUDIO) = DMA_CTRL_START | DMA_CTRL_IRQ_EN | DMA_CTRL_SRC_FIX | DMA_CTRL_BURST(burst);
    AUDIO_CTRL = 1;
    NVIC_EnableIRQ((IRQn_Type)DMA_IRQn);
}

void IRQ0_Handler(void)   /* DMA */
{
    uint32_t st = DMA_STATUS(DMA_CH_AUDIO);
    if (st & DMA_STATUS_DONE) {
        DMA_STATUS(DMA_CH_AUDIO) = DMA_STATUS_DONE;
        uint32_t done = s_audio_frame++;
        if (!(AUDIO_STATUS & AUDIO_STATUS_EOF)) {
            uint32_t slot = s_audio_frame % s_n_frames;
            DMA_DST(DMA_CH_AUDIO) = (uint32_t)(s_ring + slot * s_frame_len);
            DMA_CTRL(DMA_CH_AUDIO) |= DMA_CTRL_START;
        }
        if (s_audio_cb) s_audio_cb(done);
    }
    if (DMA_STATUS(DMA_CH_COPY) & DMA_STATUS_DONE)
        DMA_STATUS(DMA_CH_COPY) = DMA_STATUS_DONE;
}

bool hal_audio_eof(void)
{
    return (AUDIO_STATUS & AUDIO_STATUS_EOF) && !(DMA_CTRL(DMA_CH_AUDIO) & DMA_CTRL_START);
}

bool hal_dma_copy(void *dst, const void *src, size_t len, uint32_t burst)
{
    DMA_SRC(DMA_CH_COPY) = (uint32_t)src;
    DMA_DST(DMA_CH_COPY) = (uint32_t)dst;
    DMA_LEN(DMA_CH_COPY) = (uint32_t)len;
    DMA_STATUS(DMA_CH_COPY) = DMA_STATUS_DONE;
    DMA_CTRL(DMA_CH_COPY) = DMA_CTRL_START | DMA_CTRL_BURST(burst);
    while (!(DMA_STATUS(DMA_CH_COPY) & DMA_STATUS_DONE)) { }
    DMA_STATUS(DMA_CH_COPY) = DMA_STATUS_DONE;
    return true;
}

/* ---------------- timer ---------------- */
static hal_timer_cb_t s_timer_cb;
static uint32_t s_timer_period_cycles, s_timer_latency;

void hal_timer_start(uint32_t period_us, hal_timer_cb_t cb)
{
    s_timer_cb = cb;
    s_timer_period_cycles = (uint32_t)((uint64_t)period_us * CPU_HZ / 1000000u);
    TIMER_LOAD = s_timer_period_cycles;
    TIMER_STATUS = 1;
    TIMER_CTRL = TIMER_CTRL_EN | TIMER_CTRL_IRQ_EN;
    NVIC_SetPriority((IRQn_Type)TIMER_IRQn, 0);       /* highest: control loop */
    NVIC_SetPriority((IRQn_Type)DMA_IRQn, 1);
    NVIC_SetPriority((IRQn_Type)MAC_IRQn, 2);
    NVIC_EnableIRQ((IRQn_Type)TIMER_IRQn);
}

void IRQ1_Handler(void)   /* timer */
{
    /* latency = cycles since expiry = period - remaining down-count */
    s_timer_latency = s_timer_period_cycles - TIMER_VALUE;
    TIMER_STATUS = 1;
    if (s_timer_cb) s_timer_cb();
}

uint32_t hal_timer_latency_cycles(void) { return s_timer_latency; }

/* ---------------- control peripherals ---------------- */
int32_t hal_sensor_read(void) { return (int32_t)SENSOR_VALUE; }
void hal_actuator_write(int32_t v) { ACTUATOR_VALUE = (uint32_t)v; }

void hal_result_write(uint32_t infer_idx, int label, int score_q7, uint32_t cycles)
{
    RESULT_LABEL = (uint32_t)label; RESULT_SCORE = (uint32_t)score_q7;
    RESULT_CYCLES = cycles; RESULT_INFER_IDX = infer_idx;
}

void hal_marker(uint32_t id) { TRACE_MARKER = id; }

/* ---------------- MAC accelerator ---------------- */
static volatile uint32_t s_mac_done;
void IRQ2_Handler(void) { MAC_STATUS = MAC_STATUS_DONE; s_mac_done = 1; }

bool hal_mac_present(void) { return MAC_ID != 0; }

bool hal_mac_dot_rows(const int8_t *a, int32_t a_off, const int8_t *b, int32_t b_off,
                      uint32_t n, uint32_t rows, const int32_t *acc_init,
                      int32_t *results, bool wait_irq)
{
    if (!hal_mac_present()) return false;
    MAC_SRC_A = (uint32_t)a; MAC_SRC_B = (uint32_t)b;
    MAC_LEN = n; MAC_ROWS = rows;
    MAC_OFF_A = (uint32_t)a_off; MAC_OFF_B = (uint32_t)b_off;
    MAC_ACC_INIT = (uint32_t)acc_init; MAC_RESULT = (uint32_t)results;
    MAC_STATUS = MAC_STATUS_DONE;
    if (wait_irq) {
        s_mac_done = 0;
        NVIC_EnableIRQ((IRQn_Type)MAC_IRQn);
        MAC_CTRL = MAC_CTRL_START | MAC_CTRL_IRQ_EN;
        while (!s_mac_done) __WFI();
    } else {
        MAC_CTRL = MAC_CTRL_START;
        while (MAC_STATUS & MAC_STATUS_BUSY) { }
        MAC_STATUS = MAC_STATUS_DONE;
    }
    return true;
}

bool hal_mac_dot(const int8_t *a, int32_t a_off, const int8_t *b, int32_t b_off,
                 uint32_t n, int32_t acc_init, int32_t *result, bool wait_irq)
{
    return hal_mac_dot_rows(a, a_off, b, b_off, n, 1, &acc_init, result, wait_irq);
}
