# Native camera validation scope

These source-only foundations use the tested-v8 carrier
`8ba2b75713feb5c72bbc11dd8cce4048e23cdeea`. The source bundles include no Debian
packages, boot images, Windows payloads, EFI applications, private captures or
signing keys. No board nodes or shipping configuration are enabled.

## Previous revision: complete kernel and module link

The previous bundle, SHA-256
`ec319fe60543a555c2a34212477ade671ef23765d43e0f8addd526cb3d8fc405`,
contained 2 OV13858 and 21 IR patches. Its IR source ended at
`14bb8959fc37812a23ed55af73a2eb273acdb043`, driver SHA-256
`ee61374030f80665fbbba0497b46010f222c2f97118505be4909d8ffa60bdc5c`.

- A native ARM64 GCC 13.3.0/binutils 2.42 build completed with
  `make ARCH=arm64 W=1 -j4 vmlinux modules`.
- `vmlinux` and all 7,817 modules listed in `modules.order` were present and
  identified as little-endian AArch64 ELF files. The modified drivers had no
  compiler warnings or errors. Warnings elsewhere in the carrier are
  classified in the subsequent-build comparison below; this was not a
  warning-free whole-kernel build.
- The effective configuration was unchanged by `olddefconfig`, SHA-256
  `bccba597adeb6475571f813c278ca02e6a478c2134f862cf64e1319ada10bdf7`.
  It enabled both sensor drivers as modules for validation and disabled debug
  information; it was not an identical release configuration/toolchain.
- Its `vmlinux` SHA-256 was
  `83a4abe6b3a00e17c270c6820d88c99e441b1fe196816246cfc656106bf8d31c`.

That complete build did **not** contain the HDR correction added in revision 2
or the three corrections added in revision 3.

## Revision 2: HDR correction

Revision 2 retains all 23 earlier patch emails byte-for-byte and appends IR
commit `5c961dcbcb4e7742124e402800317f693634ddb4`, for 24 patches total.
That revision's IR driver SHA-256 is
`086ec2feff305a159d93a283c0e303fd04108c798e6c4252d34bfe5e45c9b5b1`.
The other three driver/binding snapshots are unchanged.

An independent replay applied all 24 unmodified emails with `git am` to the
complete original v8 commit and tree on a fresh Linux filesystem, not a
synthetic small-file fixture. All author names/emails, exact author dates and
timezones, complete messages, ordered trailers and code patch IDs were
preserved. The resulting tree was
`b1cd6a5c0b39d1e443177f7ef0d91fdbcf3a4720`, with all four snapshots matching
the manifest and only those paths plus the declared `MAINTAINERS` addition
changed from v8. Index/worktree, whitespace and connectivity checks were clean.

The correction keeps the existing 446-line maximum-register setting
when an HDR-capable model is in HDR subtraction mode and automatic/manual
exposure changes. Other modes retain their prior frame-derived maximum.
Manual register values, register order, frame timing, GPIO policy and the
VD55G0 illumination gate are unchanged.

- Independent tests extracted the actual source functions, rather than a
  rewritten model. The corrected source passed 779/779 cases on both host
  AddressSanitizer/UndefinedBehaviorSanitizer and native ARM64 Linux.
  The unchanged pre-fix source failed 176/779 HDR-sensitive cases on both.
- Cases included G0 with no HDR control, G1/G4 HDR and non-HDR operation,
  control-change flag combinations, live automatic/manual transitions and
  register-resolution/write failures. Stub tests do not exercise actual
  hardware, firmware, V4L2 runtime locking or PM/CCI services.
- A native ARM64 `W=1` object and external test-module link passed without
  warnings or errors against the previous build's read-only prepared kernel,
  configuration and `Module.symvers`. No full kernel rebuild was repeated.
