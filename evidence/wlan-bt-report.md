# WCN7850 WLAN/BT bring-up evidence — Surface Pro 11 (ARM) package 26.041.12746.0

Source package: `/Users/leon/Downloads/SurfacePro11_ARM_Win11_26100_26.041.12746.0/SurfaceUpdate/`
Relevant dirs: `qcwlanhmt/` (WLAN firmware/driver), `qcbluetooth/` (BT firmware/driver), `5gnrcoexmgr/` + `5gnrcoexmgrext/` (5G NR coexistence), `gnss/`, plus `miscwlanext/`, `miscbtext/`, `mhihost/`.

Method note: the REA MCP tools (`tools.rea.*`) and `close_binary` were not registered in this session's tool runtime, so analysis was performed with equivalent direct binary inspection (`strings -a -t x`, `xxd`, INF/UTF-16LE decoding). All offsets below are hex file offsets. Confidence marked per finding.

## 1. Firmware inventory

### WLAN (`qcwlanhmt/`)

| File | Format | Version / identity | Confidence |
|---|---|---|---|
| `wlanfw20.mbn` (≈24.7 MB in pkg) | ELF32 little-endian, EM_ARM (e_machine=0x28), e_type=EXEC — Qualcomm MBN/ELF firmware image | `QC_IMAGE_VERSION_STRING=WLAN.HMT.1.1.c5-00310-QCAHMTSWPL_V1.0_V2.0_SILICONZ-1` @ 0xa5170; `IMAGE_VARIANT_STRING=7850.wlanfw.eval_v2_TOQ` @ 0xa5110; `OEM_IMAGE_VERSION_STRING=ip-10-195-203-234` @ 0xa5140; `OEM_IMAGE_UUID_STRING=Q_SENTINEL_{1FD7BD9A-...}_20250707_0426` @ 0xa50b8. Internal strings: `WCN7850` @ 0x9f870, `WCN7851` @ 0x9f878, `phySetParam_wcn7850` @ 0x35475a | High |
| `phy_ucode20.elf` | ELF (board/PHY ucode container) | WLAN.HMT family (HMT = Hamilton, WCN7850) | Medium |
| `bdwlan*.elf` / `bdwlan.eXX` (26 files) | Board data, ELF-wrapped (`\x7fELF` magic, but these are QCA board-file containers; extensions encode bus/dev IDs) | Generic + OEM variants: `bdwlan_wcn785x_2p0_ncm825*.elf` (many SKUs: AS_S55_SA, DE01, DE02, HO_MoorO, LES790, LE_Altai, LE_C590, PZ36, QC_revB, UX3407Q) and `bdwlan_wcn785x_2p0_ncm865a*.elf` | High |
| `Data20.msc` (1.5 MB) | ASCII debug/trace ID catalog: starts `VERSION:268535144\r\n\r\n0,III,[ data_rx_pn_patched.c : 183 ]...` @ 0x00 — WLAN host/firmware trace string table (matches ath12k/WLAN debug ids like `WAL_DBGID_CHANNEL_CHANGE_FORCE_RESET`) | High |
| `regdb.bin` | Binary regulatory DB; magic bytes `74 cf 04 00 37 00 00 41 46 00 41 46 00` @ 0x00 (QCA regdb container, "AF" country-tag records) | Medium |
| `qdss_trace_config_v1/v2.cfg` | Qualcomm QDSS trace config | Low relevance to Linux bring-up |
| `qcwlanhmt8380.sys` | Windows ARM64 WLAN driver (Beryllium) | — |

INF `qcwlanhmt8380.inf` (UTF-16LE): `DriverVer = 07/14/2025,1.0.4374.1300`; hardware IDs:
- `PCI\VEN_17CB&DEV_1107` → `QcWlan_BERYLLIUM.ndi` (generic WCN785x)
- `PCI\VEN_17CB&DEV_1107&SUBSYS_141400AB` (Microsoft Surface subsystem 1414:00AB) → same Beryllium install — **direct match for the Surface Pro 11/MS Dev Kit 2008/2028 [17cb:1107] device.** (High)
- Other SUBSYS IDs (E0EB105B, 80011EAC, 65701A3B, E11517AA, E129105B, …) map to NCM865A (non-Surface) OEM SKUs.

