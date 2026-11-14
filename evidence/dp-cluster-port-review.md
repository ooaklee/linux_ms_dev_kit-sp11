# PR #85 `x/dp-aux-owner-cluster` — DP cluster port review

- Reviewed: `x/dp-aux-owner-cluster` @ `b5df058032d7` (single commit "drm/msm/dp: port AUX ownership and display state machine from 7.2 research")
- Base: `sp11/ubuntu-qcom-x1e-7.3.y` @ `c0b027b8b8b4` (merge-base == base tip; branch is a clean fast-forward of one commit)
- Reference commits present in repo history: `7de83c21291f` and `65dc3b473bb5c` (both `git cat-file -t` → `commit`), so semantics were verified against the actual commits, not reconstructed.
- Method: read-only `git show/diff/grep`. No code edited, no merges, no builds (base commit message records a successful native arm64 build of all of `drivers/gpu/drm/msm/`; a symbol cross-check was used instead — see §4).
- REA/Windows-driver cross-check: **not used** — code evidence was sufficient for all four areas; kept within the time-box.

## Area 1 — config_ctrl RMW: ISSUE FOUND (dead store, currently masked)

**Verdict: minor issue (real defect, benign on current call paths). Confidence: high.**

Branch code (`dp_ctrl.c:513-559`, `msm_dp_ctrl_config_ctrl()`):

```c
config = 0;
/* Default-> LSCLK DIV: 1/4 LCLK  */
config |= (2 << DP_CONFIGURATION_CTRL_LSCLK_DIV_SHIFT);   /* line 521 */

/*
 * RMW: ... Clear stream-specific fields before OR-ing ...
 */
config = msm_dp_read_link(ctrl, REG_DP_CONFIGURATION_CTRL); /* line 528 — overwrites line 521 */
config &= ~(RGB_YUV_MASK | BPC_MASK | SEND_VSC);
```

Findings vs upstream `7de83c21291f`:

1. **The branch folds the link and stream paths back into one function but keeps the RMW clear of the stream-specific fields** (`RGB_YUV`, `BPC`, `SEND_VSC`) — this matches the commit's core requirement: stream fields are cleared before OR-ing, and the masks used are exactly the ones introduced by `7de83c21291f` (`dp_reg.h` `GENMASK(12,11)` / `GENMASK(9,8)`). Correct in intent.
2. **However the `2 << DP_CONFIGURATION_CTRL_LSCLK_DIV_SHIFT` default computed at line 521 is dead**: it is assigned into `config` immediately before `config` is overwritten by the read at line 528. Upstream's split avoids this because the link path (fresh `config = 0`, no read) keeps the LSCLK default while the stream path does the RMW. In the branch's merged form the LSCLK divider field is never cleared and never re-set; it relies on the register retaining whatever the last writer left. On current call paths the previous writer is either the same function (preserve OK) or hardware reset (0 → LSCLK div 0). Upstream semantics say "LSCLK DIV: 1/4 LCLK default"; the branch can only preserve that if the register already holds 2<<5.
3. Same pattern applies to the other *link-level* fields the RMW preserves (`ASSR`, `NUM_OF_LANES`, `ENHANCED_FRAMING`, `P_INTERLACED`, `STATIC_DYNAMIC_CN`, `SYNC_ASYNC_CLK`) — they are OR-ed on top of the read value rather than cleared first. Since all of these fields are unconditional-or-conditional *set* bits that the function always recomputes except LSCLK, only LSCLK is actually lost; the rest are idempotent under OR. The stream-fields clear is the only non-idempotent part and it is handled.

**Bounded fix:** move the LSCLK-div default out of the pre-read position — either (a) clear-and-set: `config &= ~DP_CONFIGURATION_CTRL_LSCLK_DIV_MASK; config |= 2 << DP_CONFIGURATION_CTRL_LSCLK_DIV_SHIFT;` after the read, or (b) revert to the upstream two-function split (`_link` / `_streams`). Minimal one-line option (a) preferred for this port. Do not change the stream-field mask set — it matches upstream exactly.

## Area 2 — atomic_create_state: CORRECT

