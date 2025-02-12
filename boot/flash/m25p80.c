/*
 * MTD SPI driver for ST M25Pxx (and similar) serial flash chips
 *
 * Author: Mike Lavender, mike@steroidmicros.com
 *
 * Copyright (c) 2005, Intec Automation Inc.
 *
 * Some parts are based on lart.c by Abraham Van Der Merwe
 *
 * Cleaned up and generalized based on mtd_dataflash.c
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

//#include <linux/init.h>
//#include <linux/err.h>
#include <linux/errno.h>
//#include <linux/module.h>
//#include <linux/device.h>
//#include <linux/interrupt.h>
//#include <linux/mutex.h>
//#include <linux/math64.h>
//#include <linux/slab.h>
//#include <linux/sched.h>
//#include <linux/mod_devicetable.h>
//#include <linux/list.h>

#include <linux/mtd/cfi.h>
#include <linux/mtd/mtd.h>
//#include <linux/mtd/partitions.h>
//#include <linux/of_platform.h>
#include <string.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>

#include <asm/rtl8196x.h> 

//---------------------------------------------------------------------------------------------
static inline void *ERR_PTR(long error)
{
	return (void *) error;
}

static inline long PTR_ERR(const void *ptr)
{
	return (long) ptr;
}

static inline long IS_ERR(const void *ptr)
{
	return (unsigned long)ptr > (unsigned long)-1000L;
}

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define KERN_INFO
#define printk	prom_printf
//#define printf	prom_printf
//#define pr_debug prom_printf
#define pr_debug(format, arg...)  do {} while(0)

#define dev_err(dev, format, arg...)		\
	prom_printf(format , ## arg)

#define dev_warn(dev, format, arg...)		\
	prom_printf(format , ## arg)

#define dev_info(dev, format, arg...)		\
	prom_printf(format , ## arg)
	
#define mutex_init(x) do {} while(0)
#define mutex_lock(x) do {} while(0)
#define mutex_unlock(x) do {} while(0)

#define HZ 100
extern volatile unsigned int jiffies;

#define time_after_eq(a,b)	((long)(a) - (long)(b) >= 0)

extern struct spi_master g_spi_master;

struct m25p g_m25p;
struct spi_device g_spi_device = {
		.modalias	= "m25p80",
		.max_speed_hz	= 15000000,
//		.bus_num	= 0,
		.chip_select	= 0,
		.mode		= SPI_CPHA | SPI_CPOL,
//		.platform_data	= &sheipa_mtd_data,
		.bits_per_word = 8,
		.master = &g_spi_master,
};

u32 do_div(u32 n, u32 base)
 {
 	u32 remainder = n % base;
 	n = n / base;
 	return remainder;
 }
 
u64 div_u64_rem(u32 dividend, u32 divisor, u32 *remainder)
{
	*remainder = do_div(dividend, divisor);
	return dividend;
}

//---------------------------------------------------------------------------------------------

#define MTD_WRITEABLE		0x400	/* Device is writeable */
#define MTD_BIT_WRITEABLE	0x800	/* Single bits can be flipped */
#define MTD_NO_ERASE		0x1000	/* No erase necessary */
#define MTD_POWERUP_LOCK	0x2000	/* Always locked after reset */


/* Flash opcodes. */
#define	OPCODE_WREN		0x06	/* Write enable */
#define	OPCODE_RDSR		0x05	/* Read status register */
#define	OPCODE_WRSR		0x01	/* Write status register 1 byte */
#define	OPCODE_NORM_READ	0x03	/* Read data bytes (low frequency) */
#define	OPCODE_FAST_READ	0x0b	/* Read data bytes (high frequency) */
#define	OPCODE_QUAD_READ        0x6b    /* Read data bytes */
#define	OPCODE_PP		0x02	/* Page program (up to 256 bytes) */
#define	OPCODE_BE_4K		0x20	/* Erase 4KiB block */
#define	OPCODE_BE_4K_PMC	0xd7	/* Erase 4KiB block on PMC chips */
#define	OPCODE_BE_32K		0x52	/* Erase 32KiB block */
#define	OPCODE_CHIP_ERASE	0xc7	/* Erase whole flash chip */
#define	OPCODE_SE		0xd8	/* Sector erase (usually 64KiB) */
#define	OPCODE_RDID		0x9f	/* Read JEDEC ID */
#define	OPCODE_RDCR             0x35    /* Read configuration register */

/* 4-byte address opcodes - used on Spansion and some Macronix flashes. */
#define	OPCODE_NORM_READ_4B	0x13	/* Read data bytes (low frequency) */
#define	OPCODE_FAST_READ_4B	0x0c	/* Read data bytes (high frequency) */
#define	OPCODE_QUAD_READ_4B	0x6c    /* Read data bytes */
#define	OPCODE_PP_4B		0x12	/* Page program (up to 256 bytes) */
#define	OPCODE_SE_4B		0xdc	/* Sector erase (usually 64KiB) */

/* Support auto mode for sheipa only. */
#define	OPCODE_AUTO		0xf5

/* Extend flash opcode and used for Macronix */
#define PPX2_I		0x02		/* 1|2O dual program */
#define PPX2_II		0x02		/* 2 x I/O dual program */
#define PPX4_I		0x02		/* 1|4O quad program */
#define PPX4_II		0x38		/* 4 x I/O quad program */
#define READX2_I	0x03		/* 1|2O dual dual read*/
#define READX2_IO	0xbb		/* 2 x I/O dual read */
#define READX4_I	0x03		/* 1|4O quad read */
#define READX4_IO	0xeb		/* 4 x I/O quad read */

/* Extend flash read dummy cycles and used for Macronix only */
#define MXIC_DUAL_DUMMY_CYCLE    0x4
#define MXIC_QUAD_DUMMY_CYCLE    0x6
#define MXIC_FAST_DUMMY_CYCLE    0x8

/* ESMT */
#define ESMT_DUAL_DUMMY_CYCLE    0x4
#define ESMT_QUAD_DUMMY_CYCLE    0x6
#define ESMT_FAST_DUMMY_CYCLE    0x8

/* Winbond */
#define WINBOND_DUAL_DUMMY_CYCLE    0x4
#define WINBOND_QUAD_DUMMY_CYCLE    0x6
#define WINBOND_FAST_DUMMY_CYCLE    0x8

/* Spansion */
#define SPANSION_DUAL_DUMMY_CYCLE    0x4
#define SPANSION_QUAD_DUMMY_CYCLE    0x6
#define SPANSION_FAST_DUMMY_CYCLE    0x8

/* GigaDevice */
#define GIGADEVICE_DUAL_DUMMY_CYCLE    0x4
#define GIGADEVICE_QUAD_DUMMY_CYCLE    0x6
#define GIGADEVICE_FAST_DUMMY_CYCLE    0x8

/* EON */
#define EON_DUAL_DUMMY_CYCLE    0x4
#define EON_QUAD_DUMMY_CYCLE    0x6
#define EON_FAST_DUMMY_CYCLE    0x8

/* Used for SST flashes only. */
#define	OPCODE_BP		0x02	/* Byte program */
#define	OPCODE_WRDI		0x04	/* Write disable */
#define	OPCODE_AAI_WP		0xad	/* Auto address increment word program */

/* Used for Macronix and Winbond flashes. */
#define	OPCODE_EN4B		0xb7	/* Enter 4-byte mode */
#define	OPCODE_EX4B		0xe9	/* Exit 4-byte mode */

/* Used for Spansion flashes only. */
#define	OPCODE_BRWR		0x17	/* Bank register write */

/* Status Register bits. */
#define	SR_WIP			1	/* Write in progress */
#define	SR_WEL			2	/* Write enable latch */
/* meaning of other SR_* bits may differ between vendors */
#define	SR_BP0			4	/* Block protect 0 */
#define	SR_BP1			8	/* Block protect 1 */
#define	SR_BP2			0x10	/* Block protect 2 */
#define	SR_SRWD			0x80	/* SR write protect */

#define SR_QUAD_EN_MX           0x40    /* Macronix Quad I/O */

/* Configuration Register bits. */
#define CR_QUAD_EN_SPAN		0x2     /* Spansion Quad I/O */

/* Define max times to check status register before we give up. */
#define	MAX_READY_WAIT_JIFFIES	(40 * HZ)	/* M25P16 specs 40s max chip erase */
#define	MAX_CMD_SIZE		6

#define JEDEC_MFR(_jedec_id)	((_jedec_id) >> 16)

/****************************************************************************/
/* Extend flash multi-channel read/write type */
enum m25p80_rd_multi_type {
	RD_MULTI_NONE = 0x00,
	RD_DUAL_O     = 0x01,
	RD_DUAL_IO    = 0x02,
	RD_QUAD_O     = 0x03,
	RD_QUAD_IO    = 0x04
};

enum m25p80_wr_multi_type {
	WR_MULTI_NONE = 0x00,
	WR_DUAL_I     = 0x01,
	WR_DUAL_II    = 0x02,
	WR_QUAD_I     = 0x03,
	WR_QUAD_II    = 0x04
};

struct flash_cmd {
	volatile uint8_t ppx2_i;	/* flash_cmd; write dual channels */
	volatile uint8_t ppx2_ii;	/* flash_cmd; write dual channels */
	volatile uint8_t ppx4_i;	/* flash_cmd; wirte quad channels */
	volatile uint8_t ppx4_ii;	/* flash_cmd; wirte quad channels */
	volatile uint8_t readx2_o;	/* flash_cmd; read  dual channels */
	volatile uint8_t readx2_io;	/* flash_cmd; read  dual channels */
	volatile uint8_t readx4_o;	/* flash_cmd; read  quad channels */
	volatile uint8_t readx4_io;	/* flash_cmd; read  quad channels */
};

struct flash_dummy_cycles_info {
	uint32_t  rd_dual_dummy;
	uint32_t  rd_quad_dummy;
	uint32_t  fast_rd_dummy;
};

