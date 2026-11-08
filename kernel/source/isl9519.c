// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intersil ISL9519 narrow-VDC battery charger - BlackBerry PlayBook (winchester)
 *
 * This is the board's charger: an SMBus part at I2C1 0x48070000, 7-bit slave
 * 0x09, driven by RIM's battery_win on TabletOS (report 183).  The TWL6030
 * BCI is present but the stock OS leaves it disabled; it is not the charger.
 *
 * Register facts used here (measured on the live 1917 unit and Intersil
 * AN1570, the ISL9519TQFNEVAL2Z user guide):
 *
 *   - 16-bit SMBus words, LO byte first (i2c_smbus_read_word_data() and
 *     i2c_smbus_write_word_data() already use that order)
 *   - the value registers are masked engineering fields (static RE of RIM's
 *     battery_win, session 31):
 *       0x14 charge current     bits 12:7, 128 mA/LSB
 *       0x15 max system voltage bits 13:4,  16 mV/LSB
 *       0x3E min system voltage bits 14:8, 256 mV/LSB
 *       0x3F input current      bits 12:7, 128 mA/LSB
 *     so 640 mA is 0x0280, 4208 mV is 0x1070, 384 mA is 0x0180 and
 *     3328 mV is 0x0D00 — exactly the values the stock 1917 OS uses.
 *   - RIM writes 0x3E, 0x15, 0x14, 0x3F in that order every ~1 s; 0x44 is
 *     never touched (the "Control" register the vendor reads is 0x3D)
 *   - the output is disabled while 0x14 and 0x15 are zero, so both must be
 *     written non-zero to start charging
 *   - the part shuts down unless 0x14 or 0x15 is rewritten at least every
 *     175 s: the delayed work below refreshes all four every 5 s
 *   - 0xFE VendorID reads 0x0049 ('I'), 0xFF DeviceID reads 0x0003
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>

#define ISL9519_REG_CHARGE_CURRENT	0x14
#define ISL9519_REG_MAX_SYSTEM_VOLTAGE	0x15
#define ISL9519_REG_MIN_SYSTEM_VOLTAGE	0x3E
#define ISL9519_REG_INPUT_CURRENT	0x3F
#define ISL9519_REG_VENDOR_ID		0xFE
#define ISL9519_REG_DEVICE_ID		0xFF

#define ISL9519_VENDOR_ID		0x0049
#define ISL9519_DEVICE_ID		0x0003

/* Field positions and steps, from RIM's battery_win (battery.c). */
#define ISL9519_CHARGE_CURRENT_SHIFT	7	/* 128 mA/LSB, 6 bits */
#define ISL9519_MAX_SYSTEM_SHIFT	4	/* 16 mV/LSB, 10 bits */
#define ISL9519_MIN_SYSTEM_SHIFT	8	/* 256 mV/LSB, 7 bits */
#define ISL9519_INPUT_CURRENT_SHIFT	7	/* 128 mA/LSB, 6 bits */

#define ISL9519_CHARGE_CURRENT_STEP_MA	128
#define ISL9519_MAX_SYSTEM_STEP_MV	16
#define ISL9519_MIN_SYSTEM_STEP_MV	256
#define ISL9519_INPUT_CURRENT_STEP_MA	128

/* The part's watchdog is 175 s; RIM rewrites every ~1 s, we use 5 s. */
#define ISL9519_REFRESH_MS		5000

/* The stock 1917 host-port values (PPS: 640 mA / 4208 / 3328 / 384). */
#define ISL9519_CHARGE_CURRENT_MA	640
#define ISL9519_MAX_SYSTEM_MV		4208
#define ISL9519_MIN_SYSTEM_MV		3328
#define ISL9519_INPUT_CURRENT_MA	384

static unsigned int isl9519_field(unsigned int val, unsigned int step,
				  unsigned int shift)
{
	return (val / step) << shift;
}

struct isl9519 {
	struct i2c_client *client;
	struct power_supply *psy;
	struct delayed_work refresh;
	unsigned int charge_current_ma;
	unsigned int max_system_mv;
	unsigned int min_system_mv;
	unsigned int input_current_ma;
};

static int isl9519_write(struct isl9519 *data, u8 reg, unsigned int val)
{
	int ret;

	ret = i2c_smbus_write_word_data(data->client, reg, val);
	if (ret < 0)
		dev_err(&data->client->dev, "write 0x%02x=0x%04x failed: %d\n",
			reg, val, ret);
	return ret;
}

/*
 * RIM's order: min system voltage, max system voltage, charge current, input
 * current.  Charge current and max system voltage both have to be non-zero
 * for the output to be enabled, and rewriting those two is the watchdog kick.
 */
static int isl9519_program(struct isl9519 *data)
{
	int ret;

	ret = isl9519_write(data, ISL9519_REG_MIN_SYSTEM_VOLTAGE,
			    isl9519_field(data->min_system_mv,
					  ISL9519_MIN_SYSTEM_STEP_MV,
					  ISL9519_MIN_SYSTEM_SHIFT));
	if (ret)
		return ret;

	ret = isl9519_write(data, ISL9519_REG_MAX_SYSTEM_VOLTAGE,
			    isl9519_field(data->max_system_mv,
					  ISL9519_MAX_SYSTEM_STEP_MV,
					  ISL9519_MAX_SYSTEM_SHIFT));
	if (ret)
		return ret;

	ret = isl9519_write(data, ISL9519_REG_CHARGE_CURRENT,
			    isl9519_field(data->charge_current_ma,
					  ISL9519_CHARGE_CURRENT_STEP_MA,
					  ISL9519_CHARGE_CURRENT_SHIFT));
	if (ret)
		return ret;

	return isl9519_write(data, ISL9519_REG_INPUT_CURRENT,
			     isl9519_field(data->input_current_ma,
					   ISL9519_INPUT_CURRENT_STEP_MA,
					   ISL9519_INPUT_CURRENT_SHIFT));
}

