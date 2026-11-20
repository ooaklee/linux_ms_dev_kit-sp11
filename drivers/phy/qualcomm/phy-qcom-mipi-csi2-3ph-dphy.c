// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm MSM Camera Subsystem - CSIPHY Module 3phase v1.0
 *
 * Copyright (c) 2011-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2016-2025 Linaro Ltd.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/time64.h>

#include "phy-qcom-mipi-csi2.h"

#define CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(offset, n)	((offset) + 0x4 * (n))
#define CSIPHY_3PH_CMN_CSI_COMMON_CTRL0_PHY_SW_RESET	BIT(0)
#define CSIPHY_3PH_CMN_CSI_COMMON_CTRL5_CLK_ENABLE	BIT(7)
#define CSIPHY_3PH_CMN_CSI_COMMON_CTRL6_COMMON_PWRDN_B	BIT(0)
#define CSIPHY_3PH_CMN_CSI_COMMON_CTRL6_SHOW_REV_ID	BIT(1)
#define CSIPHY_3PH_CMN_CSI_COMMON_CTRL10_IRQ_CLEAR_CMD	BIT(0)
#define CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(offset, n)	((offset) + 0xb0 + 0x4 * (n))

/*
 * 2.1.2 2-phase lane block layout, as used by the Surface Pro 11 Windows
 * driver (qccammipicsi8380.sys, CameraMIPIPHY.c). The lane blocks are
 * 0x400 apart and each carries its own reset line at +0x18; the clock
 * lane sits at 0x0e00.
 */
#define CSIPHY_2PH_LANE_BLOCK(n)	(0x0400 * (n))
#define CSIPHY_2PH_LANE_BLOCK_NUM	4
#define CSIPHY_2PH_CLK_BLOCK		0x0e00
#define CSIPHY_2PH_LANE_RESET		0x18
#define CSIPHY_2PH_REFGEN_STATUS	0x10fc
#define CSIPHY_2PH_REFGEN_READY		BIT(7)

/*
 * 3 phase CSI has 19 common status regs with only 0-10 being used
 * and 11-18 being reserved.
 */
#define CSI_COMMON_STATUS_NUM				11
/*
 * There are a number of common control registers
 * The offset to clear the CSIPHY IRQ status starts @ 22
 * So to clear CSI_COMMON_STATUS0 this is CSI_COMMON_CONTROL22, STATUS1 is
 * CONTROL23 and so on
 */
#define CSI_CTRL_STATUS_INDEX				22

/*
 * There are 43 COMMON_CTRL registers with regs after # 33 being reserved
 */
#define CSI_CTRL_MAX					33

#define CSIPHY_DEFAULT_PARAMS				0
#define CSIPHY_SETTLE_CNT_LOWER_BYTE			2
#define CSIPHY_SKEW_CAL					7

/* 4nm 2PH v 2.1.2 2p5Gbps 4 lane DPHY mode
 *
 * Note: the common CONTROL registers (0x1014/0x1018/0x101c) were dropped
 * from this imported sequence: phy_qcom_mipi_csi2_lanes_enable() writes
 * them from the actual negotiated configuration (lane count!) and the
 * stale 4-lane values here silently overrode that write, leaving data
 * lanes 1-3 enabled for 1-lane transmitters (observed on X1E80100,
 * Surface Pro 11, VD55G0: CTRL5 read back 0xd5 after configure(1 lane)).
 * Lane blocks for unused data lanes are skipped at replay time.
 */
