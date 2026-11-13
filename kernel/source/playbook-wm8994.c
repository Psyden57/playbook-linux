// SPDX-License-Identifier: GPL-2.0-only
/*
 * playbook-wm8994.c -- SoC audio for the BlackBerry PlayBook (OMAP4430).
 *
 * The board has a Wolfson WM8994 on I2C4.  The CPU side is McBSP2 (inside
 * l4_abe).  Since session 45 (experiment E5) the McBSP is the bit/frame
 * provider again -- the non-reference design -- so that a live AIF1CLK does
 * not depend on the codec's MCLK1 pin, which does not clock under our image
 * (reports 206/209).  The codec is the AIF1 slave and its FLL1 is referenced
 * from the incoming BCLK, which this design generates and which was measured
 * to lock FLL1 every time (report 205 §3).
 *
 * The sample-rate generator is clocked from the CPU clock (SRGR2.CLKSM = 1,
 * i.e. OMAP_MCBSP_SYSCLK_CLK).  The session-41 build left CLKSM = 0, which
 * per TRM Table 23-52 derives the SRG clock from the **CLKS pin**; this board
 * drives no CLKS (clk_summary: pad_clks_ck is 12 MHz with no consumer and not
 * enabled), so the SRG never produced CLKG and the transmitter stalled with
 * XRDY = 0 / XEMPTY = 1 (report 205 §5l).  The internal path is live:
 * abe_24m_fclk = 12.288 MHz -> abe-clkctrl:0030:24 -> 40124000.mcbsp.
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

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <linux/regmap.h>

/*
 * Values from sound/soc/codecs/wm8994.h and sound/soc/ti/omap-mcbsp.h.
 */
#define PB_WM8994_FLL1			1
#define PB_WM8994_FLL_SRC_BCLK		4
#define PB_WM8994_SYSCLK_FLL1		3

/*
 * OMAP_MCBSP_SYSCLK_CLK: SRGR2.CLKSM = 1, "SRG clock derived from the CPU
 * clock" (TRM Table 23-52).  OMAP_MCBSP_CLKGDV: SRGR1.CLKGDV.
 */
#define PB_OMAP_MCBSP_SYSCLK_CLK		2
#define PB_OMAP_MCBSP_CLKGDV		0

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

/*
 * The clock the sample-rate generator actually divides.
 *
 * The clock framework reports the McBSP's functional clock as 12.288 MHz
 * (abe_24m_fclk -> abe-clkctrl:0030:24), but with CLKSM = 1 the SRG divides
 * the ABE **interface** clock: measured on the bench, CLKGDV = 7 (/8) and
 * FPER = 31 (32 CLKG/frame) produced 192 450 frames/s, i.e. exactly 4.0x the
 * requested 48 kHz, and 49.152 MHz = 4 x 12.288 MHz = ocp_abe_iclk (the L4_ABE
 * interface clock, DPLL_ABE/2).  32 = 49.152 MHz / 1.536 MHz, so CLKGDV = 31
 * gives BCLK = 1.536 MHz and FPER = 32 gives 48 kHz.
 */
#define PB_MCBSP_FCLK_HZ		49152000
#define PB_AIFCLK_FS			256
#define PB_BITS_PER_FRAME		32	/* 2 channels x 16 bit */