static void isl9519_refresh_work(struct work_struct *work)
{
	struct isl9519 *data = container_of(to_delayed_work(work),
					    struct isl9519, refresh);

	isl9519_program(data);

	schedule_delayed_work(&data->refresh,
			      msecs_to_jiffies(ISL9519_REFRESH_MS));
}

static enum power_supply_property isl9519_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
};

static int isl9519_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct isl9519 *data = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		val->intval = data->input_current_ma * 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = data->charge_current_ma * 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = data->max_system_mv * 1000;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int isl9519_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct isl9519 *data = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		data->input_current_ma = val->intval / 1000;
		ret = isl9519_write(data, ISL9519_REG_INPUT_CURRENT,
				    data->input_current_ma);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		data->charge_current_ma = val->intval / 1000;
		ret = isl9519_write(data, ISL9519_REG_CHARGE_CURRENT,
				    data->charge_current_ma);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		data->max_system_mv = val->intval / 1000;
		ret = isl9519_write(data, ISL9519_REG_MAX_SYSTEM_VOLTAGE,
				    data->max_system_mv);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int isl9519_property_is_writeable(struct power_supply *psy,
					 enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		return 1;
	default:
		return 0;
	}
}

static const struct power_supply_desc isl9519_desc = {
	.name			= "isl9519",
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= isl9519_props,
	.num_properties		= ARRAY_SIZE(isl9519_props),
	.get_property		= isl9519_get_property,
	.set_property		= isl9519_set_property,
	.property_is_writeable	= isl9519_property_is_writeable,
};

static int isl9519_probe(struct i2c_client *client)
{
	struct power_supply_config psy_cfg = {};
	struct isl9519 *data;
	int vendor, device, ret;

	vendor = i2c_smbus_read_word_data(client, ISL9519_REG_VENDOR_ID);
	if (vendor < 0)
		return dev_err_probe(&client->dev, vendor,
				     "cannot read VendorID\n");
	device = i2c_smbus_read_word_data(client, ISL9519_REG_DEVICE_ID);
	if (device < 0)
		return dev_err_probe(&client->dev, device,
				     "cannot read DeviceID\n");

	if (vendor != ISL9519_VENDOR_ID || device != ISL9519_DEVICE_ID)
		return dev_err_probe(&client->dev, -ENODEV,
				     "not an ISL9519 (vendor 0x%04x device 0x%04x)\n",
				     vendor, device);

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	data->charge_current_ma = ISL9519_CHARGE_CURRENT_MA;
	data->max_system_mv = ISL9519_MAX_SYSTEM_MV;
	data->min_system_mv = ISL9519_MIN_SYSTEM_MV;
	data->input_current_ma = ISL9519_INPUT_CURRENT_MA;
	i2c_set_clientdata(client, data);

	ret = isl9519_program(data);
	if (ret)
		return ret;

	psy_cfg.drv_data = data;
	psy_cfg.fwnode = dev_fwnode(&client->dev);
	data->psy = devm_power_supply_register(&client->dev, &isl9519_desc,
					       &psy_cfg);
	if (IS_ERR(data->psy))
		return dev_err_probe(&client->dev, PTR_ERR(data->psy),
				     "cannot register power supply\n");

	INIT_DELAYED_WORK(&data->refresh, isl9519_refresh_work);
	schedule_delayed_work(&data->refresh,
			      msecs_to_jiffies(ISL9519_REFRESH_MS));

	dev_info(&client->dev,
		 "ISL9519 charger: %u mA charge, %u/%u mV system, %u mA input\n",
		 data->charge_current_ma, data->min_system_mv,
		 data->max_system_mv, data->input_current_ma);
	return 0;
}

static void isl9519_remove(struct i2c_client *client)
{
	struct isl9519 *data = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&data->refresh);

	/* Explicitly disable the output (also stops the 175 s watchdog). */
	isl9519_write(data, ISL9519_REG_CHARGE_CURRENT, 0);
	isl9519_write(data, ISL9519_REG_MAX_SYSTEM_VOLTAGE, 0);
}

static const struct i2c_device_id isl9519_id[] = {
	{ "isl9519" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, isl9519_id);

static const struct of_device_id isl9519_of_match[] = {
	{ .compatible = "intersil,isl9519" },
	{ }
};
MODULE_DEVICE_TABLE(of, isl9519_of_match);

static struct i2c_driver isl9519_driver = {
	.driver = {
		.name = "isl9519",
		.of_match_table = isl9519_of_match,
	},
	.probe = isl9519_probe,
	.remove = isl9519_remove,
	.id_table = isl9519_id,
};
module_i2c_driver(isl9519_driver);

MODULE_AUTHOR("PlayBook Linux bring-up");
MODULE_DESCRIPTION("Intersil ISL9519 narrow-VDC battery charger");
MODULE_LICENSE("GPL");
