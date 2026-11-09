// SPDX-License-Identifier: GPL-2.0-only
/*
 * BlackBerry PlayBook (OMAP4430 "winchester") Munin touchscreen.
 *
 * The panel is a Cypress TrueTouch Gen3 (CY8CTMA3) running RIM's "Munin"
 * protocol.  On I2C2 (0x48072000) at slave 0x65, with 1-byte register
 * addressing: register 0 is a command register, register 1 is status/data.
 *
 * A cold boot leaves the controller in its bootloader (reg1 bit 3).  The
 * vendor driver resets it, reads the 21-byte bootloader block, starts the
 * resident application and then reads an 18-byte report: a status byte and
 * four 4-byte contacts, with 12-bit packed X/Y.
 *
 * The application firmware is resident on this unit (app version 0x020A and
 * build 0x3B match Pegasus_50.hex), so this driver does not upload firmware.
 * Report 194 documents the protocol; DECISIONS 45 records the choice.
 *
 * The controller is not ready while the i2c adapter is still probing on a
 * cold boot, and a blocking retry inside probe delays the boot enough for the
 * USB host to miss the gadget connect (trap 141).  The boot therefore runs in
 * a delayed work that retries until the controller answers.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/workqueue.h>

#define MUNIN_REG_CMD		0x00
#define MUNIN_REG_DATA		0x01

#define MUNIN_CMD_RESET		0x81
#define MUNIN_CMD_START_APP	0x80
#define MUNIN_CMD_COMPLETE	0x02

#define MUNIN_REG_MODE		0x1d
#define MUNIN_REG_THRESHOLD	0x1f

#define MUNIN_CONTACTS		4
#define MUNIN_REPORT_LEN	(2 + 4 * MUNIN_CONTACTS)
#define MUNIN_COORD_MAX		0xfff

#define MUNIN_BOOT_RETRIES	40
#define MUNIN_BOOT_DELAY_MS	500

/*
 * One axis of the vendor's four-point calibration (1917
 * etc/system/config/scaling.conf, the `1024x600:mode=calib` line): the raw
 * coordinate range [raw_min, raw_max] maps linearly onto the display range
 * [disp_min, disp_max].  The endpoints may be reversed; that encodes an
 * inverted axis.
 */
struct munin_cal {
	s32 raw_min;
	s32 raw_max;
	s32 disp_min;
	s32 disp_max;
};

struct munin_ts {
	struct i2c_client *client;
	struct input_dev *input;
	struct delayed_work boot_work;
	struct delayed_work poll;
	struct munin_cal cal_x;
	struct munin_cal cal_y;
	unsigned int panel_w;
	unsigned int panel_h;
	unsigned int poll_ms;
	unsigned int boot_tries;
	int irq;
	bool booted;
	bool using_irq;
};

static int munin_read(struct i2c_client *client, u8 reg, u8 *buf, int len)
{
	struct i2c_msg msgs[2] = {
		{ .addr = client->addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = client->addr, .flags = I2C_M_RD, .len = len, .buf = buf },
	};
	int ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));

	return ret == ARRAY_SIZE(msgs) ? 0 : (ret < 0 ? ret : -EIO);
}

static int munin_write(struct i2c_client *client, u8 reg, const u8 *data, int len)
{
	u8 buf[9];
	struct i2c_msg msg = {
		.addr = client->addr, .flags = 0, .len = len + 1, .buf = buf,
	};
	int ret;

	if (len < 1 || len > ARRAY_SIZE(buf) - 1)
		return -EINVAL;

	buf[0] = reg;
	memcpy(buf + 1, data, len);

	ret = i2c_transfer(client->adapter, &msg, 1);
	return ret == 1 ? 0 : (ret < 0 ? ret : -EIO);
}

static int munin_write8(struct i2c_client *client, u8 reg, u8 value)
{
	return munin_write(client, reg, &value, 1);
}

static unsigned int munin_map(const struct munin_cal *c, unsigned int raw,
			      unsigned int max)
{
	s64 span = (s64)c->raw_max - c->raw_min;
	s64 num, v;

	if (span == 0)
		return min(raw, max);

	num = ((s64)(s32)raw - c->raw_min) * ((s64)c->disp_max - c->disp_min);
	if (num >= 0)
		num += span / 2;
	else
		num -= span / 2;
	v = c->disp_min + div_s64(num, span);

	if (v < 0)
		v = 0;
	if (v > max)
		v = max;

	return (unsigned int)v;
}

/*
 * Reset/start the controller and leave it in application mode.  The vendor
 * writes the complete 21-byte block out to the log; only the status and the
 * panel identity matter here.
 */
