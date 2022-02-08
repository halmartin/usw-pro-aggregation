/*
 * $Copyright Open Broadcom Corporation$
 */
 
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>
#include <linux/delay.h>

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>

#define IPROC_NOR_COMPATIBLE "brcm,iproc-nor"
extern void __iomem *get_iproc_dmu_pcu_base(void);

/* HR2 */
#define PNOR_NAND_SEL_REG_OVERRIDE_HR2	2
#define PNOR_NAND_SEL_REG_PNOR_SEL_HR2	3

/* GH/HR3 */
#define PNOR_NAND_SEL_REG_OVERRIDE_GH	0
#define PNOR_NAND_SEL_REG_PNOR_SEL_GH	1
#define PNOR_DIRECT_CMD_OFFSET		0x10
#define PNOR_SET_OPMODE_OFFSET		0X18


#define IPROC_STRAP_BOOT_DEV_NAND		1
#define IPROC_STRAP_BOOT_DEV_PNOR		4
#define ICFG_PNOR_STRAPS__PNOR_SRAM_MW_R	0
#define PNOR_set_opmode__set_mw_R		0
#define PNOR_direct_cmd__cmd_type_R		21
#define IPROC_DMU_STRAPS_OFFSET			0x28
#define IPROC_BOOT_STRAP_MASK			0x7
#if defined(CONFIG_MACH_HR2) || defined(CONFIG_MACH_GH)
#define IPROC_STRAP_BOOT_DEV_SHIFT		9
#elif defined(CONFIG_MACH_GH2) || defined(CONFIG_MACH_SB2) || \
	defined(CONFIG_MACH_HR3)
#define IPROC_STRAP_BOOT_DEV_SHIFT		10
#endif


static struct mtd_info *nor_mtd;

static struct map_info s29gl_map = {
	.name =		"S29GL",
	.bankwidth =	4,
};


/*
 * Initialize FLASH support
 */
