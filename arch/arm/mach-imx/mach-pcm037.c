/*
 *  Copyright (C) 2008 Sascha Hauer, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/plat-ram.h>
#include <linux/memory.h>
#include <linux/gpio.h>
#include <linux/smsc911x.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/platform_data/at24.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/irq.h>
#include <linux/can/platform/sja1000.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/gfp.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>

#include "common.h"
#include "devices-imx31.h"
#include "ehci.h"
#include "hardware.h"
#include "iomux-mx3.h"
#include "pcm037.h"
#include "ulpi.h"

static enum pcm037_board_variant pcm037_instance = PCM037_PCM970;

static int __init pcm037_variant_setup(char *str)
{
	if (!strcmp("eet", str))
		pcm037_instance = PCM037_EET;
	else if (strcmp("pcm970", str))
		pr_warn("Unknown pcm037 baseboard variant %s\n", str);

	return 1;
}

/* Supported values: "pcm970" (default) and "eet" */
__setup("pcm037_variant=", pcm037_variant_setup);

enum pcm037_board_variant pcm037_variant(void)
{
	return pcm037_instance;
}

/* UART1 with RTS/CTS handshake signals */
static unsigned int pcm037_uart1_handshake_pins[] = {
	MX31_PIN_CTS1__CTS1,
	MX31_PIN_RTS1__RTS1,
	MX31_PIN_TXD1__TXD1,
	MX31_PIN_RXD1__RXD1,
};

/* UART1 without RTS/CTS handshake signals */
static unsigned int pcm037_uart1_pins[] = {
	MX31_PIN_TXD1__TXD1,
	MX31_PIN_RXD1__RXD1,
};

static unsigned int pcm037_pins[] = {
	/* I2C */
	MX31_PIN_CSPI2_MOSI__SCL,
	MX31_PIN_CSPI2_MISO__SDA,
	MX31_PIN_CSPI2_SS2__I2C3_SDA,
	MX31_PIN_CSPI2_SCLK__I2C3_SCL,
	/* SDHC1 */
	MX31_PIN_SD1_DATA3__SD1_DATA3,
	MX31_PIN_SD1_DATA2__SD1_DATA2,
	MX31_PIN_SD1_DATA1__SD1_DATA1,
	MX31_PIN_SD1_DATA0__SD1_DATA0,
	MX31_PIN_SD1_CLK__SD1_CLK,
	MX31_PIN_SD1_CMD__SD1_CMD,
	IOMUX_MODE(MX31_PIN_SCK6, IOMUX_CONFIG_GPIO), /* card detect */
	IOMUX_MODE(MX31_PIN_SFS6, IOMUX_CONFIG_GPIO), /* write protect */
	/* SPI1 */
	MX31_PIN_CSPI1_MOSI__MOSI,
	MX31_PIN_CSPI1_MISO__MISO,
	MX31_PIN_CSPI1_SCLK__SCLK,
	MX31_PIN_CSPI1_SPI_RDY__SPI_RDY,
	MX31_PIN_CSPI1_SS0__SS0,
	MX31_PIN_CSPI1_SS1__SS1,
	MX31_PIN_CSPI1_SS2__SS2,
	/* UART2 */
	MX31_PIN_TXD2__TXD2,
	MX31_PIN_RXD2__RXD2,
	MX31_PIN_CTS2__CTS2,
	MX31_PIN_RTS2__RTS2,
	/* UART3 */
	MX31_PIN_CSPI3_MOSI__RXD3,
	MX31_PIN_CSPI3_MISO__TXD3,
	MX31_PIN_CSPI3_SCLK__RTS3,
	MX31_PIN_CSPI3_SPI_RDY__CTS3,
	/* LAN9217 irq pin */
	IOMUX_MODE(MX31_PIN_GPIO3_1, IOMUX_CONFIG_GPIO),
	/* Onewire */
	MX31_PIN_BATT_LINE__OWIRE,
	/* Framebuffer */
	MX31_PIN_LD0__LD0,
	MX31_PIN_LD1__LD1,
	MX31_PIN_LD2__LD2,
	MX31_PIN_LD3__LD3,
	MX31_PIN_LD4__LD4,
	MX31_PIN_LD5__LD5,
	MX31_PIN_LD6__LD6,
	MX31_PIN_LD7__LD7,
	MX31_PIN_LD8__LD8,
	MX31_PIN_LD9__LD9,
	MX31_PIN_LD10__LD10,
	MX31_PIN_LD11__LD11,
	MX31_PIN_LD12__LD12,
	MX31_PIN_LD13__LD13,
	MX31_PIN_LD14__LD14,
	MX31_PIN_LD15__LD15,
	MX31_PIN_LD16__LD16,
	MX31_PIN_LD17__LD17,
	MX31_PIN_VSYNC3__VSYNC3,
	MX31_PIN_HSYNC__HSYNC,
	MX31_PIN_FPSHIFT__FPSHIFT,
	MX31_PIN_DRDY0__DRDY0,
	MX31_PIN_D3_REV__D3_REV,
	MX31_PIN_CONTRAST__CONTRAST,
	MX31_PIN_D3_SPL__D3_SPL,
	MX31_PIN_D3_CLS__D3_CLS,
	MX31_PIN_LCS0__GPIO3_23,
	/* GPIO */
	IOMUX_MODE(MX31_PIN_ATA_DMACK, IOMUX_CONFIG_GPIO),
	/* OTG */
	MX31_PIN_USBOTG_DATA0__USBOTG_DATA0,
	MX31_PIN_USBOTG_DATA1__USBOTG_DATA1,
	MX31_PIN_USBOTG_DATA2__USBOTG_DATA2,
	MX31_PIN_USBOTG_DATA3__USBOTG_DATA3,
	MX31_PIN_USBOTG_DATA4__USBOTG_DATA4,
	MX31_PIN_USBOTG_DATA5__USBOTG_DATA5,
	MX31_PIN_USBOTG_DATA6__USBOTG_DATA6,
	MX31_PIN_USBOTG_DATA7__USBOTG_DATA7,
	MX31_PIN_USBOTG_CLK__USBOTG_CLK,
	MX31_PIN_USBOTG_DIR__USBOTG_DIR,
	MX31_PIN_USBOTG_NXT__USBOTG_NXT,
	MX31_PIN_USBOTG_STP__USBOTG_STP,
	/* USB host 2 */
	IOMUX_MODE(MX31_PIN_USBH2_CLK, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_USBH2_DIR, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_USBH2_NXT, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_USBH2_STP, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_USBH2_DATA0, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_USBH2_DATA1, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_STXD3, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_SRXD3, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_SCK3, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_SFS3, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_STXD6, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_SRXD6, IOMUX_CONFIG_FUNC),
};

