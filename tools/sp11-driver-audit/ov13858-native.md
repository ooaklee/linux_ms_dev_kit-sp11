# Native OV13858 foundation

This development series extends the existing Linux OV13858 sensor driver.
It requires no Windows installation, MSI, vendor configuration container,
Windows executable or recovered register table to build or run. It is not yet
a working rear-camera configuration for ARM Surface Pro 11.

## Scope

The generic `ovti,ov13858` binding supplies a native firmware-node interface:
one 19.2 MHz clock, `dovdd`, `avdd` and `dvdd` supplies, an optional reset GPIO
and a CSI-2 endpoint. Existing ACPI systems retain their power-domain resource
ownership and do not acquire the new DT supplies or GPIO.

The driver retains its existing register tables. The endpoint selects the
existing 270 MHz modes, the existing 540 MHz mode, or both. Mode enumeration,
format selection and controls use that selected set. Four D-PHY data lanes
in order, clock lane zero, normal polarities and continuous clock are required;
unsupported configurations are rejected before power resources are used.

Runtime PM is enabled before asynchronous subdevice registration, with a
reference held through publication. Unknown supply state after enable,
disable or unwind failure latches a fatal power fault. Reset remains asserted,
the enabled clock is stopped once, and further register I/O or stream enable
is rejected. This is containment, not automatic electrical recovery: a reprobe
alone cannot establish that a failed regulator provider or physical rail has
recovered.

## Source provenance

Power sequencing follows native Linux regulator, clock, GPIO and V4L2 APIs.
The 1 ms reset hold and 5 ms post-release recovery use the precedent in
[Sergey Lebedev's Linux-media v3 proposal](https://lore.kernel.org/linux-media/20260831130312.26296-1-lsa.uz@pm.me/).
That work concerns Intel Surface Pro 11 for Business; these times are not
measurements on ARM Denali. Its contribution is referenced rather than
attributing this implementation or a new sign-off to that author.

Separate static research establishes milliseconds for the Windows resource
`DELAY` values, including 1 and 10 ms in the rear configuration. This does not
prove that the generic sensor timing covers the entire Denali module. Do not
silently substitute either sequence for verified board/module requirements.

## Before Denali enablement

No Denali DTS node is enabled by this series. Resolve and review:

- Current-board and effective-PCFG-bank confirmation. Static consumer
  tracing now associates the rear with slot zero; the saved MSHW0495
  allocation, HAL and SCFG together describe `cci0_i2c1` at `0x10`, CSIPHY1.
  This is not a fresh translated-resource or physical capture trace.
- Reset-net and sensor pin-to-rail ownership. Static analysis establishes
  physical GPIO110 low/high levels and the `ldom6`/`ldom1`/`ldom5`/`ldob16`
  voltage votes, but does not prove that GPIO110 is reset or that the
  separately sequenced 2.9 V rail belongs to an actuator.
- Physical sensor-to-receiver lane assignment, polarities, sensor PLL and
  module-specific timing. The verified Windows producer's nominal 592.8 MHz
  request assumes zero overhead, whose effective setting is unresolved;
  it is not a new validated Linux mode. Metadata and listed frame-length
  writes still disagree, and output and exposure clocks are distinct.
- Actuator/OTP calibration ownership and indicator sharing with the front
  and IR cameras, using native Linux interfaces.

The [resource field notes](camera-fields.md) distinguish configured serial
ordering from the generic driver's asynchronous bulk regulator enable. The
5 ms generic post-reset delay also differs from the module's configured
10 ms. Neither difference is a measured failure or a verified sensor-wide
requirement; do not change generic ACPI behaviour or add a guessed fourth
sensor supply to reproduce an unverified board sequence.

Runtime qualification must cover chip identification, test-pattern/capture
layout, start/stop/reopen, fault and probe-deferral paths, indicator release,
idle power, system sleep, and existing ACPI users. Source compilation and
binding validation are not evidence that rear capture works.

## Source checks completed

The source is split into binding commit `82d8210aa474` and native driver commit
`135b49d9a108`. The existing driver blob was identical in this research base,
the tested v8 carrier and the inspected jglathe 7.2.9 tree before these edits.

- ARM64 GCC 13.3 `W=1` driver-object builds passed with ACPI enabled, ACPI
  disabled, and runtime PM disabled in a minimal configuration.
- Targeted binding/example validation passed, with positive frequency-subset
  fixtures and negative lane, polarity, clock and frequency fixtures.
- Strict checkpatch reported zero errors, warnings or checks.
- Seven host logic scenarios using the exact extracted power, register and
  stream-guard functions passed AddressSanitizer and UndefinedBehaviorSanitizer.
  These use API stubs, not the real PM core, regulators or hardware.
- An OpenCode GLM 5.1 final source review found no source-level blocker after
  its initial findings were checked against this tree and the genuine unknown
  supply-state failure was contained.
- Both focused commits applied cleanly to an exact tested-v8 source export.
  Its exported `qcom-x1e` configuration already selected OV13858 as a module;
  the ARM64 `W=1` driver-object build passed against those carrier headers,
  with no Windows package present in the build environment.

The unchanged OV13858 snapshot was also included in the previous 23-patch
revision's complete ARM64 kernel/module link. Revision 2 adds only the
separately tested IR HDR correction; see
[the validation summary](native-camera-validation.md) for that distinction.
These checks do not qualify a combined hardware release. Integration with later
carrier changes and physical OF/ACPI regression tests remain required before a
release claim.
