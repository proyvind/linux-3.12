#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/ath9k_platform.h>

#include <asm/delay.h>

#define ag7100_delay1s()    mdelay(1000);

#include "ar7100.h"


static int ar71xx_pci_fixup_enable;

/*
 * Support for Ar7100 pci interrupt and core pci initialization
 */
/*
 * PCI interrupts.
 * roughly the interrupts flow is:
 *
 * - save flags
 * - CLI (disable all)
 * - IC->ack (mask out the source)
 * - EI (enable all, except the source that was masked of course)
 * - action (ISR)
 * - IC->enable (unmask the source)
 *
 * The reason we have a separate PCI IC is beacause of the following:
 * If we dont, then Throughout the "action" of a PCI slot, the
 * entire PCI "IP" on the cpu will remain disabled. Which means that we cant
 * prioritize between PCI interrupts. Normally this should be ok, if all PCI 
 * interrupts are considered equal. However, creating a PCI IC gives 
 * the flexibility to prioritize.
 */

static void
ar7100_pci_irq_enable(struct irq_data *irq)
{
    ar7100_reg_rmw_set(AR7100_PCI_INT_MASK, 
                       (1 << (irq->irq - AR7100_PCI_IRQ_BASE)));
}

static void
ar7100_pci_irq_disable(struct irq_data *irq)
{
    ar7100_reg_rmw_clear(AR7100_PCI_INT_MASK, 
                       (1 << (irq->irq - AR7100_PCI_IRQ_BASE)));
}

static unsigned int
ar7100_pci_irq_startup(struct irq_data *irq)
{
	ar7100_pci_irq_enable(irq);
	return 0;
}

static void
ar7100_pci_irq_shutdown(struct irq_data *irq)
{
	ar7100_pci_irq_disable(irq);
}

static void
ar7100_pci_irq_ack(struct irq_data *irq)
{
	ar7100_pci_irq_disable(irq);
}

static void
ar7100_pci_irq_end(struct irq_data *irq)
{
	ar7100_pci_irq_enable(irq);
}

static int
ar7100_pci_irq_set_affinity(unsigned int irq, const struct cpumask *mask)
{
	/* 
     * Only 1 CPU; ignore affinity request 
     */
     return 0;
}

static struct irq_chip ar7100_pci_irq_chip = {
	.name		= "AR7100 PCI ",
	.irq_mask	= ar7100_pci_irq_disable,
	.irq_unmask	= ar7100_pci_irq_enable,
	.irq_mask_ack	= ar7100_pci_irq_disable,
};


void
ar7100_pci_irq_init(int irq_base)
{
	int i;

	for (i = irq_base; i < irq_base + AR7100_PCI_IRQ_COUNT; i++) {
		irq_set_chip_and_handler(i, &ar7100_pci_irq_chip,
					 handle_level_irq);
	}
}

/*
 * init the pci controller
 */

static struct resource ar7100_io_resource = {
	.name		= "PCI IO space",
	.start		= 0x0000,
	.end		= 0,
	.flags		= IORESOURCE_IO,
};

static struct resource ar7100_mem_resource = {
	.name		= "PCI memory space",
	.start		= AR7100_PCI_MEM_BASE,
	.end		= AR7100_PCI_MEM_BASE + AR7100_PCI_WINDOW - 1,
	.flags		= IORESOURCE_MEM
};

extern struct pci_ops ar7100_pci_ops;

static struct pci_controller ar7100_pci_controller = {
	.pci_ops	    = &ar7100_pci_ops,
	.mem_resource	= &ar7100_mem_resource,
	.io_resource	= &ar7100_io_resource,
};


irqreturn_t 
ar7100_pci_core_intr(int cpl, void *dev_id)
{
	printk("PCI error intr\n");
	ar7100_check_error(1);

	return IRQ_HANDLED;
}

