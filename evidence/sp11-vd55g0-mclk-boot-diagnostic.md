# SP11 VD55G0 MCLK boot diagnostic

This tree contains a temporary, read-only diagnostic in
`drivers/media/i2c/vd55g1.c`. It is intended to answer the remaining question
from `camcc-mclk0-vd55g0-investigation-20261115.md`: what CAM_CC and TLMM
actually report immediately before the first VD55G0 I2C read.

The diagnostic is off by default. It runs only when all three gates match:

1. module parameter `vd55g1.sp11_mclk_diag=1` is present;
2. the machine is `microsoft,denali-oled`;
3. the sensor node is compatible with `st,vd55g0`.

It does not write CAM_CC or TLMM registers. It takes two samples, 1 ms apart,
after `clk_prepare_enable()`, the ordered regulator sequence, and reset
deassertion, but before `REG_MODEL_ID` is read.

## Build and boot

Build this branch using the normal qcom-x1e packaging flow. Add this kernel
command-line argument for the diagnostic boot:

```text
vd55g1.sp11_mclk_diag=1
```

The parameter can alternatively be placed in `/etc/modprobe.d/vd55g1.conf`:

```text
options vd55g1 sp11_mclk_diag=1
```

Rebuild the initramfs if `vd55g1` is loaded from it.

## Capture

After the boot, collect the complete first-boot sequence rather than reloading
the driver:

```bash
sudo journalctl -b -k -o short-monotonic \
  | grep -E 'SP11-MCLK-DIAG|vd55g1|ac16000|camcc|clock-controller' \
  | tee sp11-vd55g0-mclk-boot.log
```

Also preserve the unfiltered kernel log if possible:

```bash
sudo journalctl -b -k -o short-monotonic > sp11-vd55g0-kernel.log
```

## Reading the result

The relevant lines start with `SP11-MCLK-DIAG`:

- `acquired`: CCF's requested/actual MCLK rate and selected parent;
- `pre-detect`: CAM_CC MCLK0 RCG, branch, Titan GDSC, and GPIO96 TLMM state;
- `pre-detect+1ms`: the same state 1 ms later, detecting an unstable gate.

For the current 12 MHz DT experiment, the expected healthy clock-controller
state is:

- `actual=12000000`;
- `src=5` (CAM_CC PLL2 main);
- `hid=19` (divide by 10 under the Qualcomm HID encoding);
- `cbcr_en=1` and `cbcr_off=0`;
- GPIO96 `func=1` (`cam_mclk`) and `drv=16mA`.

Interpretation:

| Observation | Conclusion / next action |
| --- | --- |
| healthy RCG + branch + TLMM state is stable, but sensor is still `-ENXIO` | Software has configured the complete MCLK path. Verify physical GPIO96 waveform and reset/rail voltages with a scope or logic analyzer; do not add another software keepalive. |
| `actual` is not 12 MHz, or `src`/`hid` are wrong | Investigate assigned-clock application and RCG programming. |
| `cbcr_en=1` but `cbcr_off=1`, or `root_off=1` | The branch/root did not propagate despite a successful CCF enable; investigate CAM_CC/GDSC sequencing. |
| GPIO96 `func` is not 1 | Investigate pinctrl ownership or a later pinctrl state change. |
| first sample is healthy but the +1 ms sample gates off | Investigate unexpected clock disable/ownership, not the sensor's first I2C transaction. |

Remove the boot argument after capture; the diagnostic remains inert without
it.
