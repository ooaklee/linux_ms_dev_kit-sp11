# Qualcomm USB-C UCSI + Surface UCM-UCSI HID client — hardware evidence report

Source binaries (Surface Pro 11 / Windows 11 26100, package 26.041.12746.0):

| File | Role | Version |
|---|---|---|
| `SurfaceUpdate/qcusbcucsi/qcusbcucsi8380.sys` | Qualcomm USB-C UCSI controller driver (ACPI `QCOM0CA4`, device `\_SB.UCS0`) | 06/02/2025, 1.0.4338.2200, ARM64 |
| `SurfaceUpdate/qcusbcucsi/UCS0.bin` | Qualcomm "AeoB" PM-arbiter resource table for `\_SB.UCS0` (PMIC/clock/bus votes) | shipped with driver |
| `SurfaceUpdate/ucmucsihidclient/SurfaceUcmUcsiHidClient.sys` | Microsoft Surface UCM-UCSI HID transport client (DMF-based) | 06/18/2024, 5.76.1169.0, ARM64 |

Analysis method: `llvm-objdump`/`llvm-readobj` disassembly + `strings`. REA MCP was not
available in this session, so all addresses below are raw PE section+offset addresses
(image base 0x140000000) from LLVM disassembly. Strings are treated strictly as data.

Confidence legend: **[observed]** = directly read from binary/data at cited address;
**[derived]** = concluded from disassembly structure at cited addresses; **[inferred]** =
reasonable conclusion requiring runtime confirmation.

---

## 1. UCSI transport specifics

### 1.1 ACPI binding (qcusbcucsi8380)

- INF matches `ACPI\QCOM0CA4` ("USB Type-C driver (HID_UCS0)") — **[observed]**,
  `qcusbcucsi8380.inf` `[QCOM.NTARM64...]` model section.
- ACPI method-name strings compiled into the driver **[observed, file offsets]**:
  - `0x2140`: `AeiBDPBM` — repeated `AeiB` (4-byte ACPI method) before `DPBM`
    (DisplayPort Bidirectional Mode?) hint: the driver evaluates an `AeiB`-style
    ACPI method per operation, and a DP/BookMode method `DPBM` exists.
  - `0x3754c`: `AeiBAeiCAeiIAeiS` — the driver contains the literal method-name
    array `AeiB`, `AeiC`, `AeiI`, `AeiS` (single-char suffix variants B/C/I/S).
  - `0x37110`: `ATMoAeiC_DSM` — an ACPI `_DSM` wrapper (`ATMo` = ACPI-target
    module op?) used with `AeiC`.
  - `0x7b00`: `DITM` (another 4-char ACPI method, likely "Device Init/Test Mode").
  - `QUCS` at `0x155c`, `0x214c`, `0x247c8`, `0x317e8`, `0x4c9ac`, `0x50110`
    (pool tag / DEVPKEY strings).
- The driver is a DMF (Driver Module Framework) client: imports only
  `WppRecorder.sys` + `ext-ms-win-ntos-werkernel-l1-1-1.dll` **[observed]**, uses
  `WdfCustomType_DMF_AcpiTarget` (string `0x2d950`) — i.e. UCSI registers are
  reached through ACPI OpRegion/AML (`AcpiTarget`), **not** memory-mapped registers
  directly **[derived]**.
- Context type names **[observed, strings]**: `USBC_DMF_DEVICE_CONTEXT` (0x2d970),
  `USBC_DRIVER_CONTEXT` (0x2d988), `USBC_DEVICE_CONTEXT` (0x2d9a0),
  `USBC_PORT_CONTEXT` (0x2d9b8), `USBC_UCSI_CONTEXT` (0x2e6d0). Port contexts are
  `0x48` bytes each: `madd x8, x19, x8(0x48), x21` at `0x1400238d4` **[observed]**,
  and up to 9 per-device sub-objects at `0x1400238e0–0x140023934` (`cmp w25, #0x9`)
  **[derived]**.

### 1.2 UCSI register/offset mapping

