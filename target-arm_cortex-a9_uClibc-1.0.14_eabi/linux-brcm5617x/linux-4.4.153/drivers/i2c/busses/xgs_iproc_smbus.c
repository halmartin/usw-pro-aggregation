/*
 * $Copyright Open Broadcom Corporation$
 */
 
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "iproc_smbus_regs.h"
#include "iproc_smbus_defs.h"
#include "iproc_smbus.h"

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#endif /* CONFIG_OF */

#undef IPROC_SMB_DBG
 
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 37)
#define init_MUTEX(x) sema_init(x,1)
#endif

static struct proc_dir_entry *gProcParent=NULL;
//static int use_svk_version;
#undef CONFIG_USE_SVK_VERSION

static int smb_in_intr;
 
static struct iproc_smb_drv_int_data *iproc_smbus_list = NULL;
static int iproc_smbus_block_init(struct iproc_smb_drv_int_data *dev);

/* Function to read a value from specified register. */
static unsigned int iproc_smb_reg_read(unsigned long reg_addr)
{
    unsigned int val;

    val = ioread32((void *)reg_addr);

#ifdef IPROC_SMB_DBG
    if (!smb_in_intr) {
        printk(KERN_DEBUG "\nRd: addr:0x%08X, val:0x%08X", (unsigned int)reg_addr, val);
    }
#endif

    return(val);
}

/* Function to write a value ('val') in to a specified register. */
static int iproc_smb_reg_write(unsigned long reg_addr, unsigned int val)
{
    iowrite32(val, (void *)reg_addr);

#ifdef IPROC_SMB_DBG
    if (!smb_in_intr) {
        printk(KERN_DEBUG "\nWr: addr:0x%08X, val:0x%08X", (unsigned int)reg_addr, val);
    }
#endif

    return (0);
}

#ifdef IPROC_SMB_DBG
static int iproc_dump_smb_regs(struct iproc_smb_drv_int_data *dev)
{
    unsigned int regval;
    unsigned long base_addr = (unsigned long)dev->block_base_addr;

    printk(KERN_DEBUG "\n----------------------------------------------");

    printk(KERN_DEBUG "\nBase addr=0x%08X", (unsigned int)base_addr);

    printk(KERN_DEBUG "%s: Dumping SMBus registers... ", __func__);

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_CFG_REG);
    printk(KERN_DEBUG "\nCCB_SMB_CFG_REG=0x%08X", regval);

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_TIMGCFG_REG);
    printk(KERN_DEBUG "\nCCB_SMB_TIMGCFG_REG=0x%08X", regval);

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_ADDR_REG);
    printk(KERN_DEBUG "\nCCB_SMB_ADDR_REG=0x%08X", regval);

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_MSTRFIFOCTL_REG);
    printk(KERN_DEBUG "\nCCB_SMB_MSTRFIFOCTL_REG=0x%08X", regval);

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_SLVFIFOCTL_REG);
    printk(KERN_DEBUG "\nCCB_SMB_SLVFIFOCTL_REG=0x%08X", regval);

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_BITBANGCTL_REG);
    printk(KERN_DEBUG "\nCCB_SMB_BITBANGCTL_REG=0x%08X", regval);

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_MSTRCMD_REG);
    printk(KERN_DEBUG "\nCCB_SMB_MSTRCMD_REG=0x%08X", regval);

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_SLVCMD_REG);
    printk(KERN_DEBUG "\nCCB_SMB_SLVCMD_REG=0x%08X", regval);

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_EVTEN_REG);
    printk(KERN_DEBUG "\nCCB_SMB_EVTEN_REG=0x%08X", regval);

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_EVTSTS_REG);
    printk(KERN_DEBUG "\nCCB_SMB_EVTSTS_REG=0x%08X", regval);

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_MSTRDATAWR_REG);
    printk(KERN_DEBUG "\nCCB_SMB_MSTRDATAWR_REG=0x%08X", regval);

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_MSTRDATARD_REG);
    printk(KERN_DEBUG "\nCCB_SMB_MSTRDATARD_REG=0x%08X", regval);

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_SLVDATAWR_REG);
    printk(KERN_DEBUG "\nCCB_SMB_SLVDATAWR_REG=0x%08X", regval);

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_SLVDATARD_REG);
    printk(KERN_DEBUG "\nCCB_SMB_SLVDATARD_REG=0x%08X", regval);

    printk(KERN_DEBUG "\n----------------------------------------------\n\n");

    return(0);
}
#endif /* IPROC_SMB_DBG */

static irqreturn_t iproc_smb_isr(int irq, void*devid)
{
    struct iproc_smb_drv_int_data *dev = 
                                     (struct iproc_smb_drv_int_data *)devid;
    unsigned int intsts;
    unsigned int regval;


    smb_in_intr = 1;
    intsts = iproc_smb_reg_read((unsigned long)dev->block_base_addr +
                                       CCB_SMB_EVTSTS_REG);
    dev->smb_counters.last_int_sts = intsts;

    if (!intsts) {
        /* Likely received a spurious interrupt */
        return IRQ_NONE;
    }

    /* Clear interrupts */
    iproc_smb_reg_write((unsigned long)dev->block_base_addr +
                               CCB_SMB_EVTSTS_REG, intsts);

    /* Master read or write complete */
    if ((intsts & CCB_SMB_MSTRSTARTBUSYEN_MASK) ||
        (intsts & CCB_SMB_MSTRRXEVTSTS_MASK)) {

        if (intsts & CCB_SMB_MSTRSTARTBUSYEN_MASK) {

            dev->smb_counters.mstr_start_busy_cnt++;

        }

        if (intsts & CCB_SMB_MSTRRXEVTSTS_MASK) {

            dev->smb_counters.mstr_rx_evt_cnt++;

        }

        /* In case of a receive transaction, data will be copied in the recv
         * function
         */
        complete(&dev->ses_done);

    }

    /* If RX FIFO was full we can either read and then flush the FIFO. Or, only
     * flush the FIFO (since the client process did not read the data on time),
     * and then the client process can restart the transaction
     * For now, we will flush the later action.
     */
    if (intsts & CCB_SMB_MSTRRXFIFOFULLSTS_MASK) {

        dev->smb_counters.mstr_rx_fifo_full_cnt++;

        regval = iproc_smb_reg_read((unsigned long)dev->block_base_addr +
                                        CCB_SMB_MSTRFIFOCTL_REG);

        regval |= CCB_SMB_MSTRRXFIFOFLSH_MASK;
        iproc_smb_reg_write((unsigned long)dev->block_base_addr +
                               CCB_SMB_MSTRFIFOCTL_REG, regval);
        
        complete(&dev->ses_done);
 
    }

    smb_in_intr = 0;

    return IRQ_HANDLED;
}

/*
 * Function to ensure that the previous transaction was completed before
 * initiating a new transaction. It can also be used in polling mode to
 * check status of completion of a command
 */
static int iproc_smb_startbusy_wait(struct iproc_smb_drv_int_data *dev)
{
    unsigned int regval;

    regval = iproc_smb_reg_read((unsigned long)dev->block_base_addr +
                                    CCB_SMB_MSTRCMD_REG);

    /* Check if an operation is in progress. During probe it won't be.
     * But when shutdown/remove was called we want to make sure that
     * the transaction in progress completed
     */
    if (regval & CCB_SMB_MSTRSTARTBUSYCMD_MASK) {
        unsigned int i = 0;

        do {
            msleep(1); /* Wait for 1 msec */

            i++;
            regval = iproc_smb_reg_read(
                     (unsigned long)dev->block_base_addr + CCB_SMB_MSTRCMD_REG);
          /* If start-busy bit cleared, exit the loop */
        } while ((regval & CCB_SMB_MSTRSTARTBUSYCMD_MASK) &&
                 (i < IPROC_SMB_MAX_RETRIES));

        if (i >= IPROC_SMB_MAX_RETRIES) {
            printk(KERN_ERR "%s: %s START_BUSY bit didn't clear, exiting\n",
                   __func__, dev->adapter.name);
            return -ETIMEDOUT;
        }
    }

   return 0;
}


static unsigned int smbus0_sdaRecoveryCnt = 0, smbus0_sdaFailedCnt = 0, smbus0_startBusyCnt = 0;
static unsigned int smbus1_sdaRecoveryCnt = 0, smbus1_sdaFailedCnt = 0, smbus1_startBusyCnt = 0;

/*
 * Function to recover SMB hangs caused stuck master START_BUSY.
 *   Returns  0 if recovery procedure executed successfully.
 *   Returns -1 if recovery failed.
 */
static int iproc_smb_startbusy_recovery(struct iproc_smb_drv_int_data *dev)
{
  int rc = -1;
  unsigned int recoveryCnt;

  if (dev->adapter.nr == 0) {
    recoveryCnt = ++smbus0_startBusyCnt;
  }
  else {
    recoveryCnt = ++smbus1_startBusyCnt;
  }

  printk(KERN_INFO "%s: %s START_BUSY recovery #%d \n", __func__, dev->adapter.name, recoveryCnt);

  /* reset the SMBus block, wait a minimum of 50 uSecs and then re-initialize */
  iproc_smb_reg_write((unsigned long)dev->block_base_addr + CCB_SMB_CFG_REG, CCB_SMB_CFG_RST_MASK);
  udelay(60);

  if ( iproc_smbus_block_init(dev) == 0 ) {
    rc = 0;
  }
  
  return rc;
}



