/*
 * $Copyright Open Broadcom Corporation$
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#define IPROC_DMU_PCU_COMPATIBLE "brcm,iproc-dmu-pcu"
#define IPROC_WRAP_CTRL_COMPATIBLE "brcm,iproc-wrap-ctrl"
#define IPROC_IDM_COMPATIBLE "brcm,iproc-idm"
#define MAX_IDM_NUM	2

static void __iomem *iproc_dmu_pcu_base=NULL;
static void __iomem *iproc_wrap_ctrl_base=NULL;
static void __iomem *iproc_idm_base[MAX_IDM_NUM]={NULL};
static void __iomem *iproc_idm_base_phys[MAX_IDM_NUM]={NULL};


extern void request_idm_timeout_interrupts(struct device_node *);

void inline  __iomem *get_iproc_dmu_pcu_base(void) {
	return iproc_dmu_pcu_base;
}

void inline __iomem *get_iproc_wrap_ctrl_base(void) {
	return iproc_wrap_ctrl_base;
}

void inline __iomem *get_iproc_idm_base(int index) {
	return iproc_idm_base[index];
}

void inline __iomem *get_iproc_idm_base_phys(int index) {
	return iproc_idm_base_phys[index];
}

int xgs_iproc_wrap_idm_dmu_base_reg_setup(void)
{
	struct device_node *np;
	
	/* Get DMU/PCU base addr */
	np = of_find_compatible_node(NULL, NULL, IPROC_DMU_PCU_COMPATIBLE);
	if (!np) {
		pr_err("%s: No dmu/pcu node found\n", __func__);
		return -ENODEV ;
	}	
	iproc_dmu_pcu_base = of_iomap(np, 0);
	if (!iproc_dmu_pcu_base)
		return -ENOMEM;
	
	/* Get WRAP CTRL base addr */
	np = of_find_compatible_node(NULL, NULL, IPROC_WRAP_CTRL_COMPATIBLE);
	if (!np) {
		pr_err("%s: No wrap ctrl node found\n", __func__);
		return -ENODEV;
	}	
	iproc_wrap_ctrl_base = of_iomap(np, 0);
	if (!iproc_wrap_ctrl_base)
		return -ENOMEM;
	
	/* Get IDM base addr */
	np = of_find_compatible_node(NULL, NULL, IPROC_IDM_COMPATIBLE);
	if (!np) {
		pr_err("%s: No IDM node found\n", __func__);
		return -ENODEV;
	}	
	iproc_idm_base[0] = of_iomap(np, 0);
	if (!iproc_idm_base[0])
		return -ENOMEM;
	
	/* Second IDM base addr required for GH/SB2/GH2 IDM timeout handling.
	 * For other devices, the second IDM base addr is not used. So, it is 
	 * fine even the addr is NULL.
	 */		
	iproc_idm_base[1] = of_iomap(np, 1);

	return 1;
}    
    
void xgs_iproc_idm_timeout_handler_setup(void)
{
	struct device_node *np;
	struct platform_device *pdev=NULL;	
	struct resource *res_mem;    
	
	/* To get IDM phys addr */
	np = of_find_compatible_node(NULL, NULL, IPROC_IDM_COMPATIBLE);
	if (!np) {
		pr_warn("%s: No IDM node found\n", __func__);
		return;
	}	
	pdev = of_find_device_by_node(np);	
	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mem) {
		pr_warn("%s: No resource found\n", __func__);
		return;
	}		
	iproc_idm_base_phys[0] = (void __iomem *)res_mem->start;
	
	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	/* Only GH/SB2/GH2 has second IDM base addr */
	if (res_mem)
		iproc_idm_base_phys[1] = (void __iomem *)res_mem->start;

	/* register IDM timeout interrupt handler */
	request_idm_timeout_interrupts(np); 
}  