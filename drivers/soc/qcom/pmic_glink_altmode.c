// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Ltd
 */
#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/spinlock.h>
#include <linux/soc/qcom/pdr.h>
#include <drm/bridge/aux-bridge.h>

#include <linux/usb/pd.h>
#include <linux/usb/typec_altmode.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_mux.h>
#include <linux/usb/typec_retimer.h>
#include <linux/usb/typec_tbt.h>

#include <linux/soc/qcom/pmic_glink.h>

#define PMIC_GLINK_MAX_PORTS	3
/* Bound firmware-driven memory use; a full queue is reported, not overwritten. */
#define PMIC_GLINK_MAX_EVENTS	64

#define USBC_SC8180X_NOTIFY_IND	0x13
#define USBC_CMD_WRITE_REQ      0x15
#define USBC_NOTIFY_IND		0x16

#define ALTMODE_PAN_EN		0x10
#define ALTMODE_PAN_ACK		0x11

struct usbc_write_req {
	struct pmic_glink_hdr   hdr;
	__le32 cmd;
	__le32 arg;
	__le32 reserved;
};

struct usbc_sc8280x_dp_data {
	u8 pin_assignment : 6;
	u8 hpd_state : 1;
	u8 hpd_irq : 1;
	u8 res[7];
};

/* Used for both TBT and USB4 notifications */
struct usbc_sc8280x_tbt_data {
	u8 usb_speed : 3;
	u8 cable_type : 3;
	/* This field is NOP on USB4, all cables support rounded rates by spec */
	u8 rounded_cable : 1;
	u8 power_limited : 1;
	u8 res[11];
};

struct usbc_notify {
	struct pmic_glink_hdr hdr;
	u8 port_idx;
	u8 orientation;
	u8 mux_ctrl;
#define MUX_CTRL_STATE_NO_CONN			0
#define MUX_CTRL_STATE_USB3_ONLY		1
#define MUX_CTRL_STATE_DP4LN			2
#define MUX_CTRL_STATE_USB3_DP			3
#define MUX_CTRL_STATE_TUNNELING		4

	u8 res;
	__le16 vid;
	__le16 svid;
	union usbc_sc8280x_extended_data {
		struct usbc_sc8280x_dp_data dp;
		struct usbc_sc8280x_tbt_data tbt;
	} extended_data;
};

struct usbc_sc8180x_notify {
	struct pmic_glink_hdr hdr;
	__le32 notification;
	__le32 reserved[2];
};

enum pmic_glink_altmode_pin_assignment {
	DPAM_HPD_OUT,
	DPAM_HPD_A,
	DPAM_HPD_B,
	DPAM_HPD_C,
	DPAM_HPD_D,
	DPAM_HPD_E,
	DPAM_HPD_F,
};

struct pmic_glink_altmode;

struct pmic_glink_altmode_event {
	u64 generation;
	enum typec_orientation orientation;
	u16 svid;
	struct usbc_sc8280x_tbt_data tbt_data;
	u8 mode;
	u8 hpd_state;
	u8 hpd_irq;
	u8 mux_ctrl;
	/* SC8180 notifications do not carry the SC8280 mux-control field. */
	bool has_mux_ctrl;
};

#define work_to_altmode_port(w) container_of((w), struct pmic_glink_altmode_port, work)

struct pmic_glink_altmode_port {
	struct pmic_glink_altmode *altmode;
	unsigned int index;

	struct typec_switch *typec_switch;
	struct typec_mux *typec_mux;
	struct typec_retimer *typec_retimer;
	struct typec_altmode dp_alt;
	struct typec_altmode tbt_alt;

	struct work_struct work;
	DECLARE_KFIFO(events, struct pmic_glink_altmode_event, PMIC_GLINK_MAX_EVENTS);

	struct auxiliary_device *bridge;
	/* Last published HPD status, serialized by lifecycle_lock, not readiness. */
	bool hpd_connected;
};

#define work_to_altmode(w) container_of((w), struct pmic_glink_altmode, enable_work)

struct pmic_glink_altmode {
	struct device *dev;

	unsigned int owner_id;

	/* To synchronize WRITE_REQ acks */
	struct mutex lock;
	/* Serializes provider/HPD apply and reconciliation, never request waits. */
	struct mutex lifecycle_lock;

	/* One waiter at a time under lock; completion does not identify a request. */
	struct completion pan_ack;
	struct pmic_glink_client *client;