### Bluetooth (`qcbluetooth/`)

| File | Format | Version / identity | Confidence |
|---|---|---|---|
| `hmtbtfw20.tlv` (280,652 B) | QCA "Hamilton" TLV patch format (header `01 48 48 04 …` = TLV type/len records wrapping Thumb firmware blobs — same container family as Linux `btqca` reads over HCI VLAN/UART) | Build stamp `Sep 26 2025` @ 0x3b4, `02:22:34` @ 0x3c4, version string `BTFW.HAMILTON.2.0.5-00017-WOS_PATCHZ-1` @ 0x3d4 (also in `hmtbtfw20.ver`) | High |
| `hmtbtfw20.ver` | ASCII version: `BTFW.HAMILTON.2.0.5-00017-WOS_PATCHZ-1` | High |
| `hmtnv20.{bin,b105,b107,b108,b10f,b112,b3b}` (9,656 B each) | QCA BT NVM (NV) TLV files; extension = OEM/board ID in hex (e.g. `.b3b`), `.bin` = default | High (format), Medium (mapping) |
| `hpbtfw21.tlv` / `.ver` | HSP (2nd-gen, "hpbtfw21") TLV firmware, `Dec 20 2024` @ 0x338, version `BTFW.HSP.2.1.1-00476-WOS_PATCHZ-1` @ 0x358 — different chipset (HSP, likely for other SKU) | High |
| `hpnv21.b10c/.b111`, `hpnv21g.*` | NVM for HSP variant | High |
| `bsrc_bt.bin` (248 B) | ACPI "AeoB" (`41 65 6f 42` @ 0x00) device-readiness blob referencing ACPI namespace `_SB.BTH0` @ 0x22, `COMPONENT`, `STATE`, `DSTATE` — BT power-state DSD/FADT-style table used by Surface ACPI | Medium |
| `qcbluetooth8380.sys` / `.inf` | Windows BT driver, `DriverVer = 07/16/2025,1.0.4374.1301` | High |

INF hardware IDs (UTF-16LE): BT attaches over **ACPI/UART**, not USB:
`ACPI\VEN_QCOM&DEV_0C6B&SUBSYS_141400EA` (Surface), plus SUBSYS_IDPS8380, IDP08380, CRD08380, 1EF31043. So BT = QCOM HID 0C6B serial device (WCN7850 integrated BT on UART/TTY). (High)

## 2. Board / device identity

