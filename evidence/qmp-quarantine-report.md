# QMP Combo Overlay Quarantine Scheme — Reconstruction Report

Source of truth: `git show 507356d03f3c -- drivers/phy/qualcomm/phy-qcom-qmp-combo.c`
(commit `507356d03f3c0984aa8110fbc3cf7c7d52a33ed9`, "feat(qcom): preserve
experimental Type-C display recovery source", 2026-10-20; diff 1338 lines,
blob `2ebaa16a1f7e..f50f91db00e3`). This report was regenerated from that
diff directly; the prior agent's mapping was used only as a checklist and
every claim below was re-verified against the diff text.

Scope: analysis only. No code edits, no git operations, no builds.

---

## 1. The overlay scheme (as seen in the diff)

The commit replaces ad-hoc `init_count`/error-goto handling in
`phy-qcom-qmp-combo.c` with an explicit **common-resource ownership state
machine** layered on top of (not replacing) the generic-PHY reference
counts.

### 1.1 State (`struct qmp_combo_common_state`, diff ~lines 42–54)

| Field | Values | Meaning |
|---|---|---|
| `regulators` | `QMP_COM_REG_NONE / HELD / UNKNOWN` | Bulk vreg consumer ownership; `UNKNOWN` = a bulk op failed, per-vreg state indeterminate |
| `resets` | `QMP_COM_RESET_UNKNOWN / ASSERTED / DEASSERTED` | Bulk reset state; `UNKNOWN` = bulk assert/deassert failed mid-flight |
| `clocks_held`, `pipe_held` | bool | Bulk core clocks and the pipe clk are prepared+enabled |
| `configured` | bool | Common registers programmed (`qmp_combo_program_common_locked`) |
| `autonomous` | bool | USB sub-PHY left in autonomous (runtime-suspend) mode |
| `runtime_suspended` | bool | Common resources released for runtime PM |
| `last_error`, `quarantine_error`, `cleanup_error` | int | Latch: last failure, sticky quarantine, failure during release path |

The diff's own comment (line 41) is important semantics: the enum tracks
**successful consumer API references, not measured electrical state**, and
(line 51) "Bulk rollback errors cannot establish consumer ownership
reliably" — that is the entire justification for the `UNKNOWN` tri-state.

`struct qmp_combo` gains `common`, plus requested-vs-applied split
(`common_mode`/`common_orientation` requested vs `applied_mode`/
`applied_orientation`/`applied_valid` committed last), `usb_active`,
`dp_active`, `dp_aux_active`.

**Confidence: high.** Read directly from the diff.

### 1.2 Quarantine entry (`qmp_combo_common_quarantine_locked`, diff ~lines 239–260)

First error wins for `quarantine_error`; first cleanup failure wins for
`cleanup_error`; `last_error` records the most recent. Entry sites:

- **Acquire** (`qmp_combo_common_acquire_locked`): on
  `regulator_bulk_enable` failure → `regulators = UNKNOWN`, quarantine with
  `cleanup = false` — **still-owned resources are deliberately kept held**
  (you cannot know which vregs came up). Same for bulk assert and bulk
  deassert failures → `resets = UNKNOWN`, no cleanup.
- **Release** (`qmp_combo_common_release_locked`): bulk assert failure →
  `resets = UNKNOWN`, quarantine with `cleanup = true` — comment "Failed
  quiescence: keep still-owned clocks and supplies." Bulk
  `regulator_bulk_disable` failure → `regulators = UNKNOWN`, quarantine.
- Defensive: release on `regulators == UNKNOWN` → quarantine(-EIO, true);
  `WARN_ON_ONCE(clocks_held||pipe_held)` when regulators are NONE →
  quarantine(-EIO, true).

**Confidence: high.** All four entry sites verified in diff lines
~283–321 and ~337–367.

### 1.3 Failed-acquire unwind order (`unwind:` label, diff lines 396–404)

```
unwind:
	/* Preserve the failed-acquire bulk clocks -> reset -> supplies order. */
	qmp_combo_common_put_clocks_locked(qmp);      /* clk_bulk + pipe_clk */
	cleanup = qmp_combo_common_release_locked(qmp); /* reset assert, then regulators */
	if (cleanup && !state->cleanup_error) state->cleanup_error = cleanup;
	state->last_error = ret;
	return cleanup ?: ret;
```

