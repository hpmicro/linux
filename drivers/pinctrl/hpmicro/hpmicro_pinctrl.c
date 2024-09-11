// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for HPMicro GPIO Controller
 *
 * Copyright (C) 2024 HPMicro
 */
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <dt-bindings/pinctrl/pinctrl-hpmicro.h>
#include "../core.h"
#include "../pinctrl-utils.h"
#include "../pinmux.h"
#include "../pinconf.h"

#define PAD_OPEN_DRAIN              BIT(8)
#define PAD_SLEW_RATE		        BIT(6)
#define PAD_PRS_MASK                GENMASK(21, 20)
#define PAD_PRS_OFF                 20
#define PAD_KE_MASK                 BIT(16)
#define PAD_KE_OFF                  16
#define PAD_DS_MASK                 GENMASK(2, 0)
#define PAD_DS_OFF                  0
#define PAD_INPUT_SCHMITT_ENABLE	BIT(24)
#define PAD_BIAS_ENABLE		        BIT(17)
#define PAD_BIAS_PULL_UP            BIT(18)
#define PAD_BIAS_MASK \
	(PAD_BIAS_ENABLE | \
	 PAD_BIAS_PULL_UP)
#define PAD_SLEW_RATE_ADDITION_MASK GENMASK(5, 4)
#define PAD_SLEW_RATE_ADDITION_OFF  4

#define NR_GPIOS	(32 * 6)
#define HPM_IOC_FUNC_OFFSET   0
#define HPM_IOC_PAD_OFFSET    4

#define DRIVER_NAME "pinctrl-hpmicro"

static inline unsigned int hpmicro_pinmux_to_gpio(u32 v)
{
	return v & 0xFFF;
}

static inline u32 hpmicro_pinmux_to_func(u32 v)
{
	return (v >> 25) & 0x1F;
}
struct hpmicro_pinctrl {
	struct pinctrl_gpio_range gpios;
	raw_spinlock_t lock;
	int base;
	struct pinctrl_dev *pctl;
	struct mutex mutex; /* serialize adding groups and functions */
};