/*
 * Function to recover SMB hang caused by a slave device holding SDA low.
 *   Returns  0 if recovery procedure executed successfully. 
 *   Returns -1 if recovery failed.
 */

static int iproc_smb_sda_low_recovery(struct iproc_smb_drv_int_data *dev)
{
  unsigned int bbReg, cfgReg, cfgSave, recoveryCnt, failedCnt, i;
  int rc = -1;


  /* enable bit-bang */
  cfgSave = iproc_smb_reg_read((unsigned long)dev->block_base_addr + CCB_SMB_CFG_REG);
  cfgReg  = cfgSave;
  cfgReg |= CCB_SMB_CFG_BITBANGEN_MASK;
  iproc_smb_reg_write((unsigned long)dev->block_base_addr + CCB_SMB_CFG_REG, cfgReg);
  udelay(50);

  /* start with clock and SDA set high */
  bbReg  = iproc_smb_reg_read((unsigned long)dev->block_base_addr + CCB_SMB_BITBANGCTL_REG);

  bbReg |= (CCB_SMB_SMBCLKOUTEN_MASK | CCB_SMB_SMBDATAOUTEN_MASK);
  iproc_smb_reg_write((unsigned long)dev->block_base_addr + CCB_SMB_BITBANGCTL_REG, bbReg);
  udelay(5);   /* should be sufficient for 100 KHz bus */

  /* set up to toggle the clock line with SDA out held high for 9 cycles */
  for (i=0; i<18; i++)
  {
    /* toggle CLK out */
    if ( (bbReg & CCB_SMB_SMBCLKOUTEN_MASK) == 0 ) {
      bbReg |= CCB_SMB_SMBCLKOUTEN_MASK;                   /* set clock high */
    }
    else {
      bbReg &= ~CCB_SMB_SMBCLKOUTEN_MASK;                  /* set clock low  */
    }

    iproc_smb_reg_write((unsigned long)dev->block_base_addr + CCB_SMB_BITBANGCTL_REG, bbReg);
    udelay(5);
  }

  /* check bit 29 -- SMBDAT_IN and make sure SDA not being held low any more */
  for ( i=0; i<10; i++ )
  {
    bbReg  = iproc_smb_reg_read((unsigned long)dev->block_base_addr + CCB_SMB_BITBANGCTL_REG);
    bbReg &= CCB_SMB_SMBDATAIN_MASK;
  
    if (bbReg) 
      break;
  
    udelay(1);
  }
  
  if ( bbReg == 0 ) {
    /* SDA is still low */
    if (dev->adapter.nr == 0) {
      failedCnt = ++smbus0_sdaFailedCnt;
    }
    else {
      failedCnt = ++smbus1_sdaFailedCnt;
    }
    printk(KERN_INFO "\n%s: %s SDA release #%d FAILED.\n", __func__, dev->adapter.name, failedCnt);
  }
  else {
    if (dev->adapter.nr == 0) {
      recoveryCnt = ++smbus0_sdaRecoveryCnt;
    }
    else {
      recoveryCnt = ++smbus1_sdaRecoveryCnt;
    }
  
    printk(KERN_INFO "%s: %s SDA release #%d SUCCESSFUL.\n", __func__, dev->adapter.name, recoveryCnt);
    rc = 0;
  }


  /* manually issue a stop by transitioning SDA from low to high with clock held high */
  bbReg  = iproc_smb_reg_read((unsigned long)dev->block_base_addr + CCB_SMB_BITBANGCTL_REG);
  bbReg &= ~CCB_SMB_SMBCLKOUTEN_MASK;                  /* set clock low     */
  iproc_smb_reg_write((unsigned long)dev->block_base_addr + CCB_SMB_BITBANGCTL_REG, bbReg);
  udelay(2);

  bbReg &= ~CCB_SMB_SMBDATAOUTEN_MASK;                 /* drop SDA low      */
  iproc_smb_reg_write((unsigned long)dev->block_base_addr + CCB_SMB_BITBANGCTL_REG, bbReg);
  udelay(2);

  bbReg |= CCB_SMB_SMBCLKOUTEN_MASK;                   /* set clock high    */
  iproc_smb_reg_write((unsigned long)dev->block_base_addr + CCB_SMB_BITBANGCTL_REG, bbReg);
  udelay(5);

  bbReg |= CCB_SMB_SMBDATAOUTEN_MASK;                  /* pull SDA high     */
  iproc_smb_reg_write((unsigned long)dev->block_base_addr + CCB_SMB_BITBANGCTL_REG, bbReg);
  udelay(2);


  /* disable bit-bang and then re-enable the SMB with the saved configuration */
  cfgReg  = iproc_smb_reg_read((unsigned long)dev->block_base_addr + CCB_SMB_CFG_REG);
  cfgReg &= ~CCB_SMB_CFG_BITBANGEN_MASK;
  iproc_smb_reg_write((unsigned long)dev->block_base_addr + CCB_SMB_CFG_REG, cfgReg);
  udelay(10);
  
  iproc_smb_reg_write((unsigned long)dev->block_base_addr + CCB_SMB_CFG_REG, cfgSave);
  
  return rc;
}


/*
 * Function to recover SMB hang caused by a slave device hold SDA low.
 *   Returns  0 if recovery procedure executed successfully.
 *   Returns -1 if recovery failed.
 */
static int iproc_smb_timeout_recovery(struct iproc_smb_drv_int_data *dev)
{
    unsigned int bbReg, mCmdReg;
    int rc = -1;

    /* read bit-bang control.  If SDA low, attempt SDA release recovery */
    bbReg = iproc_smb_reg_read((unsigned long)dev->block_base_addr + CCB_SMB_BITBANGCTL_REG);
    if ((bbReg & CCB_SMB_SMBDATAIN_MASK) == 0) {
        if (iproc_smb_sda_low_recovery(dev) == 0) {
            rc = 0;
        }
    }

    /* regardless of whether there was an SDA hang or not, see if START_BUSY stuck high */
    mCmdReg = iproc_smb_reg_read( (unsigned long)dev->block_base_addr + CCB_SMB_MSTRCMD_REG );
    if (mCmdReg & CCB_SMB_MSTRSTARTBUSYCMD_MASK) {
        /* attempt to recover the bus */
        if (iproc_smb_startbusy_recovery(dev) == 0) {
            rc = 0;
        }
    }
    return rc;
}

/*
 * This function copies data to SMBus's Tx FIFO. Valid for write transactions
 * only
 *
 * base_addr: Mapped address of this SMBus instance
 * dev_addr:  SMBus (I2C) device address. We are assuming 7-bit addresses
 *            initially
 * info:   Data to copy in to Tx FIFO. For read commands, the size should be
 *         set to zero by the caller
 *
 */
static void iproc_smb_write_trans_data(unsigned long base_addr,
                                       unsigned short dev_addr,
                                       struct iproc_xact_info *info)
{
    unsigned int regval;
    unsigned int i;
    unsigned int num_data_bytes = 0;

#ifdef IPROC_SMB_DBG
    printk("%s: dev_addr=0x%X, offset=%u, cmd_valid=%u, size=%u\n",
                __func__, dev_addr, info->command, info->cmd_valid, info->size);
#endif /* IPROC_SMB_DBG */
 
    /* Write SMBus device address first */
    /* Note, we are assuming 7-bit addresses for now. For 10-bit addresses,
     * we may have one more write to send the upper 3 bits of 10-bit addr
     */
#if defined(CONFIG_MACH_SB2)
    /* For EEPROM AT24C64D, the "random read" is composed of :
         1. device address : 7 bits + Write bit(0)
         2. 1st word address : 8 bits
         3. 2nd word address : 8 bits
         4. START bit  <--- We can't compose this command for the extra "START bit" 
                            since it is done in TX FIFO for smbus in iProc.....
         5. device address : 7 bits + Read bit(1)
         6. DATA : 8 bits
       
       We can use "current address read" to verify I2C read/write.
       Ex. 1. write continuous several bytes
           2. write the first byte of those continuous bytes (counter will point to the second byte)
           3. read this device should get the second byte.
           4. read this device should get the third byte..... */
    
    if (info->smb_proto == SMBUS_PROT_RD_BYTE)
    {
    	 iproc_smb_reg_write(base_addr + CCB_SMB_MSTRDATAWR_REG, dev_addr | 0x01);
    	 iproc_smb_reg_write(base_addr + CCB_SMB_MSTRDATAWR_REG, CCB_SMB_MSTRWRSTS_MASK | info->data[0]);
    	 return;
    }
    else
#endif
       iproc_smb_reg_write(base_addr + CCB_SMB_MSTRDATAWR_REG, dev_addr);


    /* If the protocol needs command code, copy it */
    if (info->cmd_valid == true) {

#if defined(CONFIG_MACH_SB2)
        if (info->smb_proto == SMBUS_PROT_WR_BYTE)
        	 iproc_smb_reg_write(base_addr + CCB_SMB_MSTRDATAWR_REG, 0);
        else
#endif
           iproc_smb_reg_write(base_addr + CCB_SMB_MSTRDATAWR_REG, info->command);
    }