	struct work_struct enable_work;
	/* Protects queues/state; never held over providers, mutexes or ACK waits. */
	spinlock_t event_lock;
	bool stopping;
	bool service_up;
	/* Software cleanup completed for generation, not firmware readiness. */
	bool reconciled;
	u64 generation;

	struct pmic_glink_altmode_port ports[PMIC_GLINK_MAX_PORTS];
};

static bool pmic_glink_altmode_current(struct pmic_glink_altmode *altmode, u64 generation)
{
	unsigned long flags;
	bool valid;

	spin_lock_irqsave(&altmode->event_lock, flags);
	valid = !altmode->stopping && altmode->service_up &&
		generation == altmode->generation;
	spin_unlock_irqrestore(&altmode->event_lock, flags);
	return valid;
}

static int pmic_glink_altmode_request(struct pmic_glink_altmode *altmode,
				      u32 cmd, u32 arg, u64 generation)
{
	struct usbc_write_req req = {};
	unsigned long flags;
	unsigned long left;
	int ret;

	/*
	 * The USBC_CMD_WRITE_REQ ack doesn't identify the request, so wait for
	 * one ack at a time.
	 */
	guard(mutex)(&altmode->lock);

	if (!pmic_glink_altmode_current(altmode, generation))
		return -ECONNRESET;

	/* Clear stored credits, not late replies arriving after a new request. */
	spin_lock_irqsave(&altmode->event_lock, flags);
	reinit_completion(&altmode->pan_ack);
	spin_unlock_irqrestore(&altmode->event_lock, flags);

	req.hdr.owner = cpu_to_le32(altmode->owner_id);
	req.hdr.type = cpu_to_le32(PMIC_GLINK_REQ_RESP);
	req.hdr.opcode = cpu_to_le32(USBC_CMD_WRITE_REQ);
	req.cmd = cpu_to_le32(cmd);
	req.arg = cpu_to_le32(arg);

	ret = pmic_glink_send_generation(altmode->client, &req, sizeof(req), generation);
	if (ret) {
		dev_err(altmode->dev, "failed to send altmode request: %#x (%d)\n", cmd, ret);
		return ret;
	}

	if (!pmic_glink_altmode_current(altmode, generation))
		return -ECONNRESET;

	left = wait_for_completion_timeout(&altmode->pan_ack, 5 * HZ);
	/* PDR/stop also wake the waiter; neither wake is a firmware response. */
	if (!pmic_glink_altmode_current(altmode, generation))
		return -ECONNRESET;
	if (!left) {
		dev_err(altmode->dev, "timeout waiting for altmode request ack for: %#x\n", cmd);
		return -ETIMEDOUT;
	}

	return 0;
}

static int pmic_glink_altmode_set_mode(struct pmic_glink_altmode *altmode,
				      struct pmic_glink_altmode_port *port,
				      const struct pmic_glink_altmode_event *event,
				      struct typec_altmode *alt,
				      unsigned long mode, void *data)
{
	struct typec_mux_state state = { .alt = alt, .mode = mode, .data = data };
	struct typec_retimer_state retimer_state = { .alt = alt, .mode = mode, .data = data };
	int ret;

	lockdep_assert_held(&altmode->lifecycle_lock);
	if (!pmic_glink_altmode_current(altmode, event->generation))
		return -ECONNRESET;

	ret = typec_mux_set(port->typec_mux, &state);
	if (ret) {
		dev_err(altmode->dev, "failed to switch mux on port %u: %d\n",
			port->index, ret);
		return ret;
	}

	if (!pmic_glink_altmode_current(altmode, event->generation))
		return -ECONNRESET;

	ret = typec_retimer_set(port->typec_retimer, &retimer_state);
	if (ret)
		dev_err(altmode->dev, "failed to set retimer on port %u: %d\n",
			port->index, ret);
	if (!ret && !pmic_glink_altmode_current(altmode, event->generation))
		return -ECONNRESET;
	return ret;
}