static const struct
mipi_csi2phy_lane_regs lane_regs_x1e80100[] = {
	/* Power up lanes 2ph mode */
	{.reg_addr = 0x101c, .reg_data = 0x7a, .param_type = CSIPHY_DEFAULT_PARAMS},

	{.reg_addr = 0x0094, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x00a0, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0090, .reg_data = 0x0f, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0098, .reg_data = 0x08, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0094, .reg_data = 0x07, .delay_us = 0x01, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0030, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0000, .reg_data = 0x8e, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0038, .reg_data = 0xfe, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x002c, .reg_data = 0x01, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0034, .reg_data = 0x0f, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x001c, .reg_data = 0x0a, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0014, .reg_data = 0x60, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x003c, .reg_data = 0xb8, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0004, .reg_data = 0x0c, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0020, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0008, .reg_data = 0x10, .param_type = CSIPHY_SETTLE_CNT_LOWER_BYTE},
	{.reg_addr = 0x0010, .reg_data = 0x52, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0094, .reg_data = 0xd7, .param_type = CSIPHY_SKEW_CAL},
	{.reg_addr = 0x005c, .reg_data = 0x00, .param_type = CSIPHY_SKEW_CAL},
	{.reg_addr = 0x0060, .reg_data = 0xbd, .param_type = CSIPHY_SKEW_CAL},
	{.reg_addr = 0x0064, .reg_data = 0x7f, .param_type = CSIPHY_SKEW_CAL},

	{.reg_addr = 0x0e94, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0ea0, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e90, .reg_data = 0x0f, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e98, .reg_data = 0x08, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e94, .reg_data = 0x07, .delay_us =  0x01, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e30, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e28, .reg_data = 0x04, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e00, .reg_data = 0x80, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e0c, .reg_data = 0xff, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e38, .reg_data = 0x1f, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e2c, .reg_data = 0x01, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e34, .reg_data = 0x0f, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e1c, .reg_data = 0x0a, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e14, .reg_data = 0x60, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e3c, .reg_data = 0xb8, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e04, .reg_data = 0x0c, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e20, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0e08, .reg_data = 0x10, .param_type = CSIPHY_SETTLE_CNT_LOWER_BYTE},
	{.reg_addr = 0x0e10, .reg_data = 0x52, .param_type = CSIPHY_DEFAULT_PARAMS},

	{.reg_addr = 0x0494, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x04a0, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0490, .reg_data = 0x0f, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0498, .reg_data = 0x08, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0494, .reg_data = 0x07, .delay_us =  0x01, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0430, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0400, .reg_data = 0x8e, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0438, .reg_data = 0xfe, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x042c, .reg_data = 0x01, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0434, .reg_data = 0x0f, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x041c, .reg_data = 0x0a, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0414, .reg_data = 0x60, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x043c, .reg_data = 0xb8, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0404, .reg_data = 0x0c, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0420, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0408, .reg_data = 0x10, .param_type = CSIPHY_SETTLE_CNT_LOWER_BYTE},
	{.reg_addr = 0x0410, .reg_data = 0x52, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0494, .reg_data = 0xd7, .param_type = CSIPHY_SKEW_CAL},
	{.reg_addr = 0x045c, .reg_data = 0x00, .param_type = CSIPHY_SKEW_CAL},
	{.reg_addr = 0x0460, .reg_data = 0xbd, .param_type = CSIPHY_SKEW_CAL},
	{.reg_addr = 0x0464, .reg_data = 0x7f, .param_type = CSIPHY_SKEW_CAL},

	{.reg_addr = 0x0894, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x08a0, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0890, .reg_data = 0x0f, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0898, .reg_data = 0x08, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0894, .reg_data = 0x07, .delay_us =  0x01, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0830, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0800, .reg_data = 0x8e, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0838, .reg_data = 0xfe, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x082c, .reg_data = 0x01, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0834, .reg_data = 0x0f, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x081c, .reg_data = 0x0a, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0814, .reg_data = 0x60, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x083c, .reg_data = 0xb8, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0804, .reg_data = 0x0c, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0820, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0808, .reg_data = 0x10, .param_type = CSIPHY_SETTLE_CNT_LOWER_BYTE},
	{.reg_addr = 0x0810, .reg_data = 0x52, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0894, .reg_data = 0xd7, .param_type = CSIPHY_SKEW_CAL},
	{.reg_addr = 0x085c, .reg_data = 0x00, .param_type = CSIPHY_SKEW_CAL},
	{.reg_addr = 0x0860, .reg_data = 0xbd, .param_type = CSIPHY_SKEW_CAL},
	{.reg_addr = 0x0864, .reg_data = 0x7f, .param_type = CSIPHY_SKEW_CAL},

	{.reg_addr = 0x0c94, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0ca0, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0c90, .reg_data = 0x0f, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0c98, .reg_data = 0x08, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0c94, .reg_data = 0x07, .delay_us =  0x01, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0c30, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0c00, .reg_data = 0x8e, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0c38, .reg_data = 0xfe, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0c2c, .reg_data = 0x01, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0c34, .reg_data = 0x0f, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0c1c, .reg_data = 0x0a, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0c14, .reg_data = 0x60, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0c3c, .reg_data = 0xb8, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0c04, .reg_data = 0x0c, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0c20, .reg_data = 0x00, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0c08, .reg_data = 0x10, .param_type = CSIPHY_SETTLE_CNT_LOWER_BYTE},
	{.reg_addr = 0x0c10, .reg_data = 0x52, .param_type = CSIPHY_DEFAULT_PARAMS},
	{.reg_addr = 0x0c94, .reg_data = 0xd7, .param_type = CSIPHY_SKEW_CAL},
	{.reg_addr = 0x0c5c, .reg_data = 0x00, .param_type = CSIPHY_SKEW_CAL},
	{.reg_addr = 0x0c60, .reg_data = 0xbd, .param_type = CSIPHY_SKEW_CAL},
	{.reg_addr = 0x0c64, .reg_data = 0x7f, .param_type = CSIPHY_SKEW_CAL},
};