static const struct pinctrl_pin_desc hpmicro_pins[] = {
	PINCTRL_PIN(PIN_INDEX_PA(0), "PA0"),
	PINCTRL_PIN(PIN_INDEX_PA(1), "PA1"),
	PINCTRL_PIN(PIN_INDEX_PA(2), "PA2"),
	PINCTRL_PIN(PIN_INDEX_PA(3), "PA3"),
	PINCTRL_PIN(PIN_INDEX_PA(4), "PA4"),
	PINCTRL_PIN(PIN_INDEX_PA(5), "PA5"),
	PINCTRL_PIN(PIN_INDEX_PA(6), "PA6"),
	PINCTRL_PIN(PIN_INDEX_PA(7), "PA7"),
	PINCTRL_PIN(PIN_INDEX_PA(8), "PA8"),
	PINCTRL_PIN(PIN_INDEX_PA(9), "PA9"),
	PINCTRL_PIN(PIN_INDEX_PA(10), "PA10"),
	PINCTRL_PIN(PIN_INDEX_PA(11), "PA1"),
	PINCTRL_PIN(PIN_INDEX_PA(12), "PA12"),
	PINCTRL_PIN(PIN_INDEX_PA(13), "PA13"),
	PINCTRL_PIN(PIN_INDEX_PA(14), "PA14"),
	PINCTRL_PIN(PIN_INDEX_PA(15), "PA15"),
	PINCTRL_PIN(PIN_INDEX_PA(16), "PA16"),
	PINCTRL_PIN(PIN_INDEX_PA(17), "PA17"),
	PINCTRL_PIN(PIN_INDEX_PA(18), "PA18"),
	PINCTRL_PIN(PIN_INDEX_PA(19), "PA19"),
	PINCTRL_PIN(PIN_INDEX_PA(20), "PA20"),
	PINCTRL_PIN(PIN_INDEX_PA(21), "PA21"),
	PINCTRL_PIN(PIN_INDEX_PA(22), "PA22"),
	PINCTRL_PIN(PIN_INDEX_PA(23), "PA23"),
	PINCTRL_PIN(PIN_INDEX_PA(24), "PA24"),
	PINCTRL_PIN(PIN_INDEX_PA(25), "PA25"),
	PINCTRL_PIN(PIN_INDEX_PA(26), "PA26"),
	PINCTRL_PIN(PIN_INDEX_PA(27), "PA27"),
	PINCTRL_PIN(PIN_INDEX_PA(28), "PA28"),
	PINCTRL_PIN(PIN_INDEX_PA(29), "PA29"),
	PINCTRL_PIN(PIN_INDEX_PA(30), "PA30"),
	PINCTRL_PIN(PIN_INDEX_PA(31), "PA31"),
	PINCTRL_PIN(PIN_INDEX_PB(0), "PB0"),
	PINCTRL_PIN(PIN_INDEX_PB(1), "PB1"),
	PINCTRL_PIN(PIN_INDEX_PB(2), "PB2"),
	PINCTRL_PIN(PIN_INDEX_PB(3), "PB3"),
	PINCTRL_PIN(PIN_INDEX_PB(4), "PB4"),
	PINCTRL_PIN(PIN_INDEX_PB(5), "PB5"),
	PINCTRL_PIN(PIN_INDEX_PB(6), "PB6"),
	PINCTRL_PIN(PIN_INDEX_PB(7), "PB7"),
	PINCTRL_PIN(PIN_INDEX_PB(8), "PB8"),
	PINCTRL_PIN(PIN_INDEX_PB(9), "PB9"),
	PINCTRL_PIN(PIN_INDEX_PB(10), "PB10"),
	PINCTRL_PIN(PIN_INDEX_PB(11), "PB11"),
	PINCTRL_PIN(PIN_INDEX_PB(12), "PB12"),
	PINCTRL_PIN(PIN_INDEX_PB(13), "PB13"),
	PINCTRL_PIN(PIN_INDEX_PB(14), "PB14"),
	PINCTRL_PIN(PIN_INDEX_PB(15), "PB15"),
	PINCTRL_PIN(PIN_INDEX_PB(16), "PB16"),
	PINCTRL_PIN(PIN_INDEX_PB(17), "PB17"),
	PINCTRL_PIN(PIN_INDEX_PB(18), "PB18"),
	PINCTRL_PIN(PIN_INDEX_PB(19), "PB19"),
	PINCTRL_PIN(PIN_INDEX_PB(20), "PB20"),
	PINCTRL_PIN(PIN_INDEX_PB(21), "PB21"),
	PINCTRL_PIN(PIN_INDEX_PB(22), "PB22"),
	PINCTRL_PIN(PIN_INDEX_PB(23), "PB23"),
	PINCTRL_PIN(PIN_INDEX_PB(24), "PB24"),
	PINCTRL_PIN(PIN_INDEX_PB(25), "PB25"),
	PINCTRL_PIN(PIN_INDEX_PB(26), "PB26"),
	PINCTRL_PIN(PIN_INDEX_PB(27), "PB27"),
	PINCTRL_PIN(PIN_INDEX_PB(28), "PB28"),
	PINCTRL_PIN(PIN_INDEX_PB(29), "PB29"),
	PINCTRL_PIN(PIN_INDEX_PB(30), "PB30"),
	PINCTRL_PIN(PIN_INDEX_PB(31), "PB31"),
	PINCTRL_PIN(PIN_INDEX_PC(0), "PC0"),
	PINCTRL_PIN(PIN_INDEX_PC(1), "PC1"),
	PINCTRL_PIN(PIN_INDEX_PC(2), "PC2"),
	PINCTRL_PIN(PIN_INDEX_PC(3), "PC3"),
	PINCTRL_PIN(PIN_INDEX_PC(4), "PC4"),
	PINCTRL_PIN(PIN_INDEX_PC(5), "PC5"),
	PINCTRL_PIN(PIN_INDEX_PC(6), "PC6"),
	PINCTRL_PIN(PIN_INDEX_PC(7), "PC7"),
	PINCTRL_PIN(PIN_INDEX_PC(8), "PC8"),
	PINCTRL_PIN(PIN_INDEX_PC(9), "PC9"),
	PINCTRL_PIN(PIN_INDEX_PC(10), "PC10"),
	PINCTRL_PIN(PIN_INDEX_PC(11), "PC11"),
	PINCTRL_PIN(PIN_INDEX_PC(12), "PC12"),
	PINCTRL_PIN(PIN_INDEX_PC(13), "PC13"),
	PINCTRL_PIN(PIN_INDEX_PC(14), "PC14"),
	PINCTRL_PIN(PIN_INDEX_PC(15), "PC15"),
	PINCTRL_PIN(PIN_INDEX_PC(16), "PC16"),
	PINCTRL_PIN(PIN_INDEX_PC(17), "PC17"),
	PINCTRL_PIN(PIN_INDEX_PC(18), "PC18"),
	PINCTRL_PIN(PIN_INDEX_PC(19), "PC19"),
	PINCTRL_PIN(PIN_INDEX_PC(20), "PC20"),
	PINCTRL_PIN(PIN_INDEX_PC(21), "PC21"),
	PINCTRL_PIN(PIN_INDEX_PC(22), "PC22"),
	PINCTRL_PIN(PIN_INDEX_PC(23), "PC23"),
	PINCTRL_PIN(PIN_INDEX_PC(24), "PC24"),
	PINCTRL_PIN(PIN_INDEX_PC(25), "PC25"),
	PINCTRL_PIN(PIN_INDEX_PC(26), "PC26"),
	PINCTRL_PIN(PIN_INDEX_PC(27), "PC27"),
	PINCTRL_PIN(PIN_INDEX_PC(28), "PC28"),
	PINCTRL_PIN(PIN_INDEX_PC(29), "PC29"),
	PINCTRL_PIN(PIN_INDEX_PC(30), "PC30"),
	PINCTRL_PIN(PIN_INDEX_PC(31), "PC31"),
	PINCTRL_PIN(PIN_INDEX_PD(0), "PD0"),
	PINCTRL_PIN(PIN_INDEX_PD(1), "PD1"),
	PINCTRL_PIN(PIN_INDEX_PD(2), "PD2"),
	PINCTRL_PIN(PIN_INDEX_PD(3), "PD3"),
	PINCTRL_PIN(PIN_INDEX_PD(4), "PD4"),
	PINCTRL_PIN(PIN_INDEX_PD(5), "PD5"),
	PINCTRL_PIN(PIN_INDEX_PD(6), "PD6"),
	PINCTRL_PIN(PIN_INDEX_PD(7), "PD7"),
	PINCTRL_PIN(PIN_INDEX_PD(8), "PD8"),
	PINCTRL_PIN(PIN_INDEX_PD(9), "PD9"),
	PINCTRL_PIN(PIN_INDEX_PD(10), "PD10"),
	PINCTRL_PIN(PIN_INDEX_PD(11), "PD11"),
	PINCTRL_PIN(PIN_INDEX_PD(12), "PD12"),
	PINCTRL_PIN(PIN_INDEX_PD(13), "PD13"),
	PINCTRL_PIN(PIN_INDEX_PD(14), "PD14"),
	PINCTRL_PIN(PIN_INDEX_PD(15), "PD15"),
	PINCTRL_PIN(PIN_INDEX_PD(16), "PD16"),
	PINCTRL_PIN(PIN_INDEX_PD(17), "PD17"),
	PINCTRL_PIN(PIN_INDEX_PD(18), "PD18"),
	PINCTRL_PIN(PIN_INDEX_PD(19), "PD19"),
	PINCTRL_PIN(PIN_INDEX_PD(20), "PD20"),
	PINCTRL_PIN(PIN_INDEX_PD(21), "PD21"),
	PINCTRL_PIN(PIN_INDEX_PD(22), "PD22"),
	PINCTRL_PIN(PIN_INDEX_PD(23), "PD23"),
	PINCTRL_PIN(PIN_INDEX_PD(24), "PD24"),
	PINCTRL_PIN(PIN_INDEX_PD(25), "PD25"),
	PINCTRL_PIN(PIN_INDEX_PD(26), "PD26"),
	PINCTRL_PIN(PIN_INDEX_PD(27), "PD27"),
	PINCTRL_PIN(PIN_INDEX_PD(28), "PD28"),
	PINCTRL_PIN(PIN_INDEX_PD(29), "PD29"),
	PINCTRL_PIN(PIN_INDEX_PD(30), "PD30"),
	PINCTRL_PIN(PIN_INDEX_PD(31), "PD31"),
	PINCTRL_PIN(PIN_INDEX_PE(0), "PE0"),
	PINCTRL_PIN(PIN_INDEX_PE(1), "PE1"),
	PINCTRL_PIN(PIN_INDEX_PE(2), "PE2"),
	PINCTRL_PIN(PIN_INDEX_PE(3), "PE3"),
	PINCTRL_PIN(PIN_INDEX_PE(4), "PE4"),
	PINCTRL_PIN(PIN_INDEX_PE(5), "PE5"),
	PINCTRL_PIN(PIN_INDEX_PE(6), "PE6"),
	PINCTRL_PIN(PIN_INDEX_PE(7), "PE7"),
	PINCTRL_PIN(PIN_INDEX_PE(8), "PE8"),
	PINCTRL_PIN(PIN_INDEX_PE(9), "PE9"),
	PINCTRL_PIN(PIN_INDEX_PE(10), "PE10"),
	PINCTRL_PIN(PIN_INDEX_PE(11), "PE11"),
	PINCTRL_PIN(PIN_INDEX_PE(12), "PE12"),
	PINCTRL_PIN(PIN_INDEX_PE(13), "PE13"),
	PINCTRL_PIN(PIN_INDEX_PE(14), "PE14"),
	PINCTRL_PIN(PIN_INDEX_PE(15), "PE15"),
	PINCTRL_PIN(PIN_INDEX_PE(16), "PE16"),
	PINCTRL_PIN(PIN_INDEX_PE(17), "PE17"),
	PINCTRL_PIN(PIN_INDEX_PE(18), "PE18"),
	PINCTRL_PIN(PIN_INDEX_PE(19), "PE19"),
	PINCTRL_PIN(PIN_INDEX_PE(20), "PE20"),
	PINCTRL_PIN(PIN_INDEX_PE(21), "PE21"),
	PINCTRL_PIN(PIN_INDEX_PE(22), "PE22"),
	PINCTRL_PIN(PIN_INDEX_PE(23), "PE23"),
	PINCTRL_PIN(PIN_INDEX_PE(24), "PE24"),
	PINCTRL_PIN(PIN_INDEX_PE(25), "PE25"),
	PINCTRL_PIN(PIN_INDEX_PE(26), "PE26"),
	PINCTRL_PIN(PIN_INDEX_PE(27), "PE27"),
	PINCTRL_PIN(PIN_INDEX_PE(28), "PE28"),
	PINCTRL_PIN(PIN_INDEX_PE(29), "PE29"),
	PINCTRL_PIN(PIN_INDEX_PE(30), "PE30"),
	PINCTRL_PIN(PIN_INDEX_PE(31), "PE31"),
	PINCTRL_PIN(PIN_INDEX_PF(0), "PF0"),
	PINCTRL_PIN(PIN_INDEX_PF(1), "PF1"),
	PINCTRL_PIN(PIN_INDEX_PF(2), "PF2"),
	PINCTRL_PIN(PIN_INDEX_PF(3), "PF3"),
	PINCTRL_PIN(PIN_INDEX_PF(4), "PF4"),
	PINCTRL_PIN(PIN_INDEX_PF(5), "PF5"),
	PINCTRL_PIN(PIN_INDEX_PF(6), "PF6"),
	PINCTRL_PIN(PIN_INDEX_PF(7), "PF7"),
	PINCTRL_PIN(PIN_INDEX_PF(8), "PF8"),
	PINCTRL_PIN(PIN_INDEX_PF(9), "PF9"),
	PINCTRL_PIN(PIN_INDEX_PF(10), "PF10"),
	PINCTRL_PIN(PIN_INDEX_PF(11), "PF11"),
	PINCTRL_PIN(PIN_INDEX_PF(12), "PF12"),
	PINCTRL_PIN(PIN_INDEX_PF(13), "PF13"),
	PINCTRL_PIN(PIN_INDEX_PF(14), "PF14"),
	PINCTRL_PIN(PIN_INDEX_PF(15), "PF15"),
	PINCTRL_PIN(PIN_INDEX_PF(16), "PF16"),
	PINCTRL_PIN(PIN_INDEX_PF(17), "PF17"),
	PINCTRL_PIN(PIN_INDEX_PF(18), "PF18"),
	PINCTRL_PIN(PIN_INDEX_PF(19), "PF19"),
	PINCTRL_PIN(PIN_INDEX_PF(20), "PF20"),
	PINCTRL_PIN(PIN_INDEX_PF(21), "PF21"),
	PINCTRL_PIN(PIN_INDEX_PF(22), "PF22"),
	PINCTRL_PIN(PIN_INDEX_PF(23), "PF23"),
	PINCTRL_PIN(PIN_INDEX_PF(24), "PF24"),
	PINCTRL_PIN(PIN_INDEX_PF(25), "PF25"),
	PINCTRL_PIN(PIN_INDEX_PF(26), "PF26"),
	PINCTRL_PIN(PIN_INDEX_PF(27), "PF27"),
	PINCTRL_PIN(PIN_INDEX_PF(28), "PF28"),
	PINCTRL_PIN(PIN_INDEX_PF(29), "PF29"),
	PINCTRL_PIN(PIN_INDEX_PF(30), "PF30"),
	PINCTRL_PIN(PIN_INDEX_PF(31), "PF31"),
	PINCTRL_PIN(PIN_INDEX_PX(0), "PX0"),
	PINCTRL_PIN(PIN_INDEX_PX(1), "PX1"),
	PINCTRL_PIN(PIN_INDEX_PX(2), "PX2"),
	PINCTRL_PIN(PIN_INDEX_PX(3), "PX3"),
	PINCTRL_PIN(PIN_INDEX_PX(4), "PX4"),
	PINCTRL_PIN(PIN_INDEX_PX(5), "PX5"),
	PINCTRL_PIN(PIN_INDEX_PX(6), "PX6"),
	PINCTRL_PIN(PIN_INDEX_PX(7), "PX7"),
	PINCTRL_PIN(PIN_INDEX_PX(8), "PX8"),
	PINCTRL_PIN(PIN_INDEX_PX(9), "PX9"),
	PINCTRL_PIN(PIN_INDEX_PX(10), "PX10"),
	PINCTRL_PIN(PIN_INDEX_PX(11), "PX11"),
	PINCTRL_PIN(PIN_INDEX_PX(12), "PX12"),
	PINCTRL_PIN(PIN_INDEX_PX(13), "PX13"),
	PINCTRL_PIN(PIN_INDEX_PX(14), "PX14"),
	PINCTRL_PIN(PIN_INDEX_PX(15), "PX15"),
	PINCTRL_PIN(PIN_INDEX_PX(16), "PX16"),
	PINCTRL_PIN(PIN_INDEX_PX(17), "PX17"),
	PINCTRL_PIN(PIN_INDEX_PX(18), "PX18"),
	PINCTRL_PIN(PIN_INDEX_PX(19), "PX19"),
	PINCTRL_PIN(PIN_INDEX_PX(20), "PX20"),
	PINCTRL_PIN(PIN_INDEX_PX(21), "PX21"),
	PINCTRL_PIN(PIN_INDEX_PX(22), "PX22"),
	PINCTRL_PIN(PIN_INDEX_PX(23), "PX23"),
	PINCTRL_PIN(PIN_INDEX_PX(24), "PX24"),
	PINCTRL_PIN(PIN_INDEX_PX(25), "PX25"),
	PINCTRL_PIN(PIN_INDEX_PX(26), "PX26"),
	PINCTRL_PIN(PIN_INDEX_PX(27), "PX27"),
	PINCTRL_PIN(PIN_INDEX_PX(28), "PX28"),
	PINCTRL_PIN(PIN_INDEX_PX(29), "PX29"),
	PINCTRL_PIN(PIN_INDEX_PX(30), "PX30"),
	PINCTRL_PIN(PIN_INDEX_PX(31), "PX31"),
	PINCTRL_PIN(PIN_INDEX_PY(0), "PY0"),
	PINCTRL_PIN(PIN_INDEX_PY(1), "PY1"),
	PINCTRL_PIN(PIN_INDEX_PY(2), "PY2"),
	PINCTRL_PIN(PIN_INDEX_PY(3), "PY3"),
	PINCTRL_PIN(PIN_INDEX_PY(4), "PY4"),
	PINCTRL_PIN(PIN_INDEX_PY(5), "PY5"),
	PINCTRL_PIN(PIN_INDEX_PY(6), "PY6"),
	PINCTRL_PIN(PIN_INDEX_PY(7), "PY7"),
	PINCTRL_PIN(PIN_INDEX_PY(8), "PY8"),
	PINCTRL_PIN(PIN_INDEX_PY(9), "PY9"),
	PINCTRL_PIN(PIN_INDEX_PY(10), "PY10"),
	PINCTRL_PIN(PIN_INDEX_PY(11), "PY11"),
	PINCTRL_PIN(PIN_INDEX_PY(12), "PY12"),
	PINCTRL_PIN(PIN_INDEX_PY(13), "PY13"),
	PINCTRL_PIN(PIN_INDEX_PY(14), "PY14"),
	PINCTRL_PIN(PIN_INDEX_PY(15), "PY15"),
	PINCTRL_PIN(PIN_INDEX_PY(16), "PY16"),
	PINCTRL_PIN(PIN_INDEX_PY(17), "PY17"),
	PINCTRL_PIN(PIN_INDEX_PY(18), "PY18"),
	PINCTRL_PIN(PIN_INDEX_PY(19), "PY19"),
	PINCTRL_PIN(PIN_INDEX_PY(20), "PY20"),
	PINCTRL_PIN(PIN_INDEX_PY(21), "PY21"),
	PINCTRL_PIN(PIN_INDEX_PY(22), "PY22"),
	PINCTRL_PIN(PIN_INDEX_PY(23), "PY23"),
	PINCTRL_PIN(PIN_INDEX_PY(24), "PY24"),
	PINCTRL_PIN(PIN_INDEX_PY(25), "PY25"),
	PINCTRL_PIN(PIN_INDEX_PY(26), "PY26"),
	PINCTRL_PIN(PIN_INDEX_PY(27), "PY27"),
	PINCTRL_PIN(PIN_INDEX_PY(28), "PY28"),
	PINCTRL_PIN(PIN_INDEX_PY(29), "PY29"),
	PINCTRL_PIN(PIN_INDEX_PY(30), "PY30"),
	PINCTRL_PIN(PIN_INDEX_PY(31), "PY31"),
	PINCTRL_PIN(PIN_INDEX_PZ(0), "PZ0"),
	PINCTRL_PIN(PIN_INDEX_PZ(1), "PZ1"),
	PINCTRL_PIN(PIN_INDEX_PZ(2), "PZ2"),
	PINCTRL_PIN(PIN_INDEX_PZ(3), "PZ3"),
	PINCTRL_PIN(PIN_INDEX_PZ(4), "PZ4"),
	PINCTRL_PIN(PIN_INDEX_PZ(5), "PZ5"),
	PINCTRL_PIN(PIN_INDEX_PZ(6), "PZ6"),
	PINCTRL_PIN(PIN_INDEX_PZ(7), "PZ7"),
	PINCTRL_PIN(PIN_INDEX_PZ(8), "PZ8"),
	PINCTRL_PIN(PIN_INDEX_PZ(9), "PZ9"),
	PINCTRL_PIN(PIN_INDEX_PZ(10), "PZ10"),
	PINCTRL_PIN(PIN_INDEX_PZ(11), "PZ11"),
	PINCTRL_PIN(PIN_INDEX_PZ(12), "PZ12"),
	PINCTRL_PIN(PIN_INDEX_PZ(13), "PZ13"),
	PINCTRL_PIN(PIN_INDEX_PZ(14), "PZ14"),
	PINCTRL_PIN(PIN_INDEX_PZ(15), "PZ15"),
	PINCTRL_PIN(PIN_INDEX_PZ(16), "PZ16"),
	PINCTRL_PIN(PIN_INDEX_PZ(17), "PZ17"),
	PINCTRL_PIN(PIN_INDEX_PZ(18), "PZ18"),
	PINCTRL_PIN(PIN_INDEX_PZ(19), "PZ19"),
	PINCTRL_PIN(PIN_INDEX_PZ(20), "PZ20"),
	PINCTRL_PIN(PIN_INDEX_PZ(21), "PZ21"),
	PINCTRL_PIN(PIN_INDEX_PZ(22), "PZ22"),
	PINCTRL_PIN(PIN_INDEX_PZ(23), "PZ23"),
	PINCTRL_PIN(PIN_INDEX_PZ(24), "PZ24"),
	PINCTRL_PIN(PIN_INDEX_PZ(25), "PZ25"),
	PINCTRL_PIN(PIN_INDEX_PZ(26), "PZ26"),
	PINCTRL_PIN(PIN_INDEX_PZ(27), "PZ27"),
	PINCTRL_PIN(PIN_INDEX_PZ(28), "PZ28"),
	PINCTRL_PIN(PIN_INDEX_PZ(29), "PZ29"),
	PINCTRL_PIN(PIN_INDEX_PZ(30), "PZ30"),
	PINCTRL_PIN(PIN_INDEX_PZ(31), "PZ31"),
};

