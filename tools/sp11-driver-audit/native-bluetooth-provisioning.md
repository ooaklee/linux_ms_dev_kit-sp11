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

## Current complete native source candidate

The source-only `sp11-native-dpp-source-20261015-r3` bundle contains the complete
five-commit series over [dtbloader](https://github.com/TravMurav/dtbloader) at
`b1bf23e88b779ac323fbcbe547cf3ba939433df3`. It adds sized DTB inputs and staged
publication (`3489cd54a96f1a1e9397894bef101da220eb3e10`) to the four commits
listed below. The resulting tree is
`86123f3325150775539e3ba3ae16f6a69b873b76`. Apply it to that original base,
not on top of the earlier archives.

The revision-3 archive SHA-256 is
`dea11979cfa914cf12614d37b257dd4cb925f74bd98d62e5c37354fcb98346ab`.
Its 90,397 bytes contain 38 text files: five patch emails, 29 exact source and
licence snapshots, and four metadata files. Two deterministic packs matched
byte-for-byte. Independent fresh Linux extraction checked all 37 checksum
entries and compared all 38 extracted files against the reviewed staging.

Independent Linux replay of all five emails reproduced the exact tree and
preserved raw author identities/dates, complete messages, parsed trailers and
stable patch IDs. All six public test targets passed after replay. Repository
integrity also passed with the complete 117-commit base ancestry; initial
offline integrity checks lacked that ancestry, and their failures are retained
separately from the accepted result. Replay committer metadata may differ.

This is buildable research source, not an installable kernel or a qualified
EFI provider. The bundle contains no compiled image, Windows payload, private
capture or provisioning record. Earlier archives remain unchanged.

## Historical revision-2 reader and matcher series

The source-only `sp11-native-dpp-source-20261015-r2` bundle applies four focused
commits to [dtbloader](https://github.com/TravMurav/dtbloader) at
`b1bf23e88b779ac323fbcbe547cf3ba939433df3`:

1. `587ded14d0f8ada9324bd325bac9faef31696c8f`: isolate the known native RWFS
   fields in a bounded, allocation-free reader; check media extents/identity,
   exact payload lengths and read budgets; remove raw provisioning debug output.
2. `397ee3ca597084d91f1be5c82fe10129e03f43a4`: constrain Bluetooth vendor matching
   to `QCOM` through the existing Surface Pro 11 descriptor, preserving other
   boards' unconstrained vendor policy and the existing WLAN/byte-reversal path.
3. `0d88509c82a3752fa3b9afb982b344612f1dcc31`: honor the native matcher's
   already declared hardware-ID priority order without changing descriptors.
4. `3ba7a4bdfb92bf9869ab3783ccbe06a036dc86ce`: add portable actual-function
   board-matcher tests alongside the existing bounded-reader tests.

Revision 2 is the complete four-patch series from the same base, not an
incremental patch for the original two-patch archive. That earlier archive
remains an immutable historical revision. Replaying the four emails onto the
full exact base on Linux reproduced tree
`a91c60a401d726276d56e51d3e9a971e3c083c48`, with all 15 review snapshots and
original author identities, exact dates, messages, trailers and code patch IDs
preserved. Replay committer metadata need not match the original commits.

The revision-2 archive SHA-256 is
`c6910c329bb9edc553c8ec6eb62d01a847e92eac272596009ddd35a6dada1a57`.
It contains only 23 text files, including 15 source/licence snapshots and four
patch emails. Deterministic packing, exact member-byte comparison and fresh
Linux extraction verified all 22 checksums. Private evidence and compiled
results are not distributed in the archive.

The reader interprets only the licensed native implementation's known fields.
Unknown versions, secondary magic, region ownership and nonzero row states
remain opaque. The first zero-present row ends traversal; duplicates are
rejected within that visited prefix. Row/byte limits are implementation policy,
not assertions about factory-format maxima. No proprietary format header,
Windows binary, recovered pseudocode or real provisioning record is included.
The original BSD-3-Clause licence and contributors' attribution are retained.

## Historical revision-2 checks

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

## Follow-up: declared board-selection priority

The separate native commit `0d88509c82a3752fa3b9afb982b344612f1dcc31`
corrects a pre-existing board matcher issue: use `hwids[priority[i]]`, not
`hwids[i]`. The original loop checked IDs 0 through 8, ignoring the declared
specificity order and omitting IDs 9 through 11. The priority array, device
descriptors, additional match predicates, registration-order ties and
successful-match cache are unchanged. The correction and its portable tests
are included in revision 2, not retroactively added to the original archive.

The exact function passed 191 synthetic scenarios per optimized and
AddressSanitizer/UndefinedBehaviorSanitizer run on both macOS and native ARM64
Linux. Reversing only the indexed expression reproduced the same 75 expected
failures per run. Cases cover every declared priority pair in both registry
orders, sole identities, rejected predicates, ties, missing identities and
cache/error retry behavior. Actual project headers are used, but EFI services,
SMBIOS population and registry boundaries are synthetic; these tests do not
exercise the COFF linker registry or identify a real board.

The public `make -C tests check` and `sanitize` targets reproduce both suites;
select the installed C compiler with `CC`, such as `CC=clang-18`. Python 3 is
needed for test extraction/orchestration only, not the production provider.

The corrected source also completed the full 85-object native ARM64 EFI
compile/link without warnings. OpenCode recommended the focused correction
and these source-level gates, not deployment of the unqualified provider.

## Versioned protocol gate

The inspected dtbloader header and
[Stubble's pinned header](https://github.com/ubuntu/stubble/blob/08218605b5ac014f7064ef7129e84ba7ec582dca/include/proto/dt-fixup.h)
agree on the legacy `e617d64c-fe08-46da-f4dc-bbd5870c7300` interface with four
arguments, including flags. Stubble's official main still had that exact
revision when checked on 15 October 2026. This source agreement does not identify
the provider or consumer installed on a particular device.

The [published protocol specification](https://github.com/U-Boot-EFI/EFI_DT_FIXUP_PROTOCOL/blob/c98cfac7b4daed69bb097a579981b877b765569a/README.rst)
records an incompatible January 2026 change: GUID
`60ed6ba9-dfef-4799-ac7b-75e0f833456c`, three arguments and no flags/reservation
operation. The protocol revision value alone does not distinguish these ABIs.
Do not change only the GUID, cast between signatures or add speculative memory
reservation behavior. Pin the actual installed caller/provider pair before any
separate migration decision. Current source checks preserve the legacy ABI.

## Remaining deployment gates

### Revision-3 sized inputs and staged publication

Native commit `3489cd54a96f1a1e9397894bef101da220eb3e10` adds the sized
input/staging prerequisite over the four-commit source above. It is included
in revision 3, not retroactively added to revision 2 or a firmware release.

The file path now checks a bounded metadata query and exact read, validates
against the bytes actually read and closes file/volume handles. A shared
helper stages the complete existing callback on a private bounded tree, then
validates, packs and validates again before copying the packed extent back.
Failures leave the original buffer unchanged under the documented accessible,
exclusively owned memory/callback contract. Successful calls preserve the
pointer and unused tail. This is not concurrency-atomic or a whole-firmware/
NVRAM transaction, and does not change factory-data or board-selection policy.

The separate native ADR records the 2 MiB AArch64 boot/workspace ceiling,
existing 1 MiB local allocation, legacy protocol ABI/headroom negotiation and
successfully published ACPI-page ownership. Zeroing the local allocation before
reading makes pre-fixup normalized-hash padding deterministic; it does not
introduce authentication or change confirmation/NVRAM policy. Metadata uses a
bounded pool, avoiding a large stack frame and an unavailable stack-probe
helper without adding a Windows runtime dependency.

All six public targets (`check`, `sanitize`, `check-fdt`, `sanitize-fdt`,
`check-io`, `sanitize-io`) passed on ARM64 macOS and native ARM64 Linux. The
optional FDT suite exercises 95 scenarios/795 assertions with real libfdt;
the file-helper suite exercises 2,802 calls with real GNU-EFI types and fake
allocation/file callbacks. Neither target fetches dependencies. Six and eight
malformed source-extraction controls respectively fail closed. The original
standalone core/matcher suites still pass with unchanged test dependencies.

Separate private actual-source fixtures additionally cover loader lifecycle
with 52 scenarios and the historical partial-mutation negative control. These
do not qualify actual filesystem opening, SHA/NVRAM authorization or factory
provisioning. The full native 86-object ARM64 COFF link passed without warnings
from a pre-commit snapshot whose compiled inputs match the final commit;
retained source/object/image bytes were independently audited. The historical
87-scenario adapter fixture predates the utility-header change; it was not
rerun against revision 3 and is not a whole-revision regression claim. OpenCode's
direct GLM 5.1 supplied-source review found no introduced blocker. Its review
and these tests are not proof of firmware ABI, deployment or hardware parity.

### Subsequent file-opening source candidate

Local native commit `1c7b8f75bf6be7efceeb7aea4b8e32fa0b0cf553`, tree
`f5acac88077dcc3c7da01bb64cc4a6632dc68eb5`, is a separate follow-up over
revision 3. It is not contained in that immutable source archive.

`GetVolume()` and `FileOpen()` now return explicit native status and a separate
owned handle. Only exact successful opens publish a non-NULL output. Native
errors propagate; unexpected warnings and success without a required interface
or handle fail closed. Failed-service output pointers are not trusted or closed
as though they were owned. Borrowed protocol interfaces remain borrowed.

The six-path DTB search continues only on a file-open `EFI_NOT_FOUND`, retaining
the original order. Device/access/media failures stop immediately instead of
falling through to a lower-priority file or protocol-only success. Only genuine
exhausted-path absence is optional; lookup, metadata, read and close errors
remain fatal even when their status is also `EFI_NOT_FOUND`. Root-close failure
overrides optional absence. Existing cleanup precedence, normalized-hash order,
staged publication and the legacy protocol interface remain unchanged.

All eight public targets passed on ARM64 macOS and native ARM64 Linux, including
optimized and sanitizer variants. The helper suite now has 2,969 calls; the new
actual-source loader suite has 142 scenarios. It compiles actual GNU-EFI UTF-16
functions and libfdt rather than rewriting the search or tree algorithms.
Eighteen loader extraction controls and two behavioral mutants reject malformed
boundaries and reintroduced error masking. The full current-commit 86-object
native COFF compile/link passed without warnings. OpenCode's direct GLM 5.1
production-source review found no introduced blocker; tests and dependencies
were outside that review's scope and were checked separately.

The new native ADR `0002-explicit-file-opening-status.md` supersedes only the
opening-status limitation in ADR 0001, leaving the earlier record unchanged.
Real EFI services, handle release, SHA/NVRAM authorization and installed
caller/provider behavior remain unqualified. No EFI image was executed or
installed; these shared-loader changes also need non-SP11 firmware testing.

### Still open

The existing GPT-name comparison and broad/ambiguous partition selection still
need hardening. A stable media ID does not authenticate storage or rule out
in-place mutation. WLAN/Bluetooth success remains coupled. In revision 2 the
two FDT updates are sequential and a failed second update leaves the first
applied. Revision 3 isolates those updates in a private tree;
it does not change the callback's address-source or compatible-node policy.
Valid existing addresses, exact compatible-node selection, actual factory
record semantics and the installed Stubble/provider path need independent
qualification. The host adapter tests intentionally reproduce the partial-FDT
failure in the original callback; the revision-3 staged-entry tests verify its
failure-isolation boundary.

The sole current GPT-name caller passes the fixed literal `DPP`; the pinned
native comparison reads at most four units of its 36-unit media name. A current
GPT-name overread has not been demonstrated. Generic API hardening must not be
reported as an observed exploit. Separately, the native board matcher declares
a hardware-ID priority array; the follow-up above corrects its indexed lookup.
Actual SMBIOS generation and device selection still require qualification
before provider deployment.

Revision 3 establishes complete successful reads and validation against
supplied byte extents before staged mutation. A header's `totalsize` still
does not prove pointer accessibility. Its `GetVolume()` and `FileOpen()` still
collapse native errors into missing handles; the missing-file flag separates
failed-open control flow from post-open I/O errors only. The subsequent source
candidate above corrects that classification, not real filesystem/service
qualification. Neither revision-2 nor revision-3 archives contain that new
candidate; revision 2 also lacks revision-3 staged publication.

No raw-storage capture, provider invocation, boot-order/Secure Boot change or
hardware radio test follows from these source checks. Those require separately
scoped decisions after the installed boot path and record ownership are known.