static struct physmap_flash_data pcm037_flash_data = {
	.width  = 2,
};

static struct resource pcm037_flash_resource = {
	.start	= 0xa0000000,
	.end	= 0xa1ffffff,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device pcm037_flash = {
	.name	= "physmap-flash",
	.id	= 0,
	.dev	= {
		.platform_data  = &pcm037_flash_data,
	},
	.resource = &pcm037_flash_resource,
	.num_resources = 1,
};

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct resource smsc911x_resources[] = {
	{
		.start		= MX31_CS1_BASE_ADDR + 0x300,
		.end		= MX31_CS1_BASE_ADDR + 0x300 + SZ_64K - 1,
		.flags		= IORESOURCE_MEM,
	}, {
		/* irq number is run-time assigned */
		.flags		= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct smsc911x_platform_config smsc911x_info = {
	.flags		= SMSC911X_USE_32BIT | SMSC911X_FORCE_INTERNAL_PHY |
			  SMSC911X_SAVE_MAC_ADDRESS,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct platform_device pcm037_eth = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smsc911x_resources),
	.resource	= smsc911x_resources,
	.dev		= {
		.platform_data = &smsc911x_info,
	},
};

static struct platdata_mtd_ram pcm038_sram_data = {
	.bankwidth = 2,
};

static struct resource pcm038_sram_resource = {
	.start = MX31_CS4_BASE_ADDR,
	.end   = MX31_CS4_BASE_ADDR + 512 * 1024 - 1,
	.flags = IORESOURCE_MEM,
};

static struct platform_device pcm037_sram_device = {
	.name = "mtd-ram",
	.id = 0,
	.dev = {
		.platform_data = &pcm038_sram_data,
	},
	.num_resources = 1,
	.resource = &pcm038_sram_resource,
};

static const struct mxc_nand_platform_data
pcm037_nand_board_info __initconst = {
	.width = 1,
	.hw_ecc = 1,
};

static const struct imxi2c_platform_data pcm037_i2c1_data __initconst = {
	.bitrate = 100000,
};

static const struct imxi2c_platform_data pcm037_i2c2_data __initconst = {
	.bitrate = 20000,
};

static struct at24_platform_data board_eeprom = {
	.byte_len = 4096,
	.page_size = 32,
	.flags = AT24_FLAG_ADDR16,
};

static struct i2c_board_info pcm037_i2c_devices[] = {
	{
		I2C_BOARD_INFO("at24", 0x52), /* E0=0, E1=1, E2=0 */
		.platform_data = &board_eeprom,
	}, {
		I2C_BOARD_INFO("pcf8563", 0x51),
	}
};

/* Not connected by default */
#ifdef PCM970_SDHC_RW_SWITCH
static int pcm970_sdhc1_get_ro(struct device *dev)
{
	return gpio_get_value(IOMUX_TO_GPIO(MX31_PIN_SFS6));
}
#endif

#define SDHC1_GPIO_WP	IOMUX_TO_GPIO(MX31_PIN_SFS6)
#define SDHC1_GPIO_DET	IOMUX_TO_GPIO(MX31_PIN_SCK6)

static int pcm970_sdhc1_init(struct device *dev, irq_handler_t detect_irq,
		void *data)
{
	int ret;

	ret = gpio_request(SDHC1_GPIO_DET, "sdhc-detect");
	if (ret)
		return ret;

	gpio_direction_input(SDHC1_GPIO_DET);

#ifdef PCM970_SDHC_RW_SWITCH
	ret = gpio_request(SDHC1_GPIO_WP, "sdhc-wp");
	if (ret)
		goto err_gpio_free;
	gpio_direction_input(SDHC1_GPIO_WP);
#endif

	ret = request_irq(gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_SCK6)), detect_irq,
			IRQF_TRIGGER_FALLING, "sdhc-detect", data);
	if (ret)
		goto err_gpio_free_2;

