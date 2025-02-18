/*
 * mpc8xxx_wdt.c - MPC8xx/MPC83xx/MPC86xx watchdog userspace interface
 *
 * Authors: Dave Updegraff <dave@cray.org>
 *	    Kumar Gala <galak@kernel.crashing.org>
 *		Attribution: from 83xx_wst: Florian Schirmer <jolt@tuxbox.org>
 *				..and from sc520_wdt
 * Copyright (c) 2008  MontaVista Software, Inc.
 *                     Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * Note: it appears that you can only actually ENABLE or DISABLE the thing
 * once after POR. Once enabled, you cannot disable, and vice versa.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <sysdev/fsl_soc.h>

struct mpc8xxx_wdt {
	__be32 res0;
	__be32 swcrr; /* System watchdog control register */
#define SWCRR_SWTC 0xFFFF0000 /* Software Watchdog Time Count. */
#define SWCRR_SWEN 0x00000004 /* Watchdog Enable bit. */
#define SWCRR_SWRI 0x00000002 /* Software Watchdog Reset/Interrupt Select bit.*/
#define SWCRR_SWPR 0x00000001 /* Software Watchdog Counter Prescale bit. */
	__be32 swcnr; /* System watchdog count register */
	u8 res1[2];
	__be16 swsrr; /* System watchdog service register */
	u8 res2[0xF0];
};

struct mpc8xxx_wdt_type {
	int prescaler;
	bool hw_enabled;
};

struct mpc8xxx_wdt_ddata {
	struct mpc8xxx_wdt __iomem *base;
	struct watchdog_device wdd;
	struct timer_list timer;
	spinlock_t lock;
};

static u16 timeout = 0xffff;
module_param(timeout, ushort, 0);
MODULE_PARM_DESC(timeout,
	"Watchdog timeout in ticks. (0<timeout<65536, default=65535)");

static bool reset = 1;
module_param(reset, bool, 0);
MODULE_PARM_DESC(reset,
	"Watchdog Interrupt/Reset Mode. 0 = interrupt, 1 = reset");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
		 "(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static void mpc8xxx_wdt_keepalive(struct mpc8xxx_wdt_ddata *ddata)
{
	/* Ping the WDT */
	spin_lock(&ddata->lock);
	out_be16(&ddata->base->swsrr, 0x556c);
	out_be16(&ddata->base->swsrr, 0xaa39);
	spin_unlock(&ddata->lock);
}

static void mpc8xxx_wdt_timer_ping(unsigned long arg)
{
	struct mpc8xxx_wdt_ddata *ddata = (void *)arg;

	mpc8xxx_wdt_keepalive(ddata);
	/* We're pinging it twice faster than needed, just to be sure. */
	mod_timer(&ddata->timer, jiffies + HZ * ddata->wdd.timeout / 2);
}

static int mpc8xxx_wdt_start(struct watchdog_device *w)
{
	struct mpc8xxx_wdt_ddata *ddata =
		container_of(w, struct mpc8xxx_wdt_ddata, wdd);

	u32 tmp = SWCRR_SWEN | SWCRR_SWPR;

	/* Good, fire up the show */
	if (reset)
		tmp |= SWCRR_SWRI;

	tmp |= timeout << 16;

	out_be32(&ddata->base->swcrr, tmp);

	del_timer_sync(&ddata->timer);

	return 0;
}

static int mpc8xxx_wdt_ping(struct watchdog_device *w)
{
	struct mpc8xxx_wdt_ddata *ddata =
		container_of(w, struct mpc8xxx_wdt_ddata, wdd);

	mpc8xxx_wdt_keepalive(ddata);
	return 0;
}

static int mpc8xxx_wdt_stop(struct watchdog_device *w)
{
	struct mpc8xxx_wdt_ddata *ddata =
		container_of(w, struct mpc8xxx_wdt_ddata, wdd);

	mod_timer(&ddata->timer, jiffies);
	return 0;
}

static struct watchdog_info mpc8xxx_wdt_info = {
	.options = WDIOF_KEEPALIVEPING,
	.firmware_version = 1,
	.identity = "MPC8xxx",
};

static struct watchdog_ops mpc8xxx_wdt_ops = {
	.owner = THIS_MODULE,
	.start = mpc8xxx_wdt_start,
	.ping = mpc8xxx_wdt_ping,
	.stop = mpc8xxx_wdt_stop,
};