static inline u32 hpmicro_padctl_get(struct hpmicro_pinctrl *sfp,
			       unsigned int pin)
{
	return *(unsigned int*)(sfp->base + HPM_IOC_PAD_OFFSET + 8 * pin);
}

static inline u32 hpmicro_func_get(struct hpmicro_pinctrl *sfp,
			       unsigned int pin)
{
	return *(unsigned int*)(sfp->base + HPM_IOC_FUNC_OFFSET + 8 * pin);
}

static inline void hpmicro_padctl_set(struct hpmicro_pinctrl *sfp,
			       unsigned int pin, unsigned int value)
{
	*(unsigned int*)(sfp->base + HPM_IOC_PAD_OFFSET + 8 * pin) = value;
}

static inline void hpmicro_func_set(struct hpmicro_pinctrl *sfp,
			       unsigned int pin, unsigned int value)
{
	*(unsigned int*)(sfp->base + HPM_IOC_FUNC_OFFSET + 8 * pin) = value;
}

static inline void hpmicro_padctl_rmw(struct hpmicro_pinctrl * sfp,
					unsigned int pin, unsigned int mask, unsigned int value)
{
	unsigned long flags;
	raw_spin_lock_irqsave(&sfp->lock, flags);
	unsigned int temp = *(unsigned int*)(sfp->base + HPM_IOC_PAD_OFFSET + 8 * pin);
	temp &= ~mask;
	temp |= value;
	*(unsigned int*)(sfp->base + HPM_IOC_PAD_OFFSET + 8 * pin) = temp;
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
}