struct playbook_wm8994_priv {
	struct snd_soc_component *codec;
	struct delayed_work repair;
	struct regulator *ldo;
	struct gpio_desc *hp;
	struct snd_pcm_substream *substream;
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
	 * E5 (session 45): the McBSP is the bit/frame provider and the codec is
	 * the AIF1 slave.  The ASoC core flips the link's codec-perspective
	 * CBC_CFC to BP_FP for the CPU DAI, so omap-mcbsp's set_dai_fmt takes
	 * the master branch (CLKXM/FSXM/CLKRM/FSRM set, SRGR2.FSGM = 1 -- the
	 * SRG's FSG drives FSX) and omap_mcbsp_start() releases GRST/FRST.
	 *
	 * CLKSM = 1 is the fix for the session-41 stall: with CLKSM = 0 the SRG
	 * is clocked from the CLKS pin, and this board has no CLKS source (the
	 * pad_clks clock is unused and gated).  The internal path read live:
	 * abe_24m_fclk 12.288 MHz -> abe-clkctrl:0030:24 -> 40124000.mcbsp.
	 * With CLKGDV = 8 that is BCLK = 1.536 MHz = 48 kHz x 32 bits, and
	 * FPER = 32 (computed in omap-mcbsp from in_freq/div/rate).
	 */
	ret = snd_soc_dai_set_sysclk(cpu_dai, PB_OMAP_MCBSP_SYSCLK_CLK,
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
	 * locks (see the repair comment).
	 */
	snd_soc_component_write(codec, PB_WM8994_AIF1_CLOCKING_1, 0x0000);
	snd_soc_component_update_bits(codec, PB_WM8994_AIF1_CONTROL_1,
				      0x0060, PB_AIF1_WL_16);

	/*
	 * Lock FLL1 from **BCLK** and source AIF1CLK from it.  With the McBSP
	 * as master, BCLK is our own output, so the FLL reference exists as
	 * soon as the port starts -- and this reference was measured to lock
	 * FLL1 on every attempt (R0x732 = 0x6020, report 205 §3), unlike the
	 * MCLK1 reference, whose pin does not clock under our image.
	 */
	ret = snd_soc_dai_set_pll(codec_dai, PB_WM8994_FLL1,
				  PB_WM8994_FLL_SRC_BCLK, bclk, aifclk);
	if (ret < 0 && ret != -ENOTSUPP)
		return ret;

	snd_soc_dai_set_sysclk(codec_dai, PB_WM8994_SYSCLK_FLL1,
			       aifclk, SND_SOC_CLOCK_IN);

	dev_info(rtd->dev,
		 "rate %u Hz: AIF1CLK %u Hz, BCLK %u Hz (McBSP bit/frame provider, CLKSM=1, CLKGDV=%u, FLL1 from BCLK)\n",
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

	/*
	 * Session-45 diagnostic: is the application's audio actually in the
	 * ALSA ring the sDMA copies from?  The transport reaches the shifter
	 * (XBUFFSTAT toggles) and the codec is clocked, yet nothing is audible,
	 * so the two live candidates are "the ring holds silence" and "the
	 * codec ignores our framing".  DXR cannot be read back on this silicon
	 * (it returns a constant), so read the ring itself.
	 */
	if (priv->substream && priv->substream->runtime) {
		struct snd_pcm_runtime *rt = priv->substream->runtime;

		if (rt->dma_area && rt->dma_bytes >= 8) {
			s16 *s = (s16 *)rt->dma_area;
			unsigned int n = rt->dma_bytes / 4; /* frames (stereo s16) */

			dev_info(codec->dev,
				 "pcm: rate %u fmt %d width %u area %u B first %d %d %d %d | at hw_ptr %u: %d %d %d %d\n",
				 rt->rate, rt->format, rt->sample_bits,
				 rt->dma_bytes, s[0], s[1], s[2], s[3],
				 rt->status ? rt->status->hw_ptr % n : 0,
				 s[(rt->status ? rt->status->hw_ptr % n : 0) * 2],
				 s[(rt->status ? rt->status->hw_ptr % n : 0) * 2 + 1],
				 s[(rt->status ? (rt->status->hw_ptr + 1) % n : 0) * 2],
				 s[(rt->status ? (rt->status->hw_ptr + 1) % n : 0) * 2 + 1]);
		} else {
			dev_info(codec->dev, "pcm: no dma_area (bytes %u)\n",
				 rt->dma_bytes);
		}
	}

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
		priv->substream = substream;
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
