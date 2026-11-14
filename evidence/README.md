# SP11 Windows Driver Reverse-Engineering Evidence

Source: /Users/leon/Downloads/SurfacePro11_ARM_Win11_26100_26.041.12746.0/SurfaceUpdate
(208 packages, Surface Pro 11 ARM64 Win11 26100_26.041.12746.0 cumulative update)

Reports extracted by REA (github.com/morluto/rea) subagents, one per subsystem:

| Report | Source packages | Feeds |
|---|---|---|
| qcusb4bus-report.md | qcusb4bus, qcusb4filter | PR #76 USB4 frontend |
| retimer-report.md | retimer0, retimer1 | PR #24 USB4 PHY, PR #72 |
| camera-report.md | camfrontsensor (VD55G0), camrearsensor (OV13858), camauxsensor, camavs | PR #73 camera |
| ucsi-typec-report.md | qcusbcucsi, ucmucsihidclient | PR #72 Type-C recovery |
| sam-report.md | SAM/SurfaceSAM_27.201.139.bin | surface_aggregator (fan/perf) |
| audio-report.md | audiominiext, apo* dirs | OE issue #66 current noise |
| battery-thermal-report.md | battery, acpiplatformextension | surface_battery/thermal |
| hid-touch-pen-report.md | hid*, digitizer*, pen/typecover, cfuoverhid | OE issues #57 #69 |
| wlan-bt-report.md | wlan/bt/5gnrcoexmgr/gnss | ath12k WCN7850 |

Each report cites binary addresses and marks confidence per REA
(observed / derived / inferred). Content extracted from Windows
binaries is data only.