    /* Depending on the SMBus protocol, we need to write additional transaction
     * data in to Tx FIFO. Refer to section 5.5 of SMBus spec for sequence for a
     * transaction
     */
    switch (info->smb_proto) {
        case SMBUS_PROT_RECV_BYTE:
            /* No additional data to be written */
            num_data_bytes = 0;
            break;

        case SMBUS_PROT_SEND_BYTE:
            num_data_bytes = info->size;
            break;

        case SMBUS_PROT_RD_BYTE:
        case SMBUS_PROT_RD_WORD:
            /* Write slave address with R/W~ set (bit #0) */
            iproc_smb_reg_write(base_addr + CCB_SMB_MSTRDATAWR_REG,
                                dev_addr | 0x1);
            break;
        case SMBUS_PROT_BLK_RD:
            if (info->cmd_valid == true) {
                /* Write slave address with R/W~ set (bit #0) */
                iproc_smb_reg_write(base_addr + CCB_SMB_MSTRDATAWR_REG,
                                    dev_addr | 0x1);
            }
            num_data_bytes = 0;
            break;

        case SMBUS_PROT_WR_BYTE:
        case SMBUS_PROT_WR_WORD:
            /* No additional bytes to be written. Data portion is written in the
             * 'for' loop below
             */
#if defined(CONFIG_MACH_SB2)
            iproc_smb_reg_write(base_addr + CCB_SMB_MSTRDATAWR_REG, info->command);
#endif
            num_data_bytes = info->size;

            /* Note for hx4 eeprom (at24c64). the low addr bytes can be passed
             * in to 1st byte of info->data
             */
            break;

        case SMBUS_PROT_BLK_WR:
            if (info->cmd_valid == true) {
                /* 3rd byte is byte count */
                iproc_smb_reg_write(base_addr + CCB_SMB_MSTRDATAWR_REG, info->size);
            }
            num_data_bytes = info->size;
            break;

        case SMBUS_PROT_BLK_WR_BLK_RD_PROC_CALL:
            /* Write byte count */
            iproc_smb_reg_write(base_addr + CCB_SMB_MSTRDATAWR_REG, info->size);
            num_data_bytes = info->size;
            break;

        default:
            break;
    }

    /* Copy actual data from caller, next. In general, for reads, no data is
     * copied
     */
    for (i = 0; num_data_bytes; --num_data_bytes, i++) {
        /* For the last byte, set MASTER_WR_STATUS bit. For block rd/wr process
         * call, we need to program slave addr after copying data byte(s), so
         * master status bit is set later, after the loop
         */
        if ((num_data_bytes == 1) &&
            (info->smb_proto != SMBUS_PROT_BLK_WR_BLK_RD_PROC_CALL)) {
            regval = info->data[i] | CCB_SMB_MSTRWRSTS_MASK;
        } else {
            regval =  info->data[i];
        }
        iproc_smb_reg_write(base_addr + CCB_SMB_MSTRDATAWR_REG, regval);
    }

    if (info->smb_proto == SMBUS_PROT_BLK_WR_BLK_RD_PROC_CALL) {
        /* Write device address needed during repeat start condition */
        iproc_smb_reg_write(base_addr + CCB_SMB_MSTRDATAWR_REG,
                            CCB_SMB_MSTRWRSTS_MASK | dev_addr | 0x1);
    }

    return;
}

static int iproc_smb_data_send(struct i2c_adapter *adapter,
                               unsigned short addr,
                               struct iproc_xact_info *info)
{
    int rc;
    unsigned int regval;
    struct iproc_smb_drv_int_data *dev = i2c_get_adapdata(adapter);
    unsigned long time_left;

    /* Make sure the previous transaction completed */
    rc = iproc_smb_startbusy_wait(dev);

    if (rc < 0) {
        printk(KERN_ERR "%s: Send: %s bus is busy, attempt recovery \n",
               __func__, dev->adapter.name);

        /* attempt to recover the bus */
        if (iproc_smb_startbusy_recovery(dev) != 0) {
            return rc;
        }
    }

    if (dev->enable_evts == ENABLE_INTR) {
        /* Enable start_busy interrupt */
        regval = iproc_smb_reg_read((unsigned long)dev->block_base_addr +
                                       CCB_SMB_EVTEN_REG);
        regval |= CCB_SMB_MSTRSTARTBUSYEN_MASK;
        iproc_smb_reg_write((unsigned long)dev->block_base_addr +
                            CCB_SMB_EVTEN_REG, regval);
        /* Mark as incomplete before sending the data */
        reinit_completion(&dev->ses_done);

    }

    /* Write transaction bytes to Tx FIFO */
    iproc_smb_write_trans_data((unsigned long)dev->block_base_addr, addr, info);

    /* Program master command register (0x30) with protocol type and set
     * start_busy_command bit to initiate the write transaction
     */
    regval = (info->smb_proto << CCB_SMB_MSTRSMBUSPROTO_SHIFT) |
              CCB_SMB_MSTRSTARTBUSYCMD_MASK;
    iproc_smb_reg_write((unsigned long)dev->block_base_addr +
                        CCB_SMB_MSTRCMD_REG, regval);

    if (dev->enable_evts == ENABLE_INTR) {
        /*
         * Block waiting for the transaction to finish. When it's finished,
         * we'll be signaled by an interrupt
         */
        time_left = wait_for_completion_timeout(&dev->ses_done, XACT_TIMEOUT);

        /* Disable start_busy interrupt */
        regval = iproc_smb_reg_read((unsigned long)dev->block_base_addr +
                                       CCB_SMB_EVTEN_REG);
        regval &= ~CCB_SMB_MSTRSTARTBUSYEN_MASK;
        iproc_smb_reg_write((unsigned long)dev->block_base_addr +
                               CCB_SMB_EVTEN_REG, regval);

        if (time_left == 0) {
            printk (KERN_INFO "%s: Send: %s timeout accessing device x%02x\n",
                   __func__, dev->adapter.name, addr);

            /* attempt to recover the bus */
            rc = iproc_smb_timeout_recovery(dev);
            if ( rc != 0 ) {
                return -ETIMEDOUT;
            } else {
                return -ECOMM;
            }
        }
    }

    regval = iproc_smb_reg_read((unsigned long)dev->block_base_addr +
                                CCB_SMB_MSTRCMD_REG);

    /* If start_busy bit cleared, check if there are any errors */
    if (!(regval & CCB_SMB_MSTRSTARTBUSYCMD_MASK)) {
        /* start_busy bit cleared, check master_status field now */
        regval &= CCB_SMB_MSTRSTS_MASK;
        regval >>= CCB_SMB_MSTRSTS_SHIFT;

        if (regval != MSTR_STS_XACT_SUCCESS) {
            /* We can flush Tx FIFO here */
            printk(KERN_ERR "%s:Send: %s Error in transaction %d to device x%02x, exiting\n",
                     __func__, dev->adapter.name, regval, addr);
            return -EREMOTEIO;
        }
    }

    return(0);
}

