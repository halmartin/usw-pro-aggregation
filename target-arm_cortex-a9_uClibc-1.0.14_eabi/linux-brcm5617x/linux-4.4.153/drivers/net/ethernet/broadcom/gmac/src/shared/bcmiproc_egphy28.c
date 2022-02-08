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
#include "bcmiproc_egphy28.h"

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

extern u32 cmicd_schan_write(void __iomem *base, u32 ctrl, u32 addr, u32 val);
extern u32 cmicd_schan_read(void __iomem *base, u32 ctrl, u32 addr);


static int
egphy28_rdb_reg_read(u32 phy_addr, u32 reg_addr, u16 *data)
{
	int rv = SOC_E_NONE;
  
  /* MDIO write the RDB reg. address to reg.0x1E = <reg_addr> */
  iproc_mii_write(MII_DEV_LOCAL, phy_addr, EGPHY28_REG_RDB_ADDR,
                 (0xffff & reg_addr));
  
  /* MDIO read from reg.0x1F to get the RDB register's value as <data> */
	iproc_mii_read(MII_DEV_LOCAL, phy_addr, EGPHY28_REG_RDB_DATA, data);
  
  return rv;
}

static int
egphy28_rdb_reg_write(u32 phy_addr, u32 reg_addr, u16 data)
{
	int rv = SOC_E_NONE;
  
  /* MDIO write the RDB reg. address to reg.0x1E = <reg_addr> */
  iproc_mii_write(MII_DEV_LOCAL, phy_addr, EGPHY28_REG_RDB_ADDR,
	               (0xffff & reg_addr));
  
	/* MDIO write to reg.0x1F to set the RDB resister's value as <data> */
  iproc_mii_write(MII_DEV_LOCAL, phy_addr, EGPHY28_REG_RDB_DATA, data);
  
	return rv;
}

static void
egphy28_rdb_reg_modify(u32 phy_addr, int reg_addr, u16 data, u16 mask)
{
  u16 ori_data;
  
  egphy28_rdb_reg_read(phy_addr, reg_addr, &ori_data);
  ori_data &= ~mask;
  ori_data |= (data & mask);
  egphy28_rdb_reg_write(phy_addr, reg_addr, ori_data);
}

int
egphy28_reg_read(u32 phy_addr, int reg_addr, u16 *data)
{
	int rv = SOC_E_NONE;
	iproc_mii_read(MII_DEV_LOCAL, phy_addr, reg_addr, data);
	
  return rv;
}

int
egphy28_reg_write(u32 phy_addr, int reg_addr, u16 data)
{
	int rv = SOC_E_NONE;
	iproc_mii_write(MII_DEV_LOCAL, phy_addr, reg_addr, data);
	
  return rv;
}

static void
egphy28_reg_modify(u32 phy_addr, int reg_addr, u16 data, u16 mask)
{
  u16 ori_data;
  
  egphy28_reg_read(phy_addr, reg_addr, &ori_data);
  ori_data &= ~mask;
  ori_data |= (data & mask);
  egphy28_reg_write(phy_addr, reg_addr, ori_data);
}

static int
egphy28_ge_reset(u32 phy_addr)
{
	int rv = SOC_E_NONE;
	u16 val;

	NET_TRACE(("%s: phy_addr %d\n", __FUNCTION__, phy_addr));
  
	/* Reset the PHY */
	egphy28_reg_read(phy_addr, EGPHY28_COPPER_MII_CTRL, &val);
	val |= BMCR_RESET;
	egphy28_reg_write(phy_addr, EGPHY28_COPPER_MII_CTRL, val);
  
	SPINWAIT((!egphy28_reg_read(phy_addr, EGPHY28_COPPER_MII_CTRL, &val) &&
			  (val & BMCR_RESET)), 100000);
  
	/* Check if out of reset */
	egphy28_reg_read(phy_addr, EGPHY28_COPPER_MII_CTRL, &val);
	if (val & BMCR_RESET) {
		NET_ERROR(("%s reset not complete\n", __FUNCTION__));
		rv = SOC_E_TIMEOUT;
	} else {
		NET_TRACE(("%s reset complete\n", __FUNCTION__));
	}
  
	return rv;
}


#if 0
static void
cmid_schan_modify(void __iomem *base, u32 ctrl, u32 addr, u32 val, u32 mask)
{
	u32 ori_val;
	
	ori_val = cmicd_schan_read(base, ctrl, addr);
  ori_val &= ~mask;
  ori_val |= (val & mask);
  cmicd_schan_write(base, ctrl, addr, ori_val);
}
#endif


