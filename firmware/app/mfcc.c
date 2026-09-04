/*
 * mfcc.c -- MFCC front-end matching TensorFlow audio_ops semantics
 * (periodic Hann, 1024-pt FFT, 40-band mel filterbank, log, 10-pt DCT),
 * quantised to int8 with the model's input scale / zero point.
 *
 * Determinism: no libm calls at runtime. FFT is CMSIS-DSP f32 (pure C on
 * both host and target; build with -ffp-contract=off so host and target
 * produce bit-identical results). log() is a local polynomial.
 */
#include <string.h>
#include "arm_math.h"
#include "placement.h"
#include "mfcc.h"
#include "kws_weights.h"

/* ------------------------------------------------------------------ */
/* buffers (placement knobs)                                           */
/* ------------------------------------------------------------------ */
static float   s_window[MFCC_WIN]        SECTION_MFCC_WORK;   /* 640 samples, sliding */
static float   s_fft_in[MFCC_NFFT]       SECTION_MFCC_WORK;
static float   s_fft_out[MFCC_NFFT]      SECTION_MFCC_WORK;
static float   s_mag[MFCC_NBINS]         SECTION_MFCC_WORK;
static float   s_mel[MFCC_NMEL]          SECTION_MFCC_WORK;
static float   s_coef[MFCC_NDCT]         SECTION_MFCC_WORK;
static arm_rfft_fast_instance_f32 s_rfft SECTION_MFCC_WORK;
static int     s_hops_seen               SECTION_MFCC_WORK;

/* ------------------------------------------------------------------ */
/* deterministic log                                                   */
/* ------------------------------------------------------------------ */
float det_logf(float x)
{
    /* x = m * 2^e, m in [1,2). log(x) = e*ln2 + log(m),
     * log(m) = 2*atanh(t), t = (m-1)/(m+1) in [0, 1/3). */
    union { float f; uint32_t u; } v;
    v.f = x;
    int e = (int)((v.u >> 23) & 0xFF) - 127;
    v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;   /* m in [1,2) */
    float m = v.f;
    float t = (m - 1.0f) / (m + 1.0f);
    float t2 = t * t;
    /* 2*(t + t^3/3 + t^5/5 + t^7/7 + t^9/9 + t^11/11) */
    float p = 1.0f / 11.0f;
    p = p * t2 + (1.0f / 9.0f);
    p = p * t2 + (1.0f / 7.0f);
    p = p * t2 + (1.0f / 5.0f);
    p = p * t2 + (1.0f / 3.0f);
    p = p * t2 + 1.0f;
    return (float)e * 0.69314718056f + 2.0f * t * p;
}

/* ------------------------------------------------------------------ */
void mfcc_init(void)
{
    memset(s_window, 0, sizeof(s_window));
    s_hops_seen = 0;
    arm_rfft_fast_init_f32(&s_rfft, MFCC_NFFT);
}

static void quantize(const float *coef, int8_t *out)
{
    const float inv = 1.0f / KWS_INPUT_SCALE;
    for (int i = 0; i < MFCC_NDCT; i++) {
        float q = coef[i] * inv + (float)KWS_INPUT_ZP;
        /* round half away from zero, deterministic */
        int32_t r = (int32_t)(q >= 0.0f ? q + 0.5f : q - 0.5f);
        if (r > 127) r = 127;
        if (r < -128) r = -128;
        out[i] = (int8_t)r;
    }
}

SECTION_CODE_MFCC
int mfcc_push_hop(const int16_t *hop, int8_t *out)
{
    /* slide: window = [old second half | new hop] */
    memmove(s_window, s_window + MFCC_HOP, (MFCC_WIN - MFCC_HOP) * sizeof(float));
    for (int i = 0; i < MFCC_HOP; i++)
        s_window[MFCC_WIN - MFCC_HOP + i] = (float)hop[i] * (1.0f / 32768.0f);

    if (++s_hops_seen < 2)
        return 0;

    /* window + zero pad */
    for (int i = 0; i < MFCC_WIN; i++)
        s_fft_in[i] = s_window[i] * mfcc_hann[i];
    memset(s_fft_in + MFCC_WIN, 0, (MFCC_NFFT - MFCC_WIN) * sizeof(float));

    arm_rfft_fast_f32(&s_rfft, s_fft_in, s_fft_out, 0);

    /* magnitude per bin. CMSIS packs DC re in [0], Nyquist re in [1]. */
    s_mag[0] = s_fft_out[0] < 0 ? -s_fft_out[0] : s_fft_out[0];
    s_mag[MFCC_NBINS - 1] = s_fft_out[1] < 0 ? -s_fft_out[1] : s_fft_out[1];
    for (int k = 1; k < MFCC_NBINS - 1; k++) {
        float re = s_fft_out[2 * k], im = s_fft_out[2 * k + 1];
        float p = re * re + im * im;
        float mag;
        arm_sqrt_f32(p, &mag);          /* IEEE sqrt on both host and target */
        s_mag[k] = mag;
    }

    /* mel filterbank (TF MfccMelFilterbank::Compute) */
    memset(s_mel, 0, sizeof(s_mel));
    for (int i = MFCC_START_INDEX; i <= MFCC_END_INDEX; i++) {
        float spec = s_mag[i];
        float weighted = spec * mfcc_mel_weights[i];
        int ch = mfcc_band_mapper[i];
        if (ch >= 0) s_mel[ch] += weighted;
        ch++;
        if (ch < MFCC_NMEL) s_mel[ch] += spec - weighted;
    }
    for (int c = 0; c < MFCC_NMEL; c++) {
        float v = s_mel[c];
        if (v < 1e-12f) v = 1e-12f;
        s_mel[c] = det_logf(v);
    }

    /* DCT */
    for (int i = 0; i < MFCC_NDCT; i++) {
        const float *row = &mfcc_dct[i * MFCC_NMEL];
        float sum = 0.0f;
        for (int j = 0; j < MFCC_NMEL; j++)
            sum += row[j] * s_mel[j];
        s_coef[i] = sum;
    }
    quantize(s_coef, out);
    return 1;
}

const float *mfcc_last_float(void) { return s_coef; }
