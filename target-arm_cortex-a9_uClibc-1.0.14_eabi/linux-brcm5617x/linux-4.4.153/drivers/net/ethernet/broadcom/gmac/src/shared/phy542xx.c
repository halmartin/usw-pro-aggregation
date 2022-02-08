/*
 * $Copyright Open Broadcom Corporation$
 *
 * These routines provide access to the external phy
 *
 */
#include <bcmutils.h>
#include <bcmenetphy.h>
#include "../../../mdio/iproc_mdio.h"
#include "bcmiproc_phy.h"
#include "phy542xx.h"

/* debug/trace */
//#define BCMDBG
//#define BCMDBG_ERR
#ifdef BCMDBG
#define	NET_ERROR(args) printf args
#define	NET_TRACE(args) printf args
#elif defined(BCMDBG_ERR)
#define	NET_ERROR(args) printf args
#define NET_TRACE(args)
#else
#define	NET_ERROR(args)
#define	NET_TRACE(args)
#endif /* BCMDBG */
#define	NET_REG_TRACE(args)


#ifndef ASSERT
#define ASSERT(exp)
#endif

#define PHY542XX_RDB_REG_RD				phy542xx_rdb_reg_read
#define PHY542XX_RDB_REG_WR				phy542xx_rdb_reg_write
#define PHY542XX_REG_RD					phy542xx_reg_read
#define PHY542XX_REG_WR					phy542xx_reg_write

static int
phy542xx_rdb_reg_read(u32 phy_addr, u32 reg_addr, u16 *data)
{
	int rv = SOC_E_NONE;

    /* MDIO write the RDB reg. address to reg.0x1E = <reg_addr> */
    iproc_mii_write(MII_DEV_EXT, phy_addr, BCM542XX_REG_RDB_ADDR,
				(0xffff & reg_addr));

    /* MDIO read from reg.0x1F to get the RDB register's value as <data> */
	iproc_mii_read(MII_DEV_EXT, phy_addr, BCM542XX_REG_RDB_DATA, data);

    return rv;
}

static int
phy542xx_rdb_reg_write(u32 phy_addr, u32 reg_addr, u16 data)
{
	int rv = SOC_E_NONE;

    /* MDIO write the RDB reg. address to reg.0x1E = <reg_addr> */
    iproc_mii_write(MII_DEV_EXT, phy_addr, BCM542XX_REG_RDB_ADDR,
				(0xffff & reg_addr));

	/* MDIO write to reg.0x1F to set the RDB resister's value as <data> */
    iproc_mii_write(MII_DEV_EXT, phy_addr, BCM542XX_REG_RDB_DATA, data);

	return rv;
}

int
phy542xx_reg_read(u32 phy_addr, u32 flags, int reg_addr, u16 *data)
{
	int rv = SOC_E_NONE;
	u16 val;

	if (flags & PHY_REG_FLAGS_FIBER) { /* fiber registers */
		if (reg_addr <= 0x0f) {
			if (flags &  PHY_REG_FLAGS_1000X) {
				/* 54220 fiber is controlled by Secondary SerDes */
				PHY542XX_RDB_REG_RD(phy_addr,
						(reg_addr | BCM542XX_REG_RDB_2ND_SERDES_BASE), data);
			} else {
				/* Map 1000X page */
				PHY542XX_RDB_REG_RD(phy_addr, BCM542XX_REG_RDB_MODE_CTRL, &val);
				val |= BCM542XX_REG_MODE_CTRL_1000X_EN;
				PHY542XX_RDB_REG_WR(phy_addr, BCM542XX_REG_RDB_MODE_CTRL, val);

                /* Read 1000X IEEE register */
               iproc_mii_read(MII_DEV_EXT, phy_addr, reg_addr, data);

               /* Restore IEEE mapping */
				PHY542XX_RDB_REG_RD(phy_addr, BCM542XX_REG_RDB_MODE_CTRL, &val);
				val &= ~BCM542XX_REG_MODE_CTRL_1000X_EN;
				PHY542XX_RDB_REG_WR(phy_addr, BCM542XX_REG_RDB_MODE_CTRL, val);
            }
        }
	} else if (flags &  PHY_REG_FLAGS_RDB) {
		PHY542XX_RDB_REG_RD(phy_addr, reg_addr, data);
	} else {
		iproc_mii_read(MII_DEV_EXT, phy_addr, reg_addr, data);
	}

    return rv;
}


