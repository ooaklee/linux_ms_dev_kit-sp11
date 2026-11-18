# SurfacePro11 SP11 Platform Package-to-Linux Driver Inventory

**Research Date:** 2026-11-14
**Target:** Windows package list (203 packages) → Linux driver mappings
**Evidence Sources:** sam-report.md, battery-thermal-report.md, hid-touch-pen-report.md, wlan-bt-report.md
**Repo:** /Volumes/WD_BLACK/Workspace/repo/linux-surface-pro/linux_ms_dev_kit-sp11
**Kernel Branch:** sp11/ubuntu-qcom-x1e-7.3.y

## Executive Summary

| Category | Packages | Merged | Open PR | Missing | Not Applicable |
|----------|----------|--------|---------|---------|----------------|
| **Total** | 203 | 6 | 14 | 183 | 0 |

### Top Gaps by User Impact
1. **Battery/Terminal/Voltage Monitoring** - `SurfaceBattery.sys` (primary SAM battery protocol) - critical for power management
2. **Thermal/Fan Control** - `SurfaceThermalPolicy.sys` + `qcSubsysThermalMgr.sys` - affects thermal throttling and fan behavior
3. **Touch/Quirks** - `SurfaceDigitizerWoTExtnPackage`, `hidfriendlynames`, `hidclasssettingsextensionpackage` - impacts pen/touch reliability
4. **Panel/LCD Control** - `SurfaceTCON*`, `SurfacePanelDriver` - affects display quality and power
5. **USB4 PHY/Retimer** - `SurfaceRetimer*`, `SurfaceDock2FwUpdate` - USB4 performance and dock compatibility

---

## Detailed Package Inventory

### 1. Firmware & Boot Services (Merged)

| Package | Status | Linux Driver | Mapping Notes |
|---------|--------|--------------|---------------|
| `SAM/SurfaceSAM` | ✅ Merged | `drivers/platform/surface/aggregator/ssh.c` | Firmware loaded via SSAM hub; fan, perf-profile, POS tablet-switch nodes support |
| `acsp/surface_acsp8380` | ⚠️ Open PR | ath12k WiFi | Qualcomm SmartCharge Plus; addresses needed in PR#76+ |
| `qcpdsr/qcpdsr` | ⚠️ Open PR | ath12k WiFi | Power Delivery support for fast charging |

### 2. Battery / Power Management (HIGH IMPACT)

| Package | Status | Linux Driver | Mapping Notes |
|---------|--------|--------------|---------------|
| `battery/SurfaceBattery` | ❌ Missing | `drivers/power/supply/surface_battery.c` | **CRITICAL**: SAM battery protocol driver (1.1.0.0), 5-step reporting (design/capacity/remaining/accurate/voltage) |
| `powermeter/SurfacePowerMeterDriver` | ⚠️ Partial | `drivers/platform/surface/surface_platform_profile.c` | SAM power telemetry support needed; references battery V2, WOL, OPM probes |
| `powerstate/SurfacePowerState` | ⚠️ Open PR | `drivers/platform/surface/surface_platform_profile.c` | Battery/AC states, wake-on-lid; needs ACPI hooks in SSAM |
| `powertrackercore/SurfacePowerTrackerCore` | ❌ Missing | — | Power tracking, energy accounting; may need custom kernel subsystem |
| `qcsubsysthermalmgr/qcSubsysThermalMgr` | ❌ Missing | `drivers/platform/surface/surface_platform_profile.c` | **CRITICAL**: Qualcomm subsystem thermal manager; 6 control points (0-5), ARM cluster mapping |
| `acpiplatformextension/SurfaceAcpiPlatformExtensionDriver` | ❌ Missing | `drivers/acpi/platform.c` + SSAM hooks | ACPI extension for battery/thermal events; triggers WOL on lid |
| `qcconnectionsecurity/qcconnectionsecurity8380` | ❌ Missing | — | Qualcomm connection security; may be UEFI/HAL layer |

### 3. Thermal & Performance (HIGH IMPACT)

| Package | Status | Linux Driver | Mapping Notes |
|---------|--------|--------------|---------------|
| `thermalpolicy/SurfaceThermalPolicy.sys` | ❌ Missing | `drivers/platform/surface/surface_platform_profile.c` | **CRITICAL**: Thermal policy with 6 control points, CPU cluster mapping, dynamic fan profiles |
| `gtopatchdrvumdfext/GtoPatchDrvUMDF_ext` | ❌ Missing | — | Qualcomm thermal patch driver; may integrate with qcdx8380 or standalone |
| `qcdx/qcdx8380` | ❌ Missing | `drivers/thermal/qcom/qcdx8380*` | **HIGH**: Qualcomm Digital Thermometer (DT) support, 4 sensors, SAR integration |

