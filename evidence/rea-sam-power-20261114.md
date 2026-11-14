# REA SAM/power evidence — SurfaceBattery + SurfacePowerStateDriver (2026-11-14)

Decompile-grade follow-up to `evidence/rea-fleet-20261114.md` REA-6, produced
with REA + Ghidra 12.1.4 (`rea-agents` CLI, `decompile-all`) against the
Windows driver payloads from
`SurfacePro11_ARM_Win11_26100_26.041.12746.0/SurfaceUpdate`:

- `SurfaceBattery.sys` (3.26.7.0, PDB `SurfaceBattery.pdb`
  GUID `1619c3ce-1bd6-8f41-2a4d-82c8922cae9a`) — 887 functions decompiled.
- `SurfacePowerStateDriver.sys` (3.26.7.0, PDB
  GUID `c7ccf90a-ea6a-fd55-f1d8-f9600dd1de19`) — 660 functions decompiled.
- `SurfaceSAM_27.201.139.bin` (1.7 MB SAM/Monzino firmware payload).

## 1. SAM firmware payload (SurfaceSAM_27.201.139.bin)

- Signed by Microsoft "Monzino Firmware Signer" / "Surface LFF Firmware CA
  2021". No `_CPD_`/`FLASH` container magics — raw signed payload for the
  SAM (Monzino) EC, not a wrapped UEFI capsule.
- ~919 printable strings, all certificate/PKI material. **No SSH protocol
  strings**: request dispatch tables live in code, not strings.

## 2. SurfaceBattery.sys — SAM TC 0x02 (BAT) command map

Recovered from WPP trace strings (`Mcu*` handlers in
`REPOSITORYDIR\sys\McuHandlers.c`) plus the decompiled request-builder
functions (`0x140034a70`, `0x140034c20`, `0x140034de8`, `0x140034fc0`,
`0x1400352c0`, `0x1400355c8`, `0x140035798`, `0x140035930`).

| Handler (trace name)      | TC    | CID    | Len          | R/W | Linux equivalent |
|---------------------------|-------|--------|--------------|-----|------------------|
| `McuReadSta`              | 0x02  | 0x01   | 4            | R   | `ssam_bat_get_sta` ✓ |
| `McuReadBix`              | 0x02  | 0x02   | 0x7b (119)   | R   | `ssam_bat_get_bix` ✓ |
| `McuReadBst`              | 0x02  | 0x03   | 0x10 (16)    | R   | `ssam_bat_get_bst` ✓ |
| `McuSetBtp` / `SetBtp`    | 0x02  | 0x04   | 4            | W   | `ssam_bat_set_btp` ✓ |
| `McuReadCharger`          | 0x02  | 0x0d   | 4            | R   | `surface_ac` cid 0x0d ✓ (with **0x0f fallback** below) |
| `McuReadCharger` fallback | 0x02  | 0x0f   | 4            | R   | **not in Linux** |
| `McuReadTp`               | 0x02  | 0x53   | 0x15 (21)    | R   | **not in Linux** — trip-point array read |
| `McuSetBpmDisable`        | 0x02  | 0x19   |              | W   | **not in Linux** — battery-power-mode disable |
| `McuReadDischargeRateEwmaLvl` | 0x02 | 0x1a | 1            | R   | **not in Linux** — EWMA discharge level |
| `McuReadBatteryExtendedGeneralInfo` | 0x02 | 0x22 | 0x77 (119) | R | **not in Linux** — extended battery info block |
| `McuReadMaxChargeState`   | 0x02  | 0x69   | 1            | R   | **not in Linux** |

Linux `drivers/power/supply/surface_battery.c` implements CIDs 0x01–0x04;
`surface_charger.c` implements 0x01/0x0d. Windows additionally uses
**0x0f, 0x19, 0x1a, 0x22, 0x53, 0x69** on TC 0x02. For SP11
charge-limit/trip-point experiments, **0x53 (trip points)** and **0x19 (BPM
disable)** are the candidates reachable via ssam debugfs.

## 3. Request struct layout (confirmed)

Windows DMF passes a 7-byte request to the SSH send helper (battery image
`FUN_14000d350`):

```
{ tc:u8, iid:u8, tid:u8, sid:u8 = 1, cid:u8, rqid:u16 = 0 }
```

The DMF layer prepends the `0x80` SSH_PLD_TYPE_CMD byte on the wire. This
maps 1:1 onto Linux `struct ssh_command`
(`include/linux/surface_aggregator/serial_hub.h`):

```
u8 type; u8 tc; u8 tid; u8 sid; u8 iid; __le16 rqid; u8 cid;
```

Device-context fields in SurfaceBattery.sys: `+0x69` = slot index,
`+0x6a` = target id (tid), `+0x6b` = instance id (iid).

## 4. Device enumeration IDs

AddDevice (`0x140039940`) matches hardware IDs:

- `{BFD21D0B-D701-435B-AF19-4A20677898D3}\Target_KIP&Category_Battery`
  — KIP origin, tid = 1, iid = 1.
- `ACPI\MSHW0146` — tid = 1, iid = 1 or 2 (per-unit index at `+0x69`).

Cross-check vs Linux match tables — consistent:

- `SSAM_SDEV(BAT, SAM, 0x01, 0x00)` (BAT1) and `SSAM_SDEV(BAT, KIP, 0x01,
  0x00)` (BAT2) in `surface_battery.c`.
- `SSAM_SDEV(BAT, SAM, 0x01, 0x01)` (ADP1) in `surface_charger.c`.

## 5. SurfacePowerStateDriver.sys

- Registers two `PoRegisterPowerSettingCallback`s (`0x140027a20`):
  - **`GUID_CONSOLE_DISPLAY_STATE`**
    (`6fe69556-704a-47a0-8f24-c28d936fda47`, `0x14001ed78`) →
    `FUN_140026a30`: monitor-off stores state at ctx+0x44 and pushes
    brightness sentinel `0x65` (101); monitor-on pushes the cached
    brightness value.
  - **`GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS`**
    (`8ffee2c6-2d01-46be-adb9-398addc5b4ff`, `0x14001ec90`) →
    `FUN_1400268d0`: clamps values <0 or >100 to 0; suppresses brightness
    sends while the console display is off.
- SAM/SSH sends run through DMF `SamCommunicationViaSsh` / `SptPowerState`
  modules behind CFG-indirect calls; request bytes are assembled at
  runtime, so no static TC/CID extraction was possible from this image.
- Only 88 real functions in the image (rest is DMF library code: LiveKernel
  Dump, BufferPool, ContinuousRequestTarget, HidTarget, etc.).

## Artifacts

- Decompilation outputs: `/tmp/rea-bat-decomp` (887 fns),
  `/tmp/rea-pw-decomp` (660 fns) — session-local, not committed.
- Driver binaries: `/tmp/rea-sam/` — session-local, not committed
  (redistribution-restricted Microsoft payloads).

## Conclusion for the SP11 port

The Linux SAM battery driver covers the core `_STA`/`_BIX`/`_BST`/`_BTP`/
`_PSR` surface. Windows-only TC 0x02 CIDs — notably 0x53 (trip-point
array), 0x19 (BPM disable), 0x1a (EWMA discharge), 0x22 (extended general
info, 119 B), and the 0x0d→0x0f charger fallback — are candidate
extensions for full battery-management parity on the Denali battery.
