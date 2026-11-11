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

#include <sound/pcm_params.h>
#include <sound/soc.h>

/* Values from sound/soc/codecs/wm8994.h and sound/soc/ti/omap-mcbsp.h. */
#define PB_WM8994_FLL1			1
#define PB_WM8994_FLL_SRC_BCLK		4
#define PB_WM8994_SYSCLK_MCLK1		1
#define PB_WM8994_SYSCLK_FLL1		3
#define PB_OMAP_MCBSP_CLKGDV		0

#define PB_MCBSP_FCLK_HZ		12288000
#define PB_MCLK1_HZ			12800000
#define PB_AIFCLK_FS			256
#define PB_BITS_PER_FRAME		32	/* 2 channels x 16 bit */

static int playbook_wm8994_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int rate = params_rate(params);
	unsigned int aifclk = rate * PB_AIFCLK_FS;
	unsigned int bclk = rate * PB_BITS_PER_FRAME;
	unsigned int div = PB_MCBSP_FCLK_HZ / bclk;
	int ret;

	if (!div)
		div = 1;

	/* The McBSP sample-rate generator needs its input clock and divider. */
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, PB_MCBSP_FCLK_HZ,
				     SND_SOC_CLOCK_IN);
	if (ret < 0 && ret != -ENOTSUPP)
		return ret;

	ret = snd_soc_dai_set_clkdiv(cpu_dai, PB_OMAP_MCBSP_CLKGDV, div);
	if (ret < 0 && ret != -ENOTSUPP)
		return ret;

	/*
	 * The FLL refuses to be reconfigured while it is the active SYSCLK
	 * ("FLL1 is currently providing SYSCLK", -EBUSY).  OSS calls
	 * hw_params again for every format/rate change, so move AIF1CLK off
	 * the FLL first; the next set_sysclk switches it back.
	 */
	ret = snd_soc_dai_set_sysclk(codec_dai, PB_WM8994_SYSCLK_MCLK1,
				     PB_MCLK1_HZ, SND_SOC_CLOCK_IN);
	if (ret < 0 && ret != -ENOTSUPP)
		return ret;

	/* Lock FLL1 to the incoming BCLK and run AIF1CLK from it. */
	ret = snd_soc_dai_set_pll(codec_dai, PB_WM8994_FLL1,
				  PB_WM8994_FLL_SRC_BCLK, bclk, aifclk);
	if (ret < 0 && ret != -ENOTSUPP)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, PB_WM8994_SYSCLK_FLL1,
				     aifclk, SND_SOC_CLOCK_IN);
	if (ret < 0 && ret != -ENOTSUPP)
		return ret;

	dev_info(rtd->dev,
		 "rate %u Hz: AIF1CLK %u Hz, BCLK %u Hz (div %u, FLL1 from BCLK)\n",
		 rate, aifclk, bclk, div);
	return 0;
}

static const struct snd_soc_ops playbook_wm8994_ops = {
	.hw_params = playbook_wm8994_hw_params,
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
	struct pinctrl *pinctrl;

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