- Version handshake: `UcsiHidTransport` reads UCSI version and gates on
  `spec >= 0x26` (UCSI 2.0?) — `cmp w8, #0x26; b.ls` at `0x140020bc0` and
  `0x140020c8c` in qcusbcucsi8380 **[derived]** (offset `0x18` in the UCSI data
  block context holds the version, `ldr w8, [x8, #0x18]`).
- UCSI CCG-style polling: at `0x1400234c8` the driver masks a status byte with
  `0x7f` (`ands w8, w8, #0x7f`) and counts consecutive clears with
  `ubfx w8, w8, #1, #8; cbnz` loop; bail-out after `cmp w19, #0x3`
  (`0x140023504`) — **[derived]** a 3-iteration busy-poll of the UCSI
 CCI/status byte before falling back to a timed wait.
- Message-in length handling reads a 16-bit little-endian length at offset `+7`
  (`ldrb [x9,#0x7]`/`ldrb [x9,#0x8]` at `0x140023658-0x14002366c`) — **[derived]**
  UCSI MESSAGE_IN payload length field.
- Special 16-bit comparisons on a polled value at `0x1400236bc–0x1400236d4`:
  `cmp w6, #0x8087` and `cmp w6, #0xff00` — **[inferred]** these look like
  controller-specific status/version magic (0x8087, 0xFF00) used to detect
  controller presence/firmware mode during init.
- A base+0x647 byte array indexed by port (`ldrb w8, [x9, #0x647]`, `0x140023890`)
  acts as per-port "pending/changed" flags **[inferred]**.

### 1.3 HID client transport (SurfaceUcmUcsiHidClient)

- WPP trace strings **[observed, file offsets]**: `UcsiHidTransport_InputReportCompletionCallback.HID_REPORT_ID_UCSI`
  (0x18258) — confirms a dedicated **UCSI HID report ID**; `BufferQueueFeatureReportUcsi`
  (0x39fc8); `DMF_UcsiHidTransport_ConnectorIdGet` (0x3a018);
  `DMF_UcsiHidTransport_IsUSBDeviceControllerSupported` (0x3a088);
  `DMF_UcsiHidTransport_NumberOfConnectorsGet` (0x3a108);
  `UcsiHidTransport_UcsiDataReceive.DMF_HidTarget_FeatureGet` (0x3a1b0) —
  **UCSI reads use HID Feature reports (GET_FEATURE/SET_FEATURE on report ID
  `HID_REPORT_ID_UCSI`), input reports for asynchronous notifications** **[derived]**.
- No embedded HID report descriptor was found in either .sys **[observed]** —
  the descriptor lives in the UCS0 ACPI device / controller firmware, not the driver.
- The client is the upper filter over the HID stack; qcusbcucsi sits below ACPI
  (`\_SB.UCS0`), exporting a HID interface (INF remark `HID_UCS0`) **[derived]**.

---

## 2. Reset / recovery sequences with constants

### 2.1 Controller reset & re-init state machine (qcusbcucsi8380, PAGE:0x21088 ff.)

A function at VA `0x140021088` performs the recovery sequence (all **[derived]**
from instruction addresses cited):

1. Optional precondition / DSM call (`bl 0x140001030`, `0x14002113c`).
2. Read-modify op w/ `w2=1` via helper `0x140020aa0` (`0x140021178`) —
   **UCSI command step 1** (likely PPM_RESET prep or CCI clear).
3. Same helper with `w2=2` (`0x1400211b4`) — **command step 2**.
4. Helper with `w2=1` again (`0x1400211f0`), then **`w2=4`** (`0x140021238`) —
   step 4 is distinctive: the value `4` matches a UCSI **PPM_RESET / connector
   reset command code** argument.
5. Each step checks `cmp w19, #0x0` and aborts to a common failure path
   (`0x140021424`) on error — i.e. **no per-step retry**, fail-fast **[derived]**.

The helper `0x140020aa0(x0=ctx, w1=port, w2=cmd, x3=out)` itself:

- Sends a command via a function-table call (`0x140020c1c`, `0x140020cd4` —
  IAT/CFG-guarded indirect calls through `0x140031000+0xb18` table, offsets
  `0x650`, `0x608`, `0x7b8`).
- On nonzero return it logs WPP line `#0x2b` (43) with level 2 and beeps the
  same fail path — the caller above is expected to handle.

