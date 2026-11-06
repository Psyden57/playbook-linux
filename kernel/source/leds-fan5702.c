// SPDX-License-Identifier: GPL-2.0-only
/*
 * FAN5702 6-channel I2C LED driver -- the PlayBook status LED.
 *
 * The programming model is taken from the vendor's QNX driver
 * (led-fan5702.so) and the bootloader's LED emitter:
 *
 *   GENERAL (0x10)  channel enables: bit1 = red, bit2 = green,
 *                   bit3 = blue; bit0 = ENA (unused here)
 *   CONFIG  (0x20)  written 0x00 once (vendor init)
 *   CH3/4/5 (0x30/0x40/0x50) per-channel 6-bit dimming; the bootloader
 *                   leaves them at 0x36 (54/63) and the QNX driver relies
 *                   on that, so a freshly reset part needs them written.
 *
 * EN is GPIO1_13: low resets the chip, a low->high pulse enables it.
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>

#define FAN5702_REG_GENERAL	0x10
#define FAN5702_REG_CONFIG	0x20
#define FAN5702_REG_CH3		0x30
#define FAN5702_CH_STRIDE	0x10
#define FAN5702_NUM_LEDS	3
#define FAN5702_DIM_DEFAULT	0x36	/* bootloader value, 54/63 */

struct fan5702_led {
	struct led_classdev	cdev;
	struct fan5702		*chip;
	u8			bit;		/* GENERAL enable bit */
};

struct fan5702 {
	struct i2c_client	*client;
	struct mutex		lock;
	struct fan5702_led	leds[FAN5702_NUM_LEDS];
	u8			enable;		/* shadow of GENERAL */
};

static int fan5702_write(struct fan5702 *chip, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(chip->client, reg, val);
}

static int fan5702_brightness_set_blocking(struct led_classdev *cdev,
					   enum led_brightness value)
{
	struct fan5702_led *led = container_of(cdev, struct fan5702_led, cdev);
	struct fan5702 *chip = led->chip;
	int ret;

	mutex_lock(&chip->lock);
	if (value)
		chip->enable |= led->bit;
	else
		chip->enable &= ~led->bit;
	ret = fan5702_write(chip, FAN5702_REG_GENERAL, chip->enable);
	mutex_unlock(&chip->lock);

	return ret;
}

static int fan5702_hw_enable(struct device *dev)
{
	struct gpio_desc *en;

	en = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(en))
		return dev_err_probe(dev, PTR_ERR(en), "enable gpio\n");
	if (!en)
		return 0;

	/* EN low resets the part; pulse it high and let it settle. */
	gpiod_set_value_cansleep(en, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(en, 1);
	usleep_range(1000, 2000);

	return 0;
}

static int fan5702_chip_init(struct fan5702 *chip)
{
	int i, ret;

	ret = fan5702_write(chip, FAN5702_REG_GENERAL, 0x00);
	if (ret)
		return ret;
	ret = fan5702_write(chip, FAN5702_REG_CONFIG, 0x00);
	if (ret)
		return ret;
	for (i = 0; i < FAN5702_NUM_LEDS; i++) {
		ret = fan5702_write(chip, FAN5702_REG_CH3 + i * FAN5702_CH_STRIDE,
				    FAN5702_DIM_DEFAULT);
		if (ret)
			return ret;
	}

	chip->enable = 0;
	return 0;
}

static int fan5702_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct fwnode_handle *child;
	struct fan5702 *chip;
	int ret, i = 0;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	chip->client = client;
	mutex_init(&chip->lock);
	i2c_set_clientdata(client, chip);

	ret = fan5702_hw_enable(dev);
	if (ret)
		return ret;

	ret = fan5702_chip_init(chip);
	if (ret)
		return dev_err_probe(dev, ret, "init failed\n");

	device_for_each_child_node(dev, child) {
		struct led_init_data init_data = {};
		struct fan5702_led *led;
		u32 reg;

		if (fwnode_property_read_u32(child, "reg", &reg) ||
		    reg >= FAN5702_NUM_LEDS) {
			fwnode_handle_put(child);
			return -EINVAL;
		}

		led = &chip->leds[reg];
		led->chip = chip;
		led->bit = BIT(1 + reg);
		led->cdev.max_brightness = 1;	/* on/off; dimming is fixed */
		led->cdev.brightness_set_blocking = fan5702_brightness_set_blocking;

		init_data.fwnode = child;
		init_data.devicename = "playbook";
		init_data.default_label = NULL;

		ret = devm_led_classdev_register_ext(dev, &led->cdev, &init_data);
		if (ret) {
			fwnode_handle_put(child);
			return dev_err_probe(dev, ret, "led %u register\n", reg);
		}
		i++;
	}

	if (!i)
		return dev_err_probe(dev, -EINVAL, "no led children\n");

	return 0;
}

static const struct of_device_id fan5702_of_match[] = {
	{ .compatible = "fairchild,fan5702" },
	{ }
};
MODULE_DEVICE_TABLE(of, fan5702_of_match);

static struct i2c_driver fan5702_driver = {
	.driver = {
		.name = "leds-fan5702",
		.of_match_table = fan5702_of_match,
	},
	.probe = fan5702_probe,
};
module_i2c_driver(fan5702_driver);

MODULE_DESCRIPTION("Fairchild FAN5702 I2C LED driver");
MODULE_LICENSE("GPL");
