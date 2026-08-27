# Debugger prerequisites and public symbol follow-up

Date: 2026-08-27. This follow-up used read-only inventory and anonymous HTTP HEAD requests. It did not attach a debugger, install breakpoints, read MMIO, activate cameras/Hello, change boot/security settings, download symbols, or apply the pending recorder-ownership patch. Results of the separate privileged camera suite are outside this note.

## Verified configuration and identities

The elevated read-only `bcdedit /enum {current}` result reports **debug Yes** and `hypervisorlaunchtype Auto`; Secure Boot reports disabled. Debug boot configuration is no longer an unknown prerequisite. It does not establish a connected debugger or armed camera breakpoint.

WinDbg ARM64 1.2606.22001.0 is installed locally, with bundled KD/CDB tools and the `WinDbgX.exe` alias. No local debugger process was observed during this follow-up; that observation does not establish whether a separate host is prepared or connected.

The ten installed camera-driver image hashes still match the offline PE audit. Their service/image names, versions, ARM64 machine type, SHA-256 values and embedded PDB GUID/age identifiers are preserved in the [companion JSON](debugger-prerequisites-followup.json). These are installed-image identities, not verified loaded symbols. All ten images lack PE exports and import `MmMapIoSpaceEx`/`MmUnmapIoSpace`; those imports do not identify a CCI routine, its arguments, a live mapping or a powered block.

## Exact public URL observations

The original four uncompressed PDB HEAD checks were followed by eight anonymous HEAD checks of the compressed `.pd_` and `file.ptr` variants, using the same exact PDB/GUID/age keys. Each new request had a 15-second timeout. No credentials, cookies, PDB content or pointer body were used/downloaded.

| Role | PDB | Uncompressed | Compressed | file.ptr |
| --- | --- | ---: | ---: | ---: |
| Front | surfacecamfrontsensor8380.pdb | 404 | 404 | 404 |
| Rear | surfacecamrearsensor8380.pdb | 404 | 404 | 404 |
| IR | surfacecamauxsensor8380.pdb | 404 | 404 | 404 |
| CSI receiver | qccammipicsi8380.pdb | 404 | 404 | 404 |

These statuses apply only to the tested Microsoft public-symbol URLs and HEAD requests at the recorded times. **They do not prove that matching PDBs are unavailable everywhere.** Other public/private stores, layouts or retrieval methods were not searched, and symbol/function coverage was not validated. The JSON records exact keys, timestamps and hashes of the private observations without exposing local paths, boot identifiers, connection keys or user/host identities.

## Remaining runtime gates

- Confirm a separate Windows debugger host, supported transport and live target connection. Local kernel debugging cannot perform the breakpoint/stepping workflow needed for CCI tracing. [Microsoft local-debugging limitations](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/performing-local-kernel-debugging)
- Verify the loaded camera images and matching symbols; review the actual CCI submission/completion implementation, calling convention, address/length fields and filtering. Do not substitute a guessed camera function for the HID-SPI precedent.
- Obtain this Windows target's translated resources and live mappings, identify the active receiver instance, and establish its power/clock window and register read semantics. The reference owner's PHY2 result and static Linux DTS ranges do not satisfy those checks.
- Review a bounded per-camera capture plan and cleanup before arming anything. A Hello observation, provider registration or ETL file does not establish sensor-register capture or safe MMIO access.

Microsoft documents the separate host, target, controller and cable requirements for [USB-EEM debugging](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/setting-up-kernel-mode-debugging-over-usb-eem-arm-kdnet). No transport configuration or boot change was performed here. The current connected/armed remote Windows kernel-debug session remains **unverified**.