/*
 * We want a 1:1 mapping between PCI and DDR for inbound and outbound.
 * The PCI<---AHB decoding works as follows:
 *
 * 8 registers in the DDR unit provide software configurable 32 bit offsets
 * for each of the eight 16MB PCI windows in the 128MB. The offsets will be 
 * added to any address in the 16MB segment before being sent to the PCI unit.
 *
 * Essentially  for any AHB address generated by the CPU,
 * 1. the MSB  four bits are stripped off, [31:28],
 * 2. Bit 27 is used to decide between the lower 128Mb (PCI) or the rest of 
 *    the AHB space
 * 3. Bits 26:24 are used to access one of the 8 window registers and are 
 *    masked off.
 * 4. If it is a PCI address, then the WINDOW offset in the WINDOW register 
 *    corresponding to the next 3 bits (bit 26:24) is ADDED to the address, 
 *    to generate the address to PCI unit.
 *
 *     eg. CPU address = 0x100000ff
 *         window 0 offset = 0x10000000
 *         This points to lowermost 16MB window in PCI space.
 *         So the resulting address would be 0x000000ff+0x10000000
 *         = 0x100000ff
 *
 *         eg2. CPU address = 0x120000ff
 *         WINDOW 2 offset = 0x12000000
 *         resulting address would be 0x000000ff+0x12000000
 *                         = 0x120000ff 
 *
 * There is no translation for inbound access (PCI device as a master)
 */
static void ar71xx_pci_fixup(struct pci_dev *dev)
{
	u32 t;

	if (!ar71xx_pci_fixup_enable)
		return;
		

	if (dev->bus->number != 0 || dev->devfn != 0)
		return;


	/* setup COMMAND register */
	t = PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE
	  | PCI_COMMAND_PARITY | PCI_COMMAND_SERR | PCI_COMMAND_FAST_BACK;

	pci_write_config_word(dev, PCI_COMMAND, t);
}
DECLARE_PCI_FIXUP_EARLY(PCI_ANY_ID, PCI_ANY_ID, ar71xx_pci_fixup);

#ifdef CONFIG_DIR825
#define STARTSCAN 0x1f660000
#define DIR825B1_MAC_LOCATION_0			0x1f66ffa0
#define DIR825B1_MAC_LOCATION_1			0x1f66ffb4
static u8 mac0[6];
static u8 mac1[6];

static void dir825b1_read_ascii_mac(u8 *dest, unsigned int src_addr)
{
	int ret;
	u8 *src = (u8 *)KSEG1ADDR(src_addr);

	ret = sscanf(src, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		     &dest[0], &dest[1], &dest[2],
		     &dest[3], &dest[4], &dest[5]);

	if (ret != 6)
		memset(dest, 0, 6);
}


#else  
#define STARTSCAN 0x1f000000
#endif  

#ifdef CONFIG_WNDR3700
static u8 mac0[6];
static u8 mac1[6];
#define WNDR3700_MAC_LOCATION_0			0x1fff0000
#define WNDR3700_MAC_LOCATION_1			0x1fff000c
#endif 
static void *getCalData(int slot)
{
u8 *base;
for (base=(u8 *) KSEG1ADDR(STARTSCAN);base<KSEG1ADDR (0x1ffff000);base+=0x1000) {
    u32 *cal = (u32 *)base;
    if (*cal==0xa55a0000 || *cal==0x5aa50000) { //protection bit is always zero on inflash devices, so we can use for match it
	if (slot) {
	    base+=0x4000;
	    }
	printk(KERN_INFO "found calibration data for slot %d on 0x%08X\n",slot,base);
	return base;
	}
    }
return NULL;
}
static struct ath9k_platform_data wmac_data[2];