struct flash_rw_multi_type_info {
	enum m25p80_rd_multi_type rd_dual_type;
	enum m25p80_rd_multi_type rd_quad_type;
	enum m25p80_wr_multi_type wr_dual_type;
	enum m25p80_wr_multi_type wr_quad_type;
};

struct flash_vendor_info {
	uint8_t	flash_id;
	char	vendor_name[16];
	struct	flash_cmd *cmd;
	struct	flash_dummy_cycles_info *dummy;
	struct	flash_rw_multi_type_info *type;
};
/* Flash device cmd,
 * If you want to add new flash vendor
 * Please add new flash vendor command
 */
struct flash_cmd mxic_cmd = {
	PPX2_I,
	PPX2_II,
	PPX4_I,
	PPX4_II,
	READX2_I,
	READX2_IO,
	READX4_I,
	READX4_IO,
};

struct flash_cmd esmt_cmd = {
	PPX2_I,
	PPX2_II,
	PPX4_I,
	PPX4_II,
	READX2_I,
	READX2_IO,
	READX4_I,
	READX4_IO,
};

struct flash_cmd winbond_cmd = {
	PPX2_I,
	PPX2_II,
	PPX4_I,
	PPX4_II,
	READX2_I,
	READX2_IO,
	READX4_I,
	READX4_IO,
};

struct flash_cmd spansion_cmd = {
	PPX2_I,
	PPX2_II,
	PPX4_I,
	PPX4_II,
	READX2_I,
	READX2_IO,
	READX4_I,
	READX4_IO,
};

struct flash_cmd gigadevice_cmd = {
	PPX2_I,
	PPX2_II,
	PPX4_I,
	PPX4_II,
	READX2_I,
	READX2_IO,
	READX4_I,
	READX4_IO,
};

struct flash_cmd eon_cmd = {
	PPX2_I,
	PPX2_II,
	PPX4_I,
	PPX4_II,
	READX2_I,
	READX2_IO,
	READX4_I,
	READX4_IO,
};

/* Flash device dummy info,
 * If you want to add new flash vendor
 * Please add new flash vendor dummy
 */
struct flash_dummy_cycles_info	mxic_dummy_cycles_info = {
	MXIC_DUAL_DUMMY_CYCLE,
	MXIC_QUAD_DUMMY_CYCLE,
	MXIC_FAST_DUMMY_CYCLE,
};

struct flash_dummy_cycles_info	esmt_dummy_cycles_info = {
	ESMT_DUAL_DUMMY_CYCLE,
	ESMT_QUAD_DUMMY_CYCLE,
	ESMT_FAST_DUMMY_CYCLE,
};

struct flash_dummy_cycles_info	winbond_dummy_cycles_info = {
	WINBOND_DUAL_DUMMY_CYCLE,
	WINBOND_QUAD_DUMMY_CYCLE,
	WINBOND_FAST_DUMMY_CYCLE,
};

struct flash_dummy_cycles_info	spansion_dummy_cycles_info = {
	SPANSION_DUAL_DUMMY_CYCLE,
	SPANSION_QUAD_DUMMY_CYCLE,
	SPANSION_FAST_DUMMY_CYCLE,
};

struct flash_dummy_cycles_info	gigadevice_dummy_cycles_info = {
	GIGADEVICE_DUAL_DUMMY_CYCLE,
	GIGADEVICE_QUAD_DUMMY_CYCLE,
	GIGADEVICE_FAST_DUMMY_CYCLE,
};

struct flash_dummy_cycles_info	eon_dummy_cycles_info = {
	EON_DUAL_DUMMY_CYCLE,
	EON_QUAD_DUMMY_CYCLE,
	EON_FAST_DUMMY_CYCLE,
};

/* Flash device read/write multi type,
 * If you want to add new flash vendor
 * Please add new flash vendor dummy
 */
struct flash_rw_multi_type_info mxic_rw_multi_type_info = {
	RD_DUAL_IO,
	RD_QUAD_IO,
	WR_MULTI_NONE,
	WR_QUAD_II
};

struct flash_rw_multi_type_info esmt_rw_multi_type_info = {
	RD_DUAL_IO,
	RD_QUAD_IO,
	WR_MULTI_NONE,
	WR_QUAD_II
};

struct flash_rw_multi_type_info winbond_rw_multi_type_info = {
	RD_DUAL_IO,
	RD_QUAD_IO,
	WR_MULTI_NONE,
	WR_QUAD_II
};

struct flash_rw_multi_type_info spansion_rw_multi_type_info = {
	RD_DUAL_IO,
	RD_QUAD_IO,
	WR_MULTI_NONE,
	WR_QUAD_II
};

struct flash_rw_multi_type_info gigadevice_rw_multi_type_info = {
	RD_DUAL_IO,
	RD_QUAD_IO,
	WR_MULTI_NONE,
	WR_QUAD_II
};

struct flash_rw_multi_type_info eon_rw_multi_type_info = {
	RD_DUAL_IO,
	RD_QUAD_IO,
	WR_MULTI_NONE,
	WR_QUAD_II
};

/*-----------------------------------------------------------------*/

struct device_info {
	uint32_t device_id;
	struct flash_cmd    *cmd;
	struct flash_dummy_cycles_info *dummy;
	struct flash_rw_multi_type_info *type;
};

/* Add new flash vendor
 * format (FlashID, vendor name, flash cmd,
 * flash dummy cycle and read/write multi type),
 * if you want to add new flash
 * vendor, please add here.
 */
struct flash_vendor_info info_mxic = {
	0xC2,
	"MXIC",
	&mxic_cmd,
	&mxic_dummy_cycles_info,
	&mxic_rw_multi_type_info
};

struct flash_vendor_info info_esmt = {
	0x8C,
	"ESMT",
	&esmt_cmd,
	&esmt_dummy_cycles_info,
	&esmt_rw_multi_type_info
};

struct flash_vendor_info info_winbond = {
	0xEF,
	"Winbond",
	&winbond_cmd,
	&winbond_dummy_cycles_info,
	&winbond_rw_multi_type_info
};

struct flash_vendor_info info_spansion = {
	0x01,
	"Spansion",
	&spansion_cmd,
	&spansion_dummy_cycles_info,
	&spansion_rw_multi_type_info
};

struct flash_vendor_info info_gigadevice = {
	0xC8,
	"GigaDevice",
	&gigadevice_cmd,
	&gigadevice_dummy_cycles_info,
	&gigadevice_rw_multi_type_info
};

struct flash_vendor_info info_eon = {
	0x1C,
	"EON",
	&eon_cmd,
	&eon_dummy_cycles_info,
	&eon_rw_multi_type_info
};

/* Flash vendors eg: MXIC.... */
struct flash_device {
	struct flash_vendor_info *vendors[6];
};

struct flash_device device = {
	{ &info_mxic, &info_esmt, &info_winbond, &info_spansion, &info_gigadevice, &info_eon }
};

enum read_type {
	M25P80_NORMAL = 0,
	M25P80_FAST,
	M25P80_QUAD,
	M25P80_DUAL,
	M25P80_AUTO
};

enum write_type {
	M25P80_QUAD_WRITE = 5,
	M25P80_DUAL_WRITE,
	M25P80_NORMAL_WRITE,
	M25P80_AUTO_WRITE
};

struct m25p {
	struct spi_device	*spi;
//	struct mutex		lock;
	struct mtd_info		mtd;
	u16			page_size;
	u16			addr_width;
	u8			erase_opcode;
	u8			read_opcode;
	u8			program_opcode;
//	u8			*command;
	u8			command[8];
	enum read_type		flash_read;
	/* support special flash write mode */
	enum write_type		flash_write;
	bool			fast_read;
	/* support multi-channel write and read */
	bool			quad;
	bool			dual;
	/* support auto mode */
	bool			auto_mode;
	/* support flash vendor information */
	struct	device_info	dev_info;
	/* flash read dummy cycles */
	unsigned int	dummy;
	/* support flash read/write multi-channel type */
	unsigned int	write_type;
	unsigned int	read_type;
};

static inline struct m25p *mtd_to_m25p(struct mtd_info *mtd)
{
	return container_of(mtd, struct m25p, mtd);
}

/****************************************************************************/

/*
 * Internal helper functions
 */

/*
 * Read the status register, returning its value in the location
 * Return the status register value.
 * Returns negative if error occurred.
 */
static int read_sr(struct m25p *flash)
{
	ssize_t retval;
	u8 code = OPCODE_RDSR;
	u8 val;

	retval = spi_write_then_read(flash->spi, &code, 1, &val, 1);

	if (retval < 0) {
		dev_err(&flash->spi->dev, "error %d reading SR\n",
				(int) retval);
		return retval;
	}

	return val;
}

/*
 * Read configuration register, returning its value in the
 * location. Return the configuration register value.
 * Returns negative if error occured.
 */
static int read_cr(struct m25p *flash)
{
	u8 code = OPCODE_RDCR;
	int ret;
	u8 val;

	ret = spi_write_then_read(flash->spi, &code, 1, &val, 1);
	if (ret < 0) {
		dev_err(&flash->spi->dev, "error %d reading CR\n", ret);
		return ret;
	}

	return val;
}

/*
 * Write status register 1 byte
 * Returns negative if error occurred.
 */
static int write_sr(struct m25p *flash, u8 val)
{
	flash->command[0] = OPCODE_WRSR;
	flash->command[1] = val;

	return spi_write(flash->spi, flash->command, 2);
}

/*
 * Set write enable latch with Write Enable command.
 * Returns negative if error occurred.
 */
static inline int write_enable(struct m25p *flash)
{
	u8	code = OPCODE_WREN;

	return spi_write_then_read(flash->spi, &code, 1, NULL, 0);
}

/*
 * Send write disble instruction to the chip.
 */
static inline int write_disable(struct m25p *flash)
{
	u8	code = OPCODE_WRDI;

	return spi_write_then_read(flash->spi, &code, 1, NULL, 0);
}

/*
 * Enable/disable 4-byte addressing mode.
 */
