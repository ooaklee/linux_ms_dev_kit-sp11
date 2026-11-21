// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm MSM Camera Subsystem - CSID (CSI Decoder) Module
 *
 * Copyright (C) 2020-2025 Linaro Ltd.
 */
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/atomic.h>

#include "camss.h"
#include "camss-csid.h"
#include "camss-csid-gen2.h"

/* HW version register (same offset the core reads in csid_hw_version()). */
#define CSID_680_HW_VERSION 0x0

/* SP11 diagnostic: per-CSID log budget, reset at each stream start. */
static atomic_t csid_dbg_irqs[4] = {
	ATOMIC_INIT(0), ATOMIC_INIT(0), ATOMIC_INIT(0), ATOMIC_INIT(0)
};

#define CSID_TOP_IO_PATH_CFG0(csid)				(0x4 * (csid))
#define		CSID_TOP_IO_PATH_CFG0_INTERNAL_CSID		BIT(0)
#define		CSID_TOP_IO_PATH_CFG0_SFE_0			BIT(1)
#define		CSID_TOP_IO_PATH_CFG0_SFE_1			GENMASK(1, 0)
#define		CSID_TOP_IO_PATH_CFG0_SBI_0			BIT(4)
#define		CSID_TOP_IO_PATH_CFG0_SBI_1			GENMASK(3, 0)
#define		CSID_TOP_IO_PATH_CFG0_SBI_2			GENMASK(3, 1)
#define		CSID_TOP_IO_PATH_CFG0_OUTPUT_IFE_EN		BIT(8)
#define		CSID_TOP_IO_PATH_CFG0_SFE_OFFLINE_EN		BIT(12)

#define CSID_RESET_CMD						0x10
#define		CSID_RESET_CMD_HW_RESET				BIT(0)
#define		CSID_RESET_CMD_SW_RESET				BIT(1)
#define		CSID_RESET_CMD_IRQ_CTRL				BIT(2)

#define CSID_IRQ_CMD						0x14
#define		CSID_IRQ_CMD_CLEAR				BIT(0)
#define		CSID_IRQ_CMD_SET				BIT(4)

#define CSID_REG_UPDATE_CMD					0x18

#define CSID_CSI2_RDIN_IRQ_STATUS(rdi)					(0xec + 0x10 * (rdi))
#define		CSID_CSI2_RDIN_CCIF_VIOLATION				BIT(29)
#define		CSID_CSI2_RDIN_SENSOR_SWITCH_OUT_OF_SYNC_FRAME_DROP	BIT(28)
#define		CSID_CSI2_RDIN_ERROR_REC_WIDTH_VIOLATION		BIT(27)
#define		CSID_CSI2_RDIN_ERROR_REC_HEIGHT_VIOLATION		BIT(26)
#define		CSID_CSI2_RDIN_BATCH_END_MISSING_VIOLATION		BIT(25)
#define		CSID_CSI2_RDIN_ILLEGAL_BATCH_ID_IRQ			BIT(24)
#define		CSID_CSI2_RDIN_RUP_DONE					BIT(23)
#define		CSID_CSI2_RDIN_CAMIF_EPOCH_1_IRQ			BIT(22)
#define		CSID_CSI2_RDIN_CAMIF_EPOCH_0_IRQ			BIT(21)
#define		CSID_CSI2_RDIN_ERROR_REC_OVERFLOW_IRQ			BIT(19)
#define		CSID_CSI2_RDIN_ERROR_REC_FRAME_DROP			BIT(18)
#define		CSID_CSI2_RDIN_VCDT_GRP_CHANG				BIT(17)
#define		CSID_CSI2_RDIN_VCDT_GRP_0_SEL				BIT(16)
#define		CSID_CSI2_RDIN_VCDT_GRP_1_SEL				BIT(15)
#define		CSID_CSI2_RDIN_ERROR_LINE_COUNT				BIT(14)
#define		CSID_CSI2_RDIN_ERROR_PIX_COUNT				BIT(13)
#define		CSID_CSI2_RDIN_INFO_INPUT_SOF				BIT(12)
#define		CSID_CSI2_RDIN_INFO_INPUT_SOL				BIT(11)
#define		CSID_CSI2_RDIN_INFO_INPUT_EOL				BIT(10)
#define		CSID_CSI2_RDIN_INFO_INPUT_EOF				BIT(9)
#define		CSID_CSI2_RDIN_INFO_FRAME_DROP_SOF			BIT(8)
#define		CSID_CSI2_RDIN_INFO_FRAME_DROP_SOL			BIT(7)
#define		CSID_CSI2_RDIN_INFO_FRAME_DROP_EOL			BIT(6)
#define		CSID_CSI2_RDIN_INFO_FRAME_DROP_EOF			BIT(5)
#define		CSID_CSI2_RDIN_INFO_CAMIF_SOF				BIT(4)
#define		CSID_CSI2_RDIN_INFO_CAMIF_EOF				BIT(3)
#define		CSID_CSI2_RDIN_INFO_FIFO_OVERFLOW			BIT(2)
#define		CSID_CSI2_RDIN_RES1					BIT(1)
#define		CSID_CSI2_RDIN_RES0					BIT(0)

