/*
 * placement.h -- SW optimisation knobs.
 *
 * This is the ONE file the optimisation loop edits for data/code placement and
 * SW strategy. Every knob is a macro so a change is a one-line diff.
 *
 * Memory regions (must match link/cm4_template.ld and the platform IR):
 *   REGION_FLASH  - execute/read in place, slow (wait states)
 *   REGION_ITCM   - instruction TCM (code only), no bus
 *   REGION_DTCM   - data TCM, no bus, NOT reachable by DMA / MAC
 *   REGION_SRAM0..3 - system SRAM banks on the AHB matrix (DMA / MAC reachable)
 *
 * Placing an object in a region: the macro expands to a section attribute
 * ".place.<region>" that the linker script maps to the region.
 */
#ifndef PLACEMENT_H
#define PLACEMENT_H

/* ------------------------------------------------------------------ */
/* Region names                                                        */
/* ------------------------------------------------------------------ */
#define REGION_FLASH  flash
#define REGION_ITCM   itcm
#define REGION_DTCM   dtcm
#define REGION_SRAM0  sram0
#define REGION_SRAM1  sram1
#define REGION_SRAM2  sram2
#define REGION_SRAM3  sram3

#ifdef HOST_BUILD
#define PLACE_DATA(region)
#define PLACE_CODE(region)
#else
#define PLACE_STR2(x) #x
#define PLACE_STR(x) PLACE_STR2(x)
#define PLACE_DATA(region) __attribute__((section(".place." PLACE_STR(region))))
#define PLACE_CODE(region) __attribute__((section(".place_code." PLACE_STR(region))))
#endif

/* ------------------------------------------------------------------ */
/* KNOB: data placement (baseline = deliberately mediocre)             */
/* ------------------------------------------------------------------ */
/* Model weights, per layer. Baseline: everything executes from Flash. */
#ifndef SECTION_WEIGHTS_CONV1
#define SECTION_WEIGHTS_CONV1  PLACE_DATA(REGION_FLASH)
#endif
#ifndef SECTION_WEIGHTS_DW1
#define SECTION_WEIGHTS_DW1    PLACE_DATA(REGION_FLASH)
#endif
#ifndef SECTION_WEIGHTS_PW1
#define SECTION_WEIGHTS_PW1    PLACE_DATA(REGION_FLASH)
#endif
#ifndef SECTION_WEIGHTS_DW2
#define SECTION_WEIGHTS_DW2    PLACE_DATA(REGION_FLASH)
#endif
#ifndef SECTION_WEIGHTS_PW2
#define SECTION_WEIGHTS_PW2    PLACE_DATA(REGION_FLASH)
#endif
#ifndef SECTION_WEIGHTS_DW3
#define SECTION_WEIGHTS_DW3    PLACE_DATA(REGION_FLASH)
#endif
#ifndef SECTION_WEIGHTS_PW3
#define SECTION_WEIGHTS_PW3    PLACE_DATA(REGION_FLASH)
#endif
#ifndef SECTION_WEIGHTS_DW4
#define SECTION_WEIGHTS_DW4    PLACE_DATA(REGION_FLASH)
#endif
#ifndef SECTION_WEIGHTS_PW4
#define SECTION_WEIGHTS_PW4    PLACE_DATA(REGION_FLASH)
#endif
#ifndef SECTION_WEIGHTS_FC
#define SECTION_WEIGHTS_FC     PLACE_DATA(REGION_FLASH)
#endif

/* Activation ping-pong buffers (2 x 8000 B) and NN scratch */
#ifndef SECTION_ACT_A
#define SECTION_ACT_A          PLACE_DATA(REGION_SRAM0)
#endif
#ifndef SECTION_ACT_B
#define SECTION_ACT_B          PLACE_DATA(REGION_SRAM0)
#endif
#ifndef SECTION_NN_SCRATCH
#define SECTION_NN_SCRATCH     PLACE_DATA(REGION_SRAM0)
#endif

