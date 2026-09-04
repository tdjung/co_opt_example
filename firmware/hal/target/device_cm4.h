/* device_cm4.h -- minimal CMSIS device header for the CM4 virtual platform. */
#ifndef DEVICE_CM4_H
#define DEVICE_CM4_H
typedef enum {
    NonMaskableInt_IRQn = -14, HardFault_IRQn = -13, MemoryManagement_IRQn = -12, BusFault_IRQn = -11,
    UsageFault_IRQn = -10, SVCall_IRQn = -5, DebugMonitor_IRQn = -4, PendSV_IRQn = -2, SysTick_IRQn = -1,
    IRQ0_IRQn = 0, IRQ1_IRQn, IRQ2_IRQn, IRQ3_IRQn, IRQ4_IRQn, IRQ5_IRQn, IRQ6_IRQn, IRQ7_IRQn,
    IRQ8_IRQn, IRQ9_IRQn, IRQ10_IRQn, IRQ11_IRQn, IRQ12_IRQn, IRQ13_IRQn, IRQ14_IRQn, IRQ15_IRQn,
} IRQn_Type;
#define __CM4_REV        0x0001U
#define __MPU_PRESENT    0U
#define __NVIC_PRIO_BITS 3U
#define __Vendor_SysTickConfig 0U
#define __FPU_PRESENT    1U
#include "core_cm4.h"
#endif
