# Battery / ACPI-Extension / Thermal Drivers Analysis — SP11 ARM64 Win11 26100 (26.041.12746.0)

- **Analyst:** reverse-engineering subagent
- **Date:** 2026-11-12
- **Targets** (all under `SurfacePro11_ARM_Win11_26100_26.041.12746.0/SurfaceUpdate/`):
  - `battery/SurfaceBattery.sys` 2.135.6.0 (Surface Battery Mini Class Driver, `BATTC.SYS`)
  - `acpiplatformextension/SurfaceAcpiPlatformExtensionDriver.sys` 3.63.33.0
  - `thermalpolicy/SurfaceThermalPolicy.sys`
  - `qcsubsysthermalmgr/qcSubsysThermalMgr.sys` (Qualcomm)
  - `powermeter/SurfacePowerMeterDriver.sys` (adjacent, SAM power telemetry)
- **Audience:** Linux `surface_battery` (`drivers/power/supply/surface_battery.c`) and Qualcomm thermal work in this tree.

> Tooling note: the REA MCP namespace (`tools.rea.*`) was **not registered** in this
> session's Code Mode runtime (exhaustive tool-catalog search returned no `rea` tools and
> `opencode.list_mcp_resources` was empty). Analysis was performed with equivalent local
> tooling: `llvm-objdump` (Homebrew LLVM 23.1.4) ARM64 disassembly, Python PE parsing,
> adrp/add string-xref resolution, ASCII/UTF-16 string extraction. No REA session was
> opened, so no `close_binary` was required. Confidence tags: **[H]**igh / **[M]**edium / **[L]**ow.
> All addresses are PE virtual addresses (image base 0x140000000).

---

## 1. SurfaceBattery.sys — SAM battery protocol (primary Linux-relevant evidence)

### 1.1 Driver shape [H]

- KMDF + DMF (Driver Module Framework). PDB path fragments: `REPOSITORYDIR\sys\{miniclass,McuHandlers,AcpiHandlers,DmfInterface,wdf,SmfInterface}.c`, DMF modules `Dmf_SamCommunicationViaSsh.c`, `Dmf_SamNotificationViaSsh.c`.
- Hardware IDs (UTF-16 strings at 0x60290/0x602b0, file offsets) [H]:
  - `ACPI\MSHW0146` — battery ACPI device
  - `{BFD21D0B-D701-435B-AF19-4A20677898D3}\Target_KIP&Category_Battery` — **SSH device route: Target = KIP, TC = BAT(0x02)**. This is the SSAM `KIP` target (TC 0x02), matching the newer SP11 battery wiring (older devices used SAM target).
- Dual backend: `Mcu*` handlers (SSH/SAM path) and `Acpi*` handlers (`_BST`/`_BIX`/`_PSR`-style ACPI path), selected per platform. Trace strings: `AcpiReadBst`, `AcpiReadCharger`, `AcpiReadPsr`, `AcpiSetBtp`, `AcpiSetBpmDisable`, `AcpiReadDischargeRateEwmaLvl` [H].

### 1.2 SAM/SSH request structure [H for structure, H for constants]

The single SAM-send helper `Dmf_SamCommunicationViaSsh_SendSynchronously` wrapper is at **0x14000d350** (13 call sites). Its request descriptor is built on the stack, 7 bytes at `sp+0x20` (little-endian byte fields), and the wrapper's register args are:
`x2` = connection/flags selector (7 = sync request over KIP battery route), `x4` = output length, `w7` = timeout ms (600).

Request descriptor layout (from `McuReadSta` @ 0x140034a78–0x140034ad8):

| off | field | value |
|---|---|---|
| +0 | target category | 2 (TC_BAT) |
| +1 | target id | `ctx[0x6b]` (runtime = KIP tid, i.e. 0x03 family) |
| +2 | instance id | `ctx[0x6a]` (runtime) |
| +3 | command id | per-handler constant |
| +4 | flags/expect-response | 1 |
| +5..6 | reserved/length | 0 (u16) |

### 1.3 Extracted battery commands (TC = 0x02 BAT) [H — each verified at the `bl 0x14000d350` call site]

