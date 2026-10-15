# Native Bluetooth factory-provisioning source track

This is a separate native firmware-provider research track, not a new kernel
partition driver, a Windows compatibility layer or an installation procedure.
The current candidate has not been executed in EFI or on real factory storage.

## Existing Linux boundary

The kernel already has a firmware-node `local-bd-address` consumer in
`net/bluetooth/hci_sync.c`. Qualcomm's driver also has an explicit
`qcom,local-bd-address-broken` byte-order quirk. Those native interfaces provide
an integration boundary; their presence does not prove the current board's
property, byte order or installed provider is correct. Do not add or remove
that quirk by guesswork, invent a replacement factory address or start scanning
raw storage from a new Linux driver to work around unverified boot integration.

## Native source candidate

The source-only `sp11-native-dpp-source-20261015` bundle applies two focused
commits to [dtbloader](https://github.com/TravMurav/dtbloader) at
`b1bf23e88b779ac323fbcbe547cf3ba939433df3`:

1. `587ded14d0f8ada9324bd325bac9faef31696c8f`: isolate the known native RWFS
   fields in a bounded, allocation-free reader; check media extents/identity,
   exact payload lengths and read budgets; remove raw provisioning debug output.
2. `397ee3ca597084d91f1be5c82fe10129e03f43a4`: constrain Bluetooth vendor matching
   to `QCOM` through the existing Surface Pro 11 descriptor, preserving other
   boards' unconstrained vendor policy and the existing WLAN/byte-reversal path.

The reader interprets only the licensed native implementation's known fields.
Unknown versions, secondary magic, region ownership and nonzero row states
remain opaque. The first zero-present row ends traversal; duplicates are
rejected within that visited prefix. Row/byte limits are implementation policy,
not assertions about factory-format maxima. No proprietary format header,
Windows binary, recovered pseudocode or real provisioning record is included.
The original BSD-3-Clause licence and contributors' attribution are retained.

## What the checks establish

The actual standalone core passed 4,691 synthetic calls and 300,856 assertions
per optimized and sanitizer host run. The actual EFI adapter/Surface wrapper,
included with fake EFI/FDT boundaries, passed 87 scenarios and 2,295 assertions
per run. These include partial reads, failure-zero output, changed media,
vendor scoping, byte reversal and the exact row-cap/sentinel boundary.

The complete native project compiled and linked with the exact recorded
GNU-EFI/libfdt/SHA-1 submodules and Clang/LLD 18.1.3. All 85 expected objects
were independently checked as ARM64 COFF; the PE32+ image has the EFI
boot-service-driver subsystem and no import/delay-import tables. Project
sources used `-Wall -Werror`, with no warnings in the complete build log.
This build is freestanding native EFI: `aarch64-windows` selects COFF/ABI, not
a Windows runtime. The unsigned research image is not distributed in the bundle.

OpenCode's completed GLM 5.1 review found no new actionable defects in the
supplied production source and recommended source-only status. Its requested
tests were mapped to actual coverage; review imprecision was qualified rather
than used as authority. No source review or compilation proves device parity.

## Remaining deployment gates

The existing GPT-name comparison and broad/ambiguous partition selection still
need hardening. A stable media ID does not authenticate storage or rule out
in-place mutation. WLAN/Bluetooth success remains coupled, and the two FDT
updates remain sequential: a failed second update leaves the first applied.
Valid existing addresses, exact compatible-node selection, actual factory
record semantics and the installed Stubble/provider path need independent
qualification. The host adapter tests intentionally reproduce the partial-FDT
failure instead of claiming transactional publication.

No raw-storage capture, provider invocation, boot-order/Secure Boot change or
hardware radio test follows from these source checks. Those require separately
scoped decisions after the installed boot path and record ownership are known.
