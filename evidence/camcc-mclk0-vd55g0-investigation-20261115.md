# CAM_CC_MCLK0 keepalive (PR #99) — independent investigation

Date: 2026-11-15
Scope: why `cam_cc_mclk0_clk` cannot be enabled from `camcc-x1e80100` probe
(v9 `CLK_IS_CRITICAL`, v10 probe-time `clk_prepare_enable`), and whether the
PR premise ("MCLK0 is gated while VD55G0 powers up") is supported by evidence.
Method: read-only source analysis + boot journals on the dev unit (v8, v9, v10),
no kernel changes. No code in `fix/camcc-mclk0-vd55g0-sync-state` was touched.

## TL;DR

1. The PR premise is not supported by the v8 boot. In v8 the sensor driver
   itself enabled MCLK0 successfully (`clk_prepare_enable()` returned 0, no
   branch WARN) at ~4.4 s and then failed at the *first I2C access* (-ENXIO).
   So "MCLK0 never came up" is not the residual sensor bug.
2. The 0.0978 s / 0.0991 s failures are a *provider-probe-context* limitation:
   the camera CC cannot pass a clock through the mclk branch that early, while
   its register interface still answers (and returns post-reset values). No
   ordering tweak inside camcc fixes that.
3. Nothing in tree enables a functional CC branch from its own probe. The only
   probe-time mechanism, `driver_data->clk_cbcrs`, is used for housekeeping
   clocks only (GDSC_CLK / SLEEP_CLK / DRV_AHB / DRV_XO). Every camera sensor,
   including the sibling imx681 on the same board/CC, enables its MCLK from
   `power_on()` at sensor-probe time.
4. Recommendation: drop PR #99 from the v11 test build and test PR #98
   (rail ordering) on its own. If a keepalive is ever shown to be necessary,
   do it from the consumer, or via `clk_cbcrs`, or from deferred post-boot
   work — never from camcc probe.

## 1. Does vd55g1 really have a real clock handle?

Yes. `drivers/media/i2c/vd55g1.c:3230` uses `devm_v4l2_sensor_clk_get()`
(= `devm_clk_get_optional()`), and denali DT provides
`clocks = <&camcc CAM_CC_MCLK0_CLK>` plus
`assigned-clocks` / `assigned-clock-rates = <19200000>`
(`x1e80100-microsoft-denali-oled.dts:167-169`). So it is a real provider,
not the optional-`NULL` fallback. At probe,
`vd55g1_prepare_clock_tree()` (line 3236) validates
`clk_get_rate(xclk) in [MIN,MAX]`; the v8 probe got past it, so the provider
really returned 19.2 MHz.

## 2. v8 evidence: MCLK0 *was* enabled, at 4.4 s, by the consumer

`journalctl -b -4 -k` (kernel 7.3.0-jg-1sp11v8):

```
Nov 15 14:11:19 Larimar kernel: vd55g1 0-0062: error -ENXIO: Failed to power on during probe
Nov 15 14:46:41 Larimar kernel: vd55g1 0-0062: Sensor reset failed: -110
Nov 15 14:46:41 Larimar kernel: vd55g1 0-0062: error -ETIMEDOUT: Failed to power on during probe
Nov 15 14:11:34 Larimar kernel: camcc-x1e80100 ade0000.clock-controller: sync_state() pending due to 0-0062
```

* No `cam_cc_mclk0_clk status stuck at 'off'`, no `WARN ... clk-branch.c`,
  no `Failed to enable clock` — i.e. `clk_prepare_enable(sensor->xclk)` at
  `vd55g1.c:2863` returned 0, and the CBCR halt poll passed.
* If the enable had failed, `vd55g1_power_on()` returns at line 2864-2866 and
  the probe error would be that value (`-EBUSY` = 16), not `-ENXIO` = 6 or
  `-ETIMEDOUT` = 110. Both observed errors come from the I2C/regmap path
  after the clock was enabled.
* Boot 14:11 = the *first* probe (i2c NAK at the first read after reset
  release; `vd55g1_power_on` enables xclk first, asserts reset, raises
  vddio→vcore→vana, releases reset at line 2890, then polls FSM at 2892).
  Boot 14:46 = a manual repro that got one step further (sensor ACKed, FSM
  never reached READY_TO_BOOT).

The state that motivates the PR — `cam_cc_mclk0_clk` prepare/enable counts 0,
no consumer in `clk_summary`, `sync_state() pending due to 0-0062` — is exactly
the *unwound* state after this failed probe: the `disable_clock:` label
(line 2924) unprepares xclk, and a bound-but-failed consumer stays in the
supplier's sync_state list. It is not evidence that the clock was gated while
the sensor was powered.

Also note vd55g1's own order already matches the Windows CAMI blob:
enable xclk → assert reset → vddio → vcore → vana → release reset. The
ordering the PR wants to guarantee is already guaranteed inside the consumer,
in the same probe, before any reset release.

## 3. Why the enable fails at 0.098-0.099 s

Facts from source:

* `clk_branch2_ops` (clk-branch.c:175) has enable/disable/is_enabled only; the
  halt poll is `clk_branch2_check_halt()` (line 43): success for an enable is
  `CBCR_CLK_OFF == 0` **or** `CBCR_NOC_FSM_STATUS == FSM_STATUS_ON`;
  200 x 1 us then `WARN` + `-EBUSY` (clk-branch.c:62-88).
  `CBCR_CLK_OFF = BIT(31)` (clk-branch.h:63).
* `clk_rcg2_ops` (clk-rcg2.c:801) has **no** `.enable`/`.prepare`.
  `CMD_ROOT_EN` is written only by `clk_rcg2_set_force_enable()`
  (clk-rcg2.c:1328), used by `clk_rcg2_shared_ops`. MCLK0's src uses plain
  `clk_rcg2_ops` (camcc-x1e80100.c:946), so nothing ever sets ROOT_EN.