static inline const struct mipi_csi2phy_device_regs *
csi2phy_dev_to_regs(struct mipi_csi2phy_device *csi2phy)
{
	return &csi2phy->soc_cfg->reg_info;
}

static void phy_qcom_mipi_csi2_hw_version_read(struct mipi_csi2phy_device *csi2phy)
{
	const struct mipi_csi2phy_device_regs *regs = csi2phy_dev_to_regs(csi2phy);
	u32 tmp;

	writel(CSIPHY_3PH_CMN_CSI_COMMON_CTRL6_SHOW_REV_ID, csi2phy->base +
	       CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(regs->common_regs_offset, 6));

	tmp = readl_relaxed(csi2phy->base +
			    CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(regs->common_regs_offset, 12));
	csi2phy->hw_version = tmp;

	tmp = readl_relaxed(csi2phy->base +
			    CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(regs->common_regs_offset, 13));
	csi2phy->hw_version |= (tmp << 8) & 0xFF00;

	tmp = readl_relaxed(csi2phy->base +
			    CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(regs->common_regs_offset, 14));
	csi2phy->hw_version |= (tmp << 16) & 0xFF0000;

	tmp = readl_relaxed(csi2phy->base +
			    CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(regs->common_regs_offset, 15));
	csi2phy->hw_version |= (tmp << 24) & 0xFF000000;

	dev_dbg_once(csi2phy->dev, "CSIPHY 3PH HW Version = 0x%08x\n", csi2phy->hw_version);
}

/*
 * phy_qcom_mipi_csi2_lane_reset - Pulse the per-lane reset lines
 * @csi2phy: CSIPHY device
 *
 * The Windows driver (CameraMIPIPHY.c, CSIDPhyReset) releases the common
 * block from software reset and then pulses the reset line of every lane
 * block: clock lane first, then data lanes 0-3, each held asserted for
 * 100 us. The imported GPL 2.1.2 sequence does not contain these writes at
 * all, so without this the lane front-ends stay in their post-reset state
 * on X1E80100 and no lane ever reports ready.
 */
static void phy_qcom_mipi_csi2_lane_reset(struct mipi_csi2phy_device *csi2phy)
{
	int i;

	for (i = 0; i <= CSIPHY_2PH_LANE_BLOCK_NUM; i++) {
		u32 block = i ? CSIPHY_2PH_LANE_BLOCK(i - 1) : CSIPHY_2PH_CLK_BLOCK;

		writel(0x1, csi2phy->base + block + CSIPHY_2PH_LANE_RESET);
		udelay(100);
		writel(0x0, csi2phy->base + block + CSIPHY_2PH_LANE_RESET);
	}
}

/*
 * phy_qcom_mipi_csi2_wait_refgen - Wait for the reference generator
 * @csi2phy: CSIPHY device
 *
 * The vendor driver refuses to start the PHY until bit 7 of the refgen
 * status register reads back set. Poll it for up to 100 ms. A timeout is
 * reported but does not abort the start sequence: the register reports
 * readiness only, and some firmware leaves it unasserted while the PHY
 * still operates.
 */
static bool phy_qcom_mipi_csi2_wait_refgen(struct mipi_csi2phy_device *csi2phy)
{
	int i;

	for (i = 0; i < 100; i++) {
		if (readl_relaxed(csi2phy->base + CSIPHY_2PH_REFGEN_STATUS) &
		    CSIPHY_2PH_REFGEN_READY)
			return true;
		usleep_range(1000, 1500);
	}

	return false;
}

/*
 * phy_qcom_mipi_csi2_reset - Perform software reset on CSIPHY module
 * @phy_qcom_mipi_csi2: CSIPHY device
 */
