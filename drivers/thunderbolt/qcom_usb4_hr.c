// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm X1E USB4 host-router bring-up scaffold.
 *
 * By default this only validates that the DT describes the complete
 * host-router resource set.  The activation path is explicitly opt-in while
 * the firmware and Qualcomm-specific initialization sequence are incomplete.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/reset.h>
#include <linux/string.h>
#include <linux/unaligned.h>

#include "nhi.h"
#include "tb.h"
#include "tb_regs.h"

static const char * const qcom_usb4_hr_regs[] = {
	"router", "router_config", "tmu_config", "port_group",
	"sideband", "uc_ram", "uc_per", "uc_mbox", "nhi", "cfg",
	"debug", "usbap_config", "pcieap_config", "dpap0_aux",
	"dpap0_config", "dpap1_aux", "dpap1_config",
};

static const char * const qcom_usb4_hr_clocks[] = {
	"sys", "tmu", "ahb", "axi", "master", "phy_rx0", "phy_rx1",
	"sb", "dp0", "dp1",
};

static const char * const qcom_usb4_hr_resets[] = {
	"core", "phy_nocsr", "sys", "rx0", "rx1", "usb_pipe",
	"pcie_pipe", "tmu", "sideband_iface", "hia_master", "ahb",
	"dp0", "dp1",
};

enum qcom_usb4_hr_state {
	QCOM_USB4_HR_RESOURCE_ONLY,
	QCOM_USB4_HR_PREPARED,
	QCOM_USB4_HR_ACTIVATING,
	QCOM_USB4_HR_ACTIVE,
	QCOM_USB4_HR_FAILED,
	/* Frontend entry points stopped, not a physical power-state guarantee. */
	QCOM_USB4_HR_STOPPED,
};

struct qcom_usb4_hr {
	/* Serializes preparation, activation and all frontend diagnostics. */
	struct mutex lock;
	enum qcom_usb4_hr_state state;
	void *prepare_group;
	void *activation_group;
	bool stopping;
	bool cleanup_attempted;
	int cleanup_error;
	void __iomem *regs[ARRAY_SIZE(qcom_usb4_hr_regs)];
	void __iomem *ucsb;      /* UCS0 mailbox @ 0x81F20040, ACPI QCOM0CA4 */
	int uc_irq;
	void __iomem *win;       /* ACPI PRT0 flat window, window0 base */
	struct clk_bulk_data clocks[ARRAY_SIZE(qcom_usb4_hr_clocks)];
	struct reset_control_bulk_data resets[ARRAY_SIZE(qcom_usb4_hr_resets)];
	struct phy *usb4_phy;
	struct tb_nhi nhi;
	int ring_irq;
	int fw_irq;
	bool ring_irq_requested;
	bool fw_irq_requested;
	bool uc_irq_requested;
	bool clocks_enabled;
	bool resets_deasserted;
	bool phy_initialized;
};

static void qcom_usb4_hr_context_release(struct device *dev, void *res)
{
	/* devres frees this allocation; hardware teardown is separate. */
}

static struct qcom_usb4_hr *qcom_usb4_hr_get(struct device *dev)
{
	/* The common NHI core owns dev->driver_data once activated. */
	return devres_find(dev, qcom_usb4_hr_context_release, NULL, NULL);
}

static inline struct qcom_usb4_hr *qcom_usb4_hr_from_nhi(struct tb_nhi *nhi)
{
	return container_of(nhi, struct qcom_usb4_hr, nhi);
}

static irqreturn_t qcom_usb4_hr_fw_irq(int irq, void *data);
static irqreturn_t qcom_usb4_hr_fw_thread(int irq, void *data);
static irqreturn_t qcom_usb4_hr_uc_irq(int irq, void *data);

static void qcom_usb4_hr_shutdown_interrupts(struct tb_nhi *nhi)
{
	struct qcom_usb4_hr *hr = qcom_usb4_hr_from_nhi(nhi);

	/* Quiesce every producer before draining work or releasing its storage. */
	if (hr->uc_irq_requested) {
		devm_free_irq(nhi->dev, hr->uc_irq, hr);
		hr->uc_irq_requested = false;
	}
	if (hr->fw_irq_requested) {
		devm_free_irq(nhi->dev, hr->fw_irq, hr);
		hr->fw_irq_requested = false;
	}
	if (hr->ring_irq_requested) {
		devm_free_irq(nhi->dev, hr->ring_irq, nhi);
		hr->ring_irq_requested = false;
	}
	flush_work(&nhi->interrupt_work);
}

static int qcom_usb4_hr_init_interrupts(struct tb_nhi *nhi)
{
	struct qcom_usb4_hr *hr = qcom_usb4_hr_from_nhi(nhi);
	int ret;

	ret = devm_request_irq(nhi->dev, hr->ring_irq, nhi_msi,
			       IRQF_NO_SUSPEND, "thunderbolt", nhi);
	if (ret)
		return ret;
	hr->ring_irq_requested = true;

	/*
	 * The UC's event/notification line; the common NHI code has no use
	 * for it yet, but log activity so connect events become visible.
	 */
	ret = devm_request_threaded_irq(nhi->dev, hr->fw_irq,
					qcom_usb4_hr_fw_irq,
					qcom_usb4_hr_fw_thread,
					IRQF_NO_SUSPEND | IRQF_ONESHOT,
					"thunderbolt-fw", hr);
	if (ret)
		goto err_shutdown;
	hr->fw_irq_requested = true;

	if (hr->uc_irq >= 0) {
		ret = devm_request_irq(nhi->dev, hr->uc_irq,
				       qcom_usb4_hr_uc_irq, 0,
				       "qcom-usb4-hr-uc", hr);
		if (ret)
			goto err_shutdown;
		hr->uc_irq_requested = true;
	}
	return 0;

err_shutdown:
	qcom_usb4_hr_shutdown_interrupts(nhi);
	return ret;
}