#ifdef CONFIG_DEBUG_FS
static void hpmicro_pin_dbg_show(struct pinctrl_dev *pctldev,
				  struct seq_file *s,
				  unsigned int pin)
{
	struct hpmicro_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	unsigned int gpio = pin;
	int func, pad;

	if (gpio >= NR_GPIOS)
		return;

	func = hpmicro_func_get(sfp, pin);
	pad = hpmicro_padctl_get(sfp, pin);

	seq_printf(s, "func=%08x pad=%08x", func, pad);
}
#else
#define hpmicro_pin_dbg_show NULL
#endif

static int hpmicro_dt_node_to_map(struct pinctrl_dev *pctldev,
				   struct device_node *np,
				   struct pinctrl_map **maps,
				   unsigned int *num_maps)
{
	struct hpmicro_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = sfp->pctl->dev;
	struct device_node *child;
	struct pinctrl_map *map;
	const char **pgnames;
	const char *grpname;
	u32 *pinmux;
	int ngroups;
	int *pins;
	int nmaps;
	int ret;

	nmaps = 0;
	ngroups = 0;
	for_each_available_child_of_node(np, child) {
		int npinmux = of_property_count_u32_elems(child, "pinmux");
		int npins   = of_property_count_u32_elems(child, "pins");

		if (npinmux > 0 && npins > 0) {
			dev_err(dev, "invalid pinctrl group %pOFn.%pOFn: both pinmux and pins set\n",
				np, child);
			of_node_put(child);
			return -EINVAL;
		}
		if (npinmux == 0 && npins == 0) {
			dev_err(dev, "invalid pinctrl group %pOFn.%pOFn: neither pinmux nor pins set\n",
				np, child);
			of_node_put(child);
			return -EINVAL;
		}

		if (npinmux > 0)
			nmaps += 2;
		else
			nmaps += 1;
		ngroups += 1;
	}

	pgnames = devm_kcalloc(dev, ngroups, sizeof(*pgnames), GFP_KERNEL);
	if (!pgnames)
		return -ENOMEM;

	map = kcalloc(nmaps, sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	nmaps = 0;
	ngroups = 0;
	mutex_lock(&sfp->mutex);
	for_each_available_child_of_node(np, child) {
		int npins;
		int i;

		grpname = devm_kasprintf(dev, GFP_KERNEL, "%pOFn.%pOFn", np, child);
		if (!grpname) {
			ret = -ENOMEM;
			goto put_child;
		}

		pgnames[ngroups++] = grpname;

		if ((npins = of_property_count_u32_elems(child, "pinmux")) > 0) {
			pins = devm_kcalloc(dev, npins, sizeof(*pins), GFP_KERNEL);
			if (!pins) {
				ret = -ENOMEM;
				goto put_child;
			}

			pinmux = devm_kcalloc(dev, npins, sizeof(*pinmux), GFP_KERNEL);
			if (!pinmux) {
				ret = -ENOMEM;
				goto put_child;
			}

			ret = of_property_read_u32_array(child, "pinmux", pinmux, npins);
			if (ret)
				goto put_child;

			for (i = 0; i < npins; i++) {
				unsigned int gpio = hpmicro_pinmux_to_gpio(pinmux[i]);

				pins[i] = gpio;
			}

			map[nmaps].type = PIN_MAP_TYPE_MUX_GROUP;
			map[nmaps].data.mux.function = np->name;
			map[nmaps].data.mux.group = grpname;
			nmaps += 1;
		} else if ((npins = of_property_count_u32_elems(child, "pins")) > 0) {
			pins = devm_kcalloc(dev, npins, sizeof(*pins), GFP_KERNEL);
			if (!pins) {
				ret = -ENOMEM;
				goto put_child;
			}

			pinmux = NULL;

			for (i = 0; i < npins; i++) {
				u32 v;

				ret = of_property_read_u32_index(child, "pins", i, &v);
				if (ret)
					goto put_child;
				pins[i] = v;
			}
		} else {
			ret = -EINVAL;
			goto put_child;
		}

		ret = pinctrl_generic_add_group(pctldev, grpname, pins, npins, pinmux);
		if (ret < 0) {
			dev_err(dev, "error adding group %s: %d\n", grpname, ret);
			goto put_child;
		}

		ret = pinconf_generic_parse_dt_config(child, pctldev,
						      &map[nmaps].data.configs.configs,
						      &map[nmaps].data.configs.num_configs);
		if (ret) {
			dev_err(dev, "error parsing pin config of group %s: %d\n",
				grpname, ret);
			goto put_child;
		}

		/* don't create a map if there are no pinconf settings */
		if (map[nmaps].data.configs.num_configs == 0)
			continue;

		map[nmaps].type = PIN_MAP_TYPE_CONFIGS_GROUP;
		map[nmaps].data.configs.group_or_pin = grpname;
		nmaps += 1;
	}

	ret = pinmux_generic_add_function(pctldev, np->name, pgnames, ngroups, NULL);
	if (ret < 0) {
		dev_err(dev, "error adding function %s: %d\n", np->name, ret);
		goto free_map;
	}

	*maps = map;
	*num_maps = nmaps;
	mutex_unlock(&sfp->mutex);
	return 0;

put_child:
	of_node_put(child);
free_map:
	pinctrl_utils_free_map(pctldev, map, nmaps);
	mutex_unlock(&sfp->mutex);
	return ret;
}

static const struct pinctrl_ops hpmicro_pinctrl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.pin_dbg_show = hpmicro_pin_dbg_show,
	.dt_node_to_map = hpmicro_dt_node_to_map,
	.dt_free_map = pinctrl_utils_free_map,
};

