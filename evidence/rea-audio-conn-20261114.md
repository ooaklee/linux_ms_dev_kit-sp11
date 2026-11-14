# REA audio/connectivity evidence — SurfaceUpdate SP11 payload analysis (2026-11-14)

Pipeline: REA 6.9.0 + Ghidra 12.1.4 (`rea analyze` / `rea search <path> <pat> --provider ghidra --format json`).
Raw JSON persisted to `/tmp/rea-audio-<name>.json` for: qcaudminiport, qcbluetooth, ihvaudiofilter,
qcbtacx, mbbfwupdate, penble, qcslimbus. Source package:
`SurfacePro11_ARM_Win11_26100_26.041.12746.0/SurfaceUpdate/`.

> Tooling note: `rea analyze` returns only inventory summaries (segments/procedures/strings counts);
> all substantive findings below come from `rea search` string hits (address-anchored Ghidra defined-data
> inventory) plus INF cross-referencing. `rea function`/`decompile` could not resolve internal (unnamed)
> procedures — the binaries are stripped of symbols except RTLP strings.

## 1. Audio — codec identity and the D1-vs-D3 idle question

### Codec identity: Qualcomm Aqstic WCD on-die codec, NOT Realtek, NOT standalone SoundWire

- `qcaudminiport_Base8380.inf`: WDM audio adapter (portcls WaveRT) for **Qualcomm SoC**, hardware ID
  `AUCD\QCOM0C29` (AUCD = AUdio CoDec bus; QCOM dev 0x0C29 = X1E audio codec device).
- `SurfaceIhvAudioFilterDriver.inf` matches `AUCD\VEN_QCOM&DEV_0C29&SUBSYS_MSHW0462/0486/0542...`
  (Surface subsystem IDs) and `VEN_QCOM&DEV_0629&SUBSYS_MSHW0276` (older).
- `qcslimbus8380.sys` (446 KB, also part of the audio stack) exposes `\Device\Slimbus`, `\Device\Slimbus2`
  and is a **QMI/IPCRouter client** (`ipc_router_core_*`, `qmi_cci_*`, service-id open per client) —
  i.e. the codec control plane on this platform talks to the audio DSP over Qualcomm IPC
  (glink/ipc-router + QMI), not raw I2C register pokes from the miniport.
- `qcaudminiport8380.sys` (703 KB, portcls-based, PDB `Z:\b\WP\AudioMiniport\rel\10.9\ARM64\Release\qcaudminiport8380.pdb`)
  contains: `CMiniportWaveRT::HandleDetectionCallBack`, `CKeywordDetector::SetArmed` (voice
  activation), `RegisterJackPresenceWorkItemContext`, `SpkrProtCalibProcWorkItemContext`
  (speaker-protection calibration), `A2DP Offload Audio Filters Installed`, `BthHfp*` (HFP offload),
  `ExternalDisplayManagerWorkItemContext`, `Surprise Removal Shutdown Sequence Begin`,
  `Bootup Complete`, `Shutdown Successfully Completed`. No WCD/Soundwire/SWR register strings —
  hardware sequencing lives below this driver (ADSP firmware + qcslimbus/QMI).

### D1 idle vs D3 — ROOT CAUSE FOUND in the INF (driver-declared idle power state)

`qcaudminiport_Base8380.inf` writes these device PowerSettings under the audio FDO key:

```
HKR,PowerSettings,ConservationIdleTime,%REG_BINARY%,05,00,00,00   ; 5 s (conservation/battery)
HKR,PowerSettings,IdlePowerState,%REG_BINARY%,01,00,00,00         ; *** 1 = PowerDeviceD1 ***
HKR,PowerSettings,PerformanceIdleTime,%REG_BINARY%,05,00,00,00    ; 5 s (performance/AC)
```

This is the documented portcls/WDV "idle power management" contract: after 5 s of no active audio
streams (both on battery and AC), the audio FDO requests **D1**, not D3, for the codec device
(`AUCD\QCOM0C29`). The codec stays in a light sleep where the ADSP/codec link and jack-detect /
voice-activation (keyword detector) paths stay powered. **Implication for Linux**: the "idle D1 not
D3" behaviour is by Windows design — to replicate, the Linux ASoC card must keep the codec
runtime-PM in a shallow state while SVA (Sound Verbolizer/voice) armed or jack-detect enabled;
d3cold is only entered on removal/surprise-removal (`Surprise Removal Shutdown Sequence`) or full
shutdown. Note `audiominiext` sets `HKR,QCAUD\,DefaultSvaPowerType = 2` — SVA (keyword detect)
power profile 2 keeps the mic path alive at idle, consistent with D1.