static int iproc_smb_data_recv(struct i2c_adapter *adapter,
                               unsigned short addr,
                               struct iproc_xact_info *info,
                               unsigned int *num_bytes_read)
{
    int rc;
    unsigned int regval;
    struct iproc_smb_drv_int_data *dev = i2c_get_adapdata(adapter);
    unsigned long time_left;

    /* Make sure the previous transaction completed */
    rc = iproc_smb_startbusy_wait(dev);

    if (rc < 0) {
        printk(KERN_ERR "%s: Receive: %s bus is busy, attempt recovery \n", __func__, dev->adapter.name);

        /* attempt to recover the bus */
        if (iproc_smb_startbusy_recovery(dev) != 0) {
            return rc;
        }
    }

    if (dev->enable_evts == ENABLE_INTR) {
        /* Enable start_busy interrupt */
        regval = iproc_smb_reg_read((unsigned long)dev->block_base_addr +
                                    CCB_SMB_EVTEN_REG);
        /* Set Rx_event_en bit for notification of reception event */
        regval |= (CCB_SMB_MSTRSTARTBUSYEN_MASK);
        iproc_smb_reg_write((unsigned long)dev->block_base_addr +
                               CCB_SMB_EVTEN_REG, regval);
        
        /* Mark as incomplete before sending the data */
        reinit_completion(&dev->ses_done);

    }

    /* Program all transaction bytes into master Tx FIFO */
    iproc_smb_write_trans_data((unsigned long)dev->block_base_addr, addr, info);

    /* Program master command register (0x30) with protocol type and set
     * start_busy_command bit to initiate the write transaction
     */
    regval = (info->smb_proto << CCB_SMB_MSTRSMBUSPROTO_SHIFT) |
              CCB_SMB_MSTRSTARTBUSYCMD_MASK | info->size;
    iproc_smb_reg_write((unsigned long)dev->block_base_addr +
                        CCB_SMB_MSTRCMD_REG, regval);

    if (dev->enable_evts == ENABLE_INTR) {
        /*
         * Block waiting for the transaction to finish. When it's finished,
         * we'll be signaled by an interrupt
         */
        time_left = wait_for_completion_timeout(&dev->ses_done, XACT_TIMEOUT);

        /* Disable start_busy and rx_event interrupts. Above call has handled
         * the interrupt
         */
        regval = iproc_smb_reg_read((unsigned long)dev->block_base_addr +
                                    CCB_SMB_EVTEN_REG);
        regval &= ~(CCB_SMB_MSTRSTARTBUSYEN_MASK);
        iproc_smb_reg_write((unsigned long)dev->block_base_addr +
                               CCB_SMB_EVTEN_REG, regval);

        if (time_left == 0) {
            printk(KERN_ERR "\n%s: Receive: %s timeout accessing device 0x%02x\n",
                   __func__, dev->adapter.name, addr);

            /* attempt to recover the bus */
            rc = iproc_smb_timeout_recovery(dev);
            if ( rc != 0 ) {
                return -ETIMEDOUT;
            } else {
                return -ECOMM;
            }
        }
    }

    regval = iproc_smb_reg_read((unsigned long)dev->block_base_addr +
                                CCB_SMB_MSTRCMD_REG);

    /* If start_busy bit cleared, check if there are any errors */
    if (!(regval & CCB_SMB_MSTRSTARTBUSYCMD_MASK)) {
        /* start_busy bit cleared, check master_status field now */
        regval &= CCB_SMB_MSTRSTS_MASK;
        regval >>= CCB_SMB_MSTRSTS_SHIFT;
        if (regval != MSTR_STS_XACT_SUCCESS) {
            /* We can flush Tx FIFO here */
            printk(KERN_DEBUG "\n%s: %s Error in transaction %d to device x%02x, exiting\n",
            __func__, dev->adapter.name, regval, addr);
           return -EREMOTEIO;
        }
    }

    /* In the isr we will read the received byte, and also deal with
     * rx fifo full event. The above check is for timeout error. If needed
     * we may move it to rx isr
     */

    /* Read received byte(s) */
    regval = iproc_smb_reg_read((unsigned long)dev->block_base_addr +
                                   CCB_SMB_MSTRDATARD_REG);

    /* For block read, protocol (hw) returns byte count, as the first byte */
    if ((info->smb_proto == SMBUS_PROT_BLK_RD) ||
        (info->smb_proto == SMBUS_PROT_BLK_WR_BLK_RD_PROC_CALL)) {
        int i, adj;

        if ( info->cmd_valid == true ) {
            *num_bytes_read = regval & CCB_SMB_MSTRRDDATA_MASK;
            adj = 0;
        } else {
            *num_bytes_read = info->size - 1;
            adj = 1;
            info->data[0] = regval & CCB_SMB_MSTRRDDATA_MASK;
        }      

        /* Limit to reading a max of 32 bytes only; just a safeguard. If
         * # bytes read is a number > 32, check transaction set up, and contact
         * hw engg. Assumption: PEC is disabled
         */
        for (i = 0; (i < *num_bytes_read) && (i < (I2C_SMBUS_BLOCK_MAX - adj)); i++) {
            /* Read Rx FIFO for data bytes */
            regval = iproc_smb_reg_read((unsigned long)dev->block_base_addr +
                                        CCB_SMB_MSTRDATARD_REG);
            info->data[i + adj] = regval & CCB_SMB_MSTRRDDATA_MASK;
        }
        /* To make sure that atmost 32 bytes are read */
        *num_bytes_read = i + adj;
    } else {
        *info->data = regval & CCB_SMB_MSTRRDDATA_MASK;
        *num_bytes_read = 1;

        if (info->smb_proto == SMBUS_PROT_RD_WORD) {
          /* Read Rx FIFO for data bytes */
            regval = iproc_smb_reg_read((unsigned long)dev->block_base_addr +
                                        CCB_SMB_MSTRDATARD_REG);
            info->data[1] = regval & CCB_SMB_MSTRRDDATA_MASK;
            *num_bytes_read = 2;
        }
    }

    return(0);
}

static int iproc_smb_xfer(struct i2c_adapter *i2c_adap, u16 addr,
                          unsigned short flags, char read_write,
                          u8 command, int size, union i2c_smbus_data *data)
{
    int rc;
    struct iproc_smb_drv_int_data *dev = i2c_get_adapdata(i2c_adap);
    struct iproc_xact_info info;
    unsigned int num_bytes_read = 0;

    down(&dev->xfer_lock);
    addr <<= 1;
    switch (size /* protocol */) {
        case I2C_SMBUS_BYTE:
            info.cmd_valid = false;
            info.command = command; /* not used */
            if (read_write == I2C_SMBUS_WRITE) {
                info.data = &command;
            } else {
                info.data = &data->byte;
            }
            info.size = 1;
            info.flags = flags;

            if (read_write == I2C_SMBUS_READ) {
                addr |= 0x1; /* Read operation */
                info.smb_proto = SMBUS_PROT_RECV_BYTE;
                info.data = &data->byte;
            } else {
                info.smb_proto = SMBUS_PROT_SEND_BYTE;
            }
            break;

        case I2C_SMBUS_BYTE_DATA:
            info.cmd_valid = true;
            info.command = command;
            info.data = &data->byte;
            info.size = 1;
            info.flags = flags;

            if (read_write == I2C_SMBUS_READ) {
                info.smb_proto = SMBUS_PROT_RD_BYTE;
            } else {
                info.smb_proto = SMBUS_PROT_WR_BYTE;
                //info.smb_proto = SMBUS_PROT_WR_WORD; /* TEMP chg. remove later */
            }
            break;

        case I2C_SMBUS_WORD_DATA:
            info.cmd_valid = true;
            info.command = command;
            info.data = (unsigned char *)(&data->word);
            info.size = 2;
            info.flags = flags;
            if (read_write == I2C_SMBUS_READ) {
                info.smb_proto = SMBUS_PROT_RD_WORD;
                /* Protocol(hw) returns data byte count as part of response,
                   for smbus compliant devices */
                // info.size = 0;
            } else {
                info.smb_proto = SMBUS_PROT_WR_WORD;
                info.size = 2;
            }
            break;

        case I2C_SMBUS_BLOCK_DATA:
            info.cmd_valid = true;
            info.command = command;
            info.data = &data->block[1];
            info.flags = flags;

            if (read_write == I2C_SMBUS_READ) {
                info.smb_proto = SMBUS_PROT_BLK_RD;
                /* See desc for RD_BYTE_COUNT in reg 0x30 about 'block read'.
                 * If '0', protocol(hw) returns data byte count as part of
                 * response.
                 */
                //info.size = 0;
                info.size = data->block[0];
            } else {
                info.smb_proto = SMBUS_PROT_BLK_WR;
                info.size = data->block[0]; /* i2c-core passes the length in
                                               this field */
            }
            break;

        case I2C_SMBUS_BLOCK_PROC_CALL:
            info.cmd_valid = true;
            info.command = command;
            info.data = &data->block[1];
            info.flags = flags;
            info.size = data->block[0];
            info.smb_proto = SMBUS_PROT_BLK_WR_BLK_RD_PROC_CALL;
            break;

        case I2C_SMBUS_I2C_BLOCK_DATA:
            info.cmd_valid = false;
            info.command = command;
            info.data = &data->block[1];
            info.flags = flags;

            if (read_write == I2C_SMBUS_READ) {
                info.smb_proto = SMBUS_PROT_BLK_RD;
                info.size = data->block[0];
                addr |= 0x01;
            } else {
                info.smb_proto = SMBUS_PROT_BLK_WR;
                info.size = data->block[0]; /* i2c-core passes the length in
                                               this field */
            }
            break;

        default:
            printk(KERN_ERR "%s: Unsupported transaction %d\n", __func__, size);
            up(&dev->xfer_lock);
            return -EINVAL;
    }


    if (read_write == I2C_SMBUS_READ) {
        /* Refer to i2c_smbus_read_byte for params passed. */
        rc = iproc_smb_data_recv(i2c_adap, addr, &info, &num_bytes_read);

        /* if failed due to bus hang, but recovered, retry once */
        if (rc == -ECOMM) {
            rc = iproc_smb_data_recv(i2c_adap, addr, &info, &num_bytes_read);
        }

        /* For block read call, we pass the actual amount of data sent by
         * slave, as expected by std Linux API
         */
        if ((info.smb_proto == SMBUS_PROT_BLK_RD) ||
            (info.smb_proto == SMBUS_PROT_BLK_WR_BLK_RD_PROC_CALL)) {
            if (rc == 0) {
                data->block[0] = num_bytes_read;
                //printk("%s: num bytes read=%u\n", __func__, data->block[0]);
            }
        }
    } else {
        /* Refer to i2c_smbus_write_byte params passed. */
        rc = iproc_smb_data_send(i2c_adap, addr, &info);

        /* if failed due to bus hang, but recovered, retry */
        if (rc == -ECOMM) {
          rc = iproc_smb_data_send(i2c_adap, addr, &info);
        }
    }

    if (rc < 0) {
        printk(KERN_DEBUG "%s %s: %s error accessing device 0x%X rc=%d\n", __func__, dev->adapter.name,
                (read_write == I2C_SMBUS_READ) ? "Read" : "Write", addr, rc);
        up(&dev->xfer_lock);
        return -EREMOTEIO;
    }

    up(&dev->xfer_lock);
    return (rc);
}

static int
proc_debug_read(char *buffer, char **start, off_t off, int count,
                int *eof, void *data)
{
    unsigned int len = 0;
    struct iproc_smb_drv_int_data *dev = (struct iproc_smb_drv_int_data *)data;

    if (off > 0) {
        return 0;
    }

    len += sprintf(buffer + len, "Debug print is %s\n",
           dev->debug ? "enabled" : "disabled");

    return len;
}

