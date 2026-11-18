# UCSI recovery ladder — reset escalation, debounce constants, and merge-gap coverage

Companion to [ucsi-typec-report.md](ucsi-typec-report.md). Binaries analyzed:
`qcusbcucsi8380.sys` 1.0.4338.2200 and `SurfaceUcmUcsiHidClient.sys` 5.76.1169.0
(ARM64 PE32+, image base 0x140000000). Existing disassemblies:
`qc_ucsi.asm` (77k lines) and `hidc.asm` (56k lines). Confidence legend as in the
main report: **[observed]** / **[derived]** / **[inferred]**. Binary content treated
strictly as data.

---

## 1. Callers of the reset routine at 0x140021088 and the escalation order

All call sites of `bl 0x140021088` in qc_ucsi.asm **[observed, grep]**:

| Call site | Context | Reset args (w1=port, w2, w3, w4) | Interpretation |
|---|---|---|---|
| `0x140023d4c` | per-port op-failure path (WPP line 0xa9/0xaa region, `0x140023b88-0x140023d78`) | invoked after command helper `0x140022e78` returns failure | per-command recovery after a UCSI op fails on one port **[derived]** |
| `0x140023e58` | same function, second arm | compares per-port state fields (+0x3f, +0x36 slots) against saved copies on the stack; if they drifted, reset that port | connector-state-drift recovery **[derived]** |
| `0x140023ec4` | same function, third arm | same slot-compare pattern with opposite operand order | second drift comparator, same recovery **[derived]** |
| `0x140050588` | per-port loop around WPP line 0x194, `0x1400504e8-0x14005058c`; iterates connector index (`x22`), reads per-port state array at +0x3f | `w2=2` when a per-port status slot is nonzero and not already `2` | event/notification-triggered per-port reset, guarded against re-entry **[derived]** |
| `0x140050750` | 3-iteration per-port loop (`cmp w25, #0x3; b.lo` at `0x140050754-0x140050764`), i.e. loops over the 3 connectors of SP11 UCS0 | `w2=2, w3=0, w4=0`, gated on a stack flag byte (`sp+0x10`, cleared per port) and slot state `!= 2` | teardown / surprise-remove: broadcast soft reset to all ports with pending connector work **[derived]** |

Key gating pattern at every call site **[observed]**:

```
cmp   w27, #0x0
ccmp  w8, #0x2, #0x4, eq   ; slot state == 2 means "already reset/in-flight"
b.eq  skip_reset
```

i.e. the callers suppress a reset when the per-port slot (`ctx + 0x3f*4` /
`+0x195*4` region) already holds state 2. This is a **per-port reset-in-progress
guard**: the routine at `0x140021088` is never allowed to interleave with itself
on the same connector **[derived]**.

Escalation order, combining call sites with the ladder inside `0x140021088`
(§2 below): command failure on a port → per-port reset (`w2=2`) from
`0x140023d4c/e58/ec4`; asynchronous connector-notification drift → per-port
reset from `0x140050588`; device-level teardown → sequential per-port resets
from `0x140050750` followed by full PPM re-bring-up with the 1s→15s→60s→30s
settle ladder **[derived]**.

## 2. Reset ladder inside 0x140021088/0x1400214xx (confirmed)

**[observed, constants]** (delay wrapper `0x140002720`, negative-relative
KeDelayExecution):

| Address | ms | Step |
|---|---|---|
| `0x1400215d4` | 1000 | after cmd w2=5 (WPP 74/75) |
| `0x140021630` | 1000 | WPP 75 |
| `0x14002179c` | 15000 | after helper `0x140020aa0` attempt (WPP 0x50=80 on failure) |
| `0x1400217f8` | 60000 | after `0x140027b78` call (w2=0, w3=2) |
| `0x140021818` | 30000 | final settle before WPP 0x51=81 status report |

Ladder **1 s → 15 s → 60 s → 30 s confirmed [observed]**; total worst-case
settle ≈ 107 s. Per-step fail-fast: each helper call's return is checked
(`tbz w21, #0x1f` / `csel` pattern, e.g. `0x1400217b8-0x1400217c8`) and branches
to the common failure path; no per-step retry loops exist inside the ladder
**[derived]** — same conclusion as main report §2.1.

Post-reset pending re-check: at `0x140021800` a byte flag at `ctx+0x64e` is
written before the 60 s wait; at teardown caller `0x14005079c-0x1400507a0` the
same `+0x64e` flag is re-tested (`cbnz` skips a re-reset) after a status
comparison against `0xdeb` — i.e. **after reset the per-port pending state is
re-evaluated rather than blindly re-issuing** **[derived]**. The known caller at
`0x140023d4c` sits in a function whose preamble (`0x140023b88-0x140023b9c`)
itself re-reads per-port bytes (`+0x686`, `+0x670`) and short-circuits —
consistent with a pending-flag re-check before escalation **[derived]**.

