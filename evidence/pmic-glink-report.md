# Qualcomm PMIC GLINK / altmode — Windows vs Linux (PR #83/#84) evidence report

Source binaries (Surface Pro 11 / Windows 11 26100, package 26.041.12746.0):

| File | Role |
|---|---|
| `SurfaceUpdate/qcpmicglink/qcpmicglink8380.sys` | Qualcomm PMIC GLINK host driver (KMDF/DMF, RPE client library). PDB path `Z:\b\WP\pmicGlink\rel\10.9\PmicGlink\build\ARM64\Release\qcpmicglink8380.pdb` **[observed]** |
| `SurfaceUpdate/qcpmic/qcpmic8380.sys`, `QcPmicApps8380.sys`, `qcpmicext`, `qcpmicgpio` | SPMI/PMIC-arbiter side; no pmic_glink altmode logic found (no `PMIC_RTR` strings) **[observed]** |
| `SurfaceUpdate/qcglink/qcglink8380.sys`, `qcgpi`, `qcipcrouter` | Underlying glink/gpi transports (orthogonal to this comparison) |

Analysis method: REA MCP was **not registered in this session** (searched; same
limitation noted in `evidence/ucsi-typec-report.md`). Tools used:
`/opt/homebrew/opt/llvm/bin/llvm-objdump -d --no-show-raw-insn`, `strings`,
plus manual PE section mapping (image base `0x140000000`; sections
`.text` VA 0x140001000 size 0x27dd0, `.rdata` VA 0x140029000). Disassembly
cached at `$TMPDIR/opencode/pmicglink.asm` (61847 lines). Binary content is
data only.

Confidence legend: **[observed]** = directly read at cited address;
**[derived]** = concluded from disassembly structure; **[inferred]** = reasoned.

Linux comparators (read via `git show`, no checkout):
- PR #83 `x/pr72-pmic-glink-generation` @ `7a82074f8776` — `drivers/soc/qcom/pmic_glink.c`, `include/linux/soc/qcom/pmic_glink.h`
- PR #84 `x/pr72-altmode-port` @ `c45dda932c93` — `drivers/soc/qcom/pmic_glink_altmode.c`

---

## 1. Channel / owner / message-header parity

| Fact | Windows evidence | Confidence |
|---|---|---|
| Windows opens the same rpmsg channels Linux binds (`PMIC_RTR_ADSP_APPS`) | string @ `0x140027eb8`; used in RPE init struct built at `0x140003bc0`–`0x140003bf4` (adrp 0x140027000 + #0xeb8) | observed |
| Secondary log channel `PMIC_LOGS_ADSP_APPS` | string @ `0x140027ee8`; 16-byte name copied into request buffer at `0x1400093a8`–`0x1400093cc` (memcpy `0x140025bf8`) | observed |
| `lpass` ADSP remote proc tag | string @ `0x140027eb0` | observed |
| Receive dispatch header layout `{u32 owner; u32 type; u32 opcode}` — owner from `[msg]`, opcode from `[msg,#0xc]`, type checked `cmp w24,#0x2` (PMIC_GLINK_NOTIFY=2) | dispatcher `0x140002040`–`0x140002060` | derived |
| Owner IDs **0x800a=32778 (BATTMGR), 0x800b=32779 (USBC), 0x800c=32780 (USBC_PAN)** encoded as immediates in the dispatcher | `mov w8,#0x800a` @ `0x14000208c`; `#0x800b` @ `0x14000223c`; `#0x800c` @ `0x140002400` | observed |
| USBC_PAN branch handles opcode **0x11 (17)** — matches Linux `ALTMODE_PAN_ACK` | `mov w3,#0x11` @ `0x14000243c` on the `w21==0x800c` path | observed (constant) / derived (opcode match) |
| Adjacent trace/message IDs 0x18 (24), 0x19 (25), 0x1b (27) appear on USBC/batt paths @ `0x140002448`, `0x1400023e4`, `0x140002274` — consistent with Linux opcodes `USBC_NOTIFY_IND=0x16`, `USBC_CMD_WRITE_REQ=0x15` appearing nearby in message-construction code, but exact opcode-by-opcode tracing was not completed | addresses above | inferred |

**Mapping:** identical wire protocol on SP11's ADSP route (Linux
`pmic_glink_adsp_data` → PDR `tms/servreg` / `msm/adsp/charger_pd`,
rpmsg id `PMIC_RTR_ADSP_APPS`, `driver_data=true`). PR #83/#84 constants
(`PMIC_GLINK_OWNER_*`, hdr layout, `ALTMODE_PAN_ACK=0x11`) are confirmed
correct against the Windows binary. **No mismatch.**

## 2. Client-state generation semantics after service restart

Windows behavior (qcpmicglink8380.sys, "RPE" client library):

- Client registration is a table of callbacks: `_RpeClientInitInternal`
  requires `AppsStateNotificationCallback`,
  `GetFailureRecordCallback`, `GetMemoryRegionCallback`, `CommandCallback`,
  `SubsystemCheckStateCallback` (assert strings `0x1400284e8`–`0x1400286a8`)
  **[observed]**. `SubsystemCheckStateCallback` implies Windows re-queries
  remote state validity at each state change rather than trusting cached
  state **[derived]**.
- Restart detection is **interface Arrival/Removal** driven:
  `RPEHelperEvtNotifyInterfaceChange` (`0x140028920`) with workitems keyed
  `Arrival` (`0x140028960`) / `Removal` (`0x1400289a0`), feeding
  `SendRPEClientStatusToNotifier` (`0x1400289a8`) which pushes new client
  status to registered notifiers (`RpeRegisterForStateNotification`,
  `0x140028838`; a client cannot register for itself, `0x140028858`)
  **[observed strings; derived call flow]**.