static int
egphy28_ge_init(void __iomem *base, u32 phy_addr)
{
	int rv = SOC_E_NONE;
	/* ==== Power up PHY ==== */
#if 0	
	/* Give initial value */
	/* TOP_QGPHY_CTRL_0.EXT_PWRDOWN[23:20] = LOW */
	cmid_schan_modify(base, 0x2c800200, 0x02033800, 0x0, 0x00F00000);
	/* TOP_QGPHY_CTRL_2.GPHY_IDDQ_GLOBAL_PWR[18] = HIGH */
	cmid_schan_modify(base, 0x2c800200, 0x02033400, 0x40000, 0x040000);
	/* TOP_QGPHY_CTRL_2.IDDQ_BIAS[5] = LOW */
	cmid_schan_modify(base, 0x2c800200, 0x02033400, 0x0, 0x20);
	/* TOP_SOFT_RESET_REG.TOP_QGPHY_RST_L[21] = LOW */
	cmid_schan_modify(base, 0x2c800200, 0x02030400, 0x0, 0x200000);
	
	/* TOP_QGPHY_CTRL_2.GPHY_IDDQ_GLOBAL_PWR[18] = LOW */
	cmid_schan_modify(base, 0x2c800200, 0x02033400, 0x0, 0x040000);
	/* TOP_SOFT_RESET_REG.TOP_QGPHY_RST_L[21] = HIGH */
	cmid_schan_modify(base, 0x2c800200, 0x02030400, 0x200000, 0x200000);
	
	/* ==== Partial Power down other 3 PHYs ==== */
	
	/* Give initial value */
	/* TOP_QGPHY_CTRL_0.EXT_PWRDOWN[22:20] = LOW */
	cmid_schan_modify(base, 0x2c800200, 0x02033800, 0x0, 0x00700000);
	/* TOP_QGPHY_CTRL_2.GPHY_IDDQ_GLOBAL_PWR[18] = HIGH */
	cmid_schan_modify(base, 0x2c800200, 0x02033400, 0x40000, 0x040000);
  /* TOP_QGPHY_CTRL_2.IDDQ_BIAS[5] = LOW */
	cmid_schan_modify(base, 0x2c800200, 0x02033400, 0x0, 0x20);
  /* TOP_SOFT_RESET_REG.TOP_QGPHY_RST_L[21] = LOW */
	cmid_schan_modify(base, 0x2c800200, 0x02030400, 0x0, 0x200000);
	
	/* TOP_QGPHY_CTRL_0.EXT_PWRDOWN[22:20] = HIGH */
	cmid_schan_modify(base, 0x2c800200, 0x02033800, 0x00700000, 0x00700000);
	/* TOP_QGPHY_CTRL_2.IDDQ_BIAS[5] = HIGH */
	cmid_schan_modify(base, 0x2c800200, 0x02033400, 0x20, 0x20);
	/* TOP_SOFT_RESET_REG.TOP_QGPHY_RST_L[21] = HIGH */
	cmid_schan_modify(base, 0x2c800200, 0x02030400, 0x200000, 0x200000);
	
	/* TOP_QGPHY_CTRL_2.GPHY_IDDQ_GLOBAL_PWR[18] = LOW */
	cmid_schan_modify(base, 0x2c800200, 0x02033400, 0x0, 0x040000);
	/* TOP_QGPHY_CTRL_2.IDDQ_BIAS[5] = LOW */
	cmid_schan_modify(base, 0x2c800200, 0x02033400, 0x0, 0x20);
	/* TOP_SOFT_RESET_REG.TOP_QGPHY_RST_L[21] = LOW */
	cmid_schan_modify(base, 0x2c800200, 0x02030400, 0x0, 0x200000);
	
	/* TOP_QGPHY_CTRL_2.GPHY_IDDQ_GLOBAL_PWR[18] = HIGH */
	cmid_schan_modify(base, 0x2c800200, 0x02033400, 0x040000, 0x040000);
	/* TOP_QGPHY_CTRL_2.IDDQ_BIAS[5] = HIGH */
	cmid_schan_modify(base, 0x2c800200, 0x02033400, 0x20, 0x20);
	/* TOP_SOFT_RESET_REG.TOP_QGPHY_RST_L[21] = HIGH */
	cmid_schan_modify(base, 0x2c800200, 0x02030400, 0x200000, 0x200000);
	
	/* TOP_QGPHY_CTRL_0.EXT_PWRDOWN[23] = LOW */
	cmid_schan_modify(base, 0x2c800200, 0x02033800, 0x0, 0x00800000);
	/* TOP_QGPHY_CTRL_2.GPHY_IDDQ_GLOBAL_PWR[18] = LOW */
	cmid_schan_modify(base, 0x2c800200, 0x02033400, 0x0, 0x040000);
	/* TOP_QGPHY_CTRL_2.IDDQ_BIAS[5] = LOW */
	cmid_schan_modify(base, 0x2c800200, 0x02033400, 0x0, 0x20);
	/* TOP_SOFT_RESET_REG.TOP_QGPHY_RST_L[21] = LOW */
	cmid_schan_modify(base, 0x2c800200, 0x02030400, 0x0, 0x200000);
	
	/* TOP_SOFT_RESET_REG.TOP_QGPHY_RST_L[21] = HIGH */
	cmid_schan_modify(base, 0x2c800200, 0x02030400, 0x200000, 0x200000);
	
	/* Reset the PHY (Register[0x00], bit 15 = 1) */
	egphy28_reg_modify(phy_addr, EGPHY28_COPPER_MII_CTRL, BMCR_RESET, BMCR_RESET);
  
	/*
	 * Enable direct RDB addressing mode, write to Expansion register
	 * 0x7E = 0x0000
	 *  - MDIO write to reg 0x17 = 0x0F7E
	 *  - MDIO write to reg 0x15 = 0x0000
	 */
	egphy28_reg_write(phy_addr, EGPHY28_RDB_ACCESS_ADDR_1, EGPHY28_RDB_ACCESS_DATA_1);
	egphy28_reg_write(phy_addr, EGPHY28_RDB_ACCESS_ADDR_2, EGPHY28_RDB_ACCESS_DATA_2);
	
	/* Clear Reset the PHY (Register[0x00], bit 15 = 0) */
	egphy28_reg_modify(phy_addr, EGPHY28_COPPER_MII_CTRL, 0x0, BMCR_RESET);
  
  /* Set MAC PHY interface to be GMII mode */
  egphy28_reg_modify(phy_addr, EGPGY28_MII_ECONTROL, 0x0, (1 << 15));
  
  /* Set 1000BASE-T full-duplex and switch device port (Register[0x09], bit 9,10 = 1) */
  egphy28_reg_write(phy_addr, EGPHY28_MII_CTRL1000, ADVERTISE_1000FULL | REPEATER_DTE);
  
  /* Set Full-duplex, 1000BASE-T, Auto-Negoatiation, Restartr-AN 
   * (Register[0x00], bit 8, 6, 12, 9 = 1) 
   */
  egphy28_reg_write(phy_addr, EGPHY28_COPPER_MII_CTRL, (BMCR_FULLDPLX | BMCR_SPEED1000 |
										  BMCR_ANENABLE | BMCR_ANRESTART));
	
  /* Disable super-isolate (RDB Register[0x02a], bit 5 = 0). in default */
  egphy28_rdb_reg_modify(phy_addr, 0x2a, 0x0, (1 << 5));
  
  /* Remove power down (Register[0x00], bit 11 = 0). in default */
  egphy28_reg_modify(phy_addr, EGPHY28_COPPER_MII_CTRL, ~BMCR_PDOWN, BMCR_PDOWN);
  
  /* Enable LEDs to indicate traffic status (Register[0x10], bit 5 = 1) */
  egphy28_reg_modify(phy_addr, 0x10, (1 << 5), (1 << 5));
  
  /* Enable extended packet length (4.5k through 25k) (RDB Register[0x28], bit 14 = 1) */
  egphy28_rdb_reg_modify(phy_addr, 0x28, (1 << 14), (1 << 14));
  
  egphy28_rdb_reg_modify(phy_addr, 0x16, (1 << 0), (1 << 0));
  egphy28_rdb_reg_modify(phy_addr, 0x1b, (1 << 1), (1 << 1));
  
  /* Configure LED selectors */
  /* Disable carrier extension */
  /* IEEE compliance setup */
  egphy28_rdb_reg_write(phy_addr, 0x1E4, 0x00C0);
  egphy28_rdb_reg_write(phy_addr, 0x1E7, 0xB008);
  egphy28_rdb_reg_write(phy_addr, 0x1E2, 0x02E3);
  egphy28_rdb_reg_write(phy_addr, 0x1E0, 0x0D11);
  egphy28_rdb_reg_write(phy_addr, 0x1E3, 0x7FC0);
  egphy28_rdb_reg_write(phy_addr, 0x1EB, 0x6B40);
  egphy28_rdb_reg_write(phy_addr, 0x1E8, 0x0213);
  egphy28_rdb_reg_write(phy_addr, 0x1E9, 0x0020);
  egphy28_rdb_reg_write(phy_addr, 0x28, 0x4C30);
  egphy28_rdb_reg_write(phy_addr, 0x125, 0x211B);
  egphy28_rdb_reg_write(phy_addr, 0xE, 0x0013);
  egphy28_rdb_reg_write(phy_addr, 0xB0, 0x000C);
  egphy28_rdb_reg_write(phy_addr, 0xB0, 0x0000);
  
  /* Set Full-duplex, 1000BASE-T, Auto-Negoatiation, Restartr-AN 
   * (Register[0x00], bit 8, 6, 12, 9 = 1) 
   */
  egphy28_reg_write(phy_addr, EGPHY28_COPPER_MII_CTRL, (BMCR_FULLDPLX | BMCR_SPEED1000 |
										  BMCR_ANENABLE | BMCR_ANRESTART));
  
  /* Automatic Master/Slave configuration (Register[0x09], bit 12 = 0) in default */
  egphy28_reg_modify(phy_addr, EGPHY28_MII_CTRL1000, 0x0, (1 << 12));
  
  /* Ability advert set : IEEE 802.3, 10HD, 100HD, 10FD, 100FD */
  /* (Register[0x04] bit 0, 5, 7, 6, 8 = 1 */
  egphy28_reg_modify(phy_addr, EGPHY28_MII_ADVERTISE, 
    (ADVERTISE_10HALF | ADVERTISE_100HALF | ADVERTISE_10FULL | ADVERTISE_100FULL | ADVERTISE_CSMA),
    (ADVERTISE_10HALF | ADVERTISE_100HALF | ADVERTISE_10FULL | ADVERTISE_100FULL | ADVERTISE_CSMA));
  /* Ability advert set :  switch device port, 1000BASE-T FD, non-1000BASE-T HD */
  /* (Register[0x09] bit 10, 9 = 1, bit 8 = 0 */
  egphy28_reg_modify(phy_addr, EGPHY28_MII_CTRL1000,
     (REPEATER_DTE | ADVERTISE_1000FULL), (REPEATER_DTE | ADVERTISE_1000FULL | ADVERTISE_1000HALF));
  
  /* Set Auto-Negoatiation, Restartr-AN (Register[0x00], bit 12, 9 = 1) */
  egphy28_reg_modify(phy_addr, EGPHY28_COPPER_MII_CTRL, (BMCR_ANENABLE | BMCR_ANRESTART),
                    (BMCR_ANENABLE | BMCR_ANRESTART));
  
  /* Clear bit 14 for automatic MDI crossover (Register[RDB 0x00] bit 14 = 0) in default */
  egphy28_rdb_reg_modify(phy_addr, 0x00, 0x0, (1 << 14));
  
  /* Clear bit 9 to disable forced auto MDI xover */
  egphy28_rdb_reg_modify(phy_addr, 0x2f, 0x0, (1 << 9));
#endif
  
	return rv;
}


