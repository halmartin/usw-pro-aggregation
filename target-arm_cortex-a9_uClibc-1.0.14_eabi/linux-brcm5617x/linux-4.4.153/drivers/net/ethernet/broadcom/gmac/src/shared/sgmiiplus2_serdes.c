/*
 * $Copyright Open Broadcom Corporation$
 *
 * These routines provide access to the serdes
 *
 */

/* ---- Include Files ---------------------------------------------------- */
#include <bcmutils.h>
#include <bcmenetphy.h>
#include "bcmiproc_serdes.h"
#include "bcmiproc_serdes_def.h"
#include "../../../mdio/iproc_mdio.h"

/* ---- External Variable Declarations ----------------------------------- */
/* ---- External Function Prototypes ------------------------------------- */
/* ---- Public Variables ------------------------------------------------- */
/* ---- Private Constants and Types -------------------------------------- */
/* ---- Private Variables ------------------------------------------------ */

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

void
sgmiiplus2_serdes_reset(uint eth_num, uint phyaddr)
{
	uint16 ctrl;

	ASSERT(phyaddr < MAXEPHY);

	if (phyaddr == EPHY_NOREG)
		return;
	
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x1f, 0x0);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x0, 0x8000);

	udelay(100);

	iproc_mii_read(MII_DEV_LOCAL, phyaddr, 0x0, &ctrl);
    	if (ctrl & 0x8000)
        	NET_ERROR(("et%d: %s serdes reset not complete\n", eth_num, __FUNCTION__));

}

int
sgmiiplus2_serdes_init(uint eth_num, uint phyaddr)
{
	u16 id1, id2;

	iproc_mii_read(MII_DEV_LOCAL, phyaddr, 0x0002, &id1);
	iproc_mii_read(MII_DEV_LOCAL, phyaddr, 0x0003, &id2);
	printf("Internal phyaddr %d: Get PHY ID0:%.4x, ID1:%.4x\n", phyaddr, id1, id2);
	
	/* Disable PLL */
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x001f, 0xffd0);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x001e, 0x0000);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x001f, 0x8000);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x0010, 0x062f);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x001e, 0x0000);

	/* Disable lmtcal (broadcast to all lanes) */
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x001f, 0xffd0);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x001e, 0x001f);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x001f, 0x8480);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x0012, 0x83f8);

	/* Auto negotiation 10/100/1G - SGMII Slave (broadcast to all lanes) */
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x001f, 0x8300);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x0010, 0x0100);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x001f, 0x8340);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x001a, 0x0003);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x001f, 0x0000);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x0000, 0x1140);

	/* Change PLL calibration threshold to 0xc */
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x001f, 0xffd0);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x001e, 0x0000);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x001f, 0x8180);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x0011, 0x0600);

	/* Enable PLL */
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x001f, 0x8000);
	iproc_mii_write(MII_DEV_LOCAL, phyaddr, 0x0010, 0x262f);

	return 0;
}
