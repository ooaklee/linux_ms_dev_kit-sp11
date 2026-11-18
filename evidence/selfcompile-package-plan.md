# SP11 Self-Compile Driver Package Plan (design, not implemented)

**Goal (completion criterion):** SP11 users can self-compile the repo's research
drivers from a standalone source package, without rebuilding the whole kernel.

**Kernel target:** `linux-headers-7.3.0-jg-1sp11v4` deb (built from this repo,
branch `sp11/ubuntu-qcom-x1e-7.3.y`, head `c0b027b8b8b4` at time of writing).

**Confidence:** HIGH for the qualification table (verified against tree state
and git history on 2026-11-14); MEDIUM for the CI sketch (untested design).

---

## 1. Qualification: what can be built standalone

| Driver | Source | Standalone? | Reason |
|---|---|---|---|
| VD55G0 (via VD55G1) | `drivers/media/i2c/vd55g1.c` | **YES — qualified** | Self-contained I2C/V4L2 sensor driver; no tree-internal helpers *except* `V4L2_SUBDEV_FL_STOP_RETRY` (see §2). |
| OV13858 | `drivers/media/i2c/ov13858.c` + new OF binding | **YES — qualified** | Same profile as VD55G1: plain V4L2 subdev, depends only on the patched v4l2-subdev core flag. |
| ps883x retimer mux | `drivers/usb/typec/mux/ps883x.c` | **YES — qualified (secondary)** | Near-upstream driver; repo carries small fixes (`aa217657aa8f`, `e998c88ad163`) but no tree-internal API deps. |
| Qualcomm USB4 host-router frontend | `drivers/thunderbolt/qcom_usb4_hr.c/.h` | **NO — stays in-tree** | Linked into the `thunderbolt-` composite (`drivers/thunderbolt/Makefile:8`) and called from patched `drivers/thunderbolt/pci.c` (register/unregister at pci.c:650/665). Cannot be a separate module without forking `pci.c`. |
| USB4 PHY / QMP combo | `drivers/phy/qualcomm/phy-qcom-qmp-combo.c` + `phy-qcom-qmp-usb43-*-v8.h` | **NO — stays in-tree** | Modifies shared QMP common state, clk-common reset propagation (`6002920d5e2e`), and pipe-clock ownership; deep coupling with tree-wide PHY/clk infrastructure. |
| Type-C recovery / UCSI / NHI lifetime work | `drivers/thunderbolt/nhi.c`, `usb/typec` core changes | **NO — stays in-tree** | Framework/core behavioral changes, not add-on drivers. |

**The decisive technical fact for the camera drivers:** commits `2e99a0336214`
(v4l2-subdev core) and `c0b027b8b8b4` (both sensors opt into
`V4L2_SUBDEV_FL_STOP_RETRY`, defined in *this tree's*
`include/media/v4l2-subdev.h:984`) mean the modules are ABI-coupled to this
repo's V4L2 core. They build and load fine against
`linux-headers-7.3.0-jg-1sp11v4` (same tree), but **will fail or misbehave
against vanilla upstream 7.3 headers** (`FL_STOP_RETRY` undefined → compile
error; worst case a silent flag mismatch if numbers were hardcoded). The
package must therefore pin to this headers deb and state that loudly.

## 2. Kernel-version pinning strategy

**Decision: plain `make` against installed headers, hard-pinned. DKMS rejected
for v1.**

- Headers pin: the package `Makefile` defaults to
  `KDIR ?= /lib/modules/$(shell uname -r)/build` and the README + a `vermagic`
  post-build check require `uname -r` to be `7.3.0-jg-1sp11v4` (exact). A
  `check-kernel.sh` greps `utsrelease.h` and refuses to build otherwise.
- Why not DKMS: (a) the modules only work with this repo's patched v4l2 core —
  DKMS auto-rebuild on any other kernel silently produces broken/incompatible
  modules; (b) single-device, single-kernel audience for v1. DKMS can be added
  later by shipping a `dkms.conf` guarded on the exact kernel version.
