// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Samsung Electronics Co., Ltd
 *
 * Authors:
 *	Andrzej Hajda <a.hajda@samsung.com>
 *	Maciej Purski <m.purski@samsung.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>

#define FLD_MASK(start, end)    (((1 << ((start) - (end) + 1)) - 1) << (end))
#define FLD_VAL(val, start, end) (((val) << (end)) & FLD_MASK(start, end))

/* PPI layer registers */
#define PPI_STARTPPI		0x0104 /* START control bit */
#define PPI_LPTXTIMECNT		0x0114 /* LPTX timing signal */
#define PPI_LANEENABLE		0x0134 /* Enables each lane */
#define PPI_TX_RX_TA		0x013C /* BTA timing parameters */
#define PPI_D0S_CLRSIPOCOUNT	0x0164 /* Assertion timer for Lane 0 */
#define PPI_D1S_CLRSIPOCOUNT	0x0168 /* Assertion timer for Lane 1 */
#define PPI_D2S_CLRSIPOCOUNT	0x016C /* Assertion timer for Lane 2 */
#define PPI_D3S_CLRSIPOCOUNT	0x0170 /* Assertion timer for Lane 3 */
#define PPI_START_FUNCTION	1

/* DSI layer registers */
#define DSI_STARTDSI		0x0204 /* START control bit of DSI-TX */
#define DSI_LANEENABLE		0x0210 /* Enables each lane */
#define DSI_RX_START		1

/* Video path registers */
#define VP_CTRL			0x0450 /* Video Path Control */
#define VP_CTRL_MSF		BIT(0) /* Magic square in RGB666 */
#define VP_CTRL_VTGEN		BIT(4) /* Use chip clock for timing */
#define VP_CTRL_EVTMODE		BIT(5) /* Event mode */
#define VP_CTRL_RGB888		BIT(8) /* RGB888 mode */
#define VP_CTRL_VSDELAY(v)	FLD_VAL(v, 31, 20) /* VSYNC delay */
#define VP_CTRL_HSPOL		BIT(17) /* Polarity of HSYNC signal */
#define VP_CTRL_DEPOL		BIT(18) /* Polarity of DE signal */
#define VP_CTRL_VSPOL		BIT(19) /* Polarity of VSYNC signal */
#define VP_HTIM1		0x0454 /* Horizontal Timing Control 1 */
#define VP_HTIM1_HBP(v)		FLD_VAL(v, 24, 16)
#define VP_HTIM1_HSYNC(v)	FLD_VAL(v, 8, 0)
#define VP_HTIM2		0x0458 /* Horizontal Timing Control 2 */
#define VP_HTIM2_HFP(v)		FLD_VAL(v, 24, 16)
#define VP_HTIM2_HACT(v)	FLD_VAL(v, 10, 0)
#define VP_VTIM1		0x045C /* Vertical Timing Control 1 */
#define VP_VTIM1_VBP(v)		FLD_VAL(v, 23, 16)
#define VP_VTIM1_VSYNC(v)	FLD_VAL(v, 7, 0)
#define VP_VTIM2		0x0460 /* Vertical Timing Control 2 */
#define VP_VTIM2_VFP(v)		FLD_VAL(v, 23, 16)
#define VP_VTIM2_VACT(v)	FLD_VAL(v, 10, 0)
#define VP_VFUEN		0x0464 /* Video Frame Timing Update Enable */

/* LVDS registers */
#define LV_MX0003		0x0480 /* Mux input bit 0 to 3 */
#define LV_MX0407		0x0484 /* Mux input bit 4 to 7 */
#define LV_MX0811		0x0488 /* Mux input bit 8 to 11 */
#define LV_MX1215		0x048C /* Mux input bit 12 to 15 */
#define LV_MX1619		0x0490 /* Mux input bit 16 to 19 */
#define LV_MX2023		0x0494 /* Mux input bit 20 to 23 */
#define LV_MX2427		0x0498 /* Mux input bit 24 to 27 */
#define LV_MX(b0, b1, b2, b3)	(FLD_VAL(b0, 4, 0) | FLD_VAL(b1, 12, 8) | \
				FLD_VAL(b2, 20, 16) | FLD_VAL(b3, 28, 24))

