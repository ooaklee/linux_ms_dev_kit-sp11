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
  compiler warnings or errors. There were 1,308 compiler-warning lines
  elsewhere in the carrier; this was not a warning-free whole-kernel build.
- The effective configuration was unchanged by `olddefconfig`, SHA-256
  `bccba597adeb6475571f813c278ca02e6a478c2134f862cf64e1319ada10bdf7`.
  It enabled both sensor drivers as modules for validation and disabled debug
  information; it was not an identical release configuration/toolchain.
- Its `vmlinux` SHA-256 was
  `83a4abe6b3a00e17c270c6820d88c99e441b1fe196816246cfc656106bf8d31c`.

That complete build did **not** contain the HDR correction added in revision 2.

## Current revision: HDR correction

Revision 2 retains all 23 earlier patch emails byte-for-byte and appends IR
commit `5c961dcbcb4e7742124e402800317f693634ddb4`, for 24 patches total.
The current IR driver SHA-256 is
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

The correction keeps the established 446-line hardware auto-exposure ceiling
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

## Remaining boundaries

No Denali rear/IR graph, physical sensor identity, calibration, runtime
capture, failure recovery, system sleep or existing G1/G4 hardware regression
has been qualified. G0 nonempty `st,leds` is rejected before power resources
are used. Post-firmware GPIO input programming does not prove reset/boot pin
states or physical no-emission, electrical or optical safety.

Inherited live VBLANK/auto-exposure-ceiling synchronization and G1/G4 LED
context behavior require separate source/sensor review. The HDR correction
does not claim to resolve those limitations or qualify illumination.