static inline int set_4byte(struct m25p *flash, u32 jedec_id, int enable)
{
	int status;
	bool need_wren = false;

	switch (JEDEC_MFR(jedec_id)) {
	case CFI_MFR_ST: /* Micron, actually */
		/* Some Micron need WREN command; all will accept it */
		need_wren = true;
	case CFI_MFR_MACRONIX:
	case 0xEF /* winbond */:
		if (need_wren)
			write_enable(flash);

		flash->command[0] = enable ? OPCODE_EN4B : OPCODE_EX4B;
		status = spi_write(flash->spi, flash->command, 1);

		if (need_wren)
			write_disable(flash);

		return status;
	default:
		/* Spansion style */
		flash->command[0] = OPCODE_BRWR;
		flash->command[1] = enable << 7;
		return spi_write(flash->spi, flash->command, 2);
	}
}

/*
 * Service routine to read status register until ready, or timeout occurs.
 * Returns non-zero if error.
 */
static int wait_till_ready(struct m25p *flash)
{
	//unsigned long deadline;
	int sr;

	//deadline = jiffies + MAX_READY_WAIT_JIFFIES;

	do {
		if ((sr = read_sr(flash)) < 0)
			break;
		else if (!(sr & SR_WIP))
			return 0;

		//cond_resched();

	} while (1);  // (!time_after_eq(jiffies, deadline));

	return 1;
}

/*
 * Write status Register and configuration register with 2 bytes
 * The first byte will be written to the status register, while the
 * second byte will be written to the configuration register.
 * Return negative if error occured.
 */
static int write_sr_cr(struct m25p *flash, u16 val)
{
	flash->command[0] = OPCODE_WRSR;
	flash->command[1] = val & 0xff;
	flash->command[2] = (val >> 8);

	return spi_write(flash->spi, flash->command, 3);
}

static int macronix_quad_enable(struct m25p *flash)
{
	int ret, val;
	u8 cmd[2];
	cmd[0] = OPCODE_WRSR;

	val = read_sr(flash);
	cmd[1] = val | SR_QUAD_EN_MX;
	write_enable(flash);

	spi_write(flash->spi, &cmd, 2);

	if (wait_till_ready(flash))
		return 1;

	ret = read_sr(flash);
	if (!(ret > 0 && (ret & SR_QUAD_EN_MX))) {
		dev_err(&flash->spi->dev, "Macronix Quad bit not set\n");
		return -EINVAL;
	}

	return 0;
}

static int spansion_quad_enable(struct m25p *flash)
{
	int ret;
	int quad_en = CR_QUAD_EN_SPAN << 8;

	write_enable(flash);

	ret = write_sr_cr(flash, quad_en);
	if (ret < 0) {
		dev_err(&flash->spi->dev,
			"error while writing configuration register\n");
		return -EINVAL;
	}

	/* read back and check it */
	ret = read_cr(flash);
	if (!(ret > 0 && (ret & CR_QUAD_EN_SPAN))) {
		dev_err(&flash->spi->dev, "Spansion Quad bit not set\n");
		return -EINVAL;
	}

	return 0;
}

static int set_quad_mode(struct m25p *flash, u32 jedec_id)
{
	int status;

	switch (JEDEC_MFR(jedec_id)) {
	case CFI_MFR_MACRONIX:
		status = macronix_quad_enable(flash);
		if (status) {
			dev_err(&flash->spi->dev,
				"Macronix quad-read not enabled\n");
			return -EINVAL;
		}
		return status;
	default:
		status = spansion_quad_enable(flash);
		if (status) {
			dev_err(&flash->spi->dev,
				"Spansion quad-read not enabled\n");
			return -EINVAL;
		}
		return status;
	}
}

/* Get flash device information such as command, dummy cycle and type */
static int get_flash_device(struct m25p *flash, u32 jedec_id)
{
	uint32_t i;
	uint32_t size;

	flash->dev_info.device_id = JEDEC_MFR(jedec_id);
	size = sizeof(struct flash_device)/sizeof(struct flash_vendor_info *);
	for (i = 0; i < size; i++) {
		if (flash->dev_info.device_id == device.vendors[i]->flash_id) {
			flash->dev_info.cmd = device.vendors[i]->cmd;
			flash->dev_info.dummy = device.vendors[i]->dummy;
			flash->dev_info.type = device.vendors[i]->type;
			printk(KERN_INFO "flash vendor: %s\n", device.vendors[i]->vendor_name);
			return 0;
		}
	}
	printk(KERN_INFO "\n>>>>>No Flash Vendor support (0x%x)<<<<<\n\n", jedec_id);
#if 1
	printk("use MXIC as flash vendor instead\n");
	flash->dev_info.cmd = device.vendors[0]->cmd;
	flash->dev_info.dummy = device.vendors[0]->dummy;
	flash->dev_info.type = device.vendors[0]->type;
	return 0;
#else
	return -EINVAL;
#endif
}
/*
 * Erase the whole flash memory
 *
 * Returns 0 if successful, non-zero otherwise.
 */
static int erase_chip(struct m25p *flash)
{
	pr_debug("%s %dKiB\n", __func__,
			(flash->mtd.size >> 10));

	/* Wait until finished previous write command. */
	if (wait_till_ready(flash))
		return 1;

	/* Send write enable, then erase commands. */
	write_enable(flash);

	/* Set up command buffer. */
	flash->command[0] = OPCODE_CHIP_ERASE;

	spi_write(flash->spi, flash->command, 1);
	printk("@");
	return 0;
}

static void m25p_addr2cmd(struct m25p *flash, unsigned int addr, u8 *cmd)
{
	/* opcode is in cmd[0] */
	cmd[1] = addr >> (flash->addr_width * 8 -  8);
	cmd[2] = addr >> (flash->addr_width * 8 - 16);
	cmd[3] = addr >> (flash->addr_width * 8 - 24);
	cmd[4] = addr >> (flash->addr_width * 8 - 32);
}

static int m25p_cmdsz(struct m25p *flash)
{
	return 1 + flash->addr_width;
}

/*
 * Erase one sector of flash memory at offset ``offset'' which is any
 * address within the sector which should be erased.
 *
 * Returns 0 if successful, non-zero otherwise.
 */
static int erase_sector(struct m25p *flash, u32 offset)
{
	pr_debug("%s %dKiB at 0x%08x\n", 
			__func__, flash->mtd.erasesize / 1024, offset);

	/* Wait until finished previous write command. */
	if (wait_till_ready(flash))
		return 1;

	/* Send write enable, then erase commands. */
	write_enable(flash);

	/* Set up command buffer. */
	flash->command[0] = flash->erase_opcode;
	m25p_addr2cmd(flash, offset, flash->command);

	spi_write(flash->spi, flash->command, m25p_cmdsz(flash));
	printk(".");
	return 0;
}

/****************************************************************************/

/*
 * MTD implementation
 */

/*
 * Erase an address range on the flash chip.  The address range may extend
 * one or more erase sectors.  Return an error is there is a problem erasing.
 */
static int m25p80_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	u32 addr,len;
	uint32_t rem;

	pr_debug("%s at 0x%x, len %d\n", 
			__func__, instr->addr,
			instr->len);

	div_u64_rem(instr->len, mtd->erasesize, &rem);
	if (rem)
		return -EINVAL;

	addr = instr->addr;
	len = instr->len;

	mutex_lock(&flash->lock);

	/* whole-chip erase? */
	if (len == flash->mtd.size) {
		if (erase_chip(flash)) {
			instr->state = MTD_ERASE_FAILED;
			mutex_unlock(&flash->lock);
			return -EIO;
		}

	/* REVISIT in some cases we could speed up erasing large regions
	 * by using OPCODE_SE instead of OPCODE_BE_4K.  We may have set up
	 * to use "small sector erase", but that's not always optimal.
	 */

	/* "sector"-at-a-time erase */
	} else {
		while (len) {
			if (erase_sector(flash, addr)) {
				instr->state = MTD_ERASE_FAILED;
				mutex_unlock(&flash->lock);
				return -EIO;
			}

			addr += mtd->erasesize;
			len -= mtd->erasesize;
		}
	}

	mutex_unlock(&flash->lock);

	instr->state = MTD_ERASE_DONE;
//	mtd_erase_callback(instr);

	return 0;
}

/*
 * Dummy Cycle calculation for different type of read.
 * It can be used to support more commands with
 * different dummy cycle requirements.
 */
static inline int m25p80_dummy_cycles_read(struct m25p *flash)
{
	switch (flash->flash_read) {
	case M25P80_FAST:
	case M25P80_DUAL:
	case M25P80_QUAD:
		return 1;
	case M25P80_NORMAL:
	case M25P80_AUTO:
		return 0;
	default:
		dev_err(&flash->spi->dev, "No valid read type supported\n");
		return -1;
	}
}

static inline unsigned int m25p80_rx_nbits(const struct m25p *flash)
{
	switch (flash->flash_read) {
	case M25P80_QUAD:
		return 4;
	default:
		return 0;
	}
}

/*
 * Read an address range from the flash chip.  The address range
 * may be any size provided it is within the physical boundaries.
 */
static int m25p80_read(struct mtd_info *mtd, loff_t from, size_t len,
	size_t *retlen, u_char *buf)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	struct spi_transfer t[2];
	struct spi_message m;
	uint8_t opcode;
	int dummy;
	unsigned int type;
	uint8_t	mode;

	pr_debug("%s from 0x%08x, len %d\n", 
			__func__, (u32)from, len);

	spi_message_init(&m);
	memset(t, 0, (sizeof t));

	dummy =  m25p80_dummy_cycles_read(flash);
	if (dummy < 0) {
		dev_err(&flash->spi->dev, "No valid read command supported\n");
		return -EINVAL;
	}

	t[0].tx_buf = flash->command;
	t[0].len = m25p_cmdsz(flash) + dummy;
	spi_message_add_tail(&t[0], &m);

	t[1].rx_buf = buf;
	t[1].rx_nbits = m25p80_rx_nbits(flash);
	t[1].len = len;
	spi_message_add_tail(&t[1], &m);

	mutex_lock(&flash->lock);

	/* Wait till previous write/erase is done. */
	if (wait_till_ready(flash)) {
		/* REVISIT status return?? */
		mutex_unlock(&flash->lock);
		return 1;
	}

	/* Set up the write data buffer. */
	opcode = flash->read_opcode;
	mode = flash->flash_read;
	type = flash->read_type;
	flash->command[0] = opcode;
	m25p_addr2cmd(flash, from, flash->command);
	flash->command[5] = mode;
	flash->command[6] = flash->dummy;
	flash->command[7] = type;

	spi_sync(flash->spi, &m);

	*retlen = m.actual_length - m25p_cmdsz(flash) - dummy;

	mutex_unlock(&flash->lock);

	return 0;
}

