/*
 * smoke.c -- target HAL smoke test. Build: cd firmware && make -f target.mk SMOKE=1
 *
 * Exercises each peripheral through the HAL and prints PASS/FAIL lines:
 *   T1 log + cycle counter monotonic
 *   T2 timer: ~20 ticks in 20 ms, latency reported
 *   T3 DMA copy SRAM->SRAM matches memcpy
 *   T4 audio DMA: receives >= 2 frames, samples non-constant (needs a WAV loaded)
 *   T5 MAC: hal_mac_dot_rows == CPU reference (skipped if absent)
 *   T6 sensor/actuator: plant responds to actuator writes
 * Ends with "SMOKE done pass=<n> fail=<m>" then hal_exit(fail).
 */
#include <string.h>
#include "hal.h"
#include "placement.h"

#ifndef CPU_HZ
#define CPU_HZ 100000000u
#endif

static int s_pass, s_fail;
static void check(const char *name, int ok)
{
    hal_log_printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (ok) s_pass++; else s_fail++;
}

static volatile uint32_t s_ticks;
static void on_tick(void) { s_ticks++; }
static volatile uint32_t s_frames;
static void on_frame(uint32_t i) { (void)i; s_frames++; }

static int16_t s_ring[2 * 320] PLACE_DATA(REGION_SRAM0);
static int8_t  s_a[64] PLACE_DATA(REGION_SRAM0), s_b[4 * 64] PLACE_DATA(REGION_SRAM0);
static uint8_t s_src[1024] PLACE_DATA(REGION_SRAM0), s_dst[1024] PLACE_DATA(REGION_SRAM1);

static void spin_us(uint32_t us)
{
    uint32_t t0 = hal_cycles(), n = us * (CPU_HZ / 1000000u);
    while (hal_cycles() - t0 < n) { }
}

int main(void)
{
    hal_init();
    hal_log_puts("SMOKE start\n");

    /* T1 */
    uint32_t c0 = hal_cycles(); spin_us(10); uint32_t c1 = hal_cycles();
    check("T1 cycles monotonic", c1 > c0 && (c1 - c0) >= 10 * (CPU_HZ / 1000000u));

    /* T2 */
    hal_timer_start(1000, on_tick);
    spin_us(20500);
    hal_log_printf("  ticks=%u latency=%u cycles\n", (unsigned)s_ticks, (unsigned)hal_timer_latency_cycles());
    check("T2 timer ticks 19..22", s_ticks >= 19 && s_ticks <= 22);
    check("T2 timer latency < 2000 cycles", hal_timer_latency_cycles() < 2000);

    /* T3 */
    for (int i = 0; i < 1024; i++) { s_src[i] = (uint8_t)(i * 7 + 3); s_dst[i] = 0; }
    int ok = hal_dma_copy(s_dst, s_src, 1024, 4) && !memcmp(s_src, s_dst, 1024);
    check("T3 dma copy 1024B burst4", ok);
    memset(s_dst, 0, 1024);
    ok = hal_dma_copy(s_dst, s_src, 1000, 16) && !memcmp(s_src, s_dst, 1000) && s_dst[1000] == 0;
    check("T3 dma copy 1000B burst16", ok);

    /* T4 */
    hal_audio_start(s_ring, 320, 2, 1, on_frame);
    spin_us(45000);
    int nonconst = 0;
    for (int i = 1; i < 320; i++) if (s_ring[i] != s_ring[0]) nonconst = 1;
    hal_log_printf("  frames=%u eof=%d\n", (unsigned)s_frames, (int)hal_audio_eof());
    check("T4 audio >=2 frames in 45ms", s_frames >= 2);
    check("T4 audio samples non-constant (needs WAV)", nonconst);

    /* T5 */
    if (hal_mac_present()) {
        for (int i = 0; i < 64; i++) s_a[i] = (int8_t)(i * 5 - 100);
        for (int i = 0; i < 256; i++) s_b[i] = (int8_t)(i * 3 - 128);
        int32_t init[4] = { 1000, -1000, 0, 7 }, res[4] = { 0 }, ref[4];
        for (int r = 0; r < 4; r++) {
            int32_t acc = init[r];
            for (int i = 0; i < 64; i++) acc += (s_a[i] + 12) * (s_b[r * 64 + i] + 0);
            ref[r] = acc;
        }
        ok = hal_mac_dot_rows(s_a, 12, s_b, 0, 64, 4, init, res, false) && !memcmp(ref, res, sizeof ref);
        check("T5 mac poll == cpu", ok);
        memset(res, 0, sizeof res);
        ok = hal_mac_dot_rows(s_a, 12, s_b, 0, 64, 4, init, res, true) && !memcmp(ref, res, sizeof ref);
        check("T5 mac irq == cpu", ok);
    } else {
        hal_log_puts("SKIP T5 mac not present\n");
    }

    /* T6 */
    int32_t y0 = hal_sensor_read();
    hal_actuator_write(1000);
    spin_us(5000);
    int32_t y1 = hal_sensor_read();
    hal_log_printf("  y0=%d y1=%d\n", (int)y0, (int)y1);
    check("T6 plant moves toward actuator", y1 > y0);

    hal_log_printf("SMOKE done pass=%d fail=%d\n", s_pass, s_fail);
    hal_exit(s_fail);
    return 0;
}
