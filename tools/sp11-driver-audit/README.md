# Surface Pro 11 driver research

These independently written Linux development tools inventory an extracted
Surface driver package and decode selected camera resource metadata. They are
the first source deliverable in the SP11 parity work. They do not install
drivers, execute Windows programs, access hardware, generate device trees, or
establish that a device is present.

The intended production result is native Linux kernel and userspace source
that builds and runs without Windows or the MSI. Windows package inspection is
an optional research step, outside that production dependency chain. Device
firmware is a separate dependency whose Linux availability and device-specific
authentication must be established.

## Run

Node.js 22 or newer is sufficient for these tools; no npm dependencies are
required. Use an extracted package you already have:

```sh
node tools/sp11-driver-audit/inventory.mjs /path/to/SurfaceUpdate > inventory.json
node tools/sp11-driver-audit/aeob.mjs --input /path/to/CAMS_RES_MSHW0491.bin
node tools/sp11-driver-audit/camera-config.mjs \
  --kind prld --input /path/to/CAMP_PRLD_MSHW0495.bin
node tools/sp11-driver-audit/camera-config.mjs \
  --kind pcfg --input /path/to/CAMP_PCFG_MSHW0495.bin
node tools/sp11-driver-audit/camera-config.mjs \
  --kind scfg --input /path/to/SCFG_REAR_MSHW0491.bin
node tools/sp11-driver-audit/camera-power.mjs \
  --input /path/to/CAMS_RES_MSHW0491.bin
node tools/sp11-driver-audit/windows-resources.mjs \
  --format registry-utf16-arm64 --input /path/to/camera-platform.reg
node tools/sp11-driver-audit/chromatix.mjs \
  --input /path/to/com.surface.sensormodule.rfc_ov13858.bin
node --test tools/sp11-driver-audit/*.test.mjs
```

The inventory includes SHA-256 identities, file sizes, PE architecture, INF
versions, manufacturer-declared model bindings, service declarations and source
file declarations. Model identities are package declarations; subsequent IDs
in a model row are compatible IDs, not additional proof of installed hardware.
Service declarations include sections that might never be selected. Broken
string declarations remain unresolved and produce warnings. Child symbolic
links are skipped. INF path strings are never opened as commands or files.

The AeoB reader checks the ACPI V1 output-buffer structure: lengths, top-level
counts, nested boundaries and the minimum four-byte argument data union. This
does not align every payload to four bytes. Documented four-byte integers and
observed eight-byte integers retain full precision. Buffers and unknown tags
retain a digest and byte range without guessed device semantics. Unsupported
layouts fail explicitly. Generated JSON is research data and should remain
outside the kernel source tree.

The camera interpreter uses the [recorded field specification](camera-fields.md).
It preserves both platform configuration banks, raw values and unknown tail
fields. It does not select a board, turn orientation codes into angles, or
translate Windows resource indices into Linux GPIO or CCI numbers.

The power interpreter names the verified TLMM GPIO fields, type-one PMIC
voltage/enable/mode votes and resource-delay units. Unknown mechanisms,
component subtrees and extension fields remain opaque. Wrapper metadata is
stored once, with offset references from resources, so long device labels are
not repeated for every operation. Configured sequence order is not measured
electrical timing, and a GPIO level or voltage is not proof of a sensor pin's
function or ownership.

The Windows resource reader explicitly selects the observed ARM64 packed
layout; it does not autodetect architectures or evaluate firmware. It accepts
bounded UTF-16LE registry exports containing typed `hex(8)` lists, or a raw
list with `--format cm-arm64`. Unknown layouts, variable-length resource
types, MSI interrupts and ambiguous/duplicate registry value names fail
explicitly. Registry output can contain device-instance identifiers: retain
it privately. Nothing is imported into the registry or applied to hardware.

The Chromatix reader validates the observed 3.0.1 container, CRC32, section
extents, named descriptors, register-sequence references and resource budgets.
It does not execute operations or emit Linux driver code. Default output
contains identities, offsets, counts and digests, not register values or raw
payloads. Explicit `--include-registers` and `--include-payloads` options are
for local research; keep their output outside the source tree. Unknown
operation codes must not be interpreted as production policy.