/*
 * Write an address range to the flash chip.  Data must be written in
 * FLASH_PAGESIZE chunks.  The address range may be any size provided
 * it is within the physical boundaries.
 */
static int m25p80_write(struct mtd_info *mtd, loff_t to, size_t len,
	size_t *retlen, const u_char *buf)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	u32 page_offset, page_size;
	struct spi_transfer t[2];
	struct spi_message m;
	unsigned int type;
	uint8_t mode;

	pr_debug("%s to 0x%08x, len %d\n", 
			__func__, (u32)to, len);
	printk(">");
	spi_message_init(&m);
	memset(t, 0, (sizeof t));

	t[0].tx_buf = flash->command;
	t[0].len = m25p_cmdsz(flash);
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	spi_message_add_tail(&t[1], &m);

	mutex_lock(&flash->lock);

	/* Wait until finished previous write command. */
	if (wait_till_ready(flash)) {
		mutex_unlock(&flash->lock);
		return 1;
	}

	write_enable(flash);

	/* Set up the opcode in the write buffer. */
	mode = flash->flash_write;
	type = flash->write_type;
	flash->command[0] = flash->program_opcode;
	m25p_addr2cmd(flash, to, flash->command);
	flash->command[5] = mode;
	flash->command[7] = type;
	page_offset = to & (flash->page_size - 1);

	/* do all the bytes fit onto one page? */
	if (page_offset + len <= flash->page_size) {
		t[1].len = len;

		spi_sync(flash->spi, &m);
		printk(".");
		*retlen = m.actual_length - m25p_cmdsz(flash);
	} else {
		u32 i;

		/* the size of data remaining on the first page */
		page_size = flash->page_size - page_offset;

		t[1].len = page_size;
		spi_sync(flash->spi, &m);
		printk(".");
		*retlen = m.actual_length - m25p_cmdsz(flash);

		/* write everything in flash->page_size chunks */
		for (i = page_size; i < len; i += page_size) {
			page_size = len - i;
			if (page_size > flash->page_size)
				page_size = flash->page_size;

			/* write the next page to flash */
			m25p_addr2cmd(flash, to + i, flash->command);

			t[1].tx_buf = buf + i;
			t[1].len = page_size;

			wait_till_ready(flash);

			write_enable(flash);

			spi_sync(flash->spi, &m);
			printk(".");
			*retlen += m.actual_length - m25p_cmdsz(flash);
		}
	}

	mutex_unlock(&flash->lock);

	return 0;
}

static int sst_write(struct mtd_info *mtd, loff_t to, size_t len,
		size_t *retlen, const u_char *buf)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	struct spi_transfer t[2];
	struct spi_message m;
	size_t actual;
	int cmd_sz, ret;

	pr_debug("%s to 0x%08x, len %zd\n", 
			__func__, (u32)to, len);

	spi_message_init(&m);
	memset(t, 0, (sizeof t));

	t[0].tx_buf = flash->command;
	t[0].len = m25p_cmdsz(flash);
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	spi_message_add_tail(&t[1], &m);

	mutex_lock(&flash->lock);

	/* Wait until finished previous write command. */
	ret = wait_till_ready(flash);
	if (ret)
		goto time_out;

	write_enable(flash);

	actual = to % 2;
	/* Start write from odd address. */
	if (actual) {
		flash->command[0] = OPCODE_BP;
		m25p_addr2cmd(flash, to, flash->command);

		/* write one byte. */
		t[1].len = 1;
		spi_sync(flash->spi, &m);
		ret = wait_till_ready(flash);
		if (ret)
			goto time_out;
		*retlen += m.actual_length - m25p_cmdsz(flash);
	}
	to += actual;

	flash->command[0] = OPCODE_AAI_WP;
	m25p_addr2cmd(flash, to, flash->command);

	/* Write out most of the data here. */
	cmd_sz = m25p_cmdsz(flash);
	for (; actual < len - 1; actual += 2) {
		t[0].len = cmd_sz;
		/* write two bytes. */
		t[1].len = 2;
		t[1].tx_buf = buf + actual;

		spi_sync(flash->spi, &m);
		ret = wait_till_ready(flash);
		if (ret)
			goto time_out;
		*retlen += m.actual_length - cmd_sz;
		cmd_sz = 1;
		to += 2;
	}
	write_disable(flash);
	ret = wait_till_ready(flash);
	if (ret)
		goto time_out;

	/* Write out trailing byte if it exists. */
	if (actual != len) {
		write_enable(flash);
		flash->command[0] = OPCODE_BP;
		m25p_addr2cmd(flash, to, flash->command);
		t[0].len = m25p_cmdsz(flash);
		t[1].len = 1;
		t[1].tx_buf = buf + actual;

		spi_sync(flash->spi, &m);
		ret = wait_till_ready(flash);
		if (ret)
			goto time_out;
		*retlen += m.actual_length - m25p_cmdsz(flash);
		write_disable(flash);
	}

time_out:
	mutex_unlock(&flash->lock);
	return ret;
}

static int m25p80_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	uint32_t offset = ofs;
	uint8_t status_old, status_new;
	int res = 0;

	mutex_lock(&flash->lock);
	/* Wait until finished previous command */
	if (wait_till_ready(flash)) {
		res = 1;
		goto err;
	}

	status_old = read_sr(flash);

	if (offset < flash->mtd.size-(flash->mtd.size/2))
		status_new = status_old | SR_BP2 | SR_BP1 | SR_BP0;
	else if (offset < flash->mtd.size-(flash->mtd.size/4))
		status_new = (status_old & ~SR_BP0) | SR_BP2 | SR_BP1;
	else if (offset < flash->mtd.size-(flash->mtd.size/8))
		status_new = (status_old & ~SR_BP1) | SR_BP2 | SR_BP0;
	else if (offset < flash->mtd.size-(flash->mtd.size/16))
		status_new = (status_old & ~(SR_BP0|SR_BP1)) | SR_BP2;
	else if (offset < flash->mtd.size-(flash->mtd.size/32))
		status_new = (status_old & ~SR_BP2) | SR_BP1 | SR_BP0;
	else if (offset < flash->mtd.size-(flash->mtd.size/64))
		status_new = (status_old & ~(SR_BP2|SR_BP0)) | SR_BP1;
	else
		status_new = (status_old & ~(SR_BP2|SR_BP1)) | SR_BP0;

	/* Only modify protection if it will not unlock other areas */
	if ((status_new&(SR_BP2|SR_BP1|SR_BP0)) >
					(status_old&(SR_BP2|SR_BP1|SR_BP0))) {
		write_enable(flash);
		if (write_sr(flash, status_new) < 0) {
			res = 1;
			goto err;
		}
	}

err:	mutex_unlock(&flash->lock);
	return res;
}

static int m25p80_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	uint32_t offset = ofs;
	uint8_t status_old, status_new;
	int res = 0;

	mutex_lock(&flash->lock);
	/* Wait until finished previous command */
	if (wait_till_ready(flash)) {
		res = 1;
		goto err;
	}

	status_old = read_sr(flash);

	if (offset+len > flash->mtd.size-(flash->mtd.size/64))
		status_new = status_old & ~(SR_BP2|SR_BP1|SR_BP0);
	else if (offset+len > flash->mtd.size-(flash->mtd.size/32))
		status_new = (status_old & ~(SR_BP2|SR_BP1)) | SR_BP0;
	else if (offset+len > flash->mtd.size-(flash->mtd.size/16))
		status_new = (status_old & ~(SR_BP2|SR_BP0)) | SR_BP1;
	else if (offset+len > flash->mtd.size-(flash->mtd.size/8))
		status_new = (status_old & ~SR_BP2) | SR_BP1 | SR_BP0;
	else if (offset+len > flash->mtd.size-(flash->mtd.size/4))
		status_new = (status_old & ~(SR_BP0|SR_BP1)) | SR_BP2;
	else if (offset+len > flash->mtd.size-(flash->mtd.size/2))
		status_new = (status_old & ~SR_BP1) | SR_BP2 | SR_BP0;
	else
		status_new = (status_old & ~SR_BP0) | SR_BP2 | SR_BP1;

	/* Only modify protection if it will not lock other areas */
	if ((status_new&(SR_BP2|SR_BP1|SR_BP0)) <
					(status_old&(SR_BP2|SR_BP1|SR_BP0))) {
		write_enable(flash);
		if (write_sr(flash, status_new) < 0) {
			res = 1;
			goto err;
		}
	}

err:	mutex_unlock(&flash->lock);
	return res;
}

/****************************************************************************/

/*
 * SPI device driver setup and teardown
 */

struct flash_info {
	/* JEDEC id zero means "no ID" (most older chips); otherwise it has
	 * a high byte of zero plus three data bytes: the manufacturer id,
	 * then a two byte device id.
	 */
	u32		jedec_id;
	u16             ext_id;

	/* The size listed here is what works with OPCODE_SE, which isn't
	 * necessarily called a "sector" by the vendor.
	 */
	unsigned	sector_size;
	u16		n_sectors;

	u16		page_size;
	u16		addr_width;