**Verdict: correct. Confidence: high.**

- `dp_drm.c:66` (`msm_dp_bridge_ops`) and `dp_drm.c:244` (`msm_edp_bridge_ops`) both set `.atomic_create_state = drm_atomic_helper_bridge_create_state`, alongside the unchanged `.atomic_duplicate_state` / `.atomic_destroy_state` helpers — exactly the swap made by `65dc3b473bb5c` (verified in the commit's diff: `atomic_reset = drm_atomic_helper_bridge_reset` → `atomic_create_state = drm_atomic_helper_bridge_create_state` in both ops tables). No `.atomic_reset` remains anywhere under `drivers/gpu/drm/msm/` on the branch (`git grep` → empty).
- `drm_atomic_helper_bridge_create_state` exists in the base tree (`include/drm/drm_atomic_state_helper.h:108`).
- No custom `atomic_create_state`/`duplicate_state`/`reset` implementations were introduced; both tables keep the stock helpers, consistent with the base and the commit's intent.
- One *behavioral* delta worth flagging (not an API issue): the branch drops `.atomic_pre_enable` entirely (base `dp_drm.c:113` had `msm_dp_bridge_atomic_pre_enable → msm_dp_display_atomic_pre_enable`). The enable work (mode set, `pm_runtime_resume_and_get`, host init, link training) now all happens inside `msm_dp_bridge_atomic_enable` (`dp_display.c:2036+`). That is a deliberate part of the 7.2 overlay's "rewritten dp_display host lifecycle" per the commit message, and the removed `msm_dp_display_atomic_*` prototypes were dropped from `dp_display.h` in step. No dangling references.

## Area 3 — AUX ownership fault-drain paths: CORRECT (one nit-level observation)

**Verdict: correct, no missed drains or leaked locks found. Confidence: medium-high (single-reviewer static analysis; no runtime fault injection).**

Paths examined in `dp_aux.c` (branch copy) and `dp_display.c`:

- **`msm_dp_aux_transfer()` (dp_aux.c:396-560):** every exit goes through the single `exit:` label → `mutex_unlock(&aux->mutex)` then conditional `pm_runtime_put_sync` (`put_temporary`). Admission handshake: mutex dropped around `ops->admit()` (correct — owner may only `trylock` state_lock, comment at dp_aux.c:459), refcounted by `active_admissions`, `complete_all(&aux->owner_idle)` on last admission out. WARN_ON_ONCE guards contract violations and closes the gate (`enable_xfers = false`). Re-check of `owner_stopping` / `initted` / `phy_fault` / `enable_xfers` after re-acquiring the mutex covers races won by stop/disconnect while the mutex was dropped. No path skips unlock or the PM put.
- **Fault path (dp_aux.c:505-527):** PHY-calibration failure retains the fault (`phy_fault`), closes the gate, adopts the caller's PM lease (`fault_pm_held`, `put_temporary = false`), queues `fault_work` *before* dropping the mutex so a concurrent `owner_drain`'s `flush_work` cannot miss it. `goto clear_busy` keeps `cmd_busy` bookkeeping consistent.
- **`msm_dp_aux_fault_work` (dp_aux.c:60-81):** snapshot ops/data under mutex, call `ops->recover` without holding aux.mutex — recover (`msm_dp_display_aux_recover`, dp_display.c:634) takes `state_lock`, drains via `msm_dp_aux_finish_fault` under state_lock so a new admission can't race the lease flag, then puts the lease outside the lock. Lease accounted once (`puts + put_temporary`).
- **`msm_dp_aux_owner_stop`/`owner_drain` (dp_aux.c:717-736):** stop closes the gate under the mutex; drain waits `owner_idle` completion (initialized `complete_all`ed at probe, dp_aux.c:940-941, so no drain-before-first-admission hang) then `flush_work(&aux->fault_work)`. Display side (`msm_dp_display_stop_link_status`, dp_display.c:225-244) calls stop under `state_lock`, `synchronize_irq`, then drain and `cancel_work_sync` of the status works — ordering is sound (gate closed before irq sync before work flush).
- **`owner_set`/`owner_detach` (dp_aux.c:694-754):** both refuse (`-EBUSY`) while admissions, faults, or a different/still-live owner exist; detach only after `owner_stopping` — no way to strand an owner callback.
- **ISR (dp_aux.c:565+):** ignores (with warn) interrupts while `!cmd_busy`; `DP_INTR_AUX_ERROR` path clears PHY hw interrupts via `msm_dp_aux_clear_hw_interrupts`. No fault left undrained.
- **Nit (observation, not a defect):** `msm_dp_aux_transfer` returns `-EAGAIN`-shaped errors via `admit`'s `mutex_trylock` failure (dp_display.c:623-624). `drm_dp_aux` callers generally retry on `-EAGAIN`, but the DRM aux helpers can also propagate it to userspace EDID reads; on a busy state_lock this is the intended backpressure, just be aware DPCD reads may transiently fail with EAGAIN during display transitions. Upstream aux has no equivalent admission step, so this is new surface, by design.

## Area 4 — 7.2-era API leftovers: NONE FOUND

**Verdict: clean. Confidence: high (compile-independent symbol cross-check).**

Method: extracted every function-like symbol added by `git diff c0b027b8b8b4..x/dp-aux-owner-cluster` in `drivers/gpu/drm/msm/dp/` + `include/`, then verified each against the base tree. Results:

- All `msm_dp_*` symbols referenced are either pre-existing on base or defined *within the branch itself* (e.g. `msm_dp_aux_owner_set/stop/drain/detach` in `dp_aux.c:694-754`, `msm_dp_audio_restart` in `dp_audio.c:293`, `msm_dp_ctrl_has_phy_refs` in `dp_ctrl.c:2239`, `msm_dp_display_put_pm` in `dp_display.c:299`) with matching header declarations — no undefined references.
- All DRM-core APIs used exist on base: `drm_atomic_helper_bridge_create_state` (`include/drm/drm_atomic_state_helper.h:108`), `drm_connector_hdmi_audio_plugged_notify`, `drm_kms_helper_connector_hotplug_event`, `drm_edid_raw`, `drm_dp_dpcd_read_link_status`, `drm_dp_link_rate_to_bw_code`, `drm_mode_copy`, `drm_modeset_lock/unlock`, `struct drm_atomic_commit` (`include/drm/drm_atomic.h:410/440`), bridge `dp_audio_*` funcs (`include/drm/drm_bridge.h:792-802`). (An earlier bulk grep produced false "MISSING" hits from regex word-boundary issues; each was individually re-verified present on base.)
- No references remain to removed 7.2-era entry points (`msm_dp_display_atomic_pre_enable` etc. were deleted from `dp_display.h` along with their last uses).

## Bounded fix list (do NOT apply as part of review)

1. **[Area 1, should-fix]** `dp_ctrl.c:521/528` — dead store: `config |= 2 << LSCLK_DIV_SHIFT` is overwritten by the RMW read. Fix: after the read, `config &= ~LSCLK_DIV_MASK; config |= 2 << DP_CONFIGURATION_CTRL_LSCLK_DIV_SHIFT;` (add `DP_CONFIGURATION_CTRL_LSCLK_DIV_MASK GENMASK(...)` to `dp_reg.h` if absent), or split back into upstream `_link`/`_streams` per `7de83c21291f`. Keep the existing stream-field masks untouched.
2. **[Area 2, optional]** If closer parity with 7.3 base is desired later, consider restoring an `.atomic_pre_enable` split of host-init vs stream-enable; not required for correctness as ported (single `.atomic_enable` path is internally serialized by `state_lock`).
3. **[Area 3, optional documentation]** Comment at the `admit` trylock (`dp_display.c:623`) that `-EAGAIN` may surface to EDID/DPCD readers during display transitions, so userspace-visible behavior is documented.

No other changes recommended. No 7.2-era API debris to remove.

---
DELEGATION: gpt (this file authored by the delegated review sub-agent itself; no further sub-delegation)
CONSENSUS: NOT_REQUIRED (independent GPT peer review not run; report is self-contained with git-object evidence)