Clocks are released first, then resets are asserted, then regulators are
disabled — i.e. the exact reverse of the acquire order
(regulators → assert → deassert → bulk clks → pipe clk). This matches the
*old* code's goto ladder (`err_disable_clocks → err_assert_reset →
err_disable_regulators`) which the diff deletes from
`qmp_combo_com_init`, but now it is state-tracked rather than blind.

**Confidence: high.** Verified at diff lines 346–404.

### 1.4 Sticky quarantine gate

- `qmp_combo_common_acquire_locked` returns `state->quarantine_error`
  immediately (diff line 337–338). Acquire **refuses** while quarantined.
- `qmp_combo_common_release_locked` also returns it first (line 285–286),
  **except** the quarantine-entry paths themselves; the only way to clear
  quarantine in this design is a full successful release to
  `regulators == NONE` — visible via the release path reaching
  `state->regulators = QMP_COM_REG_NONE` (line 321). Note the diff never
  explicitly zeroes `quarantine_error` after successful release; the
  acquire path rechecks it, so within this frozen source the quarantine is
  effectively **sticky for the lifetime of the bind** unless release-side
  clearing is added. (The handoff's "needs full release to NONE" is the
  design intent; the frozen diff itself does not implement the clearing
  write. Flagged below as a porting consideration.)

**Confidence: high** for the gate reads; **medium** for the
clearing-on-release claim (absent in the frozen diff; the handoff summary
asserts the intent).

### 1.5 USB init timeout: quiesce, don't drop pipe clk

Old code: `goto err_disable_pipe_clk; clk_disable_unprepare(qmp->pipe_clk)`.
New `qmp_combo_usb_power_on_locked` (diff lines 794–815):

```
/* A timeout must quiesce the partial PHY, not release common's clock. */
qmp->usb_active = true;   /* set before first partial programming */
...
if (ret) { /* PHY_INIT_COMPLETE_TIMEOUT */
	cleanup = qmp_combo_usb_power_off_locked(qmp);
	if (cleanup) return cleanup;
}
```

The sub-PHY power-off (SW_PWRDN write) quiesces the partial init while the
pipe clk — now a **common** resource owned by the state machine, also
needed for USB2 in DP-only mode (line 378) — stays held. `usb_active` is
set optimistically *before* programming so a failed power-on still counts
as "was active" for the quiesce path.

**Confidence: high.** Diff lines 790–815.

### 1.6 Runtime suspend −EBUSY while quarantined

`qmp_combo_runtime_suspend` (diff lines 939–984): under `phy_mutex`, if
`quarantine_error` → `-EBUSY` ("Callback success permits genpd to remove
power: unknown is not off"). Also `-EBUSY` when
`!configured || !applied_valid` but clocks/regulators still held;
`-EIO` when ready-check fails; on success enables autonomous mode if
`usb_active`, puts clocks, sets `runtime_suspended`.

**Confidence: high.**

### 1.7 Type-C mux/switch: pending-intent cache + −EBUSY

The diff deletes the fire-and-forget `qmpphy_mode = new_mode` mux path and
the `printk`-laden switch path, replacing both with
`qmp_combo_typec_request(kind, orientation, mode)`:

- `qmp_combo_typec_cache_request_locked` records the *requested*
  orientation/mode first (pending intent),
- `qmp_combo_typec_reconfigure_locked` refuses with `-EBUSY` while
  `dp_powered_on && qmpphy_mode != applied_mode` ("Do not change mode
  underneath a retained logical DP power demand"),
- full sequence power_off(usb) → power_off(dp) → common_release →
  common_acquire → re-power per `usb_init_count`/`dp_init_count`/
  `dp_powered_on`, then `qmp_combo_commit_applied_locked` **commits
  applied_* last** ("Component initialization can fail after common
  acquisition: commit last"),
- `qmp_combo_typec_abort_locked` powers down and quarantines on failure,
  returning `quarantine_error ?: cause`.

**Confidence: high.** Diff lines 1040–1280.

---

## 2. Windows cross-check (from prior binary evidence)

Per the prior agent's disassembly work (recorded here as received; binary
content was treated as data only):

- `retimer0/1` are **firmware capsules** — no driver logic to compare
  against [observed]. Consistent with `evidence/retimer-report.md` being
  firmware-focused.
- `qctreeextqcom` is HDCP data, not a PHY ownership protocol [observed].
- The only driver binary with ownership-transition logic is
  **`qcusbcucsi8380.sys`**. All ownership-transition facts below are
  **[inferred]** from `evidence/ucsi-typec-report.md` §2.5 (the UCS0.bin
  AeoB resource-vote table): the Windows PPM arbitrates PMIC LDO votes
  (LDO1/2/3/8/14/15 classes, CLK3_A/CLK6_A), GCC usb30/usb3-phy clocks
  including `gcc_usb3_prim/sec_phy_{pipe,aux,com_aux}_clk`, USB GDSC
  footswitches, NoC/ICB votes, and GPIO reset-pulse trains with 10 µs/1 µs
  delay records around QMP PHY reset pulses [observed payload bytes;
  derived units; inferred semantics].

Cross-check conclusion: Windows performs the same class of sequencing
(vregs → reset pulses with µs-scale delays → GCC clocks incl. pipe clk →
GDSC) that the overlay's acquire order encodes, but as a declarative
resource table under a PPM, not as error-latching driver code. The
quarantine scheme's "keep unknown-owned resources held on bulk failure"
has no Windows counterpart visible in the binaries — Windows simply votes
whole FSTATEs. **Confidence: medium** (inference across §2.5's table).

---

## 3. Bounded-port recommendation (core deliverable)

Target file: `drivers/phy/qualcomm/phy-qcom-qmp-combo.c` on the **current
base** (post-#24, post-#81 "tunnel-clock-from-mode-register"). The current
base already has `com_initialized` (bool), `runtime_suspended` (bool),
`usb4_tunnel_clk_active` (bool, sampled from the mode register in
`qmp_combo_com_init` at lines ~4342–4351 and consulted in `com_exit` at
~4476–4489), `usb_init_count`, `dp_init_count`, `dp_powered_on`, and a
goto-based error ladder at lines 4442–4446
(`err_disable_clocks → err_assert_reset → err_disable_regulators`).
Current function map (verified by grep on the working tree):

| Item | Current-base location |
|---|---|
| `qmp_combo_com_init` | line 4269 (force/refcount entry ~4288–4290; tunnel-clk sample ~4342–4351; ladder 4442–4446) |
| `qmp_combo_com_exit` | line 4456 (tunnel-clock warning path 4476–4489; `com_initialized = false` at 4500) |
| `qmp_combo_dp_power_on/off` | 4551 / ~4596 |
| `qmp_combo_usb_power_on` | 4631 |
| typec mux/switch set | ~4900–4940 region (uses `dp_powered_on`, `dp_init_count`) |
| `qmp_combo_runtime_suspend/resume` | 5129 / ~5173 |

### Port ONLY these four elements

**(a) `qmp_combo_common_state` ownership tracking + `quarantine_error`.**
Add the struct (regulators/reset enums, `clocks_held`, `pipe_held`,
`last_error`, `quarantine_error`, `cleanup_error` — the `configured`/
`autonomous`/`runtime_suspended` booleans can be dropped since the base
already tracks `com_initialized` and `runtime_suspended`). Map:
`com_init` success path (line ~4438 `com_initialized = true`) also sets
regulators=HELD, resets=DEASSERTED, clocks_held/pipe_held=true;
`com_exit` success sets NONE/ASSERTED/false/false. **Confidence: high.**

**(b) `quarantine_locked` at the existing error paths.** In current
`qmp_combo_com_init`: `regulator_bulk_enable` failure (goto
`err_disable_regulators`, line 4316/4322) and the
`reset_control_bulk_deassert` failure (line 4327) become quarantine-entry
points with `regulators = UNKNOWN` / `resets = UNKNOWN` respectively —
keeping still-owned resources held, exactly as diff lines 346–367 do. In
`qmp_combo_com_exit`: bulk assert / bulk disable failures quarantine with
`cleanup = true` (diff lines 301–321). **Confidence: high.**

**(c) Failed-acquire unwind ordering.** The base's ladder
(4442–4446) already unwinds clocks → reset assert → regulators in the
right order; the port only adds the state transitions and
`cleanup_error` latching at each step (diff lines 396–404). **Confidence:
high.**

**(d) Sticky-quarantine gate in `com_init`.** First thing after the
`force`/refcount logic in `com_init` (~line 4290): if
`common.quarantine_error`, return it — mirroring diff lines 337–338.
**Critical #81 interaction — must verify:** the tunnel-clock sample at
4342–4351 runs *after* the acquire steps in `com_init`. The gate must sit
*before* the tunnel-clock read so a quarantined PHY never re-samples and
re-latches `usb4_tunnel_clk_active` from the mode register. Conversely,
on a successful release (`com_exit`), the base's existing
`usb4_tunnel_clk_active = false` at 4489 is the correct place to also
clear the quarantine latch (diff-verified gap, see §1.4). **Confidence:
high** on placement; **medium** on the exact mode-register read ordering
until a code-level re-check of lines 4288–4351 is done during the port.

### EXCLUDE from the port (agreed bounding)

- **Full `prepare`/`commit_ref` restructure** (`common_prepare_locked`,
  applied_*/commit-last machinery): touches every phy op and the typec
  path; not needed for quarantine semantics.
- **Type-C pending-intent caching**
  (`qmp_combo_typec_request`/`cache_request_locked`/`reconfigure_locked`):
  replaces the whole mux/switch path; independent risk.
- **DP calibrate rework** (`qmp_combo_dp_calibrate` with
  pm_runtime + operational checks, diff lines ~745–770).

These are faithfully documented in §1 for future reference but should not
ride the bounded port.

---

## 4. Caveats

1. The frozen diff never clears `quarantine_error` after a successful
   full release; the "full release to NONE clears quarantine" behavior in
   the handoff is design intent, not diff-verified code. The port must
   add the clearing write in `com_exit`'s success path.
2. Current-base line numbers come from grep on the working tree at report
   time; they will drift with any rebase.
3. Windows ownership-transition facts are [inferred] from
   `evidence/ucsi-typec-report.md` §2.5; no Windows binary was re-analyzed
   for this report (prior agent's disassembly relied upon).
4. `tools.rea` MCP was not used; no binary tools were needed for this
   diff-based task.

**Overall confidence: high** for §1 (all claims verified against the
regenerated diff), **high** for §3(a–c), **medium** for the §3(d)
gate-vs-tunnel-clock ordering detail pending one code-level re-check.