	u16		flags;
	u32		normal_clk;
	u32		fastrd_clk;
	u32		ex_flags;
#define	SECT_4K		0x01		/* OPCODE_BE_4K works uniformly */
#define	M25P_NO_ERASE	0x02		/* No erase command needed */
#define	SST_WRITE	0x04		/* use SST byte programming */
#define	M25P_NO_FR	0x08		/* Can't do fastread */
#define	SECT_4K_PMC	0x10		/* OPCODE_BE_4K_PMC works uniformly */
#define	M25P80_QUAD_READ	0x20    /* Flash supports Quad Read */
};

#define FLASH_VERIFIED	0x01		/* Flash has been verified */
#define USE_4B_READ		0x02		/* use 4-byte-address read command */

#define INFO(_jedec_id, _ext_id, _sector_size, _n_sectors, _flags, _normal_clk, _fastrd_clk, _ex_flags)	\
	((kernel_ulong_t)&(struct flash_info) {				\
		.jedec_id = (_jedec_id),				\
		.ext_id = (_ext_id),					\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = 256,					\
		.flags = (_flags),					\
		.normal_clk = (_normal_clk),		\
		.fastrd_clk = (_fastrd_clk),		\
		.ex_flags = (_ex_flags),			\
	})

#define CAT25_INFO(_sector_size, _n_sectors, _page_size, _addr_width, _flags, _normal_clk, _fastrd_clk, _ex_flags)	\
	((kernel_ulong_t)&(struct flash_info) {				\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = (_page_size),				\
		.addr_width = (_addr_width),				\
		.flags = (_flags),					\
		.normal_clk = (_normal_clk),		\
		.fastrd_clk = (_fastrd_clk),		\
		.ex_flags = (_ex_flags),			\
	})

/* NOTE: double check command sets and memory organization when you add
 * more flash chips.  This current list focusses on newer chips, which
 * have been converging on command sets which including JEDEC ID.
 */