static int hpmicro_set_mux(struct pinctrl_dev *pctldev,
			    unsigned int fsel, unsigned int gsel)
{
	struct hpmicro_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = sfp->pctl->dev;
	const struct group_desc *group;
	const u32 *pinmux;
	unsigned int i;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	pinmux = group->data;
	for (i = 0; i < group->grp.npins; i++) {
		u32 v = pinmux[i];
		unsigned int gpio = hpmicro_pinmux_to_gpio(v);
		u32 func = hpmicro_pinmux_to_func(v);
		unsigned long flags;

		dev_dbg(dev, "GPIO%u: func=0x%x\n", gpio, func);

		raw_spin_lock_irqsave(&sfp->lock, flags);
		hpmicro_func_set(sfp, gpio, func);
		raw_spin_unlock_irqrestore(&sfp->lock, flags);
	}

	return 0;
}

static const struct pinmux_ops hpmicro_pinmux_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = hpmicro_set_mux,
	.strict = true,
};

#define PIN_CONFIG_HPMICRO_INTER_RESISTANCE_STRENGTH	(PIN_CONFIG_END + 1)
#define PIN_CONFIG_HPMICRO_INTER_KEEPER_CAP	(PIN_CONFIG_END + 2)

static const struct pinconf_generic_params hpmicro_pinconf_custom_params[] = {
	{ "hpmicro,strong-pull-up", PIN_CONFIG_HPMICRO_INTER_RESISTANCE_STRENGTH, 3 },
	{ "hpmicro,keeper-cap", PIN_CONFIG_HPMICRO_INTER_KEEPER_CAP, 1 },
};

