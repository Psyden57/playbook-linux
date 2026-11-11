// SPDX-License-Identifier: GPL-2.0-only
/*
 * playbook-wm8994.c -- SoC audio for the BlackBerry PlayBook (OMAP4430).
 *
 * The board has a Wolfson WM8994 on I2C4.  The CPU side is McBSP2 (inside
 * l4_abe); it is the bit/frame provider (I2S, BCLK = 32 fs).
 *
 * Clocking: the WM8994's FLL1 locks to the incoming AIF1 BCLK and generates
 * the codec's AIF1CLK at 256 fs (12.288 MHz at 48 kHz).  The 1917 driver
 * configures FLL1 from MCLK1 at 12.8 MHz, but that pin does not clock on this
 * unit: with REFCLK_SRC=MCLK1 the FLL never reports lock (0x732 bit 5 = 0),
 * while relocking it to BCLK locks immediately (report 200).  BCLK is a
 * documented WM8994 FLL reference, so this is the working path.
 *
 * Recovered FLL1 details (1917 driver, codec_mixer/StereoAudioCodecSelectOutputPath):
 * REFCLK_DIV = 0, FRATIO = 0, OUTDIV = 7 (divide by 8), fractional mode.
 *
 * The card is a module so it loads after the USB gadget (trap 149); the
 * output widgets/routes complete the DAPM path from AIF1 to the headphone and
 * speaker pins, which the generic simple-card link never did.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <linux/regmap.h>

/*
 * Values from sound/soc/codecs/wm8994.h and sound/soc/ti/omap-mcbsp.h.
 */
#define PB_WM8994_FLL1			1
#define PB_WM8994_FLL_SRC_BCLK		4
#define PB_WM8994_SYSCLK_FLL1		3
#define PB_OMAP_MCBSP_CLKGDV		0
/* OMAP_MCBSP_SYSCLK_CLKS_FCLK: SRG from CLKS, switched to the PRCM source. */
#define PB_OMAP_MCBSP_SYSCLK_FCLK	0

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

#define PB_MCBSP_FCLK_HZ		12288000
#define PB_AIFCLK_FS			256
#define PB_BITS_PER_FRAME		32	/* 2 channels x 16 bit */

struct playbook_wm8994_priv {
	struct snd_soc_component *codec;
	struct delayed_work repair;
};

static int playbook_wm8994_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *codec = codec_dai->component;
	unsigned int rate = params_rate(params);
	unsigned int aifclk = rate * PB_AIFCLK_FS;
	unsigned int bclk = rate * PB_BITS_PER_FRAME;
	unsigned int div = PB_MCBSP_FCLK_HZ / bclk;
	int ret;

	if (!div)
		div = 1;

	/*
	 * Clock the McBSP sample-rate generator from the internal functional
	 * clock.
	 *
	 * OMAP_MCBSP_SYSCLK_CLKS_FCLK (= 0) does two things that matter: it
	 * calls omap2_mcbsp_set_clks_src(MCBSP_CLKS_PRCM_SRC), switching the
	 * SRG's CLKS input from the (undriven) CLKS pad to the PRCM functional
	 * clock, and leaves SRGR2.CLKSM = 0 so the SRG uses that CLKS input.
	 * With fclk = 12.288 MHz and CLKGDV = 7 that is a 1.536 MHz BCLK,
	 * i.e. 48 kHz x 32 bits.  Measured SRGR2 = 0x101F / SRGR1 = 0x0F07.
	 */
	ret = snd_soc_dai_set_sysclk(cpu_dai, PB_OMAP_MCBSP_SYSCLK_FCLK,
				     PB_MCBSP_FCLK_HZ, SND_SOC_CLOCK_IN);
	if (ret < 0 && ret != -ENOTSUPP)
		return ret;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, PB_OMAP_MCBSP_CLKGDV, div);
	if (ret < 0 && ret != -ENOTSUPP)
		return ret;

	/*
	 * Only the thaw and the word length here.  Everything else -- supplies,
	 * output stages, charge pump, DAC routing and un-mute -- is deferred to
	 * playbook_wm8994_repair() because none of it survives being written
	 * while the codec is frozen, and the codec *is* frozen until the FLL
	 * locks, which cannot happen before BCLK starts (see the repair comment).
	 */
	snd_soc_component_write(codec, PB_WM8994_AIF1_CLOCKING_1, 0x0000);
	snd_soc_component_update_bits(codec, PB_WM8994_AIF1_CONTROL_1,
				      0x0060, PB_AIF1_WL_16);

	/*
	 * Lock FLL1 from **BCLK** and source AIF1CLK from it.
	 *
	 * The reference uses MCLK1, and it works there because it keeps the FLL
	 * locked permanently (report 205 §5g).  On our image MCLK1 has been
	 * observed to lock only intermittently even with an identical register
	 * file and identical SoC programming (§5h), so this driver keeps the
	 * BCLK path, which was measured to lock every time (R0x732 = 0x6020).
	 *
	 * The codec freeze this arrangement can cause is handled by the thaw
	 * above plus the explicit enable writes below; with those in place PM3
	 * and the charge pump now hold through a live stream (§5b).
	 */
	ret = snd_soc_dai_set_pll(codec_dai, PB_WM8994_FLL1,
				  PB_WM8994_FLL_SRC_BCLK, bclk, aifclk);
	if (ret < 0 && ret != -ENOTSUPP)
		return ret;

	snd_soc_dai_set_sysclk(codec_dai, PB_WM8994_SYSCLK_FLL1,
			       aifclk, SND_SOC_CLOCK_IN);

	dev_info(rtd->dev,
		 "rate %u Hz: AIF1CLK %u Hz, BCLK %u Hz (div %u, FLL1 from BCLK)\n",
		 rate, aifclk, bclk, div);
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
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBC_CFC |
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