static int pmic_glink_altmode_enable_dp(struct pmic_glink_altmode *altmode,
					 struct pmic_glink_altmode_port *port,
					 const struct pmic_glink_altmode_event *event,
					 u8 mode, bool hpd_state,
					 bool hpd_irq)
{
	struct typec_displayport_data dp_data = {};

	dp_data.status = DP_STATUS_ENABLED;
	if (hpd_state)
		dp_data.status |= DP_STATUS_HPD_STATE;
	if (hpd_irq)
		dp_data.status |= DP_STATUS_IRQ_HPD;
	dp_data.conf = DP_CONF_SET_PIN_ASSIGN(mode);

	return pmic_glink_altmode_set_mode(altmode, port, event, &port->dp_alt,
					 TYPEC_MODAL_STATE(mode), &dp_data);
}

static int pmic_glink_altmode_enable_tbt(struct pmic_glink_altmode *altmode,
					 struct pmic_glink_altmode_port *port,
					 const struct pmic_glink_altmode_event *event)
{
	const struct usbc_sc8280x_tbt_data *tbt = &event->tbt_data;
	struct typec_thunderbolt_data tbt_data = {};
	u32 cable_speed;

	/* Device Discover Mode VDO */
	tbt_data.device_mode = TBT_MODE;
	tbt_data.device_mode |= TBT_SET_ADAPTER(TBT_ADAPTER_TBT3);

	/* Cable Discover Mode VDO */
	tbt_data.cable_mode = TBT_MODE;

	if (tbt->usb_speed == 0) {
		cable_speed = TBT_CABLE_USB3_PASSIVE;
	} else if (tbt->usb_speed == 1) {
		cable_speed = TBT_CABLE_10_AND_20GBPS;
	} else {
		dev_err(altmode->dev,
			"Got illegal TBT3 cable speed value (%u), falling back to passive\n",
			tbt->usb_speed);
		cable_speed = TBT_CABLE_USB3_PASSIVE;
	}
	tbt_data.cable_mode |= TBT_SET_CABLE_SPEED(cable_speed);

	if (tbt->cable_type) {
		tbt_data.cable_mode |= TBT_CABLE_ACTIVE_PASSIVE;
		tbt_data.cable_mode |= TBT_SET_CABLE_ROUNDED(tbt->rounded_cable);
	}

	/* Enter Mode VDO */
	tbt_data.enter_vdo |= TBT_MODE;
	tbt_data.enter_vdo |= TBT_ENTER_MODE_CABLE_SPEED(cable_speed);

	if (tbt->cable_type) {
		tbt_data.enter_vdo |= TBT_CABLE_ACTIVE_PASSIVE;
		tbt_data.enter_vdo |= TBT_SET_CABLE_ROUNDED(tbt->rounded_cable);
	}

	return pmic_glink_altmode_set_mode(altmode, port, event, &port->tbt_alt,
					 TYPEC_TBT_MODE, &tbt_data);
}

static int pmic_glink_altmode_enable_usb4(struct pmic_glink_altmode *altmode,
					  struct pmic_glink_altmode_port *port,
					  const struct pmic_glink_altmode_event *event)
{
	const struct usbc_sc8280x_tbt_data *tbt = &event->tbt_data;
	struct enter_usb_data data = {};

	data.eudo = FIELD_PREP(EUDO_USB_MODE_MASK, EUDO_USB_MODE_USB4);

	if (tbt->usb_speed == 0) {
		data.eudo |= FIELD_PREP(EUDO_CABLE_SPEED_MASK, EUDO_CABLE_SPEED_USB4_GEN2);
	} else if (tbt->usb_speed == 1) {
		data.eudo |= FIELD_PREP(EUDO_CABLE_SPEED_MASK, EUDO_CABLE_SPEED_USB4_GEN3);
	} else {
		pr_err("Got illegal USB4 cable speed value (%u), falling back to G2\n",
		       tbt->usb_speed);
		data.eudo |= FIELD_PREP(EUDO_CABLE_SPEED_MASK, EUDO_CABLE_SPEED_USB4_GEN2);
	}

	data.eudo |= FIELD_PREP(EUDO_CABLE_TYPE_MASK, tbt->cable_type);

	return pmic_glink_altmode_set_mode(altmode, port, event, NULL, TYPEC_MODE_USB4, &data);
}

static int pmic_glink_altmode_enable_usb(struct pmic_glink_altmode *altmode,
					  struct pmic_glink_altmode_port *port,
					  const struct pmic_glink_altmode_event *event)
{
	return pmic_glink_altmode_set_mode(altmode, port, event, NULL, TYPEC_STATE_USB, NULL);
}

