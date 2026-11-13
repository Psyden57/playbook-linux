// SPDX-License-Identifier: GPL-2.0-only
/*
 * playbook-wm8994.c -- SoC audio for the BlackBerry PlayBook (OMAP4430).
 *
 * The board has a Wolfson WM8994 on I2C4.  The CPU side is McBSP2 (inside
 * l4_abe).  Since session 42 (experiment E1) the CODEC is the AIF1 master
 * (R0x302 = 0x4000, as on the 1917 reference) and the McBSP is a clock
 * consumer: CLKX/FSX are inputs, the sample-rate generator is out of the
 * path, and FLL1 is referenced from MCLK1 (12.8 MHz) instead of BCLK --
 * with the codec as master a BCLK-referenced FLL would be circular.
 *
 * The card is a module so it loads after the USB gadget (trap 149); the
 * output widgets/routes complete the DAPM path from AIF1 to the headphone and
 * speaker pins, which the generic simple-card link never did.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <linux/regmap.h>

/*
 * Values from sound/soc/codecs/wm8994.h and sound/soc/ti/omap-mcbsp.h.
 */
#define PB_WM8994_FLL1			1
#define PB_WM8994_FLL_SRC_MCLK1		1
#define PB_WM8994_SYSCLK_FLL1		3

/* WM8994 register addresses (include/linux/mfd/wm8994/registers.h). */
#define PB_WM8994_PM1			0x001
#define PB_WM8994_PM2			0x002
#define PB_WM8994_PM3			0x003
#define PB_WM8994_PM5			0x005
#define PB_WM8994_ANALOGUE_HP_0		0x060
#define PB_WM8994_AIF1_CLOCKING_1	0x200
#define PB_WM8994_AIF1_CONTROL_1	0x300
#define PB_WM8994_OUTPUT_MIXER_1	0x02d
#define PB_WM8994_OUTPUT_MIXER_2	0x02e
#define PB_WM8994_CHARGE_PUMP_1		0x04c
#define PB_WM8994_AIF1_DAC1_FILTERS_1	0x420
#define PB_WM8994_AIF1_DAC2_FILTERS_1	0x422
#define PB_WM8994_DAC1_LEFT_MIXER	0x601
#define PB_WM8994_DAC1_RIGHT_MIXER	0x602

/*
 * Enable bits (datasheet Rev 4.6, "Power Management").
 *
 * PM3 holds the output-mixer enables: MIXOUTL_ENA is bit 5 and MIXOUTR_ENA
 * bit 4, with MIXOUTLVOL_ENA/MIXOUTRVOL_ENA at bits 7:6.  These are NOT in
 * PM4 -- PM4 is the digital-microphone block, and writing them there does
 * nothing to the output path.
 */
#define PB_PM1_HPOUT1L_ENA		0x0200
#define PB_PM1_HPOUT1R_ENA		0x0100
#define PB_PM1_SPKOUTL_ENA		0x1000
#define PB_PM1_SPKOUTR_ENA		0x2000
#define PB_PM3_MIXOUTL_ENA		0x0020
#define PB_PM3_MIXOUTR_ENA		0x0010
#define PB_PM3_MIXOUTLVOL_ENA		0x0080
#define PB_PM3_MIXOUTRVOL_ENA		0x0040
#define PB_PM5_DAC1L_ENA		0x0002
#define PB_PM5_DAC1R_ENA		0x0001
#define PB_AIF1CLK_ENA			0x0001
#define PB_AIF1_WL_16			0x0000	/* AIF1_WL = 00 = 16 bit */

/* Charge pump: CP_ENA (bit 15) powers the headphone amplifier. */
#define PB_CP_ENA			0x8000

/* AIF1DAC1_MUTE (R0x420 bit 9, AIF1DAC2_MUTE in R0x422). */
#define PB_AIF1DAC_MUTE			0x0200

/* DAC1L/R -> MIXOUTL/R un-mute (R0x02D/R0x02E bit 0). */
#define PB_DAC_TO_MIXOUT		0x0001

/* AIF1DAC1L/R -> DAC1L/R (R0x601/R0x602 bit 0, AIF1 timeslot 0). */
#define PB_AIF1DAC1_TO_DAC1		0x0001

#define PB_MCLK1_HZ			12800000
#define PB_AIFCLK_FS			256
#define PB_BITS_PER_FRAME		32	/* 2 channels x 16 bit */

struct playbook_wm8994_priv {
	struct snd_soc_component *codec;
	struct delayed_work repair;
	struct regulator *ldo;
	struct gpio_desc *hp;
};

