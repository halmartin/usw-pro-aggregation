/*
 * drivers/mmc/host/sdhci-bcm-hr3 - Broadcom HR3 SDHCI Platform driver
 *
 * Copyright (C) 2014-2016, Broadcom Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include "sdhci.h"

struct sdhci_xgs_iproc_data {
    struct sdhci_host *host;
    struct clk *clk;
    unsigned host_num;
};

struct xgs_iproc_sdhci_host {
    struct sdhci_host host;
    void __iomem *wrap_base;
    void __iomem *idm_base;
    void __iomem *cmicd_base;
    u32 shadow_cmd;
    u32 shadow_blk;
};

extern void __iomem *get_iproc_wrap_ctrl_base(void);
static int iproc_top_sdio_config(void __iomem *cmicd_base);

static inline void
iproc_sdhci_writel(struct sdhci_host *host, u32 val, int reg)
{
	/* WAR for SDIO/GPIO setting might be reset by SDK for HR3. */
	if (reg == SDHCI_INT_STATUS) {
    	struct xgs_iproc_sdhci_host *iproc_host = (struct xgs_iproc_sdhci_host *)host;
		iproc_top_sdio_config(iproc_host->cmicd_base);
	}

    writel(val, host->ioaddr + reg);
}

static inline u32
iproc_sdhci_readl(struct sdhci_host *host, int reg)
{
    return readl(host->ioaddr + reg);
}

static void
iproc_sdhci_writew(struct sdhci_host *host, u16 val, int reg)
{
    struct xgs_iproc_sdhci_host *iproc_host = (struct xgs_iproc_sdhci_host *)host;
    u32 oldval, newval;
    u32 word_num = (reg >> 1) & 1;
    u32 word_shift = word_num * 16;
    u32 mask = 0xffff << word_shift;

    if (reg == SDHCI_COMMAND) {
        if (iproc_host->shadow_blk != 0) {
            iproc_sdhci_writel(host, iproc_host->shadow_blk, SDHCI_BLOCK_SIZE);
            iproc_host->shadow_blk = 0;
        }
        oldval = iproc_host->shadow_cmd;
    } else if (reg == SDHCI_BLOCK_SIZE || reg == SDHCI_BLOCK_COUNT) {
        oldval = iproc_host->shadow_blk;
    } else {
        oldval = iproc_sdhci_readl(host, reg & ~3);
    }
    newval = (oldval & ~mask) | (val << word_shift);

    if (reg == SDHCI_TRANSFER_MODE) {
        iproc_host->shadow_cmd = newval;
    } else if (reg == SDHCI_BLOCK_SIZE || reg == SDHCI_BLOCK_COUNT) {
        iproc_host->shadow_blk = newval;
    } else {
        iproc_sdhci_writel(host, newval, reg & ~3);
    }
}

static u16
iproc_sdhci_readw(struct sdhci_host *host, int reg)
{
    u32 val, word;
    u32 word_num = (reg >> 1) & 1;
    u32 word_shift = word_num * 16;

    val = iproc_sdhci_readl(host, (reg & ~3));
	word = (val >> word_shift) & 0xffff;
    return word;
}

static void
iproc_sdhci_writeb(struct sdhci_host *host, u8 val, int reg)
{
    u32 oldval, newval;
    u32 byte_num = reg & 3;
    u32 byte_shift = byte_num * 8;
    u32 mask = 0xff << byte_shift;

    oldval = iproc_sdhci_readl(host, reg & ~3);
    newval = (oldval & ~mask) | (val << byte_shift);

    iproc_sdhci_writel(host, newval, reg & ~3);
}

static u8
iproc_sdhci_readb(struct sdhci_host *host, int reg)
{
    u32 val, byte;
    u32 byte_num = reg & 3;
    u32 byte_shift = byte_num * 8;

    val = iproc_sdhci_readl(host, (reg & ~3));
    byte = (val >> byte_shift) & 0xff;
    return byte;
}

