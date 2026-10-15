# Native camera source foundations

These source-only foundations are development material, not a qualified
kernel release or a ready-to-install camera fix. They do not enable rear or
IR camera nodes on Denali, change a boot default, or replace the working
front-camera, audio or touch configuration.

The production code uses native Linux I2C/CCI, V4L2, firmware-node, regulator,
clock and GPIO interfaces. Building it does not require Windows, the MSI,
the research readers or any Windows driver/configuration file. The VD55G0
driver includes sensor firmware patches published in ST's GPL-2.0 Linux
reference driver; Windows independence does not mean firmware independence.
This does not resolve firmware sourcing for every other SP11 subsystem.

## Contents and provenance

- `patches/ov13858/`: the OV13858 binding and native OF power-management
  commits, with existing sensor register tables retained.
- `patches/vd55g0/`: the native VD55G1-family prerequisite/VD55G0 series and
  separate local corrections. Original contributors' authorship and sign-offs
  are retained; the port does not import the newer media-core pad API or
  unrelated Intel IPU/x86 platform changes.
- `source/`: exact driver and binding snapshots for review. They belong in a
  full kernel tree, not a stand-alone Windows compatibility module or DKMS
  shim.
- `MANIFEST.json` and `SHA256SUMS`: exact carrier, source revisions, source
  hashes and patch inventory. The manifest distinguishes source checks from
  physical qualification. Licence texts accompany the source.