| Handler (trace string) | Addr (call site) | CMD (cid) | Out len | Purpose |
|---|---|---|---|---|
| `McuReadSta` | 0x140034ad8 | **0x01** | 4 | battery `_STA`-style status (presence/OK bits) — matches Linux `SAM_BATTERY_STA_CMD` cid 0x01 |
| `McuReadBst` | 0x140034cbc | **0x02** | 0x10 (16) | battery static+dynamic status (`_BST`-like, 16-byte struct) — matches Linux cid 0x02 |
| `McuReadBix` | 0x140034e80 | **0x03** | 0x7b (123) | extended battery info (`_BIX`-like, 123-byte struct) — matches Linux cid 0x03 |
| `McuReadCharger` | 0x140035034 | **0x0d** | 4 | charger/PSR status (AC online) |
| (2nd charger variant, `Artg`) | 0x1400351a4 | **0x0f** | 4 | charger status variant for ARTG (adapter-rate?) platforms — trace `McuReadCharger...Artg` |
| `McuReadTp` | 0x14003535c | **0x53** | 0x15 (21) | thermal profile? battery temp/tether data — 21-byte response **[M: name ambiguous]** |
| `McuReadDischargeRateEwmaLvl` | 0x140035e1c | **0x1a** | 1 | discharge-rate EWMA level (u8) |
| `McuReadMaxChargeState` | 0x1400360b4 | **0x69** | 1 | max-charge (BCL) state read, cached at `ctx+0x4f8` **[H: cid 0x69]** |
| `McuReadBatteryExtendedGeneralInfo` | 0x140036324 | **0x22** | 0x77 (119) | extended general info (119-byte struct), cached at `ctx+0x4fc` |
| `McuSetBtp` | 0x140035668 | **0x04** | req 11 bytes, no out | set battery trip point / notification enable (per-period `%`-change thresholds, payload built from `x6` list) — matches Linux cid 0x04 (`SAM_BATTERY_BTP_CMD`) |
| `McuSetBpmDisable` | 0x140035800 | **0x19** | req 8 bytes, no out | set Battery Protection Mode disable (payload byte + u16 = 1) — charge-limit/BPM control **[H]** |

Additional observations:

- `McuSetBtp` writes a 5-byte payload starting `[02, tid, iid, 04, len=4]` + u16 threshold data at `sp+0x1d` — i.e. BTP is armed with percentage delta windows, consistent with Linux `SPWR_BAT Annunciators/BTP` behavior [M].
- `McuSetBpmDisable` payload ends `sturh w9(=1), [sp,#0x25]` = u16 `1` → "disable BPM" is written as 1; the "battery protection state" change events flow back via `ProcessResponse.BatteryProtectionStatusChange` (trace) and are surfaced through `BatteryClassUpdateData` with tag `{Change battery protection state}[Extended]` [H].
- Charge limiting string: `UpdateBst.Bcl{Battery Charge Limiting}[Extended]` — BCL state folded into BST refresh path; Linux equivalent is the UEFI battery-limit, **no runtime SAM set-command for the limit itself was found in this driver** — the limit is read (0x69) and the BPM disable is set (0x19) [M].

### 1.4 WPP trace channel constants (context only)

Trace helper 0x1400018b8 call triples (component, level, message-id) confirm the handler-to-WPP mapping, e.g. `McuReadSta`→(2,3,0x0a), `McuReadBst`→(2,3,0x0c), `McuReadBix`→(2,3,0x0e), `McuReadTp`→(2,3,0x14), `McuSetBtp`→(2,3,0x1e/0x1f), `McuSetBpmDisable`→(2,3,0x20/0x21), `ExtGeneralInfo`→(2,3,0x1a), `MaxChargeState`→(2,5,0x10), `Ewma`→(4,6,...) [H]. Useful for matching WPP decoders, not for protocol.

### 1.5 Linux cross-check

`drivers/power/supply/surface_battery.c` in this tree already implements cids **0x01 (STA), 0x02 (BST), 0x03 (BIX), 0x04 (BTP set)** with `target_category = SSAM_SSH_TC_BAT` — all four confirmed by this analysis. **Not yet implemented and confirmed here:** cid **0x0d/0x0f charger**, **0x69 max-charge state (BCL read)**, **0x19 BPM disable (charge-protection control)**, **0x1a discharge EWMA**, **0x22 extended general info (119 B)**, **0x53 (21 B, name suggests thermal/temp profile)** [H for IDs, M for semantics]. Note the SP11 SSH route targets **KIP** (`{BFD21D0B-...}\Target_KIP&Category_Battery`), which the Linux driver must match via `SSAM_SSH_TID_KIP`.

### 1.6 Strings of interest [H]