### Panel/audio coordination

`audiominiext/surface_audiominiext8380.inf` (MSHW0462 rev 0A/0B, MSHW0486 rev 0C/0D, MSHW0542 rev 0E):

```
HKR,QCAUD\,DisableAcPowerNotifications,0x00010001,0x00000000     ; AC/DC change notifications ON
HKR,QCAUD\,DisableDisplayNotifications,0x00010001,0x00000000     ; display ON/OFF notifications ON
```

So the Windows audio stack explicitly subscribes to **display state notifications** (panel
on/off) and AC/DC transitions, and adjusts endpoints/power accordingly (e.g. speaker default
volume `HKR,QCAUD\WaveSpeaker,DefaultDeviceVolume,0xFFF40000` = -12.0 dBFS). The
`ExternalDisplayManagerWorkItemContext` in qcaudminiport8380.sys handles external-display audio
reroute. Endpoint inventory per SKU (HeadsetMicPresent/OnBoardMicPresent in ihvaudiofilter INF):
MSHW0462 = HeadsetMic yes / OnBoardMic no; MSHW0486 = both no; MSHW0542 = HeadsetMic yes.

### I2C/I2S/SoundWire setup

- No evidence of SoundWire (SWR) or I2C sequences in the miniport. The codec is driven through the
  ADSP via **SlimBus/IPC-router QMI** (`qcslimbus8380.sys` client contexts, svc_id-based
  `WdfIoTargetQueryForInterface` opens). For Linux bring-up this maps to `qcom,slimbus` /
  apr/glink audio services (q6cvm/adsp), i.e. the existing X1E upstream ASoC path.

## 2. Bluetooth — QCA UART HCI with HSP firmware load