static void phy_qcom_mipi_csi2_reset(struct mipi_csi2phy_device *csi2phy)
{
	const struct mipi_csi2phy_device_regs *regs = csi2phy_dev_to_regs(csi2phy);

	if (!phy_qcom_mipi_csi2_wait_refgen(csi2phy))
		dev_warn(csi2phy->dev, "reference generator not ready after 100 ms\n");

	writel(CSIPHY_3PH_CMN_CSI_COMMON_CTRL0_PHY_SW_RESET,
	       csi2phy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(regs->common_regs_offset, 0));
	/* The vendor driver holds the reset for 20 us, not milliseconds. */
	usleep_range(20, 30);
	writel(0x0, csi2phy->base +
	       CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(regs->common_regs_offset, 0));

	phy_qcom_mipi_csi2_lane_reset(csi2phy);
}

/*
 * phy_qcom_mipi_csi2_settle_cnt_calc - Calculate settle count value
 *
 * Helper function to calculate settle count value. This is
 * based on the CSI2 T_hs_settle parameter which in turn
 * is calculated based on the CSI2 transmitter link frequency.
 *
 * Return settle count value or 0 if the CSI2 link frequency
 * is not available
 */
/*
 * phy_qcom_mipi_csi2_settle_cnt_calc - Calculate the settle count
 * @link_freq: MIPI D-PHY lane bit rate in Hz
 *
 * The hardware counts the settle time in fixed 2.5 ns units; the vendor
 * driver computes
 *
 *	((1000 / data_rate_mbps) * 6 + 85) / 2.5 - 10
 *
 * with integer truncation at every step and writes the result straight
 * into the lane and clock settle registers. Deriving the count from the
 * CSIPHY timer clock period instead (as this driver used to) yields a
 * count ~35% too small on X1E80100: 0x13 vs 0x1d at 420 Mbps, i.e. the
 * realizable settle time falls below the D-PHY floor.
 */
static u8 phy_qcom_mipi_csi2_settle_cnt_calc(s64 link_freq)
{
	u32 data_rate_mbps;
	u32 t_hs_prepare_max_ns;
	u32 settle_cnt;

	if (link_freq <= 0)
		return 0;

	data_rate_mbps = div_u64(link_freq, 1000000);
	if (!data_rate_mbps)
		return 0;

	/* (1000 / data_rate) * 6, truncated to whole nanoseconds */
	t_hs_prepare_max_ns = 6000 / data_rate_mbps;
	t_hs_prepare_max_ns += 85;

	/* ... divided by 2.5, truncated, then offset by the fixed 6 UI */
	settle_cnt = (t_hs_prepare_max_ns * 2) / 5;
	if (settle_cnt < 10)
		return 0;

	settle_cnt -= 10;

	return min_t(u32, settle_cnt, 0xff);
}

static void
phy_qcom_mipi_csi2_gen2_config_lanes(struct mipi_csi2phy_device *csi2phy,
				     u8 settle_cnt, u8 num_data_lanes)
{
	const struct mipi_csi2phy_device_regs *regs = csi2phy_dev_to_regs(csi2phy);
	const struct mipi_csi2phy_lane_regs *r = regs->init_seq;
	int i, array_size = regs->lane_array_size;
	u32 val;

	for (i = 0; i < array_size; i++, r++) {
		/*
		 * The imported sequence programs all four D-PHY data-lane
		 * blocks (0x0000/0x0400/0x0800/0x0c00). Skip the blocks of
		 * lanes the transmitter does not drive; leaving them
		 * initialized caused idle-lane interference on X1E80100
		 * 1-lane transmitters. The clock-lane block (0x0e00) and
		 * the common block (0x1000+) are always programmed.
		 */
		if (num_data_lanes > 0 && (r->reg_addr & 0x0c00) &&
		    (r->reg_addr & 0x0c00) >> 10 >= num_data_lanes &&
		    (r->reg_addr & 0x0e00) != 0x0e00)
			continue;

		switch (r->param_type) {
		case CSIPHY_SETTLE_CNT_LOWER_BYTE:
			val = settle_cnt & 0xff;
			break;
		case CSIPHY_SKEW_CAL:
			/* TODO: support application of skew from dt flag */
			continue;
		default:
			val = r->reg_data;
			break;
		}
		writel(val, csi2phy->base + r->reg_addr);
		if (r->delay_us)
			udelay(r->delay_us);
	}
}

static int phy_qcom_mipi_csi2_lanes_enable(struct mipi_csi2phy_device *csi2phy,
					   struct mipi_csi2phy_stream_cfg *cfg)
{
	const struct mipi_csi2phy_device_regs *regs = csi2phy_dev_to_regs(csi2phy);
	struct mipi_csi2phy_lanes_cfg *lane_cfg = &cfg->lane_cfg;
	u8 settle_cnt;
	u8 val;
	int i;

