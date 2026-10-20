// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 */


#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_edid.h>

#include "dp_audio.h"
#include "dp_ctrl.h"
#include "dp_drm.h"
#include "dp_panel.h"
#include "dp_reg.h"
#include "dp_display.h"
#include "dp_utils.h"

struct msm_dp_audio_private {
	struct platform_device *pdev;
	struct drm_device *drm_dev;
	void __iomem *link_base;
	struct mutex *state_lock;
	struct msm_dp_ctrl *ctrl;

	u32 channels;
	/* ALSA intent survives a video modeset, unlike engine enable ownership. */
	bool prepared;

	struct msm_dp_audio msm_dp_audio;
};

static inline u32 msm_dp_read_link(struct msm_dp_audio_private *audio, u32 offset)
{
	return readl_relaxed(audio->link_base + offset);
}

static inline void msm_dp_write_link(struct msm_dp_audio_private *audio,
			       u32 offset, u32 data)
{
	/*
	 * To make sure link reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, audio->link_base + offset);
}

static void msm_dp_audio_stream_sdp(struct msm_dp_audio_private *audio)
{
	struct dp_sdp_header sdp_hdr = {
		.HB0 = 0x00,
		.HB1 = 0x02,
		.HB2 = 0x00,
		.HB3 = audio->channels - 1,
	};
	u32 header[2];

	msm_dp_utils_pack_sdp_header(&sdp_hdr, header);

	msm_dp_write_link(audio, MMSS_DP_AUDIO_STREAM_0, header[0]);
	msm_dp_write_link(audio, MMSS_DP_AUDIO_STREAM_1, header[1]);
}

static void msm_dp_audio_timestamp_sdp(struct msm_dp_audio_private *audio)
{
	struct dp_sdp_header sdp_hdr = {
		.HB0 = 0x00,
		.HB1 = 0x01,
		.HB2 = 0x17,
		.HB3 = 0x0 | (0x11 << 2),
	};
	u32 header[2];

	msm_dp_utils_pack_sdp_header(&sdp_hdr, header);

	msm_dp_write_link(audio, MMSS_DP_AUDIO_TIMESTAMP_0, header[0]);
	msm_dp_write_link(audio, MMSS_DP_AUDIO_TIMESTAMP_1, header[1]);
}

static void msm_dp_audio_infoframe_sdp(struct msm_dp_audio_private *audio)
{
	struct dp_sdp_header sdp_hdr = {
		.HB0 = 0x00,
		.HB1 = 0x84,
		.HB2 = 0x1b,
		.HB3 = 0x0 | (0x11 << 2),
	};
	u32 header[2];

	msm_dp_utils_pack_sdp_header(&sdp_hdr, header);

	msm_dp_write_link(audio, MMSS_DP_AUDIO_INFOFRAME_0, header[0]);
	msm_dp_write_link(audio, MMSS_DP_AUDIO_INFOFRAME_1, header[1]);
}

static void msm_dp_audio_copy_management_sdp(struct msm_dp_audio_private *audio)
{
	struct dp_sdp_header sdp_hdr = {
		.HB0 = 0x00,
		.HB1 = 0x05,
		.HB2 = 0x0f,
		.HB3 = 0x00,
	};
	u32 header[2];

	msm_dp_utils_pack_sdp_header(&sdp_hdr, header);

	msm_dp_write_link(audio, MMSS_DP_AUDIO_COPYMANAGEMENT_0, header[0]);
	msm_dp_write_link(audio, MMSS_DP_AUDIO_COPYMANAGEMENT_1, header[1]);
}

static void msm_dp_audio_isrc_sdp(struct msm_dp_audio_private *audio)
{
	struct dp_sdp_header sdp_hdr = {
		.HB0 = 0x00,
		.HB1 = 0x06,
		.HB2 = 0x0f,
		.HB3 = 0x00,
	};
	u32 header[2];
	u32 reg;

	/* XXX: is it necessary to preserve this field? */
	reg = msm_dp_read_link(audio, MMSS_DP_AUDIO_ISRC_1);
	sdp_hdr.HB3 = FIELD_GET(HEADER_3_MASK, reg);

	msm_dp_utils_pack_sdp_header(&sdp_hdr, header);

	msm_dp_write_link(audio, MMSS_DP_AUDIO_ISRC_0, header[0]);
	msm_dp_write_link(audio, MMSS_DP_AUDIO_ISRC_1, header[1]);
}