int
egphy28_reset_setup(void __iomem *base, u32 phy_addr)
{
	int rv = SOC_E_NONE;

	NET_TRACE(("%s enter\n", __FUNCTION__));
	
	rv = egphy28_ge_reset(phy_addr);
	if (SOC_SUCCESS(rv)) {
	    rv = egphy28_ge_init(base, phy_addr);
	}

  return rv;
}

int
egphy28_init(void __iomem *base, u32 phy_addr)
{
	u16	phyid0, phyid1;
  
	NET_TRACE(("%s: phy_addr %d\n", __FUNCTION__, phy_addr));
  
	egphy28_reg_read(phy_addr, EGPHY28_PHY_ID_MSB, &phyid0);
	egphy28_reg_read(phy_addr, EGPHY28_PHY_ID_LSB, &phyid1);
  
	printf("%s Phy ChipID: 0x%04x:0x%04x\n", __FUNCTION__, phyid1, phyid0);
	//egphy28_reset_setup(base, phy_addr);
  
	return 0;
}

int
egphy28_enable_set(u32 phy_addr, int enable)
{
	u16 val;
  
	NET_TRACE(("%s: phy_addr %d\n", __FUNCTION__, phy_addr));
  
	egphy28_reg_read(phy_addr, EGPHY28_COPPER_MII_CTRL, &val);
	if (enable) {
		val &= ~BMCR_PDOWN;
	} else {
		val |= BMCR_PDOWN;
	}
	egphy28_reg_write(phy_addr, EGPHY28_COPPER_MII_CTRL, val);
  
  return SOC_E_NONE;
}

