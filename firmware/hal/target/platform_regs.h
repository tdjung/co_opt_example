/*
 * platform_regs.h -- register map of the CM4 virtual platform.
 *
 * !!! TEMPLATE !!!  Every address / bit below is a PLACEHOLDER that must be
 * replaced with the real values from the platform's JSON IR and model sources.
 * Keep the macro names: hal_target.c only uses these.
 *
 * Proposed memory map (see docs/MEMORY_MAP.md); adjust to the platform.
 */
#ifndef PLATFORM_REGS_H
#define PLATFORM_REGS_H
#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(a))

/* ---------------- memory regions (informational; the linker script owns them) */
#define PLAT_FLASH_BASE   0x00000000u
#define PLAT_ITCM_BASE    0x10000000u
#define PLAT_DTCM_BASE    0x20000000u
#define PLAT_SRAM0_BASE   0x24000000u
#define PLAT_PERIPH_BASE  0x40000000u

/* ---------------- DMA (2..4 channels) ---------------- */
#define DMA_BASE          (PLAT_PERIPH_BASE + 0x0000u)
#define DMA_CH_STRIDE     0x40u
#define DMA_SRC(ch)       REG32(DMA_BASE + (ch) * DMA_CH_STRIDE + 0x00)
#define DMA_DST(ch)       REG32(DMA_BASE + (ch) * DMA_CH_STRIDE + 0x04)
#define DMA_LEN(ch)       REG32(DMA_BASE + (ch) * DMA_CH_STRIDE + 0x08)   /* bytes */
#define DMA_CTRL(ch)      REG32(DMA_BASE + (ch) * DMA_CH_STRIDE + 0x0C)
#define DMA_STATUS(ch)    REG32(DMA_BASE + (ch) * DMA_CH_STRIDE + 0x10)
#define DMA_CTRL_START    (1u << 0)
#define DMA_CTRL_IRQ_EN   (1u << 1)
#define DMA_CTRL_CIRC     (1u << 2)      /* wrap dst after LEN (audio ring) */
#define DMA_CTRL_SRC_FIX  (1u << 3)      /* source is a FIFO register */
#define DMA_CTRL_BURST(b) (((b) & 0x1Fu) << 8)   /* beats per burst */
#define DMA_STATUS_DONE   (1u << 0)      /* write 1 to clear */
#define DMA_STATUS_HALF   (1u << 1)
#define DMA_CH_AUDIO      0
#define DMA_CH_COPY       1
#define DMA_IRQn          0              /* IRQ0_Handler */

/* ---------------- Timer ---------------- */
#define TIMER_BASE        (PLAT_PERIPH_BASE + 0x1000u)
#define TIMER_LOAD        REG32(TIMER_BASE + 0x00)   /* period in cycles */
#define TIMER_VALUE       REG32(TIMER_BASE + 0x04)   /* current down-count */
#define TIMER_CTRL        REG32(TIMER_BASE + 0x08)
#define TIMER_STATUS      REG32(TIMER_BASE + 0x0C)   /* bit0 = expired, W1C */
#define TIMER_CTRL_EN     (1u << 0)
#define TIMER_CTRL_IRQ_EN (1u << 1)
#define TIMER_IRQn        1              /* IRQ1_Handler */

/* ---------------- Audio source (WAV -> FIFO) ---------------- */
#define AUDIO_BASE        (PLAT_PERIPH_BASE + 0x2000u)
#define AUDIO_FIFO        REG32(AUDIO_BASE + 0x00)   /* int16 sample in low half */
#define AUDIO_CTRL        REG32(AUDIO_BASE + 0x04)   /* bit0 = enable */
#define AUDIO_STATUS      REG32(AUDIO_BASE + 0x08)   /* bit0 = EOF, bit1 = fifo empty */
#define AUDIO_STATUS_EOF  (1u << 0)

/* ---------------- Sensor / actuator / result ---------------- */
#define CTRL_BASE         (PLAT_PERIPH_BASE + 0x3000u)
#define SENSOR_VALUE      REG32(CTRL_BASE + 0x00)    /* plant y, updated every 1 ms */
#define ACTUATOR_VALUE    REG32(CTRL_BASE + 0x04)    /* plant u */
#define RESULT_LABEL      REG32(CTRL_BASE + 0x10)
#define RESULT_SCORE      REG32(CTRL_BASE + 0x14)
#define RESULT_CYCLES     REG32(CTRL_BASE + 0x18)
#define RESULT_INFER_IDX  REG32(CTRL_BASE + 0x1C)
#define TRACE_MARKER      REG32(CTRL_BASE + 0x20)    /* write = profiling marker */
#define SIM_EXIT          REG32(CTRL_BASE + 0x24)    /* write = end simulation, value = exit code */
#define CYCLE_COUNTER     REG32(CTRL_BASE + 0x28)    /* or DWT->CYCCNT */
#define TIME_US           REG32(CTRL_BASE + 0x2C)

/* ---------------- Log (semihosting or UART model) ---------------- */
#define LOG_TX            REG32(PLAT_PERIPH_BASE + 0x4000u)   /* write a byte */

/* ---------------- MAC accelerator (docs/MAC_ACCEL_SPEC.md) ---------------- */
#define MAC_BASE          (PLAT_PERIPH_BASE + 0x5000u)
#define MAC_SRC_A         REG32(MAC_BASE + 0x00)
#define MAC_SRC_B         REG32(MAC_BASE + 0x04)
#define MAC_LEN           REG32(MAC_BASE + 0x08)     /* elements per dot product */
#define MAC_ROWS          REG32(MAC_BASE + 0x0C)     /* number of B rows */
#define MAC_OFF_A         REG32(MAC_BASE + 0x10)     /* int32 offsets */
#define MAC_OFF_B         REG32(MAC_BASE + 0x14)
#define MAC_ACC_INIT      REG32(MAC_BASE + 0x18)     /* pointer to int32[rows] or 0 */
#define MAC_RESULT        REG32(MAC_BASE + 0x1C)     /* pointer to int32[rows] */
#define MAC_CTRL          REG32(MAC_BASE + 0x20)
#define MAC_STATUS        REG32(MAC_BASE + 0x24)
#define MAC_ID            REG32(MAC_BASE + 0x28)     /* 0 = not present, else lanes */
#define MAC_CTRL_START    (1u << 0)
#define MAC_CTRL_IRQ_EN   (1u << 1)
#define MAC_STATUS_BUSY   (1u << 0)
#define MAC_STATUS_DONE   (1u << 1)                  /* W1C */
#define MAC_IRQn          2                          /* IRQ2_Handler */

#endif