static const struct spi_device_id m25p_ids[] = {
	/* Atmel -- some are (confusingly) marketed as "DataFlash" */
	{ "at25fs010",  INFO(0x1f6601, 0, 32 * 1024,   4, SECT_4K, 41000000, 41000000, 0x0) },
	{ "at25fs040",  INFO(0x1f6604, 0, 64 * 1024,   8, SECT_4K, 41000000, 41000000, 0x0) },

	{ "at25df041a", INFO(0x1f4401, 0, 64 * 1024,   8, SECT_4K, 29000000, 41000000, 0x0) },
	{ "at25df321a", INFO(0x1f4701, 0, 64 * 1024,  64, SECT_4K, 41000000, 75000000, 0x0) },
	{ "at25df641",  INFO(0x1f4800, 0, 64 * 1024, 128, SECT_4K, 41000000, 75000000, 0x0) },

	{ "at26f004",   INFO(0x1f0400, 0, 64 * 1024,  8, SECT_4K, 18000000, 29000000, 0x0) },
	{ "at26df081a", INFO(0x1f4501, 0, 64 * 1024, 16, SECT_4K, 29000000, 41000000, 0x0) },
	{ "at26df161a", INFO(0x1f4601, 0, 64 * 1024, 32, SECT_4K, 29000000, 41000000, 0x0) },
	{ "at26df321",  INFO(0x1f4700, 0, 64 * 1024, 64, SECT_4K, 29000000, 41000000, 0x0) },

	{ "at45db081d", INFO(0x1f2500, 0, 64 * 1024, 16, SECT_4K, 29000000, 41000000, 0x0) },

	/* EON -- en25xxx */
	{ "en25f16",    INFO(0x1c3115, 0, 64 * 1024,   32, SECT_4K, 41000000, 75000000, 0x0) },
	{ "en25f32",    INFO(0x1c3116, 0, 64 * 1024,   64, SECT_4K, 41000000, 75000000, 0x0) },
	{ "en25p32",    INFO(0x1c2016, 0, 64 * 1024,   64, 0, 41000000, 75000000, 0x0) },
	{ "en25q16",   INFO(0x1c3015, 0, 64 * 1024,   32, 0, 41000000, 75000000, 0x0) },
	{ "en25q32b",   INFO(0x1c3016, 0, 64 * 1024,   64, 0, 41000000, 100000000, 0x0) },
	{ "en25p64",    INFO(0x1c2017, 0, 64 * 1024,  128, 0, 41000000, 75000000, 0x0) },
	{ "en25q64",    INFO(0x1c3017, 0, 64 * 1024,  128, SECT_4K, 41000000, 100000000, 0x0) },
	{ "en25qh256",  INFO(0x1c7019, 0, 64 * 1024,  512, 0, 41000000, 75000000, 0x0) },

	/* ESMT */
	{ "f25l08qa(2s)", INFO(0x8c4014, 0, 64 * 1024, 16, SECT_4K, 29000000, 41000000, 0x0) },
	{ "f25l16pa(2s)", INFO(0x8c2115, 0, 64 * 1024, 32, SECT_4K, 41000000, 41000000, 0x0) },
	{ "f25l16qa(2s)", INFO(0x8c4015, 0, 64 * 1024, 32, SECT_4K, 41000000, 41000000, 0x0) },
	{ "f25l32pa", INFO(0x8c2016, 0, 64 * 1024, 64, SECT_4K, 29000000, 41000000, 0x0) },
	{ "f25l32qa", INFO(0x8c4016, 0, 64 * 1024, 64, SECT_4K, 29000000, 41000000, 0x0) },
	{ "f25l32qa(2s)", INFO(0x8c4116, 0, 64 * 1024, 64, SECT_4K, 41000000, 41000000, 0x0) },
	{ "f25l64qa", INFO(0x8c4117, 0, 64 * 1024, 128, SECT_4K, 41000000, 41000000, FLASH_VERIFIED) },
	{ "f25l128qa", INFO(0x8c4118, 0, 64 * 1024, 256, SECT_4K, 41000000, 100000000, 0x0) },

	/* Everspin */
	{ "mr25h256", CAT25_INFO( 32 * 1024, 1, 256, 2, M25P_NO_ERASE | M25P_NO_FR, 29000000, 29000000, 0x0) },
	{ "mr25h10",  CAT25_INFO(128 * 1024, 1, 256, 3, M25P_NO_ERASE | M25P_NO_FR, 29000000, 29000000, 0x0) },

	/* GigaDevice */
	{ "gd25q80", INFO(0xc84014, 0, 64 * 1024,  16, SECT_4K, 41000000, 100000000, 0x0) },
	{ "gd25q16", INFO(0xc84015, 0, 64 * 1024,  32, SECT_4K, 41000000, 100000000, 0x0) },
	{ "gd25q32", INFO(0xc84016, 0, 64 * 1024,  64, SECT_4K, 75000000, 100000000, 0x0) },
	{ "gd25q64", INFO(0xc84017, 0, 64 * 1024, 128, SECT_4K, 41000000, 41000000, FLASH_VERIFIED) },
	{ "gd25q128", INFO(0xc84018, 0, 64 * 1024, 256, SECT_4K, 75000000, 100000000, 0x0) },

	/* Intel/Numonyx -- xxxs33b */
	{ "160s33b",  INFO(0x898911, 0, 64 * 1024,  32, 0, 29000000, 41000000, 0x0) },
	{ "320s33b",  INFO(0x898912, 0, 64 * 1024,  64, 0, 29000000, 41000000, 0x0) },
	{ "640s33b",  INFO(0x898913, 0, 64 * 1024, 128, 0, 29000000, 41000000, 0x0) },

	/* Macronix */
	{ "mx25l2005a",  INFO(0xc22012, 0, 64 * 1024,   4, SECT_4K, 29000000, 41000000, 0x0) },
	{ "mx25l4005a",  INFO(0xc22013, 0, 64 * 1024,   8, SECT_4K, 29000000, 75000000, 0x0) },
	{ "mx25l8005",   INFO(0xc22014, 0, 64 * 1024,  16, 0, 29000000, 41000000, 0x0) },
	{ "mx25l1606e",  INFO(0xc22015, 0, 64 * 1024,  32, SECT_4K, 29000000, 75000000, 0x0) },
	{ "mx25l1633e",  INFO(0xc22415, 0, 64 * 1024,  32, SECT_4K, 29000000, 100000000, 0x0) },
	{ "mx25l1635e",  INFO(0xc22515, 0, 64 * 1024,  32, SECT_4K, 41000000, 100000000, 0x0) },
	{ "mx25l3205d",  INFO(0xc22016, 0, 64 * 1024,  64, 0, 29000000, 41000000, FLASH_VERIFIED) },
	{ "mx25l3235d",  INFO(0xc25e16, 0, 64 * 1024,  64, SECT_4K, 29000000, 100000000, 0x0) },
	{ "mx25l3255e",  INFO(0xc29e16, 0, 64 * 1024,  64, SECT_4K, 41000000, 100000000, 0x0) },
	{ "mx25l6405d",  INFO(0xc22017, 0, 64 * 1024, 128, 0, 29000000, 41000000, FLASH_VERIFIED) },
	{ "mx25l12805d", INFO(0xc22018, 0, 64 * 1024, 256, 0, 29000000, 41000000, FLASH_VERIFIED) },
	{ "mx25l12855e", INFO(0xc22618, 0, 64 * 1024, 256, 0, 41000000, 41000000, 0x0) },
	{ "mx25l25635e", INFO(0xc22019, 0, 64 * 1024, 512, 0, 41000000, 75000000, 0x0) },
	{ "mx25l25655e", INFO(0xc22619, 0, 64 * 1024, 512, 0, 41000000, 41000000, 0x0) },
	{ "mx66l51235l", INFO(0xc2201a, 0, 64 * 1024, 1024, M25P80_QUAD_READ, 41000000, 100000000, 0x0) },
	{ "mx66l1g45g", INFO(0xc2201b, 0, 64 * 1024, 2048, 0, 41000000, 100000000, 0x0) },

	/* Micron */
	{ "n25q064",     INFO(0x20ba17, 0, 64 * 1024,  128, 0, 41000000, 100000000, 0x0) },
	{ "n25q128a11",  INFO(0x20bb18, 0, 64 * 1024,  256, 0, 41000000, 100000000, 0x0) },
	{ "n25q128a13",  INFO(0x20ba18, 0, 64 * 1024,  256, 0, 41000000, 100000000, 0x0) },
	{ "n25q256a",    INFO(0x20ba19, 0, 64 * 1024,  512, SECT_4K, 41000000, 100000000, 0x0) },
	{ "n25q512a",    INFO(0x20bb20, 0, 64 * 1024, 1024, SECT_4K, 41000000, 100000000, 0x0) },

	/* PMC */
	{ "pm25lv512",   INFO(0,        0, 32 * 1024,    2, SECT_4K_PMC, 18000000, 18000000, 0x0) },
	{ "pm25lv010",   INFO(0,        0, 32 * 1024,    4, SECT_4K_PMC, 18000000, 18000000, 0x0) },
	{ "pm25lq032",   INFO(0x7f9d46, 0, 64 * 1024,   64, SECT_4K, 29000000, 100000000, 0x0) },

	/* Spansion -- single (large) sector size only, at least
	 * for the chips listed here (without boot sectors).
	 */
	{ "s25sl032p",  INFO(0x010215, 0x4d00,  64 * 1024,  64, 0, 0, 0, 0x0) },
	{ "s25sl064p",  INFO(0x010216, 0x4d00,  64 * 1024, 128, 0, 0, 0, 0x0) },
	{ "s25fl256s0", INFO(0x010219, 0x4d00, 256 * 1024, 128, 0, 41000000, 100000000, 0x0) },
	{ "s25fl256s1", INFO(0x010219, 0x4d01,  64 * 1024, 512, M25P80_QUAD_READ, 41000000, 100000000, 0x0) },
	{ "s25fl512s",  INFO(0x010220, 0x4d00, 256 * 1024, 256, M25P80_QUAD_READ, 41000000, 100000000, 0x0) },
	{ "s70fl01gs",  INFO(0x010221, 0x4d00, 256 * 1024, 256, 0, 41000000, 100000000, 0x0) },
	{ "s25sl12800", INFO(0x012018, 0x0300, 256 * 1024,  64, 0, 0, 0, 0x0) },
	{ "s25sl12801", INFO(0x012018, 0x0301,  64 * 1024, 256, 0, 0, 0, 0x0) },
	{ "s25fl129p0", INFO(0x012018, 0x4d00, 256 * 1024,  64, 0, 29000000, 100000000, 0x0) },
	{ "s25fl129p1", INFO(0x012018, 0x4d01,  64 * 1024, 256, 0, 29000000, 100000000, 0x0) },
	{ "s25sl004a",  INFO(0x010212,      0,  64 * 1024,   8, 0, 0, 0, 0x0) },
	{ "s25sl008a",  INFO(0x010213,      0,  64 * 1024,  16, 0, 0, 0, 0x0) },
	{ "s25sl016a",  INFO(0x010214,      0,  64 * 1024,  32, 0, 0, 0, 0x0) },
	{ "s25sl032a",  INFO(0x010215,      0,  64 * 1024,  64, 0, 0, 0, 0x0) },
	{ "s25sl064a",  INFO(0x010216,      0,  64 * 1024, 128, 0, 0, 0, 0x0) },
//	{ "s25fl016k",  INFO(0xef4015,      0,  64 * 1024,  32, SECT_4K, 11000000, 11000000, 0x0) },
//	{ "s25fl064k",  INFO(0xef4017,      0,  64 * 1024, 128, SECT_4K, 33000000, 80000000, FLASH_VERIFIED) },

	/* SST -- large erase sizes are "overlays", "sectors" are 4K */
	{ "sst25vf040b", INFO(0xbf258d, 0, 64 * 1024,  8, SECT_4K | SST_WRITE, 18000000, 41000000, 0x0) },
	{ "sst25vf080b", INFO(0xbf258e, 0, 64 * 1024, 16, SECT_4K | SST_WRITE, 18000000, 41000000, 0x0) },
	{ "sst25vf016b", INFO(0xbf2541, 0, 64 * 1024, 32, SECT_4K | SST_WRITE, 18000000, 41000000, 0x0) },
	{ "sst25vf032b", INFO(0xbf254a, 0, 64 * 1024, 64, SECT_4K | SST_WRITE, 18000000, 41000000, 0x0) },
	{ "sst25vf064c", INFO(0xbf254b, 0, 64 * 1024, 128, SECT_4K, 29000000, 41000000, 0x0) },
	{ "sst25wf512",  INFO(0xbf2501, 0, 64 * 1024,  1, SECT_4K | SST_WRITE, 18000000, 29000000, 0x0) },
	{ "sst25wf010",  INFO(0xbf2502, 0, 64 * 1024,  2, SECT_4K | SST_WRITE, 18000000, 29000000, 0x0) },
	{ "sst25wf020",  INFO(0xbf2503, 0, 64 * 1024,  4, SECT_4K | SST_WRITE, 18000000, 29000000, 0x0) },
	{ "sst25wf040",  INFO(0xbf2504, 0, 64 * 1024,  8, SECT_4K | SST_WRITE, 18000000, 29000000, 0x0) },

	/* ST Microelectronics -- newer production may have feature updates */
	{ "m25p05",  INFO(0x202010,  0,  32 * 1024,   2, 0, 18000000, 18000000, 0x0) },
	{ "m25p10",  INFO(0x202011,  0,  32 * 1024,   4, 0, 18000000, 18000000, 0x0) },
	{ "m25p20",  INFO(0x202012,  0,  64 * 1024,   4, 0, 18000000, 18000000, 0x0) },
	{ "m25p40",  INFO(0x202013,  0,  64 * 1024,   8, 0, 18000000, 18000000, 0x0) },
	{ "m25p80",  INFO(0x202014,  0,  64 * 1024,  16, 0, 18000000, 18000000, 0x0) },
	{ "m25p16",  INFO(0x202015,  0,  64 * 1024,  32, 0, 18000000, 41000000, 0x0) },
	{ "m25p32",  INFO(0x202016,  0,  64 * 1024,  64, 0, 18000000, 41000000, 0x0) },
	{ "m25p64",  INFO(0x202017,  0,  64 * 1024, 128, 0, 18000000, 41000000, 0x0) },
	{ "m25p128", INFO(0x202018,  0, 256 * 1024,  64, 0, 18000000, 41000000, 0x0) },
	{ "n25q032", INFO(0x20ba16,  0,  64 * 1024,  64, 0, 41000000, 100000000, 0x0) },

	{ "m25p05-nonjedec",  INFO(0, 0,  32 * 1024,   2, 0, 18000000, 18000000, 0x0) },
	{ "m25p10-nonjedec",  INFO(0, 0,  32 * 1024,   4, 0, 18000000, 18000000, 0x0) },
	{ "m25p20-nonjedec",  INFO(0, 0,  64 * 1024,   4, 0, 18000000, 18000000, 0x0) },
	{ "m25p40-nonjedec",  INFO(0, 0,  64 * 1024,   8, 0, 18000000, 18000000, 0x0) },
	{ "m25p80-nonjedec",  INFO(0, 0,  64 * 1024,  16, 0, 18000000, 18000000, 0x0) },
	{ "m25p16-nonjedec",  INFO(0, 0,  64 * 1024,  32, 0, 18000000, 41000000, 0x0) },
	{ "m25p32-nonjedec",  INFO(0, 0,  64 * 1024,  64, 0, 18000000, 41000000, 0x0) },
	{ "m25p64-nonjedec",  INFO(0, 0,  64 * 1024, 128, 0, 18000000, 41000000, 0x0) },
	{ "m25p128-nonjedec", INFO(0, 0, 256 * 1024,  64, 0, 18000000, 41000000, 0x0) },

	{ "m45pe10", INFO(0x204011,  0, 64 * 1024,    2, 0, 29000000, 41000000, 0x0) },
	{ "m45pe80", INFO(0x204014,  0, 64 * 1024,   16, 0, 29000000, 41000000, 0x0) },
	{ "m45pe16", INFO(0x204015,  0, 64 * 1024,   32, 0, 29000000, 41000000, 0x0) },

	{ "m25pe20", INFO(0x208012,  0, 64 * 1024,  4,       0, 29000000, 41000000, 0x0) },
	{ "m25pe80", INFO(0x208014,  0, 64 * 1024, 16,       0, 18000000, 41000000, 0x0) },
	{ "m25pe16", INFO(0x208015,  0, 64 * 1024, 32, SECT_4K, 29000000, 41000000, 0x0) },

	{ "m25px16",    INFO(0x207115,  0, 64 * 1024, 32, SECT_4K, 29000000, 75000000, 0x0) },
	{ "m25px32",    INFO(0x207116,  0, 64 * 1024, 64, SECT_4K, 29000000, 75000000, 0x0) },
	{ "m25px32-s0", INFO(0x207316,  0, 64 * 1024, 64, SECT_4K, 29000000, 75000000, 0x0) },
	{ "m25px32-s1", INFO(0x206316,  0, 64 * 1024, 64, SECT_4K, 29000000, 75000000, 0x0) },
	{ "m25px64",    INFO(0x207117,  0, 64 * 1024, 128, 0, 29000000, 75000000, 0x0) },

	/* Winbond -- w25x "blocks" are 64K, "sectors" are 4KiB */
	{ "w25x10", INFO(0xef3011, 0, 64 * 1024,  2,  SECT_4K, 18000000, 29000000, 0x0) },
	{ "w25x20", INFO(0xef3012, 0, 64 * 1024,  4,  SECT_4K, 18000000, 29000000, 0x0) },
	{ "w25x40", INFO(0xef3013, 0, 64 * 1024,  8,  SECT_4K, 18000000, 29000000, 0x0) },
	{ "w25x80", INFO(0xef3014, 0, 64 * 1024,  16, SECT_4K, 18000000, 29000000, 0x0) },
	{ "w25x16", INFO(0xef3015, 0, 64 * 1024,  32, SECT_4K, 29000000, 41000000, 0x0) },
	{ "w25x32", INFO(0xef3016, 0, 64 * 1024,  64, SECT_4K, 29000000, 41000000, 0x0) },
	{ "w25q16", INFO(0xef4015, 0, 64 * 1024,  32, SECT_4K, 41000000, 75000000, 0x0) },
	{ "w25q32", INFO(0xef4016, 0, 64 * 1024,  64, SECT_4K, 41000000, 75000000, 0x0) },
	{ "w25q32dw", INFO(0xef6016, 0, 64 * 1024,  64, SECT_4K, 41000000, 100000000, 0x0) },
	{ "w25x64", INFO(0xef3017, 0, 64 * 1024, 128, SECT_4K, 29000000, 41000000, 0x0) },
	{ "w25q64", INFO(0xef4017, 0, 64 * 1024, 128, SECT_4K, 29000000, 41000000, FLASH_VERIFIED) },
	{ "w25q128", INFO(0xef4018, 0, 64 * 1024, 256, SECT_4K, 29000000, 41000000, FLASH_VERIFIED) },
	{ "w25q80", INFO(0xef5014, 0, 64 * 1024,  16, SECT_4K, 41000000, 75000000, 0x0) },
	{ "w25q80bl", INFO(0xef4014, 0, 64 * 1024,  16, SECT_4K, 18000000, 41000000, 0x0) },
	{ "w25q256", INFO(0xef4019, 0, 64 * 1024, 512, SECT_4K, 41000000, 41000000, USE_4B_READ|FLASH_VERIFIED) },

	/* Catalyst / On Semiconductor -- non-JEDEC */
	{ "cat25c11", CAT25_INFO(  16, 8, 16, 1, M25P_NO_ERASE | M25P_NO_FR, 1000000, 1000000, 0x0) },
	{ "cat25c03", CAT25_INFO(  32, 8, 16, 2, M25P_NO_ERASE | M25P_NO_FR, 1000000, 1000000, 0x0) },
	{ "cat25c09", CAT25_INFO( 128, 8, 32, 2, M25P_NO_ERASE | M25P_NO_FR, 1000000, 1000000, 0x0) },
	{ "cat25c17", CAT25_INFO( 256, 8, 32, 2, M25P_NO_ERASE | M25P_NO_FR, 1000000, 1000000, 0x0) },
	{ "cat25128", CAT25_INFO(2048, 8, 64, 2, M25P_NO_ERASE | M25P_NO_FR, 4000000, 4000000, 0x0) },
	
	/* Unknown Vendors info referenced by mx25l3205d */
	{ "unknown", INFO(0xffffff, 0, 64 * 1024,  64, 0, 0, 0, 0x0) },
	{ },
};
//MODULE_DEVICE_TABLE(spi, m25p_ids);

