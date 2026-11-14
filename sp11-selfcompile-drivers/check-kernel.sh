#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# check-kernel.sh — refuse to build against a kernel that lacks the
# patched v4l2-subdev core (V4L2_SUBDEV_FL_STOP_RETRY is defined only in
# this repo's kernels, e.g. 7.3.0-jg-1sp11v4).
set -euo pipefail

KVER=${KVER:-$(uname -r)}
HDR="/lib/modules/$KVER/build"

fail() { echo "FAIL: $*" >&2; exit 1; }

[ -d "$HDR" ] || fail "no kernel headers for $KVER at $HDR (install linux-headers-$KVER)"

for f in "$HDR/include/media/v4l2-subdev.h" "$HDR/include/linux/usb/typec_mux.h"; do
	[ -f "$f" ] || fail "missing header: $f"
done

grep -q "V4L2_SUBDEV_FL_STOP_RETRY" "$HDR/include/media/v4l2-subdev.h" \
	|| fail "kernel $KVER lacks V4L2_SUBDEV_FL_STOP_RETRY; these drivers need a kernel built from ooaklee/linux_ms_dev_kit-sp11 (e.g. 7.3.0-jg-1sp11v4), not stock headers"

echo "OK: $KVER headers carry the SP11 research-driver API"