static bool qcom_usb4_hr_is_present(struct tb_nhi *nhi)
{
	return !nhi->going_away;
}

static const struct tb_nhi_ops qcom_usb4_hr_nhi_ops = {
	.is_present = qcom_usb4_hr_is_present,
	.init_interrupts = qcom_usb4_hr_init_interrupts,
	.shutdown = qcom_usb4_hr_shutdown_interrupts,
};

/*
 * ACPI PRT0 _CRS gives ONE flat window (default 0x1563F000, 768K) and the
 * Windows filter addresses everything relative to it: fw at +0x13000,
 * uc ctl at +0x22000, gates at +0xd064/+0x18, ready at +0x18. The DTB's
 * separate low-block regions (0x15600000..0x15624fff) have no ACPI
 * backing; reads there alias host DRAM.
 */
/*
 * Windows arms the UC's interrupt controller as a separate step after the
 * UC boots (decoded from QcUsb4Filter function 0x14000ba7c): five
 * interrupt sources at router+0x20a8, port_group bits 21/22/23 at
 * +0xd000, and router config bits. Without it the UC never signals the
 * host - which is why the uc IRQ stayed silent through every plug test.
 * Runtime-toggleable because a near-identical write once regressed the
 * port1 USB path.
 */
static bool arm_int;
module_param_named(arm_int, arm_int, bool, 0644);
MODULE_PARM_DESC(arm_int, "apply the Windows UC interrupt-arming sequence after ready");

static unsigned long long window0 = 0x1563F000;
module_param(window0, ullong, 0444);
MODULE_PARM_DESC(window0, "USB4 router flat window physical base");

static int __qcom_usb4_hr_nhi_preflight(struct qcom_usb4_hr *hr,
					struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *ucsb;
	struct phy *usb4_phy;
	void *group;
	int ring_irq, fw_irq, uc_irq;
	int ret;

	lockdep_assert_held(&hr->lock);
	if (hr->state == QCOM_USB4_HR_PREPARED)
		return 0;
	if (hr->state != QCOM_USB4_HR_RESOURCE_ONLY)
		return -EBUSY;

	group = devres_open_group(dev, NULL, GFP_KERNEL);
	if (!group)
		return -ENOMEM;

	ring_irq = platform_get_irq_byname(pdev, "ring");
	if (ring_irq < 0) {
		ret = ring_irq;
		goto err_release;
	}
	fw_irq = platform_get_irq_byname(pdev, "fw");
	if (fw_irq < 0) {
		ret = fw_irq;
		goto err_release;
	}

	usb4_phy = devm_phy_get(dev, "usb4");
	if (IS_ERR(usb4_phy)) {
		ret = dev_err_probe(dev, PTR_ERR(usb4_phy),
				    "USB4 PHY is not available\n");
		goto err_release;
	}
	uc_irq = platform_get_irq_byname_optional(pdev, "uc");
	if (uc_irq < 0 && uc_irq != -ENXIO) {
		ret = uc_irq;
		goto err_release;
	}
	if (uc_irq < 0)
		dev_warn(dev, "no optional UC interrupt\n");

	ucsb = devm_ioremap(dev, 0x81F20040, 0x40);
	if (!ucsb)
		dev_warn(dev, "could not map UCS0 mailbox\n");

	devres_close_group(dev, group);
	hr->prepare_group = group;
	hr->ring_irq = ring_irq;
	hr->fw_irq = fw_irq;
	hr->uc_irq = uc_irq;
	hr->usb4_phy = usb4_phy;
	hr->ucsb = ucsb;
	hr->nhi.iobase = hr->regs[8];
	hr->nhi.ops = &qcom_usb4_hr_nhi_ops;
	hr->state = QCOM_USB4_HR_PREPARED;

	dev_info(dev,
		 "NHI preflight passed: aperture=%p ring_irq=%d fw_irq=%d; activation=%s\n",
		 hr->nhi.iobase, hr->ring_irq, hr->fw_irq,
		 device_property_read_bool(dev, "qcom,nhi-activate") ?
		 "requested" : "disabled");
	return 0;

err_release:
	devres_release_group(dev, group);
	return ret;
}

static int qcom_usb4_hr_nhi_preflight(struct qcom_usb4_hr *hr,
				      struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	/*
	 * The common NHI code is ready for platform users, but Qualcomm's
	 * router-specific firmware upload and power-on sequence is not. Keep
	 * this check explicitly opt-in so a DT node cannot accidentally turn
	 * the resource validator into a boot-critical driver.
	 */
	if (!device_property_read_bool(dev, "qcom,nhi-preflight") &&
	    !device_property_read_bool(dev, "qcom,nhi-activate"))
		return 0;

	return __qcom_usb4_hr_nhi_preflight(hr, pdev);
}

static irqreturn_t qcom_usb4_hr_fw_irq(int irq, void *data)
{
	struct qcom_usb4_hr *hr = data;

	dev_dbg(hr->nhi.dev, "fw_irq fired (irq %d)\n", irq);
	return IRQ_WAKE_THREAD;
}

