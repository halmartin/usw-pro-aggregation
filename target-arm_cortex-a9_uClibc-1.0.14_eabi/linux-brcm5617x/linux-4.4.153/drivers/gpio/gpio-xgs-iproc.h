/*
 * $Copyright Open Broadcom Corporation$
 */
 
#ifndef __IPROC_PLAT_GPIO_H
#define __IPROC_PLAT_GPIO_H

#if defined(CONFIG_MACH_IPROC_P7)
#define IPROC_GPIO_CCG
#else
#define IPROC_GPIO_CCA
#define IPROC_GPIO_CCB
#endif

#define IPROC_GPIO_REG_SIZE     (0x50)

#define REGOFFSET_GPIO_DIN          0x000 /* GPIO Data in register */
#define REGOFFSET_GPIO_DOUT         0x004 /* GPIO Data out register */
#define REGOFFSET_GPIO_EN           0x008 /* GPIO output enable register */

#define IPROC_GPIO_CCA_ID   (0)
#define IPROC_GPIO_CCB_ID   (1)
#define IPROC_GPIO_CCG_ID   (2)

struct iproc_gpio_irqcfg {
	unsigned long flags;
	irqreturn_t (*handler)(int irq, void *dev);
	void (*ack)(unsigned int irq);
	void (*unmask)(unsigned int irq);
	void (*mask)(unsigned int irq);
	int (*set_type)(unsigned int irq, unsigned int type);
};

struct iproc_gpio_chip {
	int id;
	struct gpio_chip		chip;
	struct iproc_gpio_cfg	*config;
	void __iomem		*ioaddr;
	void __iomem		*intr_ioaddr;
	spinlock_t			lock;
	struct irq_domain *irq_domain;
	struct resource	* resource;
	int irq;
	struct iproc_gpio_irqcfg	*irqcfg;
	int pin_offset;
};


static inline struct iproc_gpio_chip *to_iproc_gpio(struct gpio_chip *gpc)
{
	return container_of(gpc, struct iproc_gpio_chip, chip);
}


/* locking wrappers to deal with multiple access to the same gpio bank */
#define iproc_gpio_lock(_oc, _fl) spin_lock_irqsave(&(_oc)->lock, _fl)
#define iproc_gpio_unlock(_oc, _fl) spin_unlock_irqrestore(&(_oc)->lock, _fl)

extern void iproc_gpiolib_add(struct iproc_gpio_chip *chip);

#endif
