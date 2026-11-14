# SPDX-License-Identifier: GPL-2.0
# Top-level makefile for the SP11 self-compile driver package.
#
# Requires kernel headers from a kernel built out of
# ooaklee/linux_ms_dev_kit-sp11 (e.g. linux-headers-7.3.0-jg-1sp11v4):
# the camera drivers use V4L2_SUBDEV_FL_STOP_RETRY, which exists only
# in this repo's patched v4l2-subdev core. Run ./check-kernel.sh first.

KVER ?= $(shell uname -r)
HDR   ?= /lib/modules/$(KVER)/build
DIRS   := vd55g0 ov13858 ps883x

.PHONY: all modules_install clean check $(DIRS)

all: check
	@set -e; for d in $(DIRS); do \
		$(MAKE) -C $(HDR) M=$$PWD/$$d; \
	done

modules_install: check
	@set -e; for d in $(DIRS); do \
		$(MAKE) -C $(HDR) M=$$PWD/$$d modules_install; \
	done

check:
	@KVER=$(KVER) HDR='$(HDR)' ./check-kernel.sh

clean:
	@for d in $(DIRS); do \
		$(MAKE) -C $(HDR) M=$$PWD/$$d clean || exit 1; \
	done