static u32
iproc_sdhci_get_max_clock(struct sdhci_host *host)
{
    unsigned long max_clock;

    max_clock = (host->caps & SDHCI_CLOCK_V3_BASE_MASK)
                >> SDHCI_CLOCK_BASE_SHIFT;
    max_clock *= 1000000;

    return max_clock;
}

static u32
iproc_sdhci_get_min_clock(struct sdhci_host *host)
{
    return (host->max_clk / SDHCI_MAX_DIV_SPEC_300);
}

static int
iproc_sdhci_execute_tuning(struct sdhci_host *host, u32 opcode)
{
    /*
     * Tuning is unnecessary for SDR50 and DDR50; moreover, the IPROC platform
     * doesn't support SDR104, HS200 and Hs400 cards. So, we needn't do anything
     * for tuning.
     */
    return 0;
}

static void
iproc_sdhci_set_clock(struct sdhci_host *host, unsigned int clock)
{
    /*
     * WAR that IPROC SD/MMC host need to set the driver strength
     * to TYPE_A in 3.3v DS/HS mode even if the driver strength is
     * meaningless for 3.3V signaling.
     */
    if ((host->timing == MMC_TIMING_LEGACY) ||
        (host->timing == MMC_TIMING_MMC_HS) ||
        (host->timing == MMC_TIMING_SD_HS)) {
        host->mmc->ios.drv_type = MMC_SET_DRIVER_TYPE_A;
    }

    sdhci_set_clock(host, clock);
}

static struct sdhci_ops sdhci_xgs_iproc_ops = {
#ifdef CONFIG_MMC_SDHCI_IO_ACCESSORS
    .write_l = iproc_sdhci_writel,
    .write_w = iproc_sdhci_writew,
    .write_b = iproc_sdhci_writeb,
    .read_l = iproc_sdhci_readl,
    .read_w = iproc_sdhci_readw,
    .read_b = iproc_sdhci_readb,
#else
#error The iproc SDHCI driver needs CONFIG_MMC_SDHCI_IO_ACCESSORS to be set
#endif
    .reset = sdhci_reset,
	.set_bus_width = sdhci_set_bus_width,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
    .set_clock = iproc_sdhci_set_clock,
    .get_max_clock = iproc_sdhci_get_max_clock,
    .get_min_clock = iproc_sdhci_get_min_clock,
	.platform_execute_tuning = iproc_sdhci_execute_tuning,
};

#define IPROC_CMICD_COMPATIBLE "brcm,iproc-cmicd"

#define CMIC_SBUS_RING_MAP_0_7(base)		(base + 0x10098)
#define CMIC_SBUS_RING_MAP_8_15(base)		(base + 0x1009C)
#define CMIC_SBUS_RING_MAP_16_23(base)		(base + 0x100A0)
#define CMIC_SBUS_RING_MAP_24_31(base)		(base + 0x100A4)
#define CMIC_COMMON_SCHAN_CTRL(base)		(base + 0x10000)
#define CMIC_COMMON_SCHAN_MESSAGE0(base)	(base + 0x1000C)
#define CMIC_COMMON_SCHAN_MESSAGE1(base)	(base + 0x10010)
#define CMIC_COMMON_SCHAN_MESSAGE2(base)	(base + 0x10014)

/* TOP registers */
#define TOP_SDIO_MISC_CONTROL 							0x0207e500
#define TOP_SDIO_MISC_CONTROL__TOP_SDIO_8B_INF			4
#define TOP_SDIO_MISC_CONTROL__TOP_SDIO_GPIO_INF_SEL_R	0

/* SDIO IDM registers */
#define SDIO_IDM0_IO_CONTROL_DIRECT(base)				(base + 0x0)
#define SDIO_IDM0_IO_CONTROL_DIRECT__CMD_COMFLICT_DISABLE	22
#define SDIO_IDM0_IO_CONTROL_DIRECT__FEEDBACK_CLK_EN	21
#define SDIO_IDM0_IO_CONTROL_DIRECT__clk_enable			0
#define SDIO_IDM0_IDM_RESET_CONTROL(base)				(base + 0x3F8)