static irqreturn_t qcom_usb4_hr_fw_thread(int irq, void *data)
{
	struct qcom_usb4_hr *hr = data;

	dev_info(hr->nhi.dev, "fw_irq thread: uc_per status %08x %08x %08x\n",
		 readl(hr->regs[6] + 0x00), readl(hr->regs[6] + 0x04),
		 readl(hr->regs[6] + 0x1000));
	return IRQ_HANDLED;
}

static u32 qcom_usb4_hr_rmw(void __iomem *addr, u32 value, u32 mask)
{
	u32 val = readl(addr);

	val &= ~mask;
	val |= value & mask;
	writel(val, addr);
	return val;
}

/*
 * 2026-08-26: fires here hard-lock the SoC silently (verified by netconsole
 * capture). Off until the correct X1P-aware way to release the clamp exists.
 */
static bool uc_skip_clamp = true;
module_param_named(skip_clamp, uc_skip_clamp, bool, 0444);
MODULE_PARM_DESC(skip_clamp,
		 "do not touch the raw USB4 AON clamp register (deadly on X1P)");

/*
 * 2026-08-26: a plain readl(usbap_config+0x00) also hard-locks the SoC
 * (verified by netconsole) - the aperture is unpowered in our state and
 * Windows only reaches it behind platform glue we don't run. Default off.
 */
static bool uc_usbap_allow;
module_param_named(usbap_allow, uc_usbap_allow, bool, 0444);
MODULE_PARM_DESC(usbap_allow,
		 "allow the deadly raw usbap_config RMW experiment");

/*
 * 2026-08-26 round-9: reading back "uc_ram" after the load returns LINUX
 * KERNEL STRINGS (driver-core rodata) and zero UC firmware strings - the
 * 0x15613000 window aliases host DRAM, so every "firmware load" sprayed
 * ~40KB into kernel memory (explains the corrupted activated flag and
 * Runtime PM underflows). Block the load until the real UC RAM/IOVA
 * mechanism is identified from the Windows ACPI resource table.
 */
static bool fw_load_allow;
module_param_named(fw_load_allow, fw_load_allow, bool, 0644);
MODULE_PARM_DESC(fw_load_allow,
		 "allow the UC firmware stream into the window0 fw area");

static int qcom_usb4_hr_validate_firmware(const struct firmware *fw)
{
	const u8 *data = fw->data;
	size_t remaining = fw->size;

	if (!remaining || remaining % sizeof(u32))
		return -EINVAL;

	while (remaining) {
		u32 target, count;
		size_t length;

		if (remaining < 2 * sizeof(u32))
			return -EINVAL;

		target = get_unaligned_le32(data);
		count = get_unaligned_le32(data + sizeof(u32));
		data += 2 * sizeof(u32);
		remaining -= 2 * sizeof(u32);

		if (!count || count > 0x4000 ||
		    !IS_ALIGNED(target, sizeof(u32)) ||
		    target > 56 * SZ_1K ||
		    count > remaining / sizeof(u32) ||
		    count > (56 * SZ_1K - target) / sizeof(u32))
			return -EINVAL;

		length = (size_t)count * sizeof(u32);
		data += length;
		remaining -= length;
	}

	return 0;
}