#define CSID_CSI2_RDIN_IRQ_MASK(rdi)				(0xf0 + 0x10 * (rdi))
#define CSID_CSI2_RDIN_IRQ_CLEAR(rdi)				(0xf4 + 0x10 * (rdi))
#define CSID_CSI2_RDIN_IRQ_SET(rdi)				(0xf8 + 0x10 * (rdi))

#define CSID_TOP_IRQ_STATUS					0x7c
#define CSID_TOP_IRQ_MASK					0x80
#define CSID_TOP_IRQ_CLEAR					0x84
#define		CSID_TOP_IRQ_RESET				BIT(0)
#define		CSID_TOP_IRQ_RX					BIT(2)
#define		CSID_TOP_IRQ_LONG_PKT(rdi)			(BIT(8) << (rdi))
#define		CSID_TOP_IRQ_BUF_DONE				BIT(13)

#define CSID_BUF_DONE_IRQ_STATUS				0x8c
#define	BUF_DONE_IRQ_STATUS_RDI_OFFSET				(csid_is_lite(csid) ? 1 : 14)
#define CSID_BUF_DONE_IRQ_MASK					0x90
#define CSID_BUF_DONE_IRQ_CLEAR					0x94

#define CSID_CSI2_RX_IRQ_STATUS					0x9c
#define CSID_CSI2_RX_IRQ_MASK					0xa0
#define CSID_CSI2_RX_IRQ_CLEAR					0xa4

#define CSID_RESET_CFG						0xc
#define		CSID_RESET_CFG_MODE_IMMEDIATE			BIT(0)
#define		CSID_RESET_CFG_LOCATION_COMPLETE		BIT(4)

#define CSID_CSI2_RDI_IRQ_STATUS(rdi)				(0xec + 0x10 * (rdi))
#define CSID_CSI2_RDI_IRQ_MASK(rdi)				(0xf0 + 0x10 * (rdi))
#define CSID_CSI2_RDI_IRQ_CLEAR(rdi)				(0xf4 + 0x10 * (rdi))

#define CSID_CSI2_RX_CFG0					0x200
#define		CSI2_RX_CFG0_NUM_ACTIVE_LANES			0
#define		CSI2_RX_CFG0_DL0_INPUT_SEL			4
#define		CSI2_RX_CFG0_DL1_INPUT_SEL			8
#define		CSI2_RX_CFG0_DL2_INPUT_SEL			12
#define		CSI2_RX_CFG0_DL3_INPUT_SEL			16
#define		CSI2_RX_CFG0_PHY_NUM_SEL			20
#define		CSI2_RX_CFG0_PHY_SEL_BASE_IDX			1
#define		CSI2_RX_CFG0_PHY_TYPE_SEL			24
#define		CSI2_RX_CFG0_TPG_MUX_EN				BIT(27)
#define		CSI2_RX_CFG0_TPG_MUX_SEL			GENMASK(29, 28)
#define		CSI2_RX_CFG0_TPG_NUM_SEL			BIT(28)