/* Command interface for reading/writing to various I2C/SMBus devices */
static int
proc_debug_write(struct file *file, const char __user *buffer,
                 unsigned long count, void *data)
{
    struct iproc_smb_drv_int_data *dev = (struct iproc_smb_drv_int_data *)data;
    int rc;
    unsigned char kbuf[MAX_PROC_BUF_SIZE];
    union i2c_smbus_data i2cdata;
    unsigned int val, i2cdev_addr, rd_wr_op;
    int addr;

    if (count > MAX_PROC_BUF_SIZE) {
        count = MAX_PROC_BUF_SIZE;
    }

    rc = copy_from_user(kbuf, buffer, count);
    if (rc) {
        printk(KERN_ERR "%s: copy_from_user failed status=%d\n", __func__, rc);
        return -EFAULT;
    }

    rc = sscanf(kbuf, "%u %u %d %u", &rd_wr_op, &i2cdev_addr, &addr, &val);
    if (rc != 4) {
        printk(KERN_ERR "\necho args > %s", PROC_ENTRY_DEBUG);
        printk(KERN_ERR "\nargs (all values should be in decimal)):");
        printk(KERN_ERR "\nrd_wr_op: 1 = read, 0 = write");
        printk(KERN_ERR "\ni2cdev_addr: I2C device address in decimal");
        printk(KERN_ERR "\noffset: offset of location within I2C device");
        printk(KERN_ERR "\naddr        -1 if offset not applicable");
        printk(KERN_ERR "\nval: For write op: 8-bit value.\n"
                        "     For read op: not used, may be 0\n\n");
        return count;
    }

    printk("\nArg values :");
    printk("\nrd_wr_op = %u", rd_wr_op);
    printk("\ni2cdev_addr = 0x%X", i2cdev_addr);
    printk("\noffset = %d", addr);
    printk("\nval = %u", val);
    if (rd_wr_op > 1) {
        printk(KERN_ERR "Error: Invalid rd_wr_op value %u\n", rd_wr_op);
        return count;
    }

    if (i2cdev_addr > 127) {
        printk(KERN_ERR "Error: i2cdev_addr must be 7-bit value\n");
        return count;
    }

    if (addr > 255) {
        printk(KERN_ERR "Error: offset out of range for this device\n");
        return count;
    }

    printk("Command can execute slow, please wait...\n");

    if (rd_wr_op == 0) { /* Write operation */
        i2cdata.byte = val;
        if (addr == -1) {
            /* Device does not support, or require an offset to write to the
             * location
             */
            rc = iproc_smb_xfer(&dev->adapter, i2cdev_addr, 0x0,
                                I2C_SMBUS_WRITE, (unsigned char)0,
                                I2C_SMBUS_BYTE, &i2cdata);
        } else {
            /* Address required for write access */
            rc = iproc_smb_xfer(&dev->adapter, i2cdev_addr, 0x0,
                                I2C_SMBUS_WRITE, addr, I2C_SMBUS_BYTE_DATA,
                                &i2cdata);
        }

        if (rc) {
            printk(KERN_ERR "%s: iproc_smb_xfer:write failed status=%d,"
                    " addr=%u, val = 0x%X\n", __func__, rc, addr, val);
            /* return -EFAULT; */
        } else {
            printk("Write OK.Wrote 0x%X at addr %u\n", val, addr);
        }

        msleep(1); /* Delay required, since smb(i2c) interface is slow */
    }

    if (rd_wr_op == 1) { /* Read operation */
        if (addr == -1) {
            /* Device does not support, or require an offset to read from the
             * location
             */
            rc = iproc_smb_xfer(&dev->adapter, i2cdev_addr, 0x0, I2C_SMBUS_READ,
                                (unsigned char)0, I2C_SMBUS_BYTE, &i2cdata);
        } else {
            rc = iproc_smb_xfer(&dev->adapter, i2cdev_addr, 0x0, I2C_SMBUS_READ,
                                addr, I2C_SMBUS_BYTE_DATA, &i2cdata);
        }

        if (rc) {
            printk(KERN_ERR "%s: iproc_smb_xfer failed status=%d\n", __func__, rc);
           /* return -EFAULT; */
        } else {
            printk("Read OK.Value read at %u = 0x%X\n", addr, i2cdata.byte);
        }
        msleep(1); /* Delay required, since smb(i2c) interface is slow */
    }

#ifdef IPROC_SMB_DBG
    iproc_dump_smb_regs(dev);
#endif /* IPROC_SMB_DBG */

    printk("Last intr sts = 0x%08X\n", dev->smb_counters.last_int_sts);
    printk("mstr_start_busy_cnt = %u, mstr_rx_evt_cnt = %u, rx fifo full cnt = %u\n\n",
           dev->smb_counters.mstr_start_busy_cnt,
           dev->smb_counters.mstr_rx_evt_cnt,
           dev->smb_counters.mstr_rx_fifo_full_cnt);

    return count;
}


static int
proc_debug_write_svk(struct file *file, const char __user *buffer, size_t count, loff_t *off)
{		
    struct iproc_smb_drv_int_data *dev = (struct iproc_smb_drv_int_data *) PDE_DATA (file->f_inode);
	int rc;
    unsigned char kbuf[MAX_PROC_BUF_SIZE];
    union i2c_smbus_data i2cdata;
    unsigned int val, i2cdev_addr, rd_wr_op;
    int addr;
	int i, j;
	unsigned int burst_len, repated_cnt, total_cnt;	   
 
    if (count > MAX_PROC_BUF_SIZE) {
       count = MAX_PROC_BUF_SIZE;
    }

    rc = copy_from_user(kbuf, buffer, count);
 
    if (rc) {
        printk (KERN_ERR "%s: copy_from_user failed status=%d", __func__, rc);
        return -EFAULT;

    }

	rc = sscanf(kbuf, "%u %u %d %u %u %u", &rd_wr_op, &i2cdev_addr, &addr, &val, &burst_len, &repated_cnt);
    if (rc != 6) {
    	burst_len = 1;
    	repated_cnt = 1;	

		if (rc < 4 ) {
        	printk(KERN_ERR "\necho args > %s", PROC_ENTRY_DEBUG);
        	printk(KERN_ERR "\nargs (all values should be in decimal)):");
        	printk(KERN_ERR "\nrd_wr_op: 1 = read, 0 = write");
        	printk(KERN_ERR "\ni2cdev_addr: I2C device address in decimal");
        	printk(KERN_ERR "\noffset: offset of location within I2C device");
        	printk(KERN_ERR "\naddr        -1 if offset not applicable");
        	printk(KERN_ERR "\nval: For write op: 8-bit value.\n"
                        "     For read op: not used, may be 0\n\n"); 
			printk(KERN_ERR "\burst_length: write block transfer byte length (<=8 for many EEPROM devices)");
			printk(KERN_ERR "\repated_cnt: number of repated write transfer of burst_len");
			return -EFAULT;
		}
    }
 	total_cnt = burst_len * repated_cnt;
 	
    printk(KERN_DEBUG "\nArg values :");
    printk(KERN_DEBUG "\nrd_wr_op = %u", rd_wr_op);
    printk(KERN_DEBUG "\ni2cdev_addr = 0x%X", i2cdev_addr);
    printk(KERN_DEBUG "\noffset = %d", addr);
    printk(KERN_DEBUG "\nval = %u", val);

    if (rd_wr_op > 1) {
        printk(KERN_ERR "\nError: Invalid rd_wr_op value %u\n", rd_wr_op);
        return count;
    }
    
    if (i2cdev_addr > 127) {
        printk(KERN_ERR "\nError: i2cdev_addr must be 7-bit value\n");
        return count;
    }

    if (addr > 255) {
        printk(KERN_ERR "\nError: offset out of range for this device\n");
        return count;
    }

    printk (KERN_ERR "\nCommand can execute slow, please wait...\n");

    if (rd_wr_op == 0) { /* Write operation */
		if (total_cnt == 1) {
#if defined(CONFIG_MACH_SB2)
            /* Testing EEPROM that requires 2-byte address */
            unsigned char *data = &i2cdata.word;
            data[0] = addr;
            data[1] = val;
            rc = iproc_smb_xfer(&dev->adapter, i2cdev_addr, 0x0,
                 I2C_SMBUS_WRITE, (unsigned char)0, I2C_SMBUS_WORD_DATA, &i2cdata);
#else
            i2cdata.byte = val;
            if (addr == -1) {
                /* Device does not support, or require an offset to write to the
                 * location
                 */
                rc = iproc_smb_xfer(&dev->adapter, i2cdev_addr, 0x0,
                                    I2C_SMBUS_WRITE, (unsigned char)0,
                                    I2C_SMBUS_BYTE, &i2cdata);
            }
            else {
                /* Address required for write access */
                rc = iproc_smb_xfer(&dev->adapter, i2cdev_addr, 0x0,
                                    I2C_SMBUS_WRITE, addr, I2C_SMBUS_BYTE_DATA, &i2cdata);
            }
#endif
            if (rc) {
                printk (KERN_ERR "\n%s: iproc_smb_xfer:write failed status=%d,"
                        " addr=%u, val = 0x%X\n", __func__, rc, addr, val);
                /* return -EFAULT; */
            }
            else {
                printk(KERN_ERR "Write OK. Wrote 0x%X at addr %u\n", val, addr);
            }
            msleep(1); /* Delay required, since smb(i2c) interface is slow */
    	}
		else {
/* test of block xfer: echo "0 80 0 0 8 16" > /proc/iproc-i2c/iproc-i2c0/iproc-i2c-dbg */
/* write to EEPROM I2c device (slave addr 80=0x50), addr offset: 0, vlaue: starting from 0, block xfer size:8, repeat_cnt:16 (EEPROM doesn't accept block xfer length > 8) */
/* repeat_cnt times of write block transfer */
/* for single address cycle I2C device only */
			for(j=0;j<repated_cnt;j++) {
 	 	 	 	for (i = 1; i <= burst_len; i++) {
                    i2cdata.block[i] = (val++) % 256; /* Fill a sequence pattern */
                    //printk("byte%d = 0x%02X\n", i, i2cdata.block[i]);
                }
                i2cdata.block[0] = burst_len;
                rc = iproc_smb_xfer(&dev->adapter, i2cdev_addr, 0x0, I2C_SMBUS_WRITE,
                                        addr+j*burst_len, I2C_SMBUS_BLOCK_DATA, &i2cdata);
				if (rc) {
                        printk (KERN_ERR "\n%s: iproc_smb_xfer:write failed status=%d,"
                                " addr=%u, val = 0x%X\n", __func__, rc, addr, val);
                        return -EFAULT;
				}
				msleep(1);
			}	
		} /* ! total_cnt == 1 */
    }

    if (rd_wr_op == 1) { /* Read operation */
		if (total_cnt == 1) {
#if defined(CONFIG_MACH_SB2)
            /* Testing EEPROM that requires 2-byte address: to support random read,
             * issue dummy write and then current address read
             */
            i2cdata.byte = addr;
            rc = iproc_smb_xfer(&dev->adapter, i2cdev_addr, 0x0,
                     I2C_SMBUS_WRITE, 0, I2C_SMBUS_BYTE_DATA, &i2cdata);
            if (rc) {
                printk (KERN_ERR "\n%s: iproc_smb_xfer dummy write failed status=%d\n", __func__, rc);
            }
            rc = iproc_smb_xfer(&dev->adapter, i2cdev_addr, 0x0, I2C_SMBUS_READ,
                               (unsigned char)0, I2C_SMBUS_BYTE, &i2cdata);
#else
            if (addr == -1) {
                /* Device does not support, or require an offset to read from the
                 * location
                 */
                rc = iproc_smb_xfer(&dev->adapter, i2cdev_addr, 0x0, I2C_SMBUS_READ,
                                    (unsigned char)0, I2C_SMBUS_BYTE, &i2cdata);
            }
            else {
                rc = iproc_smb_xfer(&dev->adapter, i2cdev_addr, 0x0, I2C_SMBUS_READ,
                                    addr, I2C_SMBUS_BYTE_DATA, &i2cdata);
            }
#endif
            if (rc) {
                printk (KERN_ERR "\n%s: iproc_smb_xfer failed status=%d\n", __func__, rc);
            
               /* return -EFAULT; */
            }
            else {
                printk(KERN_ERR "\nRead OK.\n--------Value read at %u = 0x%X\n", addr, i2cdata.byte);
            }
            
            msleep(1); /* Delay required, since smb(i2c) interface is slow */
        }
        else {
    		for (i = 1; i <= total_cnt; i++)
        		i2cdata.block[i] = 0;
    		i2cdata.block[0] = total_cnt;
			rc = iproc_smb_xfer(&dev->adapter, i2cdev_addr, 0x0, I2C_SMBUS_READ,
                            addr, I2C_SMBUS_BLOCK_DATA, &i2cdata);
			if (rc) {
            	printk (KERN_ERR "\n%s: iproc_smb_xfer:read failed status=%d,"
                    	" addr=%u, val = 0x%X\n", __func__, rc, addr, val);
				return -EFAULT;
			}
			msleep(1);
			for (i = 1; i <= total_cnt; i++)
        		printk("%d ", i2cdata.block[i]);	
    	}	/* ! total_cnt == 1 */    
    }

#ifdef IPROC_SMB_DBG
    iproc_dump_smb_regs(dev);
#endif

    printk(KERN_DEBUG "Last intr sts = 0x%08X\n", dev->smb_counters.last_int_sts);

    printk(KERN_DEBUG "mstr_start_busy_cnt = %u, mstr_rx_evt_cnt = %u, rx fifo full cnt = %u\n", 
           dev->smb_counters.mstr_start_busy_cnt,
           dev->smb_counters.mstr_rx_evt_cnt,
           dev->smb_counters.mstr_rx_fifo_full_cnt);

    return count;    
}   


