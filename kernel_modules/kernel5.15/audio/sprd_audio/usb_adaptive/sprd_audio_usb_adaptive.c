/*
 * Unisoc audio usb adaptive driver
 *
 * SPDX-FileCopyrightText: 2020 Unisoc (Shanghai) Technologies Co., Ltd
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#define pr_fmt(fmt) "[sprd-audio-usb:adaptive] "fmt

#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/usb.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/usb/audio-v3.h>
#include <linux/usb/hcd.h>
#include <linux/module.h>
#include <linux/soc/sprd/sprd_musb_adaptive.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <trace/hooks/audio_usboffload.h>
#include "usbaudio.h"
#include "card.h"

#define TO_STRING(e) #e

#define QUIRK_PRODUCT_ID		0x3A07

enum usb_aud_type {
	USB_AUD_TYPE_SYNC,
	USB_AUD_TYPE_ADAPTIVE,
	USB_AUD_TYPE_ASYNC,
	USB_AUD_TYPE_MAX,
};

struct snd_usb_vendor_audio {
	int usb_aud_ofld_en[SNDRV_PCM_STREAM_LAST + 1];
	int usb_aud_should_suspend;
	enum usb_aud_type usb_aud_type[SNDRV_PCM_STREAM_LAST + 1];
};

static struct snd_usb_vendor_audio vendor_audio;

int snd_vendor_audio_offload(int stream)
{
	return vendor_audio.usb_aud_ofld_en[stream];
}

static const char *const usb_aud_type_txt[] = {
	[USB_AUD_TYPE_SYNC] = TO_STRING(USB_AUD_TYPE_SYNC),
	[USB_AUD_TYPE_ADAPTIVE] = TO_STRING(USB_AUD_TYPE_ADAPTIVE),
	[USB_AUD_TYPE_ASYNC] = TO_STRING(USB_AUD_TYPE_ASYNC),
};

static const struct soc_enum usb_sync_type_enum = SOC_ENUM_SINGLE_EXT(3, usb_aud_type_txt);

/*
 * @return: 0 not offload mod, 1 offload mod
 */
static int sprd_usb_audio_offload_check(int stream)
{
	int is_offload_mod = 0;

	if (stream != SNDRV_PCM_STREAM_PLAYBACK &&
		stream != SNDRV_PCM_STREAM_CAPTURE) {
		pr_err("%s invalid stream %d\n", __func__, stream);
		return is_offload_mod;
	}

	if (snd_vendor_audio_offload(stream))
		is_offload_mod = 1;
	else
		is_offload_mod = 0;

	pr_info("%s usb stream = %s, enter offload mode %s\n", __func__,
		stream ? "capture" : "playback", snd_vendor_audio_offload(stream) ? "true" : "false");

	return is_offload_mod;
}

/* action: false: ep_stop; true: ep_start */
static void usb_offload_ep_action(void *data, void *arg, bool action)
{
	struct snd_usb_endpoint *ep = (struct snd_usb_endpoint *)arg;
	struct usb_hcd *hcd;
	struct usb_device_descriptor *descriptor;
	int is_offload_mod;
	int stream;
	int usb_sync_type;

	if (ep) {
		descriptor = &ep->chip->dev->descriptor;
		stream = usb_pipein(ep->pipe) ? SNDRV_PCM_STREAM_CAPTURE : SNDRV_PCM_STREAM_PLAYBACK;
		usb_sync_type = ep->cur_audiofmt->ep_attr & USB_ENDPOINT_SYNCTYPE;

		pr_info("%s: EP 0x%x action = %s, sync_type = 0x%x\n",
			__func__, ep->ep_num, action ? "start" : "stop", usb_sync_type);

		switch (usb_sync_type) {
		case USB_ENDPOINT_SYNC_SYNC:
			vendor_audio.usb_aud_type[stream] = USB_AUD_TYPE_SYNC;
			break;
		case USB_ENDPOINT_SYNC_ADAPTIVE:
			vendor_audio.usb_aud_type[stream] = USB_AUD_TYPE_ADAPTIVE;
			break;
		case USB_ENDPOINT_SYNC_ASYNC:
			vendor_audio.usb_aud_type[stream] = USB_AUD_TYPE_ASYNC;
			break;
		default:
			pr_err("%s unkown usb sync_type: 0x%x!\n", __func__, usb_sync_type);
		}

		is_offload_mod = sprd_usb_audio_offload_check(stream);
		if (is_offload_mod) {
			hcd = bus_to_hcd(ep->chip->dev->bus);
			if (!hcd->driver) {
				pr_err("%s hcd driver or usb_aud_config is null\n", __func__);
				return;
			}
			pr_info("%s %s adaptive mode, stream = %s, USB_AUD_TYPE = %s\n",
				__func__, action ? "enter" : "exit", stream ? "capture" : "playback",
				usb_aud_type_txt[vendor_audio.usb_aud_type[stream]]);
			musb_adaptive_config(hcd, ep->ep_num, 0, 0, 0, 48, action);

			pr_debug("%s, vendor_id = 0x%x, product_id = 0x%x, bcd_device = 0x%x\n",
				__func__, descriptor->idVendor,
				descriptor->idProduct, descriptor->bcdDevice);
			/*
			 * Workaround: bug 2113846
			 * add delay here for some specific usb device to make sure
			 * audio dsp has stopped before we call usb_set_interface.
			 */
			if (stream == SNDRV_PCM_STREAM_CAPTURE &&
				descriptor->idProduct == QUIRK_PRODUCT_ID) {
				pr_info("Workaround for specific usb device\n");
				mdelay(20);
			}
		}
	}
}