- WLAN PCIe ID confirmed: `17CB:1107`, Surface subsystem `1414:00AB` (INF, High). Matches the in-tree PCI LTR quirk for WCN7850 [17cb:1107].
- Board files: default `bdwlan.elf`; bus/device-encoded `bdwlan.eXX` copies (e36, e3b; bdwlan01.e02…e12 = DDR/board-id variants); OEM-scoped `bdwlan_wcn785x_2p0_ncm825*.elf`. Surface device would consume the generic/141400AB path; Linux ath12k expects `bdwlan.bin`-style board + `park#` files (needs conversion from .elf container). (Medium)
- FW image naming "HMT" = Hamilton (WCN7850, ath12k WLAN.HMT.* series); "ncm865a" variants are for a different product (NCM865A subsys), not Surface.
- BT SoC family: Hamilton 2.0 (matches WCN7850's BT side); NVM board IDs b105/b107/b108/b10f/b112/b3b enumerate OEMs; `.bin` default.

## 3. 5G NR coexistence (`5gnrcoexmgr/`)

- UMDF user-mode driver `qc5gnrcoexmgr.dll`; INF `surface_5gnrcoexmgr.inf`, DriverVer `08/28/2024,1.1.25.0`, Class SYSTEM.
- Installs on a UMDF interface device: `{05C9354C-AFD7-40BE-8B68-323EC7FFF97D}\Qc5GNRCoexMgr_MSFT_DE` — a device interface GUID, not PCI (coex coordination rides above the MHI/QRTR stack).
- DLL strings: `QCCI TX: %s txn: 0x%x msg_id: 0x%x len: %d` @ 0x1a4e8 and `QCCI RX:` @ 0x1a518 — **QCCI = Qualcomm Common Client Interface (QMI-family messaging)**, so the coex manager exchanges QMI-style messages (txn + msg_id) with a remote service (likely the 5G/modem subsystem via MHI/QRTR). Symbol `nrcoex_mgr_context_type` @ 0x1a320, PDB `qc5gnrcoexmgr.pdb` @ 0x1a798. (Medium-High)
- No explicit antenna-grant/WLAN channel table strings visible (stripped, mostly imports). No direct Linux-equivalent protocol spec recoverable from this binary alone. (Limitation)

## 4. GNSS (`gnss/`)

- `qcgnss.dll` (628 KB, KMDF, Class SYSTEM, DriverVer `02/16/2024,1.0.187.0`).
- Windows GNSS IOCTL interface: `IOCTL_GNSS_START_FIXSESSION` @ 0x8a960, `STOP_FIXSESSION` @ 0x8a980, `MODIFY_FIXSESSION` @ 0x8a9b8, `GET_FIXDATA` @ 0x8a9f8, `LISTEN_NMEA` @ 0x8a9f0 — so **NMEA output is supported/expected**; fix sessions via Windows GNSS DDI.
- Backend: `REPOSITORYDIR\common\hostdrivers\win\NTAD\qcgnss\qmi\loc_api_v02_client.c` @ 0x89f00 — **Qualcomm LOC API v02 over QMI** (LOC service, i.e., the same service Linux `pd-mapper`/`qrtr`/gpsd upstream would talk to, service LOC on the modem DSP). PSDS aid files: `xtra3grc.bin`, `xtra3grcej.bin`, `xtra3Mgrcej.bin` @ 0x8adc8-0x8ada8 (Qualcomm XTRA3 assistance). (High)
- Implication for Linux: GNSS is serviced by the modem's LOC QMI service over QRTR/MHI, not a discrete NMEA UART chip — Linux path is loc_client/pd-mapper (e.g., `qcom-geni`/gnss-qrtr driver), not a ttyGNSS. (Medium-High inference)

## 5. Limitations

- REA MCP tools were unavailable; analysis via strings/hex offsets only. No disassembly of ARM64 PE drivers (`.sys`/`.dll`) was performed — coex message IDs and protocol constants (QCCI msg_id values, antenna-map tables) remain unrecovered.
- `.tlv`/NVM/board files not TLV-decoded record-by-record; version/identity claims rest on embedded ASCII stamps.
- `Data20.msc` is a debug catalog only; no firmware semantics.
- Firmware images are Windows-SPL signed (SILICONZ/TOQ variant); Linux ath12k expects its own `amss.bin`/`m3.bin`/boardfile naming — exact 1:1 reuse of `wlanfw20.mbn` under ath12k is unverified (ELF is loadable in principle; ath12k uses amss+boardfile pair). Marked Low confidence for direct reuse.
- `close_binary` tool does not exist in this runtime; nothing to invoke.

## 6. Bring-up conclusions for the Linux tree

1. Device identity verified: WLAN PCI 17cb:1107 (subsys 1414:00AB) = WCN7850 "Beryllium"/Hamilton; BT = ACPI QCOM0C6B UART (HCI H4/QCA), GNSS = LOC-over-QMI. (High)
2. Firmware versions to target: WLAN `WLAN.HMT.1.1.c5-00310-QCAHMTSWPL_V1.0_V2.0_SILICONZ-1` (2025-07-07 build), BT `BTFW.HAMILTON.2.0.5-00017` (2025-09-26), matching linux-firmware ath12k/WCN7850 and qca/hmtbtfw20.tlv lineage. (High)
3. BT NVM selection: use `hmtnv20` family; Surface board-specific NVM suffix for subsys 1414:00EA is not encoded in filenames — needs runtime board-ID probing on device. (Medium)
4. Coex: 5G NR coex manager speaks QCCI/QMI with msg_id transactions over the QCIPC/MHI stack; on Linux this maps to a QRTR service on the modem — no evidence yet of a WLAN-side mailbox interface. (Medium)