## Initial measured coverage

The April 2026 package named
`SurfacePro11_ARM_Win11_26100_26.041.12746.0` contains 1,126 files and 205 INF
files. The inventory retains bindings from every INF, with two malformed vendor
string declarations reported as warnings. Static PE headers identify 190 ARM64
and 17 I386 objects; the package also contains DSP and firmware objects in other
formats.

All 72 files beginning with `AeoB` follow the checked ACPI argument-storage
layout, containing 47,008 records with maximum nesting depth six. Two short
arguments require the minimum data union; the earlier narrow reader rejected
`qcadc/ADC1.bin` because it missed this documented storage rule. Structural
decoding does not establish the meaning of ADC resources or coverage of every
possible argument encoding.

The new power interpreter accepts all 13 inspected camera-resource files;
uninterpreted component subtrees remain opaque rather than being treated as
covered policies. The rear file exposes three GPIO, eight PMIC-vote and three
delay records. A saved MSHW0495 camera-platform allocation independently
matches the two CCI memory ranges in Linux. Its four byte-identical export
copies constitute one observation, not four hardware trials; allocation data
is not itself a translated WDF resource trace. See the field specification
for the combined evidence and its limits.

The sensor-container reader validates the inspected IMX681, OV13858 and three
OV02C10 containers. The front module has 3,035 named descriptors and 915 register
rows; the rear has 2,117 descriptors and 632 rows. Some TPG and IR records use
unsupported semantics and fail explicitly. Front sequences were compared with
public IMX681 reference tables; rear output sizes agree with output-size
registers. Neither check establishes CSI timing or successful rear capture.

## Implementation tracks

| Track | Existing Linux position | Next implementation evidence |
| --- | --- | --- |
| Front IMX681 | Native driver and OLED graph; prior preview and indicator tests | Compare verified power, lane and stream/error lifecycle with this package |
| Rear OV13858 | Native OF power-management foundation under development; no Denali graph | Verified board resources, module-specific PHY/modes, actuator and indicator ownership |
| IR VD55G0/VD55G1 | Different sensors and board variants; support differs | Exact module identity, native driver, transport and illumination limits |
| Touch/pen | Native transport and userspace exist; fresh-boot autoload repair is a separate series | Preserve repair and investigate actual lifecycle gaps |
| Suspend | Several device and SoC resource owners are involved | Decode power policies and compare failed Linux transitions with measured Windows behaviour |
| Sensors | Surface SAM support is distinct from the Qualcomm sensor hub | Transport, exact board configuration, orientation and calibration |
| USB4 | Router/retimer groundwork exists | Resource ownership, sideband and working firmware/ring lifecycle |
| Audio | Working protected kernel and userspace path; an ALSA update replaced the selector | Preserve sound while completing persistent userspace selection and scoped compatibility |

Hardware identity comes from the actual board, firmware tables and device
identification. The bundle includes optional docks, modems, SSDs, panels and
camera assemblies; membership in the package does not justify enabling them.

The [native OV13858 series](ov13858-native.md) is separate from these research
tools. It uses Linux resource interfaces, leaves existing sensor tables intact
and does not load the inspected configuration files. Its board-enablement and
hardware-qualification gates remain explicit.

The research checkout began at the older integration revision
`2cbd1ec3e2da385e7bd91fd65c63ba5a8fb5b865`. These tools are independent of that
kernel revision and are fork research infrastructure. Hardware changes must
use an explicitly selected carrier: the tested v8 source is
`8ba2b75713feb5c72bbc11dd8cce4048e23cdeea`, with the subsequent touch autoload
repair tracked in fork PR #69. Upstream `jg/ubuntu-qcom-x1e-7.2.y` had advanced
to 7.2.9 at `d84d9b29dffe2199bf295824f82099ee58997ce7` when inspected on
15 October 2026. That newer integration requires separate qualification.

## Source and evidence

Tests use synthetic fixtures and run without the driver package. The source is
GPL-2.0-only. No Windows executable, firmware payload, recovered pseudocode or
private device capture is included. Keep locally generated evidence separate
from the original source changes and preserve the input hashes for every
behavioural finding. Analysis tools are not themselves a working camera or a
proof of full Linux hardware parity.