static const struct spi_device_id *jedec_probe(struct spi_device *spi)
{
	int			tmp;
	u8			code = OPCODE_RDID;
	u8			id[5];
	u32			jedec, default_jedec;
	u16                     ext_jedec;
	struct flash_info	*info;

	/* JEDEC also defines an optional "extended device information"
	 * string for after vendor-specific data, after the three bytes
	 * we use here.  Supporting some chips might require using it.
	 */
	tmp = spi_write_then_read(spi, &code, 1, id, 5);
	if (tmp < 0) {
		printk("\n>>>>>error %d reading JEDEC ID<<<<<\n\n", tmp);
		return ERR_PTR(tmp);
	}
	jedec = id[0];
	jedec = jedec << 8;
	jedec |= id[1];
	jedec = jedec << 8;
	jedec |= id[2];

	ext_jedec = id[3] << 8 | id[4];
	printk("JEDEC id %06X\n", jedec);
	for (tmp = 0; tmp < ARRAY_SIZE(m25p_ids) - 1; tmp++) {
		info = (void *)m25p_ids[tmp].driver_data;
		if (info->jedec_id == jedec) {
			if (info->ext_id != 0 && info->ext_id != ext_jedec)
				continue;
			return &m25p_ids[tmp];
		}
	}
	dev_err(&spi->dev, "\n>>>>>unrecognized JEDEC id %06x<<<<<\n\n", jedec);
	// if unrecognized, use unknown as default
	printk("use JEDEC id 0xffffff (unknown) instead\n");
	default_jedec = 0xffffff;
	for (tmp = 0; tmp < ARRAY_SIZE(m25p_ids) - 1; tmp++) {
		info = (void *)m25p_ids[tmp].driver_data;
		if (info->jedec_id == default_jedec)
			return &m25p_ids[tmp];
	}
	return ERR_PTR(-ENODEV);
}


/*
 * board specific setup should have ensured the SPI clock used here
 * matches what the READ command supports, at least until this driver
 * understands FAST_READ (for clocks over 25 MHz).
 */
