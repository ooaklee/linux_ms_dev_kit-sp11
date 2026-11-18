/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __QCOM_MIPI_CSI2_CPHY_H__
#define __QCOM_MIPI_CSI2_CPHY_H__

#include <linux/types.h>

struct platform_device;

struct qcom_mipi_csi2_cphy_match {
	bool private_cphy;
};

extern const struct qcom_mipi_csi2_cphy_match x1e80100_cphy_match;

int phy_qcom_mipi_csi2_x1e_cphy_probe(struct platform_device *pdev);

#endif
