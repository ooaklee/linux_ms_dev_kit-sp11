/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_AUDIO_H_
#define _DP_AUDIO_H_

#include <linux/platform_device.h>

#include <sound/hdmi-codec.h>

struct drm_bridge;
struct msm_dp;
struct msm_dp_ctrl;
struct mutex;

/**
 * struct msm_dp_audio
 * @lane_count: number of lanes configured in current session
 * @bw_code: link rate's bandwidth code for current session
 */
struct msm_dp_audio {
	u32 lane_count;
	u32 bw_code;
};

/**
 * msm_dp_audio_get()
 *
 * Creates and instance of dp audio.
 *
 * @pdev: caller's platform device instance.
 * @link_base: pointer to the msm_dp_link resource.
 * @ctrl: display controller sharing the audio transition lock.
 * @state_lock: display-owned lock, acquired only after a parent-PM lease.
 *
 * Returns the error code in case of failure, otherwize
 * an instance of newly created msm_dp_module.
 */
struct msm_dp_audio *msm_dp_audio_get(struct platform_device *pdev,
			      void __iomem *link_base,
			      struct msm_dp_ctrl *ctrl,
			      struct mutex *state_lock);

/*
 * Caller holds state_lock and an active parent-PM lease. Stop audio before
 * controller/clock teardown; late ALSA shutdown is then a logical no-op.
 * A nonzero result retains audio_enabled: hardware stop was not confirmed.
 */
int msm_dp_audio_stop(struct msm_dp *msm_dp_display);

/* Same lock/lease contract; restore only current, accepted ALSA prepare intent. */
int msm_dp_audio_restart(struct msm_dp *msm_dp_display);

/**
 * msm_dp_audio_put()
 *
 * Cleans the msm_dp_audio instance.
 *
 * @msm_dp_audio: an instance of msm_dp_audio.
 */
void msm_dp_audio_put(struct msm_dp_audio *msm_dp_audio);

int msm_dp_audio_prepare(struct drm_bridge *bridge,
			 struct drm_connector *connector,
			 struct hdmi_codec_daifmt *daifmt,
			 struct hdmi_codec_params *params);
void msm_dp_audio_shutdown(struct drm_bridge *bridge,
			   struct drm_connector *connector);

#endif /* _DP_AUDIO_H_ */