/* Input bit numbers used in mux registers */
enum {
	LVI_R0,
	LVI_R1,
	LVI_R2,
	LVI_R3,
	LVI_R4,
	LVI_R5,
	LVI_R6,
	LVI_R7,
	LVI_G0,
	LVI_G1,
	LVI_G2,
	LVI_G3,
	LVI_G4,
	LVI_G5,
	LVI_G6,
	LVI_G7,
	LVI_B0,
	LVI_B1,
	LVI_B2,
	LVI_B3,
	LVI_B4,
	LVI_B5,
	LVI_B6,
	LVI_B7,
	LVI_HS,
	LVI_VS,
	LVI_DE,
	LVI_L0
};

#define LV_CFG			0x049C /* LVDS Configuration */
#define LV_PHY0			0x04A0 /* LVDS PHY 0 */
#define LV_PHY0_RST(v)		FLD_VAL(v, 22, 22) /* PHY reset */
#define LV_PHY0_IS(v)		FLD_VAL(v, 15, 14)
#define LV_PHY0_ND(v)		FLD_VAL(v, 4, 0) /* Frequency range select */
#define LV_PHY0_PRBS_ON(v)	FLD_VAL(v, 20, 16) /* Clock/Data Flag pins */

/* System registers */
#define SYS_RST			0x0504 /* System Reset */
#define SYS_ID			0x0580 /* System ID */

#define SYS_RST_I2CS		BIT(0) /* Reset I2C-Slave controller */
#define SYS_RST_I2CM		BIT(1) /* Reset I2C-Master controller */
#define SYS_RST_LCD		BIT(2) /* Reset LCD controller */
#define SYS_RST_BM		BIT(3) /* Reset Bus Management controller */
#define SYS_RST_DSIRX		BIT(4) /* Reset DSI-RX and App controller */
#define SYS_RST_REG		BIT(5) /* Reset Register module */

#define LPX_PERIOD		2
#define TTA_SURE		3
#define TTA_GET			0x20000

/* Lane enable PPI and DSI register bits */
#define LANEENABLE_CLEN		BIT(0)
#define LANEENABLE_L0EN		BIT(1)
#define LANEENABLE_L1EN		BIT(2)
#define LANEENABLE_L2EN		BIT(3)
#define LANEENABLE_L3EN		BIT(4)

/* LVCFG fields */
#define LV_CFG_LVEN		BIT(0)
#define LV_CFG_LVDLINK		BIT(1)
#define LV_CFG_CLKPOL1		BIT(2)
#define LV_CFG_CLKPOL2		BIT(3)

static const char * const tc358764_supplies[] = {
	"vddc", "vddio", "vddlvds"
};

struct tc358764 {
	struct device *dev;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct regulator_bulk_data supplies[ARRAY_SIZE(tc358764_supplies)];
	struct gpio_desc *gpio_reset;
	int error;
	bool warned;
	bool inited;
};

static int tc358764_clear_error(struct tc358764 *ctx)
{
	int ret = ctx->error;

	ctx->error = 0;
	return ret;
}

static void tc358764_read(struct tc358764 *ctx, u16 addr, u32 *val)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error)
		return;

	cpu_to_le16s(&addr);
	ret = mipi_dsi_generic_read(dsi, &addr, sizeof(addr), val, sizeof(*val));
	if (ret >= 0)
		le32_to_cpus(val);

	dev_dbg(ctx->dev, "read: addr=0x%04x data=0x%08x\n", addr, *val);
}

