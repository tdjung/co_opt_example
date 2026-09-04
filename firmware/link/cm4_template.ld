/*
 * cm4_template.ld -- linker script for the CM4 virtual platform.
 *
 * REGION ORIGINS/LENGTHS ARE PLACEHOLDERS: tools/harness/gen_linker.py
 * rewrites the MEMORY block from the active HW tier config
 * (tools/tiers/*.json) so that the same script serves every tier.
 *
 * Placement model:
 *   - code/rodata default to FLASH, .data/.bss default to SRAM0
 *   - PLACE_DATA(region) puts an object in ".place.<region>";
 *     initialised objects are loaded from FLASH and copied by startup
 *   - PLACE_CODE(region) puts a function in ".place_code.<region>"
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 512K   /* @@FLASH@@ */
    ITCM  (rx)  : ORIGIN = 0x10000000, LENGTH = 16K    /* @@ITCM@@  */
    DTCM  (rw)  : ORIGIN = 0x20000000, LENGTH = 16K    /* @@DTCM@@  */
    SRAM0 (rw)  : ORIGIN = 0x24000000, LENGTH = 64K    /* @@SRAM0@@ */
    SRAM1 (rw)  : ORIGIN = 0x24010000, LENGTH = 64K    /* @@SRAM1@@ */
    SRAM2 (rw)  : ORIGIN = 0x24020000, LENGTH = 0K     /* @@SRAM2@@ */
    SRAM3 (rw)  : ORIGIN = 0x24030000, LENGTH = 0K     /* @@SRAM3@@ */
}

ENTRY(Reset_Handler)
_estack = ORIGIN(DTCM) + LENGTH(DTCM);   /* stack in DTCM */

/* helper: an initialised-data section for region R, loaded from FLASH */
SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH

    .text : {
        *(.text*)
        *(.rodata*)
        *(.place.flash)            /* explicitly Flash-resident data */
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx : { *(.ARM.exidx*) } > FLASH

    /* ---- code placed in ITCM ---- */
    .place_code.itcm : ALIGN(4) {
        __place_code_itcm_start = .;
        *(.place_code.itcm)
        . = ALIGN(4);
        __place_code_itcm_end = .;
    } > ITCM AT> FLASH
    __place_code_itcm_load = LOADADDR(.place_code.itcm);
    .place_code.flash : { *(.place_code.flash) } > FLASH

    /* ---- default .data / .bss in SRAM0 ---- */
    .data : ALIGN(4) {
        __data_start = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > SRAM0 AT> FLASH
    __data_load = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > SRAM0

    /* ---- placed data: one initialised + one NOLOAD section per region ---- */
    .place.dtcm  : ALIGN(4) { __pd_dtcm_start  = .; *(.place.dtcm)  . = ALIGN(4); __pd_dtcm_end  = .; } > DTCM  AT> FLASH
    .place.sram0 : ALIGN(4) { __pd_sram0_start = .; *(.place.sram0) . = ALIGN(4); __pd_sram0_end = .; } > SRAM0 AT> FLASH
    .place.sram1 : ALIGN(4) { __pd_sram1_start = .; *(.place.sram1) . = ALIGN(4); __pd_sram1_end = .; } > SRAM1 AT> FLASH
    .place.sram2 : ALIGN(4) { __pd_sram2_start = .; *(.place.sram2) . = ALIGN(4); __pd_sram2_end = .; } > SRAM2 AT> FLASH
    .place.sram3 : ALIGN(4) { __pd_sram3_start = .; *(.place.sram3) . = ALIGN(4); __pd_sram3_end = .; } > SRAM3 AT> FLASH
    __pd_dtcm_load  = LOADADDR(.place.dtcm);
    __pd_sram0_load = LOADADDR(.place.sram0);
    __pd_sram1_load = LOADADDR(.place.sram1);
    __pd_sram2_load = LOADADDR(.place.sram2);
    __pd_sram3_load = LOADADDR(.place.sram3);

    /* copy table consumed by startup_cm4.c: {src, dst, size}... , {0,0,0} */
    .copy_table : ALIGN(4) {
        __copy_table = .;
        LONG(__place_code_itcm_load) LONG(__place_code_itcm_start) LONG(__place_code_itcm_end - __place_code_itcm_start)
        LONG(__data_load)     LONG(__data_start)     LONG(__data_end - __data_start)
        LONG(__pd_dtcm_load)  LONG(__pd_dtcm_start)  LONG(__pd_dtcm_end  - __pd_dtcm_start)
        LONG(__pd_sram0_load) LONG(__pd_sram0_start) LONG(__pd_sram0_end - __pd_sram0_start)
        LONG(__pd_sram1_load) LONG(__pd_sram1_start) LONG(__pd_sram1_end - __pd_sram1_start)
        LONG(__pd_sram2_load) LONG(__pd_sram2_start) LONG(__pd_sram2_end - __pd_sram2_start)
        LONG(__pd_sram3_load) LONG(__pd_sram3_start) LONG(__pd_sram3_end - __pd_sram3_start)
        LONG(0) LONG(0) LONG(0)
    } > FLASH
    .zero_table : ALIGN(4) {
        __zero_table = .;
        LONG(__bss_start) LONG(__bss_end - __bss_start)
        LONG(0) LONG(0)
    } > FLASH

    PROVIDE(end = __bss_end);   /* newlib sbrk heap start (unused: no malloc in app) */

    /DISCARD/ : { *(.note*) *(.comment) }
}

/* NOTE: uninitialised objects marked PLACE_DATA() land in .place.<region> and
 * are copied from Flash like initialised data (the copy is small and keeps the
 * script simple). If Flash space matters, add NOLOAD twins. */