static void msm_dp_audio_config_sdp(struct msm_dp_audio_private *audio)
{
	u32 sdp_cfg, sdp_cfg2;

	sdp_cfg = msm_dp_read_link(audio, MMSS_DP_SDP_CFG);
	/* AUDIO_TIMESTAMP_SDP_EN */
	sdp_cfg |= BIT(1);
	/* AUDIO_STREAM_SDP_EN */
	sdp_cfg |= BIT(2);
	/* AUDIO_COPY_MANAGEMENT_SDP_EN */
	sdp_cfg |= BIT(5);
	/* AUDIO_ISRC_SDP_EN  */
	sdp_cfg |= BIT(6);
	/* AUDIO_INFOFRAME_SDP_EN  */
	sdp_cfg |= BIT(20);

	drm_dbg_dp(audio->drm_dev, "sdp_cfg = 0x%x\n", sdp_cfg);

	msm_dp_write_link(audio, MMSS_DP_SDP_CFG, sdp_cfg);

	sdp_cfg2 = msm_dp_read_link(audio, MMSS_DP_SDP_CFG2);
	/* IFRM_REGSRC -> Do not use reg values */
	sdp_cfg2 &= ~BIT(0);
	/* AUDIO_STREAM_HB3_REGSRC-> Do not use reg values */
	sdp_cfg2 &= ~BIT(1);

	drm_dbg_dp(audio->drm_dev, "sdp_cfg2 = 0x%x\n", sdp_cfg2);

	msm_dp_write_link(audio, MMSS_DP_SDP_CFG2, sdp_cfg2);
}

static void msm_dp_audio_setup_sdp(struct msm_dp_audio_private *audio)
{
	msm_dp_audio_config_sdp(audio);

	msm_dp_audio_stream_sdp(audio);
	msm_dp_audio_timestamp_sdp(audio);
	msm_dp_audio_infoframe_sdp(audio);
	msm_dp_audio_copy_management_sdp(audio);
	msm_dp_audio_isrc_sdp(audio);
}

static void msm_dp_audio_setup_acr(struct msm_dp_audio_private *audio)
{
	u32 select, acr_ctrl;

	switch (audio->msm_dp_audio.bw_code) {
	case DP_LINK_BW_1_62:
		select = 0;
		break;
	case DP_LINK_BW_2_7:
		select = 1;
		break;
	case DP_LINK_BW_5_4:
		select = 2;
		break;
	case DP_LINK_BW_8_1:
		select = 3;
		break;
	default:
		drm_dbg_dp(audio->drm_dev, "Unknown link rate\n");
		select = 0;
		break;
	}

	acr_ctrl = select << 4 | BIT(31) | BIT(8) | BIT(14);

	drm_dbg_dp(audio->drm_dev, "select: %#x, acr_ctrl: %#x\n",
		   select, acr_ctrl);

	msm_dp_write_link(audio, MMSS_DP_AUDIO_ACR_CTRL, acr_ctrl);
}

static void msm_dp_audio_safe_to_exit_level(struct msm_dp_audio_private *audio)
{
	u32 safe_to_exit_level, mainlink_levels;

	switch (audio->msm_dp_audio.lane_count) {
	case 1:
		safe_to_exit_level = 14;
		break;
	case 2:
		safe_to_exit_level = 8;
		break;
	case 4:
		safe_to_exit_level = 5;
		break;
	default:
		safe_to_exit_level = 14;
		drm_dbg_dp(audio->drm_dev,
				"setting the default safe_to_exit_level = %u\n",
				safe_to_exit_level);
		break;
	}

	mainlink_levels = msm_dp_read_link(audio, REG_DP_MAINLINK_LEVELS);
	mainlink_levels &= 0xFE0;
	mainlink_levels |= safe_to_exit_level;

	drm_dbg_dp(audio->drm_dev,
		   "mainlink_level = 0x%x, safe_to_exit_level = 0x%x\n",
		   mainlink_levels, safe_to_exit_level);

	msm_dp_write_link(audio, REG_DP_MAINLINK_LEVELS, mainlink_levels);
}