static int munin_boot(struct munin_ts *ts)
{
	struct i2c_client *client = ts->client;
	u8 info[21], status;
	int ret, i;

	ret = munin_read(client, MUNIN_REG_DATA, &status, 1);
	if (ret)
		return ret;

	if (status & 0x08) {
		ret = munin_write8(client, MUNIN_REG_CMD, MUNIN_CMD_RESET);
		if (ret)
			return ret;
		msleep(10);

		ret = munin_read(client, MUNIN_REG_DATA, info, sizeof(info));
		if (ret)
			return ret;

		dev_info(&client->dev,
			 "Munin bootloader id 0x%02x ver 0x%02x app 0x%04x build 0x%02x\n",
			 info[1], info[2],
			 (info[7] << 8) | info[8], info[20]);

		if ((info[0] & 0x09) != 0x09) {
			dev_err(&client->dev, "unexpected bootloader status 0x%02x\n",
				info[0]);
			return -ENODEV;
		}

		ret = munin_write8(client, MUNIN_REG_CMD, MUNIN_CMD_START_APP);
		if (ret)
			return ret;
		msleep(50);

		ret = munin_read(client, MUNIN_REG_DATA, &status, 1);
		if (ret)
			return ret;
		if (status & 0x08) {
			dev_err(&client->dev, "controller did not enter app mode\n");
			return -ENODEV;
		}
	}

	ret = munin_write8(client, MUNIN_REG_CMD, MUNIN_CMD_COMPLETE);
	if (ret)
		return ret;

	for (i = 0; i < 10; i++) {
		msleep(100);
		ret = munin_read(client, MUNIN_REG_DATA, &status, 1);
		if (ret)
			return ret;
		if (!(status & 0x01))
			break;
	}
	if (status & 0x01)
		dev_warn(&client->dev, "just-reset bit never cleared\n");

	munin_write8(client, MUNIN_REG_MODE, 1);	/* four-finger mode */
	munin_write8(client, MUNIN_REG_THRESHOLD, 1);	/* active threshold */

	return 0;
}

static void munin_report(struct munin_ts *ts)
{
	struct input_dev *input = ts->input;
	u8 buf[MUNIN_REPORT_LEN];
	int i, ret;

	ret = munin_read(ts->client, MUNIN_REG_DATA, buf, sizeof(buf));
	if (ret)
		return;

	/* The vendor only accepts a sample when the counter parity matches. */
	if ((buf[17] & 1) != ((buf[0] >> 4) & 1))
		return;

	for (i = 0; i < MUNIN_CONTACTS; i++) {
		int o = 1 + i * 4;
		unsigned int xr = buf[o] | ((buf[o + 2] & 0x0f) << 8);
		unsigned int yr = buf[o + 1] | ((buf[o + 2] & 0xf0) << 4);
		bool down = buf[o + 3] != 0 && yr != MUNIN_COORD_MAX;

		input_mt_slot(input, i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, down);
		if (down) {
			input_report_abs(input, ABS_MT_POSITION_X,
					 munin_map(&ts->cal_x, xr,
						   ts->panel_w - 1));
			input_report_abs(input, ABS_MT_POSITION_Y,
					 munin_map(&ts->cal_y, yr,
						   ts->panel_h - 1));
		}
	}

	input_mt_sync_frame(input);
	input_sync(input);
}

static irqreturn_t munin_irq(int irq, void *data)
{
	struct munin_ts *ts = data;

	if (ts->booted)
		munin_report(ts);

	return IRQ_HANDLED;
}

static void munin_boot_work(struct work_struct *work)
{
	struct munin_ts *ts = container_of(to_delayed_work(work),
					   struct munin_ts, boot_work);
	int ret;

	ret = munin_boot(ts);
	if (ret) {
		ts->boot_tries++;
		if (ts->boot_tries < MUNIN_BOOT_RETRIES) {
			schedule_delayed_work(&ts->boot_work,
					      msecs_to_jiffies(MUNIN_BOOT_DELAY_MS));
		} else {
			dev_err(&ts->client->dev, "touch did not boot: %d\n", ret);
		}
		return;
	}

	ts->booted = true;

	/*
	 * The IRQ is only wired once the controller is in app mode: the line is
	 * quiet in the bootloader and a request during adaptor probing would
	 * race the first (failing) I2C read.  Fall back to polling when the
	 * binding has no interrupt or the request fails.
	 */
	if (ts->irq > 0) {
		ret = request_threaded_irq(ts->irq, NULL, munin_irq,
					   IRQF_ONESHOT | IRQF_TRIGGER_FALLING,
					   dev_name(&ts->client->dev), ts);
		if (!ret) {
			ts->using_irq = true;
			dev_info(&ts->client->dev,
				 "Munin touchscreen ready (irq %d)\n", ts->irq);
		} else {
			dev_warn(&ts->client->dev,
				 "Munin irq %d unavailable (%d), polling\n",
				 ts->irq, ret);
		}
	}

	if (!ts->using_irq) {
		dev_info(&ts->client->dev,
			 "Munin touchscreen ready (poll %u ms)\n", ts->poll_ms);
		if (ts->poll_ms)
			schedule_delayed_work(&ts->poll, 0);
	}
}