/*
 * Board-level audio enables, from the reference driver's inlined
 * codec_gpio_init (report 210 §3).  At io-audio init it resolves three hwio
 * signals and drives them, in this order:
 *
 *   signal 0x92 "ldo" -- hdq_sio, pad 0x120 low, gpio_127 (GPIO4_31):
 *                        output, low, delay(100), high
 *   signal 0x93 "spk" -- gpmc_a20, pad 0x068 low, gpio_44 (GPIO2_12):
 *                        output, high
 *   signal 0x94 "hp"  -- gpmc_a25, pad 0x070 high, gpio_49 (GPIO2_17):
 *                        input
 *
 * The spk line is already held high by the board's gpio hog, and hp is a pad
 * input; the ldo line is the one action our boot never performed, and the
 * reference does it *before* it configures the codec.  The machine driver
 * probes before any stream (and therefore before hw_params/FLL setup), so this
 * is the same point in the chain.
 */
static void playbook_wm8994_board_enables(struct device *dev,
					  struct playbook_wm8994_priv *priv)
{
	int ret;

	/*
	 * The ldo is a boot-on fixed regulator: it is already up when this
	 * module loads, and force_disable() is what actually gates a boot-on
	 * regulator with no explicit enable count.  Off -> 100 ms -> on is the
	 * reference's codec_gpio_init pulse.
	 */
	priv->ldo = devm_regulator_get_optional(dev, "ldo");
	if (IS_ERR(priv->ldo)) {
		dev_warn(dev, "ldo regulator unavailable: %ld\n",
			 PTR_ERR(priv->ldo));
		priv->ldo = NULL;
	}
	if (priv->ldo) {
		ret = regulator_force_disable(priv->ldo);
		if (ret)
			dev_warn(dev, "ldo force-disable failed: %d\n", ret);
		msleep(100);
		ret = regulator_enable(priv->ldo);
		if (ret)
			dev_warn(dev, "ldo enable failed: %d\n", ret);
		else
			dev_info(dev, "audio ldo pulsed, now on\n");
	}

	priv->hp = devm_gpiod_get_optional(dev, "hp", GPIOD_IN);
	if (IS_ERR(priv->hp)) {
		dev_warn(dev, "hp gpio unavailable: %ld\n", PTR_ERR(priv->hp));
		priv->hp = NULL;
	}
	if (priv->hp)
		dev_info(dev, "audio hp detect on gpio %d (input)\n",
			 desc_to_gpio(priv->hp));
}

static int playbook_wm8994_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *codec = codec_dai->component;
	unsigned int rate = params_rate(params);
	unsigned int aifclk = rate * PB_AIFCLK_FS;
	unsigned int bclk = rate * PB_BITS_PER_FRAME;
	int ret;

	/*
	 * E1 (session 42): the codec is the AIF1 MASTER and the McBSP is a
	 * clock CONSUMER, matching the 1917 reference (R0x302 = 0x4000).  The
	 * dai_fmt below says CBP_CFP from the codec's perspective; the ASoC
	 * core flips it for the CPU DAI, so omap-mcbsp's set_dai_fmt takes the
	 * BC_FC (slave) branch: CLKXM/FSXM stay clear, CLKX/FSX are inputs, and
	 * the sample-rate generator is not started at all (omap_mcbsp_start()
	 * only raises GRST/FRST for a master).  That removes the SRG from the
	 * path where XRDY sat stuck at 0 with XEMPTY 1 (report 205 §5l).
	 *
	 * No set_sysclk/set_clkdiv on the CPU DAI: with the SRG out of the path
	 * the McBSP shifts on the codec's BCLK directly.  (The pads for clkx
	 * and fsx are PIN_INPUT in the DTS to match.)
	 */

	/*
	 * Only the thaw and the word length here.  Everything else -- supplies,
	 * output stages, charge pump, DAC routing and un-mute -- is deferred to
	 * playbook_wm8994_repair() because none of it survives being written
	 * while the codec is frozen, and the codec *is* frozen until the FLL
	 * locks (see the repair comment).
	 */
	snd_soc_component_write(codec, PB_WM8994_AIF1_CLOCKING_1, 0x0000);
	snd_soc_component_update_bits(codec, PB_WM8994_AIF1_CONTROL_1,
				      0x0060, PB_AIF1_WL_16);

	/*
	 * Lock FLL1 from **MCLK1** (AUXCLK1 = 12.8 MHz) and source AIF1CLK
	 * from it -- the reference's exact design (report 205 §5g).
	 *
	 * The BCLK reference used through session 41 is IMPOSSIBLE here: with
	 * the codec as AIF1 master, BCLK is the codec's own output, so an FLL
	 * referenced to it is circular.  MCLK1 locked once in session 41 with
	 * the codec thawed (§5h) but not on every boot; this experiment rides
	 * on that open question.
	 */
	ret = snd_soc_dai_set_pll(codec_dai, PB_WM8994_FLL1,
				  PB_WM8994_FLL_SRC_MCLK1, PB_MCLK1_HZ, aifclk);
	if (ret < 0 && ret != -ENOTSUPP)
		return ret;

	snd_soc_dai_set_sysclk(codec_dai, PB_WM8994_SYSCLK_FLL1,
			       aifclk, SND_SOC_CLOCK_IN);

	dev_info(rtd->dev,
		 "rate %u Hz: AIF1CLK %u Hz, BCLK %u Hz (codec AIF1 master, FLL1 from MCLK1)\n",
		 rate, aifclk, bclk);
	return 0;
}

