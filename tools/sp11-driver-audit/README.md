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
node --test tools/sp11-driver-audit/*.test.mjs
```

The inventory includes SHA-256 identities, file sizes, PE architecture, INF
versions, manufacturer-declared model bindings, service declarations and source
file declarations. Model identities are package declarations; subsequent IDs
in a model row are compatible IDs, not additional proof of installed hardware.
Service declarations include sections that might never be selected. Broken
string declarations remain unresolved and produce warnings. Child symbolic
links are skipped. INF path strings are never opened as commands or files.

The AeoB reader checks lengths, top-level counts, nested boundaries, integer
precision and resource limits. It recognizes the observed integer, string and
nested tuple forms. Unknown tags retain a digest and byte range without a
guessed interpretation. Unsupported layouts fail explicitly. Generated JSON
is research data and should remain outside the kernel source tree.

The camera interpreter uses the [recorded field specification](camera-fields.md).
It preserves both platform configuration banks, raw values and unknown tail
fields. It does not select a board, turn orientation codes into angles, or
translate Windows resource indices into Linux GPIO or CCI numbers.

## Initial measured coverage

The April 2026 package named
`SurfacePro11_ARM_Win11_26100_26.041.12746.0` contains 1,126 files and 205 INF
files. The inventory retains bindings from every INF, with two malformed vendor
string declarations reported as warnings. Static PE headers identify 190 ARM64
and 17 I386 objects; the package also contains DSP and firmware objects in other
formats.

Of 72 files beginning with `AeoB`, 71 follow the observed layout, containing
46,033 records with maximum nesting depth six. `qcadc/ADC1.bin` uses an
unsupported layout and is rejected at offset 253. This is a coverage limit,
not a claim that the vendor file is corrupt or that all AeoB variants match.

## Implementation tracks

| Track | Existing Linux position | Next implementation evidence |
| --- | --- | --- |
| Front IMX681 | Native driver and OLED graph; prior preview and indicator tests | Compare verified power, lane and stream/error lifecycle with this package |
| Rear OV13858 | Native sensor driver assumes an ACPI-powered device; no Denali graph | Native power resources, module-specific PHY/modes, actuator and indicator ownership |
| IR VD55G0/VD55G1 | Different sensors and board variants; support differs | Exact module identity, native driver, transport and illumination limits |
| Touch/pen | Native transport and userspace exist; fresh-boot autoload repair is a separate series | Preserve repair and investigate actual lifecycle gaps |
| Suspend | Several device and SoC resource owners are involved | Decode power policies and compare failed Linux transitions with measured Windows behaviour |
| Sensors | Surface SAM support is distinct from the Qualcomm sensor hub | Transport, exact board configuration, orientation and calibration |
| USB4 | Router/retimer groundwork exists | Resource ownership, sideband and working firmware/ring lifecycle |
| Audio | Working protected kernel and userspace path; an ALSA update replaced the selector | Preserve sound while completing persistent userspace selection and scoped compatibility |

Hardware identity comes from the actual board, firmware tables and device
identification. The bundle includes optional docks, modems, SSDs, panels and
camera assemblies; membership in the package does not justify enabling them.

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
