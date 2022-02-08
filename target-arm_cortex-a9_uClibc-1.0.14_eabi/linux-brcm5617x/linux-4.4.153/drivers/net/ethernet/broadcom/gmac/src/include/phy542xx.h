/*
 * Copyright (C) 2013, Broadcom Corporation. All Rights Reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * These routines provide access to the external phy
 *
 */

#ifndef _PHY542XX_H_
#define _PHY542XX_H_

#include <typedefs.h>

/* Broadcom BCM542xx */
#define phy542xx_rd_reg					phy542xx_reg_read
#define phy542xx_wr_reg					phy542xx_reg_write

#define BCM542XX_REG_EXP_SEL						0x17
#define BCM542XX_REG_EXP_SELECT_7E					0x0F7E
#define BCM542XX_REG_EXP_DATA						0x15
#define BCM542XX_REG_EXP_RDB_EN						0x0000

#define BCM542XX_REG_RDB_ADDR						0x1e
#define BCM542XX_REG_RDB_DATA						0x1f

#define MIIM_BCM542xx_RDB_AUXSTATUS					0x09
#define MIIM_BCM542xx_AUXSTATUS_LINKMODE_MASK		0x0700
#define MIIM_BCM542xx_AUXSTATUS_LINKMODE_SHIFT		8

#define BCM542XX_REG_RDB_MII_MISC_CTRL				0x02f

#define BCM542XX_REG_RDB_EXT_SERDES_CTRL			0x234
#define BCM542XX_REG_EXT_SERDES_AUTO_FX				(1 << 6)
#define BCM542XX_REG_EXT_SERDES_FX_FD				(1 << 5)
#define BCM542XX_REG_EXT_SERDES_FX					(1 << 4)
#define BCM542XX_REG_EXT_SERDES_LED					(1 << 3)
#define BCM542XX_REG_EXT_SEL_SYNC_ST				(1 << 2)
#define BCM542XX_REG_EXT_SELECT_SD					(1 << 1)
#define BCM542XX_REG_EXT_SERDES_SEL					(1 << 0)
#define BCM542XX_REG_EXT_SERDES_FX_MASK				(BCM542XX_REG_EXT_SERDES_FX | \
													 BCM542XX_REG_EXT_SERDES_AUTO_FX)

#define BCM542XX_REG_RDB_SGMII_SLAVE				0x235
#define BCM542XX_REG_SGMII_SLAVE_AUTO				(1 << 0)

#define MIIM_BCM542xx_RDB_AUTO_DETECT_MEDIUM		0x23e
#define BCM542XX_REG_MII_AUTO_DET_MED_2ND_SERDES	(1 << 9)
#define BCM542XX_REG_MII_INV_FIBER_SD				(1 << 8)
#define BCM542XX_REG_MII_FIBER_IN_USE_LED			(1 << 7)
#define BCM542XX_REG_MII_FIBER_LED					(1 << 6)
#define BCM542XX_REG_MII_FIBER_SD_SYNC				(1 << 5)
#define BCM542XX_REG_MII_FIBER_AUTO_PWRDN			(1 << 4)
#define BCM542XX_REG_MII_SD_en_ov					(1 << 3)
#define BCM542XX_REG_MII_AUTO_DET_MED_DEFAULT		(1 << 2)
#define BCM542XX_REG_MII_AUTO_DET_MED_PRI			(1 << 1)
#define BCM542XX_REG_MII_AUTO_DET_MED_EN			(1 << 0)
#define BCM542XX_REG_MII_AUTO_DET_MASK				0x033f

#define BCM542XX_REG_RDB_MODE_CTRL					0x021
#define BCM542XX_REG_MODE_CTRL_COPPER_LINK			(1 << 7)
#define BCM542XX_REG_MODE_CTRL_SERDES_LINK			(1 << 6)
#define BCM542XX_REG_MODE_CTRL_COPPER_ENERGY_DET	(1 << 5)
#define BCM542XX_REG_MODE_CNTL_MODE_SEL_2			(1 << 2)
#define BCM542XX_REG_MODE_CNTL_MODE_SEL_1			(1 << 1)
#define BCM542XX_REG_MODE_CTRL_1000X_EN				(1 << 0)

#define BCM542XX_REG_RDB_COPPER_MISC_CTRL			0x02f
#define BCM542XX_REG_MISC_CTRL_FORCE_AUTO_MDIX		(1 << 9)

#define BCM542XX_REG_MODE_SEL_COPPER_2_SGMII		(0x0)
#define BCM542XX_REG_MODE_SEL_FIBER_2_SGMII			(BCM542XX_REG_MODE_CNTL_MODE_SEL_1)
#define BCM542XX_REG_MODE_SEL_SGMII_2_COPPER		(BCM542XX_REG_MODE_CNTL_MODE_SEL_2)
#define BCM542XX_REG_MODE_SEL_GBIC					(BCM542XX_REG_MODE_CNTL_MODE_SEL_1 | \
													BCM542XX_REG_MODE_CNTL_MODE_SEL_2)

#define BCM542XX_REG_RDB_2ND_SERDES_BASE			0xb00
#define BCM542XX_REG_RDB_2ND_SERDES_MISC_1000X		0xb17


extern int phy542xx_reg_read(u32 phy_addr, u32 flags, int reg_addr, u16 *data);
extern int phy542xx_reg_write(u32 phy_addr, u32 flags, int reg_addr, u16 data);
extern int phy542xx_reset_setup(u32 phy_addr);
extern int phy542xx_init(u32 phy_addr);
extern int phy542xx_enable_set(u32 phy_addr, int enable);
extern int phy542xx_force_auto_mdix(u32 phy_addr, int enable);

#endif	/* _PHY542XX_H_ */