static int sprd_usb_offload_enable_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_usb_audio *chip = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int stream = mc->shift;

	if (!chip) {
		pr_err("%s chip is null\n", __func__);
		return 0;
	}
	if (stream > SNDRV_PCM_STREAM_LAST) {
		pr_err("%s invalid pcm stream %d", __func__, stream);
		return 0;
	}

	ucontrol->value.integer.value[0] = vendor_audio.usb_aud_ofld_en[stream];
	pr_info("%s audio usb offload %s %s\n", __func__,
		(stream == SNDRV_PCM_STREAM_PLAYBACK) ? "playback" : "capture",
		vendor_audio.usb_aud_ofld_en[stream] ? "ON" : "OFF");

	return 0;
}

static int sprd_usb_offload_enable_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_usb_audio *chip = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
	(struct soc_mixer_control *)kcontrol->private_value;
	int stream = mc->shift;
	int max = mc->max;
	int val;

	if (!chip) {
		pr_err("%s chip is null\n", __func__);
		return 0;
	}
	if (stream > SNDRV_PCM_STREAM_LAST) {
		pr_err("%s invalid pcm stream %d", __func__, stream);
		return 0;
	}
	val = ucontrol->value.integer.value[0];
	if (val > max) {
		pr_err("val is invalid\n");
		return -EINVAL;
	}

	vendor_audio.usb_aud_ofld_en[stream] = val;
	pr_info("%s audio usb offload %s %s, val=%#x\n", __func__,
		(stream == SNDRV_PCM_STREAM_PLAYBACK) ? "playback" : "capture",
		vendor_audio.usb_aud_ofld_en[stream] ? "ON" : "OFF",
		vendor_audio.usb_aud_ofld_en[stream]);

	return 0;
}

static int sprd_usb_should_suspend_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_usb_audio *chip = snd_kcontrol_chip(kcontrol);

	if (!chip) {
		pr_err("%s chip is null\n", __func__);
		return -EINVAL;
	}

	ucontrol->value.integer.value[0] = vendor_audio.usb_aud_should_suspend;
	return 0;
}

static int sprd_usb_should_suspend_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_usb_audio *chip = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
	(struct soc_mixer_control *)kcontrol->private_value;
	int max = mc->max, val;
	struct usb_hcd *hcd;

	if (!chip || !chip->dev || !chip->dev->bus) {
		pr_err("%s chip or dev or bus is null\n", __func__);
		return -EINVAL;
	}

	val = ucontrol->value.integer.value[0];
	if (val > max) {
		pr_err("val is invalid\n");
		return -EINVAL;
	}
	vendor_audio.usb_aud_should_suspend = val;
	pr_info("set to %s\n", val ? "suspend" : "not suspend");
	hcd = bus_to_hcd(chip->dev->bus);
	if (!hcd->driver) {
		pr_err("%s hcd driver or usb_aud_mode is null\n", __func__);
		return -EINVAL;
	}
	musb_set_adaptive_mode(hcd, vendor_audio.usb_aud_should_suspend);

	return 0;
}


static int sprd_usb_playback_type_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = vendor_audio.usb_aud_type[SNDRV_PCM_STREAM_PLAYBACK];
	return 0;
}

static int sprd_usb_capture_type_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = vendor_audio.usb_aud_type[SNDRV_PCM_STREAM_CAPTURE];
	return 0;
}

static struct snd_kcontrol_new sprd_controls[] = {
	SOC_SINGLE_EXT("USB_AUD_OFLD_P_EN", SND_SOC_NOPM,
		SNDRV_PCM_STREAM_PLAYBACK, 1, 0,
		sprd_usb_offload_enable_get,
		sprd_usb_offload_enable_put),
	SOC_SINGLE_EXT("USB_AUD_OFLD_C_EN", SND_SOC_NOPM,
		SNDRV_PCM_STREAM_CAPTURE, 1, 0,
		sprd_usb_offload_enable_get,
		sprd_usb_offload_enable_put),
	SOC_SINGLE_EXT("USB_AUD_SHOULD_SUSPEND", SND_SOC_NOPM, 0, 1, 0,
		       sprd_usb_should_suspend_get,
		       sprd_usb_should_suspend_put),
	SOC_ENUM_EXT("USB_AUD_PLAYBACK_TYPE", usb_sync_type_enum,
  		sprd_usb_playback_type_get, NULL),
	SOC_ENUM_EXT("USB_AUD_CAPTURE_TYPE", usb_sync_type_enum,
  		sprd_usb_capture_type_get, NULL),
	{},
};