static int pmic_glink_altmode_safe(struct pmic_glink_altmode *altmode,
				    struct pmic_glink_altmode_port *port,
				    const struct pmic_glink_altmode_event *event)
{
	return pmic_glink_altmode_set_mode(altmode, port, event, NULL, TYPEC_STATE_SAFE, NULL);
}

static void pmic_glink_altmode_apply(struct pmic_glink_altmode_port *alt_port,
				     const struct pmic_glink_altmode_event *event)
{
	struct pmic_glink_altmode *altmode = alt_port->altmode;
	bool dp = event->svid == USB_TYPEC_DP_SID;
	int ret;

	lockdep_assert_held(&altmode->lifecycle_lock);
	if (!pmic_glink_altmode_current(altmode, event->generation))
		return;

	ret = typec_switch_set(alt_port->typec_switch, event->orientation);
	if (ret) {
		dev_err(altmode->dev, "failed to set orientation on port %u: %d\n",
			alt_port->index, ret);
		goto out_hpd;
	}

	if (!pmic_glink_altmode_current(altmode, event->generation))
		return;

	/*
	 * MUX_CTRL_STATE_DP4LN/USB3_DP may only be set if SVID=DP, but we need
	 * to special-case the SVID=DP && mux_ctrl=NO_CONN case to deliver a
	 * HPD notification
	 */
	if (dp) {
		if (event->mux_ctrl == MUX_CTRL_STATE_NO_CONN) {
			ret = pmic_glink_altmode_safe(altmode, alt_port, event);
		} else {
			ret = pmic_glink_altmode_enable_dp(altmode, alt_port,
						    event,
						    event->mode,
						    event->hpd_state,
						    event->hpd_irq);
		}
	} else if (event->mux_ctrl == MUX_CTRL_STATE_TUNNELING) {
		if (event->svid == USB_TYPEC_TBT_SID)
			ret = pmic_glink_altmode_enable_tbt(altmode, alt_port, event);
		else
			ret = pmic_glink_altmode_enable_usb4(altmode, alt_port, event);
	} else if (event->mux_ctrl == MUX_CTRL_STATE_USB3_ONLY) {
		ret = pmic_glink_altmode_enable_usb(altmode, alt_port, event);
	} else if (event->mux_ctrl == MUX_CTRL_STATE_NO_CONN) {
		ret = pmic_glink_altmode_safe(altmode, alt_port, event);
	} else {
		dev_err(altmode->dev, "Got unknown mux_ctrl: %u on port %u, forcing safe mode\n",
			event->mux_ctrl, alt_port->index);
		ret = pmic_glink_altmode_safe(altmode, alt_port, event);
	}

out_hpd:
	if (!pmic_glink_altmode_current(altmode, event->generation))
		return;

	/* A real disconnect remains meaningful even if hardware programming failed. */
	if ((dp && (!event->hpd_state ||
		    (event->has_mux_ctrl && event->mux_ctrl == MUX_CTRL_STATE_NO_CONN))) ||
	    (event->has_mux_ctrl && event->mux_ctrl == MUX_CTRL_STATE_NO_CONN &&
	     alt_port->hpd_connected)) {
		drm_aux_hpd_bridge_notify_extra(&alt_port->bridge->dev,
						connector_status_disconnected,
						dp && event->hpd_irq ?
						DRM_CONNECTOR_DP_IRQ_HPD :
						DRM_CONNECTOR_NO_EXTRA_STATUS);
		alt_port->hpd_connected = false;
	} else if (!ret && dp) {
		drm_aux_hpd_bridge_notify_extra(&alt_port->bridge->dev,
						connector_status_connected,
						event->hpd_irq ?
						DRM_CONNECTOR_DP_IRQ_HPD :
						DRM_CONNECTOR_NO_EXTRA_STATUS);
		alt_port->hpd_connected = true;
	}
}