static void tc358764_write(struct tc358764 *ctx, u16 addr, u32 val)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	u8 data[6];

	if (ctx->error)
		return;

	data[0] = addr;
	data[1] = addr >> 8;
	data[2] = val;
	data[3] = val >> 8;
	data[4] = val >> 16;
	data[5] = val >> 24;

	ret = mipi_dsi_generic_write(dsi, data, sizeof(data));
	if (ret < 0) {
		/*
		 * PlayBook bring-up.  The TC358764's reset line is unknown, so
		 * the bridge starts in whatever state the previous OS left it
		 * in, and it does not answer the DSI bus turnaround (BTA) until
		 * it has been programmed.  The write packet itself is still
		 * transmitted; only the ack times out (-EIO).  Aborting the
		 * sequence here (ctx->error) leaves the bridge completely
		 * unconfigured — measured: "write 0x013c failed: -5" then
		 * "error initializing bridge", and a black panel.
		 *
		 * So: report the first failure once, then keep programming.
		 */
		if (!ctx->warned) {
			dev_err(ctx->dev,
				"write 0x%04x <- 0x%08x failed: %d (continuing)\n",
				addr, val, (int)ret);
			ctx->warned = true;
		}
	}
}

static inline struct tc358764 *bridge_to_tc358764(struct drm_bridge *bridge)
{
	return container_of(bridge, struct tc358764, bridge);
}

/*
 * PlayBook (winchester): reproduce QNX's bridge-GPIO self-test
 * (winch_lcdctl 0x196dc, report 126).  It writes the bridge's GPIO block
 * (0x0520 = 5), then cycles 0x0524 through 1/4/5/0; bit 0 is wired to OMAP
 * GPIO2_4 (GPIO36) and bit 2 to GPIO6_11 (GPIO171).  Reading GPIO2_DATAIN
 * bit 4 after each write is a delivery test that needs no BTA: if it does
 * not follow 0x0524 bit 0, the bridge is not receiving our generic writes.
 */
static void pb_bridge_selftest(struct tc358764 *ctx)
{
	static const u32 vals[] = { 1, 4, 5, 0 };
	void __iomem *gpio2;
	u32 v;
	int i;

	gpio2 = ioremap(0x48055000, 0x1000);
	if (!gpio2) {
		dev_err(ctx->dev, "selftest: cannot map GPIO2\n");
		return;
	}

	tc358764_write(ctx, 0x0520, 5);
	usleep_range(1000, 2000);
	for (i = 0; i < ARRAY_SIZE(vals); i++) {
		tc358764_write(ctx, 0x0524, vals[i]);
		usleep_range(1000, 2000);
		v = readl(gpio2 + 0x138);	/* GPIO2_DATAIN */
		dev_info(ctx->dev,
			 "selftest 0x0524=%u -> GPIO2_DATAIN=0x%08x GPIO36=%u\n",
			 vals[i], v, !!(v & BIT(4)));
	}
	iounmap(gpio2);
}