static void msm_dp_audio_enable(struct msm_dp_audio_private *audio, bool enable)
{
	u32 audio_ctrl;

	audio_ctrl = msm_dp_read_link(audio, MMSS_DP_AUDIO_CFG);

	if (enable)
		audio_ctrl |= BIT(0);
	else
		audio_ctrl &= ~BIT(0);

	drm_dbg_dp(audio->drm_dev, "dp_audio_cfg = 0x%x\n", audio_ctrl);

	msm_dp_write_link(audio, MMSS_DP_AUDIO_CFG, audio_ctrl);
	/* make sure audio engine is disabled */
	wmb();
}

static struct msm_dp_audio_private *msm_dp_audio_get_data(struct msm_dp *msm_dp_display)
{
	struct msm_dp_audio *msm_dp_audio;

	msm_dp_audio = msm_dp_display->msm_dp_audio;
	if (!msm_dp_audio) {
		DRM_ERROR("invalid msm_dp_audio data\n");
		return ERR_PTR(-EINVAL);
	}

	return container_of(msm_dp_audio, struct msm_dp_audio_private, msm_dp_audio);
}

static bool msm_dp_audio_ready(struct msm_dp_audio_private *audio,
			       struct msm_dp *msm_dp_display)
{
	lockdep_assert_held(audio->state_lock);
	return msm_dp_display->power_on &&
		msm_dp_ctrl_link_regs_accessible(audio->ctrl) &&
		msm_dp_ctrl_has_power_ref(audio->ctrl) &&
		!msm_dp_ctrl_has_phy_error(audio->ctrl);
}

static void msm_dp_audio_program(struct msm_dp_audio_private *audio)
{
	msm_dp_audio_setup_sdp(audio);
	msm_dp_audio_setup_acr(audio);
	msm_dp_audio_safe_to_exit_level(audio);
	msm_dp_audio_enable(audio, true);
}

int msm_dp_audio_restart(struct msm_dp *msm_dp_display)
{
	struct msm_dp_audio_private *audio = msm_dp_audio_get_data(msm_dp_display);

	if (IS_ERR(audio))
		return PTR_ERR(audio);
	lockdep_assert_held(audio->state_lock);
	if (!audio->prepared)
		return 0;
	if (!msm_dp_audio_ready(audio, msm_dp_display) || !audio->channels)
		return -EIO;
	if (!msm_dp_display->audio_enabled) {
		msm_dp_audio_program(audio);
		msm_dp_display->audio_enabled = true;
	}
	return 0;
}

int msm_dp_audio_stop(struct msm_dp *msm_dp_display)
{
	struct msm_dp_audio_private *audio;

	audio = msm_dp_audio_get_data(msm_dp_display);
	if (IS_ERR(audio))
		return PTR_ERR(audio);

	lockdep_assert_held(audio->state_lock);
	if (!msm_dp_display->audio_enabled)
		return 0;
	/* Preserve the successful-enable ledger if HW is no longer accessible. */
	if (!msm_dp_ctrl_link_regs_accessible(audio->ctrl))
		return -EIO;

	msm_dp_audio_enable(audio, false);
	msm_dp_display->audio_enabled = false;
	return 0;
}

static void msm_dp_audio_put_pm(struct msm_dp_audio_private *audio)
{
	int ret;

	/* An idle refusal still consumes this temporary parent-PM lease. */
	ret = pm_runtime_put_sync(&audio->pdev->dev);
	if (ret < 0)
		dev_dbg(&audio->pdev->dev, "audio runtime idle refused: %d\n", ret);
}

