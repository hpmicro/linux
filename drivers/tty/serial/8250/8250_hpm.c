// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2024 Jason Tan <tfx2001@outlook.com>
 *
 * HPMicro MCUs UART support
 */

#include <linux/console.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>

#include "8250.h"

#define UART_CFG 0x10
#define UART_CFG_FIFOSIZE_MASK 0x03
#define UART_CFG_FIFOSIZE_16 0x00
#define UART_CFG_FIFOSIZE_32 0x01
#define UART_CFG_FIFOSIZE_64 0x02
#define UART_CFG_FIFOSIZE_128 0x03
#define UART_8250_BASE_OFFSET 0x20

struct hpmicro_uart_data {
	int line;
};

static unsigned int hpmicro_uart_serial_in(struct uart_port *p, int offset)
{
	unsigned int value;

	value = readl(p->membase + (offset << p->regshift) +
		      UART_8250_BASE_OFFSET);
	return value;
}

static void hpmicro_uart_serial_out(struct uart_port *p, int offset, int value)
{
	writel(value,
	       p->membase + (offset << p->regshift) + UART_8250_BASE_OFFSET);
}

static unsigned int hpmicro_uart_get_fifosz(struct uart_port *p)
{
	u32 val;

	val = readl(p->membase + UART_CFG) & UART_CFG_FIFOSIZE_MASK;
	switch (val) {
	case UART_CFG_FIFOSIZE_16:
		return 16;
		break;
	case UART_CFG_FIFOSIZE_32:
		return 32;
		break;
	case UART_CFG_FIFOSIZE_64:
		return 64;
		break;
	case UART_CFG_FIFOSIZE_128:
		return 128;
		break;
	};

	return 1;
}

static int hpmicro_uart_probe(struct platform_device *pdev)
{
	struct uart_8250_port uart = {};
	struct hpmicro_uart_data *data;
	struct resource *regs;
	int ret;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "no registers defined\n");
		return -EINVAL;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "memory alloc failure\n");
		return -ENOMEM;
	}

	spin_lock_init(&uart.port.lock);
	uart.port.type = PORT_16550A;
	uart.port.flags = UPF_FIXED_PORT;
	uart.port.mapbase = regs->start;
	uart.port.serial_in = hpmicro_uart_serial_in,
	uart.port.serial_out = hpmicro_uart_serial_out,
	uart.port.dev = &pdev->dev;

	ret = uart_read_and_validate_port_properties(&uart.port);
	if (ret) {
		dev_err(&pdev->dev, "uart read port properties failed: %d\n", ret);
		return ret;
	}

	uart.port.regshift = 2;

	uart.port.membase =
		devm_ioremap(&pdev->dev, regs->start, resource_size(regs));
	if (!uart.port.membase) {
		dev_err(&pdev->dev, "ioremap failed\n");
		return -ENOMEM;
	}

	uart.port.fifosize = hpmicro_uart_get_fifosz(&uart.port);

	data->line = serial8250_register_8250_port(&uart);
	if (data->line < 0) {
		ret = data->line;
		dev_err(&pdev->dev, "register 8250 port failed: %d\n", ret);
		return ret;
	}

	dev_info(&pdev->dev, "hpm-uart probe success\n");

	platform_set_drvdata(pdev, data);
	return 0;
}

static void hpmicro_uart_remove(struct platform_device *pdev)
{
	struct hpmicro_uart_data *data = platform_get_drvdata(pdev);
	serial8250_unregister_port(data->line);
}

static const struct of_device_id of_match[] = {
	{ .compatible = "hpmicro,hpmicro-uart", .data = NULL },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match);

static struct platform_driver hpmicro_uart_platform_driver = {
	.driver = {
		.name		= "hpmicro-uart",
		.of_match_table	= of_match,
	},
	.probe			= hpmicro_uart_probe,
	.remove_new		= hpmicro_uart_remove,
};

module_platform_driver(hpmicro_uart_platform_driver);

MODULE_AUTHOR("Jason Tan <tfx2001@outlook.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HPMicro MCUs UART driver");
