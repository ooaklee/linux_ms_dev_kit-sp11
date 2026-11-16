# SP11 VD55G0 exact-route boot diagnostic

This branch combines the Windows-decoded VD55G0 route with a temporary,
read-only clock/pad diagnostic in `drivers/media/i2c/vd55g1.c`:

- CCI0 master 0 (`cci0_i2c0`);
- seven-bit sensor address `0x60` (Windows SCFG byte `0xc0`);
- MCLK0 at the Windows-configured 19.2 MHz;
- GPIO96 selected as `cam_mclk`;
- GPIO109 reset and the Windows-matched LDO sequence already in the driver.

The route is derived in `tools/sp11-driver-audit/camera-fields.md`. The
diagnostic answers the remaining pre-I2C question: what CAM_CC and TLMM report
while MCLK0 is actively enabled immediately before the first sensor read.

## Safety and gates

The diagnostic is off by default. It runs only when all three gates match:

1. module parameter `vd55g1.sp11_mclk_diag=1` is present;
2. the machine is `microsoft,denali-oled`;
3. the sensor node is compatible with `st,vd55g0`.

It does not write CAM_CC or TLMM registers. It takes two samples, 1 ms apart,
after `clk_prepare_enable()`, the ordered regulator sequence and reset
deassertion, but before the driver's first sensor transaction (the FSM-state
poll). Normal CCF, regulator and GPIO operations are unchanged except for that
diagnostic-only 1 ms observation interval.

## Build and boot

Build this branch using the normal qcom-x1e packaging flow. The diagnostic is
a `module_param` in the built-in `vd55g1` driver, so it must be enabled on the
kernel command line (for example, through `/etc/default/grub` followed by
`sudo update-grub`):

```text
vd55g1.sp11_mclk_diag=1
```

## Capture

Collect the complete first-boot sequence rather than reloading the driver:

```bash
sudo journalctl -b -k -o short-monotonic \
  | grep -E 'SP11-MCLK-DIAG|vd55g1|ac15000|camcc|clock-controller' \
  | tee sp11-vd55g0-exact-route-boot.log
```

Also preserve the unfiltered kernel log if possible:

```bash
sudo journalctl -b -k -o short-monotonic > sp11-vd55g0-exact-route-kernel.log
```

## Reading the result

The relevant lines start with `SP11-MCLK-DIAG`:

- `acquired`: CCF's actual MCLK rate and selected-parent rate;
- `pre-first-i2c`: CAM_CC MCLK0 RCG/branch, Titan GDSC, GPIO96 MCLK,
  GPIO109 reset and regulator-enable state;
- `pre-first-i2c+1ms`: the same state 1 ms later, detecting an unstable gate.

For this Windows-exact 19.2 MHz experiment, the expected healthy state is:

- `actual=19200000`;
- `src=0` and `hid=0` (BI_TCXO, divide by one);
- `cbcr_en=1` and `cbcr_off=0`;
- `root_off=0` while the child branch is enabled;
- GPIO96 `func=1`, `drv=16mA`, `pull=0` and `oe=0`.
- GPIO109 `func=0`, `drv=2mA`, `pull=0`, `oe=1` and `out=1` (reset
  deasserted; the DT descriptor is active-low).
- `vddio=1 vcore=1 vana=1`.

`root_en=0` is not by itself a failure. On a plain Qualcomm RCG the child
branch can keep the root running through hardware feedback without leaving the
software force-enable bit set.

This register sample cannot prove an external waveform or board continuity.
If the state is healthy but address `0x60` still NAKs, the next discriminating
measurement is a scope/logic-analyzer capture of GPIO96 while these lines are
emitted, together with GPIO109 reset and the first CCI0 transaction.
