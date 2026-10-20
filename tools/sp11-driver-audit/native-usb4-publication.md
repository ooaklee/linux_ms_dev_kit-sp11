# Experimental native USB4 source publication — 20 October 2026

This branch publishes preserved experimental source for review. It is **not
working or hardware-qualified USB4, an installation bundle, or a new kernel
release**. Do not load or activate it. Existing explicit DT/sysfs activation
paths remain reachable; this warning is not a compile-time interlock.

## Source and attribution

The code-only tip is `817d46464705ffdcd0f5c13837e61ac38eab795b`, based on
`537d1ac612258a650add1ea0fe2b276906b5f83b`. It preserves ten original Justin
White commits unchanged and replays eight Leon Silcott corrections with their
authors, raw author dates, complete messages, trailers and stable patch IDs
unchanged. The resulting code diff covers 40 paths: PHY/bindings/DTS groundwork,
shared NHI lifetime and the experimental Qualcomm host-router frontend.

The complete source-tree comparison against the retained R6 prototype differs
only by omission of 28 experiment-note, firmware-capture and machine-specific
configuration paths. The commits carrying those files are not ancestors of
this export. Neither their blobs nor the old private history are needed to
build this branch. Original local refs and evidence were not rewritten.

`drivers/thunderbolt/qcom_usb4_hr.c` has SHA-256:

```text
a505501e30dfe5b22c7cf3bfd2fa5f1d57d327055a302b6c81d281ec369c8ae8
```

Kernel `COPYING`, component SPDX identifiers and contributor notices remain
unchanged. The host-router frontend is GPL-2.0-only. No Windows executable,
firmware payload, recovered pseudocode, captured ACPI table, compiled module
or signing key is included in this source publication. Compilation uses the
complete kernel source and ordinary Linux build prerequisites, not Windows.
Obtaining authentic firmware for any future live use remains unresolved.

## Previously completed module validation

Subsequent validation closed the earlier R6 object-only module gate for this
exact host-router source. A native ARM64 `W=1` HR compilation, normal strict
MODPOST and both unsigned final module links passed. Same-run strong-import
providers, basic/extended CRCs, GPL, namespace and dependency checks passed.
The saved 48-file evidence inventory was rehashed before this publication;
no new compilation or test execution is claimed here.

The prepared configuration was prototype **7.2.0**, SHA-256:

```text
a3cdca096dabe73dc9c2b060722d1744733e42bf808a7e0c3edd3dca4363acec
```

The check reused 4,196 built-in objects. Their 1,148 historical diagnostics
were not rerun or resolved. The original baseline did not individually pin
historical command bytes; later before/after checks and semantic CRC/provider
comparisons do not recreate that missing provenance. This was not a fresh
whole-kernel build. Compiler/binutils were GCC 13.3.0 / binutils 2.42; new
module diagnostics were empty.

| Saved unsigned module | Bytes | SHA-256 |
| --- | ---: | --- |
| `qcom_usb4_hr.ko` | 62,840 | `c68cd65873a2273815c9c2f961461f1fb77b203c0eb8cd6c947109b364f5f68f` |
| `thunderbolt.ko` | 868,344 | `a0e397ac7182cf6bb4545519d2ec69a481873c7ec17588c85f1b018bc7edf7e5` |

These are evidence identities, not downloadable or installable artifacts in
this branch. No compatibility with the 7.2.5 v26/v27 kernels, package production,
module signing, loadability or physical USB4 operation follows from them.

## Remaining qualification and relationships

Physical MMIO/resource ownership, powered dependencies, firmware event
producer/consumer and acknowledgement contracts, IRQ handling, DMA/work
quiescence, stop/idle/restart and recovery remain unqualified. The inherited
experimental arming sequence includes unaligned accesses; publishing its
source does not justify executing it. No dock, suspend/resume, live firmware
protocol or complete Surface parity claim is made.

This branch intentionally preserves the prototype rather than rebasing it onto
a tested 7.2.5 candidate. It overlaps the older
[USB4 groundwork PR #24](https://github.com/ooaklee/linux_ms_dev_kit-sp11/pull/24)
and must not be merged alongside that stack as if the changes were independent.
The separate [7.2.5 PIPE/NHI port #71](https://github.com/ooaklee/linux_ms_dev_kit-sp11/pull/71)
and [Type-C/display recovery #72](https://github.com/ooaklee/linux_ms_dev_kit-sp11/pull/72)
remain separate reviewed candidates; this source export does not supersede
their target-ABI gates or change accepted kernels/default branches.

Track native research in [PR #74](https://github.com/ooaklee/linux_ms_dev_kit-sp11/pull/74).
GLINK channel ownership/crossed-OPEN work remains deferred and is not resumed
or repaired by this publication. Keep this PR draft until its code scope is
split or rebased, final target ABI/package checks pass, and the required
hardware/lifecycle contracts are independently qualified.