static int tc358764_init(struct tc358764 *ctx)
{
	ctx->error = 0;

	/*
	 * PlayBook (winchester): the reference DOES program the bridge.
	 *
	 * The splash sequencer in winch_lcdctl calls the function at vaddr
	 * 0x13e50 from 0x1938c (between the DSI PHY/PLL programming and the
	 * DSI VC enable).  It writes a complete register set over DSI
	 * generic long-write packets, including LV_CFG.LVEN.  Reports 121
	 * §12 and 122 concluded "nothing programs it"; that was wrong - the
	 * call is present in the splash path and this is its exact sequence
	 * (recovered instruction by instruction; see report 125).
	 *
	 * Do NOT use SYS_RST <- 0x1f: the reference writes SYS_RST_LCD (4),
	 * and the 0x1f value degrades the reference display until a battery
	 * pull (trap 28).
	 */
	/*
	 * PPI_TX_RX_TA: gma500's 0x00040006 (see dsi.c's pb_bridge_seq for the
	 * session-15 measurements — the reference's 4 leaves our bridge reads
	 * failing half the time).  Not reached on this board: pre_enable()
	 * leaves init to dsi.c's pb_bridge_program().
	 */
	tc358764_write(ctx, PPI_TX_RX_TA, 0x00040006);
	tc358764_write(ctx, PPI_LPTXTIMECNT, 4);
	tc358764_write(ctx, PPI_D0S_CLRSIPOCOUNT, 4);
	tc358764_write(ctx, PPI_D1S_CLRSIPOCOUNT, 4);
	tc358764_write(ctx, PPI_D2S_CLRSIPOCOUNT, 4);
	tc358764_write(ctx, PPI_D3S_CLRSIPOCOUNT, 4);
	tc358764_write(ctx, DSI_LANEENABLE, 0x1f);
	tc358764_write(ctx, PPI_LANEENABLE, 0x1f);
	tc358764_write(ctx, PPI_STARTPPI, PPI_START_FUNCTION);
	tc358764_write(ctx, DSI_STARTDSI, DSI_RX_START);

	tc358764_write(ctx, 0x04a4, 0);
	/* LVDS PHY: reset asserted, then the reference's IS/ND range */
	tc358764_write(ctx, LV_PHY0, 0x0044c02d);

	tc358764_write(ctx, LV_MX0003, 0x03020100);
	tc358764_write(ctx, LV_MX0407, 0x08050704);
	tc358764_write(ctx, LV_MX0811, 0x0f0e0a09);
	tc358764_write(ctx, LV_MX1215, 0x100d0c0b);
	tc358764_write(ctx, LV_MX1619, 0x12111716);
	tc358764_write(ctx, LV_MX2023, 0x1b151413);
	tc358764_write(ctx, LV_MX2427, 0x061a1819);

	tc358764_write(ctx, VP_CTRL, 0x03f00120);
	/*
	 * PlayBook: HBP/HFP live in bits 24:16 (VP_HTIM1_HBP/VP_HTIM2_HFP
	 * above), so the reference's values are 0x00780001 / 0x00760400, not
	 * the 0x78000001 / 0x76000400 this tree carried until session 15 —
	 * those put the porches in the reserved top byte and the bridge
	 * stored HBP = HFP = 0.  Report 132.
	 */
	tc358764_write(ctx, VP_HTIM1, 0x00780001);
	tc358764_write(ctx, VP_HTIM2, 0x00760400);
	tc358764_write(ctx, VP_VTIM1, 0x000a0001);
	tc358764_write(ctx, VP_VTIM2, 0x000c0258);

	msleep(100);

	/* release the LVDS PHY reset, enable LVDS (single link, no clock
	 * polarity inversion - upstream uses 0xd here, the reference 0x1) */
	tc358764_write(ctx, LV_PHY0, 0x0004c02d);
	tc358764_write(ctx, LV_CFG, LV_CFG_LVEN);
	tc358764_write(ctx, 0x0520, 5);
	tc358764_write(ctx, 0x0524, 5);
	tc358764_write(ctx, SYS_RST, SYS_RST_LCD);
	tc358764_write(ctx, VP_VFUEN, 1);

	/*
	 * Diagnostic only: a read still needs the bridge to answer BTA.  A
	 * non-zero value proves the command VC works and the writes landed;
	 * 0 is inconclusive (report 121 §7.5).
	 */
	{
		u32 id = 0;
		tc358764_read(ctx, SYS_ID, &id);
		dev_info(ctx->dev, "SYS_ID after programming: 0x%08x\n", id);
		ctx->error = 0;
	}

	pb_bridge_selftest(ctx);

	dev_info(ctx->dev, "programmed the bridge with the reference sequence\n");
	return 0;
}