/* IPROC WRAP registers */
#define IPROC_WRAP_SDIO_CONTROL(base)			(base + 0xb0)
#define IPROC_WRAP_SDIO_CONTROL1(base)			(base + 0xb4)
#define IPROC_WRAP_SDIO_CONTROL2(base)			(base + 0xb8)
#define IPROC_WRAP_SDIO_CONTROL3(base)			(base + 0xbc)
#define IPROC_WRAP_SDIO_CONTROL4(base)			(base + 0xc0)
#define IPROC_WRAP_SDIO_CONTROL5(base)			(base + 0xc4)
#define IPROC_WRAP_SDIO_1P8_FAIL_CONTROL(base)	(base + 0xc8)
#define IPROC_WRAP_SDIO_1P8_FAIL_CONTROL__SDIO_VDDO_18V_FAIL_SOVW 1
#define IPROC_WRAP_SDIO_1P8_FAIL_CONTROL__SDIO_UHS1_18V_VREG_FAIL 0

/*
 * SDIO_CAPS_L
 *
 * Field                 Bit(s)
 * ===========================
 * DDR50                     31
 * SDR104                    30
 * SDR50                     29
 * SLOTTYPE                  28:27
 * ASYNCHIRQ                 26
 * SYSBUS64                  25
 * V18                       24
 * V3                        23
 * V33                       22
 * SUPRSM                    21
 * SDMA                      20
 * HSPEED                    19
 * ADMA2                     18
 * EXTBUSMED                 17
 * MAXBLK                    16:15
 * BCLK                      14:7
 * TOUT                      6
 * TOUTFREQ                  5:0
 */
#define SDIO_CAPS_L                 0xA17f6470

/*
 * SDIO_CAPS_H
 *
 * Field                 Bit(s)
 * ===========================
 * reserved                  31:20
 * SPIBLOCKMODE              19
 * SPIMODE_CAP               18
 * CLOCKMULT                 17:10
 * RETUNE_MODE               9:8
 * USETUNE_SDR50             7
 * TMRCNT_RETUNE             6:3
 * DRVR_TYPED                2
 * DRVR_TYPEC                1
 * DRVR_TYPEA                0
 */
#define SDIO_CAPS_H                 0x000C000f

/*
 * Preset value
 *
 * Field                 Bit(s)
 * ===========================
 * Driver Strength           12:11
 * Clock Generator           10
 * SDCLK Frequeency          9:0
 */

/*
 * SDIO_PRESETVAL1
 *
 * Field                 Bit(s)      Description
 * ============================================================
 * DDR50_PRESET              25:13   Preset Value for DDR50
 * DEFAULT_PRESET            12:0    Preset Value for Default Speed
 */
#define SDIO_PRESETVAL1             0x01004004

/*
 * SDIO_PRESETVAL2
 *
 * Field                 Bit(s)      Description
 * ============================================================
 * HIGH_SPEED_PRESET         25:13   Preset Value for High Speed
 * INIT_PRESET               12:0    Preset Value for Initialization
 */
#define SDIO_PRESETVAL2             0x01004100

/*
 * SDIO_PRESETVAL3
 *
 * Field                 Bit(s)      Description
 * ============================================================
 * SDR104_PRESET             25:13   Preset Value for SDR104
 * SDR12_PRESET              12:0    Preset Value for SDR12
 */
#define SDIO_PRESETVAL3             0x00000004

/*
 * SDIO_PRESETVAL4
 *
 * Field                 Bit(s)      Description
 * ============================================================
 * SDR25_PRESET              25:13   Preset Value for SDR25
 * SDR50_PRESET              12:0    Preset Value for SDR50
 */