	return 0;

err_gpio_free_2:
#ifdef PCM970_SDHC_RW_SWITCH
	gpio_free(SDHC1_GPIO_WP);
err_gpio_free:
#endif
	gpio_free(SDHC1_GPIO_DET);

	return ret;
}

static void pcm970_sdhc1_exit(struct device *dev, void *data)
{
	free_irq(gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_SCK6)), data);
	gpio_free(SDHC1_GPIO_DET);
	gpio_free(SDHC1_GPIO_WP);
}

static const struct imxmmc_platform_data sdhc_pdata __initconst = {
#ifdef PCM970_SDHC_RW_SWITCH
	.get_ro = pcm970_sdhc1_get_ro,
#endif
	.init = pcm970_sdhc1_init,
	.exit = pcm970_sdhc1_exit,
};

static struct platform_device *devices[] __initdata = {
	&pcm037_flash,
	&pcm037_sram_device,
};

static const struct fb_videomode fb_modedb[] = {
	{
		/* 240x320 @ 60 Hz Sharp */
		.name		= "Sharp-LQ035Q7DH06-QVGA",
		.refresh	= 60,
		.xres		= 240,
		.yres		= 320,
		.pixclock	= 185925,
		.left_margin	= 9,
		.right_margin	= 16,
		.upper_margin	= 7,
		.lower_margin	= 9,
		.hsync_len	= 1,
		.vsync_len	= 1,
		.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_SHARP_MODE |
				  FB_SYNC_CLK_INVERT | FB_SYNC_CLK_IDLE_EN,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	}, {
		/* 240x320 @ 60 Hz */
		.name		= "TX090",
		.refresh	= 60,
		.xres		= 240,
		.yres		= 320,
		.pixclock	= 38255,
		.left_margin	= 144,
		.right_margin	= 0,
		.upper_margin	= 7,
		.lower_margin	= 40,
		.hsync_len	= 96,
		.vsync_len	= 1,
		.sync		= FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_ACT_HIGH,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	}, {
		/* 240x320 @ 60 Hz */
		.name		= "CMEL-OLED",
		.refresh	= 60,
		.xres		= 240,
		.yres		= 320,
		.pixclock	= 185925,
		.left_margin	= 9,
		.right_margin	= 16,
		.upper_margin	= 7,
		.lower_margin	= 9,
		.hsync_len	= 1,
		.vsync_len	= 1,
		.sync		= FB_SYNC_OE_ACT_HIGH | FB_SYNC_CLK_INVERT,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	},
};