/*
 * Codec repair, deferred until BCLK is actually running.
 *
 * ASoC offers no hook that runs after the CPU DAI starts BCLK: the machine
 * .trigger is first in the trigger order (snd_soc_link_trigger), and the codec
 * component's trigger still precedes the DAI's.  But the FLL is referenced from
 * BCLK, so the codec is frozen at every one of those points -- writes are
 * accepted, ACKed and dropped.
 *
 * The reference driver has the same constraint and answers it with fixed
 * delays around FLL enable (delay(50) before, delay(100) after).  Do the same:
 * schedule the repair shortly after trigger(START) and let the FLL lock first.
 */
static void playbook_wm8994_repair(struct work_struct *work)
{
	struct playbook_wm8994_priv *priv =
		container_of(work, struct playbook_wm8994_priv, repair.work);
	struct snd_soc_component *codec = priv->codec;

	/*
	 * Force the regmap cache to be re-read before writing.
	 *
	 * These are ordinary update_bits() calls, but the codec's cache is stale:
	 * every earlier write was dropped while it was frozen, so the cache still
	 * holds the *intended* values and update_bits() concludes there is
	 * nothing to do.  That is exactly why PM3 read 0x0000 while the driver
	 * had "written" 0x00F0.  mark_dirty/sync discards the cache so the next
	 * writes go out on the bus.
	 */
	regcache_mark_dirty(codec->regmap);
	regcache_sync(codec->regmap);

	/* FLL1 must be locked before the rest lands. */
	if (!(snd_soc_component_read(codec, 0x732) & 0x0020))
		dev_warn(codec->dev, "repair: FLL1 still unlocked\n");

	/* Supplies and output stages. */
	snd_soc_component_update_bits(codec, PB_WM8994_PM2, 0x6000, 0x6000);
	snd_soc_component_update_bits(codec, PB_WM8994_PM5,
				      PB_PM5_DAC1L_ENA | PB_PM5_DAC1R_ENA,
				      PB_PM5_DAC1L_ENA | PB_PM5_DAC1R_ENA);
	snd_soc_component_update_bits(codec, PB_WM8994_PM3,
				      PB_PM3_MIXOUTL_ENA | PB_PM3_MIXOUTR_ENA |
				      PB_PM3_MIXOUTLVOL_ENA |
				      PB_PM3_MIXOUTRVOL_ENA,
				      PB_PM3_MIXOUTL_ENA | PB_PM3_MIXOUTR_ENA |
				      PB_PM3_MIXOUTLVOL_ENA |
				      PB_PM3_MIXOUTRVOL_ENA);
	snd_soc_component_update_bits(codec, PB_WM8994_PM1,
				      PB_PM1_HPOUT1L_ENA | PB_PM1_HPOUT1R_ENA |
				      PB_PM1_SPKOUTL_ENA | PB_PM1_SPKOUTR_ENA,
				      PB_PM1_HPOUT1L_ENA | PB_PM1_HPOUT1R_ENA |
				      PB_PM1_SPKOUTL_ENA | PB_PM1_SPKOUTR_ENA);

	/* Charge pump powers the headphone amplifier. */
	snd_soc_component_update_bits(codec, PB_WM8994_CHARGE_PUMP_1,
				      PB_CP_ENA, PB_CP_ENA);

	/* DAC -> output mixer, and the output stage connect. */
	snd_soc_component_update_bits(codec, PB_WM8994_OUTPUT_MIXER_1,
				      PB_DAC_TO_MIXOUT, PB_DAC_TO_MIXOUT);
	snd_soc_component_update_bits(codec, PB_WM8994_OUTPUT_MIXER_2,
				      PB_DAC_TO_MIXOUT, PB_DAC_TO_MIXOUT);
	snd_soc_component_update_bits(codec, PB_WM8994_ANALOGUE_HP_0,
				      0x00cc, 0x00cc);

	/* Word length, AIF1->DAC routing, and the DAC soft un-mute. */
	snd_soc_component_update_bits(codec, PB_WM8994_AIF1_CONTROL_1,
				      0x0060, PB_AIF1_WL_16);
	snd_soc_component_update_bits(codec, PB_WM8994_DAC1_LEFT_MIXER,
				      PB_AIF1DAC1_TO_DAC1, PB_AIF1DAC1_TO_DAC1);
	snd_soc_component_update_bits(codec, PB_WM8994_DAC1_RIGHT_MIXER,
				      PB_AIF1DAC1_TO_DAC1, PB_AIF1DAC1_TO_DAC1);
	snd_soc_component_update_bits(codec, PB_WM8994_AIF1_DAC1_FILTERS_1,
				      PB_AIF1DAC_MUTE, 0);

	/*
	 * Re-enable AIF1CLK from the (now locked) FLL1, LAST.
	 *
	 * hw_params() clears this bit to thaw the codec, and with a live FLL
	 * there is no freeze risk in turning it back on.  Without this the DAC
	 * has no clock at all: measured R0x200 = 0x0010 after every stream
	 * start, and the analogue noise floor disappears with it.
	 */
	snd_soc_component_update_bits(codec, PB_WM8994_AIF1_CLOCKING_1,
				      PB_AIF1CLK_ENA, PB_AIF1CLK_ENA);

	dev_info(codec->dev, "repair: %s\n",
		 (snd_soc_component_read(codec, 0x732) & 0x0020) ?
		 "done, FLL1 locked" : "done, FLL1 NOT locked");
}