int
phy542xx_reg_write(u32 phy_addr, u32 flags, int reg_addr, u16 data)
{
	int rv = SOC_E_NONE;
	u16 val;

	if (flags & PHY_REG_FLAGS_FIBER) { /* fiber registers */
		if (reg_addr <= 0x0f) {
			if (flags &  PHY_REG_FLAGS_1000X) {
				/* 54220 fiber is controlled by Secondary SerDes */
				PHY542XX_RDB_REG_WR(phy_addr,
						(reg_addr | BCM542XX_REG_RDB_2ND_SERDES_BASE), data);
			} else {
				/* Map 1000X page */
				PHY542XX_RDB_REG_RD(phy_addr, BCM542XX_REG_RDB_MODE_CTRL, &val);
				val |= BCM542XX_REG_MODE_CTRL_1000X_EN;
				PHY542XX_RDB_REG_WR(phy_addr, BCM542XX_REG_RDB_MODE_CTRL, val);

                /* Write 1000X IEEE register */
                iproc_mii_write(MII_DEV_EXT, phy_addr, reg_addr, data);

               /* Restore IEEE mapping */
				PHY542XX_RDB_REG_RD(phy_addr, BCM542XX_REG_RDB_MODE_CTRL, &val);
				val &= ~BCM542XX_REG_MODE_CTRL_1000X_EN;
				PHY542XX_RDB_REG_WR(phy_addr, BCM542XX_REG_RDB_MODE_CTRL, val);
            }
        }
	} else if (flags &  PHY_REG_FLAGS_RDB) {
		PHY542XX_RDB_REG_WR(phy_addr, reg_addr, data);
	} else {
		iproc_mii_write(MII_DEV_EXT, phy_addr, reg_addr, data);
	}

    return rv;
}

static int
phy542xx_ge_reset(u32 phy_addr)
{
	int rv = SOC_E_NONE;
	u16 val;

	NET_TRACE(("%s: phy_addr %d\n", __FUNCTION__, phy_addr));

	/* Reset the PHY */
	PHY542XX_REG_RD(phy_addr, PHY_REG_FLAGS_NONE, PHY_MII_CTRLr_ADDR, &val);
	val |= MII_CTRL_RESET;
	PHY542XX_REG_WR(phy_addr, PHY_REG_FLAGS_NONE, PHY_MII_CTRLr_ADDR, val);

	SPINWAIT((!PHY542XX_REG_RD(phy_addr, PHY_REG_FLAGS_NONE, PHY_MII_CTRLr_ADDR, &val) &&
			  (val & MII_CTRL_RESET)), 100000);

	/* Check if out of reset */
	PHY542XX_REG_RD(phy_addr, PHY_REG_FLAGS_NONE, PHY_MII_CTRLr_ADDR, &val);
	if (val & MII_CTRL_RESET) {
		NET_ERROR(("%s reset not complete\n", __FUNCTION__));
		rv = SOC_E_TIMEOUT;
	} else {
		NET_TRACE(("%s reset complete\n", __FUNCTION__));
	}

	return rv;
}