#ifdef CONFIG_DEBUG_FS
static const struct pin_config_item hpmicro_pinconf_custom_conf_items[] = {
	PCONFDUMP(PIN_CONFIG_HPMICRO_INTER_RESISTANCE_STRENGTH, " internal resistance strength", NULL, false),
	PCONFDUMP(PIN_CONFIG_HPMICRO_INTER_KEEPER_CAP, " keeper capability enable", NULL, false),
};

static_assert(ARRAY_SIZE(hpmicro_pinconf_custom_conf_items) ==
	      ARRAY_SIZE(hpmicro_pinconf_custom_params));
#else
#define hpmicro_pinconf_custom_conf_items NULL
#endif

static int hpmicro_pinconf_get(struct pinctrl_dev *pctldev,
				unsigned int pin, unsigned long *config)
{
	struct hpmicro_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	int param = pinconf_to_config_param(*config);
	u32 value = hpmicro_padctl_get(sfp, pin);
	bool enabled;
	u32 arg;
	dev_info(pctldev->dev, "pin%d config get %d:", pin, param);
	switch (param) {
	case PIN_CONFIG_HPMICRO_INTER_RESISTANCE_STRENGTH:
		enabled = true;
		arg = (value & PAD_PRS_MASK) >> PAD_PRS_OFF;
		break;
	case PIN_CONFIG_HPMICRO_INTER_KEEPER_CAP:
		enabled = true;
		arg = (value & PAD_KE_MASK) >> PAD_KE_OFF;
		break;
	case PIN_CONFIG_OUTPUT_IMPEDANCE_OHMS:
		enabled = true;
		arg = (value & PAD_DS_MASK) >> PAD_DS_OFF;
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		enabled = value & PAD_OPEN_DRAIN ;
		arg = 1;
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		enabled = !(value & PAD_BIAS_ENABLE);
		arg = 0;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if ((value & PAD_BIAS_ENABLE) && !(value & PAD_BIAS_PULL_UP)) {
			enabled = true;
		}
		arg = 1;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		enabled = value & PAD_BIAS_PULL_UP;
		arg = 1;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		enabled = value & PAD_INPUT_SCHMITT_ENABLE;
		arg = enabled;
		break;
	case PIN_CONFIG_SLEW_RATE:
		enabled = value & PAD_SLEW_RATE;
		arg = (value & PAD_SLEW_RATE_ADDITION_MASK) >> PAD_SLEW_RATE_ADDITION_OFF;
		break;
	default:
		return -ENOTSUPP;
	}
	dev_info(pctldev->dev, "%s, %d", enabled ? "Enable" : "Disable", arg);

	*config = pinconf_to_config_packed(param, arg);
	return enabled ? 0 : -EINVAL;
}