static int playbook_wm8994_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct playbook_wm8994_priv *priv = snd_soc_card_get_drvdata(rtd->card);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		priv->codec = codec_dai->component;
		schedule_delayed_work(&priv->repair, msecs_to_jiffies(50));
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		cancel_delayed_work_sync(&priv->repair);
		break;
	}

	return 0;
}

static const struct snd_soc_ops playbook_wm8994_ops = {
	.hw_params = playbook_wm8994_hw_params,
	.trigger = playbook_wm8994_trigger,
};

static const struct snd_soc_dapm_widget playbook_wm8994_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static const struct snd_soc_dapm_route playbook_wm8994_routes[] = {
	{ "Headphone", NULL, "HPOUT1L" },
	{ "Headphone", NULL, "HPOUT1R" },
	{ "Speaker", NULL, "SPKOUTLP" },
	{ "Speaker", NULL, "SPKOUTLN" },
	{ "Speaker", NULL, "SPKOUTRP" },
	{ "Speaker", NULL, "SPKOUTRN" },
};

SND_SOC_DAILINK_DEFS(playbook,
	DAILINK_COMP_ARRAY(COMP_CPU("40124000.mcbsp")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm8994-codec", "wm8994-aif1")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("40124000.mcbsp")));

static struct snd_soc_dai_link playbook_wm8994_dai = {
	.name = "WM8994",
	.stream_name = "PlayBook WM8994",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBP_CFP |
		   SND_SOC_DAIFMT_NB_NF,
	.ops = &playbook_wm8994_ops,
	SND_SOC_DAILINK_REG(playbook),
};

static struct snd_soc_card playbook_wm8994_card = {
	.name = "PlayBook WM8994",
	.owner = THIS_MODULE,
	.dai_link = &playbook_wm8994_dai,
	.num_links = 1,
	.dapm_widgets = playbook_wm8994_widgets,
	.num_dapm_widgets = ARRAY_SIZE(playbook_wm8994_widgets),
	.dapm_routes = playbook_wm8994_routes,
	.num_dapm_routes = ARRAY_SIZE(playbook_wm8994_routes),
};

static int playbook_wm8994_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &playbook_wm8994_card;
	struct playbook_wm8994_priv *priv;
	struct pinctrl *pinctrl;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	INIT_DELAYED_WORK(&priv->repair, playbook_wm8994_repair);
	snd_soc_card_set_drvdata(card, priv);

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl) && PTR_ERR(pinctrl) != -ENODEV)
		return PTR_ERR(pinctrl);

	/*
	 * Reference order: board enables (ldo pulse, hp input) first, then the
	 * codec work -- which happens at hw_params, after this probe.
	 */
	playbook_wm8994_board_enables(&pdev->dev, priv);

	card->dev = &pdev->dev;
	return devm_snd_soc_register_card(&pdev->dev, card);
}

static const struct of_device_id playbook_wm8994_of_match[] = {
	{ .compatible = "blackberry,playbook-wm8994" },
	{ }
};
MODULE_DEVICE_TABLE(of, playbook_wm8994_of_match);

static struct platform_driver playbook_wm8994_driver = {
	.driver = {
		.name = "playbook-wm8994",
		.of_match_table = playbook_wm8994_of_match,
	},
	.probe = playbook_wm8994_probe,
};
module_platform_driver(playbook_wm8994_driver);

MODULE_DESCRIPTION("ASoC machine driver for the BlackBerry PlayBook WM8994");
MODULE_LICENSE("GPL");
