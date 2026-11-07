// SPDX-License-Identifier: GPL-2.0-only
/*
 * Capella CM3217 ambient light sensor - BlackBerry PlayBook (winchester)
 *
 * This part is NOT a register device, which is why mainline cm32181 refuses it.
 * The protocol is taken from RIM's own driver, /base/lib/dll/ls-cm3217.so
 * (static RE, report 181):
 *
 *   - two 7-bit slave addresses on the same bus:
 *       0x10  ALS MSB, and the command byte
 *       0x11  ALS LSB, and the configuration byte
 *   - every transfer is a bare one-byte send or receive
 *     (START, addr<<1|R/W, one byte, STOP) - no register index at all, so
 *     i2c_smbus_read_byte()/write_byte(), never regmap or read_word_data
 *   - enable:  write 0x26 to 0x10;  shutdown: write 0x27 to 0x10 (bit 0 = ALS off)
 *   - the vendor leaves the part powered DOWN and enables it per measurement,
 *     waiting ~200 ms before the count is valid
 *   - count = (byte(0x10) << 8) | byte(0x11);  no ID register is ever read
 *   - integration time = (IT_TABLE[cmd2>>5] >> 1) << cmd1[3:2] ms, where
 *     IT_TABLE = {800,400,266,200,130,100,80,66}; the vendor defaults
 *     (cmd1=0x26, cmd2=0x60) give 200 ms
 *
 * Only one of the two addresses can own an i2c_client, so the driver is bound
 * at 0x10 and creates a dummy client at 0x11 for the LSB.
 */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/iio/iio.h>
#include <linux/math64.h>

#define CM3217_ADDR_MSB		0x10
#define CM3217_ADDR_LSB		0x11

#define CM3217_CMD1_ENABLE	0x26
#define CM3217_CMD1_SHUTDOWN	0x27	/* bit 0 = ALS disable */
#define CM3217_CMD2_DEFAULT	0x60

/*
 * A read taken right after the enable returns the count; waiting longer reads
 * back zero (measured on the bench, 0/1/5/10/20/50/100/200 ms all zero except
 * 0).  Poll a few times because a single read can land in a conversion gap.
 */
#define CM3217_POLL_TRIES	25
#define CM3217_POLL_US		10000

/*
 * lux = entry.lux * raw / entry.adc, from the vendor's cal_table_IT200MS
 * (19 pairs; entry 18 saturates).  Kept as the vendor ships it so the numbers
 * match what TabletOS reported for the same illumination.
 */
static const struct {
	u32 lux;
	u32 adc;
} cm3217_cal_table[] = {
	{     0,     0 }, {     1,     1 }, {     2,     1 }, {     4,     2 },
	{     8,     4 }, {    16,     8 }, {    32,    17 }, {    64,    34 },
	{   128,    67 }, {   256,   135 }, {   512,   269 }, {  1024,   538 },
	{  2048,  1077 }, {  4096,  2154 }, {  8192,  4308 }, { 16384,  8615 },
	{ 32768, 17231 }, { 65536, 34461 }, {131072, 65535 },
};

struct cm3217_data {
	struct i2c_client *msb;
	struct i2c_client *lsb;
	struct mutex lock;		/* one measurement at a time */
};

/* Nearest lower table entry, then lux = lux0 * raw / adc0 (vendor's form). */
static unsigned int cm3217_raw_to_lux(unsigned int raw)
{
	int i;

	if (!raw)
		return 0;

	for (i = ARRAY_SIZE(cm3217_cal_table) - 1; i > 0; i--) {
		if (raw >= cm3217_cal_table[i].adc)
			break;
	}

	if (!cm3217_cal_table[i].adc)
		return 0;

	/* div_u64, not a plain u64 divide: 32-bit ARM has no __aeabi_uldivmod
	 * for modules. */
	return (unsigned int)div_u64((u64)cm3217_cal_table[i].lux * raw,
				     cm3217_cal_table[i].adc);
}