The carrier is the tested v8 source revision
`8ba2b75713feb5c72bbc11dd8cce4048e23cdeea` from
[the kernel fork](https://github.com/ooaklee/linux_ms_dev_kit-sp11).
That does not make these new changes hardware-tested. Later carrier updates,
including the separate touch-autoload repair, require their own integration
and regression checks. Fork research tools and CI are not part of the native
driver patch sets.

Revision 3 retains revision 2's 24 patch emails unchanged and appends three
IR-driver corrections: reject active format changes while streaming, establish
the shared state/control lock before publication and use its caller-locked
control helpers, and restore VD55G0's native exposure-control write sequence.
The bundle contains 2 OV13858 and 25 IR patches. Earlier archives remain
separately identified, immutable historical revisions.

VD55G0 source is based on
[Peter Marshall's native series](https://github.com/petm5/linux/commit/4a04accd5f688ac4fdca1ad4d3959ef17825085c),
the existing ST driver, and
[Jizhou Tong's register-width correction](https://patchwork.linuxtv.org/project/linux-media/patch/010601a1332f51fe-f5c0bb2d-cf7f-4e99-9feb-1e8f65057945-000000@ap-northeast-1.amazonses.com/).
The two embedded VD55G0 firmware payloads were compared byte-for-byte with
[ST's pinned Linux reference](https://github.com/STMicroelectronics/vd55g0-linux-driver/blob/a05627b0f6d8775aa54b6fa306e91f425f2cbf9e/vd55g0_patches.h).

## Apply and compile for source review

Use a separate full kernel checkout at the manifest's carrier revision.
Verify `SHA256SUMS` inside the extracted bundle before applying either
independent patch set. From the kernel checkout:

```sh
git switch -c sp11-native-camera-research
git am /path/to/source-bundle/patches/ov13858/*.patch
git am /path/to/source-bundle/patches/vd55g0/*.patch
```

For an ARM64 Linux source/object check, with the normal kernel build
dependencies installed:

```sh
mkdir -p ../sp11-native-camera-build
python3 debian/scripts/misc/annotations \
  --file debian.qcom-x1e/config/annotations \
  --arch arm64 --flavour qcom-x1e --export \
  > ../sp11-native-camera-build/.config
scripts/config --file ../sp11-native-camera-build/.config \
  --module VIDEO_OV13858 --module VIDEO_VD55G1
make O=../sp11-native-camera-build ARCH=arm64 olddefconfig
make O=../sp11-native-camera-build ARCH=arm64 W=1 -j4 \
  drivers/media/i2c/ov13858.o drivers/media/i2c/vd55g1.o
```

`olddefconfig` adapts to the available compiler/tools; review its configuration
delta rather than calling it an identical release configuration. Cross builds
also require an appropriate `CROSS_COMPILE`. These example object commands do
not produce an installable package or validate CSI capture. Do not use the
resulting objects with `insmod`.

## Source validation, not hardware qualification

A complete ARM64 `W=1` build of `vmlinux` and all 7,817 ordered modules passed
for the **previous 23-patch revision**, whose IR source ends at
`14bb8959fc37812a23ed55af73a2eb273acdb043`. Its configuration was unchanged
by `olddefconfig`; it was a validation configuration, not an identical release
toolchain/configuration. Warnings elsewhere in the carrier remain documented.
That full build does not include revision 2's HDR correction or revision 3's
three corrections.

The **current 27-patch revision** ends at IR commit
`ff99e470cc8ca665580cd26bd072145d853d1ab5`. Its exact source subsequently
completed a clean full ARM64 `W=1` build of `vmlinux` and all 7,817 ordered
modules, independently checked against the unchanged validation configuration
and source pins. No modified-driver warnings were found. Existing carrier
warning counts/categories match the earlier log; differing signatures were
individually reconciled as quotation-mark presentation only. A post-build
checker correction follows the kernel's actual `.o`-to-`.ko` ordering contract;
the original failed checker report and immutable archive remain preserved.

The exact driver also passed a focused ARM64 `W=1` object/module link against
the prepared kernel and 1,059 synthetic register-trace/error cases on each of
host sanitizers and native ARM64 Linux.
The G0 correction removes only the added maximum-register operation; all
tested G1/G4/VD65G4 traces, including HDR subtraction's 446-line value, remain
unchanged. Separate checks at the preceding format and lifecycle commits
cover active-state mutation, allocation failures and shared-lock ownership.
These are source-level tests, not sensor or full V4L2/PM/CCI-core tests.

All 27 patch emails applied with `git am` to the complete original v8
commit/tree on a Linux filesystem, preserving author identities, exact dates,
commit messages and ordered trailers. The resulting four snapshots matched;
only those paths and the declared `MAINTAINERS` addition differ from v8.

See [the validation summary](native-camera-validation.md) and the bundle's
manifest for exact source, configuration and artifact hashes. No revision
is an installable kernel release or evidence of camera, suspend or
optical-safety qualification.

## Deliberate qualification gates

OV13858 still needs verified reset-net and sensor pin-to-rail ownership,
module/actuator calibration handling, sensor/receiver lane mapping and
timing, followed by capture/lifecycle tests. See
[the OV13858 qualification notes](ov13858-native.md).

VD55G0 illumination is deliberately unsupported in this foundation:
`st,leds` is rejected for that sensor before resources are powered. The
driver does not guess GPIO1 wiring or manufacture a flash control. Sensor
GPIOs are configured as inputs after firmware reaches standby and before
streaming. This is a software policy, not proof of undocumented reset
defaults, absence of emitted IR, or electrical/eye safety.

IR board identity, actual I2C routing/address, supplies/reset, CSI graph,
capture timing, calibration and sensor-only runtime behaviour remain to be
qualified. Illuminator wiring, first-frame and live-control exposure limits,
current control and independent safety requirements need a separate review
before illumination support can be added. G0's `0x096c` is described as a
manual maximum, not a proven automatic-exposure ceiling. The current source
does not directly access it, matching ST's native driver for the same firmware
versions. This does not establish the full physical exposure range. Live
VBLANK changes update context frame length and software exposure ranges;
firmware-specific maximum/latching rules and the G1-family live static-register
policy still need qualification. No guessed group-hold or startup-only maximum
write has been added. Full capture, runtime failure recovery, suspend/resume
and physical VD55G1/G4 regression compatibility remain unproven. No device
installation instructions are supplied for this unqualified foundation.
