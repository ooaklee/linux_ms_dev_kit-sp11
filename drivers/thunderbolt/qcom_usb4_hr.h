/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _QCOM_USB4_HR_H_
#define _QCOM_USB4_HR_H_

#include <linux/kconfig.h>

#if IS_ENABLED(CONFIG_USB4_QCOM_EXPERIMENTAL)
int qcom_usb4_hr_register(void);
void qcom_usb4_hr_unregister(void);
#else
static inline int qcom_usb4_hr_register(void)
{
	return 0;
}

static inline void qcom_usb4_hr_unregister(void)
{
}
#endif

#endif /* _QCOM_USB4_HR_H_ */