static void munin_poll(struct work_struct *work)
{
	struct munin_ts *ts = container_of(to_delayed_work(work),
					   struct munin_ts, poll);

	if (ts->booted) {
		munin_report(ts);
		if (ts->poll_ms)
			schedule_delayed_work(&ts->poll,
					      msecs_to_jiffies(ts->poll_ms));
	}
}

static int munin_probe(struct i2c_client *client)
{
	struct munin_ts *ts;
	struct input_dev *input;
	u32 v[4];
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->client = client;
	i2c_set_clientdata(client, ts);

	/* Defaults: the 1917 scaling.conf `1024x600:mode=calib` line. */
	ts->cal_x = (struct munin_cal){ 172, 1108, 102, 922 };
	ts->cal_y = (struct munin_cal){ 110, 658, 60, 540 };
	ts->panel_w = 1024;
	ts->panel_h = 600;

	if (!device_property_read_u32_array(&client->dev, "rim,x-calibration",
					    v, 4))
		ts->cal_x = (struct munin_cal){ v[0], v[1], v[2], v[3] };
	if (!device_property_read_u32_array(&client->dev, "rim,y-calibration",
					    v, 4))
		ts->cal_y = (struct munin_cal){ v[0], v[1], v[2], v[3] };
	device_property_read_u32(&client->dev, "touchscreen-size-x",
				 &ts->panel_w);
	device_property_read_u32(&client->dev, "touchscreen-size-y",
				 &ts->panel_h);

	input = devm_input_allocate_device(&client->dev);
	if (!input)
		return -ENOMEM;
	ts->input = input;

	input->name = "PlayBook Munin Touch";
	input->id.bustype = BUS_I2C;
	__set_bit(INPUT_PROP_DIRECT, input->propbit);

	input_set_abs_params(input, ABS_MT_POSITION_X, 0, ts->panel_w - 1, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, ts->panel_h - 1, 0, 0);
	input_set_abs_params(input, ABS_X, 0, ts->panel_w - 1, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, ts->panel_h - 1, 0, 0);

	ret = input_mt_init_slots(input, MUNIN_CONTACTS, INPUT_MT_DIRECT);
	if (ret)
		return ret;

	ret = input_register_device(input);
	if (ret)
		return ret;

	ts->irq = client->irq;
	ts->poll_ms = 20;
	device_property_read_u32(&client->dev, "poll-interval-ms", &ts->poll_ms);

	INIT_DELAYED_WORK(&ts->boot_work, munin_boot_work);
	INIT_DELAYED_WORK(&ts->poll, munin_poll);
	schedule_delayed_work(&ts->boot_work,
			      msecs_to_jiffies(MUNIN_BOOT_DELAY_MS));

	return 0;
}

static void munin_remove(struct i2c_client *client)
{
	struct munin_ts *ts = i2c_get_clientdata(client);

	if (!ts)
		return;
	cancel_delayed_work_sync(&ts->boot_work);
	if (ts->using_irq)
		free_irq(ts->irq, ts);
	cancel_delayed_work_sync(&ts->poll);
}

static const struct of_device_id munin_of_match[] = {
	{ .compatible = "rim,playbook-munin" },
	{ }
};
MODULE_DEVICE_TABLE(of, munin_of_match);

static const struct i2c_device_id munin_id[] = {
	{ "playbook-munin", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, munin_id);

static struct i2c_driver munin_driver = {
	.driver = {
		.name = "playbook-munin",
		.of_match_table = munin_of_match,
	},
	.probe = munin_probe,
	.remove = munin_remove,
	.id_table = munin_id,
};
module_i2c_driver(munin_driver);

MODULE_AUTHOR("PlayBook Linux bring-up");
MODULE_DESCRIPTION("BlackBerry PlayBook Munin (CY8CTMA3 Gen3) touchscreen");
MODULE_LICENSE("GPL");