static void pmic_glink_altmode_worker(struct work_struct *work)
{
	struct pmic_glink_altmode_port *port = work_to_altmode_port(work);
	struct pmic_glink_altmode *altmode = port->altmode;
	struct pmic_glink_altmode_event event;
	unsigned long flags;

	scoped_guard(mutex, &altmode->lifecycle_lock) {
		spin_lock_irqsave(&altmode->event_lock, flags);
		if (altmode->stopping || !altmode->service_up || !altmode->reconciled ||
		    !kfifo_get(&port->events, &event)) {
			spin_unlock_irqrestore(&altmode->event_lock, flags);
			return;
		}
		spin_unlock_irqrestore(&altmode->event_lock, flags);

		pmic_glink_altmode_apply(port, &event);
	}

	/* Other ports/reconciliation may proceed while this request waits. */
	pmic_glink_altmode_request(altmode, ALTMODE_PAN_ACK, port->index, event.generation);

	/* Allow the freezer between events, including when an ACK times out. */
	spin_lock_irqsave(&altmode->event_lock, flags);
	if (!altmode->stopping && altmode->service_up && altmode->reconciled &&
	    !kfifo_is_empty(&port->events))
		queue_work(system_freezable_wq, &port->work);
	spin_unlock_irqrestore(&altmode->event_lock, flags);
}

static void pmic_glink_altmode_queue_event(struct pmic_glink_altmode_port *port,
					  const struct pmic_glink_altmode_event *event)
{
	struct pmic_glink_altmode *altmode = port->altmode;
	struct pmic_glink_altmode_event pending = *event;
	unsigned long flags;
	bool queued;

	spin_lock_irqsave(&altmode->event_lock, flags);
	if (altmode->stopping || !altmode->service_up) {
		spin_unlock_irqrestore(&altmode->event_lock, flags);
		return;
	}
	pending.generation = altmode->generation;
	queued = kfifo_put(&port->events, pending);
	if (queued)
		queue_work(system_freezable_wq, &port->work);
	spin_unlock_irqrestore(&altmode->event_lock, flags);

	/* Never overwrite older events or invent ACK/replay semantics on rejection. */
	if (!queued)
		dev_err_ratelimited(altmode->dev, "altmode event queue full on port %u\n",
				    port->index);
}

static enum typec_orientation pmic_glink_altmode_orientation(unsigned int orientation)
{
	if (orientation == 0)
		return TYPEC_ORIENTATION_NORMAL;
	else if (orientation == 1)
		return TYPEC_ORIENTATION_REVERSE;
	else
		return TYPEC_ORIENTATION_NONE;
}

#define SC8180X_PORT_MASK		0x000000ff
#define SC8180X_ORIENTATION_MASK	0x0000ff00
#define SC8180X_MUX_MASK		0x00ff0000
#define SC8180X_MODE_MASK		0x3f000000
#define SC8180X_HPD_STATE_MASK		0x40000000
#define SC8180X_HPD_IRQ_MASK		0x80000000

static void pmic_glink_altmode_sc8180xp_notify(struct pmic_glink_altmode *altmode,
					       const void *data, size_t len)
{
	struct pmic_glink_altmode_port *alt_port;
	struct pmic_glink_altmode_event event = {};
	const struct usbc_sc8180x_notify *msg;
	u32 notification;
	u8 orientation;
	u8 hpd_state;
	u8 hpd_irq;
	u16 svid;
	u8 port;
	u8 mode;
	u8 mux;

	if (len != sizeof(*msg)) {
		dev_warn(altmode->dev, "invalid length of USBC_NOTIFY indication: %zd\n", len);
		return;
	}

	msg = data;
	notification = le32_to_cpu(msg->notification);
	port = FIELD_GET(SC8180X_PORT_MASK, notification);
	orientation = FIELD_GET(SC8180X_ORIENTATION_MASK, notification);
	mux = FIELD_GET(SC8180X_MUX_MASK, notification);
	mode = FIELD_GET(SC8180X_MODE_MASK, notification);
	hpd_state = FIELD_GET(SC8180X_HPD_STATE_MASK, notification);
	hpd_irq = FIELD_GET(SC8180X_HPD_IRQ_MASK, notification);

	svid = mux == 2 ? USB_TYPEC_DP_SID : 0;

	if (port >= ARRAY_SIZE(altmode->ports) || !altmode->ports[port].altmode) {
		dev_dbg(altmode->dev, "notification on undefined port %d\n", port);
		return;
	}

	alt_port = &altmode->ports[port];
	event.orientation = pmic_glink_altmode_orientation(orientation);
	event.svid = svid;
	event.mode = mode;
	event.hpd_state = hpd_state;
	event.hpd_irq = hpd_irq;
	/* Do not infer SC8280 mux states from this legacy notification format. */
	pmic_glink_altmode_queue_event(alt_port, &event);
}