- The focused object SHA-256 was
  `ecdf9dd3d1ab39b32abba7c64876d6bafb6c11e8ba61d01458581ac2e6576be7`;
  the focused test-module SHA-256 was
  `e624a4dfc7fa39486b30362b4517815860b4c09546a0d57ce8fa0eb4eaeffbc6`.
  Those artifacts are not supplied as standalone installation binaries.
- The focused diff passed strict checkpatch and whitespace checks.

## Current revision: format, control lifecycle and G0 exposure writes

Revision 3 retains all 24 revision-2 patch emails byte-for-byte and appends:

- `62ff85ae8cc10d44040b293bea3cf887eff65d09`: reject active format changes
  while the core still owns a stream, including after a failed stop. TRY
  negotiation remains available. The exact source passed 209 synthetic cases
  per host-sanitizer/native-ARM64 platform; these model the surrounding core.
- `ef08c6a681e2cf61feb5aa5189bf51368401b6f0`: initialize the handler before
  choosing the shared state lock, copy initial state before control
  construction, check sticky constructor errors and release state before its
  handler. Stream callbacks use caller-locked grab helpers. The exact source
  passed 298 synthetic lifecycle cases per platform. Separate real-mutex
  cases distinguished the corrected helpers from a deadlocking intermediate;
  they do not constitute full-kernel lockdep or real V4L2 allocator tests.
- `ff99e470cc8ca665580cd26bd072145d853d1ab5`: omit the newly added G0
  maximum-register write and remove its mapping, restoring the native ST G0
  control-write pattern. Mode selection, manual exposure/gains, software
  ranges and G1/G4/VD65G4 register traces remain unchanged.

The current IR driver SHA-256 is
`600064057065a8a7a98951fa016b22dda842c634bf3832a985f2583eb488e1a0`.
The other three source snapshots and six licence/copying files are unchanged.
All 27 emails replayed onto the complete original v8 tree on a Linux filesystem,
with author identities, exact dates, messages, ordered trailers and stable
code patch IDs preserved. Only the four snapshots and declared `MAINTAINERS`
addition differ from the carrier. The manifest records the resulting tree.

The final G0 correction passed 1,059/1,059 exact-function cases on both host
AddressSanitizer/UndefinedBehaviorSanitizer and native ARM64 GCC 13.3. The
unchanged parent failed 76 intended G0-only assertions, not 76 demonstrated
physical defects. All 896 success-matrix traces differ only by deleting the
G0 `0x096c` operation; all 912 non-G0 cases, including injected failures, are
unchanged. The harness uses actual CCI encodings, maps, resolver and write/
control functions, but a stub bus and projected control/model structures.
It proves traces and first-error handling, not register acceptance or AE
efficacy. Missing G0 mapping returns `-EOPNOTSUPP` without a CCI transaction.

A focused native ARM64 `W=1` object/module link for this exact final source
passed without warnings or errors against the read-only prepared kernel,
configuration and `Module.symvers`. The object SHA-256 is
`17859f8fb91b595577ce28fc002fcbbb31f3f51227ccf4555e828be33a5600ab`;
the test-module SHA-256 is
`ad15bc5252eadba578b53c8e2f2c18f0c94b72a9ae2a0be54c9e18b311d8d8cf`.
At bundle creation, revision 3 had only the focused build above; the later
complete build is recorded below. These test artifacts are not standalone
installation modules. The final diff passed strict
checkpatch and whitespace checks; OpenCode GLM 5.1 found no source-level
blocker in the bounded correction.

## Revision 3: subsequent complete kernel and module link

The exact 27-patch replay, tree
`aa48347a793826819475a078eee05759fadc7f26`, subsequently completed a clean
native ARM64 GCC 13.3.0/binutils 2.42 build with
`make ARCH=arm64 W=1 -j4 vmlinux modules`. The same validation configuration
remained unchanged by `olddefconfig`. This includes all four source corrections
added after the earlier complete build, not just the original 23 patches.

