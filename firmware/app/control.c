/*
 * control.c -- 1 kHz PID loop in timer ISR.
 *
 * Plant model (implemented by the HAL / platform sensor register, see
 * docs/HAL_SPEC.md): y[k+1] = y[k] + (((u[k] - y[k]) * 13) >> 8), all int32.
 * The controller only sees y through hal_sensor_read() and drives u through
 * hal_actuator_write(). Integer-only so host and target sequences match.
 */
#include "placement.h"
#include "hal.h"
#include "control.h"

typedef struct {
    int32_t  integ, prev_err;
    uint32_t ticks, missed, max_latency, latency_sum;
    uint32_t seq_cksum;
    int32_t  last_u, last_y;
} ctrl_t;

static ctrl_t s_c SECTION_CTRL;

/* gains in Q8 */
#define KP 96
#define KI 20
#define KD 24
#define U_MIN (-2000)
#define U_MAX  2000

SECTION_CODE_ISR
static void control_tick(void)
{
    uint32_t lat = hal_timer_latency_cycles();
    uint32_t t_ms = hal_time_us() / 1000;
    int32_t  sp = (t_ms >= CTRL_SETPOINT_STEP_MS) ? 1000 : 0;
    int32_t  y = hal_sensor_read();
    int32_t  err = sp - y;
    s_c.integ += err;
    if (s_c.integ > 60000) s_c.integ = 60000;
    if (s_c.integ < -60000) s_c.integ = -60000;
    int32_t u = (KP * err + KI * s_c.integ + KD * (err - s_c.prev_err)) >> 8;
    if (u > U_MAX) u = U_MAX;
    if (u < U_MIN) u = U_MIN;
    s_c.prev_err = err;
    hal_actuator_write(u);

    s_c.last_u = u; s_c.last_y = y;
    s_c.ticks++;
    s_c.latency_sum += lat;
    if (lat > s_c.max_latency) s_c.max_latency = lat;
    s_c.seq_cksum = (s_c.seq_cksum ^ (uint32_t)u ^ ((uint32_t)y << 16)) * 16777619u;
}

void control_init(void)
{
    s_c = (ctrl_t){0};
    hal_timer_start(CTRL_PERIOD_US, control_tick);
}

void control_report(void)
{
    hal_log_printf("CTRL ticks=%u max_latency_cycles=%u avg_latency_cycles=%u seq_cksum=%08x last_y=%d last_u=%d\n",
                   (unsigned)s_c.ticks, (unsigned)s_c.max_latency,
                   (unsigned)(s_c.ticks ? s_c.latency_sum / s_c.ticks : 0),
                   (unsigned)s_c.seq_cksum, (int)s_c.last_y, (int)s_c.last_u);
}