#define SC8280XP_DPAM_MASK	0x3f
#define SC8280XP_HPD_STATE_MASK BIT(6)
#define SC8280XP_HPD_IRQ_MASK	BIT(7)

static void pmic_glink_altmode_sc8280xp_notify(struct pmic_glink_altmode *altmode,
					       u16 svid, const void *data, size_t len)
{
	struct pmic_glink_altmode_port *alt_port;
	struct pmic_glink_altmode_event event = {};
	const struct usbc_sc8280x_tbt_data *tbt;
	const struct usbc_sc8280x_dp_data *dp;
	const struct usbc_notify *notify;
	u8 orientation;
	u8 port;

	if (len != sizeof(*notify)) {
		dev_warn(altmode->dev, "invalid length USBC_NOTIFY_IND: %zd\n",
			 len);
		return;
	}

	notify = data;

	port = notify->port_idx;
	orientation = notify->orientation;

	if (port >= ARRAY_SIZE(altmode->ports) || !altmode->ports[port].altmode) {
		dev_dbg(altmode->dev, "notification on undefined port %d\n", port);
		return;
	}

	alt_port = &altmode->ports[port];
	event.orientation = pmic_glink_altmode_orientation(orientation);
	event.svid = svid;
	event.mux_ctrl = notify->mux_ctrl;
	event.has_mux_ctrl = true;

	if (svid == USB_TYPEC_DP_SID) {
		dp = &notify->extended_data.dp;

		event.mode = dp->pin_assignment - DPAM_HPD_A;
		event.hpd_state = dp->hpd_state;
		event.hpd_irq = dp->hpd_irq;
	} else if (event.mux_ctrl == MUX_CTRL_STATE_TUNNELING) {
		/* Valid for both USB4 and TBT3 */
		tbt = &notify->extended_data.tbt;

		event.tbt_data = *tbt;
	}

	pmic_glink_altmode_queue_event(alt_port, &event);
}

static void pmic_glink_altmode_callback(const void *data, size_t len, void *priv)
{
	struct pmic_glink_altmode *altmode = priv;
	const struct pmic_glink_hdr *hdr = data;
	unsigned long flags;
	u16 opcode;
	u16 svid;

	opcode = le32_to_cpu(hdr->opcode) & 0xff;
	svid = le32_to_cpu(hdr->opcode) >> 16;

	switch (opcode) {
	case USBC_CMD_WRITE_REQ:
		spin_lock_irqsave(&altmode->event_lock, flags);
		complete(&altmode->pan_ack);
		spin_unlock_irqrestore(&altmode->event_lock, flags);
		break;
	case USBC_NOTIFY_IND:
		pmic_glink_altmode_sc8280xp_notify(altmode, svid, data, len);
		break;
	case USBC_SC8180X_NOTIFY_IND:
		pmic_glink_altmode_sc8180xp_notify(altmode, data, len);
		break;
	}
}

static void pmic_glink_altmode_put_retimer(void *data)
{
	typec_retimer_put(data);
}

static void pmic_glink_altmode_put_mux(void *data)
{
	typec_mux_put(data);
}

static void pmic_glink_altmode_put_switch(void *data)
{
	typec_switch_put(data);
}

static void pmic_glink_altmode_enable_worker(struct work_struct *work)
{
	struct pmic_glink_altmode *altmode = work_to_altmode(work);
	struct pmic_glink_altmode_port *port;
	unsigned long flags;
	unsigned int i;
	u64 generation;
	bool up;
	int ret;

	scoped_guard(mutex, &altmode->lifecycle_lock) {
		spin_lock_irqsave(&altmode->event_lock, flags);
		/* A coalesced run may already have reconciled the latest generation. */
		if (altmode->stopping || altmode->reconciled) {
			spin_unlock_irqrestore(&altmode->event_lock, flags);
			return;
		}
		generation = altmode->generation;
		up = altmode->service_up;
		spin_unlock_irqrestore(&altmode->event_lock, flags);

		/* An old in-flight publication finishes before this reconciliation. */
		for (i = 0; i < ARRAY_SIZE(altmode->ports); i++) {
			port = &altmode->ports[i];
			if (!port->altmode)
				continue;
			if (port->hpd_connected)
				drm_aux_hpd_bridge_notify(&port->bridge->dev,
							  connector_status_disconnected);
			port->hpd_connected = false;
		}

		spin_lock_irqsave(&altmode->event_lock, flags);
		if (!altmode->stopping && up && altmode->service_up &&
		    generation == altmode->generation) {
			/* DOWN stays closed; a matching UP cleanup opens the barrier. */
			altmode->reconciled = true;
			for (i = 0; i < ARRAY_SIZE(altmode->ports); i++) {
				port = &altmode->ports[i];
				if (port->altmode && !kfifo_is_empty(&port->events))
					queue_work(system_freezable_wq, &port->work);
			}
		}
		spin_unlock_irqrestore(&altmode->event_lock, flags);
	}

	if (!up)
		return;

	/* Reconciled is software state, not a dependency on a PAN_EN reply. */
	ret = pmic_glink_altmode_request(altmode, ALTMODE_PAN_EN, 0, generation);
	if (ret)
		dev_err(altmode->dev, "failed to request altmode notifications, generation %llu: %d\n",
			(unsigned long long)generation, ret);
}