static struct mx3fb_platform_data mx3fb_pdata = {
	.name		= "Sharp-LQ035Q7DH06-QVGA",
	.mode		= fb_modedb,
	.num_modes	= ARRAY_SIZE(fb_modedb),
};

static struct resource pcm970_sja1000_resources[] = {
	{
		.start   = MX31_CS5_BASE_ADDR,
		.end     = MX31_CS5_BASE_ADDR + 0x100 - 1,
		.flags   = IORESOURCE_MEM,
	}, {
		/* irq number is run-time assigned */
		.flags   = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
};

struct sja1000_platform_data pcm970_sja1000_platform_data = {
	.osc_freq	= 16000000,
	.ocr		= OCR_TX1_PULLDOWN | OCR_TX0_PUSHPULL,
	.cdr		= CDR_CBP,
};

static struct platform_device pcm970_sja1000 = {
	.name = "sja1000_platform",
	.dev = {
		.platform_data = &pcm970_sja1000_platform_data,
	},
	.resource = pcm970_sja1000_resources,
	.num_resources = ARRAY_SIZE(pcm970_sja1000_resources),
};

static int pcm037_otg_init(struct platform_device *pdev)
{
	return mx31_initialize_usb_hw(pdev->id, MXC_EHCI_INTERFACE_DIFF_UNI);
}

static struct mxc_usbh_platform_data otg_pdata __initdata = {
	.init	= pcm037_otg_init,
	.portsc	= MXC_EHCI_MODE_ULPI,
};

static int pcm037_usbh2_init(struct platform_device *pdev)
{
	return mx31_initialize_usb_hw(pdev->id, MXC_EHCI_INTERFACE_DIFF_UNI);
}

static struct mxc_usbh_platform_data usbh2_pdata __initdata = {
	.init	= pcm037_usbh2_init,
	.portsc	= MXC_EHCI_MODE_ULPI,
};

static const struct fsl_usb2_platform_data otg_device_pdata __initconst = {
	.operating_mode = FSL_USB2_DR_DEVICE,
	.phy_mode       = FSL_USB2_PHY_ULPI,
};

static bool otg_mode_host __initdata;

static int __init pcm037_otg_mode(char *options)
{
	if (!strcmp(options, "host"))
		otg_mode_host = true;
	else if (!strcmp(options, "device"))
		otg_mode_host = false;
	else
		pr_debug("otg_mode neither \"host\" nor \"device\". "
			"Defaulting to device\n");
	return 1;
}
__setup("otg_mode=", pcm037_otg_mode);

static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vdd33a", "smsc911x"),
	REGULATOR_SUPPLY("vddvario", "smsc911x"),
};

/*
 * Board specific initialization.
 */