- No compat shims in v1: we do not `#ifdef` away `FL_STOP_RETRY`; if a user
  wants vanilla-kernel builds, that is explicitly out of scope (the retained-
  stop safety behavior is part of the research result).

## 3. Directory layout

```
selfcompile-drivers/            (release tarball: sp11-selfcompile-drivers-<ver>.tar.xz)
├── Makefile                    # top: dispatches to enabled/ dirs, KDIR override
├── check-kernel.sh             # verifies 7.3.0-jg-1sp11v4 headers
├── README.md                   # prerequisites, build, install, vermagic check, warnings
├── VERSION                     # e.g. 7.3.0-jg-1sp11v4.1 (kernel + pkg rev)
├── vd55g0/
│   ├── Makefile                # obj-m += vd55g1.o
│   ├── Kbuild
│   ├── vd55g1.c                # byte-copied from drivers/media/i2c/
│   └── README.md               # IR/global-shutter front sensor notes, DT binding
├── ov13858/
│   ├── Makefile / Kbuild       # obj-m += ov13858.o
│   ├── ov13858.c
│   └── README.md
└── ps883x/
    ├── Makefile / Kbuild       # obj-m += ps883x.o
    ├── ps883x.c
    └── README.md
```

Rules: files are **copied verbatim** from the tree at release time (no forks of
the source); a `scripts/export-package.sh` (in-tree) regenerates the tarball
from git and stamps source SHAs into each per-driver README, so the package
never drifts silently from the research drivers.

## 4. Build & test instructions (deliverable text for README)

```
sudo apt install linux-headers-7.3.0-jg-1sp11v4
tar xf sp11-selfcompile-drivers-*.tar.xz && cd sp11-selfcompile-drivers-*
./check-kernel.sh              # must print OK
make                           # builds vd55g1.ko, ov13858.ko, ps883x.ko
sudo make modules_install      # to /lib/modules/$(uname -r)/updates/
sudo depmod && sudo modprobe vd55g1   # (camera typically probes via DT, not modprobe)
modinfo vd55g1 | grep -E 'vermagic|filename'
```

Acceptance check: `modinfo` vermagic must equal `7.3.0-jg-1sp11v4 SMP preempt mod_unload aarch64`; loading with mismatched core (e.g. stock Ubuntu kernel) must be impossible.

## 5. CI sketch (design)

GitHub Actions job `selfcompile-matrix` on pushes touching any packaged source
or the export script:

1. **Export:** run `scripts/export-package.sh`; assert tarball builds and
   stamped SHAs match HEAD's file blobs (prevents drift).
2. **Build (AArch64, container):** `ubuntu:24.04-arm64` runner (or QEMU fallback);
   install the `linux-headers-7.3.0-jg-1sp11v4` deb from the repo's package
   release artifacts; run `./check-kernel.sh && make W=1`; fail on any warning
   (matches repo's W=1 hermetic build policy).
3. **Negative test:** attempt build against stock `linux-headers-generic` of the
   same 7.3 version; **expect failure** — proves the pinning guard works.
4. **Vermagic gate:** `modinfo` vermagic string equality check (§4).
5. **checkpatch:** packaged `.c` files must be byte-identical to tree sources,
   so tree CI's strict checkpatch already covers them; CI asserts identity
   (`cmp`) rather than re-linting.

No on-hardware smoke test in CI v1 (no SP11 runner); the README documents
on-device probe validation (`dmesg`, `v4l2-ctl --list-devices`) as a manual
release step, recorded in the repo's evidence log.

## 6. Explicitly out of scope / risks

- USB4 frontend, QMP-combo PHY, UCSI/Type-C recovery stay in-tree (§1); users
  wanting those must use the repo's full kernel deb.
- Windows driver packages (`SurfaceUpdate/`) are data-only REA evidence; nothing
  from them ships in the package.
- GPLv2 only: all packaged sources are GPL-2.0; tarball ships `COPYING` excerpt
  and per-file SPDX headers unchanged — legally distributable standalone.
- Risk: upstream rebases of `vd55g1.c` (it is derived from an ST driver headed
  upstream) may drop the repo's VD55G0 extensions; the export script's SHA
  stamping makes any such divergence visible.