static void pmic_glink_altmode_pdr_notify(void *priv, int state)
{
	struct pmic_glink_altmode *altmode = priv;
	u64 generation = pmic_glink_get_generation(altmode->client);
	bool up = state == SERVREG_SERVICE_STATE_UP;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&altmode->event_lock, flags);
	if (!altmode->stopping &&
	    (generation != altmode->generation || up != altmode->service_up)) {
		altmode->generation = generation;
		altmode->service_up = up;
		altmode->reconciled = false;
		for (i = 0; i < ARRAY_SIZE(altmode->ports); i++) {
			if (altmode->ports[i].altmode)
				kfifo_reset(&altmode->ports[i].events);
		}
		/* Wake a waiter to reject this generation change, not to ACK it. */
		complete(&altmode->pan_ack);
		queue_work(system_freezable_wq, &altmode->enable_work);
	}
	spin_unlock_irqrestore(&altmode->event_lock, flags);
}

static void pmic_glink_altmode_stop(void *data)
{
	struct pmic_glink_altmode *altmode = data;
	struct pmic_glink_altmode_port *port;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&altmode->event_lock, flags);
	altmode->stopping = true;
	/* The post-send/wait state checks turn this local wake into cancellation. */
	complete(&altmode->pan_ack);
	spin_unlock_irqrestore(&altmode->event_lock, flags);

	/* Keep the registered client alive so an in-flight ACK can complete. */
	cancel_work_sync(&altmode->enable_work);
	for (i = 0; i < ARRAY_SIZE(altmode->ports); i++) {
		port = &altmode->ports[i];
		if (!port->altmode)
			continue;
		cancel_work_sync(&port->work);
		spin_lock_irqsave(&altmode->event_lock, flags);
		kfifo_reset(&port->events);
		spin_unlock_irqrestore(&altmode->event_lock, flags);
	}
}

static const struct of_device_id pmic_glink_altmode_of_quirks[] = {
	{ .compatible = "qcom,sc8180x-pmic-glink", .data = (void *)PMIC_GLINK_OWNER_USBC },
	{}
};