static int
phy542xx_ge_init(u32 phy_addr)
{
	int rv = SOC_E_NONE;
	u16 val;

	/*
	 * Enable direct RDB addressing mode, write to Expansion register
	 * 0x7E = 0x0000
	 *  - MDIO write to reg 0x17 = 0x0F7E
	 *  - MDIO write to reg 0x15 = 0x0000
	 */
	PHY542XX_REG_WR(phy_addr, PHY_REG_FLAGS_NONE,
				BCM542XX_REG_EXP_SEL, BCM542XX_REG_EXP_SELECT_7E);
	PHY542XX_REG_WR(phy_addr, PHY_REG_FLAGS_NONE,
				BCM542XX_REG_EXP_DATA, BCM542XX_REG_EXP_RDB_EN);

	/* Configure auto-detect Medium */
	PHY542XX_RDB_REG_RD(phy_addr, MIIM_BCM542xx_RDB_AUTO_DETECT_MEDIUM, &val);
	val &= ~BCM542XX_REG_MII_AUTO_DET_MASK;
	/* Enable dual serdes auto-detect medium */
	val |= (BCM542XX_REG_MII_AUTO_DET_MED_2ND_SERDES |
			BCM542XX_REG_MII_FIBER_IN_USE_LED |
			BCM542XX_REG_MII_FIBER_LED |
			BCM542XX_REG_MII_FIBER_SD_SYNC);
	/* Enable auto-detect medium */
	val |= BCM542XX_REG_MII_AUTO_DET_MED_EN;
	/* Fiber selected when both media are active */
	val |= (BCM542XX_REG_MII_AUTO_DET_MED_PRI |
			BCM542XX_REG_MII_AUTO_DET_MED_DEFAULT);
	PHY542XX_RDB_REG_WR(phy_addr, MIIM_BCM542xx_RDB_AUTO_DETECT_MEDIUM, val);

	/* Power up primary SerDes, Fiber MII_CONTROL Reg. bit[11]*/
	PHY542XX_REG_RD(phy_addr, PHY_REG_FLAGS_PRI_SERDES, PHY_MII_CTRLr_ADDR, &val);
	val &= ~MII_CTRL_PD;
	PHY542XX_REG_WR(phy_addr, PHY_REG_FLAGS_PRI_SERDES, PHY_MII_CTRLr_ADDR, val);

	/* MODE_CONTROL register,  DIGX_SHD_1C_1F, RDB_0x21 */
	PHY542XX_RDB_REG_RD(phy_addr, BCM542XX_REG_RDB_MODE_CTRL, &val);
	val &= ~(BCM542XX_REG_MODE_CNTL_MODE_SEL_1 |
			 BCM542XX_REG_MODE_CNTL_MODE_SEL_2);
	val |= BCM542XX_REG_MODE_SEL_SGMII_2_COPPER;
	PHY542XX_RDB_REG_WR(phy_addr, BCM542XX_REG_RDB_MODE_CTRL, val);

	/* COPPER INTERFACE */
	PHY542XX_REG_RD(phy_addr, PHY_REG_FLAGS_NONE, PHY_MII_CTRLr_ADDR, &val);
	val &= ~MII_CTRL_PD;
	PHY542XX_REG_WR(phy_addr, PHY_REG_FLAGS_NONE, PHY_MII_CTRLr_ADDR, val);

	PHY542XX_REG_WR(phy_addr, PHY_REG_FLAGS_NONE, MII_CTRL1000, ADVERTISE_1000FULL);
	PHY542XX_REG_WR(phy_addr, PHY_REG_FLAGS_NONE, PHY_MII_CTRLr_ADDR,
				(BMCR_FULLDPLX | BMCR_SPEED100 | BMCR_ANENABLE | BMCR_ANRESTART));

	/* Enable/disable auto-detection between SGMII-slave and 1000BASE-X */
	/* External Serdes Control Reg., DIGX_SHD_1C_14, RDB_0x234 */
	PHY542XX_RDB_REG_RD(phy_addr, BCM542XX_REG_RDB_EXT_SERDES_CTRL, &val);
	val &= ~(BCM542XX_REG_EXT_SERDES_FX_MASK);
	PHY542XX_RDB_REG_WR(phy_addr, BCM542XX_REG_RDB_EXT_SERDES_CTRL, val);

	/* SGMII Slave Control Register, DIGX_SHD_1C_15, RDB_0x235 */
	PHY542XX_RDB_REG_RD(phy_addr, BCM542XX_REG_RDB_SGMII_SLAVE, &val);
	val &= ~(BCM542XX_REG_SGMII_SLAVE_AUTO);
	PHY542XX_RDB_REG_WR(phy_addr, BCM542XX_REG_RDB_SGMII_SLAVE, val);

	/* FIBER INTERFACE */
	/* Remove power down of SerDes */
	PHY542XX_REG_RD(phy_addr, PHY_REG_FLAGS_1000X, PHY_MII_CTRLr_ADDR, &val);
	val &= ~MII_CTRL_PD;
	PHY542XX_REG_WR(phy_addr, PHY_REG_FLAGS_1000X, PHY_MII_CTRLr_ADDR, val);

	/* Set the advertisement of serdes */
	PHY542XX_REG_RD(phy_addr, PHY_REG_FLAGS_1000X, PHY_MII_ANAr_ADDR, &val);
	val |= (MII_ANA_FD_1000X | MII_ANA_HD_1000X |
			MII_ANA_1000X_PAUSE | MII_ANA_1000X_ASYM_PAUSE);
	PHY542XX_REG_WR(phy_addr, PHY_REG_FLAGS_1000X, PHY_MII_ANAr_ADDR, val);

	/* Enable auto-detection between SGMII-slave and 1000BASE-X */
	/* External Serdes Control Reg., DIGX_SHD_1C_14, RDB_0x234 */
	val = (BCM542XX_REG_EXT_SERDES_LED | BCM542XX_REG_EXT_SEL_SYNC_ST |
		   BCM542XX_REG_EXT_SELECT_SD | BCM542XX_REG_EXT_SERDES_SEL);
	PHY542XX_RDB_REG_WR(phy_addr, BCM542XX_REG_RDB_EXT_SERDES_CTRL, val);

	/* SGMII Slave Control Register, DIGX_SHD_1C_15, RDB_0x235 */
	PHY542XX_RDB_REG_RD(phy_addr, BCM542XX_REG_RDB_SGMII_SLAVE, &val);
	val &= ~(BCM542XX_REG_SGMII_SLAVE_AUTO);
	PHY542XX_RDB_REG_WR(phy_addr, BCM542XX_REG_RDB_SGMII_SLAVE, val);

	/* Miscellanous Control Reg., CORE_SHD18_111, RDB_0x02f */
	PHY542XX_RDB_REG_WR(phy_addr, BCM542XX_REG_RDB_MII_MISC_CTRL, 0x2007);

	/* Second SERDES 100-FX CONTROL Register, RDB_0xb17 */
	PHY542XX_RDB_REG_WR(phy_addr, BCM542XX_REG_RDB_2ND_SERDES_MISC_1000X, 0x0);

	/* Default SerDes config & restart autonegotiation */
	PHY542XX_REG_WR(phy_addr, PHY_REG_FLAGS_1000X, PHY_MII_CTRLr_ADDR,
			(BMCR_FULLDPLX | BMCR_SPEED1000 | BMCR_ANENABLE | BMCR_ANRESTART));

	return rv;
}

