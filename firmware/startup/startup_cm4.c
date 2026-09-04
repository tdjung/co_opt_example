/*
 * startup_cm4.c -- minimal Cortex-M4 startup.
 *
 * - vector table with weak default handlers
 * - copies .data and every ".place.<region>" data section from its Flash
 *   load address to its runtime region (symbols come from the linker script)
 * - zeroes .bss and ".place.<region>" bss sections
 * - enables the FPU, calls main()
 *
 * Interrupt numbers for platform peripherals live in hal/target/platform_regs.h.
 */
#include <stdint.h>
#include <string.h>

extern uint32_t _estack;
extern int main(void);

/* Linker-provided copy/zero table: {src, dst, size} triplets, end marked by size 0 */
typedef struct { uint32_t *src; uint32_t *dst; uint32_t size; } copy_entry_t;
typedef struct { uint32_t *dst; uint32_t size; } zero_entry_t;
extern const copy_entry_t __copy_table[];
extern const zero_entry_t __zero_table[];

void Reset_Handler(void);
void Default_Handler(void);

#define WEAK_ALIAS __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void)        WEAK_ALIAS;
void HardFault_Handler(void)  WEAK_ALIAS;
void MemManage_Handler(void)  WEAK_ALIAS;
void BusFault_Handler(void)   WEAK_ALIAS;
void UsageFault_Handler(void) WEAK_ALIAS;
void SVC_Handler(void)        WEAK_ALIAS;
void DebugMon_Handler(void)   WEAK_ALIAS;
void PendSV_Handler(void)     WEAK_ALIAS;
void SysTick_Handler(void)    WEAK_ALIAS;
/* platform IRQs 0..15 -- hal/target maps the ones it uses */
void IRQ0_Handler(void)  WEAK_ALIAS;  void IRQ1_Handler(void)  WEAK_ALIAS;
void IRQ2_Handler(void)  WEAK_ALIAS;  void IRQ3_Handler(void)  WEAK_ALIAS;
void IRQ4_Handler(void)  WEAK_ALIAS;  void IRQ5_Handler(void)  WEAK_ALIAS;
void IRQ6_Handler(void)  WEAK_ALIAS;  void IRQ7_Handler(void)  WEAK_ALIAS;
void IRQ8_Handler(void)  WEAK_ALIAS;  void IRQ9_Handler(void)  WEAK_ALIAS;
void IRQ10_Handler(void) WEAK_ALIAS;  void IRQ11_Handler(void) WEAK_ALIAS;
void IRQ12_Handler(void) WEAK_ALIAS;  void IRQ13_Handler(void) WEAK_ALIAS;
void IRQ14_Handler(void) WEAK_ALIAS;  void IRQ15_Handler(void) WEAK_ALIAS;

__attribute__((section(".isr_vector"), used))
void (*const g_vectors[])(void) = {
    (void (*)(void))&_estack,
    Reset_Handler, NMI_Handler, HardFault_Handler, MemManage_Handler, BusFault_Handler,
    UsageFault_Handler, 0, 0, 0, 0, SVC_Handler, DebugMon_Handler, 0, PendSV_Handler, SysTick_Handler,
    IRQ0_Handler, IRQ1_Handler, IRQ2_Handler, IRQ3_Handler, IRQ4_Handler, IRQ5_Handler,
    IRQ6_Handler, IRQ7_Handler, IRQ8_Handler, IRQ9_Handler, IRQ10_Handler, IRQ11_Handler,
    IRQ12_Handler, IRQ13_Handler, IRQ14_Handler, IRQ15_Handler,
};

void Default_Handler(void) { for (;;) { } }

void Reset_Handler(void)
{
    /* FPU: CPACR CP10/CP11 full access */
    volatile uint32_t *cpacr = (volatile uint32_t *)0xE000ED88u;
    *cpacr |= (0xFu << 20);
    __asm volatile("dsb\n isb");

    for (const copy_entry_t *e = __copy_table; e->size; e++)
        if (e->src != e->dst) memcpy(e->dst, e->src, e->size);
    for (const zero_entry_t *e = __zero_table; e->size; e++)
        memset(e->dst, 0, e->size);

    main();
    for (;;) { }
}