### 2.2 Full bring-up with delays (VA `0x1400214xx–0x1400218xx`)

Sequence with explicit sleeps (via `0x140002720` = delay wrapper) **[observed constants]**:

| Address | Delay (ms) | Context (WPP line) |
|---|---|---|
| `0x1400215d4` | 1000 | line 74/75 after cmd w2=5 |
| `0x140021630` | 1000 | line 75 |
| `0x14002179c` | **15000** | line 79/80 after cmd w2=5 (2nd phase) |
| `0x1400217f8` | **60000** | line 80 — post-reset settle |
| `0x140021818` | **30000** | after op with w3=2 (line 80/81) |

So the Windows driver tolerates up to **60 s** for the controller to come back
after a reset command, with 1 s→15 s→60 s→30 s stepping **[derived]**. WPP trace
line numbers embedded as `mov w3, #imm` (37–163) confirm this is one long
recovery routine in `Dmf_UcsiHidTransport`-adjacent code.

### 2.3 Per-port teardown/reset (VA `0x1400238xx–0x140023970`)

- Clears per-port bit in a bitmap at base+1 (`bic w8, w8, w9; strb [x10,#0x1]`,
  `0x140023880-0x14002388c`) **[observed]**.
- Checks the base+0x647 "pending" flag; if the port context field at +0x260 is 0
  and flag byte is 1, runs per-port worker loop bounded by 9 (`cmp w25, #0x9`,
  `0x140023930`) **[derived]**.

### 2.4 Recovery constant `STATUS_UNSUCCESSFUL` retry (HID client)

At `SurfaceUcmUcsiHidClient` VA `0x14002d2xx–0x14002d4e8` **[derived]**:

- A constant `0xC0000035`? No — the pooled constant at `0x14002d540` reads
  **`0xC0000035` is actually `STATUS_OBJECT_NAME_COLLISION`; the loaded dword is
  `0xC0000035`** — see correction below.
- Actual code: `ldr w26, 0x14002d540` loads **`0xC0000035`**… wait, dump shows
  `0xc0000035` at file offset 0x2a940 mapping to VA 0x14002d540. `STATUS_OBJECT_NAME_COLLISION`
  does not fit UCSI retry semantics; but NTSTATUS `0xC0000035` is exactly
  `STATUS_OBJECT_NAME_COLLISION`. Given the loop compares an NTSTATUS from a
  DMF bridge call against this constant and retries only when equal, the more
  plausible reading is that the byte order shows `0xC0000035` =
  **`STATUS_OBJECT_NAME_COLLISION`** used as "previous instance still registered"
  during interface re-registration **[inferred]**.
- Retry loop: `w21` counts attempts; `cmp w21, #0x4; b.eq fail` at `0x14002d3e4` —
  **4 retries**, 1000 ms apart (`mov w0, #0x3e8; bl 0x14000de48` at
  `0x14002d418`, where `0x14000de48` = `KeDelayExecutionThread`-based sleep
  wrapper: `-10000 * ms` 100-ns units, `0x14000dea0-0x14000deb4`) **[observed]**.

### 2.5 PMIC / QMP interaction hints (UCS0.bin resource table)

UCS0.bin is a Qualcomm "AeoB"-magic (`0x41656f42`, `0x14a9 6294` version/len header)
ACPI-loaded **resource vote table** for `\_SB.UCS0` — i.e. what the Windows PPM
arbitrates when UCSI commands arrive **[observed]**:

- Device scope: `DEVICE \_SB.UCS0` at file offset `0x14`.
- `COMPONENT`/`FSTATE` entries mapping power states to resource sets.
- **PMIC LDO votes** (`PMICVREGVOTE` + `PPP_RESOURCE_ID_*`): LDO1_C/D/I/J,
  LDO2_B/C/D/J, LDO3_D/J, LDO8_B, LDO14_B, LDO15_B, CLK3_A, CLK6_A
  (offsets `0x68–0x61d5`).