static void tc358764_reset(struct tc358764 *ctx)
{
	if (!ctx->gpio_reset)
		return;

	/*
	 * PlayBook bring-up: RESETN is active-low, and the reference holds it
	 * asserted for 20 ms (winch_lcdctl: CLEARDATAOUT, usleep(20000),
	 * SETDATAOUT, usleep(20000) — report 122).  Upstream's 1-2 ms was
	 * never exercised on this board.
	 */
	gpiod_set_value(ctx->gpio_reset, 1);
	msleep(20);
	gpiod_set_value(ctx->gpio_reset, 0);
	msleep(20);
}

/*
 * PlayBook: the DSI host calls this from dsi_init_dsi(), after the DSI
 * PHY/PLL programming and before the bridge register writes — the exact point
 * winch_lcdctl pulses RESX (report 125/126).  The bridge's pre_enable must not
 * pulse it earlier.
 */
void tc358764_playbook_reset(struct mipi_dsi_device *dsi)
{
	struct tc358764 *ctx = mipi_dsi_get_drvdata(dsi);

	if (!ctx) {
		pr_err("tc358764: no drvdata for bridge reset\n");
		return;
	}
	tc358764_reset(ctx);
}
EXPORT_SYMBOL_GPL(tc358764_playbook_reset);

static void tc358764_post_disable(struct drm_bridge *bridge)
{
	struct tc358764 *ctx = bridge_to_tc358764(bridge);
	int ret;

	tc358764_reset(ctx);
	usleep_range(10000, 15000);
	ctx->inited = false;
	ret = regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		dev_err(ctx->dev, "error disabling regulators (%d)\n", ret);
}

static void tc358764_pre_enable(struct drm_bridge *bridge)
{
	struct tc358764 *ctx = bridge_to_tc358764(bridge);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		dev_err(ctx->dev, "error enabling regulators (%d)\n", ret);
	usleep_range(10000, 15000);

	/*
	 * PlayBook bring-up: the bridge is normally programmed in attach()
	 * while the DSI link is still in command mode.  Only retry here if
	 * that failed — by this point the host is in video mode and every
	 * generic write dies on BTA ("bta sync failed", report 121 §7).
	 */
	if (ctx->inited) {
		dev_info(ctx->dev, "bridge already programmed at attach\n");
		return;
	}

	/*
	 * PlayBook (winchester): the DSI host pulses RESX and programs the
	 * bridge inside dsi_init_dsi(), after the DSI PHY/PLL and before its
	 * VC/video enable — the reference's order (reports 125/126).  Do
	 * nothing here; doing it from this callback would run before the DSI
	 * is up and would auto-enable the host through the generic-write path.
	 */
	if (of_machine_is_compatible("blackberry,omap4-winchester")) {
		ctx->inited = true;
		return;
	}

	tc358764_reset(ctx);
	ret = tc358764_init(ctx);
	if (ret < 0)
		dev_err(ctx->dev, "error initializing bridge (%d)\n", ret);
	else
		ctx->inited = true;
}

static int tc358764_attach(struct drm_bridge *bridge,
			   enum drm_bridge_attach_flags flags)
{
	struct tc358764 *ctx = bridge_to_tc358764(bridge);

	/*
	 * Measured 2026-10-27: programming the bridge here does NOT work — the
	 * DSI host rejects transfers before the link has been configured
	 * ("write 0x013c failed: -22", -EINVAL), so init is left to
	 * pre_enable().  Kept as a note so nobody repeats the experiment
	 * without knowing the result.
	 */
	dev_info(ctx->dev, "attach: bridge init deferred to pre_enable\n");

	return drm_bridge_attach(bridge->encoder, ctx->next_bridge, bridge, flags);
}

static const struct drm_bridge_funcs tc358764_bridge_funcs = {
	.post_disable = tc358764_post_disable,
	.pre_enable = tc358764_pre_enable,
	.attach = tc358764_attach,
};