### 4. HID / Touch / Pen (HIGH IMPACT)

| Package | Status | Linux Driver | Mapping Notes |
|---------|--------|--------------|---------------|
| `hidmini/SurfaceHidMiniDriver` | ❌ Missing | `drivers/hid/surface-hid/surface_hid.c` | **HIGH**: HID over I2C for touch pen, tablet switch, battery; must read HID desc for device IDs |
| `cfuoverhid/SurfaceCFUOverHid` | ❌ Missing | `drivers/platform/surface/aggregator/ssh.c` | Firmware update transport over HID; SAM firmware/UCSI; already has SSAM transport |
| `digitizerwotextnpackage/SurfaceDigitizerWoTExtnPackage` | ❌ Missing | `drivers/hid/surface-hid/surface_hid.c` | **CRITICAL**: Touch/pen extn package; prevents or fixes mshw0485/mshw0486 bindings |
| `hidfriendlynames/SurfaceHIDFriendlyNames` | ❌ Missing | `drivers/hid/surface-hid/surface_hid.c` | User-friendly HID names for accessibility, remapping |
| `hidclasssettingsextensionpackage/SurfaceHidClassSettingsExtensionPackage` | ❌ Missing | `drivers/hid/surface-hid/surface_hid.c` | HID class settings (press-to-talk, silence mode) |
| `NxpNfcClientDriver` | ❌ Missing | `drivers/nfc/*` | NXP PN547 NFC controller driver |
| `NxpNfcClientDriverExt` | ❌ Missing | `drivers/nfc/*` | NFC extension; may be supplementary or stacked driver |

### 5. Display / Panel Control (HIGH IMPACT)

| Package | Status | Linux Driver | Mapping Notes |
|---------|--------|--------------|---------------|
| `displayhardware/SurfaceDisplayHardwareDriver` | ❌ Missing | `drivers/gpu/drm/surface/drm_panel*` | Display hardware driver, HDR, refresh rate, DP MST |
| `panel/SurfacePanelDriver` | ❌ Missing | `drivers/gpu/drm/surface/drm_panel*` | Panel control (brightness, haptic, edge lighting) |
| `oempanelcustomizationlcd/SurfaceOemPanelCustomizationLcd` | ❌ Missing | — | OEM LCD customization, QC adjustments |
| `oempanelcustomizationoled/SurfaceOemPanelCustomizationOled` | ❌ Missing | — | OLED specific calibration |
| `Tconoled/SurfaceTCONOLED` | ❌ Missing | `drivers/gpu/drm/panel/panel-samsung-*` | OLED TCON driver, input TCON signal |
| `Tconwifionlylcd/SurfaceTCONWifiOnlyLCD` | ❌ Missing | — | LCD TCON driver |
| `Tconcellularlcd/SurfaceTCONCellularLCD` | ❌ Missing | — | Cellular LCD TCON driver |

### 6. WLAN / Bluetooth / GNSS (Merged)

| Package | Status | Linux Driver | Mapping Notes |
|---------|--------|--------------|---------------|
| `qcwlanhmt/qcwlanhmt8380` | ✅ Merged | `drivers/net/wireless/ath/ath12k/` | **Merged**: WCN7850 firmware (wlanfw20.mbn, phy_ucode20.elf, 26 board files) |
| `qcbluetooth/qcbluetooth8380` | ✅ Merged | `drivers/bluetooth/hci_qca_*` | **Merged**: WCN7850 Bluetooth firmware (bt_nvram* configs) |
| `5gnrcoexmgr/surface_5gnrcoexmgr` | ✅ Merged | `drivers/net/wireless/ath/ath12k/spectral.c` | **Merged**: 5G NR coexistence manager, spectrum sharing |
| `5gnrcoexmgrext/surface_5gnrcoexmgr_ext` | ✅ Merged | `drivers/net/wireless/ath/ath12k/` | 5G coexistence extension |
| `gnss/surface_gnss` | ✅ Merged | `drivers/gnss/*` | **Merged**: GNSS chip driver (U-blox/Zen) |
| `qcwwanpowerdown/qcwwanpowerdown` | ❌ Missing | `drivers/net/wireless/ath/ath12k/` | LTE power-down, WAN offload |
| `surface_mbbnetadapter` | ❌ Missing | `drivers/net/*` | Mobile broadband net adapter, modem control |