static int qcom_usb4_hr_uc_bringup(struct qcom_usb4_hr *hr)
{
	static const char * const fw_name =
		"qcom/x1e80100/microsoft/Denali/sp11-usb4-uc-fw.bin";
	void __iomem *uc_ctl = hr->regs[6] + 0x1000; /* uc_per + 0x1000 */
	void __iomem *uc_ram = hr->regs[5];         /* uc_ram (writes real;
						     * reads alias DRAM) */
	const struct firmware *fw;
	const u8 *data;
	size_t remaining;
	unsigned int i;
	u32 val;
	int ret;

	/*
	 * Sequence reverse-engineered from the Windows QcUsb4Filter driver
	 * (static analysis only; addresses and order documented in the
	 * experiment README). The UC implements the NHI ring protocol, so
	 * once it is running the common Thunderbolt stack can talk to it.
	 */
	val = readl(uc_ctl);
	dev_info(hr->nhi.dev, "hr-bring: uc_ctl status=%#x\n", val);
	if (val & BIT(0)) {
		/*
		 * Warm start: the UC is already executing (boot firmware or
		 * ADSP started it - we have never loaded real firmware).
		 * Do NOT halt it; just fall through to gate clearing so the
		 * router reaches a ring-protocol-accessible state.
		 */
		dev_info(hr->nhi.dev,
			 "USB4 UC already running (warm start); not halting\n");
	} else if (fw_load_allow) {
		ret = request_firmware(&fw, fw_name, hr->nhi.dev);
		if (ret)
			return dev_err_probe(hr->nhi.dev, ret,
					     "USB4 UC firmware unavailable\n");

		ret = qcom_usb4_hr_validate_firmware(fw);
		if (ret) {
			release_firmware(fw);
			return dev_err_probe(hr->nhi.dev, ret,
					     "bad UC firmware container\n");
		}

		/* Halt the UC, then stream {target, count, words[]} segments. */
		writel(0, uc_ctl);
		dev_info(hr->nhi.dev, "hr-bring: halted, streaming segments\n");

		data = fw->data;
		remaining = fw->size;
		while (remaining) {
			u32 target = get_unaligned_le32(data);
			u32 count = get_unaligned_le32(data + sizeof(u32));
			size_t length = (size_t)count * sizeof(u32);

			data += 2 * sizeof(u32);
			remaining -= 2 * sizeof(u32);
			for (i = 0; i < count; i++)
				writel(get_unaligned_le32(data + i * sizeof(u32)),
				       uc_ram + target + i * sizeof(u32));
			data += length;
			remaining -= length;
		}
		dev_info(hr->nhi.dev, "USB4 UC firmware loaded (%zu bytes)\n",
			 fw->size);
		release_firmware(fw);
	} else {
		/*
		 * No load allowed and no warm UC: halt and continue; ready
		 * may or may not come up, but nothing is corrupted.
		 */
		dev_info(hr->nhi.dev,
			 "hr-bring: firmware load SKIPPED (fw_load_allow=0)\n");
		writel(0, uc_ctl);
	}


	if (uc_skip_clamp) {
		dev_info(hr->nhi.dev, "hr-bring: clamp release SKIPPED\n");
	} else {
		/*
		 * The QMP driver leaves the USB4 AON clamp engaged (com_init
		 * sets it and only the autonomous-mode path clears it, which
		 * never runs for the router). With the clamp set the PHY pins
		 * are isolated and the UC cannot see the partner. Release it:
		 * the router owns the pins.
		 */
		struct device_node *np = hr->usb4_phy->dev.of_node;
		void __iomem *phy_base = of_iomap(np, 0);
		u32 v;

		dev_info(hr->nhi.dev, "hr-bring: clamp map=%p\n", phy_base);
		if (phy_base) {
			void __iomem *clmp = phy_base + 0x104;

			v = readl(clmp);
			dev_info(hr->nhi.dev, "hr-bring: clamp read=%#x\n", v);
			writel(v & ~0x01010101u, clmp);
			dev_info(hr->nhi.dev,
				 "hr-bring: USB4 AON clamp written, now=%#x\n",
				 readl(clmp));
			iounmap(phy_base);
		} else {
			dev_warn(hr->nhi.dev,
				 "could not map PHY to release clamp\n");
		}
	}

	/* Clear the two gates, then release the UC. */
	dev_info(hr->nhi.dev, "hr-bring: clearing port_group gate\n");
	/*
	 * Round-17: the two extra writes from the Windows trace (port_group
	 * 0x7800 config + router_config BIT0 enable) did NOT unlock the UC
	 * sideband and BROKE the port1 USB3 path (usb4-port1 enable fail).
	 * REVERTED to the minimal proven flow: two gates only.
	 */
	qcom_usb4_hr_rmw(hr->regs[3] + 0x64, 0, BIT(6));   /* port_group */
	dev_info(hr->nhi.dev, "hr-bring: clearing router_config gate\n");
	qcom_usb4_hr_rmw(hr->regs[1] + 0x18, 0, BIT(24));  /* router_config */
	if (!(val & BIT(0))) {
		dev_info(hr->nhi.dev, "hr-bring: releasing UC (GO)\n");
		qcom_usb4_hr_rmw(uc_ctl, 1, BIT(0));           /* GO */
	} else {
		dev_info(hr->nhi.dev, "hr-bring: warm UC, GO not reissued\n");
	}

	/*
	 * Ready (window0+0x18 BIT24) is a boot-done pulse the UC raises on
	 * a fresh halt+GO boot. A warm UC already consumed it, so only
	 * poll when we actually (re)started the UC ourselves.
	 */
	if (!(val & BIT(0))) {
		dev_info(hr->nhi.dev, "hr-bring: polling for UC ready\n");
		ret = readl_poll_timeout(hr->regs[0] + 0x18, val,
					 val & BIT(24), 5000,
					 10 * USEC_PER_SEC);
		if (ret)
			return dev_err_probe(hr->nhi.dev, ret,
					     "USB4 UC did not report ready\n");
		dev_info(hr->nhi.dev, "USB4 UC ready\n");
	} else {
		dev_info(hr->nhi.dev,
			 "hr-bring: warm UC, skipping ready poll\n");
		ret = 0;
	}

	/*
	 * UC interrupt/config arming (Windows order, values exact):
	 * +0x4/+0x2/+0x14 config bits, +0xd000 port_group bits 21-23,
	 * and the five interrupt sources at router+0x20a8.
	 */
	if (arm_int) {
		dev_info(hr->nhi.dev,
			 "hr-bring: arming UC interrupts (Windows sequence)\n");
		qcom_usb4_hr_rmw(hr->regs[0] + 0x4, 0x0, 0x1);
		qcom_usb4_hr_rmw(hr->regs[0] + 0x2, 0x5, 0x1);
		qcom_usb4_hr_rmw(hr->regs[3] + 0x0, 0x0, 0x800000);
		qcom_usb4_hr_rmw(hr->regs[3] + 0x0, 0x0, 0x400000);
		qcom_usb4_hr_rmw(hr->regs[3] + 0x0, 0x0, 0x200000);
		qcom_usb4_hr_rmw(hr->regs[0] + 0x4, 0x0, 0x3f00);
		qcom_usb4_hr_rmw(hr->regs[0] + 0x14, 0x0, 0x4);
		qcom_usb4_hr_rmw(hr->regs[0] + 0x14, 0x0, 0x2);
		qcom_usb4_hr_rmw(hr->regs[0] + 0x20a8, 0x0, 0x80000);
		qcom_usb4_hr_rmw(hr->regs[0] + 0x20a8, 0x0, 0x20000);
		qcom_usb4_hr_rmw(hr->regs[0] + 0x20a8, 0x0, 0x10000);
		qcom_usb4_hr_rmw(hr->regs[0] + 0x20a8, 0x0, 0x10);
		qcom_usb4_hr_rmw(hr->regs[0] + 0x20a8, 0x0, 0x8);
		qcom_usb4_hr_rmw(hr->regs[0] + 0x2, 0xc, 0xc);
		dev_info(hr->nhi.dev, "hr-bring: UC interrupts armed\n");
	}
	return 0;
}

