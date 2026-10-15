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
The second word's CSIPHY 2 agrees with the existing front-camera graph. Its
Windows master 3 still needs an independent mapping to Linux `cci1_i2c1`.
The PHY match is corroboration; it does not alone prove the identities of every
sensor slot or justify enabling the rear or IR hardware.

## Linux boundary

The production driver must use native Linux firmware-node properties and
driver frameworks. It must not load these Windows configuration containers or
require the MSI at build or runtime. Recovered resource requirements inform
reviewed native configuration; unresolved physical mappings remain explicit
research tasks. Sensor identification, power ownership, error unwinding,
stream lifetime and indicator sharing require their own implementation and
tests.
