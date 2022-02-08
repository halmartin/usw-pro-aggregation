/*
 * $Copyright Open Broadcom Corporation$
 */

#include <asm/mach/arch.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/dma-mapping.h>
#include <linux/soc/bcm/xgs-iproc-wrap-idm-dmu.h>

#define DMU_CRU_RESET_BASE 0x200

#if defined(CONFIG_PL330_DMA) || defined(CONFIG_XGS_IPROC_DMA330_DMA)
/* SB2/HR3 */
#define DMAC_IDM_RESET_OFFSET 0xf800
/* HX4/KT2/HR2/GH */
#define DMAC_IDM_RESET_OFFSET_1 0x14800
#endif /* CONFIG_PL330_DMA || CONFIG_XGS_IPROC_DMA330_DMA */

enum xgs_iproc_dev_id {
	XGS_IPROC_HX4=0,
	XGS_IPROC_KT2,
	XGS_IPROC_HR2,
	XGS_IPROC_GH,
	XGS_IPROC_SB2,
	XGS_IPROC_HR3,
	XGS_IPROC_GH2,
	XGS_IPROC_GENERIC,
};

const char *const xgs_iproc_dt_compat[] = {
	"brcm,helix4",
	"brcm,katana2",
	"brcm,hurricane2",
	"brcm,greyhound",
	"brcm,saber2",
	"brcm,hurricane3",
	"brcm,greyhound2",
	"brcm,xgs-iproc",
	NULL,
};

#if defined(CONFIG_PL330_DMA) || defined(CONFIG_XGS_IPROC_DMA330_DMA)
void xgs_iproc_dmac_idm_reset(void)
{
	void __iomem *reset_base = NULL;
	
	/* Need to de-assert reset of DMAC before of_platform_populate */
	if (of_machine_is_compatible(xgs_iproc_dt_compat[XGS_IPROC_SB2]) || 
	    of_machine_is_compatible(xgs_iproc_dt_compat[XGS_IPROC_HR3]) ||
	    of_machine_is_compatible(xgs_iproc_dt_compat[XGS_IPROC_GH2]))
		reset_base = get_iproc_idm_base(0) + DMAC_IDM_RESET_OFFSET;
	else
		reset_base = get_iproc_idm_base(0) + DMAC_IDM_RESET_OFFSET_1;
		
	if (reset_base != NULL)			
		writel(readl(reset_base) & 0xFFFFFFFE, reset_base);		
}
#endif /* CONFIG_PL330_DMA || CONFIG_XGS_IPROC_DMA330_DMA */

void __init xgs_iproc_init_early(void)
{   
	/*
	 * SDK allocates coherent buffers from atomic context.
	 * Increase size of atomic coherent pool to make sure such
	 * the allocations won't fail.
	 */
#ifdef CONFIG_DMA_CMA
	/*can be overrided by "coherent_pool" in bootargs */
	init_dma_coherent_pool_size(SZ_1M * 16);
#endif
}

static void __init xgs_iproc_init(void)
{
	int ret;
	
	ret = xgs_iproc_wrap_idm_dmu_base_reg_setup();
	if (ret < 0)
		return;

#if defined(CONFIG_PL330_DMA) || defined(CONFIG_XGS_IPROC_DMA330_DMA)
	xgs_iproc_dmac_idm_reset();
#endif	
	/* Populate platform devices */    
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);

	/* Setup IDM timeout handler */
	xgs_iproc_idm_timeout_handler_setup();	
}


static void xgs_iproc_restart(enum reboot_mode mode, const char *cmd)
{
	void * __iomem reg_addr;
	u32 reg;
	
	/* CRU_RESET register */	
	reg_addr = (void * __iomem)(get_iproc_dmu_pcu_base() + 
					DMU_CRU_RESET_BASE);
	/* set iproc_reset_n to 0 */
	reg = readl(reg_addr);
	reg &= ~((u32) 1 << 1);
			
	writel(reg, reg_addr);
	
	/* Wait for reset */
	while (1)
		cpu_do_idle();	
}

DT_MACHINE_START(XGS_iProc_DT, "BRCM XGS iProc")
	.init_early = xgs_iproc_init_early,
	.init_machine = xgs_iproc_init,
	.dt_compat = xgs_iproc_dt_compat,
	.restart = xgs_iproc_restart,
	.l2c_aux_val    = 0,
	.l2c_aux_mask	= ~0,
MACHINE_END