static void qcom_usb4_hr_release_activation(struct qcom_usb4_hr *hr)
{
	struct device *dev = hr->nhi.dev;
	int ret;

	lockdep_assert_held(&hr->lock);
	if (hr->cleanup_attempted)
		return;
	hr->cleanup_attempted = true;
	if (hr->state == QCOM_USB4_HR_ACTIVE)
		nhi_remove(&hr->nhi);

	/* Domain release and IRQ/work quiescence precede managed storage. */
	if (hr->activation_group) {
		devres_release_group(dev, hr->activation_group);
		hr->activation_group = NULL;
	}
	hr->nhi.tx_rings = NULL;
	hr->nhi.rx_rings = NULL;
	hr->nhi.hop_count = 0;

	/* Keep the existing PHY/reset/clock order; no physical recovery claim. */
	if (hr->phy_initialized) {
		ret = phy_exit(hr->usb4_phy);
		if (ret) {
			dev_warn(dev, "USB4 PHY exit failed: %d\n", ret);
			if (!hr->cleanup_error)
				hr->cleanup_error = ret;
		} else {
			hr->phy_initialized = false;
		}
	}
	if (hr->resets_deasserted) {
		ret = reset_control_bulk_assert(ARRAY_SIZE(hr->resets), hr->resets);
		if (ret) {
			dev_warn(dev, "host-router reset assertion failed: %d\n", ret);
			if (!hr->cleanup_error)
				hr->cleanup_error = ret;
		} else {
			hr->resets_deasserted = false;
		}
	}
	if (hr->clocks_enabled) {
		clk_bulk_disable_unprepare(ARRAY_SIZE(hr->clocks), hr->clocks);
		hr->clocks_enabled = false;
	}
}

static int qcom_usb4_hr_activate(struct qcom_usb4_hr *hr,
					struct platform_device *pdev, bool force)
{
	struct device *dev = &pdev->dev;
	int ret;

	lockdep_assert_held(&hr->lock);
	if (hr->stopping)
		return -ENODEV;
	/*
	 * This is deliberately separate from the resource preflight.  The
	 * common NHI probe resets and touches the router, so only an explicitly
	 * named DT experiment may reach it.  No Qualcomm mailbox or firmware
	 * upload sequence is inferred here.  force=true bypasses the DT gate
	 * for the deferred sysfs-triggered activation.
	 */
	if (!force && !device_property_read_bool(dev, "qcom,nhi-activate"))
		return 0;
	if (hr->state == QCOM_USB4_HR_ACTIVE)
		return 0;
	if (hr->state == QCOM_USB4_HR_FAILED)
		return -EIO;
	if (hr->state == QCOM_USB4_HR_STOPPED)
		return -ENODEV;
	ret = __qcom_usb4_hr_nhi_preflight(hr, pdev);
	if (ret)
		return ret;

	hr->activation_group = devres_open_group(dev, NULL, GFP_KERNEL);
	if (!hr->activation_group)
		return -ENOMEM;
	hr->state = QCOM_USB4_HR_ACTIVATING;

	dev_info(dev, "hr-act: step0 activation entered%s\n",
		 force ? " (sysfs)" : "");

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(hr->clocks), hr->clocks);
	if (ret)
		goto err_release;
	hr->clocks_enabled = true;
	dev_info(dev, "hr-act: step1 clocks enabled\n");

	ret = reset_control_bulk_deassert(ARRAY_SIZE(hr->resets), hr->resets);
	if (ret)
		goto err_release;
	hr->resets_deasserted = true;
	dev_info(dev, "hr-act: step2 resets deasserted\n");

	dev_info(dev, "hr-act: step3 setting PHY mode TBT3\n");
	ret = phy_set_mode_ext(hr->usb4_phy, PHY_MODE_TBT, PHY_SUBMODE_TBT3);
	if (ret)
		goto err_release;
	dev_info(dev, "hr-act: step3a phy_set_mode_ext done\n");

	ret = phy_init(hr->usb4_phy);
	if (ret)
		goto err_release;
	hr->phy_initialized = true;
	dev_info(dev, "hr-act: step4 phy_init done\n");

	ret = qcom_usb4_hr_uc_bringup(hr);
	if (ret)
		goto err_release;
	dev_info(dev, "hr-act: step5 UC bringup done\n");

	dev_info(dev, "hr-act: step6 calling nhi_probe\n");
	ret = nhi_probe(&hr->nhi);
	if (ret)
		goto err_release;

	devres_close_group(dev, hr->activation_group);
	hr->state = QCOM_USB4_HR_ACTIVE;
	dev_info(dev,
		 "experimental Qualcomm USB4 NHI activated with UC firmware\n");
	return 0;

err_release:
	devres_close_group(dev, hr->activation_group);
	qcom_usb4_hr_release_activation(hr);
	/* A software unwind does not establish safe physical UC restart. */
	hr->state = QCOM_USB4_HR_FAILED;
	return dev_err_probe(dev, ret, "host-router activation failed\n");
}

static int qcom_usb4_hr_find_tb_domain(struct device *child, const void *data)
{
	return child->bus == &tb_bus_type;
}

