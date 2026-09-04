/*
 * hal_host.c -- host (x86) HAL for golden generation.
 *
 * Usage: kws_host --wav <file.wav> [--wav <file2.wav> ...] [--mac]
 *   --wav   16 kHz / 16-bit / mono WAV; several files are concatenated
 *   --mac   emulate the MAC accelerator in software (exercises offload path)
 *
 * Simulated time advances 20 ms per audio frame; the 1 kHz timer callback is
 * delivered 20 times per frame (before the frame callback), mirroring the
 * order the target would see (timer IRQ preempts main loop work).
 * Cycle counter = simulated time * CPU_HZ; host compute time is NOT measured.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "hal.h"

#ifndef CPU_HZ
#define CPU_HZ 100000000u
#endif

/* ---------------- state ---------------- */
static int16_t  *g_audio;        /* all samples */
static size_t    g_audio_len, g_audio_pos;
static int16_t  *g_ring;
static uint32_t  g_frame_len, g_n_frames, g_frame_idx;
static hal_audio_cb_t g_on_frame;
static hal_timer_cb_t g_timer_cb;
static uint32_t  g_timer_period_us;
static uint64_t  g_time_us;
static int32_t   g_plant_y, g_plant_u;
static bool      g_mac;

/* ---------------- WAV loader ---------------- */
static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void load_wav(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n);
    if (fread(b, 1, n, f) != (size_t)n) { fprintf(stderr, "short read %s\n", path); exit(2); }
    fclose(f);
    if (n < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { fprintf(stderr, "%s: not WAV\n", path); exit(2); }
    long p = 12; int ch = 0, rate = 0, bits = 0; const unsigned char *data = NULL; uint32_t dlen = 0;
    while (p + 8 <= n) {
        uint32_t sz = rd32(b + p + 4);
        if (!memcmp(b + p, "fmt ", 4)) { ch = rd16(b + p + 10); rate = rd32(b + p + 12); bits = rd16(b + p + 22); }
        else if (!memcmp(b + p, "data", 4)) { data = b + p + 8; dlen = sz; break; }
        p += 8 + sz + (sz & 1);
    }
    if (!data || ch != 1 || rate != 16000 || bits != 16) {
        fprintf(stderr, "%s: need 16 kHz mono 16-bit (got ch=%d rate=%d bits=%d)\n", path, ch, rate, bits); exit(2);
    }
    size_t ns = dlen / 2;
    g_audio = realloc(g_audio, (g_audio_len + ns) * sizeof(int16_t));
    for (size_t i = 0; i < ns; i++) g_audio[g_audio_len + i] = (int16_t)rd16(data + 2 * i);
    g_audio_len += ns;
    free(b);
}

void hal_host_parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--wav") && i + 1 < argc) load_wav(argv[++i]);
        else if (!strcmp(argv[i], "--mac")) g_mac = true;
        else { fprintf(stderr, "unknown arg %s\n", argv[i]); exit(2); }
    }
#ifndef HAL_HOST_NO_MAIN
    if (!g_audio_len) { fprintf(stderr, "no --wav given\n"); exit(2); }
#endif
}

/* ---------------- lifecycle ---------------- */
void hal_init(void) {}
void hal_exit(int code) { fflush(stdout); exit(code); }
uint32_t hal_cycles(void) { return (uint32_t)(g_time_us * (CPU_HZ / 1000000u)); }
uint32_t hal_time_us(void) { return (uint32_t)g_time_us; }

/* ---------------- log ---------------- */
void hal_log_puts(const char *s) { fputs(s, stdout); }
void hal_log_printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }

/* ---------------- irq ---------------- */
uint32_t hal_irq_save(void) { return 0; }
void hal_irq_restore(uint32_t s) { (void)s; }

static void deliver_timer_ticks(uint32_t n)
{
    for (uint32_t k = 0; k < n; k++) {
        g_time_us += g_timer_period_us;
        /* plant update, then ISR */
        g_plant_y += ((g_plant_u - g_plant_y) * 13) >> 8;
        if (g_timer_cb) g_timer_cb();
    }
}

/* Advance one audio frame: 20 timer ticks, then DMA completes a frame. */
void hal_wfi(void)
{
    if (g_audio_pos >= g_audio_len) return;
    uint32_t ticks = (g_frame_len * 1000000u / 16000u) / g_timer_period_us;   /* 20 */
    deliver_timer_ticks(ticks);
    int16_t *slot = g_ring + (g_frame_idx % g_n_frames) * g_frame_len;
    for (uint32_t i = 0; i < g_frame_len; i++)
        slot[i] = (g_audio_pos < g_audio_len) ? g_audio[g_audio_pos++] : 0;
    if (g_on_frame) g_on_frame(g_frame_idx);
    g_frame_idx++;
}

/* ---------------- audio ---------------- */
void hal_audio_start(int16_t *ring, uint32_t frame_len, uint32_t n_frames, uint32_t burst, hal_audio_cb_t cb)
{
    (void)burst;
    g_ring = ring; g_frame_len = frame_len; g_n_frames = n_frames; g_on_frame = cb; g_frame_idx = 0;
}
bool hal_audio_eof(void) { return g_audio_pos >= g_audio_len; }

bool hal_dma_copy(void *dst, const void *src, size_t len, uint32_t burst)
{
    (void)burst; memcpy(dst, src, len); return true;
}

/* ---------------- timer ---------------- */
void hal_timer_start(uint32_t period_us, hal_timer_cb_t cb) { g_timer_period_us = period_us ? period_us : 1000; g_timer_cb = cb; }
uint32_t hal_timer_latency_cycles(void) { return 0; }

/* ---------------- control peripherals ---------------- */
int32_t hal_sensor_read(void) { return g_plant_y; }
void hal_actuator_write(int32_t v) { g_plant_u = v; }

/* ---------------- result ---------------- */
void hal_result_write(uint32_t infer_idx, int label, int score_q7, uint32_t cycles)
{
    printf("RESULT infer=%u label=%d score=%d cycles=%u\n", (unsigned)infer_idx, label, score_q7, (unsigned)cycles);
}

/* ---------------- MAC emulation ---------------- */
bool hal_mac_present(void) { return g_mac; }

bool hal_mac_dot(const int8_t *a, int32_t a_off, const int8_t *b, int32_t b_off,
                 uint32_t n, int32_t acc_init, int32_t *result, bool wait_irq)
{
    (void)wait_irq;
    if (!g_mac) return false;
    int32_t acc = acc_init;
    for (uint32_t i = 0; i < n; i++) acc += (a[i] + a_off) * (b[i] + b_off);
    *result = acc;
    return true;
}

bool hal_mac_dot_rows(const int8_t *a, int32_t a_off, const int8_t *b, int32_t b_off,
                      uint32_t n, uint32_t rows, const int32_t *acc_init, int32_t *results, bool wait_irq)
{
    if (!g_mac) return false;
    for (uint32_t r = 0; r < rows; r++)
        hal_mac_dot(a, a_off, b + r * n, b_off, n, acc_init ? acc_init[r] : 0, &results[r], wait_irq);
    return true;
}

void hal_marker(uint32_t id) { (void)id; }

/* host entry wraps app main */
#ifndef HAL_HOST_NO_MAIN
extern int app_main(void);
int main(int argc, char **argv)
{
    hal_host_parse_args(argc, argv);
    return app_main();
}
#endif
