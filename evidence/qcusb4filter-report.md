# QcUsb4Filter8380.sys — reverse-engineering report

- **Binary**: `/Users/leon/Downloads/SurfacePro11_ARM_Win11_26100_26.041.12746.0/SurfaceUpdate/qcusb4filter/QcUsb4Filter8380.sys`
- **PE32+ AArch64 (coff-arm64)**, NT native subsystem, ImageBase `0x140000000`, SizeOfImage `0x80000`
- **File version** 1.0.4196.6900, link stamp **2024-12-17**, WPP build stamp `Nov 24 2024 17:03:31`
- **PDB path**: `Z:\b\WP\QcUsb4Filter\rel\10.9\ARM64\Release\QcUsb4Filter8380.pdb`; DMF sources under `X:\qcdksrc\rel\10.9\b\src\dmf\`
- Sections: `.text` VA 0x140001000 (0x43D1C), `.rdata` 0x140045000, `.data` 0x14004C000, `.pdata` 0x140058000, `PAGE` 0x14005B000, `INIT` 0x14007D000
- Analysis tooling: the REA MCP server was **not registered** in this session, so analysis was performed with `objdump` (LLVM 21, coff-arm64) + Python decoding of raw instruction words. All virtual addresses below are file-relative VAs (ImageBase 0x140000000).
- Confidence legend: **[observed]** = directly seen in disassembly/strings; **[derived]** = concluded from observed code/data; **[inferred]** = plausible but not proven.

## Driver role

**[observed]** The INF (`QcUsb4Filter8380.inf`, UTF-16) declares:

- Class `Extension`, `ExtensionId {fa8925f0-7e06-4544-88cb-11c9fc2dc21d}`, lower device filter:
  `AddFilter=QcUsb4Filter,,LowerFilterInstall` with `FilterPosition=Lower`
- Hardware ID: `USB4\QCOM0CD10001` — commented in the INF as *"USB4 host router (HID)"*; FriendlyName *"Qualcomm(R) USB4(TM) Host Router"*.
- This is the same device ID the **qcusb4bus** driver enumerates as child PDOs — i.e. QcUsb4Filter sits **below/around the in-box Windows USB4 host-router stack (USB4\... devnode), as a lower filter** on the Qualcomm host-router node, not on PCIe/USB. **[derived]**

**[observed]** The driver is a KMDF+DMF (Driver Module Framework) driver: strings `WdfCustomType_DMF_IoctlHandler/RingBuffer/ComponentFirmwareUpdate/...`, DMF sources `Dmf_IoctlHandler.c`, `Dmf_RingBuffer.c`, `Dmf_ContinuousRequestTarget.c`, etc. It embeds a full **Thunderbolt/USB4 link-training and sideband engine** (function names from WPP strings):

- Link manager (`lnk_*`): `lnk_init`, `lnk_phy_standby`, `lnk_activate_transmitter`, `lnk_activate_receiver`, hot-state-equalization phases `lnk_hse_p1` … `lnk_hse_p5_usb4`, `lnk_hse_p5_tbt3`, `lnk_dsr_hse`, `lnk_cancel_hse`, `lnk_port_disconnect`, `lnk_sleep_stage1`, `lnk_sleep_stage2`, `lnk_wake`, `lnk_update_lane_disable`, ISR callbacks `lnk_isr_cb_llsm_lane`, `lnk_isr_cb_llsm_global`, `lnk_isr_cb_pm`.
- Connection-manager sideband channel (`cp_*` / `sb_*`): `cp_sideband_read/write`, `cp_inject_tx/rx_packet`, `cp_send_response_to_cm`, `cp_send_notification_to_cm`, `cp_send_hot_plug`, `cp_drom_check_external`, `sb_rx_handle_at_rt_cmd` (AT/RT = adapter/Router-protocol transactions), `sb_regh_port_op`, `sb_escape_message`, `sb_tx_alloc_slot/commit_slot`, `sb_reset_regs`, `sb_register_reg_update_callback`.
- DP/PCIe tunneling helpers: `dp_send_cmd`, `dp_isr_cb`, `dp_handle_driver_cmd`, `dp_wait_rst_ack`, "CS LCK. New Estimated BW", "Commit USB BWA CS2", "PERST timeout", "DPSW_CMD[%x]".
- USB3 workaround knobs: `cio_wa_usb_ap_wr_cs0/cs2`, `usb3_warmup_knob_check_lfps_packet`.

**[derived] Role summary**: the Qualcomm USB4 host-router *function driver disguised as a filter*. Windows' in-box USB4 stack (the CM/router driver) handles topology and CM-protocol policy; QcUsb4Filter owns the **hardware door into the router**: it maps the router MMIO BAR, programs PHY/link training (HSE, lane bonding, CLx), services hardware interrupts (per-lane LLSM, global, PM, mailbox, NMI, TX/RX rings), and shuttles CM sideband packets between the inbox stack and the router's native sideband mailbox rings. For Linux PR #76 this is the strongest single source of truth for host-router frontend behavior on X1E80100.

## Register map observations

**[observed]** Raw MMIO accessors (thunks with `dsb sy` barriers, pointer arg x0 = base, w1 = offset):

| VA | Function |
|---|---|
| 0x140001050 | raw `write32(void* addr, u32 val)` |
| 0x140001090 | raw `read32(void* addr)` |
| 0x1400012E0 | `reg_read(base, off)` — `dsb sy` then read32 |
| 0x140001338 | `reg_write(base, off, val)` — `dsb sy` then write32 |
| 0x140001388 | `reg_read-modify-write(base, off, set, clear)` |

**[observed]** MMIO mapping: `MmMapIoSpaceEx` IAT slot `0x140045188`. Two call sites:

1. **0x14001854C / 0x1400594**: maps **0xB8 (184) bytes**, cache type `MmNonCached` (w2=2), from a `CM_PARTIAL_RESOURCE_DESCRIPTOR`-style structure — this is the **USB4 host-router MMIO region** (small register file, one page max). After mapping, the code reads `[map+0x9C]`, `[map+0xA4]` words (descriptor len fields) — actually this site parses a serialized resource blob: first a 0xB8-byte map, then re-maps `(len computed from fields + 0x2E)*4` bytes at 0x1400185A4 — i.e. a **variable-length, word-indexed register window** (see alignment semantics below).
2. **0x14005C84C** (PAGE section): maps `x1 = *(u32*)(ctx+0x18)`, size **0x204 (516) bytes**, `MmNonCached`, storing result at ctx+0x10. 0x204 = 0x200 window + 4 — consistent with a 128×32-bit register array plus a control word. This is the runtime **router register-access window** used by the reg_read/write/rmw helpers via ctx+0x10.

**[observed]** Constant register offsets fed to the helpers (call-site VAs; `reg_rmw` = read-modify-write with set/clear masks):

| Offset | Access | Sites (VA) | Notes |
|---|---|---|---|
| 0x104 | rmw, write | 0x14000B89C, 0x1400140D4, 0x140015530 | |
| 0x698 / 0x7A4 / 0x7A8 / 0x900 / 0xC98 / 0xDA4 / 0xDA8 / 0xF00 | write | 0x140002464, 0x140002D0C, 0x140002D3C, 0x14000DBDC, 0x140002798, 0x140002D3C, 0x140002D24, 0x14000DC04 | bring-up/PHY block (0x14000DC00 region near `lnk_phy_standby` log data) |
| 0x9A0 / 0x9AC | read | 0x140002660, 0x140002B3C, 0x140002B70, 0x140002F0C | 0x9AC read masked with 0x80 (bit7 status) |
| 0xFA0 / 0xFAC | read | 0x140002690, 0x140002BA4, 0x140002BD8, 0x140002F74 | mirrors of 0x9Ax — second lane/port copy (0x600 apart) |
| 0x1400 | write | 0x1400038C0 | |
| 0x2050 | read | 0x14000C938 | read, bits 26-29 (`& 0x3C000000 >> 26`) compared to 2 — **version/revision ID check** (tied to "HW version mismatch: %p vs %p") |
| 0x20A4 | read | 0x14000C430 | |
| 0x20A8 | read + rmw ×5 | 0x14000B6D8/0x14000B700/0x14000B728, 0x14000C2C8/0x14000C2EC/0x14000C310 | bit 0x80000 polled (link/PHY status, `RXSTANDBY_STATUS`-class polling) |
| 0x318C | read + write | 0x140002AA8, 0x140002CDC | |
| 0x3400 (×5) / 0x3414 (×3) / 0x341C / 0x3444 | write/read | 0x140002494, 0x1400020A0, 0x140002224, 0x140003C4C | 0x3400-page = link/CS block (near "CS2 UPD[%x] %p=%p", "Reset SB. L0_CS19=%p" — **L0_CS19 ⇒ offset page 0x34xx is the Lane-0 CS-space**) |
| 0x35A8 | rmw | 0x140003C04 | |
| 0x8018 / 0x801C / 0x8020 | rmw / write | 0x140005E90, 0x140006238, 0x1400175F4, 0x140017614 | |
| 0x80C8 | rmw ×2 (masks 0xFFFF0000 set, then 0xFFFF) | 0x140013EE8, 0x140013F0C | 16-bit field programming |
| 0x8144 / 0x8148 | rmw (mask -1, i.e. full write via rmw) | 0x140013F54, 0x140013F30 | **mailbox command/data registers** (adjacent to `MBOX[%x] cmd %x` string at .data 0x140056F04) |
| 0xD000 | rmw ×7 | 0x140002840, 0x14000C200, 0x14000C21C | site 0x140002840 passes set=0x7800, clr=0x4000 — **interrupt enable/status** block (near `NMI: IRQ_EN=%p` string, .data 0x140056DD1) |
| 0xD064 | rmw | 0x140005E74 | |

**[derived] Ring/interrupt setup**: ISR entry names observed in strings: `cp_isr_cb_tx`, `cp_isr_cb_rx`, `cp_isr_cb_rx_avail`, `sb_isr_cb_tx`, `sb_isr_cb_tx_in`, `sb_isr_cb_rx`, `main_isr_cb_sw_irq`, `isr_cb_nmi`, `mbox_isr_cb`, `dp_isr_cb`, `lnk_isr_cb_*`. Ring bookkeeping strings: `cpTxRing overflow`, `cpRxRing overflow`, `cpRetry.ring overflow`, `sPhyCmdRing overflow`, `CMDQ %x RXCNT %x`, `Retry is empty. RX ring first=%x last=%x`, `sb_tx_alloc_slot`, `sb_tx_commit_slot`. So the hardware exposes at least: TX ring, RX ring, RX-avail ring, retry ring, PHY command ring, mailbox, SW-IRQ, NMI. The 0x8144/0x8148 mailbox pair and the 0xD000 interrupt block above are the observed programming sites.

**[derived] MMIO mapping/alignment semantics (PR blocker #1)**: the two mapping sites show the driver does **not** map a large PCIe BAR. It maps (a) a fixed 0x204-byte register window and (b) a descriptor-derived, word-indexed window sized `(N*4)` — accesses are 32-bit only, every helper issues `dsb sy` before the access (release semantics before read/write), and offsets are byte offsets into the mapped virtual address (all observed offsets are 4-aligned). The offset space (0x9A0/0xFA0 lane mirrors; 0x34xx lane-CS page; 0xD0xx IRQ page; 0x8144 mbox) is consistent with a **flat 32-bit register file in a single BAR**, mirrored per lane at 0x600 stride.

## Firmware loading

**[observed]** The log `Starting USB4 FW ver: %x.%x.%x (%s boot) [Nov 24 2024 17:03:31]` (string @ .data 0x140056365) is a *status* print: the filter **reports** running firmware version, including a `(%s boot)` cold/warm boot indicator ("USB4"/"TBT3" strings appear near HSE paths).

**[observed]** There is **no firmware file I/O path**: no `.bin/.mdt/.elf/.bNN` file name strings, no `ZwCreateFile`/`ZwReadFile` imports (only `ZwWriteFile` — used by the LiveKernelDump/telemetry module `Dmf_LiveKernelDump.c`), no PL PIL/PIL-service interaction, no `QSEE`/`mbn` strings. Imports touching config are only `ZwOpenKey`/`ZwQueryValueKey` (IAT 0x1400451A0/0x1400451A8) and the registry paths found are telemetry (`...\Control\LKMDTel`), knobs (`PhyPresetValue`, `Usb4PCIeInterruptFlag`, `OSDefaultUsb4PCIeInterruptFlag`, `dbgNoShutdownOnTA` under `\REGISTRY\MACHINE\SYSTEM\ControlSet001\Control\USB\` and ACPI `QCOM0C8B/QCOM0C8C/QCOM0D07` Device Parameters), plus a SpbResourceHub `\Device\RESOURCE_HUB\` path (HID transport fallback for `WdfCustomType_DMF_ComponentFirmwareUpdateHidTransport`).

**[derived]** Conclusion: **the router firmware is not loaded by this driver**. Firmware is brought up before this driver runs (SoC firmware/TZ or the USB4 devnode's own firmware device, with the DMF ComponentFirmwareUpdate HID-transport custom type present but unused on this platform). For Linux PR #76: firmware loading must be resolved outside this driver's scope; Linux needs whatever the platform provides (likely none — the router firmware appears to be resident in the X1E SoC's own boot chain, matching the `(%s boot)` reporting-only pattern).

## Power / stop-restart state machine

**[observed]** Strings reveal the state machine vocabulary:

- PHY power: `lnk_phy_standby` — `PowerDown=%x CLK=%x` (string @ .data 0x140054DBA); `Activate TX %1 %1` / `Activate RX. RXSTANDBY %x -> %x`; `RXSTANDBY_STATUS timeout`; `RXFSM_WARM_RESET caught`; `RXTERM`.
- Sleep/wake: `lnk_sleep_stage1`, `lnk_sleep_stage2`, `lnk_wake`, `Drive SBTX low 1/2`, `In middle of sleep - cease HSE`, `We're in middle of Sleep - abandon disconnect`, `CLx exit`, `PM error: %p` (`lnk_isr_cb_pm`).
- Disconnect/stop: `lnk_port_disconnect`, `Disconnect reason=%x FSM=%x LD0=%1 LD1=%1`, `lnk_cb_sb_disconnect`, `CS/SB registers are reset. Disconnect completed`, `waiting for SBRX disconnect timed out`, `dbgNoShutdownOnTA is set. Cease disconnect`, `Reset SB. L0_CS19=%p` (`sb_reset_regs`).
- Restart/DPR: `lnk_dsr_dpr` — `Begin DPR. State %x`, `Waiting for DPR clear`; `Router ready. Begin DSR dispatch`; `RXFSM_WARM_RESET caught`.
- Interrupt gating at stop: the `0xD000` rmw cluster (set 0x7800 / clear 0x4000 observed at 0x140002840) is performed with IRQ_EN reporting (`NMI: IRQ_EN=%p`).

