/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019 FORTH-ICS/CARV
 *				Panagiotis Peristerakis <perister@ics.forth.gr>
 */

#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/riscv_io.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_const.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_platform.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/fdt/fdt_fixup.h>
#include <sbi_utils/ipi/aclint_mswi.h>
#include <sbi_utils/irqchip/plic.h>
#include <sbi_utils/serial/uart8250.h>
#include <sbi_utils/timer/aclint_mtimer.h>

#define CHESHIRE_UART_ADDR	      		0x03002000
#define CHESHIRE_UART_FREQ	      		50000000
#define CHESHIRE_UART_BAUDRATE	      	115200
#define CHESHIRE_UART_REG_SHIFT	      	2
#define CHESHIRE_UART_REG_WIDTH	      	4
#define CHESHIRE_PLIC_ADDR	      		0x04000000
#define CHESHIRE_PLIC_NUM_SOURCES     	58
#define CHESHIRE_HART_COUNT	      		2
#define CHESHIRE_CLINT_ADDR	      		0x02040000
#define CHESHIRE_ACLINT_MTIMER_FREQ		1000000
#define CHESHIRE_ACLINT_MSWI_ADDR     	(CHESHIRE_CLINT_ADDR + 0x0)
#define CHESHIRE_ACLINT_MTIMER_ADDR   	(CHESHIRE_CLINT_ADDR + 0xbff8)
#define CHESHIRE_ACLINT_MTIMECMP_ADDR 	(CHESHIRE_CLINT_ADDR + 0x4000)


static struct platform_uart_data uart = {
	CHESHIRE_UART_ADDR,
	CHESHIRE_UART_FREQ,
	CHESHIRE_UART_BAUDRATE,
};

static struct plic_data plic = {
	.addr = CHESHIRE_PLIC_ADDR,
	.num_src = CHESHIRE_PLIC_NUM_SOURCES,
};

static struct aclint_mswi_data mswi = {
	.addr = CHESHIRE_ACLINT_MSWI_ADDR,
	.size = ACLINT_MSWI_SIZE,
	.first_hartid = 0,
	.hart_count = CHESHIRE_HART_COUNT,
};

static struct aclint_mtimer_data mtimer = {
	.mtime_freq = CHESHIRE_ACLINT_MTIMER_FREQ,
	.mtime_addr = CHESHIRE_ACLINT_MTIMER_ADDR,
	.mtime_size = 8,
	.mtimecmp_addr = CHESHIRE_ACLINT_MTIMECMP_ADDR,
	.mtimecmp_size = 16,
	.first_hartid = 0,
	.hart_count = CHESHIRE_HART_COUNT,
	.has_64bit_mmio = FALSE,
};

/*
 * Cheshire platform early initialization.
 */
static int cheshire_early_init(bool cold_boot)
{
	void *fdt;
	struct platform_uart_data uart_data;
	int rc;

	if (!cold_boot)
		return 0;
	fdt = fdt_get_address();

	rc = fdt_parse_uart8250(fdt, &uart_data, "ns16550a");
	if (!rc)
		uart = uart_data;

	return 0;
}

/*
 * Cheshire platform final initialization.
 */
static int cheshire_final_init(bool cold_boot)
{
	void *fdt;

	if (!cold_boot)
		return 0;

	fdt = fdt_get_address();
	fdt_fixups(fdt);

	return 0;
}

/*
 * Initialize the cheshire console.
 */
static int cheshire_console_init(void)
{
	return uart8250_init(uart.addr,
			     uart.freq,
			     uart.baud,
			     CHESHIRE_UART_REG_SHIFT,
			     CHESHIRE_UART_REG_WIDTH);
}

static int plic_cheshire_warm_irqchip_init(int m_cntx_id, int s_cntx_id)
{
//  size_t i, ie_words = CHESHIRE_PLIC_NUM_SOURCES / 32 + 1;

	/* By default, enable all IRQs for M-mode of target HART */
//  if (m_cntx_id > -1) {
//  	for (i = 0; i < ie_words; i++)
//  		plic_set_ie(&plic, m_cntx_id, i, 1);
//  }
//  /* Enable all IRQs for S-mode of target HART */
//  if (s_cntx_id > -1) {
//  	for (i = 0; i < ie_words; i++)
//  		plic_set_ie(&plic, s_cntx_id, i, 1);
//  }
//  /* By default, enable M-mode threshold */
//  if (m_cntx_id > -1)
//  	plic_set_thresh(&plic, m_cntx_id, 1);
//  /* By default, disable S-mode threshold */
//  if (s_cntx_id > -1)
//  	plic_set_thresh(&plic, s_cntx_id, 0);

	return plic_warm_irqchip_init(&plic, m_cntx_id, s_cntx_id);
}