- **No numeric generation counter was found** in the disassembled
  dispatcher/state paths. Windows' stale-state protection is structural:
  on Removal the io-target and channel are torn down (`_openRpenIoTarget`,
  `0x140028ac0`) and re-registered on Arrival, so in-flight requests fail
  against a dead target and clients re-derive state from the
  `AppsStateNotificationCallback` + fresh `SubsystemCheckStateCallback`
  **[inferred]**.

**Mapping to PR #83:** Linux cannot tear down the rpmsg endpoint on ADSP SSR
(the endpoint persists), so PR #83 substitutes an explicit
`atomic64_t state_generation` incremented in
`pmic_glink_state_notify_clients()` (`pmic_glink.c` ~line 300) on every
client-state transition, with `pmic_glink_get_generation()` snapshot and
`pmic_glink_send_generation()` rejecting (`-ECONNRESET`) sends that straddle
a transition. This is a **stronger, equivalent-intent** guard: Windows
achieves the same by failing sends on a removed target. **No mismatch; Linux
is more conservative.** One nuance: the Windows callback contract pairs every
state notification with a *state re-check callback*; PR #83's token only
invalidates, it does not refresh — acceptable since Linux clients
re-register/re-scan (altmode PAN_EN re-enable in PR #84) on each UP
notification **[inferred]**.

## 3. Altmode event FIFO / ack semantics

Windows evidence for the PAN (USB-C altmode) service is thin in this package:
the USBC_PAN owner path in qcpmicglink8380.sys is a *transport* client; the
actual policy lives in ADSP firmware. Observable host-side facts:

- Single synchronous request/ack structure: the 0x800c branch treats
  opcode 0x11 as the ack/completion trigger for an outstanding PAN request
  (`0x140002400`–`0x140002444`) **[derived]** — matching PR #84's single
  `pan_ack` completion with one in-flight `USBC_CMD_WRITE_REQ` (ack carries
  no request identifier) **[observed in PR #84 code]**
- No host-side event queue deeper than the glink rx ring is visible; Windows
  relies on the transport's per-channel FIFO (qcglink) **[inferred]**.

**Mapping to PR #84:** PR #84 adds a bounded per-port kfifo
(`PMIC_GLINK_MAX_EVENTS=64`), drop-with-report on overflow (never overwrite),
per-event generation tagging with stale-generation discard at apply time, and
one-ack-at-a-time WRITE_REQ serialization. Windows gives no evidence of
host-side replay or coalescing, so Linux's explicit FIFO is a superset
designed for the weaker Linux restart model. **No mismatch.** The
"never invent ACK/replay semantics on rejection" choice matches the Windows
observation that acks are un-correlated with requests **[inferred]**.

## 4. HPD state transitions

- No HPD-specific logic exists in any qcpmic* host binary examined
  (`hpd`/`altmode` strings absent from qcpmicglink/qcpmic/QcPmicApps)
  **[observed]**. HPD bits (`hpd_state`/`hpd_irq` in
  `usbc_sc8280x_dp_data`) are firmware-generated payload; host relays them.
- PR #84 behavior: per-port `hpd_connected` last-published tracking under
  `lifecycle_lock`; on service transition every port is forced
  `connector_status_disconnected` before reopening the `reconciled` barrier
  (`pmic_glink_altmode_enable_worker`, enable-work sequence at
  `0x140002400`-analog in Linux file lines ~658–694) **[observed in PR #84]**.
- Windows analogue: on Removal, `SendRPEClientStatusToNotifier` pushes a
  down status which consumers treat as "no display path" — the connector
  deasserts when the ADSP client is torn down **[inferred]**.

**Mapping: consistent.** PR #84's force-disconnect-on-transition is the
correct Linux-side reproduction of Windows' teardown-driven HPD drop.

## 5. Mismatch flags vs PR #83/#84

| # | Item | Status |
|---|---|---|
| M1 | Owner IDs, hdr layout, NOTIFY type, PAN_ACK opcode 0x11, channel names | Match — confirmed observed |
| M2 | Generation counter vs Windows Arrival/Removal teardown | No Windows counter exists; PR #83 design is a Linux-appropriate equivalent, stricter. Not a mismatch |
| M3 | 64-deep host kfifo | No Windows equivalent observable; bounded-drop is safe superset. Not a mismatch |
| M4 | Exact mapping of Windows trace IDs 0x18/0x19/0x1b to Linux opcodes 0x15/0x16 | Unresolved (inferred only); wire-level opcode confirmation beyond 0x11 was not completed within analysis budget |
| M5 | SP11 uses ADSP route (`PMIC_RTR_ADSP_APPS`, PDR-available) — Linux must use `qcom,pmic-glink` ADSP data, not SOCCP | Confirmed observed; PR branches retain both |

## Commands run (summary)

```
git show x/pr72-pmic-glink-generation:drivers/soc/qcom/pmic_glink.c
git show x/pr72-pmic-glink-generation:include/linux/soc/qcom/pmic_glink.h
git show x/pr72-altmode-port:drivers/soc/qcom/pmic_glink_altmode.c
ls SurfaceUpdate/{qcpmic*,qcglink,qcgpi,qcaucd,qcusbcucsi,ucmucsihidclient}
strings + /opt/homebrew/opt/llvm/bin/llvm-objdump -d --no-show-raw-insn \
  qcpmicglink8380.sys QcPmicApps8380.sys qcpmic8380.sys
python3 PE section mapping (file offset <-> VA) + adrp/add xref resolution
```

No git mutations were performed; branches were only read via `git show`.

---

ROUTE: omni-route/zai/glm-5.3
DELEGATION: none
CONSENSUS: NOT_REQUIRED
