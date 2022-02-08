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

#ifndef _EGPHY28_H_
#define _EGPHY28_H_

#define EGPHY28_REG_RDB_ADDR         0x1e
#define EGPHY28_REG_RDB_DATA         0x1f

#define EGPHY28_RDB_ACCESS_ADDR_1    0x17
#define EGPHY28_RDB_ACCESS_DATA_1  0x0f7e
#define EGPHY28_RDB_ACCESS_ADDR_2    0x15
#define EGPHY28_RDB_ACCESS_DATA_2  0x0000

/* Generic MII registers              */
#define EGPHY28_COPPER_MII_CTRL    0x00
#define EGPHY28_PHY_ID_MSB         0x02  /* PHY ID MSB                       */
#define EGPHY28_PHY_ID_LSB         0x03  /* PHY ID LSB                       */
#define EGPHY28_MII_ADVERTISE	     0x04  /* Advertisement control reg        */
#define EGPHY28_MII_CTRL1000	     0x09  /* 1000BASE-T control	             */
#define EGPGY28_MII_ECONTROL       0x10  /* Extended Control                 */
#define EGPHY28_COPPER_MISC_CTRL   0x2f  /* COPPER MISC CONTROL              */

/* For EGPHY28_COPPER_MII_CTRL(0x00)  */
#define BMCR_FULLDPLX	           0x0100  /* Full duplex		                   */
#define BMCR_SPEED1000           0x0040  /* MSB of Speed (1000)	             */
#define BMCR_ANENABLE            0x1000  /* Enable auto negotiation          */
#define BMCR_ANRESTART           0x0200  /* Auto negotiation restart         */
#define BMCR_PDOWN		           0x0800  /* Powerdown EGPHY28                */
#define BMCR_RESET		           0x8000  /* Reset EGPHY28                    */

/* For EGPHY28_MII_ADVERTISE(0x04)    */
#define ADVERTISE_1000HALF       0x0100  /* Advertise 1000BASE-T half duplex */
#define ADVERTISE_CSMA           0x0001	 /* Only selector supported          */
#define ADVERTISE_100FULL        0x0100	 /* Try for 100mbps full-duplex      */
#define ADVERTISE_10FULL         0x0040	 /* Try for 10mbps full-duplex       */
#define ADVERTISE_100HALF        0x0080	 /* Try for 100mbps half-duplex      */
#define ADVERTISE_10HALF         0x0020	 /* Try for 10mbps half-duplex       */

/* For EGPHY28_MII_CTRL1000(0x09)     */
#define ADVERTISE_1000FULL	     0x0200	 /* Advertise 1000BASE-T full duplex */
#define REPEATER_DTE             0x0400  /* Repeater/switch or DTE port type */

/* For EGPHY28_COPPER_MISC_CTRL(0x2f) */
#define BMCR_FORCE_AUTO_MDIX     0x0200

extern int egphy28_enable_set(u32 phy_addr, int enable);
extern int egphy28_init(void __iomem *base, u32 phy_addr);
extern int egphy28_enable_set(u32 phy_addr, int enable);
extern int egphy28_force_auto_mdix(u32 phy_addr, int enable);

#endif
