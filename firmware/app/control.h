#ifndef CONTROL_H
#define CONTROL_H
#include <stdint.h>
#define CTRL_PERIOD_US       1000
#define CTRL_MAX_LATENCY_US  20
#define CTRL_SETPOINT_STEP_MS 300   /* setpoint 0 -> 1000 at t=300 ms */

void control_init(void);           /* starts the 1 kHz timer */
void control_report(void);         /* prints CTRL summary line */
#endif