#define SDIO_PRESETVAL4             0x01005001

u32 
cmicd_schan_read(void __iomem *base, u32 ctrl, u32 addr) {
    u32 read = 0x0;

    writel(ctrl, CMIC_COMMON_SCHAN_MESSAGE0(base));
    writel(addr, CMIC_COMMON_SCHAN_MESSAGE1(base));

    writel(0x1, CMIC_COMMON_SCHAN_CTRL(base));

    while (read != 0x2) {
       read = readl(CMIC_COMMON_SCHAN_CTRL(base));
    }
    read = readl(CMIC_COMMON_SCHAN_MESSAGE1(base));
    return read;
}

u32 
cmicd_schan_write(void __iomem *base, u32 ctrl, u32 addr, u32 val) {
    u32 read = 0x0;

    writel(ctrl, CMIC_COMMON_SCHAN_MESSAGE0(base));
    writel(addr, CMIC_COMMON_SCHAN_MESSAGE1(base));
    writel(val, CMIC_COMMON_SCHAN_MESSAGE2(base));

    writel(0x1, CMIC_COMMON_SCHAN_CTRL(base));

    while (read != 0x2) {
       read = readl(CMIC_COMMON_SCHAN_CTRL(base));
    }
    return read;
}

static void 
cmicd_init_soc(void __iomem *base) {
    /* Configure SBUS Ring Map for TOP, block id = 16, ring number = 4 */
    writel(0x11112200, CMIC_SBUS_RING_MAP_0_7(base));
    writel(0x00430001, CMIC_SBUS_RING_MAP_8_15(base));
    writel(0x00005064, CMIC_SBUS_RING_MAP_16_23(base));
    writel(0x00000000, CMIC_SBUS_RING_MAP_24_31(base));
}

static int
iproc_top_sdio_config(void __iomem *cmicd_base)
{
    u32 val;

    cmicd_init_soc(cmicd_base);

    /* Enable SDIO 8 bit mode */
    val = cmicd_schan_read(cmicd_base, 0x2c800200, TOP_SDIO_MISC_CONTROL);
	if ((val & 0x1f) != 0x1f) {
	    val |= (0x1 << TOP_SDIO_MISC_CONTROL__TOP_SDIO_8B_INF);
    	val |= (0xf << TOP_SDIO_MISC_CONTROL__TOP_SDIO_GPIO_INF_SEL_R);
    	cmicd_schan_write(cmicd_base, 0x34800200, TOP_SDIO_MISC_CONTROL, val);
	}
	
	return 0;
}