static int pmic_glink_altmode_probe(struct auxiliary_device *adev,
				    const struct auxiliary_device_id *id)
{
	struct pmic_glink_altmode_port *alt_port;
	struct pmic_glink_altmode *altmode;
	const struct of_device_id *match;
	struct fwnode_handle *fwnode;
	struct device *dev = &adev->dev;
	u32 port;
	int ret;

	altmode = devm_kzalloc(dev, sizeof(*altmode), GFP_KERNEL);
	if (!altmode)
		return -ENOMEM;

	altmode->dev = dev;

	match = of_match_device(pmic_glink_altmode_of_quirks, dev->parent);
	if (match)
		altmode->owner_id = (unsigned long)match->data;
	else
		altmode->owner_id = PMIC_GLINK_OWNER_USBC_PAN;

	INIT_WORK(&altmode->enable_work, pmic_glink_altmode_enable_worker);
	init_completion(&altmode->pan_ack);
	mutex_init(&altmode->lock);
	mutex_init(&altmode->lifecycle_lock);
	spin_lock_init(&altmode->event_lock);

	device_for_each_child_node(dev, fwnode) {
		ret = fwnode_property_read_u32(fwnode, "reg", &port);
		if (ret < 0) {
			dev_err(dev, "missing reg property of %pOFn\n", fwnode);
			fwnode_handle_put(fwnode);
			return ret;
		}

		if (port >= ARRAY_SIZE(altmode->ports)) {
			dev_warn(dev, "invalid connector number, ignoring\n");
			continue;
		}

		if (altmode->ports[port].altmode) {
			dev_err(dev, "multiple connector definition for port %u\n", port);
			fwnode_handle_put(fwnode);
			return -EINVAL;
		}

		alt_port = &altmode->ports[port];
		alt_port->altmode = altmode;
		alt_port->index = port;
		INIT_WORK(&alt_port->work, pmic_glink_altmode_worker);
		INIT_KFIFO(alt_port->events);

		alt_port->bridge = devm_drm_dp_hpd_bridge_alloc(dev, to_of_node(fwnode));
		if (IS_ERR(alt_port->bridge)) {
			fwnode_handle_put(fwnode);
			return PTR_ERR(alt_port->bridge);
		}

		alt_port->dp_alt.svid = USB_TYPEC_DP_SID;
		alt_port->dp_alt.mode = USB_TYPEC_DP_MODE;
		alt_port->dp_alt.active = 1;

		alt_port->tbt_alt.svid = USB_TYPEC_TBT_SID;
		alt_port->tbt_alt.mode = TYPEC_TBT_MODE;
		alt_port->tbt_alt.active = 1;

		alt_port->typec_mux = fwnode_typec_mux_get(fwnode);
		if (IS_ERR(alt_port->typec_mux)) {
			fwnode_handle_put(fwnode);
			return dev_err_probe(dev, PTR_ERR(alt_port->typec_mux),
					     "failed to acquire mode-switch for port: %d\n",
					     port);
		}

		ret = devm_add_action_or_reset(dev, pmic_glink_altmode_put_mux,
					       alt_port->typec_mux);
		if (ret) {
			fwnode_handle_put(fwnode);
			return ret;
		}

		alt_port->typec_retimer = fwnode_typec_retimer_get(fwnode);
		if (IS_ERR(alt_port->typec_retimer)) {
			fwnode_handle_put(fwnode);
			return dev_err_probe(dev, PTR_ERR(alt_port->typec_retimer),
					     "failed to acquire retimer-switch for port: %d\n",
					     port);
		}

		ret = devm_add_action_or_reset(dev, pmic_glink_altmode_put_retimer,
					       alt_port->typec_retimer);
		if (ret) {
			fwnode_handle_put(fwnode);
			return ret;
		}

		alt_port->typec_switch = fwnode_typec_switch_get(fwnode);
		if (IS_ERR(alt_port->typec_switch)) {
			fwnode_handle_put(fwnode);
			return dev_err_probe(dev, PTR_ERR(alt_port->typec_switch),
					     "failed to acquire orientation-switch for port: %d\n",
					     port);
		}

		ret = devm_add_action_or_reset(dev, pmic_glink_altmode_put_switch,
					       alt_port->typec_switch);
		if (ret) {
			fwnode_handle_put(fwnode);
			return ret;
		}
	}

	for (port = 0; port < ARRAY_SIZE(altmode->ports); port++) {
		alt_port = &altmode->ports[port];
		if (!alt_port->bridge)
			continue;

		ret = devm_drm_dp_hpd_bridge_add(dev, alt_port->bridge);
		if (ret)
			return ret;
	}

	altmode->client = devm_pmic_glink_client_alloc(dev,
						       altmode->owner_id,
						       pmic_glink_altmode_callback,
						       pmic_glink_altmode_pdr_notify,
						       altmode);
	if (IS_ERR(altmode->client))
		return PTR_ERR(altmode->client);

	/* Runs before client release and before Type-C/DRM providers are released. */
	ret = devm_add_action_or_reset(dev, pmic_glink_altmode_stop, altmode);
	if (ret)
		return ret;

	pmic_glink_client_register(altmode->client);

	return 0;
}

static const struct auxiliary_device_id pmic_glink_altmode_id_table[] = {
	{ .name = "pmic_glink.altmode", },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, pmic_glink_altmode_id_table);

static struct auxiliary_driver pmic_glink_altmode_driver = {
	.name = "pmic_glink_altmode",
	.probe = pmic_glink_altmode_probe,
	.id_table = pmic_glink_altmode_id_table,
};

module_auxiliary_driver(pmic_glink_altmode_driver);

MODULE_DESCRIPTION("Qualcomm PMIC GLINK Altmode driver");
MODULE_LICENSE("GPL");