#define CSID_CSI2_RX_CFG1					0x204
#define		CSI2_RX_CFG1_PACKET_ECC_CORRECTION_EN		BIT(0)
#define		CSI2_RX_CFG1_DE_SCRAMBLE_EN			BIT(1)
#define		CSI2_RX_CFG1_VC_MODE				BIT(2)
#define		CSI2_RX_CFG1_COMPLETE_STREAM_EN			BIT(4)
#define		CSI2_RX_CFG1_COMPLETE_STREAM_FRAME_TIMING	BIT(5)
#define		CSI2_RX_CFG1_MISR_EN				BIT(6)
#define		CSI2_RX_CFG1_CGC_MODE				BIT(7)

#define CSID_CSI2_RX_CAPTURE_CTRL				0x208
#define		CSI2_RX_CAPTURE_CTRL_LONG_PKT_EN		BIT(0)
#define		CSI2_RX_CAPTURE_CTRL_SHORT_PKT_EN		BIT(1)
#define		CSI2_RX_CAPTURE_CTRL_CPHY_PKT_EN		BIT(2)
#define		CSI2_RX_CAPTURE_CTRL_LONG_PKT_DT		GENMASK(9, 4)
#define		CSI2_RX_CAPTURE_CTRL_LONG_PKT_VC		GENMASK(14, 10)
#define		CSI2_RX_CAPTURE_CTRL_SHORT_PKT_VC		GENMASK(19, 15)
#define		CSI2_RX_CAPTURE_CTRL_CPHY_PKT_DT		GENMASK(20, 25)
#define		CSI2_RX_CAPTURE_CTRL_CPHY_PKT_VC		GENMASK(30, 26)

#define CSID_CSI2_RX_TOTAL_PKTS_RCVD				0x240
#define CSID_CSI2_RX_STATS_ECC					0x244
#define CSID_CSI2_RX_CRC_ERRORS					0x248

#define CSID_RDI_CFG0(rdi)					(0x500 + 0x100 * (rdi))
#define		RDI_CFG0_DECODE_FORMAT				12
#define		RDI_CFG0_DATA_TYPE				16
#define		RDI_CFG0_VIRTUAL_CHANNEL			22
#define		RDI_CFG0_DT_ID					27
#define		RDI_CFG0_ENABLE					BIT(31)

#define CSID_RDI_CTRL(rdi)					(0x504 + 0x100 * (rdi))
#define		CSID_RDI_CTRL_HALT_CMD_HALT_AT_FRAME_BOUNDARY	0
#define		CSID_RDI_CTRL_HALT_CMD_RESUME_AT_FRAME_BOUNDARY	1

#define CSID_RDI_CFG1(rdi)					(0x510 + 0x100 * (rdi))
#define		RDI_CFG1_TIMESTAMP_STB_FRAME			BIT(0)
#define		RDI_CFG1_TIMESTAMP_STB_IRQ			BIT(1)
#define		RDI_CFG1_BYTE_CNTR_EN				BIT(2)
#define		RDI_CFG1_TIMESTAMP_EN				BIT(4)
#define		RDI_CFG1_DROP_H_EN				BIT(5)
#define		RDI_CFG1_DROP_V_EN				BIT(6)
#define		RDI_CFG1_CROP_H_EN				BIT(7)
#define		RDI_CFG1_CROP_V_EN				BIT(8)
#define		RDI_CFG1_MISR_EN				BIT(9)
#define		RDI_CFG1_PLAIN_ALIGN_MSB			BIT(11)
#define		RDI_CFG1_EARLY_EOF_EN				BIT(14)
#define		RDI_CFG1_PACKING_MIPI				BIT(15)

#define CSID_RDI_ERR_RECOVERY_CFG0(rdi)				(0x514 + 0x100 * (rdi))
#define CSID_RDI_EPOCH_IRQ_CFG(rdi)				(0x52c + 0x100 * (rdi))
#define CSID_RDI_FRM_DROP_PATTERN(rdi)				(0x540 + 0x100 * (rdi))
#define CSID_RDI_FRM_DROP_PERIOD(rdi)				(0x544 + 0x100 * (rdi))
#define CSID_RDI_IRQ_SUBSAMPLE_PATTERN(rdi)			(0x548 + 0x100 * (rdi))
#define CSID_RDI_IRQ_SUBSAMPLE_PERIOD(rdi)			(0x54c + 0x100 * (rdi))
#define CSID_RDI_RPP_HCROP(rdi)					(0x550 + 0x100 * (rdi))
#define CSID_RDI_PIX_DROP_PATTERN(rdi)				(0x558 + 0x100 * (rdi))
#define CSID_RDI_PIX_DROP_PERIOD(rdi)				(0x55c + 0x100 * (rdi))
#define CSID_RDI_LINE_DROP_PATTERN(rdi)				(0x560 + 0x100 * (rdi))
#define CSID_RDI_LINE_DROP_PERIOD(rdi)				(0x564 + 0x100 * (rdi))