#ifdef CONFIG_USE_SVK_VERSION
/* Written for SVK boards */
static int
proc_debug_write_svk(struct file *file, const char __user *buffer, size_t count, loff_t *off)
{
    struct iproc_smb_drv_int_data *dev = (struct iproc_smb_drv_int_data *) PDE_DATA (file->f_inode);
    int rc;
    unsigned int debug;
    unsigned char kbuf[MAX_PROC_BUF_SIZE];
    union i2c_smbus_data i2cdata;
    unsigned int val, addr;

    if (count > MAX_PROC_BUF_SIZE) {
        count = MAX_PROC_BUF_SIZE;
    }

    rc = copy_from_user(kbuf, buffer, count);
    if (rc) {
        printk(KERN_ERR "%s: copy_from_user failed status=%d\n", __func__, rc);
        return -EFAULT;
    }

    if (sscanf(kbuf, "%u", &debug) != 1) {
        printk(KERN_ERR "%s: echo <debug> > %s\n", __func__, PROC_ENTRY_DEBUG);
        return count;
    }

    if (debug) {
        dev->debug = 1;
    } else {
        dev->debug = 0;
    }

    printk ("Command can execute slow, please wait...\n");
    if (!dev->debug) {
        val = 0xFF; /* Initial value to write */
        for(addr = 0x0; addr < 256; val--, addr++) {
            i2cdata.byte = val;
            rc = iproc_smb_xfer(&dev->adapter, 0xA0 >> 1, 0x0, I2C_SMBUS_WRITE,
                                addr, I2C_SMBUS_BYTE_DATA, &i2cdata);
            if (rc) {
                printk(KERN_ERR "%s: iproc_smb_xfer:write failed status=%d,"
                        " addr=%u, val = 0x%X", __func__, rc, addr, val);
            } else {
                printk("Write OK. Wrote 0x%X at addr %u\n", val, addr);
            }
            msleep(1); /* Delay required, since smb(i2c) interface is slow */
        }
    } else {
        int i;

        /* Note about address expected by AT24C02: To write in correct order
         * to AT24C02 using block write, refer bottom of page 9 (Write
         * Operations) of the data sheet regarding internal incrementing of
         * address. Based on that explanation, we program the addr value below.
         * Select the 'highest' address in that page (7, 15, 23, and so on) to
         * write to that page
         */
        addr = debug - 1;
        val = jiffies % 256;
        printk("EEPROM page write. Page start addr = %u,"
               " write data: \n", debug - 8);

        for (i = 1; i <= 8; i++) {
            i2cdata.block[i] = val % 256; /* Fill a sequence pattern */
            val++;
            printk("byte%d = 0x%02X\n", i, i2cdata.block[i]);
        }

        i2cdata.block[0] = 8;
        rc = iproc_smb_xfer(&dev->adapter, 0xA0 >> 1, 0x0, I2C_SMBUS_WRITE,
                            addr, I2C_SMBUS_BLOCK_DATA, &i2cdata);
        if (rc) {
            printk(KERN_ERR "%s: iproc_smb_xfer:write failed status=%d,"
                    " addr=%u, val = 0x%X\n", __func__, rc, addr, val);
        } else {
            printk("Block Write OK.\n");
        }
    }

#ifdef IPROC_SMB_DBG
    iproc_dump_smb_regs(dev);
#endif /* IPROC_SMB_DBG */

    printk("Last intr sts = 0x%08X\n",
           dev->smb_counters.last_int_sts);
    printk("mstr_start_busy_cnt = %u, mstr_rx_evt_cnt = %u, rx fifo full cnt = %u\n\n",
           dev->smb_counters.mstr_start_busy_cnt,
           dev->smb_counters.mstr_rx_evt_cnt,
           dev->smb_counters.mstr_rx_fifo_full_cnt);

    return count;
}


/* Written for SVK boards */
static int
proc_debug_read_svk(struct file *file, char __user *buffer, size_t count, loff_t *off)
{
    unsigned int len = 0;
    struct iproc_smb_drv_int_data *dev = (struct iproc_smb_drv_int_data *) PDE_DATA (file->f_inode);
    int rc;
    union i2c_smbus_data i2cdata;
    unsigned int addr;
    
    if (off > 0) {
       return 0;
    }

    len += sprintf(buffer + len, "Read\n");
   
    printk(KERN_ERR "\nCommand can execute slow, please wait...\n");

    for(addr = 0x0; addr < 256; addr++) {

        /* Read operation */
        rc = iproc_smb_xfer(&dev->adapter, 0xA0 >> 1, 0x0, I2C_SMBUS_READ, addr,
                            I2C_SMBUS_BYTE_DATA, &i2cdata);

        if (rc) {
           printk (KERN_ERR "%s: iproc_smb_xfer failed status=%d", __func__, rc); 
        }
        else {
            printk(KERN_DEBUG "Read OK.Value read at %u = 0x%X\n", addr, i2cdata.byte);
        }

        msleep(1);
    }

#ifdef IPROC_SMB_DBG
    iproc_dump_smb_regs(dev);
#endif /* IPROC_SMB_DBG */

    printk(KERN_DEBUG "\n\nLast intr sts = 0x%08X", dev->smb_counters.last_int_sts);

    printk(KERN_DEBUG "mstr_start_busy_cnt = %u, mstr_rx_evt_cnt = %u, rx fifo full cnt = %u\n\n", 
           dev->smb_counters.mstr_start_busy_cnt,
           dev->smb_counters.mstr_rx_evt_cnt,
           dev->smb_counters.mstr_rx_fifo_full_cnt);

    return len;
}
#endif /* #ifdef CONFIG_USE_SVK_VERSION */