/* MFCC: feature window (49x10 int8), FFT work buffers, mel tables */
#ifndef SECTION_FEATURES
#define SECTION_FEATURES       PLACE_DATA(REGION_SRAM0)
#endif
#ifndef SECTION_MFCC_WORK
#define SECTION_MFCC_WORK      PLACE_DATA(REGION_SRAM0)
#endif
#ifndef SECTION_MFCC_TABLES
#define SECTION_MFCC_TABLES    PLACE_DATA(REGION_FLASH)
#endif

/* Audio DMA ring buffer (written by DMA -> must NOT be DTCM) */
#ifndef SECTION_AUDIO_RING
#define SECTION_AUDIO_RING     PLACE_DATA(REGION_SRAM0)
#endif

/* Control loop state */
#ifndef SECTION_CTRL
#define SECTION_CTRL           PLACE_DATA(REGION_SRAM0)
#endif

/* Hot code (baseline: Flash) */
#ifndef SECTION_CODE_MFCC
#define SECTION_CODE_MFCC      PLACE_CODE(REGION_FLASH)
#endif
#ifndef SECTION_CODE_NN
#define SECTION_CODE_NN        PLACE_CODE(REGION_FLASH)
#endif
#ifndef SECTION_CODE_ISR
#define SECTION_CODE_ISR       PLACE_CODE(REGION_FLASH)
#endif

/* ------------------------------------------------------------------ */
/* KNOB: weight loading strategy                                       */
/*   0 = use weights in place (wherever SECTION_WEIGHTS_* put them)   */
/*   1 = at boot, copy all weights Flash -> KWS_WEIGHT_COPY_REGION    */
/*       with CPU memcpy                                              */
/*   2 = at boot, copy with DMA (region must be DMA reachable)        */
/* ------------------------------------------------------------------ */
#ifndef KWS_WEIGHT_LOAD_MODE
#define KWS_WEIGHT_LOAD_MODE   0
#endif
#ifndef SECTION_WEIGHT_COPY
#define SECTION_WEIGHT_COPY    PLACE_DATA(REGION_SRAM1)
#endif

/* ------------------------------------------------------------------ */
/* KNOB: MAC accelerator offload, per layer (0 = CPU CMSIS-NN,        */
/*   1 = accelerator). Ignored when the platform has no MAC unit.      */
/* ------------------------------------------------------------------ */
#ifndef OFFLOAD_CONV1
#define OFFLOAD_CONV1  0
#endif
#ifndef OFFLOAD_PW1
#define OFFLOAD_PW1    0
#endif
#ifndef OFFLOAD_PW2
#define OFFLOAD_PW2    0
#endif
#ifndef OFFLOAD_PW3
#define OFFLOAD_PW3    0
#endif
#ifndef OFFLOAD_PW4
#define OFFLOAD_PW4    0
#endif
#ifndef OFFLOAD_FC
#define OFFLOAD_FC     0
#endif
/* 0 = poll status register, 1 = wait for completion interrupt (WFI) */
#ifndef OFFLOAD_WAIT_IRQ
#define OFFLOAD_WAIT_IRQ 0
#endif

/* ------------------------------------------------------------------ */
/* KNOB: DMA / buffering                                               */
/* ------------------------------------------------------------------ */
#ifndef AUDIO_DMA_BURST
#define AUDIO_DMA_BURST        1      /* beats per burst: 1/4/8/16 */
#endif
#ifndef AUDIO_RING_FRAMES
#define AUDIO_RING_FRAMES      2      /* 2 = double buffering, 3 = triple */
#endif

/* ------------------------------------------------------------------ */
/* KNOB: inference scheduling                                          */
/* ------------------------------------------------------------------ */
#ifndef KWS_INFER_HOP_FRAMES
#define KWS_INFER_HOP_FRAMES   5      /* run the CNN every N audio frames (N*20 ms) */
#endif
#define KWS_DEADLINE_US        (KWS_INFER_HOP_FRAMES * 20000)

#endif /* PLACEMENT_H */