Independent read-only checks verified `vmlinux` and all 7,817 ordered modules
as ELF64 little-endian AArch64 files, with unique canonical paths, no object/
module aliases and recorded hashes. The frozen build wrapper had returned a
post-build verification error because it expected `.ko` entries in
`modules.order`. The kernel's own `scripts/Makefile.modfinal` instead maps its
listed `.o` entries to `.ko`. A separate corrected checker verified every
mapped output; neither the build nor its original failed report was replaced.

Both full logs contain 1,307 source-positioned compiler warnings and 2,467 raw
warning-colon lines, including diagnostics without source positions. Neither
modified sensor appears in those warning lines. The ten category counts match.
Review of all added/removed signatures found only typographic versus ASCII
apostrophes; complete warning multisets match after that exact normalization.
This is explicit review of a presentation difference, not a warning-free build
or an assertion that existing carrier warnings are harmless.

The complete-build artifact SHA-256 values are:

- `vmlinux`: `f73d23291d20eca9c5305b61a06923274089364aacd37b5639c53028a8a748ed`.
- `ov13858.ko`: `bb4f5551712351a307e9b5d87bae02d9f22b4e77297a9945676ecd4add35fb69`.
- `vd55g1.ko`: `902d482c62721a8552f8f042adf35602a5417452b441da881e766f72bfa47abd`.

The existing source archive remains immutable. These later checks qualify
compilation/linking of its exact source, not Debian packaging, binding or
`MAINTAINERS` semantics, release-toolchain equivalence, installation, capture,
firmware behavior or physical hardware safety. Generated signing material was
not read, hashed or exported by the audit.

## Scoped binding and example checks

The same exact two binding snapshots subsequently passed direct document
validation, YAML lint and the kernel's binding-style checker using dtschema
2026.9. Each direct check exited successfully without diagnostics. The selected
kernel `dt_binding_check` target generated both example DTBs and all three check
stamps without changing source.

Separate checks confirmed both schema IDs were present in the processed schema
and inspected JSON validation diagnostics, not just command exit status. In
this tool version, `dt-validate` returns zero even for validation errors, and
the kernel example recipe masks that exit with `true`.

Four synthetic examples passed: the original OV13858 and G1 examples, plus G0
without LEDs using 600 MHz and the 375 MHz schema-minimum link frequencies.
Five controls were
rejected by the intended schemas: missing `reg` on either original sensor,
OV13858 at the unsupported 592.8 MHz link frequency, G0 with `st,leds`, and G0
below 375 MHz. These checks cover the stated example constraints, not every possible
tree, driver behavior or physical clock/sensor configuration.

Whole-tree schema aggregation also reported an unrelated ath12k parsing error,
duplicate EC-compatible schemas and a jobserver warning. These remain recorded;
the check is not described as a clean validation of every carrier binding.
No board graph, camera operation, electrical or optical behavior was qualified.

## Remaining boundaries

No Denali rear/IR graph, physical sensor identity, calibration, runtime
capture, failure recovery, system sleep or existing G1/G4 hardware regression
has been qualified. G0 nonempty `st,leds` is rejected before power resources
are used. Post-firmware GPIO input programming does not prove reset/boot pin
states or physical no-emission, electrical or optical safety.

ST's pinned native G0 source, with the same cut1 firmware 2.11 and cut2 0.5
payloads, never directly writes `0x096c`. Public documentation describes that
register as a manual maximum in a standby-configured group; this is not proof
of an AUTO ceiling or a firmware-specific live-write exception. The current
source restores the vendor-native absence of that host operation, not full
vendor-driver equivalence or physical exposure-range validation.

Live VBLANK changes continue to update context frame length and software
exposure ranges. Register latching, full manual range, G1-family live
static-register writes, partial-write recovery and LED context behavior
require separate source/sensor review. No speculative startup-only maximum
write or group-hold transaction is added. None of these changes qualifies
illumination.