static inline int reg_update_rdi(struct csid_device *csid, int n)
{
	return BIT(4 + n) + BIT(20 + n);
}

static void csid_reg_update(struct csid_device *csid, int port_id)
{
	csid->reg_update |= reg_update_rdi(csid, port_id);
	writel(csid->reg_update, csid->base + CSID_REG_UPDATE_CMD);
}

static inline void csid_reg_update_clear(struct csid_device *csid,
					 int port_id)
{
	csid->reg_update &= ~reg_update_rdi(csid, port_id);
	writel(csid->reg_update, csid->base + CSID_REG_UPDATE_CMD);
}

static void __csid_configure_rx(struct csid_device *csid,
				struct csid_phy_config *phy, int vc)
{
	u32 val;
	struct camss *camss;

	camss = csid->camss;
	if (csid_is_denali_cphy(csid)) {
		/* Zero active lanes and input zero encode the supported trio 0. */
		val = BIT(CSI2_RX_CFG0_PHY_TYPE_SEL);
	} else {
		val = (phy->lane_cnt - 1) << CSI2_RX_CFG0_NUM_ACTIVE_LANES;
		val |= phy->lane_assign << CSI2_RX_CFG0_DL0_INPUT_SEL;
	}

	if (camss->tpg && csid->tpg_linked &&
	    camss->tpg[phy->csiphy_id].testgen.mode != TPG_PAYLOAD_MODE_DISABLED) {
		val |= FIELD_PREP(CSI2_RX_CFG0_TPG_MUX_SEL, phy->csiphy_id + 1);
		val |= CSI2_RX_CFG0_TPG_MUX_EN;
	} else {
		val |= (phy->csiphy_id + CSI2_RX_CFG0_PHY_SEL_BASE_IDX)
			<< CSI2_RX_CFG0_PHY_NUM_SEL;
		if (csid_is_denali_cphy(csid))
			val |= CSI2_RX_CFG0_TPG_NUM_SEL;
	}

	writel(val, csid->base + CSID_CSI2_RX_CFG0);

	dev_info(csid->camss->dev,
		 "csid%d rx_cfg0=0x%08x (lanes=%u assign=0x%x phy=%u vc=%d tpg=%d)\n",
		 csid->id, val, phy->lane_cnt, phy->lane_assign,
		 phy->csiphy_id, vc, csid->tpg_linked);

	val = CSI2_RX_CFG1_PACKET_ECC_CORRECTION_EN;
	if (vc > 3)
		val |= CSI2_RX_CFG1_VC_MODE;
	writel(val, csid->base + CSID_CSI2_RX_CFG1);
}

static void __csid_ctrl_rdi(struct csid_device *csid, int enable, u8 rdi)
{
	u32 val;

	if (enable)
		val = CSID_RDI_CTRL_HALT_CMD_RESUME_AT_FRAME_BOUNDARY;
	else
		val = CSID_RDI_CTRL_HALT_CMD_HALT_AT_FRAME_BOUNDARY;

	writel(val, csid->base + CSID_RDI_CTRL(rdi));
}

static void __csid_configure_top(struct csid_device *csid)
{
	u32 val;

	val = CSID_TOP_IO_PATH_CFG0_OUTPUT_IFE_EN | CSID_TOP_IO_PATH_CFG0_INTERNAL_CSID;
	writel(val, csid->camss->csid_wrapper_base +
	    CSID_TOP_IO_PATH_CFG0(csid->id));
}