int m25p_probe(void)
{
	struct spi_device *spi = &g_spi_device;
//	const struct spi_device_id	*id = spi_get_device_id(spi);
	const struct spi_device_id	*id;
//	struct flash_platform_data	*data;
	struct m25p			*flash;
	struct flash_info		*info;
	unsigned			i;
//	struct mtd_part_parser_data	ppdata;
#ifdef CONFIG_OF
	struct device_node *np = spi->dev.of_node;
#endif
	int ret;
	u32 max_hz;

	spi_setup(spi);

#if 0
	/* Platform data helps sort out which chip type we have, as
	 * well as how this board partitions it.  If we don't have
	 * a chip ID, try the JEDEC id commands; they'll work for most
	 * newer chips, even if we don't recognize the particular chip.
	 */
	data = dev_get_platdata(&spi->dev);
	if (data && data->type) {
		const struct spi_device_id *plat_id;

		for (i = 0; i < ARRAY_SIZE(m25p_ids) - 1; i++) {
			plat_id = &m25p_ids[i];
			if (strcmp(data->type, plat_id->name))
				continue;
			break;
		}

		if (i < ARRAY_SIZE(m25p_ids) - 1)
			id = plat_id;
		else
			dev_warn(&spi->dev, "unrecognized id %s\n", data->type);
	}

	info = (void *)id->driver_data;
#endif

	/*if (info->jedec_id)*/ {
		const struct spi_device_id *jid;

		jid = jedec_probe(spi);
		if (IS_ERR(jid)) {
			return PTR_ERR(jid);
		} else if (jid != id) {
			/*
			 * JEDEC knows better, so overwrite platform ID. We
			 * can't trust partitions any longer, but we'll let
			 * mtd apply them anyway, since some partitions may be
			 * marked read-only, and we don't want to lose that
			 * information, even if it's not 100% accurate.
			 */
			dev_warn(&spi->dev, "found %s\n", jid->name);
			id = jid;
			info = (void *)jid->driver_data;
		}
	}

	flash = &g_m25p;

//	flash->command = devm_kzalloc(&spi->dev, MAX_CMD_SIZE, GFP_KERNEL);
//	if (!flash->command)
//		return -ENOMEM;

	flash->spi = spi;
	mutex_init(&flash->lock);
//	spi_set_drvdata(spi, flash);
	spi->driver_data = flash;

	/*
	 * Atmel, SST and Intel/Numonyx serial flash tend to power
	 * up with the software protection bits set
	 */

	if (JEDEC_MFR(info->jedec_id) == CFI_MFR_ATMEL ||
	    JEDEC_MFR(info->jedec_id) == CFI_MFR_INTEL ||
	    JEDEC_MFR(info->jedec_id) == CFI_MFR_SST) {
		write_enable(flash);
		write_sr(flash, 0);
	}

//	if (data && data->name)
//		flash->mtd.name = data->name;
//	else
//		flash->mtd.name = dev_name(&spi->dev);

	flash->mtd.type = MTD_NORFLASH;
	flash->mtd.writesize = 1;
	flash->mtd.flags = MTD_CAP_NORFLASH;
	flash->mtd.size = info->sector_size * info->n_sectors;
	flash->mtd._erase = m25p80_erase;
	flash->mtd._read = m25p80_read;

	/* flash protection support for STmicro chips */
	if (JEDEC_MFR(info->jedec_id) == CFI_MFR_ST) {
		flash->mtd._lock = m25p80_lock;
		flash->mtd._unlock = m25p80_unlock;
	}

	/* sst flash chips use AAI word program */
	if (info->flags & SST_WRITE)
		flash->mtd._write = sst_write;
	else
		flash->mtd._write = m25p80_write;

	/* prefer "small sector" erase if possible */
	if (info->flags & SECT_4K) {
		flash->erase_opcode = OPCODE_BE_4K;
		flash->mtd.erasesize = 4096;
	} else if (info->flags & SECT_4K_PMC) {
		flash->erase_opcode = OPCODE_BE_4K_PMC;
		flash->mtd.erasesize = 4096;
	} else {
		flash->erase_opcode = OPCODE_SE;
		flash->mtd.erasesize = info->sector_size;
	}

	if (info->flags & M25P_NO_ERASE)
		flash->mtd.flags |= MTD_NO_ERASE;

//	ppdata.of_node = spi->dev.of_node;
//	flash->mtd.dev.parent = &spi->dev;
	flash->page_size = info->page_size;
	flash->mtd.writebufsize = flash->page_size;

	flash->fast_read = false;
	flash->quad = false;
	flash->dual = false;
	flash->auto_mode = false;
#ifdef CONFIG_OF
	if (np) {
		/* If we were instantiated by DT, use it */
		if (of_property_read_bool(np, "m25p,fast-read"))
			flash->flash_read = M25P80_FAST;
		else
			flash->flash_read = M25P80_NORMAL;
	} else {
		/* If we weren't instantiated by DT, default to fast-read */
		flash->flash_read = M25P80_FAST;
	}
#endif

	max_hz = spi->max_speed_hz;
	spi->max_speed_hz = info->normal_clk;
#ifdef CONFIG_M25PXX_USE_MULTI_CHANNEL
#ifdef CONFIG_M25PXX_USE_QUAD
	flash->quad = true;
#endif
#ifdef CONFIG_M25PXX_USE_DUAL
	flash->dual = true;
#endif
#else
#ifdef CONFIG_M25PXX_USE_FAST_READ
	flash->fast_read = true;
	spi->max_speed_hz = info->fastrd_clk;
#endif
#endif
#ifdef CONFIG_M25PXX_USE_AUTO_MODE
	flash->auto_mode = true;
#endif

	/* If this flash hasn't been verified by ourself, then set the transfer clock a safe value */
	if(!(info->ex_flags & FLASH_VERIFIED))
		spi->max_speed_hz = spi->max_speed_hz / 3;

	/* The datasheet doesn't exist, we use default value instead. */
	if(spi->max_speed_hz == 0)
		spi->max_speed_hz = max_hz;

	/* Some devices cannot do fast-read, no matter what DT tells us */
	if (info->flags & M25P_NO_FR)
		flash->flash_read = M25P80_NORMAL;

	/* Quad-read mode takes precedence over fast/normal */
	if (spi->mode & SPI_RX_QUAD && info->flags & M25P80_QUAD_READ) {
		ret = set_quad_mode(flash, info->jedec_id);
		if (ret) {
			dev_err(&flash->spi->dev, "quad mode not supported\n");
			return ret;
		}
		flash->flash_read = M25P80_QUAD;
	}

	/* flash-specific code  */
	ret = get_flash_device(flash, info->jedec_id);
	if (ret) {
		dev_err(&flash->spi->dev, "no flash device supported\n");
		return ret;
	}

	if (flash->auto_mode) {
		flash->flash_read = M25P80_AUTO;
		flash->flash_write = M25P80_AUTO_WRITE;
	} else if (flash->quad) {
		flash->flash_read = M25P80_QUAD;
		flash->flash_write = M25P80_QUAD_WRITE;
	} else if (flash->dual) {
		flash->flash_read = M25P80_DUAL;
		flash->flash_write = M25P80_DUAL_WRITE;
	} else if (flash->fast_read) {
		flash->flash_read = M25P80_FAST;
		flash->flash_write = M25P80_NORMAL_WRITE;
	} else {
		flash->flash_read = M25P80_NORMAL;
		flash->flash_write = M25P80_NORMAL_WRITE;
	}

	/* Default commands */
	switch (flash->flash_read) {
	case M25P80_QUAD:
		flash->read_opcode = flash->dev_info.cmd->readx4_io;
		flash->dummy = flash->dev_info.dummy->rd_quad_dummy;
		flash->read_type = flash->dev_info.type->rd_quad_type;
		break;
	case M25P80_DUAL:
		flash->read_opcode = flash->dev_info.cmd->readx2_io;
		flash->dummy = flash->dev_info.dummy->rd_dual_dummy;
		flash->read_type = flash->dev_info.type->rd_dual_type;
		break;
	case M25P80_FAST:
		flash->read_opcode = OPCODE_FAST_READ;
		flash->dummy = flash->dev_info.dummy->fast_rd_dummy;
		break;
	case M25P80_NORMAL:
		flash->read_opcode = OPCODE_NORM_READ;
		break;
	case M25P80_AUTO:
		flash->read_opcode = OPCODE_AUTO;
		break;
	default:
		dev_err(&flash->spi->dev, "No Read opcode defined\n");
		return -EINVAL;
	}

	switch (flash->flash_write) {
	case M25P80_QUAD_WRITE:
		flash->program_opcode = flash->dev_info.cmd->ppx4_ii;
		flash->write_type = flash->dev_info.type->wr_quad_type;
		break;
	case M25P80_DUAL_WRITE:
		flash->program_opcode = flash->dev_info.cmd->ppx2_ii;
		flash->write_type = flash->dev_info.type->wr_dual_type;
		break;
	case M25P80_NORMAL_WRITE:
		flash->program_opcode = OPCODE_PP;
		break;
	case M25P80_AUTO_WRITE:
		flash->program_opcode = OPCODE_AUTO;
		break;
	default:
		dev_err(&flash->spi->dev, "No write opcode defined\n");
		return -EINVAL;
	}

	if (info->addr_width)
		flash->addr_width = info->addr_width;
	else if (flash->mtd.size > 0x1000000) {
		/* enable 4-byte addressing if the device exceeds 16MiB */
		flash->addr_width = 4;
		if (JEDEC_MFR(info->jedec_id) == CFI_MFR_AMD) {
			/* Dedicated 4-byte command set */
			switch (flash->flash_read) {
			case M25P80_QUAD:
				flash->read_opcode = OPCODE_QUAD_READ_4B;
				break;
			case M25P80_FAST:
				flash->read_opcode = OPCODE_FAST_READ_4B;
				break;
			case M25P80_NORMAL:
			case M25P80_DUAL:
			case M25P80_AUTO:
				flash->read_opcode = OPCODE_NORM_READ_4B;
				break;
			}
			flash->program_opcode = OPCODE_PP_4B;
			/* No small sector erase for 4-byte command set */
			flash->erase_opcode = OPCODE_SE_4B;
			flash->mtd.erasesize = info->sector_size;
		}
		else {
			if (info->ex_flags & USE_4B_READ) {
				switch (flash->flash_read) {
				case M25P80_FAST:
					flash->read_opcode = OPCODE_FAST_READ_4B;
					break;
				case M25P80_NORMAL:
					flash->read_opcode = OPCODE_NORM_READ_4B;
					break;
				}
			}
			set_4byte(flash, info->jedec_id, 1);
		}
	}
	else {
		flash->addr_width = 3;
	}

/*	dev_info(&spi->dev, "%s (%d Kbytes)\n", id->name,
			(long long)flash->mtd.size >> 10);
*/

/*	pr_debug(".size = 0x%x (%dMiB) "
			".erasesize = 0x%x (%dKiB) .numeraseregions = %d\n",
		(long long)flash->mtd.size, (long long)(flash->mtd.size >> 20),
		flash->mtd.erasesize, flash->mtd.erasesize / 1024,
		flash->mtd.numeraseregions);
*/
	printk("%s, size=%dMB, ", id->name, (long long)flash->mtd.size >> 20);
	printk("erasesize=%dKB, max_speed_hz=%dHz\n", flash->mtd.erasesize/1024, spi->max_speed_hz);
	printk("auto_mode=%d addr_width=%d erase_opcode=0x%x\n", flash->auto_mode, flash->addr_width, flash->erase_opcode);
/*
	if (flash->mtd.numeraseregions)
		for (i = 0; i < flash->mtd.numeraseregions; i++)
			pr_debug("mtd.eraseregions[%d] = { .offset = 0x%llx, "
				".erasesize = 0x%.8x (%uKiB), "
				".numblocks = %d }\n",
				i, (long long)flash->mtd.eraseregions[i].offset,
				flash->mtd.eraseregions[i].erasesize,
				flash->mtd.eraseregions[i].erasesize / 1024,
				flash->mtd.eraseregions[i].numblocks);
*/

	/* partitions should match sector boundaries; and it may be good to
	 * use readonly partitions for writeprotected sectors (BP2..BP0).
	 */
//	return mtd_device_parse_register(&flash->mtd, NULL, &ppdata,
//			data ? data->parts : NULL,
//			data ? data->nr_parts : 0);
}

#if 0
static int m25p_remove(struct spi_device *spi)
{
	struct m25p	*flash = spi_get_drvdata(spi);

	/* Clean up MTD stuff. */
	return mtd_device_unregister(&flash->mtd);
}


static struct spi_driver m25p80_driver = {
	.driver = {
		.name	= "m25p80",
		.owner	= THIS_MODULE,
	},
	.id_table	= m25p_ids,
	.probe	= m25p_probe,
	.remove	= m25p_remove,

	/* REVISIT: many of these chips have deep power-down modes, which
	 * should clearly be entered on suspend() to minimize power use.
	 * And also when they're otherwise idle...
	 */
};

module_spi_driver(m25p80_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mike Lavender");
MODULE_DESCRIPTION("MTD SPI driver for ST M25Pxx flash chips");
#endif

int flashread (unsigned long dst, unsigned int src, unsigned long length)
{
	int retlen, ret;
	//printk("flashread: chip(uiChip)=%d; dst(pucBuffer)=%x; src(uiAddr)=%x; length=%x\n", 0, dst, src, length);
	//return spi_flash_info[0].pfRead(0, src, length, (unsigned char*)dst);
	ret = g_m25p.mtd._read(&g_m25p.mtd, src, length, &retlen, (unsigned char *)dst);
	//memcpy(dst, (FLASH_BASE+src), length);
	return (0 == ret);
}

int spi_flw_image(unsigned int chip, unsigned int flash_addr_offset ,unsigned char *image_addr, unsigned int image_size)
{
	int retlen, ret, begin, end;
	struct erase_info er_info;

	//printk("spi_flw_image: chip=%x; flash_addr_offset=%x; image_addr=%x; image_size=%x\n", chip, flash_addr_offset, (unsigned int)image_addr, image_size);
	//return spi_flash_info[chip].pfWrite(chip, flash_addr_offset, image_size, image_addr);
	begin = flash_addr_offset / g_m25p.mtd.erasesize;
	end = (flash_addr_offset+image_size) / g_m25p.mtd.erasesize;
	er_info.addr = begin * g_m25p.mtd.erasesize;
	er_info.len = (end - begin + 1) * g_m25p.mtd.erasesize;
	ret = g_m25p.mtd._erase(&g_m25p.mtd, &er_info);
	//printk("ret1=%d\n", ret);
	if (0 == ret)
		ret = g_m25p.mtd._write(&g_m25p.mtd, flash_addr_offset, image_size, &retlen, image_addr);
	//printk("ret=%d\n", ret);
	return (0 == ret);
}

void flash_erase_chip(void)
{
	erase_chip(&g_m25p);
}

void flash_erase_sector(u32 offset)
{
	int ret;
	ret = erase_sector(&g_m25p, offset);
	//printk("ret=%d\n", ret);
}
