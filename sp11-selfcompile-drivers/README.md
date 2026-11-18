# SP11 Self-Compile Driver Package

Standalone out-of-tree sources for the Surface Pro 11 (ARM64) research
drivers from
[ooaklee/linux_ms_dev_kit-sp11](https://github.com/ooaklee/linux_ms_dev_kit-sp11),
so SP11 owners can build and install the camera/retimer drivers without
rebuilding the whole kernel.

Verified against kernel ABI **`7.3.0-jg-1sp11v7-qcom-x1e`** (see the
[v7 release](https://github.com/ooaklee/linux_ms_dev_kit-sp11/releases/tag/v7.3.0-jg-1sp11v7)).

## What works

| Module | Driver | Hardware | Status |
|---|---|---|---|
| `imx681.ko` | IMX681 front sensor | front camera | builds + probes (front camera stack) |
| `ov13858.ko` | OV13858 rear sensor | rear camera | builds; power-ownership quirks documented in `evidence/ov13858-power-ownership-report.md` |
| `vd55g1.ko` | VD55G1/VD55G0 depth/IR sensor | front depth/IR | builds; probes via DT overlay |
| `ps883x.ko` | PS883x Type-C retimer mux | Type-C/DP orientation | builds; near-upstream + SP11 fixes |

## Kernel requirements

These modules **only build against kernels produced by this repo** (the
"lexr" `7.3.0-jg-1sp11*` series). The camera drivers opt into
`V4L2_SUBDEV_FL_STOP_RETRY`, which exists only in this repo's patched
`include/media/v4l2-subdev.h`. Stock Ubuntu/vanilla headers will not
work — `./check-kernel.sh` enforces this gate and refuses the build.

Also note the compiler requirement: the `v7` kernel was built with
**gcc 15**; older compilers (gcc 13/14) reject the headers'
`-fmin-function-alignment=` flags. On Ubuntu 24.04 install `gcc-15`
from a newer toolchain PPA, or use the Docker flow below (which uses
`ubuntu:25.10` where gcc-15 is the natural compiler).

## Quick start (Docker, any host)

With the repo checked out and the `v7` header debs from the release in
`./debs/`:

```sh
docker run --rm --platform linux/arm64 \
  -v "$PWD/sp11-selfcompile-drivers:/pkg" -v "$PWD/debs:/debs:ro" \
  ubuntu:25.10 bash -euxo pipefail -c '
    apt-get update -qq && apt-get install -y -qq make gcc-15 gcc kmod file >/dev/null
    ln -sf /usr/bin/gcc-15 /usr/bin/gcc && ln -sf /usr/bin/gcc-15 /usr/bin/cc
    dpkg -i /debs/linux-headers-7.3.0-jg-1sp11v7-qcom-x1e_*.deb /debs/linux-qcom-x1e-headers-7.3.0-jg-1sp11v7_*.deb
    cd /pkg && make KVER=7.3.0-jg-1sp11v7-qcom-x1e
    for ko in vd55g0/vd55g1.ko ov13858/ov13858.ko ps883x/ps883x.ko imx681/imx681.ko; do modinfo -F vermagic "$ko"; done'
```

All four `.ko` files land in their per-driver subdirectories; the
`modinfo` loop must print `7.3.0-jg-1sp11v7-qcom-x1e ... aarch64` for
each. Copy them to the device afterwards.

## Native Ubuntu (on-device, 24.04+)

Prerequisites:

- The kernel itself (`linux-image-7.3.0-jg-1sp11v7-qcom-x1e`) booted —
  from the [v7 release](https://github.com/ooaklee/linux_ms_dev_kit-sp11/releases/tag/v7.3.0-jg-1sp11v7) assets.
- Its two header debs installed:
  `linux-headers-7.3.0-jg-1sp11v7-qcom-x1e` and
  `linux-qcom-x1e-headers-7.3.0-jg-1sp11v7` (both attached to the
  release; `dpkg -i` them).
- `build-essential`, plus `gcc-15` (see the compiler note above).

Build:

```sh
sudo dpkg -i linux-headers-7.3.0-jg-1sp11v7-qcom-x1e_*.deb linux-qcom-x1e-headers-7.3.0-jg-1sp11v7_*.deb
cd sp11-selfcompile-drivers
./check-kernel.sh          # must print OK — refuses stock headers
make KVER=7.3.0-jg-1sp11v7-qcom-x1e
sudo make modules_install KVER=7.3.0-jg-1sp11v7-qcom-x1e
sudo depmod -a 7.3.0-jg-1sp11v7-qcom-x1e
```

## Install / load order

After `modules_install` + `depmod`, load sensors before consumers;
the retimer is independent:

```sh
sudo modprobe ps883x        # Type-C retimer (safe anytime)
sudo modprobe vd55g1        # depth/IR sensor
sudo modprobe imx681        # front sensor
sudo modprobe ov13858       # rear sensor
```

If you prefer a specific order for camera bring-up: retimer first
(`ps883x`), then depth (`vd55g1`), then sensors (`imx681`, `ov13858`).
Modules are also autoloadable via their OF modalias matching once the
corresponding DT nodes are present.

## License

GPL-2.0 — see the accompanying `COPYING` file (copied from the kernel
tree by the export script). Individual sources carry SPDX identifiers
and their original copyright notices.

Sources are copied **verbatim** from the tree by
`scripts/export-package.sh`, which stamps each per-driver `.source-sha`
with the git blob SHA — if your package SHA matches the repo, the
sources are unmodified.