static int sprd_usb_control_add(struct snd_usb_audio *chip)
{
	int ret = 0;
	int i = 0;

	if (!chip) {
		pr_err("%s failed chip is null\n", __func__);
		return -EINVAL;
	}

	while (sprd_controls[i].name) {
		ret = snd_ctl_add(chip->card, snd_ctl_new1(&sprd_controls[i],
				chip));
		if (ret < 0) {
			dev_err(&chip->dev->dev, "cannot add control.\n");
			return ret;
		}
		i++;
	}
	return 0;
}

static void usb_offload_add_ctrl(void *data, struct usb_interface *intf, struct snd_usb_audio *chip)
{

	vendor_audio.usb_aud_ofld_en[SNDRV_PCM_STREAM_PLAYBACK] = 0;
	vendor_audio.usb_aud_ofld_en[SNDRV_PCM_STREAM_CAPTURE] = 0;
	vendor_audio.usb_aud_should_suspend = 1;

	sprd_usb_control_add(chip);
	pr_info("%s\n", __func__);
}

static int sprd_usb_aud_ofld_en(struct snd_usb_audio *chip, int stream)
{
	if (stream != SNDRV_PCM_STREAM_PLAYBACK &&
		stream != SNDRV_PCM_STREAM_CAPTURE) {
		pr_err("%s invalid stream %d\n", __func__, stream);
		return 0;
	}

	if (!chip) {
		pr_err("%s chip is null stream %d\n", __func__, stream);
		return 0;
	}

	return snd_vendor_audio_offload(stream);
}

static int sprd_ofld_synctype_ignore(struct snd_usb_audio *chip,
	int stream, int attr)
{
	if (!sprd_usb_aud_ofld_en(chip, stream)) {
		pr_debug("not enable usb audio offload, can't ignore\n");
		return 0;
	}
	if (attr != USB_ENDPOINT_SYNC_SYNC) {
		pr_debug("offload ignore synctype stream %s sync_type %d\n",
			stream ? "capture" : "playback", attr);
		return 0;
	}
	pr_debug("stream %s sync_type is %d\n",
		stream ? "capture" : "playback", attr);

	return 0;
}

static void usb_offload_synctype_ignore(void *data, void *arg, int attr, bool *need_ignore)
{
	struct snd_usb_substream *subs = (struct snd_usb_substream *)arg;
	struct snd_usb_audio *chip;
	int ignore;

	chip = subs->stream ? subs->stream->chip : NULL;
	ignore = sprd_ofld_synctype_ignore(chip, subs->direction, attr);
	if (ignore) {
		pr_info("ofld_en %d, stream %d, sync_type %#x, ignore this audiofmt\n",
			sprd_usb_aud_ofld_en(chip, subs->direction), subs->direction, attr);
		*need_ignore = true;
	} else {
		*need_ignore = false;
	}

	pr_info("%s, need_ignore = %s\n", __func__, (*need_ignore) ? "true" : "false");
}

static void usb_offload_suspend(void *data, struct snd_pcm_substream *substream, int cmd, bool *suspend)
{
	int is_offload_mod;

	is_offload_mod = sprd_usb_audio_offload_check(substream->stream);
	if (is_offload_mod && cmd == SNDRV_PCM_TRIGGER_SUSPEND) {
		*suspend = false;
		pr_info("%s, adaptive mode, ignore suspend\n", __func__);
	}
	pr_info("%s, is_offload_mod = %d, suspend = %s\n",
			__func__, is_offload_mod, (*suspend) ? "true" : "false");
}

static int usb_offload_init(void)
{
	pr_info("%s\n", __func__);
	register_trace_android_vh_audio_usb_offload_connect(usb_offload_add_ctrl, NULL);
	register_trace_android_vh_audio_usb_offload_ep_action(usb_offload_ep_action, NULL);
	register_trace_android_vh_audio_usb_offload_synctype(usb_offload_synctype_ignore, NULL);
	register_trace_android_vh_audio_usb_offload_suspend(usb_offload_suspend, NULL);
	return 0;
}

static void usb_offload_exit(void)
{
	pr_info("%s\n", __func__);
}

postcore_initcall(usb_offload_init);
module_exit(usb_offload_exit);

MODULE_DESCRIPTION("sprd audio usb offload support");
MODULE_LICENSE("GPL");