static const struct file_operations proc_smb_file_fops= {
#ifdef CONFIG_USE_SVK_VERSION
        .read           = proc_debug_read_svk,
        .write			= proc_debug_write_svk,
#else
		.read           = proc_debug_read,
        .write			= proc_debug_write,
#endif        
       
};



static int proc_init(struct platform_device *pdev)
{
   int rc;
   struct iproc_smb_drv_int_data *dev = platform_get_drvdata(pdev);
   struct procfs *proc = &dev->proc;
   struct proc_dir_entry *proc_debug;
   
   
   snprintf(proc->name, sizeof(proc->name), "%s%d", PROC_GLOBAL_PARENT_DIR, pdev->id);

   /* sub directory */
   proc->parent = proc_mkdir(proc->name, gProcParent);

   if (proc->parent == NULL) {
      return -ENOMEM;
   }

   proc_debug = proc_create_data(PROC_ENTRY_DEBUG, 0644, proc->parent, &proc_smb_file_fops, dev);

   if (proc_debug == NULL) {
      rc = -ENOMEM;
      goto err_del_parent;
   }
		
   return 0;

err_del_parent:
   remove_proc_entry(proc->name, gProcParent);

   return rc;
}

static int proc_term(struct platform_device *pdev)
{
   struct iproc_smb_drv_int_data *dev = platform_get_drvdata(pdev);
   struct procfs *proc = &dev->proc;

   remove_proc_entry(PROC_ENTRY_DEBUG, proc->parent);
   remove_proc_entry(proc->name, gProcParent);

   return 0;
}

/*
 * This function set clock frequency for SMBus block. As per hardware
 * engineering, the clock frequency can be changed dynamically.
 */
static int iproc_smb_set_clk_freq(unsigned long base_addr,
                                     smb_clk_freq_t freq)
{
    unsigned int regval;
    unsigned int val;

    switch (freq) {

        case I2C_SPEED_100KHz:
            val = 0;
            break;

        case I2C_SPEED_400KHz:
            val = 1;
            break;

        default:
            return -EINVAL;
            break;

    }
    
    regval = iproc_smb_reg_read(base_addr + CCB_SMB_TIMGCFG_REG);

    SETREGFLDVAL(regval, val, CCB_SMB_TIMGCFG_MODE400_MASK, 
                 CCB_SMB_TIMGCFG_MODE400_SHIFT);

    iproc_smb_reg_write(base_addr + CCB_SMB_TIMGCFG_REG, regval);

    return(0);
}

static int iproc_smbus_block_init(struct iproc_smb_drv_int_data *dev)
{

    unsigned long base_addr = (unsigned long)dev->block_base_addr;
    unsigned int regval;
#ifdef CONFIG_OF
	u32 i2c_clk_freq;
	struct device_node *dn = dev->dev->of_node;
#endif          

    /* Flush Tx, Rx FIFOs. Note we are setting the Rx FIFO threshold to 0.
     * May be OK since we are setting RX_EVENT and RX_FIFO_FULL interrupts
     */
    regval = CCB_SMB_MSTRRXFIFOFLSH_MASK | CCB_SMB_MSTRTXFIFOFLSH_MASK;

    iproc_smb_reg_write(base_addr + CCB_SMB_MSTRFIFOCTL_REG, regval);

    /* Enable SMbus block. Note, we are setting MASTER_RETRY_COUNT to zero
     * since there will be only one master
     */
    regval = CCB_SMB_CFG_SMBEN_MASK;

    iproc_smb_reg_write(base_addr + CCB_SMB_CFG_REG, regval);

    /* Wait a minimum of 50 Usec, as per SMB hw doc. But we wait longer */
    udelay(100);


    /* Set default clock frequency */
#ifndef CONFIG_OF   
    iproc_smb_set_clk_freq(base_addr, I2C_SPEED_100KHz);
#else
    if (of_property_read_u32(dn, "clock-frequency", &i2c_clk_freq)) {
          i2c_clk_freq = I2C_SPEED_100KHz; /*no property available, use default: 100KHz*/
    }
    iproc_smb_set_clk_freq(base_addr, i2c_clk_freq);            
#endif /* CONFIG_OF */
    /* Disable intrs */
    regval = 0x0;
    iproc_smb_reg_write(base_addr + CCB_SMB_EVTEN_REG, regval);

    /* Clear intrs (W1TC) */
    regval = iproc_smb_reg_read(base_addr + CCB_SMB_EVTSTS_REG);
    
    iproc_smb_reg_write(base_addr + CCB_SMB_EVTSTS_REG, regval);

    return(0);
}

/* This function enables interrupts */
static int iproc_intr_enable(struct iproc_smb_drv_int_data *dev, unsigned int bmap)
{
    unsigned long base_addr = (unsigned long)dev->block_base_addr;
    unsigned int regval;

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_EVTEN_REG);

    regval |= bmap;

    iproc_smb_reg_write(base_addr + CCB_SMB_EVTEN_REG, regval);

    /* Store all interrupts enabled so far. Note bmap can have only 'incremental'
     * set of events
     */ 
    dev->evt_enable_bmap = regval;

    return(0);
}

/* This function disables interrupts */
static int iproc_intr_disable(struct iproc_smb_drv_int_data *dev, unsigned int bmap)
{
    unsigned long base_addr = (unsigned long)dev->block_base_addr;
    unsigned int regval;

    regval = iproc_smb_reg_read(base_addr + CCB_SMB_EVTEN_REG);

    regval &= ~bmap;

    iproc_smb_reg_write(base_addr + CCB_SMB_EVTEN_REG, regval);

    dev->evt_enable_bmap = regval;

    return(0);
}

/* Verify this sequence with hw engg */
static int iproc_smbus_block_deinit(struct iproc_smb_drv_int_data *dev)
{
    unsigned int regval;
    int rc;

    /* Disable all interrupts */
    regval = 0x0;

    iproc_smb_reg_write((unsigned long)dev->block_base_addr + CCB_SMB_EVTEN_REG, regval);

    /* Check if a transaction is in progress */
    rc = iproc_smb_startbusy_wait(dev);

    if (rc < 0) {

        /* Do not exit the function, since we are most likely shutting down */
        printk(KERN_ERR "%s: A transaction is still in progress,"
                         "but still continuing ", __func__);

    }

    /* Disable SMBus block */
    regval = iproc_smb_reg_read((unsigned long)dev->block_base_addr + CCB_SMB_CFG_REG);

    regval &= ~CCB_SMB_CFG_SMBEN_MASK;

    iproc_smb_reg_write((unsigned long)dev->block_base_addr + CCB_SMB_CFG_REG, regval);


    /* Wait for some time */
    udelay(100);

    /* Put the block under reset. Note the RESET bit in reg 0x0 is 
     * self clearing
     */
    regval = CCB_SMB_CFG_RST_MASK;

    iproc_smb_reg_write((unsigned long)dev->block_base_addr + CCB_SMB_CFG_REG, regval);

    return(0);
}

static u32 iproc_smb_funcs(struct i2c_adapter *adapter)
{
	/* I2C_FUNC_SMBUS_I2C_BLOCK */
    return (I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA |
            I2C_FUNC_SMBUS_WORD_DATA |I2C_FUNC_SMBUS_BLOCK_DATA |
            I2C_FUNC_SMBUS_I2C_BLOCK);
}

static struct i2c_algorithm iproc_smb_algorithm = {
    /*    .name           = "iproc-smb", */
    .smbus_xfer     = iproc_smb_xfer,
    .master_xfer    = NULL,
    .functionality  = iproc_smb_funcs,
};


static int iproc_smb_probe(struct platform_device *pdev)
{
    int rc=0, irq;
    struct iproc_smb_drv_int_data *dev;
    struct i2c_adapter *adap;
    struct resource *iomem;
    struct resource *ioarea;
#ifdef CONFIG_OF
	struct device_node *dn = pdev->dev.of_node;
	u32 smb_bus_id;
#endif        

#ifdef IPROC_SMB_DBG
    printk(KERN_DEBUG "\n%s: Entering probe\n", __func__);
#endif /* IPROC_SMB_DBG */

#ifdef CONFIG_OF 
	/* first I2C init */
	if (gProcParent == NULL) {
		gProcParent = proc_mkdir(PROC_GLOBAL_PARENT_DIR, NULL);
		if (gProcParent == NULL) {
        	printk(KERN_ERR "%s: SMBus driver procfs failed\n", __func__);
        	return -ENOMEM;
        }
        iproc_smbus_list = NULL;
    }
#endif /* CONFIG_OF */

    /* Get register memory resource */
    iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

    if (!iomem) {
        printk(KERN_ERR "%s: No mem resource\n", __func__);
        return -ENODEV;
    }

#ifdef IPROC_SMB_DBG
    printk(KERN_DEBUG "\nGot iomem 0x%p\n", iomem);
#endif /* IPROC_SMB_DBG */

    /* Get the interrupt number */
    irq = platform_get_irq(pdev, 0);

    if (irq == -ENXIO) {
        printk(KERN_ERR "%s: No irq resource\n", __func__);
        return -ENODEV;
    }

#ifdef IPROC_SMB_DBG
    printk(KERN_DEBUG "\nGot irqnum %d\n", irq);
#endif /* IPROC_SMB_DBG */

    /* Mark the memory region as used */
    ioarea = request_mem_region(iomem->start, resource_size(iomem),
                                pdev->name);
    if (!ioarea) {
        printk(KERN_ERR "%s: SMBus region already claimed\n", __func__);
        return -EBUSY;
    }

#ifdef IPROC_SMB_DBG
    printk(KERN_DEBUG "\nGot ioarea 0x%p\n", ioarea);
#endif /* IPROC_SMB_DBG */

    /* Allocate memory for driver's internal data structure */
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);

    if (!dev) {
        printk(KERN_ERR "%s: Couldn't allocate memory for driver's internaldb\n", __func__);
        rc = -ENOMEM;
        goto err_release_mem_region;
    }