- **GCC clocks** (CLOCK entries): `gcc_usb30_prim/sec_{sleep,master,mock_utmi}_clk`,
  `gcc_usb3_prim/sec_phy_{pipe,aux,com_aux}_clk`, `gcc_aggre_usb3_prim/sec_axi_clk`,
  `gcc_cfg_noc_usb3_prim/sec_axi_clk`, `gcc_aggre_noc_usb_{north,south}_axi_clk`,
  `gcc_cfg_noc_usb_anoc_{,north_,south_}ahb_clk`, `gcc_sys_noc_usb_axi_clk`,
  `tcsr_usb{2,4}_{0,1}_clkref_en`.
- **GDSC footswitches**: `gcc_usb30_prim_gdsc`, `gcc_usb30_sec_gdsc`,
  `gcc_usb_0_phy_gdsc`, `gcc_usb_1_phy_gdsc`.
- **NoC/ICB bus arbitration**: `ICBID_MASTER_USB3_0/1 ↔ ICBID_SLAVE_EBI1`,
  `ICBID_MASTER_APPSS_PROC ↔ ICBID_SLAVE_USB3_0/1`.
- **Rail cornering**: `NPARESOURCE /arc/client/rail_cx`.
- **GPIO sequencing**: `PMICGPIO` + `IOCTL_PM_GPIO_CONFIG_DIGITAL_OUTPUT`
  and `TLMMGPIO` entries each followed by `DELAY` records; the DELAY payload
  block after each GPIO op is `03 00 0c 00 00 00 08 00 <u32 value>` —
  decode: type 3, size 0x0c, value tag `08 00` (µs units), then a u32:
  **10 µs (`0x0a`)** or **1 µs (`0x01`)** or e.g. `0x0165` (357), `0x0269` (617),
  `0x0832` (2098), `0x0750` (1872), `0x02e1`, `0x0700`… (mixed units — values
  like 0x0165/0x0832 are likely µs delays in the PHY reset pulse train)
  **[observed payload bytes; derived unit; inferred exact semantics]**.
  GPIO toggle pattern: (configure PMIC GPIO → delay 10 µs) xN with value flips
  10→10→1 (or 10→…→1) then longer settling delays — consistent with a
  **QMP PHY reset pulse** driven from the PMIC plus TLMM pins **[inferred]**.

Relevance to Linux: the recovery overlay should, after failed UCSI PPM_RESET,
perform the analogous re-vote sequence — disable/enable `gcc_usb*_phy_gdsc`,
`usb3_prim_phy_{aux,com_aux,pipe}_clk`, USB30 GDSC, and the `tcsr_usb*_clkref_en`
clocks before re-initializing UCSI — mirroring FSTATE transitions in UCS0.bin
**[inferred]**.

---

## 3. Timeouts / retry counts (summary table)

| Constant | Where | Meaning | Confidence |
|---|---|---|---|
| 3 iterations | qcusbcucsi `0x140023504` (`cmp w19,#3`) | CCI busy-poll retries before timed wait | derived |
| 4 retries × 1000 ms | HID client `0x14002d3e4`/`0x14002d418` | Recovery retry loop w/ 1 s interval | observed |
| 1000 ms | qcusbcucsi `0x1400215d4`, `0x140021630`, `0x140021a04`, `0x140021b1c` | inter-step settle in reset sequence | observed |
| 15000 ms | qcusbcucsi `0x14002179c` | post-command wait (phase 2) | observed |
| 30000 ms | qcusbcucsi `0x140021818` | post-reset wait | observed |
| 60000 ms | qcusbcucsi `0x1400217f8` | max controller reset settle | observed |
| 100 (units) | qcusbcucsi `0x140008f88` etc., HID client `0x14000d198` etc. | WPP/recorder or 100 ms-class delays (context-dependent) | inferred |
| 10 µs / 1 µs / ~0.36–2.1 ms DELAY records | UCS0.bin offsets listed in §2.5 | PMIC/TLMM GPIO reset pulse timing | observed bytes / derived unit |
| `0x8087`, `0xff00` | qcusbcucsi `0x1400236c0`/`0x1400236cc` | controller status magic comparisons | observed compare / inferred meaning |
| `>= 0x26` version gate | qcusbcucsi `0x140020bc0` | UCSI version check (2.0.x?) | derived |
| 9 sub-objects/port loop bound | qcusbcucsi `0x140023930` | per-port recovery iteration bound | derived |