- `EnableBatterySimulation`, `Battery Simulation`, `BatterySimulationControl`, `Parameters` (registry simulation knobs).
- `SurfaceBattery%d` device interface naming.
- Telemetry: `BatteryProtectionStatusChange`, `Bcl{Battery Charge Limiting}`.

---

## 2. SurfaceAcpiPlatformExtensionDriver.sys — PEP/ACPI extension

### 2.1 Role [H]

Windows **PEP (Platform Extension Plugin) for ACPI** KMDF driver ("Surface Hot-Plug Driver" in version resource, 3.63.33.0, (C) 2022). It registers with PoFx as a PEP (`AcpiPepDevice_PepRegister`) and handles:

`PEP_ACPI_PREPARE_DEVICE`, `PEP_ACPI_REGISTER_DEVICE`, `PEP_ACPI_UNREGISTER_DEVICE`, `PEP_ACPI_ABANDON_DEVICE`, `PEP_ACPI_ENUMERATE_DEVICE_NAMESPACE`, `PEP_ACPI_EVALUATE_CONTROL_METHOD`, `PEP_ACPI_QUERY_OBJECT_INFORMATION`, `PEP_ACPI_QUERY_DEVICE_CONTROL_RESOURCES`, `PEP_ACPI_TRANSLATED_DEVICE_CONTROL_RESOURCES`, `PEP_ACPI_WORK` [H — all strings present].

### 2.2 ACPI methods evaluated [H]

- `_DSM` (UNICODE_STRING entry at file offset 0x2e8b8, VA 0x14002e8b8 region) — generic device-specific method dispatch.
- `_FST` (UNICODE_STRING entry at 0x2e8a8, adjacent) — **fan status method**, used by the `AcpiPepDeviceFan` module (`AcpiPepDeviceFan_SyncEvaluateControlMethod`, `_DSM_FST` combined trace name at 0x8674).
- Namespace root string `\_SB` (UTF-16 @ 0x2ca08) and **fan device `\_SB.FAN0`** (UTF-16 @ 0x56428) — the PEP services the fan at ACPI path `\_SB.FAN0`, evaluating `_FST` for status and a `_DSM` (function index table at 0x2e8c8: entries 0x01/0x04/0x07 map into a method-index table with 10-slot array at +0x140008ac0) [M for exact DSM function indexes].
- ETW channel: `Microsoft-Surface-SurfaceAcpiPlatformExtensionDriver/Operational`, event `NotifyCode` — hot-plug notify events forwarded from ACPI notifications (`Dmf_AcpiNotification`) [H].

### 2.3 Linux relevance

Confirms SP11 fan control is ACPI-based at `\_SB.FAN0` via `_FST` + `_DSM` under the PEP plugin — relevant to `surface_platform_profile`/fan work: a Linux driver should probe `\_SB.FAN0` `_DSM`/`_FST` rather than expecting a SAM fan TC on this device [M].

---

## 3. SurfaceThermalPolicy.sys — thermal policy interface

Small KMDF driver wrapping Windows thermal APIs: strings `PoCreateThermalRequest`, `PoDeleteThermalRequest`, `PoSetThermalPassiveCooling`, `Dmf_ThermalPolicyInterface.c` [H]. It is a **passive-cooling request broker** — no SAM commands, no ACPI methods, no numeric thresholds in this binary [H]. No direct Linux evidence value beyond confirming Windows uses PoFx thermal requests (kernel-managed cooling), i.e. thermal policy decisions live in the kernel/PEP, not in this driver.

---

## 4. qcSubsysThermalMgr.sys — Qualcomm subsystem thermal mitigation

Qualcomm UMDF/KMDF driver ("Qualcomm(R) Subsystem Thermal Mitigation Device", "Subsys Base Thermal Mitigation Device"). Source paths: `Z:\b\WP\SubsysThermalMgr\soc_agn\latest\SubsysThermalMgr\{BaseTMD_Device,device}.c`; PDB `qcSubsysThermalMgr.pdb` [H]. Talks over `\Device\IPC` (UTF-16 @ 0x20660) — Qualcomm IPC router — to remote subsystems (modem/ADSP/CDSP) for thermal mitigation [M]. **No ACPI methods, no embedded temperature thresholds, no SAM commands** [H]. Linux counterpart: `qcom subsystem thermal` / thermal mitigation over QRTR — nothing new extractable here.

---

## 5. SurfacePowerMeterDriver.sys — power metering (adjacent evidence)