static void __csid_configure_rdi_stream(struct csid_device *csid, u8 enable, u8 port, u8 vc)
{
	struct v4l2_mbus_framefmt *input_format = &csid->fmt[MSM_CSID_PAD_FIRST_SRC + port];
	struct v4l2_mbus_framefmt *sink_format = &csid->fmt[MSM_CSID_PAD_SINK];
	const struct csid_format_info *format = csid_get_fmt_entry(csid->res->formats->formats,
								   csid->res->formats->nformats,
								   input_format->code);
	bool sp11_imx681 = csid_is_sp11_imx681_format(csid, sink_format);
	bool crop = csid_is_cphy_raw10_3844(csid, sink_format);
	u8 lane_cnt = csid->phy.lane_cnt;
	u8 dt_id;
	u32 val;

	if (!lane_cnt)
		lane_cnt = 4;

	/* Preserve the existing reset-value path outside the qualified route. */
	if (!sp11_imx681)
		writel(0, csid->base + CSID_RDI_FRM_DROP_PERIOD(port));

	/*
	 * DT_ID is a two bit bitfield that is concatenated with
	 * the four least significant bits of the five bit VC
	 * bitfield to generate an internal CID value.
	 *
	 * CSID_RDI_CFG0(port)
	 * DT_ID : 28:27
	 * VC    : 26:22
	 * DT    : 21:16
	 *
	 * CID   : VC 3:0 << 2 | DT_ID 1:0
	 */
	dt_id = port & 0x03;
	val = 0;

	/* Decoding is required for the explicitly selected receiver crop. */
	if (crop)
		val |= format->decode_format << RDI_CFG0_DECODE_FORMAT;
	else
		val |= DECODE_FORMAT_PAYLOAD_ONLY << RDI_CFG0_DECODE_FORMAT;
	val |= format->data_type << RDI_CFG0_DATA_TYPE;
	val |= vc << RDI_CFG0_VIRTUAL_CHANNEL;
	val |= dt_id << RDI_CFG0_DT_ID;
	writel(val, csid->base + CSID_RDI_CFG0(port));

	val = RDI_CFG1_TIMESTAMP_STB_FRAME;
	val |= RDI_CFG1_BYTE_CNTR_EN;
	val |= RDI_CFG1_TIMESTAMP_EN;
	if (!sp11_imx681) {
		val |= RDI_CFG1_DROP_H_EN;
		val |= RDI_CFG1_DROP_V_EN;
		val |= RDI_CFG1_CROP_H_EN;
		val |= RDI_CFG1_CROP_V_EN;
	}
	val |= RDI_CFG1_PACKING_MIPI;
	if (crop) {
		/* C-PHY RAW10 3844 is the IMX681 route: crop to 4800-byte rows. */
		writel(3839 << 16, csid->base + CSID_RDI_RPP_HCROP(port));
		val |= RDI_CFG1_CROP_H_EN;
	}

	writel(val, csid->base + CSID_RDI_CFG1(port));

	if (sp11_imx681) {
		/* Program an explicit keep-all state for every drop engine. */
		writel(1, csid->base + CSID_RDI_FRM_DROP_PERIOD(port));
		writel(0, csid->base + CSID_RDI_FRM_DROP_PATTERN(port));
		writel(1, csid->base + CSID_RDI_PIX_DROP_PERIOD(port));
		writel(0, csid->base + CSID_RDI_PIX_DROP_PATTERN(port));
		writel(1, csid->base + CSID_RDI_LINE_DROP_PERIOD(port));
		writel(0, csid->base + CSID_RDI_LINE_DROP_PATTERN(port));
	}

	val = 0;
	writel(val, csid->base + CSID_RDI_IRQ_SUBSAMPLE_PERIOD(port));

	val = 1;
	writel(val, csid->base + CSID_RDI_IRQ_SUBSAMPLE_PATTERN(port));

	val = 0;
	writel(val, csid->base + CSID_RDI_CTRL(port));

	val = readl(csid->base + CSID_RDI_CFG0(port));
	if (enable)
		val |= RDI_CFG0_ENABLE;
	else
		val &= ~RDI_CFG0_ENABLE;
	writel(val, csid->base + CSID_RDI_CFG0(port));
}

