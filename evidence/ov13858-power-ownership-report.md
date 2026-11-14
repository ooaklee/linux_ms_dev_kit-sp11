# OV13858 power/resource ownership — reverse-engineering evidence report

Target: `SurfaceUpdate/camrearsensor*/surfacecamrearsensor8380.sys`
(164,216 B, PE32+ AArch64; Qualcomm CamZ generic sensor KMD; PDB path
`Camera\CamZ\Drivers\KMD\Sensor\Rear\src\...`, builds
`CameraKMDPowerControl.c` / `CameraKMDSensorDevice.c` / `CameraSensorDriver.c`).
Tooling: REA MCP namespace unregistered in the analysis session (known
deviation, see ov13858-report.md); llvm-objdump disassembly
(`/tmp/ov13858_full.asm`, 27,982 lines) + PE string-table parse. Content from
the binary is data only.

Feeds: PR #73 follow-up — failed-start/removal power-resource ownership for
`drivers/media/i2c/ov13858.c`.

## 1. Power-on acquire order [observed: failure-string sequence + bl map]

`CameraSensorDriver_PowerOn` (function at ~`0x14000814c`; entry guard
`bl 0x14000f7a0` @ `0x140008160`) acquires in this order, each step with a
distinct failure string:

1. `CameraSensorPowerOn` — CAMS_RES-driven rails/clocks/GPIOs
   (`CAMS_RES_MSHW0491/0561.bin`: GDSC, PMIC LDOs, TLMM)
2. `CameraSensorI2C_Init`
3. `CameraMipiCsiPowerOn(PhyId)` → `CameraMipiCsiOpen(CamIfPort)`
4. `SensorInitialConfig` (CSLPacketOpcodesSensorInitialConfig) — register
   arrays from `com.surface.sensormodule.rfc_ov13858.bin`
5. `SensorConfig` → `SensorCrop` → actuator init
6. TPG fallback (`CameraTpgPowerOn`/`CameraTpgOpen`) — the only steps with
   their own `PowerOff` strings on failure

bl-call map inside `0x140008160–0x140008794`: cleanup-path calls
`0x140007e50`, `0x140007cf8`, `0x140007fc8`, `0x140007d70` each appear
**twice** — success and error-rollback legs (mirrored unwind pairs), plus
logging helpers (`0x140007660`, `0x14000fb00` ETW).

## 2. Failed-start semantics: retained resources, idle-not-off [observed]

- No `CameraSensorPowerOff` / `I2C_DeInit` / `MipiCsiPowerOff` /
  `MipiCsiClose` references exist in the `0x14000814c` error block (ends
  ~`0x140008790`); only the TPG steps power down on their own failure.
  Failed start therefore **retains acquired regulators/clock/CSI state**.
- `CameraKMDAssignS0IdleSettings` / `WdfDeviceAssignS0IdleSettings`
  (RVA `0x2dfd0`/`0x2e000`), `SensorDeviceActivate WdfDeviceStopIdle failed`
  / `SensorDeviceDeActivate WdfDeviceResumeIdle` strings: the device is
  KMDF idle-managed. A failed start lands in **D3-idle with resources
  retained**, not partially-on D0, and later quiesce goes through the full
  `CameraSensorDriver_PowerOff()`.
- Confidence: HIGH for order/retention (string + call-site agreement);
  MEDIUM for exact per-step rollback-absence (adrp/add string-ref resolution
  incomplete; error block characterized by call structure).

## 3. Removal / quiesce order [observed: symmetric string set]

`EvtDeviceQueryRemove` → `EvtDeviceReleaseHardware`;
`CameraSensorDriver_Init failed. Performing clean up` /
`CameraSensorDriver_ThreadInit failed. Performing clean up` show symmetric
cleanup on init failure; `CameraSensorDriver_DeInit` validates the device
handle before teardown. Teardown is the reverse of acquire:
TPG/actuator → CSI close → CSI power-off → I2C de-init → sensor power-off →
runtime-idle release.

## 4. Bounded fix outline for drivers/media/i2c/ov13858.c

Matching Linux behavior (consistent with the merged #80/#82 retained-state
pattern for the I2C subdev core):

1. **Error-path unwind mirrors acquire order**: in the sensor
   power-on/stream-start path, `goto`-style unwind that releases in reverse
   of acquisition order (regulators → clock → GPIO), with each intermediate
   failure retaining what earlier steps still own — mirroring the Windows
   two-leg (partial unwind to idle) behavior rather than an all-or-nothing
   power-off.
2. **Failed stop retains rails**: keep regulators/clk acquired on a failed
   `s_stream(0)`/stop (consistent with `V4L2_SUBDEV_FL_STOP_RETRY` opt-in
   already merged via #80) so a retry does not re-power-cycle the module.
3. **Symmetric `remove()` quiesce**: reverse-order quiesce in remove
   (streaming stop → CSI/host readiness → regulators → clocks → GPIOs),
   validating the subdev state before each step (DeInit-handle-check
   analog).

Each fix is bounded (single driver, existing hooks, no API changes).