---

## 4. Strings of interest

**qcusbcucsi8380.sys** (file offsets) — all data, not instructions:

- `0x2e918`: `Z:\b\WP\QcUsbCUcsi\rel\10.9\ARM64\Release\qcusbcucsi8380.pdb` (build path)
- `0x2e6d0`: `USBC_UCSI_CONTEXT`
- `0x37110`: `ATMoAeiC_DSM`; `0x3754c`: `AeiB AeiC AeiI AeiS`; `0x2140`: `AeiB DPBM`; `0x7b00`: `DITM`
- `0x2d950`: `WdfCustomType_DMF_AcpiTarget`
- Imports: `WppRecorder.sys`, `ext-ms-win-ntos-werkernel-l1-1-1.dll`, `HAL.dll`, `WDFLDR.SYS`
- PoFx APIs present (`PoFxRegisterDevice`, `PoFxStartDevicePowerManagement`, …) —
  runtime power delegation to PoFx **[observed import strings]**.

**SurfaceUcmUcsiHidClient.sys** (file offsets):

- `0x19f18`: `SurfaceUcmUcsiHidClient.pdb`
- `0x18210`: `REPOSITORYDIR\Kernel\DMF\Modules.Surface\Dmf_UcsiHidTransport.c`
- `0x18258`: `UcsiHidTransport_InputReportCompletionCallback.HID_REPORT_ID_UCSI`
- `0x3a1b0`: `UcsiHidTransport_UcsiDataReceive.DMF_HidTarget_FeatureGet`
- `0x3a018`/`0x3a088`/`0x3a108`: `DMF_UcsiHidTransport_{ConnectorIdGet,IsUSBDeviceControllerSupported,NumberOfConnectorsGet}`
- `0x39c28`: `UcmUcsiHidClient`, `0x39c40`: `UcmUcsiClient`
- Live-kernel-dump telemetry strings (`LKMDTEL: …`) at `0x39810–0x39b08`.

**UCS0.bin**: see §2.5 for the full resource-name list (PMIC LDO/CLK votes, GCC
clocks, GDSCs, ICB bus votes, PMIC/TLMM GPIO + DELAY sequencing).

---

## 5. Limitations

1. **No REA MCP**: the `rea` tool namespace was not registered in this session;
   analysis used `llvm-objdump`/`llvm-readobj`/`strings` instead. Addresses are
   image-base-relative VAs and raw file offsets; symbols are absent (PDB not
   shipped), so function names are structural descriptions only.
2. **No decompiler**: control-flow conclusions come from manual reading of the
   disassembly; the mapping of helper functions (e.g. `0x140020aa0` = "send UCSI
   command with args") is derived, not symbolic.
3. **WPP-encoded**: most trace messages are compiled WPP TMF strings; only the
   static portions (function.line tags like `#0x2b`, line numbers 37–163) are
   visible. Exact message text requires the TMF from the matching PDB
   (`qcusbcucsi8380.pdb` rel 10.9), which is not in the package.
4. **UCSI command opcodes are not plainly visible**: command codes travel as
   `w2` arguments (values 1, 2, 4, 5, 6 observed); the definitive opcode→step
   mapping needs the PDB or a decompiler pass.
5. **HID report descriptor**: not embedded in either driver; report ID constant
   value (`HID_REPORT_ID_UCSI`) is a named constant whose numeric value was not
   recovered. It should be dumped from the live `\_SB.UCS0` HID device on
   Windows (e.g. via ` HidD_GetPreparsedData`) for the Linux overlay.
6. **UCS0.bin DELAY units**: the `08 00` unit tag is assumed microseconds from
   Qualcomm NPA/QMP conventions; absolute values (0x0a=10, 0x1=1, 0x832=2098…)
   are observed but their unit scaling (µs vs 31.25 µs ticks) is inferred.
7. `0xC0000035` at HID-client `0x14002d540`: read as
   `STATUS_OBJECT_NAME_COLLISION`-style constant gating the 4×1 s retry loop;
   exact semantic (interface re-registration collision vs. UCSI error alias)
   is inferred from context only.

— Generated by reverse-engineering subagent, 2026-11-12. No repository sources
were modified.