static void csid_log_status(struct csid_device *csid)
{
	int i;

	/* SP11: any X1E80100 sensor gets the packet counters, not just IMX681. */
	if (csid->camss->res->version != CAMSS_X1E80100)
		return;

	dev_info(csid->camss->dev,
		 "csid%d stop: pkts=%u ecc=%u crc=%u rx_cfg0=%#010x rx_cfg1=%#010x rx_mask=%#010x rx_irq=%#010x top=%#010x bufdone=%#010x\n",
		 csid->id,
		 readl(csid->base + CSID_CSI2_RX_TOTAL_PKTS_RCVD),
		 readl(csid->base + CSID_CSI2_RX_STATS_ECC),
		 readl(csid->base + CSID_CSI2_RX_CRC_ERRORS),
		 readl(csid->base + CSID_CSI2_RX_CFG0),
		 readl(csid->base + CSID_CSI2_RX_CFG1),
		 readl(csid->base + CSID_CSI2_RX_IRQ_MASK),
		 readl(csid->base + CSID_CSI2_RX_IRQ_STATUS),
		 readl(csid->base + CSID_TOP_IRQ_STATUS),
		 readl(csid->base + CSID_BUF_DONE_IRQ_STATUS));

	for (i = 0; i < MSM_CSID_MAX_SRC_STREAMS; i++) {
		if (!(csid->phy.en_vc & BIT(i)))
			continue;

		dev_info(csid->camss->dev,
			 "csid%d rdi%d: irq=%#010x cfg0=%#010x ctrl=%#010x cfg1=%#010x\n",
			 csid->id, i,
			 readl(csid->base + CSID_CSI2_RDIN_IRQ_STATUS(i)),
			 readl(csid->base + CSID_RDI_CFG0(i)),
			 readl(csid->base + CSID_RDI_CTRL(i)),
			 readl(csid->base + CSID_RDI_CFG1(i)));
	}
}

static void csid_configure_stream(struct csid_device *csid, u8 enable)
{
	int i;

	if (enable)
		dev_info(csid->camss->dev,
			 "csid%d configure: csiphy=%u lanes=%u lane_assign=0x%x en_vc=0x%x mbus=0x%x\n",
			 csid->id, csid->phy.csiphy_id, csid->phy.lane_cnt,
			 csid->phy.lane_assign, csid->phy.en_vc,
			 csid->fmt[0].code);

	atomic_set(&csid_dbg_irqs[csid->id & 3], 0);

	__csid_configure_top(csid);

	/* Loop through all enabled ports and configure a stream for each */
	for (i = 0; i < MSM_CSID_MAX_SRC_STREAMS; i++) {
		if (csid->phy.en_vc & BIT(i)) {
			__csid_configure_rdi_stream(csid, enable, i, 0);
			__csid_configure_rx(csid, &csid->phy, 0);
			__csid_ctrl_rdi(csid, enable, i);
		}
	}

	if (enable)
		dev_info(csid->camss->dev,
			 "csid%d post-config: hw=0x%08x rx_cfg0=0x%08x rx_cfg1=0x%08x rdi0_cfg0=0x%08x rdi0_ctrl=0x%08x\n",
			 csid->id, readl(csid->base + CSID_680_HW_VERSION),
			 readl(csid->base + CSID_CSI2_RX_CFG0),
			 readl(csid->base + CSID_CSI2_RX_CFG1),
			 readl(csid->base + CSID_RDI_CFG0(0)),
			 readl(csid->base + CSID_RDI_CTRL(0)));
}

/*
 * csid_reset - Trigger reset on CSID module and wait to complete
 * @csid: CSID device
 *
 * Return 0 on success or a negative error code otherwise
 */
