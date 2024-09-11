// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for HPMicro GPIO Controller
 *
 * Copyright (C) 2024 HPMicro
 */

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>

#define HPM_DI_OFFSET  0x00
#define HPM_DO_OFFSET  0x100
#define HPM_SET_OFFSET 0x104
#define HPM_CLR_OFFSET 0x108
#define HPM_DIR_OUT_OFFSET 0x204
#define HPM_DIR_CLR_OFFSET 0x208
#define HPM_TOGGLE_OFFSET  0x10C
#define HPM_IF_OFFSET  0x300
#define HPM_INT_POLARITY_VAL 0x500
#define HPM_INT_POLARITY_SET 0x504
#define HPM_INT_POLARITY_CLR 0x508
#define HPM_INIT_TYPE_LEVEL_SET_OFFSET 0x604
#define HPM_INIT_TYPE_LEVEL_CLR_OFFSET 0x608
#define HPM_IRQ_ENABLE_OFFSET  0x404
#define HPM_IRQ_DISABLE_OFFSET  0x408

#define HPM_MAX_PORTS        8
#define HPM_MAX_PORT_PINX   32

static const struct of_device_id hpm_gpio_of_table[] = {
	{ .compatible = "hpmicro,gpio", (void *)8 },
	{ }
};
MODULE_DEVICE_TABLE(of, hpm_gpio_of_table);

struct hpm_gpio_port;
struct hpm_gpio {
    unsigned int base;
    struct gpio_chip **chip;
    struct hpm_gpio_port **chip_data;
    unsigned int *dev;
};

struct hpm_gpio_port {
    char *name;
    unsigned int base;
    unsigned int irq_num;
    struct gpio_chip chip;
    struct hpm_gpio *gpio;
};

#define to_hpm_gpio(_gc) \
    (container_of(_gc, struct hpm_gpio_port, chip)->gpio)
/*-------------------------------------------------------------------------*/

static int hpm_gpio_input(struct gpio_chip *chip, unsigned int offset)
{
    struct hpm_gpio_port *port = gpiochip_get_data(chip);
    int status = *(unsigned int *)(port->base + HPM_DIR_CLR_OFFSET) = (1 << offset);
    return status;
}

static int hpm_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
    struct hpm_gpio_port *port = gpiochip_get_data(chip);
    int value = *(unsigned int *)(port->base + HPM_DI_OFFSET) & (1 << offset);
    return (value == 0) ? 0 : 1;
}

static int hpm_gpio_get_multiple(struct gpio_chip *chip, unsigned long *mask,
                unsigned long *bits)
{
    struct hpm_gpio_port *port = gpiochip_get_data(chip);
    int value = *(unsigned int *)(port->base + HPM_DI_OFFSET);

    *bits &= ~*mask;
    *bits |= value & *mask;

    return 0;
}

static int hpm_gpio_dir_out(struct gpio_chip *chip, unsigned int offset, int value)
{
    struct hpm_gpio_port *port = gpiochip_get_data(chip);
    unsigned int bit = 1 << offset;

    *(unsigned int *)(port->base + HPM_DIR_OUT_OFFSET) = bit;

    return 0;
}

static void hpm_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
    struct hpm_gpio_port *port = gpiochip_get_data(chip);
    unsigned int bit = 1 << offset;

    if (value)
        *(unsigned int *)(port->base + HPM_SET_OFFSET) = bit;
    else
        *(unsigned int *)(port->base + HPM_CLR_OFFSET) = bit;
}

static void hpm_gpio_set_multiple(struct gpio_chip *chip, unsigned long *mask,
                 unsigned long *bits)
{
    struct hpm_gpio_port *port = gpiochip_get_data(chip);
    *(unsigned int *)(port->base + HPM_SET_OFFSET) = ((*bits) & (*mask));
}

/*-------------------------------------------------------------------------*/
static void hpm_toggle_trigger(struct hpm_gpio_port *port, unsigned int offs)
{
    u32 pol = *(unsigned int *)(port->base + HPM_INT_POLARITY_VAL);

    *(unsigned int *)(port->base + HPM_INIT_TYPE_LEVEL_SET_OFFSET) = 1 << offs;
    if (pol & (1 << offs) ) {
        *(unsigned int *)(port->base + HPM_INT_POLARITY_CLR) = 1 << offs;
    } else {
        *(unsigned int *)(port->base + HPM_INT_POLARITY_SET) = 1 << offs;
    }
}

