# SP11 Self-Compile Driver Package

Standalone sources for the Surface Pro 11 (ARM64) research drivers from
[ooaklee/linux_ms_dev_kit-sp11](https://github.com/ooaklee/linux_ms_dev_kit-sp11),
so users can build and install them without rebuilding the whole kernel.

## License

GPL-2.0 — see the accompanying `COPYING` file (copied from the kernel
tree by the export script). Individual sources carry SPDX identifiers
and their original copyright notices.

## Requirements

- Kernel **built from this repo** — e.g. `7.3.0-jg-1sp11v4` — with its
  matching `linux-headers-*` deb installed. Stock Ubuntu/vanilla headers
  **will not work**: the camera drivers opt into `V4L2_SUBDEV_FL_STOP_RETRY`,
  which exists only in this repo's patched `include/media/v4l2-subdev.h`.
- `make`, `gcc` (aarch64), kernel headers prepared for external modules.

## Build

```sh
sudo apt install linux-headers-7.3.0-jg-1sp11v4   # adjust to your -jg kernel
tar xf sp11-selfcompile-drivers-<version>.tar.xz && cd sp11-selfcompile-drivers-<version>
./check-kernel.sh     # must print OK — refuses stock headers
make                  # builds vd55g1.ko, ov13858.ko, ps883x.ko
sudo make modules_install
sudo depmod
```

## Drivers

| Module | Driver | Hardware | Notes |
|---|---|---|---|
| `vd55g1.ko` | VD55G1/VD55G0 front IR sensor | front camera (IR/global-shutter) | probes via DT overlay |
| `ov13858.ko` | OV13858 rear sensor | rear camera | needs OF binding |
| `ps883x.ko` | PS883x Type-C retimer mux | Type-C/DP orientation | near-upstream + SP11 fixes |

Sources are copied **verbatim** from the tree by `scripts/export-package.sh`,
which stamps each per-driver `.source-sha` with the git blob SHA — if your
package SHA matches the repo, you are building exactly the reviewed source.

## Not included (must stay in-tree)

- Qualcomm USB4 host-router frontend (`qcom_usb4_hr`) — hooks
  `drivers/thunderbolt/pci.c`, composite-linked.
- QMP-combo USB4 PHY — shared QMP/clk state, cannot build standalone.
- Type-C/UCSI/NHI recovery core changes.

## Layout

```
sp11-selfcompile-drivers/
├── check-kernel.sh   # guards against stock headers
├── Makefile
├── vd55g0/  ov13858/  ps883x/  imx681/  # per-driver: Kbuild + source + .source-sha
└── README.md
```