static int tc358764_parse_dt(struct tc358764 *ctx)
{
	struct device *dev = ctx->dev;

	/*
	 * PlayBook bring-up: the bridge reset line is not known yet.  Treat the
	 * GPIO as optional so the driver can bind and we can see whether the
	 * bootloader already left the bridge configured.
	 */
	ctx->gpio_reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->gpio_reset))
		return PTR_ERR(ctx->gpio_reset);

	ctx->next_bridge = devm_drm_of_get_bridge(dev, dev->of_node, 1, 0);
	if (IS_ERR(ctx->next_bridge))
		return PTR_ERR(ctx->next_bridge);

	return 0;
}

static int tc358764_configure_regulators(struct tc358764 *ctx)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); ++i)
		ctx->supplies[i].supply = tc358764_supplies[i];

	ret = devm_regulator_bulk_get(ctx->dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		dev_err(ctx->dev, "failed to get regulators: %d\n", ret);

	return ret;
}

static int tc358764_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct tc358764 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(struct tc358764), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	/*
	 * PlayBook bring-up: the DSI host takes its lane count from the DT
	 * `lanes` list on the host endpoint, and its PLL range from hs_rate /
	 * lp_rate, which this driver used to leave at 0 (the OMAP host then
	 * computes pll_max = 0 and refuses the mode).  Allow the board to set
	 * them; keep the old 4-lane/0-rate default otherwise.
	 */
	dsi->lanes = 4;
	of_property_read_u32(dev->of_node, "lanes", &dsi->lanes);
	dsi->hs_rate = 604800000;
	dsi->lp_rate = 10000000;
	{
		u32 tmp;

		if (!of_property_read_u32(dev->of_node, "hs-rate", &tmp))
			dsi->hs_rate = tmp;
		if (!of_property_read_u32(dev->of_node, "lp-rate", &tmp))
			dsi->lp_rate = tmp;
	}
	dsi->format = MIPI_DSI_FMT_RGB888;
	/*
	 * PlayBook: keep BURST (non-burst sync-pulse is rejected at this
	 * pixel clock, trap 31).  Do NOT set MIPI_DSI_MODE_LPM: it makes
	 * mipi_dsi_generic_write() mark every bridge register message
	 * MIPI_DSI_MSG_USE_LPM, i.e. send it on the LP path, while the
	 * reference's sender leaves VC1 on the normal HS packet path.  The
	 * bridge's HS receiver is the packet path; LP writes did not reach it
	 * (report 126 delivery test: GPIO36 never followed 0x0524 bit 0).
	 */
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST
		| MIPI_DSI_MODE_VIDEO_AUTO_VERT;

	ret = tc358764_parse_dt(ctx);
	if (ret < 0)
		return ret;

	ret = tc358764_configure_regulators(ctx);
	if (ret < 0)
		return ret;

	ctx->bridge.funcs = &tc358764_bridge_funcs;
	ctx->bridge.of_node = dev->of_node;
	ctx->bridge.pre_enable_prev_first = true;

	drm_bridge_add(&ctx->bridge);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_bridge_remove(&ctx->bridge);
		dev_err(dev, "failed to attach dsi\n");
	}

	return ret;
}

static void tc358764_remove(struct mipi_dsi_device *dsi)
{
	struct tc358764 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_bridge_remove(&ctx->bridge);
}

static const struct of_device_id tc358764_of_match[] = {
	{ .compatible = "toshiba,tc358764" },
	{ }
};
MODULE_DEVICE_TABLE(of, tc358764_of_match);

static struct mipi_dsi_driver tc358764_driver = {
	.probe = tc358764_probe,
	.remove = tc358764_remove,
	.driver = {
		.name = "tc358764",
		.of_match_table = tc358764_of_match,
	},
};
module_mipi_dsi_driver(tc358764_driver);

MODULE_AUTHOR("Andrzej Hajda <a.hajda@samsung.com>");
MODULE_AUTHOR("Maciej Purski <m.purski@samsung.com>");
MODULE_DESCRIPTION("MIPI-DSI based Driver for TC358764 DSI/LVDS Bridge");
MODULE_LICENSE("GPL v2");
