/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_CTRL_H_
#define _DP_CTRL_H_

#include "dp_aux.h"
#include "dp_panel.h"
#include "dp_link.h"

struct msm_dp_ctrl {
	bool wide_bus_en;
};

struct phy;
struct mutex;

/*
 * Resource transitions and ownership/error queries require the state_lock
 * supplied to msm_dp_ctrl_get(). IRQ/completion handling remains lock-free.
 */
int msm_dp_ctrl_on_link(struct msm_dp_ctrl *msm_dp_ctrl, bool restore_xfers);
int msm_dp_ctrl_on_stream(struct msm_dp_ctrl *msm_dp_ctrl, bool force_link_train);
int msm_dp_ctrl_off_link_stream(struct msm_dp_ctrl *msm_dp_ctrl);
int msm_dp_ctrl_off(struct msm_dp_ctrl *msm_dp_ctrl);
void msm_dp_ctrl_push_idle(struct msm_dp_ctrl *msm_dp_ctrl);
irqreturn_t msm_dp_ctrl_isr(struct msm_dp_ctrl *msm_dp_ctrl);
int msm_dp_ctrl_handle_sink_request(struct msm_dp_ctrl *msm_dp_ctrl);
struct msm_dp_ctrl *msm_dp_ctrl_get(struct device *dev,
				    struct msm_dp_link *link,
				    struct msm_dp_panel *panel,
				    struct drm_dp_aux *aux,
				    struct phy *phy,
				    struct mutex *state_lock,
				    void __iomem *ahb_base,
				    void __iomem *link_base);

void msm_dp_ctrl_reset(struct msm_dp_ctrl *msm_dp_ctrl);
int msm_dp_ctrl_phy_init(struct msm_dp_ctrl *msm_dp_ctrl, bool *newly_owned);
int msm_dp_ctrl_phy_exit(struct msm_dp_ctrl *msm_dp_ctrl);
bool msm_dp_ctrl_has_phy_refs(struct msm_dp_ctrl *msm_dp_ctrl);
bool msm_dp_ctrl_has_power_ref(struct msm_dp_ctrl *msm_dp_ctrl);
bool msm_dp_ctrl_has_phy_error(struct msm_dp_ctrl *msm_dp_ctrl);
/* Record a negative AUX fault without clearing the AUX owner's latch. */
void msm_dp_ctrl_record_aux_fault(struct msm_dp_ctrl *msm_dp_ctrl, int error);
bool msm_dp_ctrl_link_regs_accessible(struct msm_dp_ctrl *msm_dp_ctrl);
void msm_dp_ctrl_irq_phy_exit(struct msm_dp_ctrl *msm_dp_ctrl);

void msm_dp_ctrl_set_psr(struct msm_dp_ctrl *msm_dp_ctrl, bool enable);
void msm_dp_ctrl_config_psr(struct msm_dp_ctrl *msm_dp_ctrl);

int msm_dp_ctrl_core_clk_enable(struct msm_dp_ctrl *msm_dp_ctrl);
void msm_dp_ctrl_core_clk_disable(struct msm_dp_ctrl *msm_dp_ctrl);

void msm_dp_ctrl_enable_irq(struct msm_dp_ctrl *msm_dp_ctrl);
void msm_dp_ctrl_disable_irq(struct msm_dp_ctrl *msm_dp_ctrl);

#endif /* _DP_CTRL_H_ */