/*
 * A child domain is an additional reason not to repeat bring-up. The
 * frontend state remains the readiness/ownership contract; child lookup
 * alone does not serialize activation against teardown.
 */
static bool qcom_usb4_hr_is_live(struct platform_device *pdev)
{
	struct device *tb_dev = device_find_child(&pdev->dev, NULL,
					qcom_usb4_hr_find_tb_domain);

	if (!tb_dev)
		return false;
	put_device(tb_dev);
	return true;
}

static int qcom_usb4_hr_lock_active(struct qcom_usb4_hr *hr)
{
	int ret;

	ret = mutex_lock_interruptible(&hr->lock);
	if (ret)
		return ret;
	if (hr->stopping || hr->state != QCOM_USB4_HR_ACTIVE) {
		ret = hr->state == QCOM_USB4_HR_FAILED ? -EIO : -ENODEV;
		mutex_unlock(&hr->lock);
		return ret;
	}
	return 0;
}

/*
 * Writes issue a raw config-space read to the host router, testing whether
 * the UC's command processor still answers long after the boot-time
 * handshake. Read ROUTER_CS_1 (router vendor ID) and report latency.
 */
static ssize_t uc_ping_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct qcom_usb4_hr *hr = qcom_usb4_hr_get(dev);
	struct device *tb_dev;
	struct tb *tb;
	ktime_t t0;
	int i;
	s64 us;
	u32 val = 0;
	int ret;

	if (!hr)
		return -ENODEV;
	ret = qcom_usb4_hr_lock_active(hr);
	if (ret)
		return ret;
	tb_dev = device_find_child(dev, NULL, qcom_usb4_hr_find_tb_domain);
	if (!tb_dev) {
		ret = -ENODEV;
		goto out_hr_unlock;
	}
	tb = container_of(tb_dev, struct tb, dev);
	ret = mutex_lock_interruptible(&tb->lock);
	if (ret)
		goto out_put;
	if (!tb->root_switch) {
		ret = -ENODEV;
		goto out_unlock;
	}

	t0 = ktime_get();
	ret = tb_sw_read(tb->root_switch, &val, TB_CFG_SWITCH, ROUTER_CS_1, 1);
	us = ktime_us_delta(ktime_get(), t0);

	dev_info(dev, "uc_ping: ret=%d val=%#x latency=%lld us\n",
		 ret, val, us);

	for (i = 0; i <= tb->root_switch->config.max_port_number; i++) {
		struct tb_port *port = &tb->root_switch->ports[i];
		u32 cs18 = 0, cs19 = 0;

		if (!port->cap_usb4)
			continue;
		tb_port_read(port, &cs18, TB_CFG_PORT,
			     port->cap_usb4 + PORT_CS_18, 1);
		tb_port_read(port, &cs19, TB_CFG_PORT,
			     port->cap_usb4 + PORT_CS_19, 1);
		dev_info(dev,
			 "uc_ping: port%d cap_usb4=%#x CS18=%#x CS19=%#x TIP=%d CPS=%d BE=%d\n",
			 i, port->cap_usb4, cs18, cs19,
			 !!(cs18 & PORT_CS_18_TIP), !!(cs18 & PORT_CS_18_CPS),
			 !!(cs18 & PORT_CS_18_BE));
	}
out_unlock:
	mutex_unlock(&tb->lock);
out_put:
	put_device(tb_dev);
