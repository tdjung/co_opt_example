/*
 * main.c -- KWS + control-loop application entry.
 *
 * Flow:
 *   hal_init -> kws_init (MFCC, weights) -> control_init (1 kHz timer)
 *   -> hal_audio_start (DMA fills ring, ISR marks frames)
 *   -> main loop: wait frame -> kws_process_hop
 *   -> at EOF: print RUN summary, hal_exit(0)
 *
 * Log line formats consumed by tools/harness (do not change without updating it):
 *   MFCC frame=<f> <10 ints>
 *   PROF frame=<f> mfcc_cycles=<c>
 *   [infer <n>] frame=<f> t=<ms>ms top1=<label> (<0..255>) top2=... cycles=<c>
 *   GOLDEN infer=<n> frame=<f> logits=<12 ints> probs=<12 ints> cksum=<11 hex>
 *   LAYERS infer=<n> <name>=<cycles> ...
 *   CTRL ticks=... max_latency_cycles=... seq_cksum=...
 *   RUN frames=<f> inferences=<n> total_cycles=<c> deadline_us=<d> deadline_miss=<m> max_infer_cycles=<c>
 */
#include <string.h>
#include "placement.h"
#include "hal.h"
#include "kws.h"
#include "control.h"
#include "mfcc_tables.h"

#ifndef CPU_HZ
#define CPU_HZ 100000000u
#endif

static int16_t  s_ring[AUDIO_RING_FRAMES * MFCC_HOP] SECTION_AUDIO_RING;
static volatile uint32_t s_frames_ready SECTION_AUDIO_RING;   /* produced by ISR */
static uint32_t s_frames_taken SECTION_AUDIO_RING;
static uint32_t s_overruns SECTION_AUDIO_RING;

static void on_audio_frame(uint32_t frame_idx)
{
    (void)frame_idx;
    s_frames_ready++;
}

#ifdef HOST_BUILD
int app_main(void)
#else
int main(void)
#endif
{
    hal_init();
    hal_log_puts("BOOT kws+ctrl\n");
    kws_init();
    control_init();
    hal_audio_start(s_ring, MFCC_HOP, AUDIO_RING_FRAMES, AUDIO_DMA_BURST, on_audio_frame);

    uint32_t t_start = hal_cycles();
    uint32_t max_infer = 0, misses = 0, last_infer = 0;
    const uint32_t deadline_cycles = (uint32_t)((uint64_t)KWS_DEADLINE_US * CPU_HZ / 1000000u);

    for (;;) {
        while (s_frames_ready == s_frames_taken) {
            if (hal_audio_eof()) goto done;
            hal_wfi();
        }
        uint32_t pending = s_frames_ready - s_frames_taken;
        if (pending > AUDIO_RING_FRAMES) {
            s_overruns += pending - AUDIO_RING_FRAMES;
            s_frames_taken = s_frames_ready - AUDIO_RING_FRAMES;
        }
        uint32_t idx = s_frames_taken;
        const int16_t *hop = &s_ring[(idx % AUDIO_RING_FRAMES) * MFCC_HOP];
        uint32_t t0 = hal_cycles();
        kws_process_hop(hop, idx);
        uint32_t dt = hal_cycles() - t0;
        if (kws_inferences_done() != last_infer) {
            last_infer = kws_inferences_done();
            if (dt > max_infer) max_infer = dt;
            if (dt > deadline_cycles) misses++;
        }
        s_frames_taken++;
    }
done:
    control_report();
    hal_log_printf("RUN frames=%u inferences=%u total_cycles=%u deadline_us=%u deadline_miss=%u max_infer_cycles=%u overruns=%u\n",
                   (unsigned)s_frames_taken, (unsigned)kws_inferences_done(),
                   (unsigned)(hal_cycles() - t_start), (unsigned)KWS_DEADLINE_US,
                   (unsigned)misses, (unsigned)max_infer, (unsigned)s_overruns);
    hal_exit(0);
    return 0;
}
