# Camera field observations

These field definitions come from static analysis of the shipped ARM64
`qccamplatform8380.sys`, SHA-256
`836714ec41f92f45af363d7bf3c9b9cfc2cccdbd19a1c57f355b471068648049`.
REA 6.5.0 with Ghidra 12.1.4 imported the PE image at `0x140000000`.
Function boundaries, string references, recovered control flow and ARM64
instructions were inspected locally. The original inputs and recovered code
remain local; this document records interoperable data-field observations.

## PRLD

The routine at `0x140001858`, referenced by the privacy-configuration log
string, consumes six numeric fields from the first nested tuple. Its ARM64
instructions at `0x140001a18` through `0x140001ad4` confirm the bit extraction.
The execution path at `0x140001b38` distinguishes GPIO, PMIC and component
P-state operations and maintains aggregate usage counters.

| Bits | Observed role | Limits |
| --- | --- | --- |
| 0–7 | Membership in eight sensor slots | Mapping slots to physical sensors requires separate evidence |
| 8–9 | Mechanism: 1 GPIO, 2 PMIC, 3 component P-state | Zero is inactive or unsupported |
| 10–12 | GPIO selector or component index | GPIO selectors 0–5 use Resource Hub; 6–7 only log in the observed path |
| 13 | GPIO output inversion | Applies to the GPIO mechanism |
| 10–15, 16–25 | PMIC selector fields | Their physical meanings are not established |

For the observed MSHW0495 file, `0x106` selects slots 1 and 2 through GPIO
resource index 0; `0x501` selects slot 0 through GPIO resource index 1. Thus the
decimal value 1281 is a packed descriptor. Neither value is a physical GPIO.
The mapping from these resource indices to the board's GPIO resources remains
separate from this format decoding.

## PCFG

The routine at `0x140002ef0` reads a flag field and eight sensor words, then
handles four logical sensor slots. Flag bits 0–3 form their presence mask;
bits 4–7 remain uninterpreted. It selects the second group of four words when
another callback returns `0x27b`; the interpreter preserves both banks. This
observation does not identify the callback's platform ID namespace.

The ARM64 bit extractions at `0x140003238` through `0x1400032a4` and the adjacent
field-specific log arguments support these names:

| Bits in each sensor word | Observed field |
| --- | --- |
| 0–3 | Orientation enumeration |
| 4–7 | Direction enumeration |
| 8 | Flash presence |
| 9–11 | Flash index |
| 12–13 | Flash/CCI timer index |
| 14–15 | Trigger type |
| 16–19 | Windows I2C master index |
| 20–23 | CSIPHY index |
| 24 | Face-authentication flag |
| 25 | Shutter type |

Flag bit 8 indicates position-offset data and bit 9 indicates privacy-shutter
data. Tail words remain uninterpreted in the source tool. Orientation values
are not degrees, Windows I2C indices are not a proven Linux bus mapping, and
neither CSI lane layout nor link frequency follows from these fields.

The MSHW0495 bank-zero words begin `0x00110000`, `0x00230010`, `0x01000110`.
The second word's CSIPHY 2 agrees with the existing front-camera graph. The
combined CCI evidence below now maps logical master 3 to Linux `cci1_i2c1`
on the captured platform. The PHY match alone does not prove the identities
of every sensor slot or justify enabling the rear or IR hardware.

## SCFG

The inspected front and rear sensor configuration files each contain two
string labels and three numeric words. REA inspected the front consumer's
getter at `0x140001b48`, probe at `0x140009f18` and read helper at `0x14000ad68`;
the rear equivalents are `0x140005188`, `0x14000d8a0` and `0x14000e6c8`.
The front consumer SHA-256 is
`80a8e4a1ef8f0dacfbc2e8c6919cb269993057ffd3133c2ef7016ff742e46f03`;
the rear consumer SHA-256 is
`b7d7a278c5e7b92ebf35f870a7e06cbad670ffb35bfaf40106e27b09bf33fabb`.

| First numeric word | Observed role |
| --- | --- |
| Bits 0–15 | Raw Windows slave-address field |
| Bits 16–17 | Frequency code: 0/1/2 map to 100/400/1000; 3 falls back to 400 |
| Bits 18–19 | Register-address type: 0/1/2 map to 1/2/3; 3 falls back to 1 |
| Bits 20–21 | Register-data type: same enumeration mapping |
| Bits 22–31 | Uninterpreted |