static int hpmicro_pinconf_set(struct pinctrl_dev *pctldev,
				unsigned int pin, unsigned long *config, unsigned int num_configs)
{
	struct hpmicro_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	int param;
	u32 value;
	u32 i;
	dev_info(pctldev->dev, "pin%d config set:", pin);
	for (i = 0; i < num_configs; i++, config++) {
		param = pinconf_to_config_param(*config);
		value = pinconf_to_config_argument(*config);
		dev_info(pctldev->dev, "		%d:%d", param, value);
		switch (param) {
		case PIN_CONFIG_HPMICRO_INTER_RESISTANCE_STRENGTH:
			hpmicro_padctl_rmw(sfp, pin, PAD_PRS_MASK, value << PAD_PRS_OFF);
			break;
		case PIN_CONFIG_HPMICRO_INTER_KEEPER_CAP:
			hpmicro_padctl_rmw(sfp, pin, PAD_KE_MASK, value << PAD_KE_OFF);
			break;
		case PIN_CONFIG_OUTPUT_IMPEDANCE_OHMS:
			hpmicro_padctl_rmw(sfp, pin, PAD_DS_MASK, value << PAD_DS_OFF);
			break;
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			hpmicro_padctl_rmw(sfp, pin, PAD_OPEN_DRAIN, PAD_OPEN_DRAIN);
			break;
		case PIN_CONFIG_BIAS_DISABLE:
			hpmicro_padctl_rmw(sfp, pin, PAD_BIAS_ENABLE, 0);
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			hpmicro_padctl_rmw(sfp, pin, PAD_BIAS_PULL_UP, 0);
			hpmicro_padctl_rmw(sfp, pin, PAD_BIAS_ENABLE, PAD_BIAS_ENABLE);
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			hpmicro_padctl_rmw(sfp, pin, PAD_BIAS_PULL_UP, PAD_BIAS_PULL_UP);
			hpmicro_padctl_rmw(sfp, pin, PAD_BIAS_ENABLE, PAD_BIAS_ENABLE);
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			hpmicro_padctl_rmw(sfp, pin, PAD_INPUT_SCHMITT_ENABLE, PAD_INPUT_SCHMITT_ENABLE);
			break;
		case PIN_CONFIG_SLEW_RATE:
			hpmicro_padctl_rmw(sfp, pin, PAD_SLEW_RATE_ADDITION_MASK, value << PAD_SLEW_RATE_ADDITION_OFF);
			break;
		default:
			return -ENOTSUPP;
		}
	}

	return 0;
}