static void ath_pci_fixup(struct pci_dev *dev)
{
	void __iomem *mem;
	u16 *cal_data = NULL;
	u16 cmd;
	u32 bar0;
	u32 val;

	if (!ar71xx_pci_fixup_enable)
		return;

	switch (PCI_SLOT(dev->devfn)) {
	case 0:
		cal_data = (u16 *)getCalData(0);
		if (cal_data) {
		memcpy(wmac_data[0].eeprom_data,cal_data,sizeof(wmac_data[0].eeprom_data));
		#ifdef CONFIG_DIR825
		dir825b1_read_ascii_mac(mac0, DIR825B1_MAC_LOCATION_0);
		wmac_data[0].macaddr = mac0;
		#endif
		#ifdef CONFIG_WNDR3700
		memcpy(mac0,KSEG1ADDR(WNDR3700_MAC_LOCATION_0),6);
		wmac_data[0].macaddr = mac0;
		/* 2.4 GHz uses the first fixed antenna group (1, 0, 1, 0) */
		wmac_data[0].gpio_mask = (0xf << 6);
		wmac_data[0].gpio_val = (0xa << 6);
		#endif
		dev->dev.platform_data = &wmac_data[0];
		}
		break;
	case 1:
		cal_data = (u16 *)getCalData(1);
		if (cal_data) {
		memcpy(wmac_data[1].eeprom_data,cal_data,sizeof(wmac_data[1].eeprom_data));
		#ifdef CONFIG_DIR825
		dir825b1_read_ascii_mac(mac1, DIR825B1_MAC_LOCATION_1);
		wmac_data[1].macaddr = mac1;
		#endif
		#ifdef CONFIG_WNDR3700
		memcpy(mac1,KSEG1ADDR(WNDR3700_MAC_LOCATION_1),6);
		wmac_data[1].macaddr = mac1;
		/* 5 GHz uses the second fixed antenna group (0, 1, 1, 0) */
		wmac_data[1].gpio_mask = (0xf << 6);
		wmac_data[1].gpio_val = (0x6 << 6);
		#endif
		dev->dev.platform_data = &wmac_data[1];
		}
		break;
	default:
		return;
	}
	if (!cal_data) {
		printk(KERN_INFO "no in flash calibration fata found, no fix required\n");
		return;
	}
	
	mem = ioremap(AR71XX_PCI_MEM_BASE, 0x10000);
	if (!mem) {
		printk(KERN_ERR "PCI: ioremap error for device %s\n",
		       pci_name(dev));
		return;
	}

	printk(KERN_INFO "PCI: fixup device %s\n", pci_name(dev));

	pci_read_config_dword(dev, PCI_BASE_ADDRESS_0, &bar0);

	/* Setup the PCI device to allow access to the internal registers */
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0, AR71XX_PCI_MEM_BASE);
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY;
	pci_write_config_word(dev, PCI_COMMAND, cmd);

	/* set pointer to first reg address */
	cal_data += 3;
	while (*cal_data != 0xffff) {
		u32 reg;
		reg = *cal_data++;
		val = *cal_data++;
		val |= (*cal_data++) << 16;

		__raw_writel(val, mem + reg);
		udelay(100);
	}

	pci_read_config_dword(dev, PCI_VENDOR_ID, &val);
	dev->vendor = val & 0xffff;
	dev->device = (val >> 16) & 0xffff;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &val);
	dev->revision = val & 0xff;
	dev->class = val >> 8; /* upper 3 bytes */

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	cmd &= ~(PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY);
	pci_write_config_word(dev, PCI_COMMAND, cmd);

	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0, bar0);

	iounmap(mem);
	return;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_ATHEROS, PCI_ANY_ID, ath_pci_fixup);
  
static int __init ar7100_pcibios_init(void)
{
	uint32_t cmd;

	ar7100_reg_rmw_set(AR7100_RESET, 
			(AR7100_RESET_PCI_BUS|AR7100_RESET_PCI_CORE));
	ag7100_delay1s();

	ar7100_reg_rmw_clear(AR7100_RESET, 
			(AR7100_RESET_PCI_BUS|AR7100_RESET_PCI_CORE));
	ag7100_delay1s();

	ar7100_write_pci_window(0);
	ar7100_write_pci_window(1);
	ar7100_write_pci_window(2);
	ar7100_write_pci_window(3);
	ar7100_write_pci_window(4);
	ar7100_write_pci_window(5);
	ar7100_write_pci_window(6);
	ar7100_write_pci_window(7);

	ag7100_delay1s();


	cmd = PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE |
		PCI_COMMAND_PARITY|PCI_COMMAND_SERR|PCI_COMMAND_FAST_BACK;

	ar7100_local_write_config(PCI_COMMAND, 4, cmd);

	/*
	 * clear any lingering errors and register core error IRQ
	 */
	ar7100_check_error(0);

	ar71xx_pci_fixup_enable = 1;
	register_pci_controller(&ar7100_pci_controller);
	request_irq(AR7100_PCI_IRQ_CORE, ar7100_pci_core_intr, IRQF_DISABLED,"ar7100 pci core", NULL);

	return 0;
}

arch_initcall(ar7100_pcibios_init);