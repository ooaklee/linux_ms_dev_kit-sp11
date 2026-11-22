# VD55G0 zero frames — CSIPHY D-PHY parity with the Windows stack

Date: 2026-11-20
Kernel: `7.3.3-jg-1sp11v2` (`debian.qcom-x1e/changelog`, tag
`v7.3.3-jg-1sp11v2`)
Driver: `drivers/phy/qualcomm/phy-qcom-mipi-csi2-3ph-dphy.c`
Windows reference: `qccammipicsi8380.sys` (Surface Pro 11 camera stack),
static analysis in `../sp11-windows-capture/` (`CameraMIPIPHY.c`,
`cam_csiphy_2_1_2_hwreg.h`).

## Why

`ae3800910` established that CSID0 receives no MIPI traffic from the VD55G0
while the sensor itself clocks, streams and programs OIF_CTRL correctly
(`1sp11v14`). The D-PHY therefore never produced a bit stream. The Windows
driver for the same silicon was decoded to get the authoritative start
sequence, which differs from the imported GPL 2.1.2 table in five places.

## Changes in this build

| # | Change | Register level |
|---|---|---|
| 1 | Pulse the per-lane reset lines after the common reset | lane block `+0x18`: clock lane `0x0e18` first, then `0x0018/0x0418/0x0818/0x0c18`; write 1, 100 µs, write 0 |
| 2 | Gate the reset on the reference generator | poll `0x10fc` bit 7 for up to 100 ms (warn only), common reset pulse shortened to 20 µs |
| 3 | Program all four data-lane blocks | the vendor replays the whole 105-entry table; the unused-lane skip is gone |
| 4 | Load the GPL 2.1.2 common-control values | `0x102c..0x1054` = `ff fe e6 df df fc fb 9b 7f bf ff` instead of zero (these are not mask-all-interrupts registers) |
| 5 | Wait for lane ready | poll `0xc4/0x4c4/0x8c4/0xcc4/0xec4` until all zero, 1 ms interval, ~100 ms budget |
| 6 | Settle count in vendor units | `((1000/mbps)*6+85)/2.5-10`: `0x1e` at 384 Mbps (`0x1d` at 420 Mbps) instead of `0x13` |

Changes 1, 2, 5 and 6 are functional gaps; 3 and 4 are reversions of
earlier deviations from the working Windows stack.

## How to read the result

```
dmesg | grep -iE "csiphy|camss|csid|vd55"
```

Expected progression for a fixed link:

1. no `D-PHY lanes did not report ready` warning from
   `phy_qcom_mipi_csi2_lanes_enable()`;
2. no `reference generator not ready after 100 ms` warning from
   `phy_qcom_mipi_csi2_reset()`;
3. CSID reports SOF/EOF (`camss` debug or `/sys/kernel/debug/media`) and the
   receiver no longer reports `stream off` with zero frames.

If the lane-ready wait times out but CSID still sees SOF, change 5 is not the
blocker and the wait can be dropped independently.

## Bisect points

Each step is a separate commit on top of `52812090b`:

- `4fa423324` fix D-PHY stream-on for per-lane configs (from 7.3.0 line)
- `0fe077a70` reset every lane block during D-PHY reset
- `cff704277` derive the settle count in vendor units
- `a03521569` program all lanes and wait for lane ready

Reverting `a03521569` alone puts the table filtering, the zeroed
common-control writes and the missing ready wait back; `cff704277` alone
restores the timer-period based settle count.