`qcbluetooth8380.sys` (622 KB) + `qcbtacx_transportdriver8380.sys` (66 KB, "Aqstic BT ACX
Transport", ACPI\QCOM0D04, also spawns BLE audio child devices `{DAADDA84-...}\Render|Capture`).

- UART attached: INF hw-id `ACPI\VEN_QCOM&DEV_0C6B&SUBSYS_*` (QCOM serial 0x0C6B) — matches the
  X1E geni-serial BT UART in Linux DSDT.
- Firmware load: HCI VS-command based, WCN399x-class "Rome/HSP" style sequence. Strings (addresses in
  /tmp/rea-audio-qcbluetooth.json):
  - `FdoDevFirmwareInitialize`, `FdoInitFirmwareAndPdoCB`, `Failed to init firmware`
  - `FdoDevConfigHighBaudRate`, `FdoDevConfigHighBaudRate_FlowCtrl`, `FdoDevConfigHighBaudRate_rome`,
    `FdoDevResumeDefBaudRate`, `qca_uart_set_port_baudrate`/`qca_uart_get_port_baudrate`,
    `DefaultBaudRate` (registry), `Failed to setup Rome SoC baud rate`, `Set Baud rate finished` —
    i.e. boot at default baud (115200), download firmware/NVM, then switch to high baud via HCI VSC.
  - NVM provisioning via `NVM_ACCESS_SET` HCI command with per-registry overrides:
    `QCOM\BT_NVMTAG35/36/83/87/174.PROVISION`, `QCOM\BT.PROVISION`, `QCOM\BT_RegPower.PROVISION`,
    `QCOM\BT_SarCfg.PROVISION`, `QCOM\BT_SarPower.PROVISION`, `UserNvmFile`.
  - Sleep/wake: `Failed to setup SoC sleep mode`, `Soc sleep mode disabled, Initializing firmware
    now...`, `Send wakeup ack finished`, registry `ExplicitRtsWakeup` (0x1) — **wake via UART RTS
    line + controller wakeup-indicator (wakeup ack)**, no dedicated wake GPIO in the driver; plus
    `FdoWakeupIdleChildren`, `FdoSetIdleSettings`.
  - VSC/Microsoft extension events: `HandleHciVsSet_CMD/EVENT`, MSFT-defined HCI VSC events.
- Firmware files shipped in the same package: `hmtbtfw20.tlv` + `hmtnv20.b1xx` (Hamilton
  BTFW.HAMILTON.2.0.5-00017-WOS_PATCHZ-1) and `hpbtfw21.tlv` + `hpnv21*.b1xx`
  (BTFW.HSP.2.1.1-00476-WOS_PATCHZ-1), plus `bsrc_bt.bin` (bootstrap/ROM patch). This is standard
  Linux `btusb`/`hci_qca` TLV format — the X1E WCN SOC uses the HSP 2.1 firmware variant
  (`hpbtfw21.tlv`, NVM `hpnv21g.b10c`/`.b111` by board ID).

## 3. Wi-Fi/WWAN firmware-update paths

- `miscwlanext` is INF-only (no driver): regulatory tweaks for PCI `VEN_17CB&DEV_1107`
  (QC WLAN): `DisableReport6GAPtoOS=0x10` (hide VLP APs), `subbandCTLEnable=3`,
  `RemapCA5GCTL`, `RemapSA6GSubbandCTL=1`, `EnableCustomizedRegdomain=3` (US->non-VLP),
  UNII-4 enable. DriverVer 1.14.490.0.
- `mbbfwupdate` is a UMDF service DLL (`QcMbbFWUpdateDriver.dll`) + signed `update.zip` (126 MB)
  implementing the QCMBBUPDATE MBN flow; `FwVersions.txt` lists modem MFVS from 240105 through
  250613 (SDX-class, `DE` build tags). Kernel bring-up relevance: firmware update path only —
  no kernel interface needed on Linux (MBN comes with wlan fw / ath11k-generic packages).

## 4. Pen

- `pen0c0ffirmwareupdate`: UMDF HID extension (matching BLE HID
  `{00001812-...}_Dev_VID&02045e_PID&0C0F&Col03`, `HidTargetInStack=1`) delivering CFU-style
  offer/payload pairs: `SurfacePen0C0F_{MCU-B0,BLE-HI,BLE-LO,HAPTIC}.{offer,payload}.bin`.
  Charging/hotkey handling is NOT here — it is HID (Col03 usage page FFF4).
- `penblelcaddradaptation/SurfacePenBleLcAddrAdaptationDriver.sys`: KMDF HID filter over G6 touch /
  pen-charger nodes (`HID\VID_045E&PID_096D/099F/09AD/09B0/09B1&Col03&UP:FFF4_U:0001`). REA strings
  show BLE LC (link-control) address adaptation:
  - `ConvertPenIDToMACAddress` (AutoHostID0 = Slim Pen 1, AutoHostID1 = Slim Pen 2), `GetHostMacAddress`
    (HidP_GetUsageValueArray), `USAGE_HOST_BTH_ADDR`, `SignBufferUsingMAC`/`FlipMAC`/`ReverseMAC`,
    `ValidateMacSignatureOfBuffer`, `USAGE_DEVICE_AUTO_BONDING_CAPABILITY`, `USAGE_DEVICE_KNOWN_HOST`
    — i.e. the host writes its BT MAC into the pen over HID FFF4 usages so the pen can auto-bond to
    the Surface BT controller; a MAC-signed buffer (anti-spoof) is validated on reads.
  - Pen charger: `SendFHID[BuiltInPenCharger]` (Studio), `SendFHID[SeparatePenCharger]` (Pro-line
    attachable charger), `GetFhidWithRetry` — charger and digitizer nodes coordinate driver start
    order (ioTargetCollection).
  - Pen state persists in a per-pen registry key (`Pen ID`, Get/Read/AssignValueInPenRegistryKey).

## 5. Actionable gaps for the Linux repo

1. Audio idle: mirror Windows by keeping codec D0/D1 while jack-detect or voice-activation armed;
   full D3 only on card removal. The Windows 5-second ConservationIdleTime/PerformanceIdleTime with
   IdlePowerState=D1 (both 5 s) is the qualification reference.
2. Audio codec control is ADSP-mediated (SlimBus/QMI, AUCD QCOM0C29): Linux should use upstream
   X1E80100 ASoC (sm8450/snd-q6dsp family) rather than expecting direct codec I2C.
3. BT: hci_qca already supports this flow (bsrc bootstrap -> tlv fw -> nvm -> baud VSC -> sleep
   with RTS wakeup `ExplicitRtsWakeup`). Ensure DTS powers the 0x0C6B UART and sets
   `enforce-active-before-pm`.
4. Pen auto-bonding (HID FFF4 MAC programming) and CFU firmware update are userspace/HID concerns —
   hid-surface-hid + surfacepen userspace coverage; no new kernel driver strictly required for bring-up.

— Generated by REA CLI + INF cross-reference, 2026-11-14.