out_hr_unlock:
	mutex_unlock(&hr->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(uc_ping);

/*
 * Replicates the Windows QcUsb4Filter post-bring-up flow: RMW of
 * usbap_config+0x10 (0x15681010) with value/mask 0xc/0xc. The static
 * trace shows this happens once after "router ready" and it is the best
 * candidate for what arms UC link training. Bracketed with markers so a
 * wedge here is attributable, like every other hardware step.
 */
static ssize_t uc_usbap_rmw_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct qcom_usb4_hr *hr = qcom_usb4_hr_get(dev);
	void __iomem *usbap;
	u32 val;
	int ret;

	if (!hr)
		return -ENODEV;
	ret = qcom_usb4_hr_lock_active(hr);
	if (ret)
		return ret;
	if (!uc_usbap_allow) {
		dev_info(dev,
			 "uc_usbap: BLOCKED (usbap_allow=0; raw read is a known instant lockup)\n");
		ret = -EPERM;
		goto out_unlock;
	}

	usbap = hr->regs[11]; /* usbap_config @ 0x15681000 */
	dev_info(dev, "hr-usbap: dumping before RMW\n");
	val = readl(usbap + 0x00);
	dev_info(dev, "hr-usbap: 0x00=%#x\n", val);
	val = readl(usbap + 0x10);
	dev_info(dev, "hr-usbap: 0x10 before=%#x\n", val);
	val = readl(usbap + 0x14);
	dev_info(dev, "hr-usbap: 0x14=%#x\n", val);

	val = qcom_usb4_hr_rmw(usbap + 0x10, 0xc, 0xc);
	dev_info(dev, "hr-usbap: RMW 0xc/0xc done, now=%#x\n",
		 readl(usbap + 0x10));
	ret = 0;
out_unlock:
	mutex_unlock(&hr->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(uc_usbap_rmw);

/*
 * Sideband-aperture dump (0x15612000, "sideband" region): the UC<->ps883x
 * SB interface state. A previous session's ucpeek read this region while
 * the domain was live without a lockup, unlike usbap_config. Each word is
 * logged immediately after its read so a fatal access is attributable to
 * one offset.
 */
static ssize_t uc_sb_dump_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct qcom_usb4_hr *hr = qcom_usb4_hr_get(dev);
	void __iomem *sb;
	int i, ret;

	if (!hr)
		return -ENODEV;
	ret = qcom_usb4_hr_lock_active(hr);
	if (ret)
		return ret;

	sb = hr->regs[4]; /* sideband @ 0x15612000 (reads may alias) */
	dev_info(dev, "hr-sb: dump start\n");
	for (i = 0; i < 0x40; i += 4) {
		u32 v = readl(sb + i);

		dev_info(dev, "hr-sb: [%#02x] = %#010x\n", i, v);
	}
	dev_info(dev, "hr-sb: dump done\n");
	mutex_unlock(&hr->lock);
	return count;
}
static DEVICE_ATTR_WO(uc_sb_dump);

/*
 * Scan the experimental UC RAM aperture for printable runs. Keep MMIO
 * reads aligned while preserving the logical byte offsets.
 *
 * Physical aperture ownership and host-read safety are not established
 * by this diagnostic.
 */
static ssize_t uc_ram_dump_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct qcom_usb4_hr *hr = qcom_usb4_hr_get(dev);
	void __iomem *ram;
	size_t off, run;
	char line[96];
	int ret;

	if (!hr)
		return -ENODEV;
	ret = qcom_usb4_hr_lock_active(hr);
	if (ret)
		return ret;

	ram = hr->regs[5]; /* uc_ram (reads may alias DRAM) */
	dev_info(dev, "hr-ram: scan start (0xe000 bytes)\n");
	for (off = 0; off < 0xe000; off += max_t(size_t, run, 4)) {
		u32 word = readl(ram + (off & ~3u));
		u8 c0 = (word >> ((off & 3) * 8)) & 0xff;

		if (c0 < 0x20 || c0 > 0x7e) {
			run = 4;
			continue;
		}
		run = 0;
		while (off + run < 0xe000 && run < sizeof(line) - 1) {
			u32 w = readl(ram + ((off + run) & ~3u));
			u8 c = (w >> (((off + run) & 3) * 8)) & 0xff;

			if (c < 0x20 || c > 0x7e)
				break;
			line[run++] = c;
		}
		line[run] = '\0';
		if (run >= 6)
			dev_info(dev, "hr-ram: +%#06zx: %s\n", off, line);
	}
	dev_info(dev, "hr-ram: scan done\n");
	mutex_unlock(&hr->lock);
	return count;
}
static DEVICE_ATTR_WO(uc_ram_dump);


/*
 * ACPI PRT0 declares a third interrupt (GSI 287 / SPI 255, edge +
 * wake-capable) that the DT never had. Log every firing: if this is the
 * UC firmware/completion line we will see it on plug/boot events.
 */
static irqreturn_t qcom_usb4_hr_uc_irq(int irq, void *data)
{
	struct qcom_usb4_hr *hr = data;

	dev_info(hr->nhi.dev, "hr-uc: uc irq %d fired\n", irq);
	return IRQ_HANDLED;
}

/*
 * Read-only dump of the UCS0 shared mailbox (ACPI QCOM0CA4, OperationRegion
 * USBC @ 0x81F20040, 0x2F bytes): INFO, UPDT, then 3 x {connected, mux,
 * res, vid16, sid16, state64}. Watch UPDT bits change on plug if the ADSP
 * populates this on Linux too.
 */
static ssize_t uc_mailbox_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct qcom_usb4_hr *hr = qcom_usb4_hr_get(dev);
	int i, n = 0, ret;

	if (!hr)
		return -ENODEV;
	ret = qcom_usb4_hr_lock_active(hr);
	if (ret)
		return ret;
	if (!hr->ucsb) {
		ret = -ENODEV;
		goto out_unlock;
	}
	for (i = 0; i < 0x2f; i += 4) {
		u32 v = readl(hr->ucsb + i);

		n += scnprintf(buf + n, PAGE_SIZE - n, "[%#02x] %#010x\n",
			       i, v);
	}
	ret = n;
out_unlock:
	mutex_unlock(&hr->lock);
	return ret;
}
static DEVICE_ATTR_RO(uc_mailbox);

/*
 * Deferred activation: runs the full bring-up (clocks, resets, PHY, clamp
 * release, UC firmware load/start, NHI probe) on demand. Used to start the
 * UC only after the Type-C stack has brought the retimer out of reset, so
 * the UC's init-time sideband discovery sees a live target.
 */