static int hpmicro_pinconf_group_get(struct pinctrl_dev *pctldev,
				      unsigned int gsel,
				      unsigned long *configs)
{
	return -ENOTSUPP;
}

static int hpmicro_pinconf_group_set(struct pinctrl_dev *pctldev,
				      unsigned int gsel,
				      unsigned long *configs,
				     unsigned int num_configs)
{
	const struct group_desc *group;
	int i;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	for (i = 0; i < group->grp.npins; i++)
		hpmicro_pinconf_set(pctldev, group->grp.pins[i], configs, num_configs);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void hpmicro_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				      struct seq_file *s, unsigned int pin)
{
	struct hpmicro_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	u32 value = hpmicro_padctl_get(sfp, pin);

	seq_printf(s, " (0x%03x)", value);
}
#else
#define hpmicro_pinconf_dbg_show NULL
#endif

static const struct pinconf_ops hpmicro_pinconf_ops = {
	.pin_config_set = hpmicro_pinconf_set,
	.pin_config_get = hpmicro_pinconf_get,
	.pin_config_group_get = hpmicro_pinconf_group_get,
	.pin_config_group_set = hpmicro_pinconf_group_set,
	.pin_config_dbg_show = hpmicro_pinconf_dbg_show,
	.is_generic = true,
};

static struct pinctrl_desc hpmicro_desc = {
	.name = DRIVER_NAME,
	.pins = hpmicro_pins,
	.npins = ARRAY_SIZE(hpmicro_pins),
	.pctlops = &hpmicro_pinctrl_ops,
	.pmxops = &hpmicro_pinmux_ops,
	.confops = &hpmicro_pinconf_ops,
	.owner = THIS_MODULE,
	.num_custom_params = ARRAY_SIZE(hpmicro_pinconf_custom_params),
	.custom_params = hpmicro_pinconf_custom_params,
	.custom_conf_items = hpmicro_pinconf_custom_conf_items,
};

static int hpmicro_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hpmicro_pinctrl *sfp;
	int ret;
	sfp = devm_kzalloc(dev, sizeof(*sfp), GFP_KERNEL);
	if (!sfp)
		return -ENOMEM;

	sfp->base = (int)devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR((const void *)sfp->base))
		return PTR_ERR((const void *)sfp->base);

	platform_set_drvdata(pdev, sfp);
	raw_spin_lock_init(&sfp->lock);
	mutex_init(&sfp->mutex);

	ret = devm_pinctrl_register_and_init(dev, &hpmicro_desc, sfp, &sfp->pctl);
	if (ret)
		return dev_err_probe(dev, ret, "could not register pinctrl driver\n");
	sfp->pctl->dev = dev;
	return pinctrl_enable(sfp->pctl);
}

static const struct of_device_id hpmicro_of_match[] = {
	{ .compatible = "hpmicro,ioc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hpmicro_of_match);

static struct platform_driver hpmicro_pinctrl_driver = {
	.probe = hpmicro_pinctrl_probe,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = hpmicro_of_match,
	},
};
module_platform_driver(hpmicro_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for HPMicro SoCs");
MODULE_AUTHOR("Zihan XU <zihan.xu@hpmicro.com>");
MODULE_LICENSE("GPL v2");