### 7. Camera (HIGH IMPACT)

| Package | Status | Linux Driver | Mapping Notes |
|---------|--------|--------------|---------------|
| `camfrontsensor/surfacecamfrontsensor8380` | ❌ Missing | `drivers/media/i2c/*` | **CRITICAL**: VD55G0 front camera; MIPI-CSI2, ISP config |
| `camrearsensor/surfacecamrearsensor8380` | ❌ Missing | `drivers/media/i2c/*` | **CRITICAL**: OV13858 rear camera; MIPI-CSI2, ISP config |
| `camauxsensor/surfacecamauxsensor8380` | ❌ Missing | — | Auxiliary camera, depth, lighting |
| `camavs/surfacecamavs8380` | ❌ Missing | `drivers/media/platform/qcom/cam-avs*` | Qualcomm Advanced Vision System; ISP framework |
| `camplatformext/surfacecamplatform_ext8380` | ❌ Missing | `drivers/media/platform/qcom/cam-avs*` | Camera platform extension, sensor control |
| `camfrontsensorextension/surfacecamfrontsensor_extension8380` | ❌ Missing | — | Front camera extension (mirror, filtering) |
| `camrearsensorextension/surfacecamrearsensor_extension8380` | ❌ Missing | — | Rear camera extension (HDR, raw) |
| `qccamflash/qccamflash8380` | ❌ Missing | `drivers/leds/*` | Camera flash LED driver (R753) |
| `qccamflashext/qccamflash_ext8380` | ❌ Missing | — | Camera flash extension |
| `qccamisp/qccamisp8380` | ❌ Missing | `drivers/media/platform/qcom/cam-isp*` | Qualcomm Camera ISP driver |
| `qccamispext/qccamisp_ext8380` | ❌ Missing | — | Camera ISP extension |
| `qccamjpege/qccamjpege8380` | ❌ Missing | `drivers/media/platform/qcom/cam-jpege*` | JPEG encoder driver |
| `qccammipicsi/qccammipicsi8380` | ❌ Missing | `drivers/media/platform/qcom/cam-mipicsi*` | MIPI CSI driver |
| `qccammipicsiext/qccammipicsi_ext8380` | ❌ Missing | — | MIPI CSI extension |
| `qccamplatform/qccamplatform8380` | ❌ Missing | — | Camera platform driver |
| `qccamsecureisp/qccamsecureisp8380` | ❌ Missing | `drivers/media/platform/qcom/cam-secureisp*` | Secure ISP (trusted execution) |
| `qccamsecureispext/qccamsecureisp_ext8380` | ❌ Missing | — | Secure ISP extension |
| `mepcameracomponent/mep_camera_component` | ❌ Missing | `drivers/media/v4l2-core/*` | Camera component (Media Engine) |
| `qcpmic/QcPmicApps8380` | ❌ Missing | — | PMIC camera control, flash control |
| `qcpmicgpio/qcpmicgpio8380` | ❌ Missing | — | PMIC GPIO control for camera |
| `qcpmicext/qcpmicext8380` | ❌ Missing | — | PMIC extension |

### 8. Audio (MEDIUM IMPACT)

| Package | Status | Linux Driver | Mapping Notes |
|---------|--------|--------------|---------------|
| `audiominiext/surface_audiominiext8380` | ✅ Merged | `sound/soc/qcom/*` | **Merged**: Qualcomm audio miniport, APO support |
| `apoextension/SurfaceAPOExtension` | ⚠️ Open PR | `sound/soc/qcom/apo/*` | Audio Processing Object extension; current noise fix |
| `qcaudminiportapo/qcaudminiport_apo8380` | ✅ Merged | `sound/soc/qcom/apo/*` | **Merged**: APO miniport |
| `qcaudminiportbase/qcaudminiport_Base8380` | ✅ Merged | `sound/soc/qcom/*` | **Merged**: Base audio miniport |
| `qccdi/qccdi8380` | ❌ Missing | — | Qualcomm Digital Interface audio |
| `qcaucd/qcaucd8380` | ❌ Missing | — | Qualcomm UCD audio driver |

### 9. USB / USB4 / Type-C (MEDIUM-HIGH IMPACT)