/*
 * Initialize the cheshire interrupt controller for current HART.
 */
static int cheshire_irqchip_init(bool cold_boot)
{
	u32 hartid = current_hartid();
	int ret;

	// sbi_printf("Cheshire: Initializing IRQ chip for HART %u (%s boot)\n", 
    //             hartid, cold_boot ? "cold" : "warm");

	if (cold_boot) {
		ret = plic_cold_irqchip_init(&plic);
		if (ret)
			return ret;
	}
	return plic_cheshire_warm_irqchip_init(2 * hartid, 2 * hartid + 1);
}

/*
 * Initialize IPI for current HART.
 */
static int cheshire_ipi_init(bool cold_boot)
{
	int ret;

	if (cold_boot) {
		ret = aclint_mswi_cold_init(&mswi);
		if (ret)
			return ret;
	}

	return aclint_mswi_warm_init();
}

/*
 * Initialize cheshire timer for current HART.
 */
static int cheshire_timer_init(bool cold_boot)
{
	int ret;

	if (cold_boot) {
		ret = aclint_mtimer_cold_init(&mtimer, NULL);
		if (ret)
			return ret;
	}

	return aclint_mtimer_warm_init();
}
/*
 * e-call to flush cache.
 */
// static int cheshire_vendor_ext_provider(long extid, long funcid,
//     const struct sbi_trap_regs *regs,
//     unsigned long *out_value,
//     struct sbi_trap_info *out_trap)
// {
//     if (extid == 0x09000001 && funcid == 0) {
// 		sbi_printf("Cache flush ecall received from HART %d\n", current_hartid());
//         // Flush CVA6 dcache by disabling then re-enabling.
//         // Must be done from M-mode since CSR 0x7C1 is M-mode only.
//         asm volatile("csrrwi x0, 0x7C1, 0x0 \n\t" : : : "memory");
//         asm volatile("fence rw, rw" : : : "memory");
//         asm volatile("csrrwi x0, 0x7C1, 0x1 \n\t" : : : "memory");
//         *out_value = 0;
//         return SBI_SUCCESS;
//     }
//     return SBI_ERR_NOT_SUPPORTED;
// }

// Use the code below in your s-mode space to trigger a cache flush ecall from s-mode:
// #define SBI_EXT_DCACHE_FLUSH_EID  0x09000001
// #define SBI_EXT_DCACHE_FLUSH_FID  0

// static inline void sbi_dcache_flush(void)
// {
//     register unsigned long a0 asm("a0") = 0;
//     register unsigned long a6 asm("a6") = SBI_EXT_DCACHE_FLUSH_FID;
//     register unsigned long a7 asm("a7") = SBI_EXT_DCACHE_FLUSH_EID;
//     asm volatile("ecall"
//         : "+r"(a0)
//         : "r"(a6), "r"(a7)
//         : "memory");
// }

/*
 * Platform descriptor.
 */
const struct sbi_platform_operations platform_ops = {
    .early_init          = cheshire_early_init,
    .final_init          = cheshire_final_init,
    .console_init        = cheshire_console_init,
    .irqchip_init        = cheshire_irqchip_init,
    .ipi_init            = cheshire_ipi_init,
    .timer_init          = cheshire_timer_init,
    // .vendor_ext_provider = cheshire_vendor_ext_provider,
};

const struct sbi_platform platform = {
	.opensbi_version = OPENSBI_VERSION,
	.platform_version = SBI_PLATFORM_VERSION(0x0, 0x01),
	.name = "CHESHIRE RISC-V",
	.features = SBI_PLATFORM_DEFAULT_FEATURES,
	.hart_count = CHESHIRE_HART_COUNT,
	.hart_stack_size = SBI_PLATFORM_DEFAULT_HART_STACK_SIZE,
	.platform_ops_addr = (unsigned long)&platform_ops
};