int msm_dp_audio_prepare(struct drm_bridge *bridge,
			 struct drm_connector *connector,
			 struct hdmi_codec_daifmt *daifmt,
			 struct hdmi_codec_params *params)
{
	int rc = 0;
	int active;
	struct msm_dp_audio_private *audio;
	struct msm_dp *msm_dp_display;

	msm_dp_display = to_dp_bridge(bridge)->msm_dp_display;

	audio = msm_dp_audio_get_data(msm_dp_display);
	if (IS_ERR(audio))
		return PTR_ERR(audio);
	/* The HDMI codec advertises two to eight playback channels. */
	if (params->channels < 2 || params->channels > 8)
		return -EINVAL;

	/* Parent runtime callbacks take the same state lock. */
	active = pm_runtime_get_if_active(&audio->pdev->dev);
	if (active < 0)
		return active;

	mutex_lock(audio->state_lock);
	/* Software intent can be staged while inactive, without waking the HW. */
	if (!msm_dp_display->power_on) {
		audio->channels = params->channels;
		audio->prepared = true;
		goto unlock;
	}
	if (!active) {
		rc = -EAGAIN;
		goto unlock;
	}
	if (!msm_dp_audio_ready(audio, msm_dp_display)) {
		rc = -EIO;
		goto unlock;
	}

	audio->channels = params->channels;

	msm_dp_audio_program(audio);
	msm_dp_display->audio_enabled = true;
	audio->prepared = true;

unlock:
	mutex_unlock(audio->state_lock);
	if (active > 0)
		msm_dp_audio_put_pm(audio);
	return rc;
}

void msm_dp_audio_shutdown(struct drm_bridge *bridge,
			   struct drm_connector *connecter)
{
	struct msm_dp_audio_private *audio;
	struct msm_dp *msm_dp_display;
	int active, ret = 0;

	msm_dp_display = to_dp_bridge(bridge)->msm_dp_display;
	audio = msm_dp_audio_get_data(msm_dp_display);
	if (IS_ERR(audio)) {
		DRM_ERROR("failed to get audio data\n");
		return;
	}

	/* Late ALSA shutdown must not resume a disconnected display. */
	active = pm_runtime_get_if_active(&audio->pdev->dev);
	mutex_lock(audio->state_lock);
	/* Clearing ALSA intent is software-only, not a hardware-stop claim. */
	audio->prepared = false;
	if (active > 0)
		ret = msm_dp_audio_stop(msm_dp_display);
	else if (msm_dp_display->audio_enabled)
		ret = active < 0 ? active : -EAGAIN;
	mutex_unlock(audio->state_lock);
	if (active < 0)
		dev_err(&audio->pdev->dev,
			"audio shutdown PM admission failed: %d\n", active);
	if (ret)
		dev_err(&audio->pdev->dev,
			"audio shutdown could not disable hardware: %d\n", ret);
	if (active > 0)
		msm_dp_audio_put_pm(audio);
}

struct msm_dp_audio *msm_dp_audio_get(struct platform_device *pdev,
			      void __iomem *link_base,
			      struct msm_dp_ctrl *ctrl,
			      struct mutex *state_lock)
{
	int rc = 0;
	struct msm_dp_audio_private *audio;
	struct msm_dp_audio *msm_dp_audio;

	if (!pdev || !ctrl || !state_lock) {
		DRM_ERROR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	audio = devm_kzalloc(&pdev->dev, sizeof(*audio), GFP_KERNEL);
	if (!audio) {
		rc = -ENOMEM;
		goto error;
	}

	audio->pdev = pdev;
	audio->link_base = link_base;
	audio->ctrl = ctrl;
	audio->state_lock = state_lock;

	msm_dp_audio = &audio->msm_dp_audio;

	return msm_dp_audio;
error:
	return ERR_PTR(rc);
}

void msm_dp_audio_put(struct msm_dp_audio *msm_dp_audio)
{
	struct msm_dp_audio_private *audio;

	if (!msm_dp_audio)
		return;

	audio = container_of(msm_dp_audio, struct msm_dp_audio_private, msm_dp_audio);

	devm_kfree(&audio->pdev->dev, audio);
}