static int
iproc_sdio_init(struct xgs_iproc_sdhci_host *iproc_host)
{
	int ret = 0;
	u32 val;
	
	/* Enable SDIO for SDIO/GPIO selection */
	ret = iproc_top_sdio_config(iproc_host->cmicd_base);
	if (ret < 0) {
		return ret;
	}

	/* Release reset */
	writel(0x1, SDIO_IDM0_IDM_RESET_CONTROL(iproc_host->idm_base));
	udelay(1000);
	writel(0x0, SDIO_IDM0_IDM_RESET_CONTROL(iproc_host->idm_base));

	/* Enable the SDIO clock */
	val = readl(SDIO_IDM0_IO_CONTROL_DIRECT(iproc_host->idm_base));
	val |= (1 << SDIO_IDM0_IO_CONTROL_DIRECT__CMD_COMFLICT_DISABLE);
	val |= (1 << SDIO_IDM0_IO_CONTROL_DIRECT__FEEDBACK_CLK_EN);
	val |= (1 << SDIO_IDM0_IO_CONTROL_DIRECT__clk_enable);
	writel(val, SDIO_IDM0_IO_CONTROL_DIRECT(iproc_host->idm_base));

	/* Set the 1.8v fail control for HR3.
	 * This setting will not impact the uboot SD/MMC driver, since uboot doesn't 
	 * support 1.8v. The 1.8v SDIO will be supportted in Kernel. 
	 */
	val = readl(IPROC_WRAP_SDIO_1P8_FAIL_CONTROL(iproc_host->wrap_base));
	val |= (1 << IPROC_WRAP_SDIO_1P8_FAIL_CONTROL__SDIO_VDDO_18V_FAIL_SOVW);
	val &= ~(1 << IPROC_WRAP_SDIO_1P8_FAIL_CONTROL__SDIO_UHS1_18V_VREG_FAIL);
	writel(val, IPROC_WRAP_SDIO_1P8_FAIL_CONTROL(iproc_host->wrap_base));

	/*
	 * Configure SDIO host controller capabilities
	 * (common setting for all SDIO controllers)
	 */
	writel(SDIO_CAPS_H, IPROC_WRAP_SDIO_CONTROL(iproc_host->wrap_base));
	writel(SDIO_CAPS_L, IPROC_WRAP_SDIO_CONTROL1(iproc_host->wrap_base));

	/*
	 * Configure SDIO host controller preset values
	 * (common setting for all SDIO controllers)
	 */
	writel(SDIO_PRESETVAL1, IPROC_WRAP_SDIO_CONTROL2(iproc_host->wrap_base));
	writel(SDIO_PRESETVAL2, IPROC_WRAP_SDIO_CONTROL3(iproc_host->wrap_base));
	writel(SDIO_PRESETVAL3, IPROC_WRAP_SDIO_CONTROL4(iproc_host->wrap_base));
	writel(SDIO_PRESETVAL4, IPROC_WRAP_SDIO_CONTROL5(iproc_host->wrap_base));

	return 0;
}

static int
sdhci_xgs_iproc_probe(struct platform_device *pdev)
{
    struct xgs_iproc_sdhci_host *iproc_host;
    struct sdhci_host *host;
    struct sdhci_xgs_iproc_data *data;
    struct device_node *np = pdev->dev.of_node;
    int ret = 0;

    /* allocate SDHCI host + platform data memory */
    host = sdhci_alloc_host(&pdev->dev, sizeof(struct sdhci_xgs_iproc_data));
    if (IS_ERR(host)) {
        printk(KERN_ERR "SDIO%d: Unable to allocate SDHCI host\n", pdev->id);
        return PTR_ERR(host);
    }

	iproc_host = (struct xgs_iproc_sdhci_host *)host;
	
    /* set up data structure */
    data = sdhci_priv(host);
    data->host = host;
    data->host_num = pdev->id;
    host->hw_name = "IPROC-SDIO";
    host->ops = &sdhci_xgs_iproc_ops;
    host->mmc->caps = MMC_CAP_8_BIT_DATA;
    host->quirks = SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
                   SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12;
    host->irq = (unsigned int)irq_of_parse_and_map(np, 0);
    host->ioaddr = (void *)of_iomap(np, 0);
    if (!host->ioaddr) {
        printk(KERN_ERR "SDIO%d: Unable to iomap SDIO registers\n", pdev->id);
        ret = -ENXIO;
        goto err_free_host;
    }

	iproc_host->idm_base = of_iomap(np, 1);
	if (!iproc_host->idm_base) {
	    printk(KERN_ERR "Unable to iomap SDIO IDM base address\n");
	    ret = -ENXIO;
	    goto err_iounmap;
	}
	
	np = of_find_compatible_node(NULL, NULL, IPROC_CMICD_COMPATIBLE);
	if (!np) {
	    printk(KERN_ERR "Failed to find IPROC_CMICD defined in DT\n");
	    ret = -ENODEV;
	    goto err_iounmap;
	}
	
	iproc_host->cmicd_base = of_iomap(np, 0);
	if (!iproc_host->cmicd_base) {
	    printk(KERN_ERR "Unable to iomap IPROC CMICD base address\n");
	    ret = -ENXIO;
	    goto err_iounmap;
	}

    iproc_host->wrap_base = get_iproc_wrap_ctrl_base();
	if (!iproc_host->wrap_base) {
	    printk(KERN_ERR "Unable to get IPROC WRAP base address\n");
	    ret = -ENXIO;
	    goto err_iounmap;
	}

	ret = iproc_sdio_init(iproc_host);
	if (ret < 0) {
	    printk(KERN_ERR "SDIO%d: SDIO initial failed\n", pdev->id);
	    ret = -ENXIO;
	    goto err_iounmap;
	}

	platform_set_drvdata(pdev, data);
	
	ret = sdhci_add_host(host);
	if (ret) {
	    printk(KERN_ERR "SDIO%d: Failed to add SDHCI host\n", pdev->id);
	    goto err_iounmap;
	}
	
	return ret;

err_iounmap:
	if (iproc_host->idm_base) 
    	iounmap(iproc_host->idm_base);
	if (iproc_host->cmicd_base) 
    	iounmap(iproc_host->cmicd_base);
	if (host->ioaddr) 
    	iounmap(host->ioaddr);

err_free_host:
    sdhci_free_host(host);

    return ret;
}

