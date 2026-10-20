# Native USB4 source R5 — validation supplement, 17 October 2026

This is experimental source validation, **not a working USB4 release**. Do not
load or activate the Qualcomm host-router frontend. Its explicit DT/sysfs
activation paths remain reachable; the prohibition is operational, not a
compile-time interlock. This work does not change the accepted v25/v26 kernels.

This dated note supplements the immutable R5 source archive. The archive's
README correctly records the object-only evidence available when it was
created; the subsequent module checks below do not rewrite that history.

## Source identity and delivery

The complete base is `e02a1a495958175c63c65643c4cc8ab80647330f` from
[the kernel fork](https://github.com/ooaklee/linux_ms_dev_kit-sp11).
The local corrected tip is `bae6318b004835cfcbf60fff75e80f1902d5c315`, tree
`5c525eaf0627d3cd1af9d8e93bac684932765c8f`. The local tip is not claimed to be
publicly retrievable. Seven included email patches reconstruct its six changed
files from the exact base. This is not a standalone or DKMS driver.

The source-only archive is `sp11-native-usb4-source-20261017-r5.tar.gz`,
88,024 bytes, SHA-256:

```text
e24c557cd5bc3f02288141ee2309f1b1066ca881630f60b0b59cf62cebfca94b
```

It contains six complete changed source files, seven email patches, both base
licence texts, a README, manifest and checksum list. The first six emails and
licences remain byte-identical to R4. A minimal six-file replay preserved author
metadata, raw dates, complete messages and stable patch IDs for all seven commits
and matched the final snapshots. That is not a full-tree replay claim. An
independent in-memory archive reconstruction matched every byte.

No Windows executable, firmware payload, recovered pseudocode, captured table,
private log, compiled module or signing key is included. Compiling this
experimental source does not require Windows or the MSI. Firmware sourcing,
authenticity and the physical activation protocol remain unresolved requirements.

## Correction and completed checks

R5 validates the entire cold-load firmware container before that branch halts
the UC or writes payloads. It checks complete headers and payloads using
remaining-byte arithmetic, retains destination/count limits, rejects empty or
trailing incomplete input and unaligned destinations, and reads little-endian
words without assuming input alignment. Record order and overlaps are retained.
Earlier resource preparation and register reads are outside this guarantee.

The exact parser/bring-up functions passed 90 host cases and 130,436 assertions
in each optimized and ASan/UBSan mode, with ten named inverse controls rejected.
Full ordered reference comparison covers aligned inputs; other byte alignments
have payload/counter oracles. One observed sample passed the pure validator in
memory without MMIO. These do not authenticate firmware or define all vendor
formats.

The later native ARM64 check freshly compiled `qcom_usb4_hr.o` with GCC 13.3.0,
binutils 2.42 and `W=1`, ran normal in-tree MODPOST, and linked both unsigned
modules. No new-module diagnostics were captured. OpenCode reviewed the
prepared procedure; the completed evidence was separately inspected and audited.

The configuration was unchanged `ubuntu_x1e_defconfig`, SHA-256:

```text
a3cdca096dabe73dc9c2b060722d1744733e42bf808a7e0c3edd3dca4363acec
```

This is the prototype release **`7.2.0`**, not the accepted v26 `7.2.5` ABI.
Built-in objects were reused, not freshly rebuilt: all 4,196 object hashes and
current GENKSYMS command hashes were checked before/after. The original baseline
did not individually pin those command bytes. All 15,895 regenerated five-field
symbol rows matched its captured table after MODPOST; that captured table was
never imported or copied as a build input. The original built-in pass's 1,148
diagnostics remain historical evidence, not a warning-free whole-kernel claim.

Both final AArch64 ELF modules passed basic and extended version checks, strong
import/provider resolution, namespace/GPL checks and dependency comparison.
`nhi_remove`'s final export metadata matches CRC `0x8aa954cf`, GPL flag 1 and
empty namespace. This narrow check does not establish relocation correctness or
module loadability.

- `qcom_usb4_hr.ko`: 61,144 bytes, 69 versioned imports, dependency `thunderbolt`.
  Compared with R4, only `_dev_err` was removed; no imports were added.
- `thunderbolt.ko`: 868,344 bytes, 234 versioned imports; unchanged module bytes.

No signing, installation, loading, device activation or hardware test occurred.
The temporary private key in the original build cache was neither opened nor
copied into the new output. The modules remain private validation artifacts,
not an installation bundle.

## Reproduce a build only

Obtain a disposable complete checkout of the exact base and apply all seven
archive patches in order, following its checksum/application instructions.
On native ARM64 Linux with the normal kernel prerequisites:

```sh
make O=../usb4-r5-check ARCH=arm64 ubuntu_x1e_defconfig
sha256sum ../usb4-r5-check/.config
make O=../usb4-r5-check ARCH=arm64 W=1 -j2 vmlinux_o
make O=../usb4-r5-check ARCH=arm64 W=1 V=1 -j2 \
  drivers/thunderbolt/thunderbolt.ko drivers/thunderbolt/qcom_usb4_hr.ko
```

Compare the configuration digest with the one above before continuing. A
different compiler or prerequisite set can change it; stop and review that
delta instead of claiming identical validation. A fresh built-in build can
generate default temporary signing material: keep it private and out of any
archive. Do not import another kernel's `Module.symvers` or use suppression
flags to bypass module checks. These commands do not install the results, and
the results must not be loaded into an unrelated installed kernel.

## Next functional work

Static investigation establishes a missing event-channel contract, not a safe
register sequence to copy. A running UC does not by itself establish a valid
event-buffer location, bounded layout or interrupt readiness. Native work must
validate resource-relative offsets, entry/header extents, producer/consumer
indices and lifetimes, including a fresh-instance warm start.

The inspected Windows path can publish event state and continue after its
positive timeout result. Linux should retain negative timeout failure instead
of adopting that behavior. The inspected PCIe-interrupt selector is mutable
software policy, not board identity; native support must not depend on Windows
registry values. Decompiler output alone incorrectly suggested a constant
selector, which the actual instructions and output-pointer calls contradict.

Physical resource/MMIO ownership, IRQ acknowledgement and masking, event-memory
coherency, DMA/work quiescence, powered dependencies, stop/idle/restart and
recovery still require implementation and qualification. The inherited arming
sequence still contains unaligned word accesses; guessed address substitutions
are not a justified repair. Suspend/resume, working USB4 and full Windows/Linux
feature parity remain unproven.