	settle_cnt = phy_qcom_mipi_csi2_settle_cnt_calc(cfg->link_freq);

	/*
	 * Common-block analog setup, matching downstream 2.1.2
	 * (csiphy_common_reg_2_1_2) and the Windows-observed sequence for
	 * this chip: {0x1084 = 0}, {0x108C = 0 + 1 ms}. Without these the
	 * receiver front-end never settles on X1E80100.
	 */
	writel(0x00, csi2phy->base + 0x1084);
	writel(0x00, csi2phy->base + 0x108c);
	usleep_range(1000, 1100);

	val = CSIPHY_3PH_CMN_CSI_COMMON_CTRL5_CLK_ENABLE;
	for (i = 0; i < cfg->num_data_lanes; i++)
		val |= BIT(lane_cfg->data[i].pos * 2);

	writel(val, csi2phy->base +
	       CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(regs->common_regs_offset, 5));

	val = CSIPHY_3PH_CMN_CSI_COMMON_CTRL6_COMMON_PWRDN_B;
	writel(val, csi2phy->base +
	       CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(regs->common_regs_offset, 6));

	val = 0x02;
	writel(val, csi2phy->base +
	       CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(regs->common_regs_offset, 7));

	val = 0x00;
	writel(val, csi2phy->base +
	       CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(regs->common_regs_offset, 0));

	phy_qcom_mipi_csi2_gen2_config_lanes(csi2phy, settle_cnt,
					     cfg->num_data_lanes);

	/*
	 * Release the PHY from idle into 2-phase mission mode. Downstream
	 * cam_csiphy_2_1_2_hwreg.h ends the D-PHY (2ph) sequence with
	 * {0x1000, 0x02} (3ph uses 0x0e); without this write the receiver
	 * never leaves idle and no lane lock is possible. Observed on
	 * X1E80100 + VD55G0: CTRL5/settle correct but zero reception with
	 * CTRL0 left at 0x00.
	 */
	writel(0x02, csi2phy->base +
	       CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(regs->common_regs_offset, 0));

	/* IRQ_MASK registers - disable all interrupts */
	for (i = CSI_COMMON_STATUS_NUM; i < CSI_CTRL_STATUS_INDEX; i++) {
		writel(0, csi2phy->base +
		       CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(regs->common_regs_offset, i));
	}

	return 0;
}

static void
phy_qcom_mipi_csi2_lanes_disable(struct mipi_csi2phy_device *csi2phy,
				 struct mipi_csi2phy_stream_cfg *cfg)
{
	const struct mipi_csi2phy_device_regs *regs = csi2phy_dev_to_regs(csi2phy);

	writel(0, csi2phy->base +
	       CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(regs->common_regs_offset, 5));

	writel(0, csi2phy->base +
	       CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(regs->common_regs_offset, 6));
}

static const struct mipi_csi2phy_hw_ops phy_qcom_mipi_csi2_ops_3ph_1_0 = {
	.hw_version_read = phy_qcom_mipi_csi2_hw_version_read,
	.reset = phy_qcom_mipi_csi2_reset,
	.lanes_enable = phy_qcom_mipi_csi2_lanes_enable,
	.lanes_disable = phy_qcom_mipi_csi2_lanes_disable,
};

static const char * const x1e_clks[] = {
	"camnoc_axi",
	"cpas_ahb",
	"csiphy",
	"csiphy_timer"
};

static const char * const x1e_supplies[] = {
	"vdda-0p8",
	"vdda-1p2"
};

static const char * const x1e_genpd_names[] = {
	"mx",
	"mmcx",
};

const struct mipi_csi2phy_soc_cfg mipi_csi2_dphy_4nm_x1e = {
	.ops = &phy_qcom_mipi_csi2_ops_3ph_1_0,
	.reg_info = {
		.init_seq = lane_regs_x1e80100,
		.lane_array_size = ARRAY_SIZE(lane_regs_x1e80100),
		.common_regs_offset = 0x1000,
		.generation = GEN2,
	},
	.supply_names = (const char **)x1e_supplies,
	.num_supplies = ARRAY_SIZE(x1e_supplies),
	.clk_names = (const char **)x1e_clks,
	.num_clk = ARRAY_SIZE(x1e_clks),
	.opp_clk = x1e_clks[2],
	.timer_clk = x1e_clks[3],
	.genpd_names = (const char **)x1e_genpd_names,
	.num_genpd_names = ARRAY_SIZE(x1e_genpd_names),
};