static int __exit
sdhci_xgs_iproc_remove(struct platform_device *pdev)
{
    struct sdhci_xgs_iproc_data *data = platform_get_drvdata(pdev);
    struct sdhci_host *host = data->host;
    struct xgs_iproc_sdhci_host *iproc_host = (struct xgs_iproc_sdhci_host *)host;

    sdhci_remove_host(host, 0);
    platform_set_drvdata(pdev, NULL);
	
	if (iproc_host->idm_base) 
    	iounmap(iproc_host->idm_base);
	if (iproc_host->cmicd_base) 
    	iounmap(iproc_host->cmicd_base);
	if (host->ioaddr) 
    	iounmap(host->ioaddr);
    
    sdhci_free_host(host);
    release_mem_region(pdev->resource[0].start,
                       pdev->resource[0].end - pdev->resource[0].start + 1);
    return 0;
}

#ifdef CONFIG_PM
static int
sdhci_xgs_iproc_suspend(struct platform_device *pdev, pm_message_t state)
{
    int ret = 0;
    struct sdhci_xgs_iproc_data *data = platform_get_drvdata(pdev);

    ret = sdhci_suspend_host(data->host);
    if (ret < 0) {
        printk("%s: %d\n", __FILE__, __LINE__);
        return ret;
    }

    return 0;
}

static int
sdhci_xgs_iproc_resume(struct platform_device *pdev)
{
    int ret = 0;
    struct sdhci_xgs_iproc_data *data = platform_get_drvdata(pdev);

    ret = sdhci_resume_host(data->host);
    if (ret < 0) {
        printk("%s: %d\n", __FILE__, __LINE__);
        return ret;
    }
    return 0;
}
#else /* CONFIG_PM */

#define sdhci_xgs_iproc_suspend NULL
#define sdhci_xgs_iproc_resume NULL

#endif /* CONFIG_PM */


static const struct of_device_id brcm_iproc_hr3_dt_ids[] = {
	{ .compatible = "brcm,iproc-hr3-sdio"},
	{ }
};
MODULE_DEVICE_TABLE(of, brcm_iproc_dt_ids);

static struct platform_driver sdhci_xgs_iproc_driver = {
    .probe = sdhci_xgs_iproc_probe,
    .remove = __exit_p(sdhci_xgs_iproc_remove),
    .suspend = sdhci_xgs_iproc_suspend,
    .resume = sdhci_xgs_iproc_resume,
    .driver = {
        .name = "iproc-hr3-sdio",
        .owner = THIS_MODULE,
		.of_match_table = of_match_ptr(brcm_iproc_hr3_dt_ids),
    },
};

module_platform_driver(sdhci_xgs_iproc_driver);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("SDHCI XGS HR3 driver");
MODULE_LICENSE("GPL");