static void __init pcm037_init(void)
{
	imx31_soc_init();

	regulator_register_fixed(0, dummy_supplies, ARRAY_SIZE(dummy_supplies));

	mxc_iomux_set_gpr(MUX_PGP_UH2, 1);

	mxc_iomux_setup_multiple_pins(pcm037_pins, ARRAY_SIZE(pcm037_pins),
			"pcm037");

#define H2_PAD_CFG (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_HYS_CMOS \
		| PAD_CTL_ODE_CMOS | PAD_CTL_100K_PU)

	mxc_iomux_set_pad(MX31_PIN_USBH2_CLK, H2_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DIR, H2_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_NXT, H2_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_STP, H2_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DATA0, H2_PAD_CFG); /* USBH2_DATA0 */
	mxc_iomux_set_pad(MX31_PIN_USBH2_DATA1, H2_PAD_CFG); /* USBH2_DATA1 */
	mxc_iomux_set_pad(MX31_PIN_SRXD6, H2_PAD_CFG);	/* USBH2_DATA2 */
	mxc_iomux_set_pad(MX31_PIN_STXD6, H2_PAD_CFG);	/* USBH2_DATA3 */
	mxc_iomux_set_pad(MX31_PIN_SFS3, H2_PAD_CFG);	/* USBH2_DATA4 */
	mxc_iomux_set_pad(MX31_PIN_SCK3, H2_PAD_CFG);	/* USBH2_DATA5 */
	mxc_iomux_set_pad(MX31_PIN_SRXD3, H2_PAD_CFG);	/* USBH2_DATA6 */
	mxc_iomux_set_pad(MX31_PIN_STXD3, H2_PAD_CFG);	/* USBH2_DATA7 */

	if (pcm037_variant() == PCM037_EET)
		mxc_iomux_setup_multiple_pins(pcm037_uart1_pins,
			ARRAY_SIZE(pcm037_uart1_pins), "pcm037_uart1");
	else
		mxc_iomux_setup_multiple_pins(pcm037_uart1_handshake_pins,
			ARRAY_SIZE(pcm037_uart1_handshake_pins),
			"pcm037_uart1");

	platform_add_devices(devices, ARRAY_SIZE(devices));

	imx31_add_imx2_wdt();
	imx31_add_imx_uart0(&uart_pdata);
	/* XXX: should't this have .flags = 0 (i.e. no RTSCTS) on PCM037_EET? */
	imx31_add_imx_uart1(&uart_pdata);
	imx31_add_imx_uart2(&uart_pdata);

	imx31_add_mxc_w1();

	/* I2C adapters and devices */
	i2c_register_board_info(1, pcm037_i2c_devices,
			ARRAY_SIZE(pcm037_i2c_devices));

	imx31_add_imx_i2c1(&pcm037_i2c1_data);
	imx31_add_imx_i2c2(&pcm037_i2c2_data);

	imx31_add_mxc_nand(&pcm037_nand_board_info);
	imx31_add_ipu_core();
	imx31_add_mx3_sdc_fb(&mx3fb_pdata);

	if (otg_mode_host) {
		otg_pdata.otg = imx_otg_ulpi_create(ULPI_OTG_DRVVBUS |
				ULPI_OTG_DRVVBUS_EXT);
		if (otg_pdata.otg)
			imx31_add_mxc_ehci_otg(&otg_pdata);
	}

	usbh2_pdata.otg = imx_otg_ulpi_create(ULPI_OTG_DRVVBUS |
			ULPI_OTG_DRVVBUS_EXT);
	if (usbh2_pdata.otg)
		imx31_add_mxc_ehci_hs(2, &usbh2_pdata);

	if (!otg_mode_host)
		imx31_add_fsl_usb2_udc(&otg_device_pdata);
}

static void __init pcm037_timer_init(void)
{
	mx31_clocks_init(26000000);
}

static void __init pcm037_init_late(void)
{
	int ret;

	/* LAN9217 IRQ pin */
	ret = gpio_request(IOMUX_TO_GPIO(MX31_PIN_GPIO3_1), "lan9217-irq");
	if (!ret) {
		gpio_direction_input(IOMUX_TO_GPIO(MX31_PIN_GPIO3_1));
		smsc911x_resources[1].start =
			gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_GPIO3_1));
		smsc911x_resources[1].end =
			gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_GPIO3_1));
		platform_device_register(&pcm037_eth);
	} else {
		pr_warn("could not get LAN irq gpio\n");
	}

	imx31_add_mxc_mmc(0, &sdhc_pdata);

	pcm970_sja1000_resources[1].start =
			gpio_to_irq(IOMUX_TO_GPIO(IOMUX_PIN(48, 105)));
	pcm970_sja1000_resources[1].end =
			gpio_to_irq(IOMUX_TO_GPIO(IOMUX_PIN(48, 105)));
	platform_device_register(&pcm970_sja1000);

	pcm037_eet_init_devices();
}

MACHINE_START(PCM037, "Phytec Phycore pcm037")
	/* Maintainer: Pengutronix */
	.atag_offset = 0x100,
	.map_io = mx31_map_io,
	.init_early = imx31_init_early,
	.init_irq = mx31_init_irq,
	.init_time	= pcm037_timer_init,
	.init_machine = pcm037_init,
	.init_late = pcm037_init_late,
	.restart	= mxc_restart,
MACHINE_END