The second word contains the probe register in bits 0–15 and expected value
in bits 16–31. The third word remains opaque. Observed type two uses a two-byte
register address and combines two returned bytes high first. The numerical
frequency-setting units are not established by this consumer. Enum fallback
is observed Windows behavior, not an endorsement of unsupported hardware.

Rear `0xd855300b` means a probe of register `0x300b` for bytes `d8 55`, agreeing
with the native OV13858 driver's three-byte `00 d8 55` check at `0x300a`.
Front `0x0aff0004` instead describes a probe at `0x0004` for `0aff`; it is not
the existing native IMX681 model-ID check at `0x0016` for `0681` and does not
justify replacing that check. Both inspected files contain raw address `0x20`.
Neither consumer establishes seven-bit normalization or Linux CCI bus mapping.
The 16-bit stored field is copied into a byte in the inspected interface;
both the stored field and that byte are reported without normalizing either.
The tool retains names as inert labels and opens no referenced paths.

### Platform CCI conversion

Further analysis resolves the address conversion outside those consumers.
In the platform binary identified at the start of this document, command
builder `0x14000f838` extracts bits 1–7 of the raw interface byte; instruction
`0x14000f85c` is the corresponding `ubfx`. It places the result in the CCI
SET_PARAM address field. Native `drivers/i2c/busses/i2c-qcom-cci.c` puts its
seven-bit Linux address into the same field without that shift. Consequently
raw `0x20` corresponds to CCI/Linux address `0x10`, not Linux address `0x20`.
The interpreter reports that observed conversion alongside both raw fields.

Controller routing is a separate step. Initialization at
`0x14000a988` splits a nonnegative logical master index into controller
instance `index / 2` and master `index % 2`. However, `0x14003b850` obtains
controller bases dynamically from translated firmware resource ordinals two
and three. The split alone does not prove their Linux identities.

### Captured controller resources

A saved `ACPI\QCOM0C32` / MSHW0495 registry export supplies the missing
allocation addresses. Its SHA-256 is
`a46ebcfc358440fc30c196f9dc0c14b028f06a197bd6964bcd93b374b592f061`.
`BootConfig` and `AllocConfig` contain the same 200-byte resource list,
SHA-256 `f340bd8f69c8e52ce6bfcf82e6912eeea896c0c4a86ba405d5c75815bca9f857`.
Four byte-identical copies are one observation.

The observed list has one full descriptor, version/revision 1/1 and nine
20-byte partial descriptors. Absolute ordinal 2 is memory at `0x0ac15000`,
length `0x1000`; ordinal 3 is memory at `0x0ac16000`, length `0x1000`.
Those match `cci0` and `cci1` in `hamoa.dtsi`. IRQ vectors 492 and 303 also
agree with the respective Linux GIC SPI 460 and 271 plus the architectural
32 offset. Combining that capture with the independently verified ordinal
consumer and index split gives this controller map:

| Logical master | Captured controller memory | Linux bus |
| --- | --- | --- |
| 0 | `0x0ac15000` | `cci0_i2c0` |
| 1 | `0x0ac15000` | `cci0_i2c1` |
| 2 | `0x0ac16000` | `cci1_i2c0` |
| 3 | `0x0ac16000` | `cci1_i2c1` |

These are captured registry allocations, not an evaluated `_CRS` or a direct
WDF translated-list dump. Do not present them as either, infer physical sensor
presence from them, or assume a different board revision has identical wiring.
Associating a PCFG slot with a sensor still requires the sensor-ID lookup
chain; controller mapping alone is not board enablement.

The bounded reader follows the documented
[resource-list structure](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_cm_resource_list)
and [partial-descriptor fields](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_cm_partial_resource_descriptor),
with an explicit observed-layout selection. It never applies resources or
turns connection IDs into Linux GPIO numbers.

## Power-resource delays

Static analysis of `qcpep8380.sys`, SHA-256
`e5bdeb8c0fb8823990f7520a19c00807711cf909f91f8d67db78ffb18c1b2277`,
establishes milliseconds for the normal runtime `DELAY` resource path:

1. The provider registered at `0x140089880` handles the interface queried by
   the camera platform; `0x14003de98` validates the AeoB signature.
2. Type-nine parsing at `0x140414680` copies the first integer unscaled into
   resource data at offset `0x18`.
3. Conversion at `0x14040aa74` and construction at `0x1404265f0` preserve it
   into runtime-node offset `0x20`.
4. Execution at `0x140128170` converts it to negative `value * 10000` relative
   100-nanosecond ticks for the sleep path, or `value * 1000` microseconds for
   the busy-wait path.

Thus the front and rear resource values 1 and 10 represent 1 ms and 10 ms.
Elapsed time can already satisfy a delay, and the crashdump path only logs its
completion; this is not a claim that every path always sleeps. The platform's
fixed 20-microsecond polling and named sensor-mode delay fields are separate.
These observations do not establish supply mapping, regulator ownership or
safe Linux sequencing by themselves.

## GPIO and regulator resource fields

Further instruction-level PEP analysis establishes the mandatory six-field
`TLMMGPIO` payload. Parsing at `0x1404100e0`, conversion at `0x14040a2f0`
and construction at `0x14041fe70` preserve these fields into the runtime
node consumed by `0x14011d290`:

| Field | Observed meaning |
| --- | --- |
| 0 | Physical TLMM GPIO number |
| 1 | Raw output level, not a logical polarity flag |
| 2 | Function/mux encoding |
| 3 | Output-enable encoding |
| 4 | Pull encoding |
| 5 | Drive-strength encoding |

Instructions `0x1402082e4..0x14020833c` clear or set output bit 1 at the
GPIO's I/O register. The rear tuples select GPIO110, physical low then high,
and low during power-down. They select GPIO mux, output enabled, no pull
and a drive encoding equivalent to native 2 mA. There is no inversion flag
in these six fields. This does not independently prove that the net is
OV13858 reset rather than another module enable.

For observed `PMICVREGVOTE` input type 1, `0x140440af0` selects internal
type zero and `0x140412cb0` reads voltage, software-enable, mode and a fourth
vote field. The fourth field and optional extensions remain uninterpreted.
The native service callback chain reaches `0x1402271c0`, which emits voltage
divided by 1,000, enable and mode into the same RPMh fields used by Linux.
The resource-name table maps the rear votes to `ldom6` at 1.8 V, `ldom1`
at 1.2 V, `ldom5` at 2.8 V and `ldob16` at 2.9 V. Off tuples remove that
consumer's voltage and enable votes; shared physical rails may remain on.

The configured rear order holds GPIO110 low, votes the first three rails,
delays 1 ms, votes the 2.9 V rail, enables MCLK1 at 19.2 MHz, drives GPIO110
high and delays 10 ms. Power-down configures low, delays 1 ms, stops MCLK1
and withdraws the rail votes in reverse order. This is configured order,
not measured electrical timing or exclusive ownership. In particular,
voltage does not prove sensor pin roles or that `ldob16` powers autofocus.

Linux TLMM pinctrl/GPIO and RPMh regulator providers already express these
operations. A production driver must use those frameworks, not a Windows
resource interpreter or a raw-MMIO compatibility layer. Native
`regulator_bulk_enable()` dispatches asynchronously: array order must not be
mistaken for serialization. Any required sensor-specific order needs a
reviewed native sequence and tracked error unwind after pin/rail ownership
and timing are established.

References: [ACPI output-buffer layout](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/acpiioct/ns-acpiioct-_acpi_eval_output_buffer_v1),
[argument storage](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/acpiioct/ns-acpiioct-_acpi_method_argument_v1),
[relative sleep units](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kedelayexecutionthread),
[stall units](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kestallexecutionprocessor).

## Linux boundary

The production driver must use native Linux firmware-node properties and
driver frameworks. It must not load these Windows configuration containers or
require the MSI at build or runtime. Recovered resource requirements inform
reviewed native configuration; unresolved physical mappings remain explicit
research tasks. Sensor identification, power ownership, error unwinding,
stream lifetime and indicator sharing require their own implementation and
tests.