**[derived] Stop/restart ownership (PR blocker #3)**: Windows splits responsibility — the inbox USB4 stack (above the filter) issues router-level operations via AT/RT commands (`sb_rx_handle_at_rt_cmd`, `Enumerated R/W` / `Non-enumerated R/W`), while this filter owns *link-level* stop/restart: on disconnect it drives sideband low (`Drive SBTX low`), puts PHY in standby via the `PowerDown/CLK` register pair (0xF00/0x900 writes region), resets the SB register file, then reports `CS/SB registers are reset. Disconnect completed`. Warm restart is DSR-dispatch based (`Router ready. Begin DSR dispatch`) with a DPR (downstream-port-reset?) handshake. The `(%s boot)` warm/cold distinction and `RXFSM_WARM_RESET` show the hardware supports warm re-init without full power cycle. **Tunnel-clock note**: no separate tunnel-clock register block was isolated; clock control is folded into the `PowerDown=%x CLK=%x` PHY-standby programming (function `lnk_phy_standby`) — the same register governs power-down and clock gating. **[inferred]** given the log prints both fields from one call site.

**[observed]** KMDF power plumbing exists as expected for a filter (IRP_MJ_POWER handled by KMDF; no custom power IRP construction observed in disassembly — all power sequencing happens in these `lnk_*` internal FSM callbacks triggered by interrupts/timers, not in PO IRP dispatchers).

## Strings/constants of interest

- `Starting USB4 FW ver: %x.%x.%x (%s boot) [Nov 24 2024 17:03:31]` @ .data 0x140056365
- `PowerDown=%x CLK=%x` @ 0x140054DBA; `Activate TX/RX` @ 0x140054DDE+
- `NMI: IRQ_EN=%p` @ 0x140056DD1; `MBOX[%x] cmd %x` @ 0x140056F04
- `Invalid register %x` @ 0x14005687A (register-index validation in `cp_handle_read_write`)
- `HW version mismatch: %p vs %p` @ 0x140055888 (pairs with read of 0x2050, bits 26–29)
- `Attempt to register callback for LT %x` @ 0x14005683B; `Register AT/RT listener`; `Register SBRX_CONNECT callback`
- `CMDQ %x RXCNT %x` @ 0x140056117; `sPhyCmdRing overflow` @ 0x14005594A
- HSE phase strings p1–p5 @ 0x140054E00–0x140055200 region
- Registry knobs: `PhyPresetValue`, `Usb4PCIeInterruptFlag`, `OSDefaultUsb4PCIeInterruptFlag`, `dbgNoShutdownOnTA` (ANSI, service-params style)
- Registry paths: `\REGISTRY\MACHINE\SYSTEM\ControlSet001\Control\USB\`, ACPI `QCOM0C8B/QCOM0C8C/QCOM0D07` Device Parameters, `\Registry\Machine\SYSTEM\CurrentControlSet\Control\LKMDTel` (telemetry)
- INF: lower filter on `USB4\QCOM0CD10001`, `HKR,Parameters,BootFlags = CM_SERVICE_USB_DISK_BOOT_LOAD (4)`, `VerboseOn=1` (WPP IFR verbose)
- Build path: `Z:\b\WP\QcUsb4Filter\rel\10.9\ARM64\Release\`

No prompt-injection attempts were found in the binary's strings; all strings were treated as data.

## Limitations

1. **REA MCP unavailable**: the `rea` namespace was not registered in this session, so no decompiler (pseudo-C) output was produced; analysis used objdump disassembly + string cross-referencing. Function boundaries were derived from `.pdata`-adjacent prologues (`pacibsp`); names like `lnk_*`/`cp_*` are **WPP log strings**, not symbol names — actual functions are unnamed.
2. WPP trace strings live in `.data` WPP-encoded structures; I located their addresses but did not decode the full WPP trace GUID-to-function mapping, so per-string → exact-function bindings (other than through accessor call sites) are **derived** from adjacency and call-site correlation.
3. Register-offset → semantic-name assignments for the 0x104/0x698/0x3400/etc. clusters are **derived/inferred** from nearby log-string parameters (e.g. `L0_CS19` ↔ 0x3414 page); no datasheet was available to confirm.
4. The 0x140018534 mapping site parses a serialized resource descriptor (words at +0x9C/+0xA4); the exact producer of that blob (ACPI _CRS vs. inbox USB4 driver handshake) was not traced.
5. Ring element sizes/depths are not recoverable from static strings alone; only ring names and overflow paths were observed.
6. `QcUsb4Bus8380.sys` was not re-analyzed here (prior session established it as a pure enumeration bus driver); cross-references to `USB4\QCOM0CD10001` in the INF corroborate the PDO/filter relationship.