EMI (Energy Meter Interface) driver with SAM telemetry: modules `PowerMeterBus/Chip/Device/Maxim/Mcu/Virtual`, `DMF_AcpiTarget_InvokeDsmRaw DSM_CHANNEL_INFO_FUNCTION_INDEX`, device interface `{C5845582-D98F-43dd-87F7-7EC1EED74054}\PowerMeter%d`, EMI v1 interface, `EMI_EX_GET_CURRENT/VOLTAGE`, `DMF_SamCommunicationViaSsh_SendSynchronously` [H strings]. Relevant for Linux power--meter/EMI support on SP11: channel metadata comes from an ACPI **`_DSM` with function index `DSM_CHANNEL_INFO`** and a SAM SSH sync send for meter updates [M]. No thermal thresholds.

---

## 6. Thermal thresholds summary

**No numeric thermal trip points (e.g. `_CRT`/`_PSV`/`_HOT` values) were found in any of these drivers** [H]. On this platform:
- Battery-side temperature data enters via the BAT TC responses (`McuReadBst` 0x02 / `McuReadTp` 0x53 payloads carry temperature fields [M]).
- Passive cooling policy is brokered via PoFx (`PoSetThermalPassiveCooling`) with thresholds held in ACPI DSDT/Windows kernel, not in these binaries.
- Qualcomm-side mitigation values live behind the IPC router in remote subsystems (see `thermalpolicy`, `qcSubsysThermalMgr` sections).

For concrete trip points, dump the DSDT/`\_TZ` namespace from a live SP11 (the `acpiplatformextension` INF confirms ACPI loads before PEP, and `timealarmacpifilter`/`asdext` packages in the same bundle contain the AML tables).

---

## 7. Constants / command-ID quick table

| Value | Meaning | Source |
|---|---|---|
| TC 0x02 | SSH target category Battery | SurfaceBattery request descriptors + Linux `SSAM_SSH_TC_BAT` |
| TID: KIP | battery SSH target on SP11 (`Target_KIP&Category_Battery`) | SurfaceBattery UTF-16 strings |
| cid 0x01 | BAT read STA | SurfaceBattery 0x140034ad8 |
| cid 0x02 | BAT read BST (16 B) | 0x140034cbc |
| cid 0x03 | BAT read BIX (123 B) | 0x140034e80 |
| cid 0x04 | BAT set BTP (notify thresholds) | 0x140035668 |
| cid 0x0d | charger status | 0x140035034 |
| cid 0x0f | charger status (Artg variant) | 0x1400351a4 |
| cid 0x19 | BAT set BPM disable (charge protection) | 0x140035800 |
| cid 0x1a | discharge-rate EWMA level (u8) | 0x140035e1c |
| cid 0x22 | battery extended general info (119 B) | 0x140036324 |
| cid 0x53 | BAT "Tp" read (21 B; thermal/temp profile?) | 0x14003535c |
| cid 0x69 | max-charge (BCL) state read (u8) | 0x1400360b4 |
| timeout 600 ms | default sync-request timeout (w7=0x258) | all send sites |
| `ACPI\MSHW0146` | battery ACPI HID | SurfaceBattery strings |
| `\_SB.FAN0`, `_FST`, `_DSM` | fan ACPI path + methods | AcpiPlatformExtension strings |
| `{BFD21D0B-D701-435B-AF19-4A20677898D3}` | SSH/KIP device interface GUID (battery) | SurfaceBattery strings |
| `{C5845582-D98F-43dd-87F7-7EC1EED74054}` | PowerMeter%d EMI interface GUID | PowerMeter strings |

## 8. Limitations

- Field-level struct layouts (offsets within the 16/123/119/21-byte responses) are not recoverable from this driver alone — the miniclass copies them opaquely into `BATTERY_INFORMATION`/`BATTERY_STATUS` via `BatteryClassUpdateData`; correlate with live SAM traces or the SAM firmware image (`evidence/sam-report.md`) for semantics.
- `McuReadTp` cid 0x53 semantics inferred from name only [L].
- No thermal trip-point constants exist in these binaries (verified by full string/immediate scans) — trip points require DSDT extraction from a live device.
- The `Artg` suffix (charger variant) likely denotes a charger IC family; which SKUs use 0x0d vs 0x0f is runtime-selected (`ctx+0x68` flag) [M].
- REA MCP tooling was unavailable in this runtime; all conclusions come from local disassembly (`llvm-objdump`) and byte-level analysis as documented above. No binary was left open in any tool.