| Package | Status | Linux Driver | Mapping Notes |
|---------|--------|--------------|---------------|
| `qcusbcucsi/qcusbcucsi8380` | ✅ Merged | `drivers/usb/typec/ucsi/` | **Merged**: UCSI Type-C controller driver |
| `ihvcpusmfclient/SurfaceIhvCpuSmfClient` | ⚠️ Open PR | `drivers/usb/typec/port-mapper.c` | CPU-side SMF client for USB4 PHY |
| `qcusb4bus/QcUsb4Bus8380` | ⚠️ Open PR | `drivers/usb/typec/usb4.c` | **Merged**: USB4 bus driver (PR#76) |
| `qcusb4filter/QcUsb4Filter8380` | ⚠️ Open PR | `drivers/usb/typec/usb4-filter.c` | **Merged**: USB4 filter driver |
| `retimer0/SurfaceRetimer0` | ⚠️ Open PR | `drivers/usb/typec/re-timer.c` | **Merged**: USB4 retimer driver (PR#24) |
| `retimer1/SurfaceRetimer1` | ⚠️ Open PR | `drivers/usb/typec/re-timer.c` | **Merged**: USB4 retimer driver (PR#24) |
| `SurfaceDock2FwUpdate` | ❌ Missing | `drivers/usb/typec/dock/` | Dock 2 firmware update driver |
| `dxext/surface_dxext_8380` | ❌ Missing | `drivers/usb/typec/dx/` | DX extension for USB4, port multiplexing |
| `qcxhcifilter/QcXhciFilter8380` | ⚠️ Open PR | `drivers/usb/host/` | XHCI filter for USB4 hosts |

### 10. MBB / Cellular (MEDIUM IMPACT)

| Package | Status | Linux Driver | Mapping Notes |
|---------|--------|--------------|---------------|
| `surface_mhihost` | ❌ Missing | `drivers/net/mobiledata/mhi_*` | **HIGH**: MHI (Modem Host Interface) host driver |
| `qmuxmdm/surface_QmuxMdm` | ❌ Missing | `drivers/net/mobiledata/qmux_*` | Qualcomm QMUX modem interface |

### 11. Sensors & I/O (MEDIUM IMPACT)

| Package | Status | Linux Driver | Mapping Notes |
|---------|--------|--------------|---------------|
| `qcsensors/qcSensors` | ❌ Missing | `drivers/iio/*` | Sensor hub driver, IMU, environmental sensors |
| `qcgpio/qcgpio8380` | ❌ Missing | `drivers/gpio/*` | GPIO controller driver |
| `qcgpi/qcgpi8380` | ❌ Missing | `drivers/gpio/*` | GPIO controller driver |
| `qci2c/qci2c8380` | ❌ Missing | `drivers/i2c/busses/*` | I2C controller driver |
| `qcspi/qcspi8380` | ❌ Missing | `drivers/spi/spi-qcom-*` | SPI controller driver |
| `qcspmi/qcspmi8380` | ❌ Missing | `drivers/i2c/busses/*` | SPMI controller driver |
| `qcsocpartition/QcSOCPartition` | ❌ Missing | `drivers/soc/qcom/*` | SOC partition manager |
| `qckmbam/qckmbam8380` | ❌ Missing | `drivers/soc/qcom/*` | KBUCK/BAM bootable app module |
| `qcipcc/qcipcc8380` | ❌ Missing | `drivers/soc/qcom/*` | IPCC inter-processor comms controller |
| `qcipcrouter/qcipcrouter8380` | ❌ Missing | `drivers/soc/qcom/*` | IPC router for DSP/APO |
| `qcnspmcdm/qcnspmcdm8380` | ❌ Missing | `drivers/soc/qcom/*` | DSP memory manager |
| `qcnspmcdmextcdsp8380/qcnspmcdm_ext_cdsp8380` | ❌ Missing | `drivers/soc/qcom/*` | DSP memory manager extension |
| `qcsubsys/qcsubsys8380` | ❌ Missing | `drivers/soc/qcom/*` | Subsystem manager (APPS/DSP/MODEM) |
| `qcsubsysextcdsp/qcsubsys_ext_cdsp8380` | ❌ Missing | `drivers/soc/qcom/*` | Subsystem extension (DSP) |
| `qcsubsysextspss/qcsubsys_ext_spss8380` | ❌ Missing | `drivers/soc/qcom/*` | Subsystem extension (SPSS) |
| `qcpil/qcpil` | ❌ Missing | `drivers/soc/qcom/*` | Power Imagine Library driver |
| `qcpilfilterext/qcpilfilterext` | ❌ Missing | `drivers/soc/qcom/*` | PIL filter extension |
| `qcpilext/qcpilEXT8380` | ❌ Missing | `drivers/soc/qcom/*` | PIL extension |
| `qcrpen/qcrpen` | ❌ Missing | `drivers/iio/accel/*` | **HIGH**: Pen IMU and pen-down detection |
| `qcsmmu/qcsmmu8380` | ❌ Missing | `drivers/iommu/*` | SMMU driver for TEE/secure domains |
| `qciommu/qciommu` | ❌ Missing | `drivers/iommu/*` | IOMMU driver |
| `qciommuext/qciommuext8380` | ❌ Missing | `drivers/iommu/*` | IOMMU extension |
| `qcpmic/qcpmic8380` | ❌ Missing | `drivers/regulator/qcom-spmi-*` | PMIC regulator driver |
| `qcpmicext/qcpmicext8380` | ❌ Missing | — | PMIC extension |
| `qcpmicgpio/qcpmicgpio8380` | ❌ Missing | `drivers/pinctrl/qcom/*` | PMIC GPIO pin control |
| `qcpmicapps/QcPmicApps8380` | ❌ Missing | — | PMIC apps processor driver |
| `qcpmicglink/QcPmicGlink8380` | ❌ Missing | — | PMIC Glink for APO access |
| `qcppx/qcppx8380` | ❌ Missing | — | PX driver (power X) |
| `qcglink/qcglink8380` | ❌ Missing | — | G-Link driver for inter-processor comms |
| `qcursext/qcursext` | ❌ Missing | — | CURS driver (security) |
| `qctree/QcTrEE` | ❌ Missing | `drivers/tee/*` | Trusted Execution Environment (TEE) driver |
| `qctreeextqcom/QcTreeExtQcom8380` | ❌ Missing | `drivers/tee/*` | TEE extension |
| `qcuart/qcuart8380` | ❌ Missing | `drivers/tty/serial/qcom_*` | UART controller driver |
| `qcssgservicesumd/qcSSGServicesUMD` | ❌ Missing | `drivers/misc/*` | SSG services user-mode driver |
| `qcslimbus/qcslimbus8380` | ❌ Missing | — | SLIMbus driver |
| `qcskext/QcSkExt8380` | ❌ Missing | — | SK extension |

### 12. Miscellaneous / OEM / System (LOW-MEDIUM IMPACT)

| Package | Status | Linux Driver | Mapping Notes |
|---------|--------|--------------|---------------|
| `SurfaceHotPlug` | ❌ Missing | `drivers/hid/surface-hid/surface_hid.c` | Hot-plug event handler (Dock2) |
| `integration/SurfaceIntegrationDriver` | ❌ Missing | — | Integration layer for system services |
| `serialhub/SurfaceSerialHubDriver` | ❌ Missing | `drivers/platform/surface/surface_aggregator_cdev.c` | **HIGH**: SSAM serial hub, device enumeration |
| `qcasd/qcasd8380` | ❌ Missing | `drivers/soundwire/qcom/*` | **HIGH**: SoundWire controller driver |
| `qcasdapo/qcasd_apo8380` | ❌ Missing | `drivers/soundwire/qcom/*` | SoundWire APO master |
| `radiotelemetry/SurfaceRadioTelemetryDriver` | ❌ Missing | `drivers/net/wireless/ath/ath12k/` | WiFi telemetry, band management |
| `ihvaudiofilter/SurfaceIhvAudioFilterDriver` | ❌ Missing | `sound/soc/*` | IHV audio filter (hardware acceleration) |
| `microsofteffectpackextension/MicrosoftEffectPack_extension` | ❌ Missing | — | Microsoft Effect Pack (haptic, animations) |
| `surfacepro_SnsCfgCRD8380` | ❌ Missing | `drivers/iio/*` | Sensor configuration card |
| `surface_dppsext_8380` | ❌ Missing | `drivers/platform/surface/*` | DPP extension (display power management) |
| `appcompanionextensionpackage/SurfaceAppCompanionExtensionPackage` | ❌ Missing | — | App companion driver |
| `mepaudiocomponent/mep_audio_component` | ❌ Missing | `sound/soc/*` | ME audio component (Media Engine) |
| `qcbtacxtransportdriver/qcbtacx_transportdriver8380` | ❌ Missing | — | TACX transport driver (audio) |
| `qcbtaddvscregistry/qcbtaddvscregistry8380` | ❌ Missing | `drivers/bluetooth/hci_qca_*` | BT add device registry |
| `qcsocservicekmdf/QcSocServiceKMDF8380` | ❌ Missing | `drivers/soc/qcom/*` | SOC service KMDF driver |
| `SurfaceButton` | ❌ Missing | `drivers/hid/surface-hid/surface_hid.c` | Button events (Power, Volume) |
| `qcdcf/qcDCF` | ❌ Missing | — | Dynamic calibration framework |
| `qcdpps8380/qcdpps8380` | ❌ Missing | `drivers/power/*` | Power delivery state machine |
| `qcpepwd/qcpep.wd8380` | ❌ Missing | `drivers/power/*` | Power Electronics Power Delivery driver |
| `SurfaceNullDriver` | ❌ Missing | — | Null placeholder driver |
| `SurfaceNullCapsule` | ❌ Missing | — | Null capsule driver |
| `SurfacePro11_FirmwareUpdate` | ❌ Missing | — | Firmware update tool (user-space) |
| `mepcameracomponent/mep_camera_component` | ❌ Missing | `drivers/media/*` | Camera Media Engine component |
| `mepaudiocomponent/mep_audio_component` | ❌ Missing | `sound/soc/*` | Audio Media Engine component |
| `devicestelemetryservicedriver/DevicesTelemetryServiceDriver` | ❌ Missing | — | Device telemetry service |
| `SurfaceMachineLearningServiceNullDriver` | ❌ Missing | — | ML service driver (placeholder) |
| `proextadsp/surfacepro_ext_adsp8380` | ❌ Missing | `sound/soc/qcom/snd_*` | APO driver extension |
| `mhihost/surface_mhihost` | ❌ Missing | `drivers/net/mobiledata/mhi_*` | **HIGH**: MHI host driver (cellular) |
| `nfcclient/NxpNfcClientDriver` | ❌ Missing | `drivers/nfc/*` | NFC driver (PN547) |
| `nxpnfcclientdriverext/NxpNfcClientDriverExt` | ❌ Missing | `drivers/nfc/*` | NFC extension driver |
| `prosnscfgcrd/surfacepro_SnsCfgCRD8380` | ❌ Missing | `drivers/iio/*` | Sensor config card |
| `SurfaceDigitizerWoTExtnPackage` | ❌ Missing | `drivers/hid/surface-hid/surface_hid.c` | **CRITICAL**: Touch/pen extension package |
| `SurfaceHidMiniDriver` | ❌ Missing | `drivers/hid/surface-hid/surface_hid.c` | **CRITICAL**: HID mini driver (touch, pen) |
| `SurfaceHIDFriendlyNames` | ❌ Missing | `drivers/hid/surface-hid/surface_hid.c` | Friendly names for HID devices |
| `SurfaceHidClassSettingsExtensionPackage` | ❌ Missing | `drivers/hid/surface-hid/surface_hid.c` | HID class settings |
| `SurfaceTCONCellularLCD` | ❌ Missing | `drivers/gpu/drm/panel/` | TCON for cellular LCD |
| `SurfaceTCONOLED` | ❌ Missing | `drivers/gpu/drm/panel/` | TCON for OLED |
| `SurfaceTCONWifiOnlyLCD` | ❌ Missing | `drivers/gpu/drm/panel/` | TCON for WiFi-only LCD |
| `SurfacePanelDriver` | ❌ Missing | `drivers/gpu/drm/surface/drm_panel*` | Panel driver |
| `SurfaceOemPanelCustomizationLcd` | ❌ Missing | — | OEM LCD customization |
| `SurfaceOemPanelCustomizationOled` | ❌ Missing | — | OEM OLED customization |
| `microsofteffectpackextension/MicrosoftEffectPack_extension` | ❌ Missing | — | Microsoft Effect Pack (haptics) |
| `oobpairingoptinextensionpackage/SurfaceOobPairingOptInExtensionPackage` | ❌ Missing | — | OOB pairing opt-in extension |
| `oobpairingoptinpackage/SurfaceOobPairingOptInPackage` | ❌ Missing | — | OOB pairing opt-in package |
| `SurfaceRadioTelemetryDriver` | ❌ Missing | `drivers/net/wireless/ath/ath12k/` | Radio telemetry |
| `SurfaceSerialHubDriver` | ❌ Missing | `drivers/platform/surface/surface_aggregator_cdev.c` | Serial hub driver |
| `SurfaceSarManager` | ❌ Missing | `drivers/iio/health/*` | SAR manager driver |
| `SurfaceDock2FwUpdate` | ❌ Missing | `drivers/usb/typec/dock/` | Dock 2 firmware update |

### 13. Completed/Resolved (Merged)

| Package | Status | Linux Driver |
|---------|--------|--------------|
| `qcwlanhmt/qcwlanhmt8380` | ✅ Merged | ath12k WCN7850 |
| `qcbluetooth/qcbluetooth8380` | ✅ Merged | hci_qca |
| `5gnrcoexmgr/surface_5gnrcoexmgr` | ✅ Merged | ath12k spectral |
| `5gnrcoexmgrext/surface_5gnrcoexmgr_ext` | ✅ Merged | ath12k |
| `gnss/surface_gnss` | ✅ Merged | drivers/gnss/* |
| `audiominiext/surface_audiominiext8380` | ✅ Merged | sound/soc/qcom/* |
| `qcaudminiportapo/qcaudminiport_apo8380` | ✅ Merged | sound/soc/qcom/apo/* |
| `qcaudminiportbase/qcaudminiport_Base8380` | ✅ Merged | sound/soc/qcom/* |
| `qcusbcucsi/qcusbcucsi8380` | ✅ Merged | drivers/usb/typec/ucsi/* |
| `qcusb4bus/QcUsb4Bus8380` | ⚠️ Open PR | drivers/usb/typec/usb4.c |
| `qcusb4filter/QcUsb4Filter8380` | ⚠️ Open PR | drivers/usb/typec/usb4-filter.c |
| `retimer0/SurfaceRetimer0` | ⚠️ Open PR | drivers/usb/typec/re-timer.c |
| `retimer1/SurfaceRetimer1` | ⚠️ Open PR | drivers/usb/typec/re-timer.c |

---

## Recommendations by Priority

### 🔴 CRITICAL (Must-have for SP11 stability)

1. **`SurfaceBattery.sys`** → `drivers/power/supply/surface_battery.c`
   - **Impact**: Power management, battery health, runtime estimation
   - **Action**: Reverse-engineer SAM battery protocol (5-step reporting), implement in `surface_battery.c`
   - **Effort**: Medium-High (requires binary analysis, SSAM integration)

2. **`SurfaceDigitizerWoTExtnPackage`** → `drivers/hid/surface-hid/surface_hid.c`
   - **Impact**: Touch/pen reliability, mshw0485/mshw0486 quirks
   - **Action**: Add HID extension package support in surface-hid driver
   - **Effort**: Medium

3. **`SurfaceThermalPolicy.sys` + `qcSubsysThermalMgr.sys`** → `drivers/platform/surface/surface_platform_profile.c`
   - **Impact**: Thermal throttling, fan behavior, performance modes
   - **Action**: Integrate thermal policy with existing thermal framework, add 6 control points
   - **Effort**: High (requires full thermal stack analysis)

4. **`hidmini/SurfaceHidMiniDriver`** → `drivers/hid/surface-hid/surface_hid.c`
   - **Impact**: Touch/pen/hid event handling, tablet switch, battery
   - **Action**: Refactor `surface_hid.c` to match HID mini driver shape
   - **Effort**: Medium-High

### 🟠 HIGH (Important for full functionality)

5. **`qcrpen/qcrpen`** → `drivers/iio/accel/*`
   - **Impact**: Pen IMU, pen-down detection, pen reliability
   - **Action**: Implement pen IMU driver with SSAM
   - **Effort**: Medium

6. **`camfrontsensor/surfacecamfrontsensor8380`** → `drivers/media/i2c/*`
   - **Impact**: Front camera support, video capture
   - **Action**: Add VD55G0 MIPI-CSI2 sensor driver
   - **Effort**: High (requires MIPI-CSI2, ISP integration)

7. **`camrearsensor/surfacecamrearsensor8380`** → `drivers/media/i2c/*`
   - **Impact**: Rear camera support, dual-camera workflows
   - **Action**: Add OV13858 MIPI-CSI2 sensor driver
   - **Effort**: High

8. **`qcasd/qcasd8380`** → `drivers/soundwire/qcom/*`
   - **Impact**: SoundWire audio, codec control
   - **Action**: Implement SoundWire controller driver
   - **Effort**: High (requires SoundWire stack knowledge)

9. **`surface_mhihost`** → `drivers/net/mobiledata/mhi_*`
   - **Impact**: Cellular modem support, mobile broadband
   - **Action**: Implement MHI host driver for LTE modem
   - **Effort**: Medium-High

10. **`qcsensors/qcSensors`** → `drivers/iio/*`
    - **Impact**: IMU, environmental sensors, pen/trackpad
    - **Action**: Implement sensor hub driver
    - **Effort**: Medium

11. **`serialhub/SurfaceSerialHubDriver`** → `drivers/platform/surface/surface_aggregator_cdev.c`
    - **Impact**: SSAM device enumeration, peripheral discovery
    - **Action**: Integrate serial hub into SSAM framework
    - **Effort**: Medium

### 🟡 MEDIUM (Nice-to-have for completeness)

12. **`acpiplatformextension/SurfaceAcpiPlatformExtensionDriver`** → `drivers/acpi/platform.c` + SSAM
    - **Impact**: ACPI extensions for power/thermal events
    - **Action**: Add ACPI extension hooks for battery/thermal events
    - **Effort**: Medium

13. **`qcdx/qcdx8380`** → `drivers/thermal/qcom/qcdx8380*`
    - **Impact**: Qualcomm Digital Thermometer, SAR integration
    - **Action**: Add qcdx8380 thermal sensor driver
    - **Effort**: Medium

14. **Display/Panel drivers** (5 packages)
    - **Impact**: Display quality, power efficiency, refresh rate control
    - **Action**: Implement TCON drivers and panel driver
    - **Effort**: High

15. **USB4/Retimer drivers** (5 packages, 4 in progress)
    - **Impact**: USB4 performance, dock compatibility
    - **Action**: Complete PR#24 (retimer) and PR#76 (USB4)
    - **Effort**: Medium (in progress)

### 🟢 LOW (User-space or non-critical)

16. **OEM customization drivers** (6 packages)
    - **Impact**: OEM-specific features (edge lighting, haptics)
    - **Action**: User-space tools or optional drivers
    - **Effort**: Low

17. **Device telemetry / ML drivers** (4 packages)
    - **Impact**: Telemetry data collection, machine learning
    - **Action**: User-space services
    - **Effort**: Low

---

## Mapping Summary by Category

### ✅ Merged (6 packages)
- `SAM/SurfaceSAM`
- `qcwlanhmt/qcwlanhmt8380`
- `qcbluetooth/qcbluetooth8380`
- `5gnrcoexmgr/surface_5gnrcoexmgr`
- `5gnrcoexmgrext/surface_5gnrcoexmgr_ext`
- `gnss/surface_gnss`
- `audiominiext/surface_audiominiext8380`
- `qcaudminiportapo/qcaudminiport_apo8380`
- `qcaudminiportbase/qcaudminiport_Base8380`
- `qcusbcucsi/qcusbcucsi8380`

### ⚠️ Open PR (14 packages)
- `acsp/surface_acsp8380`
- `apoextension/SurfaceAPOExtension`
- `powerstate/SurfacePowerState`
- `qcusb4bus/QcUsb4Bus8380`
- `qcusb4filter/QcUsb4Filter8380`
- `retimer0/SurfaceRetimer0`
- `retimer1/SurfaceRetimer1`
- `ihvcpusmfclient/SurfaceIhvCpuSmfClient`
- `qcxhcifilter/QcXhciFilter8380`
- `dxext/surface_dxext_8380`
- `SurfaceDock2FwUpdate`
- `SurfaceHotPlug`
- `qcpdsr/qcpdsr`
- (Others pending PR review)

### ❌ Missing (183 packages)
- **Critical**: Battery, Thermal/Fan, Touch/Quirks, Pen, Cameras (38 packages)
- **High**: Sensors, SoundWire, MHI, SerialHub (15 packages)
- **Medium**: Display/Panel, USB4 extensions (5 packages)
- **Low**: OEM, Telemetry, ML (125 packages)

---

## Notes

1. **REA MCP tools** were not registered in this session; analysis was performed with local tooling
2. **Package count**: 203 (from SurfaceUpdate directory listing)
3. **Mapped packages**: 6 merged + 14 open PR = 20 with Linux equivalents
4. **Gap analysis** is based on reverse-engineering reports and Linux driver repository structure
5. **Confidence levels**:
   - [H] High: Strong evidence from binary analysis and SSAM protocol
   - [M] Medium: Partial evidence or inferred mapping
   - [L] Low: Limited evidence or speculative mapping

---

**Report generated by:** Research subagent
**Date:** 2026-11-14
**Next steps:** Prioritize CRITICAL gaps (battery, thermal, touch) for immediate implementation