static u32 hpm_do_irq(struct hpm_gpio_port *port)
{
    struct gpio_chip *gc = &port->chip;
    unsigned long irq_status;
    irq_hw_number_t hwirq;

    irq_status = *(unsigned int *)(port->base + HPM_IF_OFFSET);
    for_each_set_bit(hwirq, &irq_status, port->chip.ngpio) {
        int gpio_irq = irq_find_mapping(gc->irq.domain, hwirq);
        u32 irq_type = irq_get_trigger_type(gpio_irq);

        generic_handle_irq(gpio_irq);
        *(unsigned int *)(port->base + HPM_IF_OFFSET) = 1 << hwirq;
        if ((irq_type & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH)
            hpm_toggle_trigger(port, hwirq);
    }
    return irq_status;
}

static void hpm_gpio_irq(struct irq_desc *desc)
{
    struct irq_chip *core_chip = irq_desc_get_chip(desc);

    struct hpm_gpio_port *port = irq_desc_get_handler_data(desc);
    chained_irq_enter(core_chip, desc);
    hpm_do_irq(port);
    /* Clear all pending interrupts */
    chained_irq_exit(core_chip, desc);

    return;
}

static void hpm_gpio_irq_eoi(struct irq_data *d)
{
    struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
    struct hpm_gpio_port *chip = gpiochip_get_data(gc);

    unsigned int flag = *(unsigned int *)(chip->base + HPM_IF_OFFSET);
    /* Clear all pending interrupts */
    *(unsigned int *)(chip->base + HPM_IF_OFFSET) = flag;

    irq_chip_eoi_parent(d);
}
/*
 * NOP functions
 */
static void noop(struct irq_data *data) { }

static void hpm_gpio_irq_enable(struct irq_data *d)
{
    struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
    struct hpm_gpio_port *chip = gpiochip_get_data(gc);
    irq_hw_number_t hwirq = irqd_to_hwirq(d);
    *(unsigned int *)(chip->base + HPM_IRQ_ENABLE_OFFSET) = BIT(hwirq);
}

static void hpm_gpio_irq_disable(struct irq_data *d)
{
    struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
    struct hpm_gpio_port *chip = gpiochip_get_data(gc);
    irq_hw_number_t hwirq = irqd_to_hwirq(d);
    *(unsigned int *)(chip->base + HPM_IRQ_DISABLE_OFFSET) = BIT(hwirq);
}

static int hpm_gpio_irq_set_type(struct irq_data *d, u32 type)
{
    struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
    struct hpm_gpio_port *chip = gpiochip_get_data(gc);
    irq_hw_number_t bit = irqd_to_hwirq(d);

    switch (type) {
    case IRQ_TYPE_EDGE_BOTH:
        *(unsigned int *)(chip->base + HPM_INIT_TYPE_LEVEL_SET_OFFSET) = BIT(bit);
        *(unsigned int *)(chip->base + HPM_INT_POLARITY_SET) = BIT(bit);
        break;
    case IRQ_TYPE_EDGE_RISING:
        *(unsigned int *)(chip->base + HPM_INIT_TYPE_LEVEL_SET_OFFSET) = BIT(bit);
        *(unsigned int *)(chip->base + HPM_INT_POLARITY_CLR) = BIT(bit);
        break;
    case IRQ_TYPE_EDGE_FALLING:
        *(unsigned int *)(chip->base + HPM_INIT_TYPE_LEVEL_SET_OFFSET) = BIT(bit);
        *(unsigned int *)(chip->base + HPM_INT_POLARITY_SET) = BIT(bit);
        break;
    case IRQ_TYPE_LEVEL_HIGH:
        *(unsigned int *)(chip->base + HPM_INIT_TYPE_LEVEL_CLR_OFFSET) = BIT(bit);
        *(unsigned int *)(chip->base + HPM_INT_POLARITY_CLR) = BIT(bit);
        break;
    case IRQ_TYPE_LEVEL_LOW:
        *(unsigned int *)(chip->base + HPM_INIT_TYPE_LEVEL_CLR_OFFSET) = BIT(bit);
        *(unsigned int *)(chip->base + HPM_INT_POLARITY_SET) = BIT(bit);
        break;
    }

    if (type & IRQ_TYPE_LEVEL_MASK)
        irq_set_handler_locked(d, handle_level_irq);
    else if (type & IRQ_TYPE_EDGE_BOTH)
        irq_set_handler_locked(d, handle_edge_irq);

    return 0;
}

static const struct irq_chip hpm_gpio_irq_chip = {
    .name = "hpm_gpio",
    .irq_enable = hpm_gpio_irq_enable,
    .irq_disable = hpm_gpio_irq_disable,
    .irq_ack = noop,
    .irq_mask = noop,
    .irq_unmask = noop,
    .irq_set_type = hpm_gpio_irq_set_type,
    .irq_eoi = hpm_gpio_irq_eoi,
    .flags = IRQCHIP_IMMUTABLE,
    GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

/*-------------------------------------------------------------------------*/

static int hpm_gpio_probe(struct platform_device *pdev)
{
    struct hpm_gpio *gpio;
    struct gpio_chip *chip;
    struct hpm_gpio_port *port_data;
    struct device_node *child;
    unsigned int n_ports = 0;
    int status;
    unsigned int idx;
    unsigned int ngpio;
    unsigned int irq;
    device_property_read_u32(&pdev->dev, "n-ports", &n_ports);

    /* Allocate, initialize, and register this gpio_chip. */
    gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
    if (!gpio) {
        status = -ENOMEM;
        goto fail;
    }
    gpio->chip = devm_kzalloc(&pdev->dev, n_ports * sizeof(struct gpio_chip *), GFP_KERNEL);
    if (!gpio->chip) {
        status = -ENOMEM;
        goto fail;
    }
    gpio->base = (unsigned int)devm_platform_ioremap_resource(pdev, 0);

    for_each_child_of_node(pdev->dev.of_node, child)  {
        if (of_property_read_u32(child, "reg", &idx) ||
            idx >= HPM_MAX_PORTS) {
            dev_err(&pdev->dev,
                    "missing/invalid port index for port %d\n", idx);
        } else {
            if (of_property_read_u32(child, "ngpios", &ngpio) ||
                idx >= HPM_MAX_PORT_PINX) {
                dev_info(&pdev->dev, "npgios is lost or ngpio is larger then 32.\n");
                ngpio = 32;
            }
            struct platform_device *new_dev = of_platform_device_create(child, NULL, &pdev->dev);
            if (!new_dev) {
                status = -ENOMEM;
                goto fail;
            }
            dev_info(&pdev->dev, "port%idx, ngpios%d", idx, ngpio);
            port_data = devm_kzalloc(&pdev->dev, sizeof(struct hpm_gpio_port), GFP_KERNEL);
            if (!port_data) {
                status = -ENOMEM;
                goto fail;
            }
            port_data->gpio = gpio;
            port_data->base = (int)((uint)gpio->base + idx * 0x10);
            chip = &port_data->chip;
            gpio->chip[idx] = chip;
            gpio->chip[idx]->label = child->name;
            new_dev->name = child->name;

            chip->can_sleep = false;
            chip->owner = THIS_MODULE;
            chip->get = hpm_gpio_get;
            chip->get_multiple = hpm_gpio_get_multiple;
            chip->set = hpm_gpio_set;
            chip->set_multiple = hpm_gpio_set_multiple;
            chip->direction_input = hpm_gpio_input;
            chip->direction_output = hpm_gpio_dir_out;
            chip->request = gpiochip_generic_request;
            chip->free = gpiochip_generic_free;
            chip->base = -1;
            chip->ngpio = ngpio;
            chip->parent = &new_dev->dev;
            chip->owner = THIS_MODULE;

            /* interrupts */
            irq = irq_of_parse_and_map(child, 0);
            /* Enable irqchip if we have an interrupt */
            if (irq > 0) {
                port_data->irq_num = irq;

                struct gpio_irq_chip *girq;
                girq = &chip->irq;
                gpio_irq_chip_set_chip(girq, &hpm_gpio_irq_chip);
                /* This will let us handle the parent IRQ in the driver */
                girq->parent_handler = hpm_gpio_irq;
                girq->parent_handler_data = port_data;
                girq->num_parents = 1;
                girq->parents = devm_kcalloc(&new_dev->dev, 1, sizeof(*girq->parents), GFP_KERNEL);
                girq->default_type = IRQ_TYPE_NONE;
                girq->parents[0] = irq;
                girq->handler = handle_bad_irq;
                devm_gpiochip_add_data(&new_dev->dev, chip, port_data);
                dev_info(&pdev->dev, "gpio irq_num %d, dev_name%s", port_data->irq_num, dev_name(&pdev->dev));

                if (status < 0)
                    goto fail;
            }
        }
    }

    gpio->dev = (unsigned int *)pdev;

    return 0;

fail:
    dev_dbg(&pdev->dev, "probe error %d for '%s'\n", status,
        pdev->name);
    if (gpio) {
        if (gpio->chip) {
            for (int j = 0; j < n_ports; j++) {
                if (gpio->chip[j]) {
                    if (gpio->chip_data) {
                        if (gpio->chip_data[j]) {
                            devm_kfree(&pdev->dev, gpio->chip_data[j]);
                        }
                    }
                    devm_kfree(&pdev->dev, gpio->chip[j]);
                }
            }
            if (gpio->chip_data) {
                devm_kfree(&pdev->dev, gpio->chip_data);
            }
            devm_kfree(&pdev->dev, gpio->chip);
        }
        devm_kfree(&pdev->dev, gpio);
    }
    return status;
}

static struct platform_driver hpm_gpio_driver = {
    .driver        = {
        .name    = "hpmicro gpio",
        .of_match_table = hpm_gpio_of_table,
    },
    .probe        = hpm_gpio_probe,
};

module_platform_driver(hpm_gpio_driver);

MODULE_DESCRIPTION("Driver for HPMicro GPIO Controller");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zihan XU");