static int __init s29gl_mtd_init(void)
{
	struct device_node *np;
	void __iomem *reg_addr;
#ifdef CONFIG_MACH_IPROC_P7
	void __iomem *reg_strap;
#endif
	void __iomem *nor_enable=NULL;
	struct platform_device *pdev;	
	struct resource *memres;
	struct mtd_part_parser_data ppdata;
	u32 straps, val;

	np = of_find_compatible_node(NULL, NULL, IPROC_NOR_COMPATIBLE);
	if (!np) {
		printk(KERN_INFO "No NOR flash controller enabled in DT\n");
		return -ENODEV;
	}
	
	if (!of_device_is_available(np)) {
		printk(KERN_INFO "NOR flash controller disabled in DT\n");
		return -ENODEV;
	}
		
	reg_addr = of_iomap(np, 0);
	if (!reg_addr) {
		printk(KERN_ERR "NOR base addr ioremap eror\n");
		return -EIO;	
	}		

	nor_enable = of_iomap(np, 2);
	if (!nor_enable) {
		printk(KERN_ERR "PNOR sel ioremap eror\n");
		return -EIO;
	}

	/* Check boot device */
	straps = readl(get_iproc_dmu_pcu_base() + IPROC_DMU_STRAPS_OFFSET);
	straps = (straps >> IPROC_STRAP_BOOT_DEV_SHIFT) & IPROC_BOOT_STRAP_MASK;
		
	if (straps == IPROC_STRAP_BOOT_DEV_NAND) {
        	/* If booting from NAND, PNOR cannot be used */
		return -ENODEV;
	} else if (straps != IPROC_STRAP_BOOT_DEV_PNOR) {
		/* Switching to PNOR only if not booting from PNOR */
		val = readl_relaxed(nor_enable);
		if (of_find_compatible_node(NULL, NULL, "brcm,hurricane2")) {
			val |= (1UL << PNOR_NAND_SEL_REG_OVERRIDE_HR2) |
				(1UL << PNOR_NAND_SEL_REG_PNOR_SEL_HR2);
		} else {
			val |= (1UL << PNOR_NAND_SEL_REG_OVERRIDE_GH) |
				(1UL << PNOR_NAND_SEL_REG_PNOR_SEL_GH);
		}
		writel_relaxed(val, nor_enable);
           
#ifdef CONFIG_MACH_IPROC_P7
		/* Configure controller memory width based on strap */
		reg_strap = of_iomap(np, 3);
		if (!reg_strap) {
			printk(KERN_ERR "NOR strap addr ioremap eror\n");
			return -EIO;	
		}	
		straps = readl_relaxed(reg_strap) & 
			(1 << ICFG_PNOR_STRAPS__PNOR_SRAM_MW_R);
		if (straps) {
			/* 16-bit */
			val = readl_relaxed ((void * __iomem)
				(reg_addr + PNOR_SET_OPMODE_OFFSET));
			val |= (1 << PNOR_set_opmode__set_mw_R);
			writel_relaxed(val, (void * __iomem)(
				reg_addr + PNOR_SET_OPMODE_OFFSET));
		} else {
			/* 8-bit */
			val = readl_relaxed((void * __iomem)
            			(reg_addr + PNOR_SET_OPMODE_OFFSET));
			val &= ~(1 << PNOR_set_opmode__set_mw_R);
			writel_relaxed(val, (void * __iomem)
            			(reg_addr + PNOR_SET_OPMODE_OFFSET));
		}
		val = readl_relaxed((void * __iomem)(reg_addr + 
        			PNOR_DIRECT_CMD_OFFSET));
        	val |= (2 << PNOR_direct_cmd__cmd_type_R);
		writel_relaxed(val, (void * __iomem)(reg_addr + 
        			PNOR_DIRECT_CMD_OFFSET));
#endif 
	}

	printk(KERN_INFO "S29GL-MTD: NOR_INTERFACE Enabled\n");

	udelay(1000);
	
	pdev = of_find_device_by_node(np);
	memres = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	s29gl_map.phys = memres->start;
	s29gl_map.size = resource_size(memres);
	s29gl_map.virt = ioremap(s29gl_map.phys, s29gl_map.size);
	
	if (!s29gl_map.virt) {
		printk(KERN_ERR "S29GL-MTD: ioremap failed\n");
		if (nor_enable) {
			/* revert to NAND mode */
			val = readl_relaxed(nor_enable);
			if (of_find_compatible_node(NULL, NULL, 
				"brcm,hurricane2"))
				val &= ~(1UL << PNOR_NAND_SEL_REG_PNOR_SEL_HR2);
			else
				val &= ~(1UL << PNOR_NAND_SEL_REG_PNOR_SEL_GH);
			writel_relaxed(val, nor_enable);
		}
		return -EIO;
	}

	simple_map_init(&s29gl_map);

	// Probe for flash bankwidth 4
	printk (KERN_INFO "S29GL-MTD probing 32bit FLASH\n");
	nor_mtd = do_map_probe("cfi_probe", &s29gl_map);
	if (!nor_mtd) {
		printk (KERN_INFO "S29GL-MTD probing 16bit FLASH\n");
		// Probe for bankwidth 2
		s29gl_map.bankwidth = 2;
		nor_mtd = do_map_probe("cfi_probe", &s29gl_map);
	}

	if (nor_mtd) {
		nor_mtd->owner = THIS_MODULE;
		ppdata.of_node = np;
		mtd_device_parse_register(nor_mtd, NULL, &ppdata, NULL, 0);
		printk (KERN_INFO "S29GL-MTD MTD partitions parsed!\n");
		return 0;
	}

	printk (KERN_INFO "S29GL-MTD NO FLASH found!\n");
	if (nor_enable) {
		/* revert to NAND mode */
		val = readl_relaxed(nor_enable);
		if (of_find_compatible_node(NULL, NULL, "brcm,hurricane2"))
			val &= ~(1UL << PNOR_NAND_SEL_REG_PNOR_SEL_HR2);
		else
			val &= ~(1UL << PNOR_NAND_SEL_REG_PNOR_SEL_GH);
		writel_relaxed(val, nor_enable);
	}
	iounmap((void *)s29gl_map.virt);
	return -ENXIO;
}

/*
 * Cleanup
 */
static void __exit s29gl_mtd_cleanup(void)
{
	if (nor_mtd) {
		mtd_device_unregister(nor_mtd);
		map_destroy(nor_mtd);
	}

	if (s29gl_map.virt) {
		iounmap((void *)s29gl_map.virt);
		s29gl_map.virt = 0;
	}
}


module_init(s29gl_mtd_init);
module_exit(s29gl_mtd_cleanup);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MTD map driver for Hurricane2 Deerhound evaluation boards");