static int cm3217_measure(struct cm3217_data *data, unsigned int *lux)
{
	struct i2c_client *msb = data->msb;
	struct i2c_client *lsb = data->lsb;
	int hi, lo, ret, i;

	mutex_lock(&data->lock);

	ret = i2c_smbus_write_byte(msb, CM3217_CMD1_ENABLE);
	if (ret < 0)
		goto out;

	/*
	 * Read straight away and poll: on this part the count is available
	 * immediately after the enable (a longer wait reads back zero), and a
	 * single read can land in a conversion gap.  Take the first non-zero
	 * pair, up to ~250 ms.
	 */
	ret = -EIO;
	for (i = 0; i < CM3217_POLL_TRIES; i++) {
		lo = i2c_smbus_read_byte(lsb);
		if (lo < 0) {
			ret = lo;
			break;
		}

		hi = i2c_smbus_read_byte(msb);
		if (hi < 0) {
			ret = hi;
			break;
		}

		if ((lo | hi) != 0) {
			*lux = cm3217_raw_to_lux((hi << 8) | lo);
			ret = 0;
			break;
		}

		usleep_range(CM3217_POLL_US, CM3217_POLL_US * 2);
	}

out:
	/* Leave it powered down: leaving it enabled wedges the part's own
	 * automatic-brightness path (measured, report 182). */
	i2c_smbus_write_byte(msb, CM3217_CMD1_SHUTDOWN);
	mutex_unlock(&data->lock);
	return ret;
}

static int cm3217_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct cm3217_data *data = iio_priv(indio_dev);
	unsigned int lux;
	int ret;

	if (mask != IIO_CHAN_INFO_PROCESSED)
		return -EINVAL;

	ret = cm3217_measure(data, &lux);
	if (ret)
		return ret;

	*val = lux;
	return IIO_VAL_INT;
}

static const struct iio_chan_spec cm3217_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	},
};

static const struct iio_info cm3217_info = {
	.read_raw = cm3217_read_raw,
};

static int cm3217_probe(struct i2c_client *client)
{
	struct cm3217_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->msb = client;
	mutex_init(&data->lock);

	/* 0x11 carries the LSB and the configuration byte. */
	data->lsb = i2c_new_dummy_device(client->adapter, CM3217_ADDR_LSB);
	if (IS_ERR(data->lsb))
		return dev_err_probe(&client->dev, PTR_ERR(data->lsb),
				     "cannot claim 0x%02x\n", CM3217_ADDR_LSB);

	/*
	 * The vendor writes cmd2 once at init and then shuts the ALS down; the
	 * part is left enabled only for the duration of a measurement.
	 */
	ret = i2c_smbus_write_byte(data->lsb, CM3217_CMD2_DEFAULT);
	if (ret < 0) {
		dev_err(&client->dev, "cmd2 write failed: %d\n", ret);
		goto err_free_dummy;
	}

	ret = i2c_smbus_write_byte(client, CM3217_CMD1_SHUTDOWN);
	if (ret < 0) {
		dev_err(&client->dev, "shutdown write failed: %d\n", ret);
		goto err_free_dummy;
	}

	indio_dev->name = "cm3217";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = cm3217_channels;
	indio_dev->num_channels = ARRAY_SIZE(cm3217_channels);
	indio_dev->info = &cm3217_info;

	i2c_set_clientdata(client, indio_dev);

	ret = devm_iio_device_register(&client->dev, indio_dev);
	if (ret)
		goto err_free_dummy;

	return 0;

err_free_dummy:
	i2c_unregister_device(data->lsb);
	return ret;
}

static void cm3217_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct cm3217_data *data = iio_priv(indio_dev);

	i2c_unregister_device(data->lsb);
}

static const struct i2c_device_id cm3217_id[] = {
	{ "cm3217" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cm3217_id);

static const struct of_device_id cm3217_of_match[] = {
	{ .compatible = "capella,cm3217" },
	{ }
};
MODULE_DEVICE_TABLE(of, cm3217_of_match);

static struct i2c_driver cm3217_driver = {
	.driver = {
		.name = "cm3217",
		.of_match_table = cm3217_of_match,
	},
	.probe = cm3217_probe,
	.remove = cm3217_remove,
	.id_table = cm3217_id,
};
module_i2c_driver(cm3217_driver);

MODULE_AUTHOR("PlayBook Linux bring-up");
MODULE_DESCRIPTION("Capella CM3217 ambient light sensor (two-address protocol)");
MODULE_LICENSE("GPL");