So "RCG never enabled" is a real property of the driver, but it is *not* what
distinguishes v10 from v8: in v8 the same clock, same code path, enabled fine
at 4.4 s. Root must therefore already be enabled in hardware (post-reset
default / bootloader) by the time consumers use it.

The parent-selection theory is also refuted:

* `ftbl_cam_cc_mclk0_clk_src` contains `F(19200000, P_BI_TCXO, 1, 0, 0)` and
  `cam_cc_parent_map_1` maps `P_BI_TCXO -> 0`; 19.2 MHz is reachable directly
  from the always-on TCXO, PLL2 is not needed.
* In v10 the explicit `clk_set_rate(mclk0, 19200000)` **succeeded**: the probe
  only reports the later `-EBUSY` from `clk_prepare_enable`, and
  `update_config()` would have WARNed ("rcg didn't update its configuration")
  if `CMD_UPDATE` had not cleared.

What remains, and fits both v9 (0.0978 s) and v10 (0.0991 s), is that the
camera CC is not yet in a state where the mclk branch can propagate a clock at
CC-probe time, while its register interface still answers and returns
post-reset values (`CBCR = 0x80000000` = CLK_OFF, `CMD_RCGR = 0x0` = "update
complete"). A 200 us poll can never pass under those conditions. Both failure
timestamps are within 1.3 ms of each other across two different patches, and
the identical clock enables fine ~4 s later under the same driver — timing, not
clock-tree configuration.

Cross-check that this CC is *not* safe to poke outside a valid power-up:
with `mxc`/`mmcx` at `off-0` (`pm_genpd_summary`) I rebound
`ade0000.clock-controller` at runtime on the v10 unit. The board hung and did
not return (see "Device incident"). So the CC's functional window is real and
narrow; a probe that touches it that early is fragile by construction.

## 4. Sibling precedent / the "extra step"

* `grep clk_prepare_enable drivers/clk/qcom/camcc-*.c` → only
  `camcc-x1e80100.c` (this PR). No other CC enables a branch.
* Probe-time always-on is done with `qcom_branch_set_clk_en()` over
  `desc->driver_data->clk_cbcrs`, and only for housekeeping clocks:
  `CAM_CC_GDSC_CLK`, `CAM_CC_SLEEP_CLK` (x1e80100, glyphur, sc8180x, sm8450),
  plus `DRV_AHB_CLK`/`DRV_XO_CLK` on kaanapali. No camcc lists an MCLK.
  `CLK_IS_CRITICAL` in camcc exists only on `cam_cc_gdsc_clk` (sc7280,
  sm8250) — a clock that is already running at registration. That is why it
  works there and why it cannot work for MCLK0 (v9).
* `grep -l clk_prepare_enable drivers/media/i2c/*.c` → 82 drivers; e.g.
  `imx681.c:119` inside `imx681_power_on()`, called from `imx681_probe()`
  (line 627). imx681 is on the same board, same camcc, same 19.2 MHz
  (`CAM_CC_MCLK4_CLK`, denali dts:199-201) and uses exactly the shape vd55g1
  uses — at sensor-probe time (~4 s), not at CC-probe time.

So there is no "extra step" in the clock tree that a keepalive would supply.
The only difference is elapsed boot time after the provider is live.

## 5. Blast radius of the v10 change

`camcc` now fails probe, so `camss` (acb7000), `cci` (ac16000), `csiphy`
(ace4000/ace8000) all stay deferred (`deferred probe pending: supplier
ade0000.clock-controller not ready`), and *both* cameras are dead — the
front imx681 (MCLK4) as well as the IR vd55g0. A keepalive that cannot succeed
turns a sensor-specific bug into a subsystem-wide outage.

## Recommendation

1. **Drop PR #99 from the v11 build** and test PR #98 (rail ordering) alone.
   v8 proves MCLK0 was available; #98 is the only untested variable with a
   plausible mechanism (v8 errors: i2c NAK, then FSM timeout).
2. If a keepalive is ever proven necessary, in order of preference:
   a. keep it in the consumer (`vd55g1_power_on()` already does the right
      order, before reset release);
   b. `clk_cbcrs` raw write in `cam_cc_x1e80100_driver_data` — it does not
      poll and cannot fail the provider (but still pins the camera rail on
      forever, unlike Windows which votes MCLK0 off on DSTATE=3);
   c. deferred post-boot enable (delayed work / late initcall) with retries.
   Never from `cam_cc_x1e80100_probe()`.
3. If certainty is wanted, one build settles the mechanism: in
   `cam_cc_x1e80100_probe()` dump `0x15000` (CMD_RCGR), `0x15004`
   (CFG_RCGR) and `0x15018` (CBCR) right after `qcom_cc_probe()`, then do
   rate+enable from a delayed work 1 s later. Early dump = post-reset values
   and late enable succeeds → confirms the model above and gives the correct
   shape for any keepalive (c). Late enable also -EBUSY → no keepalive is
   deliverable from the CC driver at all; revert and rely on the consumer.

## Device incident (disclosure)

At 20:57 UTC I ran, on the v10 unit, the runtime-rebind experiment:

```
echo ade0000.clock-controller > /sys/bus/platform/drivers/camcc-x1e80100/bind
```

The SSH session was closed and the board has not answered on the ngrok tunnel
since (checked repeatedly for ~20 min). It most likely needs a physical power
cycle. Do not repeat that experiment on a live board; it was intended as a
zero-build way to separate "early-boot only" from "structural", and it did
produce a data point — touching this CC while `mxc`/`mmcx` are off wedges the
SoC — but at the cost of the test unit.