static ssize_t uc_activate_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct qcom_usb4_hr *hr = qcom_usb4_hr_get(dev);
	int ret;

	if (!hr)
		return -ENODEV;
	ret = mutex_lock_interruptible(&hr->lock);
	if (ret)
		return ret;
	if (hr->stopping) {
		ret = -ENODEV;
		goto out_unlock;
	}
	if (hr->state == QCOM_USB4_HR_ACTIVE) {
		ret = 0;
		goto out_unlock;
	}
	if (qcom_usb4_hr_is_live(pdev)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	ret = qcom_usb4_hr_activate(hr, pdev, true);
out_unlock:
	mutex_unlock(&hr->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(uc_activate);

static struct attribute *qcom_usb4_hr_attrs[] = {
	&dev_attr_uc_ping.attr,
	&dev_attr_uc_activate.attr,
	&dev_attr_uc_usbap_rmw.attr,
	&dev_attr_uc_sb_dump.attr,
	&dev_attr_uc_ram_dump.attr,
	&dev_attr_uc_mailbox.attr,
	NULL,
};
ATTRIBUTE_GROUPS(qcom_usb4_hr);

static void qcom_usb4_hr_stop(struct qcom_usb4_hr *hr)
{
	struct device *dev = hr->nhi.dev;

	mutex_lock(&hr->lock);
	hr->stopping = true;
	if (hr->state == QCOM_USB4_HR_STOPPED)
		goto out_unlock;
	if (hr->state == QCOM_USB4_HR_ACTIVE)
		qcom_usb4_hr_release_activation(hr);
	if (hr->cleanup_error)
		dev_warn(dev, "frontend stopped; physical cleanup failed: %d\n",
			 hr->cleanup_error);
	hr->state = QCOM_USB4_HR_STOPPED;
	if (hr->prepare_group) {
		devres_release_group(dev, hr->prepare_group);
		hr->prepare_group = NULL;
	}
	hr->usb4_phy = NULL;
	hr->ucsb = NULL;
	hr->nhi.ops = NULL;
	hr->nhi.iobase = NULL;
	/* Failed cleanup flags remain historical evidence, not usable handles. */
out_unlock:
	mutex_unlock(&hr->lock);
}

static int qcom_usb4_hr_probe(struct platform_device *pdev)
{
	struct qcom_usb4_hr *hr;
	int i, ret;

	hr = devres_alloc(qcom_usb4_hr_context_release, sizeof(*hr), GFP_KERNEL);
	if (!hr)
		return -ENOMEM;
	devres_add(&pdev->dev, hr);
	mutex_init(&hr->lock);
	hr->nhi.dev = &pdev->dev;

	for (i = 0; i < ARRAY_SIZE(qcom_usb4_hr_regs); i++) {
		hr->regs[i] = devm_platform_ioremap_resource_byname(pdev,
							 qcom_usb4_hr_regs[i]);
		if (IS_ERR(hr->regs[i]))
			return dev_err_probe(&pdev->dev, PTR_ERR(hr->regs[i]),
					     "missing MMIO resource %s\n",
					     qcom_usb4_hr_regs[i]);
	}

	for (i = 0; i < ARRAY_SIZE(qcom_usb4_hr_clocks); i++)
		hr->clocks[i].id = qcom_usb4_hr_clocks[i];

	ret = devm_clk_bulk_get(&pdev->dev, ARRAY_SIZE(hr->clocks),
					hr->clocks);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "missing host-router clocks\n");

	for (i = 0; i < ARRAY_SIZE(qcom_usb4_hr_resets); i++)
		hr->resets[i].id = qcom_usb4_hr_resets[i];

	/* Identify reset ownership failures individually while bringing this up. */
	for (i = 0; i < ARRAY_SIZE(hr->resets); i++) {
		/* The QMP PHY owns this reset on the DT wiring used here. */
		if (!strcmp(hr->resets[i].id, "phy_nocsr"))
			hr->resets[i].rstc = devm_reset_control_get_optional_shared(
				&pdev->dev, hr->resets[i].id);
		else
			hr->resets[i].rstc = devm_reset_control_get_shared(&pdev->dev,
								 hr->resets[i].id);
		if (IS_ERR(hr->resets[i].rstc))
			return dev_err_probe(&pdev->dev, PTR_ERR(hr->resets[i].rstc),
					     "reset %s unavailable\n",
					     hr->resets[i].id);
	}

	mutex_lock(&hr->lock);
	ret = qcom_usb4_hr_nhi_preflight(hr, pdev);
	if (!ret)
		ret = qcom_usb4_hr_activate(hr, pdev, false);
	mutex_unlock(&hr->lock);
	if (ret) {
		qcom_usb4_hr_stop(hr);
		return ret;
	}

	dev_info(&pdev->dev,
		 hr->state == QCOM_USB4_HR_ACTIVE ?
		 "resource validation passed; host-router NHI is active\n" :
		 "resource validation passed; host-router hardware access is disabled\n");
	return 0;
}

static void qcom_usb4_hr_remove(struct platform_device *pdev)
{
	struct qcom_usb4_hr *hr = qcom_usb4_hr_get(&pdev->dev);

	/* Driver core has removed and drained dev_groups before this callback. */
	qcom_usb4_hr_stop(hr);
}

static void qcom_usb4_hr_shutdown(struct platform_device *pdev)
{
	struct qcom_usb4_hr *hr = qcom_usb4_hr_get(&pdev->dev);

	/* Shutdown, unlike unbind, does not drain the driver's sysfs groups. */
	mutex_lock(&hr->lock);
	hr->stopping = true;
	mutex_unlock(&hr->lock);
	device_remove_groups(&pdev->dev, qcom_usb4_hr_groups);
	qcom_usb4_hr_stop(hr);
}

static const struct of_device_id qcom_usb4_hr_of_match[] = {
	{ .compatible = "qcom,x1e80100-usb4-hr" },
	{ .compatible = "qcom,usb4-hr" },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_usb4_hr_of_match);

static struct platform_driver qcom_usb4_hr_driver = {
	.probe = qcom_usb4_hr_probe,
	.remove = qcom_usb4_hr_remove,
	.shutdown = qcom_usb4_hr_shutdown,
	.driver = {
		.name = "qcom-usb4-hr",
		.of_match_table = qcom_usb4_hr_of_match,
		.dev_groups = qcom_usb4_hr_groups,
	},
};
module_platform_driver(qcom_usb4_hr_driver);

MODULE_DESCRIPTION("Qualcomm X1E USB4 host-router bring-up scaffold");
MODULE_LICENSE("GPL");