int
phy542xx_reset_setup(u32 phy_addr)
{
	int rv = SOC_E_NONE;

	NET_TRACE(("%s enter\n", __FUNCTION__));

	rv = phy542xx_ge_reset(phy_addr);

	if (SOC_SUCCESS(rv)) {
	    rv = phy542xx_ge_init(phy_addr);
	}

    return rv;
}

int
phy542xx_init(u32 phy_addr)
{
	u16	phyid0, phyid1;

	NET_TRACE(("%s: phy_addr %d\n", __FUNCTION__, phy_addr));

	PHY542XX_REG_RD(phy_addr, PHY_REG_FLAGS_NONE, PHY_MII_PHY_ID0r_ADDR, &phyid0);
	PHY542XX_REG_RD(phy_addr, PHY_REG_FLAGS_NONE, PHY_MII_PHY_ID1r_ADDR, &phyid1);

	printf("%s Phy ChipID: 0x%04x:0x%04x\n", __FUNCTION__, phyid1, phyid0);

	phy542xx_reset_setup(phy_addr);

	return 0;
}

int
phy542xx_enable_set(u32 phy_addr, int enable)
{
	u16 val;

	NET_TRACE(("%s: phy_addr %d\n", __FUNCTION__, phy_addr));

	PHY542XX_REG_RD(phy_addr, PHY_REG_FLAGS_PRI_SERDES, PHY_MII_CTRLr_ADDR, &val);
	if (enable) {
		val &= ~MII_CTRL_PD;
	} else {
		val |= MII_CTRL_PD;
	}
	PHY542XX_REG_WR(phy_addr, PHY_REG_FLAGS_PRI_SERDES, PHY_MII_CTRLr_ADDR, val);

    return SOC_E_NONE;
}

int
phy542xx_force_auto_mdix(u32 phy_addr, int enable)
{
	u16 val;

	NET_TRACE(("%s: phy_addr %d\n", __FUNCTION__, phy_addr));

	PHY542XX_RDB_REG_RD(phy_addr, BCM542XX_REG_RDB_COPPER_MISC_CTRL, &val);
	if (enable) {
		val |= BCM542XX_REG_MISC_CTRL_FORCE_AUTO_MDIX;
	} else {
		val &= ~BCM542XX_REG_MISC_CTRL_FORCE_AUTO_MDIX;
	}
	PHY542XX_RDB_REG_WR(phy_addr, BCM542XX_REG_RDB_COPPER_MISC_CTRL, val);

    return SOC_E_NONE;
}