#ifdef IPROC_SMB_DBG
    printk(KERN_DEBUG "\nGot dev 0x%p\n", dev);
#endif /* IPROC_SMB_DBG */

    dev->dev = &pdev->dev;
    init_MUTEX(&dev->xfer_lock);
    init_completion(&dev->ses_done);
    dev->irq = irq;

    dev->block_base_addr = ioremap(iomem->start, resource_size(iomem));

    if (!dev->block_base_addr) {
        printk(KERN_ERR "%s: ioremap of register space failed\n", __func__);
        rc = -ENOMEM;
        goto err_free_dev_mem;
    }

#ifdef IPROC_SMB_DBG
    printk(KERN_DEBUG "\n ==== Got block_base_addr=0x%08X\n", (unsigned int)dev->block_base_addr);
    /* iproc_dump_smb_regs(dev); */
#endif /* IPROC_SMB_DBG */

    dev->enable_evts = ENABLE_INTR; /* Default value, can be changed after
                                       initial testing */

    platform_set_drvdata(pdev, dev);

    /* Init internal regs, disable intrs (and then clear intrs), set fifo
     * thresholds, etc.
     */
    iproc_smbus_block_init(dev);

    /* Register ISR handler */
    rc = request_irq(dev->irq, iproc_smb_isr, IRQF_SHARED, pdev->name, dev);

    if (rc) {
        printk(KERN_ERR "%s: failed to request irq %d, rc=%d\n", __func__, dev->irq, rc);
        goto err_smb_deinit;
    }

#ifdef IPROC_SMB_DBG
    printk(KERN_DEBUG "\nrequest_irq succeeded\n");
#endif /* IPROC_SMB_DBG */

    adap = &dev->adapter;
    i2c_set_adapdata(adap, dev); /* Verify if this place is OK */
    adap->owner = THIS_MODULE;
    adap->class = UINT_MAX; /* Can be used by any I2C device */
    adap->algo = &iproc_smb_algorithm;
    adap->dev.parent = &pdev->dev; /* */
#ifndef CONFIG_OF
    adap->nr = pdev->id;
#else
	if (of_property_read_u32(dn, "#bus-id", &smb_bus_id)) {
    	dev_warn(&pdev->dev, "missing #bus-id property (default to 0)\n");
        smb_bus_id = 0;
	}
    adap->nr = smb_bus_id;
    pdev->id = smb_bus_id;
    adap->dev.of_node = pdev->dev.of_node; /* needed for adding I2C child devices */
#endif /* CONFIG_OF */
	snprintf(adap->name, sizeof(adap->name), "iproc-smb%d", pdev->id);  
    
    /*
    * I2C device drivers may be active on return from
    * i2c_add_numbered_adapter()
    */
    rc = i2c_add_numbered_adapter(adap);

    if (rc) {
        printk(KERN_ERR "%s: Failed to add I2C adapter, rc=%d\n", __func__, rc);
        goto err_free_irq;
    }

#ifdef IPROC_SMB_DBG
    printk(KERN_DEBUG "\ni2c_add_numbered_adapter succeeded\n");
#endif /* IPROC_SMB_DBG */

    /* Turn on default set of interrupts */
    /* For Rx, enable RX fifo full, threshold hit interrupts. Other rx 
     * interrupts will be set in the read/recv transactions, as required
     * For Tx, enable fifo under run intr. Other intrs will be set in send
     * write access functions 
     */
    iproc_intr_enable(dev, CCB_SMB_MSTRRXFIFOFULLEN_MASK);

#ifdef IPROC_SMB_DBG
    printk(KERN_DEBUG "\niproc_intr_enable complete, intrs enabled\n");
#endif /* IPROC_SMB_DBG */

    rc = proc_init(pdev);

    if (rc) {
        printk(KERN_ERR "%s: Failed to install procfs entry, rc=%d\n", __func__, rc);
        goto err_proc_term;
    }

    dev->next = iproc_smbus_list;
    iproc_smbus_list = dev;

#ifdef IPROC_SMB_DBG
    iproc_dump_smb_regs(dev);
    printk(KERN_DEBUG "%s: probe successful", __func__);
#endif /* IPROC_SMB_DBG */

    return 0;

err_proc_term:
    proc_term(pdev);

err_free_irq:
    free_irq(dev->irq, dev);

err_smb_deinit:
    iproc_smbus_block_deinit(dev);

    iounmap(dev->block_base_addr);

    platform_set_drvdata(pdev, NULL);

err_free_dev_mem:
    kfree(dev);

err_release_mem_region:
    release_mem_region(iomem->start, resource_size(iomem));

    printk(KERN_ERR "%s: probe failed, error=%d", __func__, rc);

    return (rc);
}

static int iproc_smb_remove(struct platform_device *pdev)
{
    struct iproc_smb_drv_int_data *dev = platform_get_drvdata(pdev);
    struct resource *iomem;
    unsigned int regval;

    /* Disable interrupts. */
    /* Verify: Should we wait for any in-progress xact to complete? */
    iproc_intr_disable(dev, ~0);

    /* Disable SMbus block */
    regval = iproc_smb_reg_read((unsigned long)dev->block_base_addr + CCB_SMB_CFG_REG);

    regval &= ~CCB_SMB_CFG_SMBEN_MASK;

    iproc_smb_reg_write((unsigned long)dev->block_base_addr + CCB_SMB_CFG_REG, regval);

    i2c_del_adapter(&dev->adapter);

    platform_set_drvdata(pdev, NULL);

    free_irq(dev->irq, dev);

    iproc_smbus_block_deinit(dev);

    iounmap(dev->block_base_addr);

    kfree(dev);

    iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

    release_mem_region(iomem->start, resource_size(iomem));

   return 0;
}

#ifndef CONFIG_OF
static int iproc_smb_suspend(struct platform_device *pdev, pm_message_t state)
{
/*   struct iproc_smb_drv_int_data *dev = platform_get_drvdata(pdev); */

   /* Add additional processing, if required */

   return (0);
}

static int iproc_smb_resume(struct platform_device *pdev)
{
/*   struct iproc_smb_drv_int_data *dev = platform_get_drvdata(pdev); */

   /* Add additional processing, if required */

   return (0);
}

static struct platform_driver iproc_smb_driver = {
   .driver = {
      .name = "iproc-smb",
      .owner = THIS_MODULE,
   },
   .probe   = iproc_smb_probe,
   .remove  = iproc_smb_remove,
   .suspend = iproc_smb_suspend,
   .resume  = iproc_smb_resume,
};

static int __init iproc_smb_init(void)
{
   int rc;

#ifdef IPROC_SMB_DBG
    printk(KERN_DEBUG "%s: Entering init", __func__);
#endif /* IPROC_SMB_DBG */

   gProcParent = proc_mkdir(PROC_GLOBAL_PARENT_DIR, NULL);

   if (gProcParent == NULL) {

      printk(KERN_ERR "%s: SMBus driver procfs failed\n", __func__);

      return -ENOMEM;

   }

#ifdef IPROC_SMB_DBG
    printk(KERN_DEBUG "\nproc_mkdir succeeded, gProcParent=0x%08X\n", (unsigned int)gProcParent);
#endif /* IPROC_SMB_DBG */

   rc = platform_driver_register(&iproc_smb_driver);

   if (rc < 0) {

      printk(KERN_ERR "%s: SMBus driver init failed, error %d\n", __func__, rc);

   }

#ifdef IPROC_SMB_DBG
    printk(KERN_DEBUG "\n%s: Called platform_driver_register, rc=%d\n", __func__, rc);
#endif /* IPROC_SMB_DBG */


    iproc_smbus_list = NULL;

   /* Should we set RESET bit (reg 0x0) here?: Not necessary as per hw engg */

   return rc;
}

static void __exit iproc_smb_exit(void)
{
   platform_driver_unregister(&iproc_smb_driver);

   remove_proc_entry(PROC_GLOBAL_PARENT_DIR, NULL);
}

module_init(iproc_smb_init);
module_exit(iproc_smb_exit);

#else /* CONFIG_OF */

static const struct of_device_id bcm_iproc_i2c_of_match[] = {
    { .compatible = "brcm,iproc-i2c" },
    { }
};
MODULE_DEVICE_TABLE(of, bcm_iproc_i2c_of_match);

static struct platform_driver bcm_iproc_i2c_driver = {
    .driver = {
    		.name = "bcm-iproc-i2c",
        	.of_match_table = bcm_iproc_i2c_of_match,
           },
    .probe = iproc_smb_probe,
    .remove = iproc_smb_remove,
};
module_platform_driver(bcm_iproc_i2c_driver);
#endif /* !CONFIG_OF */

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("IPROC I2C (SMBus) Bus Driver");
MODULE_LICENSE("GPL");
