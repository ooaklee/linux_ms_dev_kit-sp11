# SAM Firmware Image Analysis — SurfaceSAM_27.201.139.bin

- **Analyst:** reverse-engineering subagent
- **Date:** 2026-11-12
- **Target:** `SurfacePro11_ARM_Win11_26100_26.041.12746.0/SurfaceUpdate/SAM/SurfaceSAM_27.201.139.bin` (1,695,141 bytes, 0x19DDA5)
- **Audience:** Linux Surface Aggregator driver work (`drivers/platform/surface`), fork additions: fan, perf-profile, POS tablet-switch nodes, `microsoft,sam-no-d3-close-handle`

> Tooling note: the REA MCP namespace (`tools.rea.*`) was **not present** in this session's
> Code Mode runtime (tool search returned no `rea` tools and MCP resource listing was empty).
> Analysis was performed with equivalent local tooling (Python byte parsing, string scans,
> structure heuristics). No `close_binary` tool exists in this runtime; nothing was left open.
> Confidence tags: **[H]**igh / **[M]**edium / **[L]**ow.

## 1. Format

The file is a Microsoft **Surface firmware-package container**, not a raw MCU image. Structure:

| Offset (example) | Content |
|---|---|
| `0x0000000` | Container header. GUID `6dcbd5ed-e82d-4c44-bda1-7194199ad92a` [M], then LE32 fields: `0x20` (?), `0x50000` (block/chunk size?), `0x19DDA5` = exact file size (strong self-describing length field [H]) |
| `0x0000016–0x000003F` | More header fields, including a second GUID `632cbe1c-c56f-4c4c-b309-87e5da648d16` at offset ~0x34–0x43 [M] — this is the **FirmwareId** (`UEFI\RES_{632CBE1C-...}`) also present in the .inf |
| `0x00000FD–0x00008xx` | X.509 certificate / signature block: strings `Microsoft Surface LFF Firmware CA 2021` (0x11D, 0x823), `Microsoft Surface Monzino Firmware Signer` (0x1C5), CRL/CRT URLs under `microsoft.com/pkiops/...` (0x4F7, 0x56D) [H — PKCS#7-style ASN.1, no `RSA1`/`_SSH` raw blobs found] |
| `~0x001000+` | **Payload copy 1** — firmware image data |
| `~0x0CE97F+` | **Payload copy 2** — byte-for-byte duplicate of payload 1 (delta between mirrored strings `0xCE97F`, e.g. `Module`@0x4023 ↔ 0xD29A2, `SAMR`@0x773A ↔ 0xD60B9; the second half is A/B-redundant or load-shadow copy) [H for duplication, M for purpose] |
| `0x19DDA1` end | Trailer with pointer-like words `... 10 00 00 10 ...` and final dword `0x15967D9E` (likely CRC/checksum of a region — not whole-file CRC32; whole-file CRC32 = `0x9A8336F9`) [L] |

Payload internals: no `MZ`+valid-PE pair was found (the `MZ` hits at e.g. 0x8DAC are
coincidental — `e_lfanew` does not lead to a `PE\0\0`), so the payload is a **raw ARM
firmware image** for the SAM MCU. Strings are THREADEX/RTOS-style (thread/queue/semaphore
names, `%u - %s` diagnostics) — consistent with a ThreadX-class RTOS **[M]**.

Repeated token `1.3.6.1.4.1.311.76.9.x.y` at 0xA9B6–0xAA1F (and mirrored at 0xD9335+) are
SNMP-style **enterprise OIDs** (Microsoft PEN 311, subenterprise 76) — likely internal
SAM telemetry/event namespace identifiers, versions `…9.1.117`, `…143.1.117`, `…141.1.117` **[M]**.

Board codenames in thermal-table names (0x8E13–0xA200): **DENALI** and **TORRANCE** (LCD/OLED
variants) — SAM ships multiple-device thermal profiles in this image **[H]**.

## 2. Version / Identity

| Item | Value | Source | Conf |
|---|---|---|---|
| DriverVer | `10/07/2025, 27.201.139.0` | `SurfaceSAM.inf` [Version] | H |
| FirmwareId | `{632CBE1C-C56F-4C4C-B309-87E5DA648D16}` | .inf `Firmware_AddReg` + `UEFI\RES_...` HWID; GUID bytes also at bin offset ~0x34 | H (inf) / M (bin) |
| FirmwareVersion (registry DWORD) | `0x1B00C98B` = 452985741 | .inf `HKR,,FirmwareVersion` | H |
| Payload copy | duplicated A/B at ~0x1000 and ~0xCE97F | bin | H |
| Catalog | `SurfaceSAM.cat` signs the package | .inf | H |

The .inf installs a **null driver** (`AddService=,%NULL_DRIVER%`) — the Windows SAM client
driver is delivered elsewhere; this package only stages UEFI capsule firmware.

## 3. Events / Registry of Interest (fan, perf-profile, tablet-switch)

### Internal SAM subsystems present in firmware strings (SSH = SAM Serial Hub)

| Offset (copy1) | String | Relevance | Conf |
|---|---|---|---|
| 0x5A4E–0x5A6E | `Thermal SSH Handler`, **`Fan SSH Handler`** @0x5A57 | dedicated fan subsystem; confirms SAM-side fan EC API the Linux `surface_fan` driver talks to | H |
| 0x876C | **`Cmpnt fan`** | fan component label in thermal/component table (see pointer table at 0x8740 with `ThermTrip`, `mACPresent`, temp-sensor list) | H |
| 0x87xx region | sensor labels `U390x` (skin temps 1–6), `BATT1_T`, `TP_TMP`, `TEMP5_PA`, `MODEM`, `*_RTS*`, `*_VTS*` | thermal sensor name table | H |
| 0x9BC4–0xA200 | **Perf-profile table names**: `Quiet_/Nominal_/Perf_/Max_/Gaming_/Mixed_Reality_/Kiosk_` × `{DENALI,TORRANCE}×{LCD,OLED}` (e.g. `Perf_DENALI_LCD`, `Max_TORRANCE_OLED`) | **strongest perf-profile evidence**: SAM enumerates fan/perf modes matching Linux `surface_perf_profile` "Fan profile" categories (quiet/balanced/perf/max + vendor extras Gaming, Mixed Reality, Kiosk) | H |
| 0xA269–0xA2D2 | policy strings `SOFT_SHUTDOWN`, `HARD_SHUTDOWN`, `DIS_FAST_CHRG_`, `SLOW_CHRG_LM`, `TRICKLE_CHRG_LMT` | battery policy states | H |
| 0xAFBB+ | `BPM_IDLE/BPM_ACCUMULATING/BPM_ACTIVE/BPM_INVALID`, `CmpntPolicyBattery` | battery policy state machine (perf-profile adjacent) | H |
| 0x7CDF–0x7D60 | **`Posture State Async Publisher`**, **`ProLidEv`** (PostureLidEvent), `ProRepEv` (PostureReportEvent), `PwrBtnDbnce` | **tablet-mode/posture subsystem**: confirms a SAM posture service reporting lid/slate events asynchronously — directly relevant to the fork's POS tablet-switch node | H |
| 0x5458–0x5490 | `Posture SSH Handler`, `PostureEvent`, `PostureSS` | same, SSH-facing side | H |
| 0x681E–0x6870 | **`HostRegistration Async Publisher`**, `Host Registration SSH Handler` | the host "registry/event registration" SAM service — the mechanism by which the host subscribes to SAM events (matches Linux aggregator relay/event registration, TC `0x0B` domain) | H |
| 0x6770–0x67D0 | `Dispatcher`, `DispatchEvents`, `PendPublishers`, `PendSubscribers`, `PORT`, `%u - %s` | internal pub/sub event dispatcher — telemetry/event routing core | H |
| 0x6650 | `NvmTelemetryThrottlingTimer` | telemetry is stored to NVM with throttling — persistent diagnostics, host-inaccessible by default | M |
| 0x7E40 | `HID Over SSH` | HID-over-Serial-Hub (keyboard/pen/touch transport) | H |
| 0x6486/0x64FC | `SamTpD…`, `SecureMode` | platform secure mode | L |
| 0x75E2–0x7860 | pin/net names `SAM_SWD_CLK/DIO`, `SAM_RESET`, `SAM_DBG_RX/TX`, `SAM_SOC_UART`, `SAMTX_SOCRX`, `SAM_VFG_*`, `U2909_IA/IB`, `U2907_IB` | debug/pin mux table; U29xx = board ICs (UART cross-connect SoC↔SAM) | H |

### Event IDs

No numeric SSH event/TC/cat IDs could be recovered from the string table alone (they are
compiled constants in the firmware code, not strings). The .inf contains **no** event or
telemetry registry keys beyond `FirmwareId`/`FirmwareVersion`/`FirmwareFilename` —
`SurfaceSAM.inf` is a pure firmware-install INF.

Practical guidance for the fork [M]:
- Fan: SAM clearly owns fan control (`Fan SSH Handler`, `Cmpnt fan`); the Linux
  `surface_fan` + thermal SSID (`0x05` thermal, fan r/w `0x05/0x??`) approach matches
  the firmware architecture seen here, but exact category/target IDs must be validated
  by tracing, not from this image.
- Perf-profile: the mode name set (`Quiet/Nominal/Perf/Max/Gaming/Mixed Reality/Kiosk`
  per panel/board) suggests SAM's platform-performance-profile table exceeds the Linux
  `platform_profile` standard set (extra Gaming / Mixed Reality / Kiosk entries may map
  to the fork's perf-profile extension work).
- Tablet-switch: posture subsystem (`Posture…`, `ProLidEv`, `ProRepEv`, async publisher)
  confirms posture events exist as a first-class async notification — supports a
  POS/tablet-switch event source rather than polling.
- `microsoft,sam-no-d3-close-handle`: no direct evidence in this image (that property is
  host-side power-behavior handling); nothing found here contradicts or supports it [L].

## 4. Limitations

1. **No REA tools** were available in this session; all findings come from local
   byte/structure/string analysis. No disassembly (no ARM base address recovered, no
   symbol table beyond RTT string names).
2. Numeric SSH target/category IDs, event codes, and registry-value encodings are **not
   recoverable** from strings; they require disassembly or runtime tracing (e.g. of
   `Host Registration SSH Handler` paths).
3. Container header fields beyond the file-size word are inferred, not verified against
   a published spec — offsets in the table are exact but field semantics are [M]/[L].
4. The checksum trailer (`0x15967D9E` final dword) could not be matched to CRC32/SHA of
   any obvious region; signature verification should be done against `SurfaceSAM.cat`.
5. Payload duplication (A/B at 0x1000 / 0xCE97F) is verified byte-wise for string
   regions; the exact payload boundaries were localized only to within a few KB.
6. Only fan/perf/posture-adjacent strings were curated; the ~2,250 ASCII runs in the
   image include more (battery FRU auth, Surflink/USB-C, VFG fuel-gauge, DPTF-ish
   `PerformanceDP` @0x92374) that may merit a second pass.

## 5. Quick-offset index

| Offset | What |
|---|---|
| 0x00 | container GUID `6dcbd5ed-…` |
| 0x24 | file size `0x19DDA5` |
| ~0x34 | FirmwareId GUID `632cbe1c-…` |
| 0x0FD–0x8xx | cert: `Microsoft Surface LFF Firmware CA 2021`, `Monzino Firmware Signer` |
| 0x5A57 | `Fan SSH Handler` |
| 0x7CDF–0x7D60 | `Posture State Async Publisher`, `ProLidEv`, `ProRepEv` |
| 0x681E | `HostRegistration Async Publisher` / `Host Registration SSH Handler` |
| 0x8740–0x8B00 | thermal component/sensor table (`Cmpnt fan`, `ThermTrip`, `U390x`, `BATT1_T`, …) |
| 0x8E13–0xA200 | perf-profile tables `Quiet…Kiosk × DENALI/TORRANCE × LCD/OLED` |
| 0xA9B6–0xAA1F | OID strings `1.3.6.1.4.1.311.76.9.x.y` |
| 0xAFBB | `BPM_*` battery-policy states |
| 0xCE97F | start of duplicated payload copy |
| 0x19DDA1-end | trailer pointers + dword `0x15967D9E` |