static int mpc8xxx_wdt_probe(struct platform_device *ofdev)
{
	int ret;
	struct resource *res;
	const struct mpc8xxx_wdt_type *wdt_type;
	struct mpc8xxx_wdt_ddata *ddata;
	u32 freq = fsl_get_sys_freq();
	bool enabled;
	unsigned int timeout_sec;

	wdt_type = of_device_get_match_data(&ofdev->dev);
	if (!wdt_type)
		return -EINVAL;

	if (!freq || freq == -1)
		return -EINVAL;

	ddata = devm_kzalloc(&ofdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	res = platform_get_resource(ofdev, IORESOURCE_MEM, 0);
	ddata->base = devm_ioremap_resource(&ofdev->dev, res);
	if (IS_ERR(ddata->base))
		return PTR_ERR(ddata->base);

	enabled = in_be32(&ddata->base->swcrr) & SWCRR_SWEN;
	if (!enabled && wdt_type->hw_enabled) {
		pr_debug("could not be enabled in software\n");
		return -ENODEV;
	}

	spin_lock_init(&ddata->lock);
	setup_timer(&ddata->timer, mpc8xxx_wdt_timer_ping,
		    (unsigned long)ddata);

	ddata->wdd.info = &mpc8xxx_wdt_info,
	ddata->wdd.ops = &mpc8xxx_wdt_ops,

	/* Calculate the timeout in seconds */
	timeout_sec = (timeout * wdt_type->prescaler) / freq;

	ddata->wdd.timeout = timeout_sec;

	watchdog_set_nowayout(&ddata->wdd, nowayout);

	ret = watchdog_register_device(&ddata->wdd);
	if (ret) {
		pr_err("cannot register watchdog device (err=%d)\n", ret);
		return ret;
	}

	pr_debug("WDT driver for MPC8xxx initialized. mode:%s timeout=%d (%d seconds)\n",
		reset ? "reset" : "interrupt", timeout, timeout_sec);

	/*
	 * If the watchdog was previously enabled or we're running on
	 * MPC8xxx, we should ping the wdt from the kernel until the
	 * userspace handles it.
	 */
	if (enabled)
		mod_timer(&ddata->timer, jiffies);

	platform_set_drvdata(ofdev, ddata);
	return 0;
}

static int mpc8xxx_wdt_remove(struct platform_device *ofdev)
{
	struct mpc8xxx_wdt_ddata *ddata = platform_get_drvdata(ofdev);

	pr_crit("Watchdog removed, expect the %s soon!\n",
		reset ? "reset" : "machine check exception");
	del_timer_sync(&ddata->timer);
	watchdog_unregister_device(&ddata->wdd);

	return 0;
}

static const struct of_device_id mpc8xxx_wdt_match[] = {
	{
		.compatible = "mpc83xx_wdt",
		.data = &(struct mpc8xxx_wdt_type) {
			.prescaler = 0x10000,
		},
	},
	{
		.compatible = "fsl,mpc8610-wdt",
		.data = &(struct mpc8xxx_wdt_type) {
			.prescaler = 0x10000,
			.hw_enabled = true,
		},
	},
	{
		.compatible = "fsl,mpc823-wdt",
		.data = &(struct mpc8xxx_wdt_type) {
			.prescaler = 0x800,
			.hw_enabled = true,
		},
	},
	{},
};
MODULE_DEVICE_TABLE(of, mpc8xxx_wdt_match);

static struct platform_driver mpc8xxx_wdt_driver = {
	.probe		= mpc8xxx_wdt_probe,
	.remove		= mpc8xxx_wdt_remove,
	.driver = {
		.name = "mpc8xxx_wdt",
		.of_match_table = mpc8xxx_wdt_match,
	},
};

static int __init mpc8xxx_wdt_init(void)
{
	return platform_driver_register(&mpc8xxx_wdt_driver);
}
arch_initcall(mpc8xxx_wdt_init);

static void __exit mpc8xxx_wdt_exit(void)
{
	platform_driver_unregister(&mpc8xxx_wdt_driver);
}
module_exit(mpc8xxx_wdt_exit);

MODULE_AUTHOR("Dave Updegraff, Kumar Gala");
MODULE_DESCRIPTION("Driver for watchdog timer in MPC8xx/MPC83xx/MPC86xx "
		   "uProcessors");
MODULE_LICENSE("GPL");
