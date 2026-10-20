# Native USB4 resource bounds — 17 October 2026

This is a source-only correction to the experimental Qualcomm host-router
frontend, **not working or hardware-qualified USB4 support**. Do not load or
activate it. Existing explicit DT/sysfs activation paths remain reachable;
this warning is not a compile-time interlock. Accepted v25/v26 sources and the
Surface installation are unchanged.

## Correction and source identity

The local source commit is `7620268bebf4c7984691707e32effbd0778b25bf`, following
R5 commit `bae6318b004835cfcbf60fff75e80f1902d5c315`. Only
`drivers/thunderbolt/qcom_usb4_hr.c` changes. Its SHA-256 is:

```text
a505501e30dfe5b22c7cf3bfd2fa5f1d57d327055a302b6c81d281ec369c8ae8
```

The firmware parser limits destinations to 56 KiB, but the old probe accepted
a successfully mapped UC RAM resource smaller than that. Other register and
diagnostic accesses also assumed minimum extents. Probe now validates every
named range before mapping any of them or entering power/PHY/firmware setup.
Missing, reversed, inclusive-size-wrapped and undersized ranges fail.
Mapping errors retain their error codes and device-managed cleanup.

The nonzero minimum extents cover the existing frontend accesses:

| Named resource | Minimum bytes |
| --- | ---: |
| `router` | `0x20ac` |
| `router_config` | `0x1c` |
| `port_group` | `0x68` |
| `sideband` | `0x40` |
| `uc_ram` | `0xe000` |
| `uc_per` | `0x1004` |
| `usbap_config` | `0x18` |
| `nhi` | `0x40000` |

The NHI value conservatively retains the complete described Qualcomm aperture;
it is not a derived minimum for every NHI implementation. Unused named ranges
remain required with a representable nonzero size. Larger UC RAM resources
do not enlarge the firmware parser's fixed destination limit. The complete
firmware validator and bring-up functions remain byte-identical to R5.

Logical bounds do not prove physical ownership, powered access or alignment
safety. In particular, this does not repair or authorize the inherited
optional unaligned interrupt-arming accesses or raw PHY/mailbox experiments.

## Validation actually completed

The exact production resource declaration, table and mapping helper were
extracted into a host fixture. Each optimized and ASan/UBSan run passed 97
named cases and 6,703 assertions. Tests cover names/order, exact and short
extents, larger resources, missing and reversed ranges, full-width wrap,
mapping failures and validation before any map. Removing the minimum-size
guard failed the independent UC RAM extent oracle in both modes.

The actual probe's early error-return ordering was checked statically, not
executed as a complete platform lifecycle. The fixture does not simulate
physical MMIO or device-managed cleanup. Earlier firmware tests were not
rerun or presented as new whole-driver coverage.

A fresh native ARM64 `W=1` object compilation passed with GCC 13.3.0 and
binutils 2.42, using unchanged `ubuntu_x1e_defconfig`:

```text
a3cdca096dabe73dc9c2b060722d1744733e42bf808a7e0c3edd3dca4363acec
```

The 56,832-byte AArch64 ELF relocatable object has SHA-256:

```text
d36db78b50f6222fb36aaad21283ff880c0e73d2befb7c12b479b3db42ae905e
```

The compiler ran non-root with fresh output and read-only source. The full
source carrier and six overlays were checked before/after; no compiler
diagnostics were captured. OpenCode reviewed the source and prepared procedure;
completed artifacts received a separate read-only audit.

This is the prototype **7.2.0** configuration, not the installed v26 7.2.5 ABI.
The new object adds `devm_ioremap_resource` and
`platform_get_resource_byname`, replacing
`devm_platform_ioremap_resource_byname`. These are undefined object symbols,
not newly verified module imports. R5's later
[module validation](native-usb4-r5-validation.md) does not apply automatically
to this changed object. New MODPOST, module linking, packaging, loadability
and hardware qualification remain outstanding.

## Source-only companion delivery

The immutable R5 archive remains unchanged. Apply the companion email patch
after its seven patches in a disposable complete checkout of the exact base:

`sp11-native-usb4-resource-bounds-20261017.patch` — 4,834 bytes, SHA-256:

```text
b211dd75be346927bb0bc3b9ab2e64f86d7ef5f75f28d15efb50d19effd8b0cd
```

A matching `.patch.sha256` sidecar is supplied. The patch was applied to a
fresh minimal repository containing all six exact R5 snapshots. All six final
files, author/raw date/complete message and stable patch ID matched the new
source. That is a minimal source replay, not a full-tree replay. The local
commit is not claimed publicly retrievable, and this note does not publish
the patch or archive remotely.

This is not a standalone or DKMS driver. Use the complete exact kernel base
and matching build prerequisites; no Windows files are needed for compilation.
No Windows binaries, firmware, recovered pseudocode, private captures,
compiled modules or signing keys are part of the companion patch. Firmware
sourcing/authenticity remains a separate unresolved dependency.

## Next functional requirement

Static analysis now distinguishes the configuration command from event-ring
initialization and traces notification scheduling. The observed counter lock
does not establish that firmware ring entries remain stable while consumed.
Linux still needs a verified producer/consumer and acknowledgement contract,
resource ownership, safe warm-start reconstruction and physical stop/idle.
Do not infer USB4, dock, standby or complete feature parity from these checks.