CCI handling: no dedicated CCI byte parse appears at the caller sites; CCI-level
semantics are hidden behind helper `0x140020aa0` / `0x140022e78`, which return
NTSTATUS-shaped values (`tbz w, #0x1f` sign checks) **[derived]**. WPP line
numbers 0xa9/0xaa (169/170) bracket the op-failure → reset transition in the
`0x140023d4c` caller **[observed line numbers]**.

## 3. HPD/connect debounce constants

qc_ucsi.asm small-ms immediates near timer/command setup **[observed grep]**:

| Address | Constant | Context |
|---|---|---|
| `0x140005e54-0x140005e60` | `0x3e8` | stack slot reuse, not a timer (ldr/str pair) — likely not debounce |
| `0x140021a04`, `0x140021b1c` | `0x3e8` (1000 ms) | additional 1 s waits in adjacent reset-worker functions — settle, not HPD |
| `0x140041f98`, `0x140041fb4` | `0x3e8` (1000 ms) | WPP-adjacent command timeout region |
| `0x140020d68-0x140020d74` | `w3=0x2d/0x2e/0x2f` (45/46/47) | WPP line numbers in the command helper, not debounce |

hidc.asm (SurfaceUcmUcsiHidClient) **[observed grep]**: every `0x3e8` hit is a
struct-offset load (`ldr x2, [x8, #0x3e8]`) — that is a DMF/UCSI context field
offset, **not** a time constant. Genuinely timed constants in hidc.asm:
`0x64` (100 ms) at `0x14000d198`, `0x14000dcf0`, `0x140017b9c` (as `w0`, sleep
arg) and as `w3` at `0x140015c5c`, `0x1400241bc`, `0x14002a724`, `0x14002ba28`;
`0xc8` (200 ms) at `0x140026674`; `0x1f4` (500 ms) at `0x140026a8c`.

Conclusion: **HPD/connect/disconnect debounce in the UCSI path is 100 ms /
200 ms / 500 ms in the HID client (get-connect-status / ACK retry pacing), not
in qcusbcucsi** — the Qualcomm driver only knows multi-second settle delays.
No dedicated HPD debounce constant was identified in either binary; HPD
debounce likely lives in the display-path drivers instead **[inferred]**.

## 4. Coverage matrix vs merged changes #77 and #79

Linux-side features vs. evidence in this firmware:

| Feature | Evidence in qcusbcucsi8380 | Coverage |
|---|---|---|
| #77 teardown guard (don't reset during remove) | `0x140050750` loop gates each per-port reset on a teardown flag byte and slot `!= 2` before calling `0x140021088` **[derived]** | covered by firmware; kernel driver should mirror the guard |
| #77 truncated-ACK handling | Helper `0x140020aa0` checks each command's return; WPP 0x2b on nonzero; callers fail-fast — no lenient parse of short CCI observed **[derived]** | partial: fail-fast exists, no explicit truncated-ACK repair |
| #79 reset-error propagation | `0x140023d4c/e58/ec4` escalate to per-port reset only on confirmed failure; common failure path at `0x140021424` consolidates errors **[derived]** | covered |
| #79 PS883x retry | 4×1000 ms retry loop is in **hidc.asm** (`0x14002d3e4`, `0x14002d418`) — the client, not the controller driver, owns transport-level retry **[observed, main report §2.4]** | firmware N/A for PS883x (Parade retimer is a separate device); kernel retimer driver owns this |

## 5. Bounded-fix-or-skip per gap

1. **Settle ladder** — firmware waits 1 s→15 s→60 s→30 s after PPM reset.
   Bounded fix: in the kernel UCSI glue, after `ucsi_reset`, wait a fixed
   bounded window (e.g. 1 s) before the first command and treat later
   commands with retry; **skip** replicating the 105 s worst-case settle.
   Rationale: per-step fail-fast in firmware shows the long waits are
   conservative padding, not protocol requirements **[inferred]**.
2. **Post-reset pending re-check** — firmware re-tests the `+0x64e` flag and
   per-port slots before re-issuing resets (`0x14005079c`, `0x140023b88`).
   Bounded fix: after connector reset, re-read CONNECT status (GET_CONNECTOR_STATUS)
   and only re-arm work if still changed — mirrors firmware behavior; small,
   testable. **Fix recommended.**
3. **Truncated-ACK repair** — no firmware analog found; **skip** (keep #77's
   existing defensive length checks).
4. **PS883x retry pacing** — 100/200/500 ms constants in the HID client suggest
   transport retry pacing of that magnitude; if the kernel retimer path needs a
   retry, use 100 ms initial, 500 ms cap. **Optional, only if instability is
   observed.**

---

Sources: `/private/var/folders/j2/xnbqqc716l32ds45rcln1lt80000gn/T/opencode/qc_ucsi.asm`,
`.../hidc.asm` (pre-existing llvm-objdump disassemblies); original report
[ucsi-typec-report.md](ucsi-typec-report.md). No code changes, no git changes.