static int csid_reset(struct csid_device *csid)
{
	unsigned long time;
	u32 val;
	int i;

	reinit_completion(&csid->reset_complete);

	writel(CSID_IRQ_CMD_CLEAR, csid->base + CSID_IRQ_CMD);

	/* preserve registers */
	val = CSID_RESET_CFG_MODE_IMMEDIATE | CSID_RESET_CFG_LOCATION_COMPLETE;
	writel(val, csid->base + CSID_RESET_CFG);

	val = CSID_RESET_CMD_HW_RESET | CSID_RESET_CMD_SW_RESET;
	writel(val, csid->base + CSID_RESET_CMD);

	time = wait_for_completion_timeout(&csid->reset_complete,
					   msecs_to_jiffies(CSID_RESET_TIMEOUT_MS));
	if (!time) {
		dev_err(csid->camss->dev, "CSID reset timeout\n");
		return -EIO;
	}

	for (i = 0; i < MSM_CSID_MAX_SRC_STREAMS; i++) {
		/* Enable RUP done for the client port */
		writel(CSID_CSI2_RDIN_RUP_DONE, csid->base + CSID_CSI2_RDIN_IRQ_MASK(i));
	}

	/* Clear RDI status */
	writel(~0u, csid->base + CSID_BUF_DONE_IRQ_CLEAR);

	/* Enable BUF_DONE bit for all write-master client ports */
	writel(~0u, csid->base + CSID_BUF_DONE_IRQ_MASK);

	/* Unmask all TOP interrupts */
	writel(~0u, csid->base + CSID_TOP_IRQ_MASK);

	return 0;
}

static void csid_rup_complete(struct csid_device *csid, int rdi)
{
	csid_reg_update_clear(csid, rdi);
}

/*
 * csid_isr - CSID module interrupt service routine
 * @irq: Interrupt line
 * @dev: CSID device
 *
 * Return IRQ_HANDLED on success
 */
static irqreturn_t csid_isr(int irq, void *dev)
{
	struct csid_device *csid = dev;
	u32 buf_done_val, rx_val, val, val_top, rdi0_val = 0;
	int i;

	/* Latch and clear TOP status */
	val_top = readl(csid->base + CSID_TOP_IRQ_STATUS);
	writel(val_top, csid->base + CSID_TOP_IRQ_CLEAR);

	/* Latch and clear CSID_CSI2 status; keep the latched word for logging */
	rx_val = readl(csid->base + CSID_CSI2_RX_IRQ_STATUS);
	writel(rx_val, csid->base + CSID_CSI2_RX_IRQ_CLEAR);

	/* Latch and clear top level BUF_DONE status */
	buf_done_val = readl(csid->base + CSID_BUF_DONE_IRQ_STATUS);
	writel(buf_done_val, csid->base + CSID_BUF_DONE_IRQ_CLEAR);

	/* Process state for each RDI channel */
	for (i = 0; i < MSM_CSID_MAX_SRC_STREAMS; i++) {
		val = readl(csid->base + CSID_CSI2_RDIN_IRQ_STATUS(i));
		if (i == 0)
			rdi0_val = val;
		if (val)
			writel(val, csid->base + CSID_CSI2_RDIN_IRQ_CLEAR(i));

		if (val & CSID_CSI2_RDIN_RUP_DONE)
			csid_rup_complete(csid, i);

		if (buf_done_val & BIT(BUF_DONE_IRQ_STATUS_RDI_OFFSET + i))
			camss_buf_done(csid->camss, csid->id, i);
	}

	/* Issue clear command */
	writel(CSID_IRQ_CMD_CLEAR, csid->base + CSID_IRQ_CMD);

	/* Reset complete */
	if (val_top & CSID_TOP_IRQ_RESET)
		complete(&csid->reset_complete);

	/* Log real event traffic only; the reset interrupt alone is not data. */
	if ((val_top | rx_val | buf_done_val | rdi0_val) &&
	    atomic_inc_return(&csid_dbg_irqs[csid->id & 3]) <= 32)
		dev_info(csid->camss->dev,
			 "csid%d isr: top=0x%08x rx=0x%08x bufdone=0x%08x rdi0=0x%08x\n",
			 csid->id, val_top, rx_val, buf_done_val, rdi0_val);

	return IRQ_HANDLED;
}

static void csid_subdev_reg_update(struct csid_device *csid, int port_id, bool is_clear)
{
	if (is_clear)
		csid_reg_update_clear(csid, port_id);
	else
		csid_reg_update(csid, port_id);
}

static void csid_subdev_init(struct csid_device *csid) {}

const struct csid_hw_ops csid_ops_680 = {
	.configure_testgen_pattern = NULL,
	.configure_stream = csid_configure_stream,
	.log_status = csid_log_status,
	.hw_version = csid_hw_version,
	.isr = csid_isr,
	.reset = csid_reset,
	.src_pad_code = csid_src_pad_code,
	.subdev_init = csid_subdev_init,
	.reg_update = csid_subdev_reg_update,
};
