/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_AUX_H_
#define _DP_AUX_H_

#include <drm/display/drm_dp_helper.h>

/*
 * admit runs with the raw caller's parent-PM lease, but without aux.mutex.
 * Its caller may hold drm_dp_aux.hw_mutex: never block on display state_lock.
 * On an error only, it may transfer the lease to cleanup accounting by
 * clearing put_temporary. Success must leave that lease with the raw caller.
 * recover owns the first fault's transferred lease. It must finish_fault
 * while holding display state_lock, adopt the returned lease under that lock
 * or put it after unlocking. It must account for the lease while stopping.
 */
struct msm_dp_aux_owner_ops {
	int (*admit)(void *data, bool *put_temporary);
	void (*recover)(void *data);
};

/* First attach, or restart the identical owner only after stop and drain. */
int msm_dp_aux_owner_set(struct drm_dp_aux *aux,
			 const struct msm_dp_aux_owner_ops *ops, void *data);
/* Stop under display state_lock; drain outside state/aux/hw_mutex. */
void msm_dp_aux_owner_stop(struct drm_dp_aux *aux);
void msm_dp_aux_owner_drain(struct drm_dp_aux *aux);
/* Detach only after owner drain and AUX child/interface/raw-user drains. */
int msm_dp_aux_owner_detach(struct drm_dp_aux *aux);
int msm_dp_aux_get_fault(struct drm_dp_aux *aux);
bool msm_dp_aux_fault_pending(struct drm_dp_aux *aux);
/* Caller holds display state_lock; true transfers one actual parent lease. */
bool msm_dp_aux_finish_fault(struct drm_dp_aux *aux, bool fully_drained);
/* Checked gate publication after actual PHY admission, under state_lock. */
int msm_dp_aux_admit_xfers(struct drm_dp_aux *aux);

int msm_dp_aux_register(struct drm_dp_aux *msm_dp_aux);
void msm_dp_aux_unregister(struct drm_dp_aux *msm_dp_aux);
irqreturn_t msm_dp_aux_isr(struct drm_dp_aux *msm_dp_aux, u32 isr);
bool msm_dp_aux_enable_xfers(struct drm_dp_aux *msm_dp_aux, bool enabled);
void msm_dp_aux_init(struct drm_dp_aux *msm_dp_aux);
void msm_dp_aux_deinit(struct drm_dp_aux *msm_dp_aux);
void msm_dp_aux_reconfig(struct drm_dp_aux *msm_dp_aux);

void msm_dp_aux_hpd_enable(struct drm_dp_aux *msm_dp_aux);
void msm_dp_aux_hpd_disable(struct drm_dp_aux *msm_dp_aux);
void msm_dp_aux_hpd_intr_enable(struct drm_dp_aux *msm_dp_aux);
void msm_dp_aux_hpd_intr_disable(struct drm_dp_aux *msm_dp_aux);
u32 msm_dp_aux_get_hpd_intr_status(struct drm_dp_aux *msm_dp_aux);
u32 msm_dp_aux_is_link_connected(struct drm_dp_aux *msm_dp_aux);

struct phy;
struct drm_dp_aux *msm_dp_aux_get(struct device *dev,
			      struct phy *phy,
			      bool is_edp,
			      void __iomem *aux_base);
void msm_dp_aux_put(struct drm_dp_aux *aux);

#endif /*__DP_AUX_H_*/
